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
 * - Textured tris are unshaded (GXSOFT feeds the rasterizer texture + UVs
 *   only, so vertex colors are ignored); untextured quads use the vertex
 *   color. The 0x1000 origin bit needs no GL replication (normUv folds its
 *   effect into the byte/256 edge); other exotic
 *   pSetOrigin bits only gate the backface rule. pDrawTriangle/pDrawLine
 *   stay no-ops (as in GXSOFT).
 * - Ordering: gxDrawTriUV/gxDrawQuad/gxDrawPolygon only QUEUE 0x50-byte
 *   records (backface-cull + trivial-reject at queue time); gxFlip runs
 *   the gxSortPolyRecords bucket painter sort (descending z-sum: far
 *   first) and rasterizes in that order. The port replicates this with a
 *   deferred queue: every tri is queued with its z-sum key and emitted
 *   back-to-front at pFlip (stable within equal keys, so z==0 HUD draws
 *   keep submission order on top). Drawing immediately in submission
 *   order hides characters behind the mall floor and floats transparent
 *   items above nearer geometry.
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
 */

extern int g_nGfxMode; /* @0x4580c4 */

#define GL_BATCH_MAX_VERTS 65536

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
static GLint s_uMVP;
static GLint s_uUseTex = -1;
static GLint s_uKeyBlack = -1;
static GLVertex s_batch[GL_BATCH_MAX_VERTS];
static int s_batchCount;
static GLuint s_boundTex;
static int s_blendOn;
static int s_keyBlack;
/* Fullscreen streaming texture for presentFrame CPU buffers (R5G6B5). */
static GLuint s_streamTex;
static int s_viewX, s_viewY, s_viewW, s_viewH;
static int s_origin;
static int s_width = 640, s_height = 480;
static int s_drawWidth = 640, s_drawHeight = 480;
static int s_outputX, s_outputY, s_outputWidth = 640, s_outputHeight = 480;
static float s_outputScale = 1.0f;

/* int handle (1-based) -> GLuint registry. */
#define GL_TEX_MAX 4096
static GLuint s_tex[GL_TEX_MAX];
static int s_texCount;

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
    "void main(){ vec4 p=uMVP*vec4(aPos,0.0,1.0); gl_Position=vec4(p.xy*aW,0.0,aW); vUV=aUV; vCol=aCol; }\n";
static const char *kFS =
    "#version 330 core\n"
    "in vec2 vUV; in vec4 vCol;\n"
    "uniform sampler2D uTex;\n"
    "uniform int uUseTex;\n"
    "uniform int uKeyBlack;\n"
    "out vec4 oCol;\n"
    /* Near-black discard replicates the _g_abColorKey LUT built by
     * gxLoadTexture @0x100019b0 (palette RGB all < 8 keeps the
     * destination). Byte-exact threshold 7.5 keeps index 0 (8,8,8)
     * opaque; NEAREST filtering yields exact texel values. Blend-path
     * tris arrive with vCol.a 0.5 to approximate the _g_abBlend
     * averaging LUT (uKeyBlack is 0 there, so black still contributes). */
    "void main(){ vec4 t=texture(uTex,vUV); if(uUseTex!=0){ if(uKeyBlack!=0 && t.r*255.0<7.5 && t.g*255.0<7.5 && t.b*255.0<7.5) discard; oCol=vec4(t.rgb*vCol.rgb,t.a*vCol.a); } else oCol=vCol; }\n";

static GLuint compileShader(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    return sh;
}

static void batchFlush(void)
{
    if (s_batchCount == 0 || s_prog == 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
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

static void useDrawState(GLuint tex, int blend, int key)
{
    if (tex != s_boundTex || blend != s_blendOn || key != s_keyBlack) {
        batchFlush();
        s_boundTex = tex;
        s_blendOn = blend;
        s_keyBlack = key;
        glBindTexture(GL_TEXTURE_2D, tex ? tex : 0);
        glUniform1i(s_uUseTex, tex ? 1 : 0);
        glUniform1i(s_uKeyBlack, (tex && key) ? 1 : 0);
        if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    }
}

/* Deferred painter queue replicating the GXSOFT record queue + the
 * gxSortPolyRecords bucket sort at gxFlip time. Every tri from
 * glDrawPolygon/glDrawTriUV/glDrawQuad is queued with its z-sum key
 * (GxVert z0+z1+z2, larger = farther: scene depth is (aspect+wz)*16,
 * 2D draws use z 0) and emitted back-to-front at glFlip. Ordering within
 * equal keys is stable (sequence tiebreak) so HUD layering is preserved.
 * The per-tri texture rides along because the sorted emission rebinds
 * (useDrawState flushes the GL batch on change, as before). */
typedef struct QueuedTri {
    GLVertex v[3];
    GLuint tex;
    int zsum;
    unsigned seq;
    int blend; /* GL blend enable at emission (blend rasterizer path) */
    int key;   /* near-black discard at emission (color-key path) */
} QueuedTri;

static QueuedTri *s_queue;
static size_t s_qCount, s_qCap;
static unsigned s_qSeq;
static int s_pendingBlit; /* fullscreen present deferred past sorted emission */

static void queueTri(float x0, float y0, float u0, float v0, float w0,
                     float x1, float y1, float u1, float v1, float w1,
                     float x2, float y2, float u2, float v2, float w2,
                     float r, float g, float b, float a,
                     GLuint tex, int zsum, int blend, int key)
{
    if (s_qCount >= s_qCap) {
        size_t ncap = s_qCap ? s_qCap * 2 : 4096;
        QueuedTri *nq = (QueuedTri *)realloc(s_queue, ncap * sizeof(QueuedTri));
        if (!nq) return; /* OOM: drop (never observed; queue drains every flip) */
        s_queue = nq;
        s_qCap = ncap;
    }
    {
        QueuedTri *q = &s_queue[s_qCount++];
        q->v[0] = (GLVertex){x0, y0, u0, v0, r, g, b, a, w0};
        q->v[1] = (GLVertex){x1, y1, u1, v1, r, g, b, a, w1};
        q->v[2] = (GLVertex){x2, y2, u2, v2, r, g, b, a, w2};
        q->tex = tex;
        q->zsum = zsum;
        q->seq = s_qSeq++;
        q->blend = blend;
        q->key = key;
    }
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
static void queueEmitSorted(void)
{
    if (s_qCount > 1) qsort(s_queue, s_qCount, sizeof(QueuedTri), queueTriCmp);
    for (size_t i = 0; i < s_qCount; i++) {
        QueuedTri *q = &s_queue[i];
        useDrawState(q->tex, q->blend, q->key);
        batchEnsure(3);
        s_batch[s_batchCount++] = q->v[0];
        s_batch[s_batchCount++] = q->v[1];
        s_batch[s_batchCount++] = q->v[2];
    }
    s_qCount = 0;
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

static int texAlloc(GLuint gl)
{
    if (s_texCount >= GL_TEX_MAX) return 0;
    s_tex[s_texCount] = gl;
    return ++s_texCount; /* 1-based handle */
}

static GLuint texLookup(int handle)
{
    if (handle <= 0 || handle > s_texCount) return 0;
    return s_tex[handle - 1];
}

static void glApplyViewport(void)
{
    int left = s_outputX + (int)floorf(s_viewX * s_outputScale);
    int bottom = s_outputY + (int)floorf(s_viewY * s_outputScale);
    int right = s_outputX + (int)ceilf((s_viewX + s_viewW) * s_outputScale);
    int top = s_outputY + (int)ceilf((s_viewY + s_viewH) * s_outputScale);
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
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
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
    /* Ortho MVP for 640x480 (y-down to match software rasterizer). */
    float m[16] = {0};
    m[0] = 2.0f / (float)s_width; m[5] = -2.0f / (float)s_height;
    m[10] = -1.0f; m[12] = -1.0f; m[13] = 1.0f; m[15] = 1.0f;
    glUseProgram(s_prog);
    glUniformMatrix4fv(s_uMVP, 1, GL_FALSE, m);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
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
    /* Sorted 3D/UI emission first, then a deferred fullscreen blit (if
     * any), then swap — mirroring gxFlip's sort/draw/blit order. */
    queueEmitSorted();
    if (s_pendingBlit) {
        s_pendingBlit = 0;
        useDrawState(s_streamTex, 0, 0);
        batchTri(0, 0, 0, 0, 1, (float)s_width, 0, 1, 0, 1,
                 (float)s_width, (float)s_height, 1, 1, 1, 1, 1, 1, 1);
        batchTri(0, 0, 0, 0, 1, (float)s_width, (float)s_height, 1, 1, 1,
                 0, (float)s_height, 0, 1, 1, 1, 1, 1, 1);
    }
    batchFlush();
    SDL_Window *w = (SDL_Window *)platformWindow();
    if (w) SDL_GL_SwapWindow(w);
    return 1;
}

static int glClearScreen(int clearMode, int color)
{
    (void)clearMode;
    batchFlush();
    glUpdateDrawableSize();
    if (s_outputX != 0 || s_outputY != 0 ||
        s_outputWidth != s_drawWidth || s_outputHeight != s_drawHeight) {
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        glApplyViewport();
    }
    float r = ((color >> 16) & 0xff) / 255.0f;
    float g = ((color >> 8) & 0xff) / 255.0f;
    float b = (color & 0xff) / 255.0f;
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    return 1;
}

static int glSetViewport(void *rect)
{
    batchFlush();
    if (rect) {
        int *r = (int *)rect; /* left,top,right,bottom (pixels) */
        int w = r[2] - r[0], h = r[3] - r[1];
        if (w < 0) w = 0;
        if (h < 0) h = 0;
        s_viewX = r[0]; s_viewY = s_height - r[3]; s_viewW = w; s_viewH = h;
        glApplyViewport();
    } else {
        s_viewX = 0; s_viewY = 0; s_viewW = s_width; s_viewH = s_height;
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
    glDisable(GL_BLEND);
    s_viewX = 0; s_viewY = 0; s_viewW = s_width; s_viewH = s_height;
    glApplyViewport();
    return 1;
}

/* Expand 256x256 indexed + 256 RGBA palette to RGBA8. Textures upload
 * fully opaque: transparency is a rasterizer behavior selected per draw
 * by the origin flags (opaque direct copy vs near-black color-key vs
 * blend average, see the header note), not a baked texel property. The
 * .tpg 4th palette byte is padding (uniformly 0xcd), not alpha.
 * PORT DEVIATION (cross-platform): always highest texture LOD — full-res
 * RGBA8 upload, NEAREST sampling, mip levels clamped to base level 0 so
 * the sampler can never pick a downsampled level. */
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
    glGenTextures(1, &tex);
    batchFlush();
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    /* Restore the cached draw binding (skip when the cache is the
     * unknown sentinel left by a pending presentFrame upload — the
     * flip-time useDrawState re-establishes it). */
    if (s_boundTex != 0xFFFFFFFFu) glBindTexture(GL_TEXTURE_2D, s_boundTex);
    return texAlloc(tex);
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
 * Textured tris carry no vertex color (gxDrawPolyRecords @0x10002670 feeds
 * the rasterizer texture + UVs only), so textured draws use white; without
 * bit 2 the driver ignores the UV fields and the quad renders untextured
 * with the vertex color. */
static void glDrawPolygon(void *a0, void *a1, void *a2, void *a3, int flags, void *uvRec)
{
    GxVert *v0 = (GxVert *)a0, *v1 = (GxVert *)a1, *v2 = (GxVert *)a2, *v3 = (GxVert *)a3;
    unsigned char *uv = (unsigned char *)uvRec;
    GLuint tex = 0;
    float r = 1, g = 1, b = 1;
    float u0 = 0, v00 = 0, u1 = 0, v1_ = 0, u2 = 0, v2_ = 0, u3 = 0, v3_ = 0;
    if (!v0 || !v1 || !v2 || !v3) return;
    r = v0->r / 255.0f; g = v0->g / 255.0f; b = v0->b / 255.0f;
    if (uv && (flags & 4)) {
        int h = 0;
        memcpy(&h, uv + 0, 4);
        tex = texLookup(h);
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
        int key = (tex != 0);
        queueTri(x0, y0, u0, v00, depthW(v0->z), x1, y1, u1, v1_, depthW(v1->z),
                 x2, y2, u2, v2_, depthW(v2->z), r, g, b, 1, tex, z012, 0, key);
        queueTri(x0, y0, u0, v00, depthW(v0->z), x2, y2, u2, v2_, depthW(v2->z),
                 x3, y3, u3, v3_, depthW(v3->z), r, g, b, 1, tex, z023, 0, key);
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
    static unsigned char *s_rgba;
    unsigned short *px = (unsigned short *)tex;
    (void)a; (void)b; (void)c; (void)x; (void)y; (void)w; (void)h; (void)h2;
    if (!px) return 0;
    if (!s_rgba) s_rgba = (unsigned char *)malloc(640 * 480 * 4);
    if (!s_rgba) return 0;
    if (!s_streamTex) {
        glGenTextures(1, &s_streamTex);
        batchFlush();
        glBindTexture(GL_TEXTURE_2D, s_streamTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    for (int i = 0; i < 640 * 480; i++) {
        unsigned short v = px[i];
        s_rgba[i*4+0] = (unsigned char)(((v >> 11) & 0x1f) * 255 / 31);
        s_rgba[i*4+1] = (unsigned char)(((v >> 5) & 0x3f) * 255 / 63);
        s_rgba[i*4+2] = (unsigned char)((v & 0x1f) * 255 / 31);
        s_rgba[i*4+3] = 0xff;
    }
    batchFlush();
    glBindTexture(GL_TEXTURE_2D, s_streamTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_rgba);
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
    int next = packed & 0xffff;
    if (next != s_origin) batchFlush();
    s_origin = next;
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
    float r = 1, g = 1, b = 1;
    (void)color;
    if (!v0 || !v1 || !v2) return;
    if (backfaceCulled(v0, v1, v2)) return;
    r = v0->r / 255.0f; g = v0->g / 255.0f; b = v0->b / 255.0f;
    if (uv) { memcpy(&h, uv + 0, 4); tex = texLookup(h); }
    {
        /* Texture record is the 0x10-byte mesh/COLS record (verified
         * against gxSoft gxDrawTriUV @0x100021f0, see meshDrawPoly note
         * in scene_render.c): handle at +0, corner UV bytes at +8..+13.
         * Same byte convention as glDrawPolygon. Edges map via normUv
         * (byte/256), which already folds the origin 0x1000 corner shift.
         * Textured tris are unshaded (see glDrawPolygon note). Queued
         * with the GXSOFT z-sum key (z0+z1+z2); emitted sorted at flip. */
        int half = (s_origin & 0x1000) != 0;
        float u0 = 0, v0_ = 0, u1 = 1, v1_ = 0, u2 = 0, v2_ = 1;
        float a = 1.0f;
        int blend = 0, key = 0;
        if (uv && tex) {
            u0 = normUv(uv[8], half);  v0_ = normUv(uv[9], half);
            u1 = normUv(uv[10], half); v1_ = normUv(uv[11], half);
            u2 = normUv(uv[12], half); v2_ = normUv(uv[13], half);
            r = g = b = 1.0f;
            /* Rasterizer selection mirrors gxSetOrigin @0x10001f90: 0x20
             * blends (approximated with 0.5 vertex alpha, black included),
             * 0x4000 color-keys near-black, otherwise fully opaque. */
            if (s_origin & 0x20) { blend = 1; a = 0.5f; }
            else if (s_origin & 0x4000) { key = 1; }
        }
        queueTri(fx8(v0->x), fx8(v0->y), u0, v0_, depthW(v0->z),
                 fx8(v1->x), fx8(v1->y), u1, v1_, depthW(v1->z),
                 fx8(v2->x), fx8(v2->y), u2, v2_, depthW(v2->z), r, g, b, a,
                 tex, v0->z + v1->z + v2->z, blend, key);
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
     * alignment. UV edges map via normUv (see glDrawTriUV);
     * textured tris are unshaded (see glDrawPolygon note). */
    GxVert *v0 = (GxVert *)a0, *v1 = (GxVert *)a1,
           *v2 = (GxVert *)a2, *v3 = (GxVert *)a3;
    unsigned char *uv = (unsigned char *)uvRec;
    GLuint tex = 0;
    int h = 0;
    float r = 1, g = 1, b = 1;
    (void)color;
    if (!v0 || !v1 || !v2 || !v3) return;
    if (backfaceCulled(v0, v1, v2)) return;
    r = v0->r / 255.0f; g = v0->g / 255.0f; b = v0->b / 255.0f;
    if (uv) { memcpy(&h, uv + 0, 4); tex = texLookup(h); }
    {
        int half = (s_origin & 0x1000) != 0;
        float u0 = 0, v0_ = 0, u1 = 1, v1_ = 0, u2 = 0, v2_ = 1, u3 = 1, v3_ = 1;
        float a = 1.0f;
        int blend = 0, key = 0;
        if (uv && tex) {
            u0 = normUv(uv[8], half);  v0_ = normUv(uv[9], half);
            u1 = normUv(uv[10], half); v1_ = normUv(uv[11], half);
            u2 = normUv(uv[12], half); v2_ = normUv(uv[13], half);
            u3 = normUv(uv[14], half); v3_ = normUv(uv[15], half);
            r = g = b = 1.0f;
            /* Same rasterizer selection as glDrawTriUV (see note there). */
            if (s_origin & 0x20) { blend = 1; a = 0.5f; }
            else if (s_origin & 0x4000) { key = 1; }
        }
        /* Queued with the GXSOFT z-sum keys; emitted sorted at flip.
         * Per-vertex w = az depth restores perspective-correct UVs. */
        queueTri(fx8(v0->x), fx8(v0->y), u0, v0_, depthW(v0->z),
                 fx8(v1->x), fx8(v1->y), u1, v1_, depthW(v1->z),
                 fx8(v2->x), fx8(v2->y), u2, v2_, depthW(v2->z), r, g, b, a,
                 tex, v0->z + v1->z + v2->z, blend, key);
        queueTri(fx8(v0->x), fx8(v0->y), u0, v0_, depthW(v0->z),
                 fx8(v2->x), fx8(v2->y), u2, v2_, depthW(v2->z),
                 fx8(v3->x), fx8(v3->y), u3, v3_, depthW(v3->z), r, g, b, a,
                 tex, v0->z + v2->z + v3->z, blend, key);
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
    GLuint tex;
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
    if (!rgba) { free(idx); free(pal); return 0; }
    for (int i = 0; i < 256 * 256; i++) {
        unsigned char p = idx[i];
        rgba[i*4+0] = pal[p*4+0]; rgba[i*4+1] = pal[p*4+1];
        rgba[i*4+2] = pal[p*4+2];
        /* Fully opaque upload (see glLoadTexture note): the color-key
         * discard happens per draw in the fragment shader. */
        rgba[i*4+3] = 0xff;
    }
    free(idx); free(pal);
    glGenTextures(1, &tex);
    batchFlush();
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* Highest texture LOD: clamp to base level 0 (see glLoadTexture note). */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    /* Restore the cached draw binding (skip when the cache is the
     * unknown sentinel left by a pending presentFrame upload — the
     * flip-time useDrawState re-establishes it). */
    if (s_boundTex != 0xFFFFFFFFu) glBindTexture(GL_TEXTURE_2D, s_boundTex);
    return texAlloc(tex);
}

int gxGLBackendInstall(void)
{
    GLuint vs, fs;
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
    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
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
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    s_viewX = 0; s_viewY = 0; s_viewW = s_width; s_viewH = s_height;
    glScissor(0, 0, s_width, s_height);
    s_batchCount = 0;
    s_boundTex = 0;
    s_blendOn = 0;
    s_keyBlack = 0;
    s_qCount = 0;
    s_qSeq = 0;
    s_pendingBlit = 0;
    s_texCount = 0;
    memset(s_tex, 0, sizeof(s_tex));

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
    s_qCount = 0;
    s_qSeq = 0;
    s_pendingBlit = 0;
    for (int i = 0; i < s_texCount; i++)
        if (s_tex[i]) glDeleteTextures(1, &s_tex[i]);
    s_texCount = 0;
    if (s_streamTex) { glDeleteTextures(1, &s_streamTex); s_streamTex = 0; }
    if (s_vbo) { glDeleteBuffers(1, &s_vbo); s_vbo = 0; }
    if (s_vao) { glDeleteVertexArrays(1, &s_vao); s_vao = 0; }
    if (s_prog) { glDeleteProgram(s_prog); s_prog = 0; }
    memset(&g_driver.api, 0, sizeof(g_driver.api));
    g_driver.hDriverModule = NULL;
    g_driver.nDriverActive = 0;
}
