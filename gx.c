#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gx.h"

/* Vertical-slice GX driver adapter. Reimplements the narrow slice of the
 * maniac-side gx* wrappers that the GUI vertical slice needs. Driver is the
 * original DRIVERS\GXSOFT.DLL (software rasterizer), loaded directly (the
 * original gxLoadDriver @0x432ea0 reads the driver path + options from the
 * registry; that configuration path is deferred per Rebuild.md). */

static GxDriver g_driver;

/* gxDLLInit / gxDLLExit are cdecl exports of gxSoft.dll. */
typedef int  (__cdecl *pfn_gxDLLInit)(GxDriverApi *api);
typedef void (__cdecl *pfn_gxDLLExit)(void);

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
    g_driver.nDriverActive = 1;

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

int gxLoadTexture(const char *name, void *data)
{
    /* menuInit @0x419c20: gxLoadTexture(0,1,name,data,data+0x10000).
     * data = .tpg file bytes (0x10000 pixels + 0x400 palette). */
    if (g_driver.api.pLoadTexture == NULL) return 0;
    return g_driver.api.pLoadTexture(0, 1, name, data, (char *)data + 0x10000);
}

void presentFrame(void *texture)
{
    /* presentFrame @0x410310:
     *   gxBlitSurface(1,0,0,tex,0,0,0x280,0x280,0x1e0); gxFlip(); gxClearScreen(1,g_nClearColor);
     * g_nClearColor @0x45892c is 0. */
    if (texture != NULL && g_driver.api.pBlitSurface != NULL) {
        g_driver.api.pBlitSurface(1, 0, 0, texture, 0, 0, 0x280, 0x280, 0x1e0);
        if (g_driver.api.pFlip != NULL) g_driver.api.pFlip();
        if (g_driver.api.pClearScreen != NULL) g_driver.api.pClearScreen(1, 0);
    }
}