#include <stdio.h>
#include "stubs.h"
#include "gx.h"
#include "util.h"
#include "menu.h"

/* =====================================================================
 * TODO stubs — see stubs.h for contracts. Safe, log-and-return-failure
 * placeholders so future milestones can wire real implementations without
 * changing callers. Original addresses noted in stubs.h.
 * ===================================================================== */

/* gameInit @0x409d90 — full game init. TODO stub: log + return 0. */
int gameInit(void)
{
    appLog("[stub TODO] gameInit @0x409d90 not implemented (returns 0)");
    return 0;
}

/* gameFrameUpdate @0x41a8c0 — advance one game frame. TODO stub. */
int gameFrameUpdate(void)
{
    appLog("[stub TODO] gameFrameUpdate @0x41a8c0 not implemented (returns 0)");
    return 0;
}

/* pollKeyboard @0x416a10 — DirectInput keyboard poll. TODO stub; the slice
 * uses window messages instead. */
int pollKeyboard(void)
{
    appLog("[stub TODO] pollKeyboard @0x416a10 not implemented (returns 0)");
    return 0;
}

/* shutdownRenderer — original gxUnloadDriver @0x432880 path (see gx.c). */
void shutdownRenderer(void)
{
    /* Implemented via gx.c (gxUnloadDriver @0x432880). */
    gxShutdown();
}

/* Main-menu row targets (menu.c dispatch table). TODO stubs: log once and
 * return to the menu. Replace the bodies later without changing the
 * interfaces or the dispatch table. */

/* stateGameTypeSelect @0x41c010 — game-type select screen (menu "Spela").
 * Original: 4 modes (Varujakten/Matkrig/Frögesporten/Vagnrace) -> modeInit*;
 * ESC -> menuUpdate. TODO stub: log + return to menu. */
int stateGameTypeSelect(int nType, int nKey, int nKeyType)
{
    static int bLogged;
    (void)nType; (void)nKey; (void)nKeyType;
    if (!bLogged) {
        bLogged = 1;
        appLog("[stub TODO] stateGameTypeSelect @0x41c010 not implemented (back to menu)");
    }
    g_pStateFunc = menuUpdate;
    return 0;
}

/* stateNetworkMenu @0x420190 — network/lobby menu. Original: host/join lobby
 * states; ESC tail -> menuUpdate. TODO stub: log + return to menu. */
int stateNetworkMenu(int nType, int nKey, int nKeyType)
{
    static int bLogged;
    (void)nType; (void)nKey; (void)nKeyType;
    if (!bLogged) {
        bLogged = 1;
        appLog("[stub TODO] stateNetworkMenu @0x420190 not implemented (back to menu)");
    }
    g_pStateFunc = menuUpdate;
    return 0;
}

/* gotoOptions @0x41d300 — menu "Alternativ". Original: copies g_nGfxMode into
 * g_nRendererMode @0x45a390 then g_pStateFunc = stateOptions @0x41c6a0.
 * TODO stub: log + return to menu. */
int gotoOptions(int nType, int nKey, int nKeyType)
{
    static int bLogged;
    (void)nType; (void)nKey; (void)nKeyType;
    if (!bLogged) {
        bLogged = 1;
        appLog("[stub TODO] gotoOptions @0x41d300 not implemented (back to menu)");
    }
    g_pStateFunc = menuUpdate;
    return 0;
}

/* stateHighScoreTable @0x41dfd0 — records / high-score table (menu "Rekord").
 * Original: draws 2 rows of the fshi table via commandDispatch; keys 6/7 ->
 * menuUpdate. TODO stub: log + return to menu. */
int stateHighScoreTable(int nType, int nKey, int nKeyType)
{
    static int bLogged;
    (void)nType; (void)nKey; (void)nKeyType;
    if (!bLogged) {
        bLogged = 1;
        appLog("[stub TODO] stateHighScoreTable @0x41dfd0 not implemented (back to menu)");
    }
    g_pStateFunc = menuUpdate;
    return 0;
}