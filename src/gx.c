#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gx.h"
#include "pool.h"
#include "util.h"

/* Vertical-slice GX driver adapter. Reimplements the narrow slice of the
 * maniac-side gx* wrappers that the GUI vertical slice needs. Driver is the
 * original DRIVERS\GXSOFT.DLL (software rasterizer), loaded directly (the
 * original gxLoadDriver @0x432ea0 reads the driver path + options from the
 * registry; that configuration path is deferred per Rebuild.md). */

/* gxDLLInit / gxDLLExit are cdecl exports of gxSoft.dll. */
typedef int  (__cdecl *pfn_gxDLLInit)(GxDriverApi *api);
typedef void (__cdecl *pfn_gxDLLExit)(void);

/* gxInit @0x4332f0 — load GXSOFT.DLL, gxDLLInit, pSetMode (registry path
 * deferred per Rebuild.md; file name is known). */
int gxInit(GxMode *mode)
{
    HMODULE         hMod;
    pfn_gxDLLInit   pInit;
    GxDriverApi    *api = &g_driver.api;

    /* gxLoadDriver @0x432ea0: registry-sourced path; we use the known file. */
    hMod = LoadLibraryA("DRIVERS\\GXSOFT.DLL");
    if (hMod == NULL) {
        fprintf(stderr, "[gxInit] LoadLibraryA(DRIVERS\\GXSOFT.DLL) failed: %lu\n",
                (unsigned long)GetLastError());
        return 0;
    }
    g_driver.hDriverModule = hMod;

    memset(api, 0, sizeof(*api));

    pInit = (pfn_gxDLLInit)GetProcAddress(hMod, "gxDLLInit");
    if (pInit == NULL) {
        fprintf(stderr, "[gxInit] gxDLLInit not found\n");
        goto fail;
    }
    if (pInit(api) == 0) {
        fprintf(stderr, "[gxInit] gxDLLInit failed\n");
        goto fail;
    }

    if (api->pSetMode == NULL || api->pSetMode(mode) == 0) {
        fprintf(stderr, "[gxInit] pSetMode failed\n");
        goto fail;
    }
    /* gxLoadDriver @0x432ea0 zeroes nSoftwareMode then nDriverActive after a
     * successful gxDLLInit (software mode check is off; texture loads drive
     * nDriverActive). */
    g_driver.api.nSoftwareMode = 0;
    g_driver.nDriverActive = 0;

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

/* gxShutdown @0x432880 (gxUnloadDriver) — gxDLLExit + FreeLibrary. */
void gxShutdown(void)
{
    HMODULE hMod = g_driver.hDriverModule;

    if (hMod != NULL) {
        /* gxUnloadDriver @0x432880: gxDLLExit then FreeLibrary. */
        pfn_gxDLLExit pExit = (pfn_gxDLLExit)GetProcAddress(hMod, "gxDLLExit");
        if (pExit != NULL) pExit();
        FreeLibrary(hMod);
    }
    g_driver.hDriverModule = NULL;
    g_driver.nDriverActive = 0;
    memset(&g_driver.api, 0, sizeof(g_driver.api));
}

/* gxLoadTexture @0x4333d0 — maniac wrapper around GxDriverApi.pLoadTexture.
 * param_1 selects the .tpg load/free action; when loading (data!=0, mode==0)
 * bumps nDriverActive, when freeing (data==0, mode!=0) decrements it. */
int gxLoadTexture(int mode, int reserved, const char *name, void *data,
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
void presentFrame(void *texture)
{
    
    if (texture != NULL) {
        gxBlitSurface(1, 0, 0, texture, 0, 0, 0x280, 0x280, 0x1e0);
        gxFlip();
        gxClearScreen(1, 0);
    }
}

/* gxLoadTpgFile @0x416060 — fileReadRaw(0,path) -> gxLoadTexture(0,1,path,
 * data,data+0x10000) -> memPoolFree(0,data); returns texture handle. Uses
 * pool 0 ("DEFAULT", created by memPoolSystemInit). */
int gxLoadTpgFile(const char *path)
{
    char *data;
    int r;

    data = fileReadRaw(0, path);
    if (data == NULL) return 0;
    r = gxLoadTexture(0, 1, path, data, data + 0x10000);
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
int gxCreateSurface(const char *path)
{
    if (g_driver.api.pCreateSurface != NULL) {
        return g_driver.api.pCreateSurface(path);
    }
    return 0;
}

/* gxDrawPolygon @0x433440 — in software mode (nSoftwareMode==1) the four
 * vertex pointers are (x,y) float pairs truncated to int in place before
 * dispatch. flags bit 2 (value 4) triggers a color/uv repack of param_6 into
 * a local 0x20-byte buffer. */
void gxDrawPolygon(void *v0, void *v1, void *v2, void *v3, int flags,
                   void *colorUv)
{
    if (g_driver.api.pDrawPolygon == NULL) return;

    if (g_driver.api.nSoftwareMode == 1) {
        float *f[4] = { (float *)v0, (float *)v1, (float *)v2, (float *)v3 };
        int i;
        for (i = 0; i < 4; i++) {
            if (f[i] != NULL) {
                f[i][0] = (float)(int)f[i][0];
                f[i][1] = (float)(int)f[i][1];
            }
        }
    }

    if (((unsigned)flags & 4) == 4) {
        /* Color/uv repack: copy bytes 0..3,4..5,8..9 then 0xd,0xf,0x11,
         * 0x13,0x15,0x17,0x19,0x1b of colorUv into a local 0x20-byte
         * struct (see decompile of gxDrawPolygon @0x433440). */
        unsigned char local[0x20];
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
        g_driver.api.pDrawPolygon(v0, v1, v2, v3, flags, &local[0]);
        return;
    }

    g_driver.api.pDrawPolygon(v0, v1, v2, v3, flags, colorUv);
}

/* gxBlitSurface @0x433580 — gated on nDrawEnabled (decompiled
 * GxDriverApi.nDrawEnabled), dispatches with 9 args. */
int gxBlitSurface(int a, int b, int c, void *tex, int x, int y,
                  int w, int h, int h2)
{
    if (g_driver.api.nDrawEnabled == 0) return 0;
    if (g_driver.api.pBlitSurface == NULL) return 0;
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
