/* test_quit_input.c — SDL2 port: the quit-confirm SDL_QUIT path.
 * stateQuitConfirm @0x4200b0 confirms J/Y character events (routed from
 * SDL_TEXTINPUT by platformPumpEvents); confirmation pushes an SDL_QUIT
 * event via platformRequestQuit. Other keys return to menuUpdate with no
 * quit event. Needs only the SDL events subsystem (headless-safe). */

#include <SDL2/SDL.h>

#include "../src/menu.h"
#include "../src/platform_sdl2.h"

static int drainQuit(void)
{
    SDL_Event ev;
    int found = 0;
    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) == 1)
        found = 1;
    return found;
}

int main(void)
{
    if (SDL_Init(SDL_INIT_EVENTS) != 0) return 100;

    g_pStateFunc = stateQuitConfirm;
    g_hMenuQuitTex = NULL;

    /* A physical J produces keydown + text input; the character confirms. */
    if (drainQuit()) return 1;
    stateQuitConfirm(1, 'j', 0);
    if (!drainQuit()) return 2;

    /* 'n' returns to the menu with no quit event. */
    g_pStateFunc = stateQuitConfirm;
    stateQuitConfirm(1, 'n', 0);
    if (g_pStateFunc != menuUpdate) return 3;
    if (drainQuit()) return 4;

    SDL_Quit();
    return 0;
}
