#include "compat_types.h"
#include <stdio.h>
#include "gx.h"
#include "input.h"
#include "custom_helpers.h"
#include "menu.h"
#include "options.h"
#include "gameplay.h"
#include "stubs.h"
#include "time.h"
#include "platform_sdl2.h"
void gameInit(void); /* @0x409d90 — defined in game.c */

/* =====================================================================
 * Mall Maniacs (maniac.exe) replacement — SDL2 entry point.
 *
 * WinMain @0x4160a0 -> int main(argc, argv): SDL2 window 640x480 + GL 3.3
 * core context (platform_sdl2). The SDL_PollEvent pump replaces the
 * PeekMessageA loop; scancode mapping + WM_CHAR routing + WM_ACTIVATE
 * focus handling live in platformPumpEvents (input.c polling logic is
 * untouched). gameInit @0x409d90 still owns gxLoadDriver + gxInit.
 * ===================================================================== */

extern void *g_hWnd;          /* @0x459ce0 (defined in platform_sdl2.c) */
extern void *g_hAppInstance;  /* @0x459cdc (defined in platform_sdl2.c) */
static int g_bRunning = 1;  /* rebuild loop state; no original global */

/* initWindowAndInput @0x4165f0 — SDL2 window + GL context (platform). */
static int initWindowAndInput(int argc, char **argv)
{
    if (!platformInit(argc, argv)) {
        appLog("[init] platformInit failed");
        return 0;
    }
    appLog("[init] window created (sdl)");
    return 1;
}

/* main — SDL2 port of WinMain @0x4160a0. */
int main(int argc, char **argv)
{
    appLog("[winmain] === start ===");

    if (!initWindowAndInput(argc, argv)) {
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

    /* Event pump + frame update — mirrors the original WinMain idle path:
     * after the event batch, each iteration advances one game frame via
     * gameFrameUpdate @0x41a8c0 (pollKeyboard @0x416a10 ->
     * dispatchKeyEvent @0x41ade0) or gameRunFrame while a round is live. */
    while (g_bRunning) {
        if (!platformPumpEvents()) {
            g_bRunning = 0;
            break;
        }
        if (g_bGameActive != 0) {
            gameRunFrame(0);
        } else {
            gameFrameUpdate();
        }
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

    platformShutdown();
    g_hWnd = NULL;
    appLog("[winmain] === done ===\n");
    return 0;
}
