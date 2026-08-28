#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gx.h"
#include "pool.h"
#include "util.h"

/* Vertical-slice GX driver adapter. Reimplements the narrow slice of the
 * maniac-side gx* wrappers that the GUI vertical slice needs. Driver is the
 * original DRIVERS\GXSOFT.DLL (software rasterizer), loaded directly (the
 * original gxLoadDriver @0x432ea0 reads the driver path + options from the
 * registry. */

/* gxDLLInit / gxDLLExit use the compiler's default C convention. */
typedef int  (*pfn_gxDLLInit)(GxDriverApi *api);
typedef void (*pfn_gxDLLExit)(void);

/* GxDriverApi extension @0x45eb40, populated by gxDLLInit. */
GxDriver g_driver;

/* gxDrawPolygon scale globals @0x45eb38/@0x45eb3c. The registry-driven
 * option loading that supplies alternate driver scales is deferred; the
 * fixed-point scale is the GXSOFT-compatible default for this rebuild. */
static float g_gxScaleY = 1.0f / 256.0f; /* @0x45eb38 */
static float g_gxScaleX = 1.0f / 256.0f; /* @0x45eb3c */

/* gxLoadDriver @0x432ea0 — the registry-selected driver path is deferred;
 * this slice accepts the known GXSOFT DLL path directly. */
int gxLoadDriver(char *driverName)
{
    HMODULE         hMod;
    pfn_gxDLLInit   pInit;
    GxDriverApi    *api = &g_driver.api;

    /* gxLoadDriver @0x432ea0 unloads an existing module first. */
    if (g_driver.hDriverModule != NULL) {
        gxUnloadDriver();
    }
    /* Registry-sourced path is deferred; use the known file when the caller
     * supplies the original NULL/configuration form. */
    hMod = LoadLibraryA(driverName != NULL ? driverName : "DRIVERS\\GXSOFT.DLL");
    if (hMod == NULL) {
        fprintf(stderr, "[gxLoadDriver] LoadLibraryA(DRIVERS\\GXSOFT.DLL) failed: %lu\n",
                (unsigned long)GetLastError());
        return 0;
    }
    g_driver.hDriverModule = hMod;

    memset(api, 0, sizeof(*api));
    /* The original loader clears this before gxDLLInit; the driver may then
     * set the software-mode flag while filling its table. */
    g_driver.api.nSoftwareMode = 0;
    g_driver.nDriverActive = 0;

    pInit = (pfn_gxDLLInit)GetProcAddress(hMod, "gxDLLInit");
    if (pInit == NULL) {
        fprintf(stderr, "[gxLoadDriver] gxDLLInit not found\n");
        goto fail;
    }
    if (pInit(api) == 0) {
        fprintf(stderr, "[gxLoadDriver] gxDLLInit failed\n");
        goto fail;
    }


    /* First texture load installs the DirectDraw palette (gxLoadTexture
     * @0x100019b0, DAT_1006bff0 branch). Callers should load a .tpg before
     * blitting indexed art so the palette is correct. */
    return 1;

fail:
    {
        pfn_gxDLLExit pExit = (pfn_gxDLLExit)GetProcAddress(hMod, "gxDLLExit");
        if (pExit != NULL) pExit();
    }
    FreeLibrary(hMod);
    g_driver.hDriverModule = NULL;
    return 0;
}

/* gxInit @0x4332f0 — the original is only the pSetMode dispatch. */
int gxInit(GxMode *mode)
{
    if (g_driver.api.pSetMode != NULL) {
        return g_driver.api.pSetMode(mode);
    }
    return 0;
}

/* gxUnloadDriver @0x433280 — gxDLLExit + FreeLibrary. */
int gxUnloadDriver(void)
{
    HMODULE hMod = g_driver.hDriverModule;

    if (hMod != NULL) {
        /* gxUnloadDriver @0x433280: gxDLLExit then FreeLibrary. */
        pfn_gxDLLExit pExit = (pfn_gxDLLExit)GetProcAddress(hMod, "gxDLLExit");
        if (pExit != NULL) pExit();
        FreeLibrary(hMod);
    }
    g_driver.hDriverModule = NULL;
    g_driver.nDriverActive = 0;
    return 1;
}

/* gxLoadTexture @0x4333d0 — maniac wrapper around GxDriverApi.pLoadTexture.
 * param_1 selects the .tpg load/free action; when loading (data!=0, mode==0)
 * bumps nDriverActive, when freeing (data==0, mode!=0) decrements it. */
int gxLoadTexture(int mode, int reserved, char *name, void *data,
                  void *palette)
{
    if (data == 0) {
        if (mode == 0) return 0;
        g_driver.nDriverActive--;
    } else if (mode == 0) {
        g_driver.nDriverActive++;
    }
    if (g_driver.api.pLoadTexture == NULL) return 0;
    return g_driver.api.pLoadTexture(mode, reserved, name, data, palette);
}

/* presentFrame @0x410310:
 *   gxBlitSurface(1,0,0,tex,0,0,0x280,0x280,0x1e0); gxFlip(); gxClearScreen(1,g_nClearColor);
 * g_nClearColor @0x45892c is 0. */
void presentFrame(int texture)
{
    if (texture != 0) {
        gxBlitSurface(1, 0, 0, (void *)(size_t)texture,
                      0, 0, 0x280, 0x280, 0x1e0);
        gxFlip();
        gxClearScreen(1, 0);
    }
}

/* gxLoadTpgFile @0x416060 — fileReadRaw(0,path) -> gxLoadTexture(0,1,path,
 * data,data+0x10000) -> memPoolFree(0,data); returns texture handle. Uses
 * pool 0 ("DEFAULT", created by memPoolSystemInit). */
int gxLoadTpgFile(LPCSTR path)
{
    char *data;
    int r;

    data = fileReadRaw(0, path);
    if (data == NULL) return 0;
    r = gxLoadTexture(0, 1, (char *)path, data, data + 0x10000);
    memPoolFree(0, data);
    return r;
}

/* ------------------------------------------------------------------ */
/* Maniac-side GX wrapper cluster — thin dispatches through GxDriverApi.
 * Each mirrors the maniac.exe function at the noted address.           */
/* ------------------------------------------------------------------ */

/* gxGetMode @0x433310 */
int gxGetMode(GxMode *mode)
{
    if (g_driver.api.pGetMode != NULL) {
        return g_driver.api.pGetMode(mode);
    }
    return 0;
}

/* gxSnooze @0x433330 */
int gxSnooze(void)
{
    if (g_driver.api.pSnooze != NULL) {
        return g_driver.api.pSnooze();
    }
    return 0;
}

/* gxFlip @0x433340 */
int gxFlip(void)
{
    if (g_driver.api.pFlip != NULL) {
        return g_driver.api.pFlip();
    }
    return 0;
}

/* gxClearScreen @0x433350 */
int gxClearScreen(int clearMode, int color)
{
    if (g_driver.api.pClearScreen != NULL) {
        return g_driver.api.pClearScreen(clearMode, color);
    }
    return 0;
}

/* gxSetViewport @0x433370 */
int gxSetViewport(void *rect)
{
    if (g_driver.api.pSetViewport != NULL) {
        return g_driver.api.pSetViewport(rect);
    }
    return 0;
}

/* gxGetViewport @0x433390 */
int gxGetViewport(void *rect)
{
    if (g_driver.api.pGetViewport != NULL) {
        return g_driver.api.pGetViewport(rect);
    }
    return 0;
}

/* gxResetState @0x4333b0 — zeroes nDriverActive then calls the driver slot. */
int gxResetState(void)
{
    g_driver.nDriverActive = 0;
    if (g_driver.api.pResetState != NULL) {
        return g_driver.api.pResetState();
    }
    return 0;
}

/* gxCreateSurface @0x433420 */
int gxCreateSurface(char *path)
{
    if (g_driver.api.pCreateSurface != NULL) {
        return g_driver.api.pCreateSurface(path);
    }
    return 0;
}

/* gxDrawPolygon @0x433440 — when nSoftwareMode is 1, convert each vertex's
 * integer coordinate with the original X/Y float scales and __ftol-equivalent
 * truncating cast before dispatch. GXSOFT leaves nSoftwareMode at zero and
 * therefore receives textDraw's original 8.8 fixed-point coordinates.
 * flags bit 2 (value 4) triggers a color/uv repack of colorUv into a local
 * 0x10-byte packed record. Signature matches the Ghidra definition:
 * void gxDrawPolygon(GxVert *v0, GxVert *v1, GxVert *v2, GxVert *v3,
 *                    int flags, GxColorUv *colorUv). */
void gxDrawPolygon(GxVert *v0, GxVert *v1, GxVert *v2, GxVert *v3, int flags,
                   GxColorUv *colorUv)
{
    unsigned char local[0x10];

    if (g_driver.api.pDrawPolygon == NULL) return;

    if (g_driver.api.nSoftwareMode == 1) {
        v0->x = (int)((float)v0->x * g_gxScaleX);
        v0->y = (int)((float)v0->y * g_gxScaleY);
        v1->x = (int)((float)v1->x * g_gxScaleX);
        v1->y = (int)((float)v1->y * g_gxScaleY);
        v2->x = (int)((float)v2->x * g_gxScaleX);
        v2->y = (int)((float)v2->y * g_gxScaleY);
        v3->x = (int)((float)v3->x * g_gxScaleX);
        v3->y = (int)((float)v3->y * g_gxScaleY);
    }

    if (((unsigned)flags & 4) == 4) {
        /* Color/uv repack: copy bytes 0..3,4..5,8..9 then 0xd,0xf,0x11,
         * 0x13,0x15,0x17,0x19,0x1b of colorUv into a local 0x10-byte
         * struct (see decompile of gxDrawPolygon @0x433440). */
        const unsigned char *src = (const unsigned char *)colorUv;
        unsigned char *dst = local;
        memcpy(dst + 0,  src + 0,  4);
        memcpy(dst + 4,  src + 4,  2);
        memcpy(dst + 6,  src + 8,  2);
        dst[8]  = src[0xd];
        dst[9]  = src[0xf];
        dst[10] = src[0x11];
        dst[11] = src[0x13];
        dst[12] = src[0x15];
        dst[13] = src[0x17];
        dst[14] = src[0x19];
        dst[15] = src[0x1b];
    }
    /* The original always passes its local record, even when flags bit 2 is
     * clear; the driver ignores the unused fields in that case. */
    g_driver.api.pDrawPolygon(v0, v1, v2, v3, flags, &local[0]);
}

/* gxBlitSurface @0x433580 — gated on nDrawEnabled (decompiled
 * GxDriverApi.nDrawEnabled), dispatches with 9 args. */
int gxBlitSurface(int a, int b, int c, void *tex, int x, int y,
                  int w, int h, int h2)
{
    if (g_driver.api.nDrawEnabled == 0) return 0;
    return g_driver.api.pBlitSurface(a, b, c, tex, x, y, w, h, h2);
}

/* gxSetOrigin @0x4335d0 */
void gxSetOrigin(int packedOrigin)
{
    if (g_driver.api.pSetOrigin != NULL) {
        g_driver.api.pSetOrigin(packedOrigin);
    }
}

/* gxDrawTriangle @0x4335f0 */
void gxDrawTriangle(void *v0, int color)
{
    if (g_driver.api.pDrawTriangle != NULL) {
        g_driver.api.pDrawTriangle(v0, color);
    }
}

/* gxDrawLine @0x433610 — gate on pDrawTriangle (decompiled wrapper checks
 * GxDriverApi.pDrawTriangle), dispatch via pDrawLine. */
void gxDrawLine(void *v0, void *v1, int color)
{
    if (g_driver.api.pDrawTriangle != NULL) {
        g_driver.api.pDrawLine(v0, v1, color);
    }
}

/* gxDrawTriUV @0x433640 — gate on pDrawTriangle, dispatch via pDrawTriUV. */
void gxDrawTriUV(void *v0, void *v1, void *v2, int color, void *uv)
{
    if (g_driver.api.pDrawTriangle != NULL) {
        g_driver.api.pDrawTriUV(v0, v1, v2, color, uv);
    }
}

/* gxDrawQuad @0x433670 — gate on pDrawTriangle, dispatch via pDrawQuad. */
void gxDrawQuad(void *v0, void *v1, void *v2, void *v3, int color, void *uv)
{
    if (g_driver.api.pDrawTriangle != NULL) {
        g_driver.api.pDrawQuad(v0, v1, v2, v3, color, uv);
    }
}

/* gxDrawQuadColor @0x414470 — colored textured quad helper used by Rekord
 * screen (and others). Original builds GxVert[4] at x0*0x100 etc, y*0x100,
 * z 0, r=g=b=0xff, and GxColorUv with U=u0*0x100 etc, then gxDrawPolygon
 * @0x433440 with flags 0x2004. Replicates the original's fixed-point math. */
void gxDrawQuadColor(void *tex,int x0,int y0,int x1,int y1,int u0,int v0,int u1,int v1)
{
    GxVert v00,v01,v02,v03; GxColorUv uv;
    v00.x = x0 << 8; v00.y = y0 << 8; v00.z = 0; v00.r=v00.g=v00.b=0xff;
    v01.x = x1 << 8; v01.y = y0 << 8; v01.z = 0; v01.r=v01.g=v01.b=0xff;
    v02.x = x1 << 8; v02.y = y1 << 8; v02.z = 0; v02.r=v02.g=v02.b=0xff;
    v03.x = x0 << 8; v03.y = y1 << 8; v03.z = 0; v03.r=v03.g=v03.b=0xff;
    uv.pTexture = tex; uv.pParam5 = NULL; uv.pad = 0;
    uv.U = (unsigned short)(u0 << 8); uv.U2 = uv.U;
    uv.V = (unsigned short)(v0 << 8); uv.V2 = uv.V;
    uv.gwU = (unsigned short)(u1 << 8); uv.gwU2 = uv.gwU;
    uv.hV = (unsigned short)(v1 << 8); uv.hV2 = uv.hV;
    gxDrawPolygon(&v00,&v01,&v02,&v03,0x2004,&uv);
}

/* ===================================================================
 * 2D vector helpers
 * =================================================================== */

/* gxVec2SetAngleZero @0x434f90 — set to unit X {1.0f, 0.0f}. Moved here
 * from game.c so the zone/geometry cluster can share it. */
void gxVec2SetAngleZero(GxVec2 *pVec) /* @0x434f90 */
{
    pVec->x = 1.0f;
    pVec->y = 0.0f;
}

/* gxVec2Set @0x434fa0 — store (x, y). Original __thiscall RET 0x8. */
void gxVec2Set(GxVec2 *pVec, float x, float y) /* @0x434fa0 */
{
    pVec->x = x;
    pVec->y = y;
}

/* gxVec2FromPolar @0x434fc0 — pPolar holds {length, angle}; writes
 * cartesian out = length * (cos(angle), sin(angle)) via FSIN/FCOS. */
void gxVec2FromPolar(GxVec2 *pOut, const GxVec2 *pPolar) /* @0x434fc0 */
{
    pOut->x = pPolar->x * (float)cos((double)pPolar->y);
    pOut->y = pPolar->x * (float)sin((double)pPolar->y);
}
