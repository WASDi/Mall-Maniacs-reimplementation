#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "gx.h"
#include "util.h"

/* =====================================================================
 * Mall Maniacs (maniac.exe) replacement — GUI vertical slice.
 * Compiled to maniac_rebuild.exe per Rebuild.md milestone 1.
 *
 * Reimplements a narrow slice of the original:
 *   - WinMain @0x4160a0           (simplified: no DirectInput, no net loop)
 *   - initWindowAndInput @0x4165f0 (window class + creation only)
 *   - WindowProc @0x4161b0         (WM_CLOSE/Escape only; other msgs -> DefWindowProc)
 *   - gxInit/gxLoadTexture/presentFrame  (see gx.c)
 *   - menuInit @0x419c20           (asset loads needed for the slice)
 * ===================================================================== */

static HWND    g_hWnd;          /* maniac g_hMainWindow @0x459cd0 */
static HINSTANCE g_hInstance;   /* maniac g_hAppInstance @0x459cdc */
static void   *g_pIntroTex;     /* maniac g_hIntroTexAddgames @0x45a618 */
static int     g_bRunning = 1;

/* WindowProc @0x4161b0 — narrowed to close/escape for the slice. The original
 * also routes keyboard to gameKeyHandler / DirectInput polling. */
static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_CLOSE:                        /* 0x10 */
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:                      /* 0x100 */
        if (wParam == VK_ESCAPE) {        /* 0x1b */
            PostQuitMessage(0);
            return 0;
        }
        break;
    case WM_DESTROY:                      /* 0x2 */
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

/* initWindowAndInput @0x4165f0 — window class + creation only (DirectInput
 * deferred; input via window messages for this milestone). */
static int initWindow(void)
{
    WNDCLASSEXA wc;
    static const char *kClassName = "MallManiacsRebuild";

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_HREDRAW | CS_VREDRAW;  /* original style = 3 */
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = g_hInstance;
    wc.hCursor       = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;

    if (!RegisterClassExA(&wc)) {
        appLog("[init] RegisterClassExA failed: %lu", (unsigned long)GetLastError());
        return 0;
    }

    /* Original CreateWindowExA style 0xcf0000 (WS_POPUP|WS_VISIBLE|WS_SYSMENU).
     * Create a fixed 640x480 window for the slice. */
    g_hWnd = CreateWindowExA(0, kClassName, "Mall Maniacs",
                             0xcf0000, 0, 0, 640, 480,
                             NULL, NULL, g_hInstance, NULL);
    if (g_hWnd == NULL) {
        appLog("[init] CreateWindowExA failed: %lu", (unsigned long)GetLastError());
        return 0;
    }
    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);
    appLog("[init] window created (hwnd=%08x)", (unsigned int)g_hWnd);
    return 1;
}

/* menuInit @0x419c20 — the asset loads required for the intro-logo screen:
 * intro_addgames.tga pixels + a .tpg to install the driver palette. */
static int loadIntroAssets(void)
{
    size_t size = 0;
    void  *tpg;

    /* First .tpg load installs the DirectDraw palette (gxLoadTexture
     * @0x100019b0, first-call branch). MERGED00 shares the intro palette
     * (249/256 entries), matching menuInit's order. */
    tpg = readFileAlloc("menu\\MERGED00.TPG", &size);
    if (tpg == NULL) {
        appLog("[assets] menu\\MERGED00.TPG missing");
        return 0;
    }
    if (size < 0x10400) {
        appLog("[assets] menu\\MERGED00.TPG too small (%u bytes)", (unsigned)size);
        free(tpg);
        return 0;
    }
    gxLoadTexture("MERGED00", tpg);
    appLog("[assets] MERGED00.TPG loaded (%u bytes, palette set)", (unsigned)size);
    free(tpg);

    g_pIntroTex = loadTga640x480("menu\\intro_addgames.tga");
    if (g_pIntroTex == NULL) {
        appLog("[assets] intro_addgames.tga load failed");
        return 0;
    }
    appLog("[assets] intro_addgames.tga -> 640x480 index buffer @%p", g_pIntroTex);
    return 1;
}

/* WinMain @0x4160a0 — simplified: window, driver, intro logo loop, clean close. */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    GxMode mode;
    MSG    msg;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    g_hInstance = hInstance;
    appLog("[winmain] === start ===");

    if (!initWindow()) {
        appLog("[winmain] initWindow failed");
        return 1;
    }

    /* Mode struct as built by gameInit @0x40a0cd: width/height/bpp/hInstance/hwnd. */
    mode.width    = 0x280;
    mode.height   = 0x1e0;
    mode.bpp      = 0x10;
    mode.hInstance = (unsigned int)g_hInstance;
    mode.hwnd     = (unsigned int)g_hWnd;

    if (!gxInit(&mode)) {
        appLog("[winmain] gxInit failed");
        goto out;
    }
    appLog("[winmain] gxSetMode done (640x480, bpp forced by driver)");

    if (!loadIntroAssets()) {
        appLog("[winmain] asset load failed");
        goto out;
    }
    appLog("[winmain] presenting intro_addgames.tga continuously");

    /* Message loop — present the static logo every iteration. */
    while (g_bRunning) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_bRunning = 0;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!g_bRunning) break;
        presentFrame(g_pIntroTex);
    }

    appLog("[winmain] exiting cleanly");

out:
    if (g_pIntroTex) free(g_pIntroTex);
    gxShutdown();
    if (g_hWnd) DestroyWindow(g_hWnd);
    appLog("[winmain] === done ===\n");
    return 0;
}