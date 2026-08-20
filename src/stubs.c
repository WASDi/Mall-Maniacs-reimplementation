#include <stdio.h>
#include "stubs.h"
#include "menu.h"
#include "custom_helpers.h"

/* =====================================================================
 * TODO stubs — see stubs.h for contracts. Safe, logged no-op or placeholder
 * transitions let future milestones wire real implementations without
 * changing callers. Original addresses noted in stubs.h.
 * ===================================================================== */

/* gameInit @0x409d90 — full game init. TODO replacement: log + no-op.
 * Original signature is void(void); this stub deliberately makes
 * no success claim because the replacement WinMain bypasses gameInit. */
void gameInit(void)
{
    static int bLogged;
    if (!bLogged) {
        bLogged = 1;
        appLog("[stub TODO] gameInit @0x409d90 not implemented (no-op)");
    }
}

/* Main-menu row targets (menu.c dispatch table). TODO stubs: log once and
 * return to the menu. Replace the bodies later without changing the
 * interfaces or the dispatch table. */

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

/* stateCharacterSelect @0x41efa0 — character-select screen, entered by the
 * modeInit* game-type initializers (@0x41bf60-0x41bfe0). Original: character
 * picker + start; keys 6/7 advance/cancel. TODO stub: log + return to the
 * game-type select so the flow stays within the implemented menu slice. */
int stateCharacterSelect(int nType, int nKey, int nKeyType)
{
    static int bLogged;
    (void)nType; (void)nKey; (void)nKeyType;
    if (!bLogged) {
        bLogged = 1;
        appLog("[stub TODO] stateCharacterSelect @0x41efa0 not implemented (back to game-type select)");
    }
    g_pStateFunc = stateGameTypeSelect;
    return 0;
}
