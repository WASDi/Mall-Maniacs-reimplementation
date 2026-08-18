#include <windows.h>
#include <stdio.h>
#include "gx.h"
#include "custom_helpers.h"
#include "menu.h"

/* =====================================================================
 * Mall Maniacs (maniac.exe) replacement — main translation unit. Compiled
 * to maniac_rebuild.exe per Rebuild.md milestones.
 *
 * Reimplements a narrow slice of the original:
 *   - WinMain @0x4160a0           (simplified: no DirectInput, no net loop)
 *   - initWindowAndInput @0x4165f0 (window class + creation only)
 *   - WindowProc @0x4161b0         (close/escape + key events to state fn)
 *   - gxInit/gxLoadTexture/presentFrame  (see gx.c)
 *   - menuInit/introUpdate/menuUpdate/stateQuitConfirm (see menu.c)
 * ===================================================================== */

static HWND      g_hWnd;          /* maniac g_hMainWindow @0x459ce0 */
static HINSTANCE g_hInstance;     /* maniac g_hAppInstance @0x459cdc */

/* WindowProc @0x4161b0 — narrowed to close/escape for the slice. The
 * original also routes keyboard to gameKeyHandler / DirectInput polling;
 * we forward every keydown to the state function (dispatchKeyEvent analog,
 * see 0x41ade0): intro skips, the menu navigates, Escape opens the
 * quit-confirm screen. */
static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_CLOSE:                        /* 0x10 */
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:                      /* 0x100 */
        if (g_pStateFunc != NULL) {
            g_pStateFunc(1, vkToKeyId((int)wParam), 2);
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
static int initWindowAndInput(int nShowCmd)
{
    WNDCLASSEXA wc;
    static const char *kClassName = "MallManiacsRebuild";

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_HREDRAW | CS_VREDRAW;  /* original style = 3 */
    wc.lpfnWndProc   = WindowProc;
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
    ShowWindow(g_hWnd, nShowCmd);
    UpdateWindow(g_hWnd);
    appLog("[init] window created (hwnd=%08x)", (unsigned int)g_hWnd);
    return 1;
}

/* WinMain @0x4160a0 — simplified: window, driver, state loop (intro
 * timeline then main menu), clean close. */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd)
{
    GxMode mode;
    MSG    msg;
    DWORD  now;

    (void)hPrevInstance;
    (void)lpCmdLine;

    g_hInstance = hInstance;
    appLog("[winmain] === start ===");

    if (!initWindowAndInput(nShowCmd)) {
        appLog("[winmain] initWindowAndInput failed");
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

    menuInit(0);
    if (g_pStateFunc == NULL) {
        appLog("[winmain] state setup failed");
        goto out;
    }
    appLog("[winmain] running intro timeline (6 logos)");

    /* Message loop + frame update — mirrors the original WinMain idle path
     * (gameFrameUpdate timing, see 0x41a8c0): the state advances at most
     * every 25ms with g_flFrameDelta = elapsed ms * 0.04, then menuFramePost
     * flips/clears for the menu states. */
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

        now = timeGetTime();
        if ((int)(now - g_nLastFrameTime) >= 0x19) {
            g_flFrameDelta = (float)(now - g_nLastFrameTime) * 0.04f;
            g_nLastFrameTime = now;
            if (g_pStateFunc != NULL) {
                g_pStateFunc(0, 0, 0);
                menuFramePost();
            }
        }
    }

    appLog("[winmain] exiting cleanly");

out:
    gxShutdown();
    if (g_hWnd) DestroyWindow(g_hWnd);
    appLog("[winmain] === done ===\n");
    return 0;
}
