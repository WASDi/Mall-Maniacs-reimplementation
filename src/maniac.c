#include <windows.h>
#include <stdio.h>
#include "gx.h"
#include "input.h"
#include "custom_helpers.h"
#include "menu.h"
#include "options.h"
#include "gameplay.h"
#include "stubs.h"
#include "time.h"
void gameInit(void); /* @0x409d90 — defined in game.c */

/* =====================================================================
 * Mall Maniacs (maniac.exe) replacement — main translation unit. Compiled
 * to maniac_rebuild.exe.
 *
 * Reimplements a narrow slice of the original:
 *   - WinMain @0x4160a0           (simplified: no DirectInput, no net loop)
 *   - initWindowAndInput @0x4165f0 (window class + creation only)
 *   - WindowProc @0x4161b0         (close/escape + records key state)
 *   - gxLoadDriver/gxInit/gxLoadTexture/presentFrame (see gx.c)
 *   - menuInit/introUpdate/menuUpdate/stateQuitConfirm (see menu.c)
 * ===================================================================== */

HWND      g_hWnd;          /* maniac g_hMainWindow @0x459ce0 */
HINSTANCE g_hAppInstance;        /* maniac g_hAppInstance @0x459cdc */
static int       g_bRunning = 1;  /* rebuild loop state; no original global */

/* WindowProc @0x4161b0 — close/escape, focus suspend/resume and key
 * recording. The original routes inactive-menu keydowns through DirectInput
 * polling and dispatches WM_CHAR immediately. The rebuild records the mapped
 * key state here and lets pollKeyboard @0x416a10 (called from gameFrameUpdate
 * after the message batch) apply the original debounce and dispatch the
 * events; this preserves the original ordering where DirectInput polling
 * follows the message batch, preventing the translated character for Enter
 * from being consumed by the newly selected quit-confirm state.
 * WM_ACTIVATE @0x4161f6 / WM_ACTIVATEAPP @0x416287 mirror the original focus
 * handling: set g_nFrameDue (freezes the game clock), snooze the renderer on
 * deactivation and re-init the GX mode on reactivation. */
static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_KEYDOWN:                      /* 0x100 */
    case WM_KEYUP:                        /* 0x101 */
    case WM_CHAR:                         /* 0x102 */
    default:
        break;
    }
    switch (uMsg) {
    case WM_CLOSE:                        /* 0x10 */
        /* The original lets DefWindowProc destroy the window; WM_DESTROY
         * posts WM_QUIT after the window teardown. */
        break;
    case WM_KEYDOWN:                      /* 0x100 */
        {
            int key = -1;

            /* Record the held key (0x80 state byte, as the original DirectInput
             * poll reads). pollKeyboard @0x416a10 debounces and dispatches it. */
            switch (wParam) {
            case VK_RIGHT:  key = 0; break;
            case VK_LEFT:   key = 1; break;
            case VK_UP:     key = 2; break;
            case VK_DOWN:   key = 3; break;
            case VK_SPACE:  key = 4; break;
            case VK_RETURN: key = 6; break;
            case VK_ESCAPE: key = 7; break;
            }
            if (key >= 0) g_abInputKeyHeld[key] = 0x80;
        }
        break;
    case WM_KEYUP:                        /* 0x101 */
        {
            int key = -1;

            switch (wParam) {
            case VK_RIGHT:  key = 0; break;
            case VK_LEFT:   key = 1; break;
            case VK_UP:     key = 2; break;
            case VK_DOWN:   key = 3; break;
            case VK_SPACE:  key = 4; break;
            case VK_RETURN: key = 6; break;
            case VK_ESCAPE: key = 7; break;
            }
            if (key >= 0) g_abInputKeyHeld[key] = 0;
        }
        break;
    case WM_CHAR:                         /* 0x102 */
        /* Original WindowProc @0x4163b0: while a round is active the
         * translated character goes to gameKeyHandler @0x40db80 as
         * (char, 0) — the J/Y confirm / N resume / quest answers; only
         * menu states receive it through dispatchKeyEvent. */
        if (g_bGameActive != 0) {
            gameKeyHandler((int)wParam, 0);
        }
        else if (g_pStateFunc != NULL) {
            g_pStateFunc(1, (int)wParam, 0);
        }
        break;
    case WM_DESTROY:                      /* 0x2 */
        g_hWnd = NULL;
        PostQuitMessage(0);
        return 0;
    case WM_ACTIVATE:                     /* 0x6 @0x4161f6 */
        /* WA_INACTIVE (low word 0) freezes the clock and snoozes the
         * renderer once; any active state advances the clock and re-inits
         * GX if it had been snoozed. */
        g_nFrameDue = (int)(wParam & 0xffff);
        if ((wParam & 0xffff) == 0) {
            if (g_nGameFrameActive == 0) {
                gxSnooze();                            /* @0x433330 @0x416273 */
                g_nGameFrameActive = 1;                /* @0x45834c @0x416278 */
            }
        }
        else {
            /* Original @0x416206 saves the clock cache, calls getGameTime
             * (advancing g_nClock), reads g_nGameFrameActive and restores
             * the cache before the GX re-init. */
            int nSavedClockCache = g_nClockCache;
            getGameTime();                             /* @0x40dfe0 @0x41620c */
            if (g_nGameFrameActive != 0) {             /* @0x45834c @0x416211 */
                GxMode mode;
                mode.width = 0x280;                    /* @0x416233 */
                mode.height = 0x1e0;                   /* @0x41623b */
                mode.bpp = 0x10;                       /* @0x416242 */
                mode.hInstance = (unsigned int)(size_t)g_hAppInstance; /* @0x416247 */
                mode.hwnd = (unsigned int)(size_t)g_hWnd;              /* @0x41624b */
                gxInit(&mode);                         /* @0x4332f0 @0x41624f */
                g_nGameFrameActive = 0;                /* @0x416257 */
            }
            g_nClockCache = nSavedClockCache;          /* @0x416216 */
        }
        break;
    case WM_ACTIVATEAPP:                  /* 0x1c @0x416287 */
        g_nFrameDue = (int)wParam;        /* @0x459cd4 */
        break;
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
    wc.hInstance     = g_hAppInstance;
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
                             NULL, NULL, g_hAppInstance, NULL);
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
 * timeline then main menu), clean close. The original calls gameInit @0x409d90
 * which handles gxLoadDriver + gxInit (menuInit is entered lazily from
 * gameFrameUpdate @0x41a8c0). */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd)
{
    MSG    msg;

    (void)hPrevInstance;
    (void)lpCmdLine;

    g_hAppInstance = hInstance;
    appLog("[winmain] === start ===");

    if (!initWindowAndInput(nShowCmd)) {
        appLog("[winmain] initWindowAndInput failed");
        return 1;
    }

    /* gameInit @0x409d90 — original WinMain calls this; it handles
     * gxLoadDriver + gxInit (NOT menuInit — menuInit is entered lazily
     * from gameFrameUpdate/dispatchKeyEvent). */
    gameInit();
    appLog("[winmain] gameInit done (gfxMode=%d)", g_nGfxMode);
    /* State is not set by gameInit; first frame's gameFrameUpdate will
     * call menuInit(0) lazily as in the original. */
    appLog("[winmain] running intro timeline (6 logos)");

    /* Message loop + frame update — the idle path mirrors the original
     * WinMain: after the message batch, each iteration advances one game
     * frame via gameFrameUpdate @0x41a8c0, which polls the keyboard
     * (pollKeyboard @0x416a10 -> dispatchKeyEvent @0x41ade0) and then runs
     * the menu background + state update + flip/clear, with its own 25ms
     * timing gate. */
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

        if (g_bGameActive != 0) {
            gameRunFrame(0);
        } else {
            gameFrameUpdate();
        }

        /* Yield the CPU between frames: the fixed-step gate inside
         * gameFrameUpdate returns without work most iterations, and
         * without this the pump spins one core at 100%. ~10 ms keeps the
         * menu/gameplay frame pacing smooth (input polled every turn). */
        Sleep(10);
    }

    appLog("[winmain] exiting cleanly");

    /* Original teardown (0x4162aa..0x4162d5): if a round is still live,
     * end it before shutting the renderer down; otherwise unload the
     * menu/game world first. Both paths then call shutdownRenderer
     * @0x40a490 (save + moveState free + gxUnloadDriver). */
    if (g_bGameActive != 0) {
        roundTeardown();
        shutdownRenderer();
    } else {
        unloadGameWorld();
        shutdownRenderer();
    }

    if (g_hWnd != NULL) {
        HWND hWnd = g_hWnd;
        g_hWnd = NULL;
        DestroyWindow(hWnd);
    }
    appLog("[winmain] === done ===\n");
    return 0;
}
