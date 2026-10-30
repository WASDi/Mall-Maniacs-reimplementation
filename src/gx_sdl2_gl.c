#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "gx.h"
#include "gx_sdl2_gl.h"
#include "platform_sdl2.h"
#include "custom_helpers.h"

/* OpenGL 3.3 backend (simple, good-looking port).
 *
 * GX boundary contracts (verified against gxSoft.dll gxDrawPolygon
 * @0x100020e0, gxDrawTriUV @0x100021f0, gxDrawQuad @0x10002540,
 * gxDrawPolyRecords @0x10002670, gxSortPolyRecords, gxFlip @0x10001520,
 * gxRasterTextureTri @0x100050b0, gxSetOrigin @0x10001f90):
 * - Texture handles are opaque ints owned by the backend; pLoadTexture
 *   (mode 0 load) returns a new int handle, freeing is via the wrapper's
 *   data==0 path. pCreateSurface loads an indexed .tpg file into a handle.
 * - 8-bit indexed 256x256 .tpg + 256 RGBA palette expands to RGBA8
 *   at load time. g_nGfxMode==1 R5G6B5 fullscreen (tgaLoad16Pal) converts
 *   directly to RGBA8. No palette-LUT shader.
 * - Vertices are 8.8 fixed-point x/y (/256.0f); nSoftwareMode stays 0 on
 *   this backend. UV bytes map per normUv (/256.0f, GL equivalent of the
 *   GXSOFT corner-minus-half + truncating rasterizer; see normUv note).
 * - GXSOFT textured tris do not supply vertex tint: textured draws are
 *   white-modulated (full texel brightness, matching the original's
 *   colors) and untextured draws use the vertex color. The 0x1000 origin bit needs no GL replication (normUv
 *   folds its effect into the byte/256 edge); other exotic
 *   pSetOrigin bits only gate the backface rule. pDrawTriangle/pDrawLine
 *   stay no-ops (as in GXSOFT).
 * - Ordering: opaque scene triangles are submitted without sorting and use
 *   the depth buffer; scene-transparent triangles retain the far-first stable
 *   painter sort with depth writes disabled, while UI/fullscreen draws follow
 *   in submission order. The per-tri texture rides along because state changes
 *   flush the GL batch as before.
 * - Transparency follows gxSetOrigin @0x10001f90 exactly: textured draws
 *   with origin bit 0x20 use the blend rasterizer (LUT _g_abBlend, 50%
 *   average — every texel contributes, including near-black); with 0x20
 *   clear and 0x4000 set they use the color-key rasterizer (LUT
 *   _g_abColorKey: palette entries with all RGB components < 8 keep the
 *   destination, i.e. near-black texels are transparent); with both
 *   clear they use the opaque direct-copy rasterizer (FUN_10002ab0 —
 *   every texel is drawn, black stays black). Character meshes mix
 *   opaque (e.g. flags 0x1c) and keyed (e.g. 0x401c) prims, so keying
 *   black unconditionally for every textured draw punches transparent
 *   holes into opaque faces (eyes/hair on the charselect preview use
 *   palette index 195 = RGB 0,0,0). The port therefore uploads textures
 *   fully opaque and discards near-black texels in the fragment shader
 *   only for tris queued with the key flag; blend-path tris use vertex
 *   alpha 0.5 to approximate the averaging LUT. gxDrawPolygon always ORs
 *   0xd000 into the origin, so 2D UI quads are always keyed (fonts rely
 *   on it: glyph backgrounds are palette index 194 = RGB 0,0,0).
 *   Palette index 0 itself (RGB 8,8,8 in shipped .tpg files) is NOT
 *   special in the original — it fails the <8 test and stays opaque.
 * - The fullscreen pBlitSurface present runs after the sorted records in
 *   gxFlip (FUN_10002810 after gxDrawPolyRecords), so blit quads are
 *   deferred past the sorted emission as well. pClearScreen only fills
 *   the framebuffer (the record queue survives it); callers always flip
 *   (draining the queue) before clearing, so the GL clear needs no queue flush.
 * - PORT DEVIATION (cross_platform_plan.md Phase 3): perspective-correct
 *   UVs. GXSOFT gxRasterTextureTri @0x100050b0 walks UVs linearly in
 *   screen space (affine, PS1-style warp on oblique floors/walls). The
 *   port instead feeds each GxVert's az depth (z/16) as GL clip w, so the
 *   GPU perspective-divides vUV/vCol. Screen positions are unchanged
 *   (p.xy*w/w); 2D draws (z 0 -> w 1) render exactly as before.
 * - PORT ADDITION (HD look, no GXSOFT counterpart): anisotropic filtering
 *   on the linear/mipmap scene textures, alpha-to-coverage on the
 *   premultiplied color-key path, and an offscreen scene framebuffer with a
 *   post-processing pass (render scale, FXAA, SSAO, bloom, LUT grade,
 *   vignette, depth of field). Only the opaque/transparent scene buckets
 *   render into the offscreen target; UI records and the fullscreen blit
 *   still draw straight to the window after the composite, so 2D screens are
 *   untouched. Every knob is a MANIAC_* environment variable (see
 *   postConfigLoad); MANIAC_POSTFX=0 restores the direct-to-window path.
 */

extern int g_nGfxMode; /* @0x4580c4 */

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#define GL_BATCH_MAX_VERTS 65536

/* Linear depth range in az units: window depth = az / GL_DEPTH_FAR. Mall
 * interiors reach well past 4096 az (the old range clamped everything
 * beyond it to the far plane, where GL_LESS rejected it); with 24-bit
 * linear depth 65536 still resolves ~0.004 az. */
#define GL_DEPTH_FAR_STR "65536.0"

typedef struct GLTexture {
    GLuint nearest;
    GLuint linear;
    GLuint linearKeyed;
} GLTexture;

typedef struct GLVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
    float w; /* clip w for perspective-correct UVs (az depth; 1.0 for 2D) */
} GLVertex;

/* GxVert z is (aspect + worldZ) * 16 (see sceneNodeRender @0x42f8c0); the
 * projected screen pos already divides by that depth, so feeding it back
 * as clip w restores perspective-correct UV interpolation. z <= 0 marks
 * unprojected 2D draws (gxDrawQuadColor @0x414470, fonts, HUD) where every
 * vert shares w = 1 (affine == perspective there). */
static float depthW(int z)
{
    return (z > 0) ? ((float)z / 16.0f) : 1.0f;
}

static GLuint s_prog;
static GLuint s_vao, s_vbo;
static int s_vboBound;
static GLint s_uMVP;
static GLint s_uUseTex = -1;
static GLint s_uKeyBlack = -1;
static GLint s_uKeyAlpha = -1;
static GLint s_uA2C = -1;
static GLVertex s_batch[GL_BATCH_MAX_VERTS];
static int s_batchCount;
static GLuint s_boundTex;
static int s_blendOn;
static int s_keyBlack;
static int s_depthTest;
static int s_depthWrite;
static int s_depthEnabled;
static int s_a2cOn;
/* Fullscreen streaming texture for presentFrame CPU buffers (R5G6B5). */
static GLuint s_streamTex;
static unsigned char *s_rgba;
static unsigned char *s_streamSource;
static int s_streamSourceValid;
static int s_viewX, s_viewY, s_viewW, s_viewH;
static int s_origin;
static int s_width = 640, s_height = 480;
static int s_drawWidth = 640, s_drawHeight = 480;
static int s_outputX, s_outputY, s_outputWidth = 640, s_outputHeight = 480;
static float s_outputScale = 1.0f;

/* int handle (1-based) -> GLuint registry. */
#define GL_TEX_MAX 4096
static GLTexture s_tex[GL_TEX_MAX];
static int s_texCount;
static int s_linearSceneTextures = 1;
static float s_anisotropy = 1.0f; /* applied to linear/mipmap uploads */

/* HD post-processing state (PORT ADDITION, see header note). The scene
 * buckets render into s_postSceneFbo (multisampled when s_postSamples > 1),
 * resolve into color/depth textures, and composite into the window's
 * output rect. Window depth is linear in az (the vertex shader maps
 * az/GL_DEPTH_FAR -> [0,1]), so SSAO/DoF read distances as
 * depth * GL_DEPTH_FAR. */
typedef struct GLPostConfig {
    int enabled;
    float renderScale;    /* offscreen size / window output size */
    int msaa;             /* requested offscreen samples */
    int fxaa;
    float ssao;           /* 0 disables; occlusion strength */
    float ssaoRadius;     /* world units */
    float projScale;      /* view-space x per NDC x per unit depth */
    float bloom;          /* 0 disables; additive strength */
    float bloomThreshold;
    int grade;            /* 3D LUT colour grade */
    float vignette;
    float dof;            /* 0 disables; max circle of confusion (pixels) */
    float dofFocus;       /* az units */
    float dofRange;       /* az units from focus to full blur */
    int alphaToCoverage;
    int debugAo;          /* show the SSAO buffer instead of the scene */
} GLPostConfig;

static GLPostConfig s_post;
static int s_postReady;       /* shaders built; FBOs (re)built lazily */
static int s_sceneTarget;     /* 1 while the offscreen scene FBO is bound */
static int s_windowSamples;
static int s_postSamples;
static int s_postW, s_postH;
static GLuint s_postSceneFbo, s_postSceneColorRb, s_postSceneDepthRb;
static GLuint s_postResolveFbo, s_postColorTex, s_postDepthTex;
static GLuint s_postAoFbo, s_postAoTex;
static GLuint s_postBloomFbo[2], s_postBloomTex[2];
static GLuint s_postLutTex;
static int s_postLutSize;
static GLuint s_postVao;
static GLuint s_progAo, s_progBright, s_progBlur, s_progComposite;

static int postActive(void);
static void postSetTarget(int scene);
static void postRender(void);

static const char *kVS =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "layout(location=2) in vec4 aCol;\n"
    "layout(location=3) in float aW;\n"
    "uniform mat4 uMVP;\n"
    "out vec2 vUV; out vec4 vCol;\n"
    /* NDC xy stay identical (p.xy * w / w), but a varying w lets GL
     * perspective-divide vUV/vCol instead of affine screen-lerping them.
     * w == 1 reproduces the old path exactly (2D UI, fullscreen blit). */
    "void main(){ vec4 p=uMVP*vec4(aPos,0.0,1.0); float depth=clamp((aW/" GL_DEPTH_FAR_STR ")*2.0-1.0,-1.0,1.0); gl_Position=vec4(p.xy*aW,depth*aW,aW); vUV=aUV; vCol=aCol; }\n";
static const char *kFS =
    "#version 330 core\n"
    "in vec2 vUV; in vec4 vCol;\n"
    "uniform sampler2D uTex;\n"
    "uniform int uUseTex;\n"
    "uniform int uKeyBlack;\n"
    "uniform int uKeyAlpha;\n"
    "uniform int uA2C;\n"
    "out vec4 oCol;\n"
    /* Near-black discard replicates the _g_abColorKey LUT built by
     * gxLoadTexture @0x100019b0 (palette RGB all < 8 keeps the
     * destination). Byte-exact threshold 7.5 keeps index 0 (8,8,8)
     * opaque; NEAREST filtering yields exact texel values. Blend-path
     * tris arrive with vCol.a 0.5 to approximate the _g_abBlend
     * averaging LUT (uKeyBlack is 0 there, so black still contributes).
     * PORT ADDITION: with uA2C the premultiplied keyed path sharpens the
     * filtered alpha to a ~1 px ramp around the 0.5 cut and feeds it to
     * GL_SAMPLE_ALPHA_TO_COVERAGE, so cut-out edges (hair, fences, plants)
     * get MSAA-smoothed instead of the hard discard staircase. */
    "void main(){ if(uUseTex!=0){ vec4 t=texture(uTex,vUV); float a=1.0; if(uKeyBlack!=0){ if(uKeyAlpha!=0){ if(uA2C!=0){ a=clamp((t.a-0.5)/max(fwidth(t.a),0.0001)+0.5,0.0,1.0); if(a<=0.0) discard; } else if(t.a<0.5) discard; t.rgb/=max(t.a,0.0001); } else if(t.r*255.0<7.5 && t.g*255.0<7.5 && t.b*255.0<7.5) discard; } oCol=vec4(t.rgb*vCol.rgb,(uKeyBlack!=0?a:t.a)*vCol.a); } else oCol=vCol; }\n";

static GLuint compileShader(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        appLog("[gxGL] shader compile failed: %s", log);
    }
    return sh;
}

/* --- HD post-processing shaders (PORT ADDITION) --- */

/* Fullscreen triangle from gl_VertexID (no vertex buffer). */
static const char *kPostVS =
    "#version 330 core\n"
    "out vec2 vUV;\n"
    "void main(){ vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2)); vUV=p; gl_Position=vec4(p*2.0-1.0,0.0,1.0); }\n";

/* SSAO at half resolution. View position is rebuilt from the linear depth
 * (az = depth * GL_DEPTH_FAR) and the scene projection (sceneNodeRender @0x42f8c0:
 * ndc.x = wx / (az * nWidth), ndc.y = 1.333 * wy / (az * nWidth)), the
 * normal from the smaller of the forward/backward depth differences, and
 * 12 cosine-weighted hemisphere taps rotated per pixel. */
static const char *kPostAoFS =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 oAo;\n"
    "uniform sampler2D uDepth;\n"
    "uniform vec2 uProj; uniform float uRadius; uniform vec2 uDepthTexel;\n"
    "const float FAR=" GL_DEPTH_FAR_STR ";\n"
    "vec3 viewPos(vec2 uv){ float d=texture(uDepth,uv).r*FAR; return vec3((uv*2.0-1.0)*uProj*d,d); }\n"
    "float ign(vec2 p){ return fract(52.9829189*fract(dot(p,vec2(0.06711056,0.00583715)))); }\n"
    "void main(){\n"
    " float d=texture(uDepth,vUV).r;\n"
    " if(d>=1.0||d*FAR<1.5){ oAo=vec4(1.0); return; }\n"
    " vec3 P=viewPos(vUV);\n"
    " vec3 r=viewPos(vUV+vec2(uDepthTexel.x,0.0)), l=viewPos(vUV-vec2(uDepthTexel.x,0.0));\n"
    " vec3 t=viewPos(vUV+vec2(0.0,uDepthTexel.y)), b=viewPos(vUV-vec2(0.0,uDepthTexel.y));\n"
    " vec3 dx=abs(r.z-P.z)<abs(P.z-l.z)?r-P:P-l;\n"
    " vec3 dy=abs(t.z-P.z)<abs(P.z-b.z)?t-P:P-b;\n"
    " vec3 N=normalize(cross(dx,dy)); if(dot(N,P)>0.0) N=-N;\n"
    " vec3 T=normalize(abs(N.x)<0.9?cross(N,vec3(1.0,0.0,0.0)):cross(N,vec3(0.0,1.0,0.0)));\n"
    " vec3 B=cross(N,T);\n"
    " float rnd=ign(gl_FragCoord.xy);\n"
    " float occ=0.0;\n"
    " for(int i=0;i<12;i++){\n"
    "  float f=(float(i)+0.5)/12.0;\n"
    "  float phi=float(i)*2.3999632+rnd*6.2831853;\n"
    "  float s=fract(float(i)*0.618034+rnd);\n"
    "  vec3 dir=(T*cos(phi)+B*sin(phi))*sqrt(f)+N*max(sqrt(1.0-f),0.15);\n"
    "  vec3 S=P+dir*uRadius*mix(0.1,1.0,s*s);\n"
    "  if(S.z<=1.0) continue;\n"
    "  vec2 suv=(S.xy/(S.z*uProj))*0.5+0.5;\n"
    "  float sd=texture(uDepth,suv).r*FAR;\n"
    "  float range=smoothstep(0.0,1.0,uRadius/max(abs(P.z-sd),0.0001));\n"
    "  occ+=(sd<S.z-uRadius*0.03?1.0:0.0)*range;\n"
    " }\n"
    " oAo=vec4(1.0-occ/12.0);\n"
    "}\n";

/* Bloom bright pass (quarter resolution, 4 bilinear taps, soft knee). */
static const char *kPostBrightFS =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 oCol;\n"
    "uniform sampler2D uColor; uniform vec2 uTexel; uniform float uThreshold;\n"
    "void main(){\n"
    " vec3 c=0.25*(texture(uColor,vUV+uTexel*vec2(-1.0,-1.0)).rgb+texture(uColor,vUV+uTexel*vec2(1.0,-1.0)).rgb\n"
    "  +texture(uColor,vUV+uTexel*vec2(-1.0,1.0)).rgb+texture(uColor,vUV+uTexel*vec2(1.0,1.0)).rgb);\n"
    " float br=max(c.r,max(c.g,c.b)); float knee=0.5*uThreshold+0.0001;\n"
    " float soft=clamp(br-uThreshold+knee,0.0,2.0*knee); soft=soft*soft/(4.0*knee);\n"
    " oCol=vec4(c*(max(soft,br-uThreshold)/max(br,0.0001)),1.0);\n"
    "}\n";

/* Separable 9-tap gaussian using 5 linear fetches. */
static const char *kPostBlurFS =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 oCol;\n"
    "uniform sampler2D uSrc; uniform vec2 uDir;\n"
    "void main(){\n"
    " vec3 c=texture(uSrc,vUV).rgb*0.2270270270;\n"
    " c+=(texture(uSrc,vUV+uDir*1.3846153846).rgb+texture(uSrc,vUV-uDir*1.3846153846).rgb)*0.3162162162;\n"
    " c+=(texture(uSrc,vUV+uDir*3.2307692308).rgb+texture(uSrc,vUV-uDir*3.2307692308).rgb)*0.0702702703;\n"
    " oCol=vec4(c,1.0);\n"
    "}\n";

/* Composite into the window output rect: resample (supersampling box or
 * FXAA), depth of field, SSAO, bloom, LUT grade, vignette, dither,
 * blended in by target alpha (scene coverage; backdrop UI leaves it 0).
 * Depth-based effects skip the cleared background (depth 1) and z<=0 2D
 * draws (az 1 -> depth 1/GL_DEPTH_FAR). */
static const char *kPostCompositeFS =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 oCol;\n"
    "uniform sampler2D uColor; uniform sampler2D uDepth; uniform sampler2D uAo;\n"
    "uniform sampler2D uBloom; uniform sampler3D uLut;\n"
    "uniform vec2 uTexel; uniform vec2 uAoTexel; uniform vec2 uOutputSize;\n"
    "uniform float uRenderScale; uniform int uFxaa;\n"
    "uniform float uSsao; uniform float uBloomStrength; uniform int uGrade; uniform float uLutSize;\n"
    "uniform float uVignette; uniform float uDof; uniform float uDofFocus; uniform float uDofRange;\n"
    "uniform int uDebugAo;\n"
    "const float FAR=" GL_DEPTH_FAR_STR ";\n"
    "float luma(vec3 c){ return dot(c,vec3(0.299,0.587,0.114)); }\n"
    "float ign(vec2 p){ return fract(52.9829189*fract(dot(p,vec2(0.06711056,0.00583715)))); }\n"
    "vec3 fxaa(vec2 uv){\n"
    " vec2 t=uTexel;\n"
    " vec3 nw=texture(uColor,uv+vec2(-1.0,-1.0)*t).rgb, ne=texture(uColor,uv+vec2(1.0,-1.0)*t).rgb;\n"
    " vec3 sw=texture(uColor,uv+vec2(-1.0,1.0)*t).rgb, se=texture(uColor,uv+vec2(1.0,1.0)*t).rgb;\n"
    " vec3 m=texture(uColor,uv).rgb;\n"
    " float lNW=luma(nw), lNE=luma(ne), lSW=luma(sw), lSE=luma(se), lM=luma(m);\n"
    " float lMin=min(lM,min(min(lNW,lNE),min(lSW,lSE))), lMax=max(lM,max(max(lNW,lNE),max(lSW,lSE)));\n"
    " vec2 dir=vec2(-((lNW+lNE)-(lSW+lSE)),(lNW+lSW)-(lNE+lSE));\n"
    " float red=max((lNW+lNE+lSW+lSE)*(0.25*0.125),1.0/128.0);\n"
    " float rcp=1.0/(min(abs(dir.x),abs(dir.y))+red);\n"
    " dir=clamp(dir*rcp,vec2(-8.0),vec2(8.0))*t;\n"
    " vec3 a=0.5*(texture(uColor,uv+dir*(1.0/3.0-0.5)).rgb+texture(uColor,uv+dir*(2.0/3.0-0.5)).rgb);\n"
    " vec3 b=a*0.5+0.25*(texture(uColor,uv-dir*0.5).rgb+texture(uColor,uv+dir*0.5).rgb);\n"
    " float lb=luma(b); return (lb<lMin||lb>lMax)?a:b;\n"
    "}\n"
    "vec3 sceneColor(vec2 uv){\n"
    " if(uFxaa!=0) return fxaa(uv);\n"
    " if(uRenderScale>1.01){ vec2 o=0.25*uTexel*uRenderScale;\n"
    "  return 0.25*(texture(uColor,uv+vec2(-o.x,-o.y)).rgb+texture(uColor,uv+vec2(o.x,-o.y)).rgb\n"
    "   +texture(uColor,uv+vec2(-o.x,o.y)).rgb+texture(uColor,uv+vec2(o.x,o.y)).rgb); }\n"
    " return texture(uColor,uv).rgb;\n"
    "}\n"
    "float coc(float dist){ float t=clamp(abs(dist-uDofFocus)/max(uDofRange,1.0),0.0,1.0); return uDof*t*t; }\n"
    "void main(){\n"
    " float dist=texture(uDepth,vUV).r*FAR;\n"
    " bool geo=dist<FAR&&dist>1.5;\n"
    " vec3 c=sceneColor(vUV);\n"
    " vec3 raw=c; float cover=texture(uColor,vUV).a;\n"
    " if(uDof>0.0&&dist>1.5){\n"
    "  float cc=coc(dist);\n"
    "  if(cc>0.5){\n"
    "   vec3 acc=c; float wsum=1.0;\n"
    "   for(int i=0;i<16;i++){\n"
    "    float f=(float(i)+0.5)/16.0; float ang=float(i)*2.3999632;\n"
    "    vec2 suv=vUV+vec2(cos(ang),sin(ang))*sqrt(f)*cc*uTexel*uRenderScale;\n"
    "    float sd=texture(uDepth,suv).r*FAR;\n"
    "    float w=sd<dist?clamp(coc(sd)/cc,0.0,1.0):1.0;\n"
    "    acc+=texture(uColor,suv).rgb*w; wsum+=w;\n"
    "   }\n"
    "   c=acc/wsum;\n"
    "  }\n"
    " }\n"
    " if(geo&&uSsao>0.0){\n"
    "  float ao=texture(uAo,vUV).r, wsum=1.0;\n"
    "  for(int i=0;i<4;i++){\n"
    "   vec2 o=vec2((i&1)==0?-1.0:1.0,(i&2)==0?-1.0:1.0)*uAoTexel;\n"
    "   float sd=texture(uDepth,vUV+o).r*FAR;\n"
    "   float w=exp(-abs(sd-dist)/(dist*0.02+1.0));\n"
    "   ao+=texture(uAo,vUV+o).r*w; wsum+=w;\n"
    "  }\n"
    "  c*=mix(1.0,ao/wsum,uSsao);\n"
    "  if(uDebugAo!=0){ oCol=vec4(vec3(ao/wsum),1.0); return; }\n"
    " }\n"
    " if(uBloomStrength>0.0) c+=texture(uBloom,vUV).rgb*uBloomStrength;\n"
    " c=clamp(c,0.0,1.0);\n"
    " if(uGrade!=0) c=texture(uLut,c*((uLutSize-1.0)/uLutSize)+0.5/uLutSize).rgb;\n"
    " if(uVignette>0.0){ vec2 p=vUV*2.0-1.0; p.x*=uOutputSize.x/uOutputSize.y;\n"
    "  c*=1.0-uVignette*smoothstep(0.5,1.7,length(p)); }\n"
    " c+=(ign(gl_FragCoord.xy)-0.5)/255.0;\n"
    " oCol=vec4(mix(raw,c,cover),1.0);\n"
    "}\n";

static void bindVbo(void)
{
    if (!s_vboBound) {
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        s_vboBound = 1;
    }
}

static void orphanVbo(void)
{
    if (!s_vbo) return;
    bindVbo();
    glBufferData(GL_ARRAY_BUFFER, sizeof(s_batch), NULL, GL_STREAM_DRAW);
}

static void batchFlush(void)
{
    if (s_batchCount == 0 || s_prog == 0) return;
    bindVbo();
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(s_batchCount * sizeof(GLVertex)), s_batch);
    glDrawArrays(GL_TRIANGLES, 0, s_batchCount);
    s_batchCount = 0;
}

static void batchEnsure(int need)
{
    if (s_batchCount + need > GL_BATCH_MAX_VERTS) batchFlush();
}

static void batchTri(float x0, float y0, float u0, float v0, float w0,
                     float x1, float y1, float u1, float v1, float w1,
                     float x2, float y2, float u2, float v2, float w2,
                      float r, float g, float b, float a)
{
    batchEnsure(3);
    s_batch[s_batchCount++] = (GLVertex){x0, y0, u0, v0, r, g, b, a, w0};
    s_batch[s_batchCount++] = (GLVertex){x1, y1, u1, v1, r, g, b, a, w1};
    s_batch[s_batchCount++] = (GLVertex){x2, y2, u2, v2, r, g, b, a, w2};
}

static void useDrawState(GLuint tex, int blend, int key,
                          int depthTest, int depthWrite)
{
    int texChanged = tex != s_boundTex;
    int blendChanged = blend != s_blendOn;
    int keyChanged = key != s_keyBlack;
    int depthTestChanged = depthTest != s_depthTest;
    int depthWriteChanged = depthWrite != s_depthWrite;
    /* Alpha-to-coverage only means something on a multisampled target. */
    int targetSamples = s_sceneTarget ? s_postSamples : s_windowSamples;
    int a2c = s_post.alphaToCoverage && tex && key == 2 && targetSamples > 1;
    int a2cChanged = a2c != s_a2cOn;
    if (!texChanged && !blendChanged && !keyChanged &&
        !depthTestChanged && !depthWriteChanged && !a2cChanged) return;
    batchFlush();
    if (a2cChanged) {
        s_a2cOn = a2c;
        if (a2c) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        glUniform1i(s_uA2C, a2c);
    }
    if (texChanged) {
        s_boundTex = tex;
        glBindTexture(GL_TEXTURE_2D, tex ? tex : 0);
        glUniform1i(s_uUseTex, tex ? 1 : 0);
    }
    if (texChanged || keyChanged) {
        s_keyBlack = key;
        glUniform1i(s_uKeyBlack, (tex && key) ? 1 : 0);
        glUniform1i(s_uKeyAlpha, (tex && key == 2) ? 1 : 0);
    }
    if (blendChanged) {
        s_blendOn = blend;
        if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    }
    if (depthTestChanged) {
        s_depthTest = depthTest;
        if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    }
    if (depthWriteChanged) {
        s_depthWrite = depthWrite;
        glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
    }
}

/* Deferred painter queues: opaque scene records are emitted in submission
 * order, transparent scene records are far-first sorted, and UI records keep
 * submission order. */
typedef struct QueuedTri {
    GLVertex v[3];
    GLuint tex;
    int zsum;
    unsigned seq;
    int blend; /* GL blend enable at emission (blend rasterizer path) */
    int key;   /* near-black discard at emission (color-key path) */
} QueuedTri;

typedef struct QueueBucket {
    QueuedTri *data;
    size_t count;
    size_t cap;
} QueueBucket;

static QueueBucket s_opaqueQueue;
static QueueBucket s_transparentQueue;
static QueueBucket s_uiQueue;
static QueueBucket s_legacyQueue;
static unsigned s_qSeq;
static int s_transparentOrdered = 1;
static int s_legacyOrdered = 1;
static int s_pendingBlit; /* fullscreen present deferred past sorted emission */

static void queueTri(float x0, float y0, float u0, float v0, float w0,
                     float x1, float y1, float u1, float v1, float w1,
                     float x2, float y2, float u2, float v2, float w2,
                     float r0, float g0, float b0,
                     float r1, float g1, float b1,
                     float r2, float g2, float b2, float a,
                     int scene, GLuint tex, int zsum, int blend, int key)
{
    QueuedTri q;
    QueueBucket *bucket;
    if (!s_depthEnabled)
        bucket = &s_legacyQueue;
    else if (scene && (blend || key))
        bucket = &s_transparentQueue;
    else if (scene)
        bucket = &s_opaqueQueue;
    else
        bucket = &s_uiQueue;
    if (bucket->count >= bucket->cap) {
        size_t ncap = bucket->cap ? bucket->cap * 2 : 4096;
        QueuedTri *nq = (QueuedTri *)realloc(bucket->data,
                                             ncap * sizeof(QueuedTri));
        if (!nq) return;
        bucket->data = nq;
        bucket->cap = ncap;
    }
    q.v[0] = (GLVertex){x0, y0, u0, v0, r0, g0, b0, a, w0};
    q.v[1] = (GLVertex){x1, y1, u1, v1, r1, g1, b1, a, w1};
    q.v[2] = (GLVertex){x2, y2, u2, v2, r2, g2, b2, a, w2};
    q.tex = tex;
    q.zsum = zsum;
    q.seq = s_qSeq++;
    q.blend = blend;
    q.key = key;
    if (bucket == &s_transparentQueue && bucket->count > 0 &&
        q.zsum > bucket->data[bucket->count - 1].zsum)
        s_transparentOrdered = 0;
    if (bucket == &s_legacyQueue && bucket->count > 0 &&
        q.zsum > bucket->data[bucket->count - 1].zsum)
        s_legacyOrdered = 0;
    bucket->data[bucket->count++] = q;
}

static int queueTriCmp(const void *a, const void *b)
{
    const QueuedTri *qa = (const QueuedTri *)a, *qb = (const QueuedTri *)b;
    if (qa->zsum != qb->zsum) return (qa->zsum > qb->zsum) ? -1 : 1; /* far first */
    if (qa->seq != qb->seq) return (qa->seq < qb->seq) ? -1 : 1;     /* stable */
    return 0;
}

/* Sort + emit the deferred queue (gxSortPolyRecords + gxDrawPolyRecords),
 * then the deferred fullscreen blit (FUN_10002810 runs after the records
 * in gxFlip), then swap. */
static void queueEmitBucket(QueueBucket *bucket, int depthTest, int depthWrite,
                            int keyedDepthWrite)
{
    for (size_t i = 0; i < bucket->count; i++) {
        QueuedTri *q = &bucket->data[i];
        int write = depthWrite || (keyedDepthWrite && q->key && !q->blend);
        useDrawState(q->tex, q->blend, q->key, depthTest, write);
        batchEnsure(3);
        s_batch[s_batchCount++] = q->v[0];
        s_batch[s_batchCount++] = q->v[1];
        s_batch[s_batchCount++] = q->v[2];
    }
}

/* UI records split around the scene: in the GXSOFT painter sort they share
 * one far-first order with the scene tris, so a UI record farther than every
 * scene tri (e.g. the charselect backdrop tiles at z 500000) is painted
 * before the scene; everything else follows it in submission order. */
static void queueEmitUi(int behind, int sceneZsumMax)
{
    for (size_t i = 0; i < s_uiQueue.count; i++) {
        QueuedTri *q = &s_uiQueue.data[i];
        if ((q->zsum > sceneZsumMax) != behind) continue;
        useDrawState(q->tex, q->blend, q->key, 0, 0);
        batchEnsure(3);
        s_batch[s_batchCount++] = q->v[0];
        s_batch[s_batchCount++] = q->v[1];
        s_batch[s_batchCount++] = q->v[2];
    }
}

static void queueEmitSorted(void)
{
    int sceneZsumMax = 0x7fffffff;
    if (!s_depthEnabled) {
        if (s_legacyQueue.count > 1 && !s_legacyOrdered)
            qsort(s_legacyQueue.data, s_legacyQueue.count,
                  sizeof(QueuedTri), queueTriCmp);
        queueEmitBucket(&s_legacyQueue, 0, 0, 0);
        s_legacyQueue.count = 0;
        s_legacyOrdered = 1;
        return;
    }
    if (s_transparentQueue.count > 1 && !s_transparentOrdered)
        qsort(s_transparentQueue.data, s_transparentQueue.count,
              sizeof(QueuedTri), queueTriCmp);
    if (s_opaqueQueue.count || s_transparentQueue.count) {
        sceneZsumMax = -0x7fffffff;
        for (size_t i = 0; i < s_opaqueQueue.count; i++)
            if (s_opaqueQueue.data[i].zsum > sceneZsumMax)
                sceneZsumMax = s_opaqueQueue.data[i].zsum;
        if (s_transparentQueue.count && s_transparentQueue.data[0].zsum > sceneZsumMax)
            sceneZsumMax = s_transparentQueue.data[0].zsum; /* sorted far-first */
    }
    if (postActive() && (s_opaqueQueue.count || s_transparentQueue.count)) {
        /* Scene buckets go to the offscreen target. Color-keyed (not
         * blended) tris also write depth there: the key is binary, and the
         * post pass needs their depth so SSAO/DoF follow the cut-out
         * surface rather than whatever is behind it. Backdrop UI goes in
         * first with alpha masked, so target alpha stays scene coverage
         * and the composite leaves pure-2D pixels ungraded. */
        postSetTarget(1);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        queueEmitUi(1, sceneZsumMax);
        batchFlush();
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        queueEmitBucket(&s_opaqueQueue, 1, 1, 0);
        queueEmitBucket(&s_transparentQueue, 1, 0, 1);
        batchFlush();
        postRender();
    } else {
        queueEmitUi(1, sceneZsumMax);
        queueEmitBucket(&s_opaqueQueue, 1, 1, 0);
        queueEmitBucket(&s_transparentQueue, 1, 0, 0);
    }
    queueEmitUi(0, sceneZsumMax);
    s_opaqueQueue.count = 0;
    s_transparentQueue.count = 0;
    s_uiQueue.count = 0;
    s_transparentOrdered = 1;
}

/* Fixed 8.8 -> pixels; UV 8.8 -> 0..1 (texture is 256x256). */
static float fx8(int v) { return (float)v / 256.0f; }

/* Texel byte (0..255) -> normalized UV. GXSOFT gxDrawTriUV @0x100021f0
 * records the corner float (byte - 0.5) when origin bit 0x1000 is set and
 * the rasterizer truncates the 16.16 fixed-point walk (gxRasterTextureTri
 * @0x100050b0 / gxRasterTextureSpan @0x10006419 use __ftol + >>0x10), so the
 * fetched texel is floor(byte - 0.5 + t) = byte for interior pixels (the
 * first pixel center sits 0.5px in from the corner). A GL NEAREST sampler
 * instead resolves the pixel-center UV to the nearest texel center, so the
 * equivalent edge is byte/256: the pixel center then lands at (byte+0.5)/256
 * (the texel center, stable) rather than exactly on the byte-0.5 boundary
 * where NEAREST rounds unpredictably into the neighbor strip. The previous
 * (byte - 0.5)/256 mapping put 1:1 UI quads (e.g. the levelselect header
 * strip, 51px showing 51 texels) exactly on the boundary, leaking one row
 * of the adjacent strip as a white line above the aqua header. The half
 * flag is therefore intentionally ignored (kept as a parameter for the
 * call sites); both paths use byte/256. */
static float normUv(int b, int half)
{
    (void)half;
    return (float)b / 256.0f;
}

static int outsideViewport(const GxVert *v0, const GxVert *v1,
                           const GxVert *v2, const GxVert *v3)
{
    int minX = v0->x;
    int maxX = v0->x;
    int minY = v0->y;
    int maxY = v0->y;
    if (v1) {
        if (v1->x < minX) minX = v1->x;
        if (v1->x > maxX) maxX = v1->x;
        if (v1->y < minY) minY = v1->y;
        if (v1->y > maxY) maxY = v1->y;
    }
    if (v2) {
        if (v2->x < minX) minX = v2->x;
        if (v2->x > maxX) maxX = v2->x;
        if (v2->y < minY) minY = v2->y;
        if (v2->y > maxY) maxY = v2->y;
    }
    if (v3) {
        if (v3->x < minX) minX = v3->x;
        if (v3->x > maxX) maxX = v3->x;
        if (v3->y < minY) minY = v3->y;
        if (v3->y > maxY) maxY = v3->y;
    }
    if (s_viewW <= 0 || s_viewH <= 0) return 1;
    return maxX < s_viewX * 256 ||
           minX > (s_viewX + s_viewW) * 256 ||
           maxY < (s_height - s_viewY - s_viewH) * 256 ||
           minY > (s_height - s_viewY) * 256;
}

static int texAlloc(GLTexture texture)
{
    if (s_texCount >= GL_TEX_MAX) return 0;
    s_tex[s_texCount] = texture;
    return ++s_texCount; /* 1-based handle */
}

static GLuint texLookup(int handle, int keyed, int *keyMode)
{
    GLTexture *texture;
    if (keyMode) *keyMode = keyed ? 1 : 0;
    if (handle <= 0 || handle > s_texCount) return 0;
    texture = &s_tex[handle - 1];
    if (s_linearSceneTextures) {
        if (keyed && texture->linearKeyed) {
            if (keyMode) *keyMode = 2;
            return texture->linearKeyed;
        }
        if (!keyed && texture->linear) return texture->linear;
    }
    return texture->nearest;
}

static GLuint uploadTexture(const unsigned char *rgba, int linear)
{
    GLuint tex;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, linear ? 8 : 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (linear) {
        glGenerateMipmap(GL_TEXTURE_2D);
        /* Floors and long walls are seen at grazing angles; anisotropic
         * filtering keeps them sharp where trilinear alone blurs. */
        if (s_anisotropy > 1.0f)
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                            s_anisotropy);
    }
    return tex;
}

/* --- HD post-processing (PORT ADDITION, see header note) --- */

static void glApplyViewport(void);

static float envFloat(const char *name, float def)
{
    const char *value = getenv(name);
    char *end;
    double parsed;
    if (!value || !*value) return def;
    parsed = strtod(value, &end);
    return (end == value) ? def : (float)parsed;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Defaults keep the original's colors (no grade, no vignette) and add only
 * edge/depth cues; distances are az world units (window depth * GL_DEPTH_FAR). MANIAC_POSTFX=0 bypasses the offscreen
 * target entirely; each effect is disabled by setting its strength to 0. */
static void postConfigLoad(void)
{
    s_post.enabled = envFloat("MANIAC_POSTFX", 1.0f) != 0.0f;
    s_post.renderScale = clampf(envFloat("MANIAC_RENDER_SCALE", 1.0f), 0.25f, 4.0f);
    s_post.msaa = (int)envFloat("MANIAC_MSAA", 4.0f);
    s_post.fxaa = envFloat("MANIAC_FXAA", 0.0f) != 0.0f;
    s_post.ssao = clampf(envFloat("MANIAC_SSAO", 0.3f), 0.0f, 1.0f);
    s_post.ssaoRadius = envFloat("MANIAC_SSAO_RADIUS", 350.0f);
    s_post.projScale = envFloat("MANIAC_SSAO_PROJ", 1.0f);
    s_post.bloom = envFloat("MANIAC_BLOOM", 0.25f);
    s_post.bloomThreshold = envFloat("MANIAC_BLOOM_THRESHOLD", 0.75f);
    /* Grading is opt-in; naming a .cube LUT turns it on. */
    s_post.grade = envFloat("MANIAC_GRADE", getenv("MANIAC_GRADE_LUT") ? 1.0f : 0.0f) != 0.0f;
    s_post.vignette = clampf(envFloat("MANIAC_VIGNETTE", 0.0f), 0.0f, 1.0f);
    s_post.dof = envFloat("MANIAC_DOF", 0.0f);
    s_post.dofFocus = envFloat("MANIAC_DOF_FOCUS", 400.0f);
    s_post.dofRange = envFloat("MANIAC_DOF_RANGE", 2400.0f);
    s_post.alphaToCoverage = envFloat("MANIAC_ALPHA_TO_COVERAGE", 1.0f) != 0.0f;
    s_post.debugAo = envFloat("MANIAC_SSAO_DEBUG", 0.0f) != 0.0f;
}

/* Adobe .cube 3D LUT (LUT_3D_SIZE n, then n^3 "r g b" rows, red fastest —
 * the same layout as a GL 3D texture). */
static float *postLoadCube(const char *path, int *outSize)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int n = 0, count = 0;
    float *data = NULL;
    if (!f) {
        appLog("[gxGL] grade LUT %s not found; using built-in grade", path);
        return NULL;
    }
    while (fgets(line, sizeof(line), f)) {
        float r, g, b;
        if (line[0] == '#') continue;
        if (!data && sscanf(line, "LUT_3D_SIZE %d", &n) == 1) {
            if (n < 2 || n > 128) break;
            data = (float *)malloc((size_t)n * n * n * 3 * sizeof(float));
            if (!data) break;
            continue;
        }
        if (data && count < n * n * n &&
            sscanf(line, "%f %f %f", &r, &g, &b) == 3) {
            data[count * 3 + 0] = r;
            data[count * 3 + 1] = g;
            data[count * 3 + 2] = b;
            count++;
        }
    }
    fclose(f);
    if (!data || count != n * n * n) {
        appLog("[gxGL] grade LUT %s is not a valid .cube; using built-in grade", path);
        free(data);
        return NULL;
    }
    *outSize = n;
    return data;
}

/* Built-in light grade: +10% saturation, a gentle S-curve, slightly warm
 * highlights and cool shadows. */
static float *postBuildGrade(int n)
{
    float *data = (float *)malloc((size_t)n * n * n * 3 * sizeof(float));
    if (!data) return NULL;
    for (int b = 0; b < n; b++)
        for (int g = 0; g < n; g++)
            for (int r = 0; r < n; r++) {
                float c[3] = {(float)r / (n - 1), (float)g / (n - 1), (float)b / (n - 1)};
                float luma = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
                float *out = &data[((b * n + g) * n + r) * 3];
                for (int k = 0; k < 3; k++) {
                    float v = luma + (c[k] - luma) * 1.10f;
                    v = clampf(v, 0.0f, 1.0f);
                    v += 0.30f * (v * v * (3.0f - 2.0f * v) - v);
                    out[k] = v;
                }
                out[0] += 0.020f * luma;
                out[2] -= 0.015f * luma;
                out[2] += 0.015f * (1.0f - luma) * (1.0f - luma);
                for (int k = 0; k < 3; k++) out[k] = clampf(out[k], 0.0f, 1.0f);
            }
    return data;
}

static void postBuildLut(void)
{
    const char *path = getenv("MANIAC_GRADE_LUT");
    int n = 0;
    float *data = NULL;
    if (path && *path) data = postLoadCube(path, &n);
    if (!data) {
        n = 16;
        data = postBuildGrade(n);
    }
    if (!data) return;
    glGenTextures(1, &s_postLutTex);
    glBindTexture(GL_TEXTURE_3D, s_postLutTex);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB16F, n, n, n, 0, GL_RGB, GL_FLOAT, data);
    glBindTexture(GL_TEXTURE_3D, 0);
    s_postLutSize = n;
    free(data);
}

static GLuint postLinkProgram(const char *fsSrc, const char *name)
{
    GLuint vs = compileShader(GL_VERTEX_SHADER, kPostVS);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    GLuint prog = glCreateProgram();
    GLint ok = 0;
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        appLog("[gxGL] post shader %s failed to link: %s", name, log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static void postSampler(GLuint prog, const char *name, int unit)
{
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, name), unit);
}

static GLuint postMakeTex(GLenum internal, GLenum format, GLenum type,
                          int w, int h, GLint filter)
{
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, NULL);
    return tex;
}

static GLuint postMakeFbo(GLuint color, GLuint depth, int *ok)
{
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, color, 0);
    if (depth)
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, depth, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        *ok = 0;
    return fbo;
}

static void postRelease(void)
{
    GLuint textures[] = {s_postColorTex, s_postDepthTex, s_postAoTex,
                         s_postBloomTex[0], s_postBloomTex[1]};
    GLuint fbos[] = {s_postResolveFbo, s_postAoFbo,
                     s_postBloomFbo[0], s_postBloomFbo[1]};
    GLuint rbs[] = {s_postSceneColorRb, s_postSceneDepthRb};
    if (s_postSceneFbo && s_postSceneFbo != s_postResolveFbo)
        glDeleteFramebuffers(1, &s_postSceneFbo);
    glDeleteFramebuffers(4, fbos);
    glDeleteRenderbuffers(2, rbs);
    glDeleteTextures(5, textures);
    s_postSceneFbo = s_postResolveFbo = s_postAoFbo = 0;
    s_postBloomFbo[0] = s_postBloomFbo[1] = 0;
    s_postSceneColorRb = s_postSceneDepthRb = 0;
    s_postColorTex = s_postDepthTex = s_postAoTex = 0;
    s_postBloomTex[0] = s_postBloomTex[1] = 0;
    s_postW = s_postH = 0;
}

/* (Re)build the offscreen targets for the current output rect. */
static void postResize(void)
{
    int w, h, ok = 1;
    GLint maxSize = 0;
    if (!s_postReady) return;
    w = (int)(s_outputWidth * s_post.renderScale + 0.5f);
    h = (int)(s_outputHeight * s_post.renderScale + 0.5f);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxSize);
    if (maxSize > 0) {
        if (w > maxSize) w = maxSize;
        if (h > maxSize) h = maxSize;
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w == s_postW && h == s_postH && s_postResolveFbo) return;
    batchFlush();
    postRelease();
    s_postW = w;
    s_postH = h;
    s_postColorTex = postMakeTex(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, w, h, GL_LINEAR);
    s_postDepthTex = postMakeTex(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT,
                                 GL_UNSIGNED_INT, w, h, GL_NEAREST);
    s_postResolveFbo = postMakeFbo(s_postColorTex, s_postDepthTex, &ok);
    if (s_postSamples > 1) {
        glGenRenderbuffers(1, &s_postSceneColorRb);
        glBindRenderbuffer(GL_RENDERBUFFER, s_postSceneColorRb);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, s_postSamples, GL_RGBA8, w, h);
        glGenRenderbuffers(1, &s_postSceneDepthRb);
        glBindRenderbuffer(GL_RENDERBUFFER, s_postSceneDepthRb);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, s_postSamples,
                                         GL_DEPTH_COMPONENT24, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glGenFramebuffers(1, &s_postSceneFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, s_postSceneFbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, s_postSceneColorRb);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, s_postSceneDepthRb);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            ok = 0;
    } else {
        s_postSceneFbo = s_postResolveFbo;
    }
    s_postAoTex = postMakeTex(GL_R8, GL_RED, GL_UNSIGNED_BYTE,
                              (w + 1) / 2, (h + 1) / 2, GL_LINEAR);
    s_postAoFbo = postMakeFbo(s_postAoTex, 0, &ok);
    for (int i = 0; i < 2; i++) {
        s_postBloomTex[i] = postMakeTex(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE,
                                        (w + 3) / 4, (h + 3) / 4, GL_LINEAR);
        s_postBloomFbo[i] = postMakeFbo(s_postBloomTex[i], 0, &ok);
    }
    if (ok) {
        glBindFramebuffer(GL_FRAMEBUFFER, s_postSceneFbo);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 0);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        s_depthWrite = 1;
        appLog("[gxGL] post target %dx%d (%d samples)", w, h, s_postSamples);
    } else {
        appLog("[gxGL] post framebuffers incomplete; post-processing disabled");
        postRelease();
        s_postReady = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    s_sceneTarget = 0;
    /* Manual binds above: force the next useDrawState to rebind. */
    s_boundTex = 0xFFFFFFFFu;
}

static int postActive(void)
{
    return s_postReady && s_post.enabled && s_depthEnabled && s_postResolveFbo;
}

static void postSetTarget(int scene)
{
    batchFlush();
    s_sceneTarget = scene;
    glBindFramebuffer(GL_FRAMEBUFFER, scene ? s_postSceneFbo : 0);
    glApplyViewport();
}

static void postInit(void)
{
    GLint maxSamples = 0, ext = 0;
    float maxAniso = 1.0f;
    postConfigLoad();
    glGetIntegerv(GL_SAMPLES, &s_windowSamples);
    /* Anisotropic filtering: core in GL 4.6, otherwise EXT/ARB extension. */
    glGetIntegerv(GL_NUM_EXTENSIONS, &ext);
    for (GLint i = 0; i < ext; i++) {
        const char *name = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (name && (strcmp(name, "GL_EXT_texture_filter_anisotropic") == 0 ||
                     strcmp(name, "GL_ARB_texture_filter_anisotropic") == 0)) {
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
            break;
        }
    }
    s_anisotropy = clampf(envFloat("MANIAC_ANISOTROPY", 16.0f), 1.0f, maxAniso);
    appLog("[gxGL] anisotropic filtering %.0fx (max %.0fx)", s_anisotropy, maxAniso);

    s_postReady = 0;
    s_postW = s_postH = 0;
    s_sceneTarget = 0;
    if (!s_post.enabled || !s_depthEnabled) {
        appLog("[gxGL] post-processing off (enabled %d, depth %d)", s_post.enabled, s_depthEnabled);
        return;
    }
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    s_postSamples = s_post.msaa > 1 ? s_post.msaa : 0;
    if (s_postSamples > maxSamples) s_postSamples = maxSamples;
    s_progAo = postLinkProgram(kPostAoFS, "ssao");
    s_progBright = postLinkProgram(kPostBrightFS, "bright");
    s_progBlur = postLinkProgram(kPostBlurFS, "blur");
    s_progComposite = postLinkProgram(kPostCompositeFS, "composite");
    if (!s_progAo || !s_progBright || !s_progBlur || !s_progComposite) return;
    postSampler(s_progAo, "uDepth", 1);
    postSampler(s_progBright, "uColor", 0);
    postSampler(s_progBlur, "uSrc", 3);
    postSampler(s_progComposite, "uColor", 0);
    postSampler(s_progComposite, "uDepth", 1);
    postSampler(s_progComposite, "uAo", 2);
    postSampler(s_progComposite, "uBloom", 3);
    postSampler(s_progComposite, "uLut", 4);
    postBuildLut();
    glGenVertexArrays(1, &s_postVao);
    s_postReady = 1;
    appLog("[gxGL] post-processing on (scale %.2f, fxaa %d, ssao %.2f, "
           "bloom %.2f, grade %d, vignette %.2f, dof %.1f, a2c %d)",
           s_post.renderScale, s_post.fxaa, s_post.ssao,
           s_post.bloom, s_post.grade && s_postLutTex, s_post.vignette,
           s_post.dof, s_post.alphaToCoverage);
}

static void postShutdown(void)
{
    postRelease();
    if (s_progAo) glDeleteProgram(s_progAo);
    if (s_progBright) glDeleteProgram(s_progBright);
    if (s_progBlur) glDeleteProgram(s_progBlur);
    if (s_progComposite) glDeleteProgram(s_progComposite);
    if (s_postLutTex) glDeleteTextures(1, &s_postLutTex);
    if (s_postVao) glDeleteVertexArrays(1, &s_postVao);
    s_progAo = s_progBright = s_progBlur = s_progComposite = 0;
    s_postLutTex = 0;
    s_postVao = 0;
    s_postReady = 0;
    s_sceneTarget = 0;
}

static void postUniform1f(GLuint prog, const char *name, float v)
{
    glUniform1f(glGetUniformLocation(prog, name), v);
}

static void postUniform2f(GLuint prog, const char *name, float x, float y)
{
    glUniform2f(glGetUniformLocation(prog, name), x, y);
}

/* Resolve the scene target and composite it into the window output rect;
 * leaves the window bound with the GX draw state invalidated. */
static void postRender(void)
{
    int aoW = (s_postW + 1) / 2, aoH = (s_postH + 1) / 2;
    int bloomW = (s_postW + 3) / 4, bloomH = (s_postH + 3) / 4;
    int ssaoOn = s_post.ssao > 0.0f;
    int bloomOn = s_post.bloom > 0.0f;
    GLuint prog;

    glDisable(GL_SCISSOR_TEST); /* blits and fullscreen passes ignore the GX viewport */
    if (s_postSamples > 1) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_postSceneFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_postResolveFbo);
        glBlitFramebuffer(0, 0, s_postW, s_postH, 0, 0, s_postW, s_postH,
                          GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glBindVertexArray(s_postVao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_postColorTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_postDepthTex);

    if (ssaoOn) {
        glBindFramebuffer(GL_FRAMEBUFFER, s_postAoFbo);
        glViewport(0, 0, aoW, aoH);
        glUseProgram(s_progAo);
        /* sceneNodeRender: ndc.x = wx / (az * nWidth), ndc.y carries the
         * 1.333 Y scale, so view y spans 0.75 of view x per NDC unit. */
        postUniform2f(s_progAo, "uProj", s_post.projScale, s_post.projScale * 0.75f);
        postUniform1f(s_progAo, "uRadius", s_post.ssaoRadius);
        postUniform2f(s_progAo, "uDepthTexel", 1.0f / s_postW, 1.0f / s_postH);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, s_postAoTex);

    if (bloomOn) {
        glBindFramebuffer(GL_FRAMEBUFFER, s_postBloomFbo[0]);
        glViewport(0, 0, bloomW, bloomH);
        glUseProgram(s_progBright);
        postUniform2f(s_progBright, "uTexel", 1.0f / s_postW, 1.0f / s_postH);
        postUniform1f(s_progBright, "uThreshold", s_post.bloomThreshold);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glUseProgram(s_progBlur);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, s_postBloomTex[0]);
        glBindFramebuffer(GL_FRAMEBUFFER, s_postBloomFbo[1]);
        postUniform2f(s_progBlur, "uDir", 1.0f / bloomW, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindTexture(GL_TEXTURE_2D, s_postBloomTex[1]);
        glBindFramebuffer(GL_FRAMEBUFFER, s_postBloomFbo[0]);
        postUniform2f(s_progBlur, "uDir", 0.0f, 1.0f / bloomH);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, s_postBloomTex[0]);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_3D, s_postLutTex);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(s_outputX, s_outputY, s_outputWidth, s_outputHeight);
    prog = s_progComposite;
    glUseProgram(prog);
    postUniform2f(prog, "uTexel", 1.0f / s_postW, 1.0f / s_postH);
    postUniform2f(prog, "uAoTexel", 1.0f / aoW, 1.0f / aoH);
    postUniform2f(prog, "uOutputSize", (float)s_outputWidth, (float)s_outputHeight);
    postUniform1f(prog, "uRenderScale", (float)s_postW / (float)s_outputWidth);
    glUniform1i(glGetUniformLocation(prog, "uFxaa"), s_post.fxaa);
    postUniform1f(prog, "uSsao", ssaoOn ? s_post.ssao : 0.0f);
    postUniform1f(prog, "uBloomStrength", bloomOn ? s_post.bloom : 0.0f);
    glUniform1i(glGetUniformLocation(prog, "uGrade"), s_post.grade && s_postLutTex);
    postUniform1f(prog, "uLutSize", (float)s_postLutSize);
    postUniform1f(prog, "uVignette", s_post.vignette);
    postUniform1f(prog, "uDof", s_post.dof);
    postUniform1f(prog, "uDofFocus", s_post.dofFocus);
    postUniform1f(prog, "uDofRange", s_post.dofRange);
    glUniform1i(glGetUniformLocation(prog, "uDebugAo"), s_post.debugAo);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    /* Back to the GX batch pipeline on the window target. */
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(s_prog);
    glBindVertexArray(s_vao);
    s_sceneTarget = 0;
    glEnable(GL_SCISSOR_TEST);
    glApplyViewport();
    s_boundTex = 0xFFFFFFFFu;
    s_blendOn = -1;
    s_keyBlack = -1;
    s_depthTest = -1;
    s_depthWrite = -1;
    s_a2cOn = -1; /* GL enable was cleared above; resync the uA2C uniform */
}

void gxGLSetDepthOfField(float strength, float focus, float range)
{
    s_post.dof = strength;
    s_post.dofFocus = focus;
    s_post.dofRange = range;
}

static void glApplyViewport(void)
{
    /* The window target letterboxes the logical canvas into the output
     * rect; the offscreen scene target is exactly that rect at its own
     * (render-scaled) size. */
    int originX = s_sceneTarget ? 0 : s_outputX;
    int originY = s_sceneTarget ? 0 : s_outputY;
    float scaleX = s_sceneTarget ? (float)s_postW / (float)s_width : s_outputScale;
    float scaleY = s_sceneTarget ? (float)s_postH / (float)s_height : s_outputScale;
    int left = originX + (int)floorf(s_viewX * scaleX);
    int bottom = originY + (int)floorf(s_viewY * scaleY);
    int right = originX + (int)ceilf((s_viewX + s_viewW) * scaleX);
    int top = originY + (int)ceilf((s_viewY + s_viewH) * scaleY);
    glViewport(left, bottom, right - left, top - bottom);
    glScissor(left, bottom, right - left, top - bottom);
}

static void glUpdateDrawableSize(void)
{
    SDL_Window *window = (SDL_Window *)platformWindow();
    int width, height;
    float scaleX, scaleY;
    if (!window) return;
    SDL_GL_GetDrawableSize(window, &width, &height);
    if (width <= 0 || height <= 0 ||
        (width == s_drawWidth && height == s_drawHeight)) return;

    batchFlush();
    s_drawWidth = width;
    s_drawHeight = height;
    scaleX = (float)width / (float)s_width;
    scaleY = (float)height / (float)s_height;
    s_outputScale = (scaleX < scaleY) ? scaleX : scaleY;
    s_outputWidth = (int)floorf(s_width * s_outputScale);
    s_outputHeight = (int)floorf(s_height * s_outputScale);
    if (s_outputWidth < 1) s_outputWidth = 1;
    if (s_outputHeight < 1) s_outputHeight = 1;
    s_outputX = (width - s_outputWidth) / 2;
    s_outputY = (height - s_outputHeight) / 2;

    /* Clear newly exposed letterbox areas before drawing the next frame. */
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glDepthMask(GL_TRUE);
    s_depthWrite = 1;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    postResize();
    glApplyViewport();
}

/* --- backend slot implementations --- */

static int glSetMode(GxMode *mode)
{
    if (mode) {
        if (mode->width) s_width = mode->width;
        if (mode->height) s_height = mode->height;
    }
    batchFlush();
    /* Mode set restores the full logical viewport (driver power-on state);
     * s_view* must track it — gxGetViewport feeds sceneRender's clip/save
     * restore, and stale (zero-init) values would clip everything to a
     * 0-size scissor on the first scene render (frozen screen). */
    s_viewX = 0; s_viewY = 0; s_viewW = s_width; s_viewH = s_height;
    s_drawWidth = 0; s_drawHeight = 0;
    glUpdateDrawableSize();
    glApplyViewport();
    glEnable(GL_SCISSOR_TEST);
    glDepthFunc(GL_LESS);
    glClearDepth(1.0);
    glDepthMask(GL_TRUE);
    s_depthTest = s_depthEnabled;
    s_depthWrite = 1;
    if (s_depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    /* Ortho MVP for 640x480 (y-down to match software rasterizer). */
    float m[16] = {0};
    m[0] = 2.0f / (float)s_width; m[5] = -2.0f / (float)s_height;
    m[10] = -1.0f; m[12] = -1.0f; m[13] = 1.0f; m[15] = 1.0f;
    glUseProgram(s_prog);
    glUniformMatrix4fv(s_uMVP, 1, GL_FALSE, m);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return 1;
}

static int glGetMode(GxMode *mode)
{
    if (!mode) return 0;
    mode->width = (unsigned short)s_width;
    mode->height = (unsigned short)s_height;
    mode->bpp = 32;
    mode->hInstance = NULL;
    mode->hwnd = NULL;
    return 1;
}

static int glSnooze(void) { batchFlush(); return 1; }

static int glFlip(void)
{
    /* Window size changes affect the drawable independently of GX's fixed
     * 640x480 logical mode; scale that canvas on the next presentation. */
    glUpdateDrawableSize();
    if (s_batchCount != 0 || s_opaqueQueue.count != 0 ||
        s_transparentQueue.count != 0 || s_uiQueue.count != 0 ||
        s_legacyQueue.count != 0 || s_pendingBlit) {
        batchFlush();
        orphanVbo();
    }
    /* Sorted 3D/UI emission first, then a deferred fullscreen blit (if
     * any), then swap — mirroring gxFlip's sort/draw/blit order. */
    queueEmitSorted();
    if (s_pendingBlit) {
        s_pendingBlit = 0;
        useDrawState(s_streamTex, 0, 0, 0, 0);
        batchTri(0, 0, 0, 0, 1, (float)s_width, 0, 1, 0, 1,
                 (float)s_width, (float)s_height, 1, 1, 1, 1, 1, 1, 1);
        batchTri(0, 0, 0, 0, 1, (float)s_width, (float)s_height, 1, 1, 1,
                 0, (float)s_height, 0, 1, 1, 1, 1, 1, 1);
    }
    batchFlush();
    SDL_Window *w = (SDL_Window *)platformWindow();
    if (w) {
        SDL_GL_SwapWindow(w);
        platformUpdateFPS();
    }
    return 1;
}

static int glClearScreen(int clearMode, int color)
{
    (void)clearMode;
    batchFlush();
    if (s_outputX != 0 || s_outputY != 0 ||
        s_outputWidth != s_drawWidth || s_outputHeight != s_drawHeight) {
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glDepthMask(GL_TRUE);
        s_depthWrite = 1;
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        glApplyViewport();
    }
    float r = ((color >> 16) & 0xff) / 255.0f;
    float g = ((color >> 8) & 0xff) / 255.0f;
    float b = (color & 0xff) / 255.0f;
    glClearColor(r, g, b, 1.0f);
    glDepthMask(GL_TRUE);
    s_depthWrite = 1;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (postActive()) {
        /* Same scissored clear on the offscreen scene target; alpha 0
         * marks "no scene coverage" for the composite. */
        postSetTarget(1);
        glClearColor(r, g, b, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        postSetTarget(0);
    }
    return 1;
}

static int glSetViewport(void *rect)
{
    int oldX = s_viewX;
    int oldY = s_viewY;
    int oldW = s_viewW;
    int oldH = s_viewH;
    if (rect) {
        int *r = (int *)rect; /* left,top,right,bottom (pixels) */
        int w = r[2] - r[0], h = r[3] - r[1];
        if (w < 0) w = 0;
        if (h < 0) h = 0;
        s_viewX = r[0]; s_viewY = s_height - r[3]; s_viewW = w; s_viewH = h;
    } else {
        s_viewX = 0; s_viewY = 0; s_viewW = s_width; s_viewH = s_height;
    }
    if (s_viewX != oldX || s_viewY != oldY ||
        s_viewW != oldW || s_viewH != oldH) {
        glApplyViewport();
    }
    return 1;
}

static int glGetViewport(void *rect)
{
    if (rect) {
        int *r = (int *)rect;
        r[0] = s_viewX; r[1] = s_height - (s_viewY + s_viewH);
        r[2] = s_viewX + s_viewW; r[3] = s_height - s_viewY;
    }
    return 1;
}

static int glResetState(void)
{
    batchFlush();
    s_boundTex = 0;
    s_blendOn = 0;
    s_keyBlack = 0;
    glBindTexture(GL_TEXTURE_2D, 0);
    glUniform1i(s_uUseTex, 0);
    glUniform1i(s_uKeyBlack, 0);
    glUniform1i(s_uKeyAlpha, 0);
    s_a2cOn = 0;
    glUniform1i(s_uA2C, 0);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_BLEND);
    glDepthFunc(GL_LESS);
    glClearDepth(1.0);
    glDepthMask(GL_TRUE);
    s_depthTest = s_depthEnabled;
    s_depthWrite = 1;
    if (s_depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    s_viewX = 0; s_viewY = 0; s_viewW = s_width; s_viewH = s_height;
    glApplyViewport();
    return 1;
}

/* Expand 256x256 indexed + 256 RGBA palette to RGBA8. Textures upload
 * fully opaque: transparency is a rasterizer behavior selected per draw
 * by the origin flags (opaque direct copy vs near-black color-key vs
 * blend average, see the header note), not a baked texel property. The
 * .tpg 4th palette byte is padding (uniformly 0xcd), not alpha.
 * Shared/UI texture loads remain nearest sampled. Scene surfaces use a
 * separate optional linear/mipmap path with a premultiplied color-key
 * variant so filtering cannot create dark fringes at transparent texels. */
static int glLoadTexture(int mode, int reserved, const char *name, void *data, void *palette)
{
    (void)mode; (void)reserved; (void)name;
    unsigned char *idx = (unsigned char *)data;
    unsigned char *pal = (unsigned char *)palette;
    unsigned char *rgba;
    GLuint tex;
    if (!data) return 0;
    rgba = (unsigned char *)malloc(256 * 256 * 4);
    if (!rgba) return 0;
    for (int i = 0; i < 256 * 256; i++) {
        unsigned char p = idx[i];
        rgba[i * 4 + 0] = pal[p * 4 + 0];
        rgba[i * 4 + 1] = pal[p * 4 + 1];
        rgba[i * 4 + 2] = pal[p * 4 + 2];
        rgba[i * 4 + 3] = 0xff;
    }
    batchFlush();
    tex = uploadTexture(rgba, 0);
    free(rgba);
    /* Restore the cached draw binding (skip when the cache is the
     * unknown sentinel left by a pending presentFrame upload — the
     * flip-time useDrawState re-establishes it). */
    if (s_boundTex != 0xFFFFFFFFu) glBindTexture(GL_TEXTURE_2D, s_boundTex);
    if (!tex) return 0;
    {
        int handle = texAlloc((GLTexture){tex, 0, 0});
        if (!handle) glDeleteTextures(1, &tex);
        return handle;
    }
}

static void glUpdate(void) { batchFlush(); }

static int glCreateSurface(const char *path);

/* GxVert layout: x,y,z 8.8 fixed; r,g,b,a bytes. UV record format
 * depends on flags bit 2 (value 4): when set, gx.c repacks the GxColorUv
 * into a 0x10-byte record (tex id[0..3], packed UV high-bytes[8..15],
 * each byte a 0..255 texel coordinate, matching GXSOFT's >>8 reads).
 * Corner split replicates GXSOFT gxDrawPolygon @0x100020e0 exactly: it
 * draws two gxDrawTriUV @0x100021f0 tris, (v0,v1,v2) with UV corners
 * (U,V),(gwU,V2),(gwU2,hV) and (v0,v2,v3) with (U,V),(gwU2,hV),(U2,hV2)
 * (second tri repacks bytes 8,9,12,13,14,15). The path always ORs 0xd000
 * into the origin, so the 0x1000 half-texel rule applies (see normUv).
 * This 2D polygon entry point retains white modulation for textured draws;
 * without bit 2 the driver ignores UVs and uses the vertex color. Scene-mesh
 * draws through glDrawTriUV/glDrawQuad are white-modulated the same way. */
static void glDrawPolygon(void *a0, void *a1, void *a2, void *a3, int flags, void *uvRec)
{
    GxVert *v0 = (GxVert *)a0, *v1 = (GxVert *)a1, *v2 = (GxVert *)a2, *v3 = (GxVert *)a3;
    unsigned char *uv = (unsigned char *)uvRec;
    GLuint tex = 0;
    int keyMode = 0;
    float r = 1, g = 1, b = 1;
    float u0 = 0, v00 = 0, u1 = 0, v1_ = 0, u2 = 0, v2_ = 0, u3 = 0, v3_ = 0;
    if (!v0 || !v1 || !v2 || !v3) return;
    if (outsideViewport(v0, v1, v2, v3)) return;
    r = v0->r / 255.0f; g = v0->g / 255.0f; b = v0->b / 255.0f;
    if (uv && (flags & 4)) {
        int h = 0;
        memcpy(&h, uv + 0, 4);
        tex = texLookup(h, 1, &keyMode);
        /* Edges at byte/256 (see normUv): the implicit 0xd000 origin's
         * half-texel corner shift is folded in there. */
        u0 = normUv(uv[8], 1);  v00 = normUv(uv[9], 1);
        u1 = normUv(uv[10], 1); v1_ = normUv(uv[11], 1);
        u2 = normUv(uv[12], 1); v2_ = normUv(uv[13], 1);
        u3 = normUv(uv[14], 1); v3_ = normUv(uv[15], 1);
        r = g = b = 1.0f;
    }
    /* Binding happens per-tri at sorted emission; queue with tex + z-sum.
     * This path always ORs 0xd000 into the origin (hence 0x4000 set, 0x20
     * clear), so textured draws are always color-keyed (fonts depend on
     * it); untextured draws carry the vertex color opaquely. */
    {
        float x0 = fx8(v0->x), y0 = fx8(v0->y);
        float x1 = fx8(v1->x), y1 = fx8(v1->y);
        float x2 = fx8(v2->x), y2 = fx8(v2->y);
        float x3 = fx8(v3->x), y3 = fx8(v3->y);
        /* Queued with the GXSOFT z-sum key (z0+z1+z2); emitted sorted at flip.
         * Per-vertex w = az depth restores perspective-correct UVs (2D draws
         * use z 0 -> w 1, identical to the old affine path). */
        int z012 = v0->z + v1->z + v2->z;
        int z023 = v0->z + v2->z + v3->z;
        int key = tex ? keyMode : 0;
        queueTri(x0, y0, u0, v00, depthW(v0->z), x1, y1, u1, v1_, depthW(v1->z),
                 x2, y2, u2, v2_, depthW(v2->z),
                 r, g, b, r, g, b, r, g, b, 1, 0, tex, z012, 0, key);
        queueTri(x0, y0, u0, v00, depthW(v0->z), x2, y2, u2, v2_, depthW(v2->z),
                 x3, y3, u3, v3_, depthW(v3->z),
                 r, g, b, r, g, b, r, g, b, 1, 0, tex, z023, 0, key);
    }
}

static int glBlitSurface(int a, int b, int c, void *tex, int x, int y, int w, int h, int h2)
{
    /* Fullscreen present path (presentFrame @0x410310): tex is the CPU
     * R5G6B5 pixel buffer (640x480, 0x4b000 bytes) from tgaLoad16Pal.
     * tgaLoad16Pal @0x415ec0 packs R5 at bits 11-15, G6 at bits 5-10 and
     * B5 at bits 0-4 (verified against the decompile: (B>>3) +
     * (((R&0xf8)*0x20 + (G&0x1ffc))*8)), so decode R5G6B5 here — not
     * X1R5G5B5. No palette LUT. */
    unsigned short *px = (unsigned short *)tex;
    (void)a; (void)b; (void)c; (void)x; (void)y; (void)w; (void)h; (void)h2;
    if (!px) return 0;
    if (s_streamTex && s_streamSource && s_streamSourceValid &&
        memcmp(px, s_streamSource, 640 * 480 * 2) == 0) {
        s_boundTex = 0xFFFFFFFFu;
        s_pendingBlit = 1;
        return 1;
    }
    if (!s_rgba) s_rgba = (unsigned char *)malloc(640 * 480 * 4);
    if (!s_rgba) return 0;
    if (!s_streamSource) s_streamSource = (unsigned char *)malloc(640 * 480 * 2);
    if (!s_streamTex) {
        glGenTextures(1, &s_streamTex);
        batchFlush();
        glBindTexture(GL_TEXTURE_2D, s_streamTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 640, 480, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    batchFlush();
    glBindTexture(GL_TEXTURE_2D, s_streamTex);
    for (int i = 0; i < 640 * 480; i++) {
        unsigned short v = px[i];
        s_rgba[i*4+0] = (unsigned char)(((v >> 11) & 0x1f) * 255 / 31);
        s_rgba[i*4+1] = (unsigned char)(((v >> 5) & 0x3f) * 255 / 63);
        s_rgba[i*4+2] = (unsigned char)((v & 0x1f) * 255 / 31);
        s_rgba[i*4+3] = 0xff;
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 640, 480,
                    GL_RGBA, GL_UNSIGNED_BYTE, s_rgba);
    if (s_streamSource) {
        memcpy(s_streamSource, px, 640 * 480 * 2);
        s_streamSourceValid = 1;
    }
    /* The binds above are manual upload binds, not draw state: mark the
     * useDrawState cache unknown (never a real texture name) so the
     * flip-time useDrawState(s_streamTex, ...) always rebinds and
     * reprograms uUseTex/uKeyBlack/blend. Caching s_streamTex here would
     * make a later identical call a no-op that leaves stale uniforms
     * behind (e.g. uUseTex 0 from init -> fullscreen white quad). */
    s_boundTex = 0xFFFFFFFFu;
    /* Deferred past the sorted emission at flip (gxFlip blits after the
     * sorted records); glFlip appends the fullscreen quad + swaps. */
    s_pendingBlit = 1;
    return 1;
}

static void glSetOrigin(int packed)
{
    /* GXSOFT gxSetOrigin @0x10001f90 keeps the low 16 bits as g_dwFlags;
     * bit 0x8000 disables the backface-cull in gxDrawTriUV/gxDrawQuad. */
    s_origin = packed & 0xffff;
}

static void glDrawTriangle(void *v0, void *color) { (void)v0; (void)color; }
static void glDrawLine(void *v0, void *v1, void *color) { (void)v0; (void)v1; (void)color; }

static int backfaceCulled(const GxVert *v0, const GxVert *v1, const GxVert *v2)
{
    /* GXSOFT gxDrawTriUV @0x100021f0 / gxDrawQuad @0x10002540: draw when
     * (flags & 0x8000) or cross < 1, where cross is computed on the
     * 8.8-fixed x/y shifted down by 8 (arithmetic shift):
     *   (y2-y1)*(x0-x1) - (y0-y1)*(x2-x1) < 1.
     * meshDrawPoly @0x42e940 selects the origin flags per prim, so the
     * enable bit arrives via glSetOrigin (low 16 bits). */
    if (s_origin & 0x8000) return 0;
    {
        int x0 = v0->x >> 8, y0 = v0->y >> 8;
        int x1 = v1->x >> 8, y1 = v1->y >> 8;
        int x2 = v2->x >> 8, y2 = v2->y >> 8;
        int cross = (y2 - y1) * (x0 - x1) - (y0 - y1) * (x2 - x1);
        return cross >= 1;
    }
}

static void glDrawTriUV(void *a0, void *a1, void *a2, void *color, void *uvRec)
{
    GxVert *v0 = (GxVert *)a0, *v1 = (GxVert *)a1, *v2 = (GxVert *)a2;
    unsigned char *uv = (unsigned char *)uvRec;
    GLuint tex = 0;
    int h = 0;
    int keyMode = 0;
    float r = 1, g = 1, b = 1;
    (void)color;
    if (!v0 || !v1 || !v2) return;
    if (outsideViewport(v0, v1, v2, NULL)) return;
    if (backfaceCulled(v0, v1, v2)) return;
    r = v0->r / 255.0f; g = v0->g / 255.0f; b = v0->b / 255.0f;
    if (uv) memcpy(&h, uv + 0, 4);
    {
        /* Texture record is the 0x10-byte mesh/COLS record (verified
         * against gxSoft gxDrawTriUV @0x100021f0, see meshDrawPoly note
         * in scene_render.c): handle at +0, corner UV bytes at +8..+13.
         * Same byte convention as glDrawPolygon. Edges map via normUv
         * (byte/256), which already folds the origin 0x1000 corner shift.
         * Textured tris are white-modulated (no vertex tint, as in
         * GXSOFT). Queued with the GXSOFT
         * z-sum key (z0+z1+z2); emitted sorted at flip. */
        int half = (s_origin & 0x1000) != 0;
        float u0 = 0, v0_ = 0, u1 = 1, v1_ = 0, u2 = 0, v2_ = 1;
        float a = 1.0f;
        int blend = 0, key = 0;
        float r0 = r, g0 = g, b0 = b;
        float r1 = r, g1 = g, b1 = b;
        float r2 = r, g2 = g, b2 = b;
        if (uv && h) {
            int blendWanted = (s_origin & 0x20) != 0;
            int keyWanted = !blendWanted && (s_origin & 0x4000) != 0;
            tex = texLookup(h, keyWanted, &keyMode);
            /* Rasterizer selection mirrors gxSetOrigin @0x10001f90: 0x20
             * blends (approximated with 0.5 vertex alpha, black included),
             * 0x4000 color-keys near-black, otherwise fully opaque. */
            if (tex) {
                if (blendWanted) { blend = 1; a = 0.5f; }
                else if (keyWanted) key = keyMode;
                u0 = normUv(uv[8], half);  v0_ = normUv(uv[9], half);
                u1 = normUv(uv[10], half); v1_ = normUv(uv[11], half);
                u2 = normUv(uv[12], half); v2_ = normUv(uv[13], half);
                r = g = b = 1.0f;
                r0 = r1 = r2 = r;
                g0 = g1 = g2 = g;
                b0 = b1 = b2 = b;
            }
        }
        queueTri(fx8(v0->x), fx8(v0->y), u0, v0_, depthW(v0->z),
                 fx8(v1->x), fx8(v1->y), u1, v1_, depthW(v1->z),
                 fx8(v2->x), fx8(v2->y), u2, v2_, depthW(v2->z),
                 r0, g0, b0, r1, g1, b1, r2, g2, b2, a,
                 1, tex, v0->z + v1->z + v2->z, blend, key);
    }
}

static void glDrawQuad(void *a0, void *a1, void *a2, void *a3, void *color, void *uvRec)
{
    /* GXSOFT gxDrawQuad @0x10002540: one backface test on (v0,v1,v2) for
     * the whole quad (same <1 rule + 0x8000 override as gxDrawTriUV),
     * then two gxDrawTriUV calls: (v0,v1,v2) with the record as-is and
     * (v0,v2,v3) with a repacked record holding bytes 8,9,12,13,14,15, so
     * v3 samples the record's fourth corner (bytes 14,15) — not a copy of
     * the third. Kept separate from glDrawPolygon: here `color` is the
     * mesh color record (a pointer carried as int), not bit flags, so
     * routing it through the `flags & 4` test would key texturing off heap
     * alignment. UV edges map via normUv (see glDrawTriUV); textured
     * draws are white-modulated (no vertex tint, as in GXSOFT). */
    GxVert *v0 = (GxVert *)a0, *v1 = (GxVert *)a1,
           *v2 = (GxVert *)a2, *v3 = (GxVert *)a3;
    unsigned char *uv = (unsigned char *)uvRec;
    GLuint tex = 0;
    int h = 0;
    int keyMode = 0;
    float r = 1, g = 1, b = 1;
    (void)color;
    if (!v0 || !v1 || !v2 || !v3) return;
    if (outsideViewport(v0, v1, v2, v3)) return;
    if (backfaceCulled(v0, v1, v2)) return;
    r = v0->r / 255.0f; g = v0->g / 255.0f; b = v0->b / 255.0f;
    if (uv) memcpy(&h, uv + 0, 4);
    {
        int half = (s_origin & 0x1000) != 0;
        float u0 = 0, v0_ = 0, u1 = 1, v1_ = 0, u2 = 0, v2_ = 1, u3 = 1, v3_ = 1;
        float a = 1.0f;
        int blend = 0, key = 0;
        float r0 = r, g0 = g, b0 = b;
        float r1 = r, g1 = g, b1 = b;
        float r2 = r, g2 = g, b2 = b;
        float r3 = r, g3 = g, b3 = b;
        if (uv && h) {
            int blendWanted = (s_origin & 0x20) != 0;
            int keyWanted = !blendWanted && (s_origin & 0x4000) != 0;
            tex = texLookup(h, keyWanted, &keyMode);
            /* Same rasterizer selection as glDrawTriUV (see note there). */
            if (tex) {
                if (blendWanted) { blend = 1; a = 0.5f; }
                else if (keyWanted) key = keyMode;
                u0 = normUv(uv[8], half);  v0_ = normUv(uv[9], half);
                u1 = normUv(uv[10], half); v1_ = normUv(uv[11], half);
                u2 = normUv(uv[12], half); v2_ = normUv(uv[13], half);
                u3 = normUv(uv[14], half); v3_ = normUv(uv[15], half);
                r = g = b = 1.0f;
                r0 = r1 = r2 = r3 = r;
                g0 = g1 = g2 = g3 = g;
                b0 = b1 = b2 = b3 = b;
            }
        }
        /* Queued with the GXSOFT z-sum keys; emitted sorted at flip.
         * Per-vertex w = az depth restores perspective-correct UVs. */
        queueTri(fx8(v0->x), fx8(v0->y), u0, v0_, depthW(v0->z),
                 fx8(v1->x), fx8(v1->y), u1, v1_, depthW(v1->z),
                 fx8(v2->x), fx8(v2->y), u2, v2_, depthW(v2->z),
                 r0, g0, b0, r1, g1, b1, r2, g2, b2, a,
                 1, tex, v0->z + v1->z + v2->z, blend, key);
        queueTri(fx8(v0->x), fx8(v0->y), u0, v0_, depthW(v0->z),
                 fx8(v2->x), fx8(v2->y), u2, v2_, depthW(v2->z),
                 fx8(v3->x), fx8(v3->y), u3, v3_, depthW(v3->z),
                 r0, g0, b0, r2, g2, b2, r3, g3, b3, a,
                 1, tex, v0->z + v2->z + v3->z, blend, key);
    }
}

static int glCreateSurface(const char *path)
{
    /* Surface = indexed .tpg on disk (256x256 + palette). TNAM names are
     * bare (no dir/extension, e.g. "MERGED00"); resolve against the
     * current scene texture dir (set by sceneLoadSen from the .sen path)
     * with a ".TPG" extension fallback, then the bare asset lookup.
     * Backslash literals are normalized to '/'. */
    char norm[1024], full[2048];
    FILE *f = NULL;
    unsigned char *idx;
    unsigned char *pal;
    unsigned char *rgba;
    unsigned char *rgbaKeyed;
    GLTexture texture = {0, 0, 0};
    size_t n;
    const char *texDir;
    if (!path) return 0;
    snprintf(norm, sizeof(norm), "%s", path);
    for (char *p = norm; *p; p++) if (*p == '\\') *p = '/';
    texDir = gxGetTextureDir();
    {
        char cand[2048];
        const char *dirs[2] = { texDir, "" };
        for (int d = 0; d < 2 && !f; d++) {
            for (int e = 0; e < 2 && !f; e++) {
                if (dirs[d][0])
                    snprintf(cand, sizeof(cand), "%s/%s%s", dirs[d], norm, e ? ".TPG" : "");
                else
                    snprintf(cand, sizeof(cand), "%s%s", norm, e ? ".TPG" : "");
                platformAssetPath(cand, full, sizeof(full));
                f = fopen(full, "rb");
                if (!f) f = fopen(cand, "rb");
                if (!f) f = platformFopenCI(full, "rb");
                if (!f) f = platformFopenCI(cand, "rb");
            }
        }
    }
    if (!f) return 0;
    idx = (unsigned char *)malloc(256 * 256);
    pal = (unsigned char *)malloc(256 * 4);
    if (!idx || !pal) { free(idx); free(pal); fclose(f); return 0; }
    n = fread(idx, 1, 256 * 256, f);
    if (n != 256 * 256) { free(idx); free(pal); fclose(f); return 0; }
    n = fread(pal, 1, 256 * 4, f);
    fclose(f);
    if (n != 256 * 4) { free(idx); free(pal); return 0; }
    rgba = (unsigned char *)malloc(256 * 256 * 4);
    rgbaKeyed = s_linearSceneTextures ? (unsigned char *)malloc(256 * 256 * 4) : NULL;
    if (!rgba) { free(idx); free(pal); free(rgbaKeyed); return 0; }
    for (int i = 0; i < 256 * 256; i++) {
        unsigned char p = idx[i];
        unsigned char red = pal[p*4+0], green = pal[p*4+1], blue = pal[p*4+2];
        int keyed = (red < 8 && green < 8 && blue < 8);
        rgba[i*4+0] = red; rgba[i*4+1] = green; rgba[i*4+2] = blue;
        /* Fully opaque upload (see glLoadTexture note): the color-key
         * discard happens per draw in the fragment shader. */
        rgba[i*4+3] = 0xff;
        if (rgbaKeyed) {
            /* Premultiplication makes linear and mip filtering combine
             * only visible color; keyed fragments are discarded after
             * sampling, avoiding dark halos at near-black texels. */
            rgbaKeyed[i*4+0] = keyed ? 0 : red;
            rgbaKeyed[i*4+1] = keyed ? 0 : green;
            rgbaKeyed[i*4+2] = keyed ? 0 : blue;
            rgbaKeyed[i*4+3] = keyed ? 0 : 0xff;
        }
    }
    free(idx); free(pal);
    batchFlush();
    texture.nearest = uploadTexture(rgba, 0);
    if (rgbaKeyed) {
        texture.linear = uploadTexture(rgba, 1);
        texture.linearKeyed = uploadTexture(rgbaKeyed, 1);
    }
    free(rgba); free(rgbaKeyed);
    /* Restore the cached draw binding (skip when the cache is the
     * unknown sentinel left by a pending presentFrame upload — the
     * flip-time useDrawState re-establishes it). */
    if (s_boundTex != 0xFFFFFFFFu) glBindTexture(GL_TEXTURE_2D, s_boundTex);
    if (!texture.nearest) {
        GLuint textures[] = {texture.linear, texture.linearKeyed};
        glDeleteTextures(2, textures);
        return 0;
    }
    {
        int handle = texAlloc(texture);
        if (!handle) {
            GLuint textures[] = {texture.nearest, texture.linear, texture.linearKeyed};
            glDeleteTextures(3, textures);
        }
        return handle;
    }
}

int gxGLBackendInstall(void)
{
    GLuint vs, fs;
    GLint depthBits = 0;
    vs = compileShader(GL_VERTEX_SHADER, kVS);
    fs = compileShader(GL_FRAGMENT_SHADER, kFS);
    s_prog = glCreateProgram();
    glAttachShader(s_prog, vs);
    glAttachShader(s_prog, fs);
    glLinkProgram(s_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    s_uMVP = glGetUniformLocation(s_prog, "uMVP");
    s_uUseTex = glGetUniformLocation(s_prog, "uUseTex");
    s_uKeyBlack = glGetUniformLocation(s_prog, "uKeyBlack");
    s_uKeyAlpha = glGetUniformLocation(s_prog, "uKeyAlpha");
    s_uA2C = glGetUniformLocation(s_prog, "uA2C");
    /* GL_DEPTH_BITS is not a valid core-profile query (it returned 0 and
     * silently kept the painter-only path); ask the default framebuffer. */
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH,
        GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depthBits);
    s_depthEnabled = depthBits > 0;
    appLog("[gxGL] window depth buffer %d bits", (int)depthBits);
    postInit();
    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    s_vboBound = 1;
    glBufferData(GL_ARRAY_BUFFER, sizeof(s_batch), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)8);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)16);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)32);
    glUseProgram(s_prog);
    glUniform1i(glGetUniformLocation(s_prog, "uTex"), 0);
    glUniform1i(s_uUseTex, 0);
    glUniform1i(s_uKeyBlack, 0);
    glUniform1i(s_uKeyAlpha, 0);
    glUniform1i(s_uA2C, 0);
    s_a2cOn = 0;
    glDepthFunc(GL_LESS);
    glClearDepth(1.0);
    glDepthMask(GL_TRUE);
    s_depthTest = s_depthEnabled;
    s_depthWrite = 1;
    if (s_depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    /* Alpha accumulates coverage (src + dst * (1 - src)); only the post
     * target reads it back, as the scene mask. */
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    s_viewX = 0; s_viewY = 0; s_viewW = s_width; s_viewH = s_height;
    glScissor(0, 0, s_width, s_height);
    s_batchCount = 0;
    s_boundTex = 0;
    s_blendOn = 0;
    s_keyBlack = 0;
    s_opaqueQueue.count = 0;
    s_transparentQueue.count = 0;
    s_uiQueue.count = 0;
    s_legacyQueue.count = 0;
    s_qSeq = 0;
    s_transparentOrdered = 1;
    s_legacyOrdered = 1;
    s_pendingBlit = 0;
    s_texCount = 0;
    memset(s_tex, 0, sizeof(s_tex));
    {
        const char *filter = getenv("MANIAC_SCENE_TEXTURE_FILTER");
        /* Scene surfaces default to linear/trilinear sampling. Set this to
         * "nearest" to restore the original sampling path for comparison. */
        s_linearSceneTextures = !(filter && strcmp(filter, "nearest") == 0);
    }

    g_driver.api.pField_0 = NULL;
    g_driver.api.pSetMode = glSetMode;
    g_driver.api.pGetMode = glGetMode;
    g_driver.api.pSnooze = glSnooze;
    g_driver.api.pField_10 = NULL;
    g_driver.api.pField_14 = NULL;
    g_driver.api.pFlip = glFlip;
    g_driver.api.pField_1c = NULL;
    g_driver.api.pClearScreen = glClearScreen;
    g_driver.api.pField_24 = NULL;
    g_driver.api.pField_28 = NULL;
    g_driver.api.pField_2c = NULL;
    g_driver.api.pField_30 = NULL;
    g_driver.api.pField_34 = NULL;
    g_driver.api.pSetViewport = glSetViewport;
    g_driver.api.pGetViewport = glGetViewport;
    g_driver.api.pResetState = glResetState;
    g_driver.api.pLoadTexture = glLoadTexture;
    g_driver.api.pUpdate = glUpdate;
    g_driver.api.pCreateSurface = glCreateSurface;
    g_driver.api.pData50 = NULL;
    g_driver.api.pData54 = NULL;
    g_driver.api.pData58 = NULL;
    g_driver.api.pDrawPolygon = glDrawPolygon;
    g_driver.api.nDrawEnabled = 1;
    g_driver.api.pBlitSurface = glBlitSurface;
    g_driver.api.pSetOrigin = glSetOrigin;
    g_driver.api.pDrawTriangle = glDrawTriangle;
    g_driver.api.pDrawLine = glDrawLine;
    g_driver.api.pDrawTriUV = glDrawTriUV;
    g_driver.api.pDrawQuad = glDrawQuad;
    g_driver.api.nSoftwareMode = 0;
    g_driver.hDriverModule = (void *)0x1; /* built-in marker (not a DLL) */
    g_driver.nDriverActive = 0;
    appLog("[gxGL] backend installed (GL 3.3, batch %d verts)", GL_BATCH_MAX_VERTS);
    return 1;
}

void gxGLBackendUninstall(void)
{
    batchFlush();
    s_opaqueQueue.count = 0;
    s_transparentQueue.count = 0;
    s_uiQueue.count = 0;
    s_legacyQueue.count = 0;
    s_qSeq = 0;
    s_transparentOrdered = 1;
    s_legacyOrdered = 1;
    s_pendingBlit = 0;
    for (int i = 0; i < s_texCount; i++) {
        GLuint textures[] = {s_tex[i].nearest, s_tex[i].linear,
                             s_tex[i].linearKeyed};
        glDeleteTextures(3, textures);
    }
    s_texCount = 0;
    if (s_streamTex) { glDeleteTextures(1, &s_streamTex); s_streamTex = 0; }
    free(s_rgba); s_rgba = NULL;
    free(s_streamSource); s_streamSource = NULL;
    s_streamSourceValid = 0;
    if (s_vbo) { glDeleteBuffers(1, &s_vbo); s_vbo = 0; s_vboBound = 0; }
    if (s_vao) { glDeleteVertexArrays(1, &s_vao); s_vao = 0; }
    if (s_prog) { glDeleteProgram(s_prog); s_prog = 0; }
    postShutdown();
    memset(&g_driver.api, 0, sizeof(g_driver.api));
    g_driver.hDriverModule = NULL;
    g_driver.nDriverActive = 0;
}
