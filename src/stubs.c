#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compat_types.h"
#include "stubs.h"
#include "time.h"
#include "menu.h"
#include "charselect.h"
#include "gx.h"
#include "pool.h"
#include "player.h"
#include "obj.h"
#include "nav.h"
#include "sen.h"
#include "font.h"
#include "record.h"
#include "scene_system.h"
#include "sound.h"
#include "custom_helpers.h"

/* =====================================================================
 * TODO stubs — see stubs.h for contracts. Safe, logged no-op or placeholder
 * transitions let future milestones wire real implementations without
 * changing callers. Original addresses noted in stubs.h.
 * ===================================================================== */

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

/* netExit @0x426b30, netGameUpdate @0x414fa0, netIsActive @0x426ed0,
 * netServerSendSubCmd @0x415d20 and netClientSendSubCmd @0x415cb0 are not
 * reconstructed and not called: networking is out of scope for the offline
 * rebuild, and the offline branches no longer reference them. */

/* commandDispatch — original command/config query contract. Query replies
 * ("get fshi/vahi<lvl>time<slot>", "get toplevel") return a static text
 * buffer; every other command returns NULL, which is safe for callers
 * that only use the result as optional text (fmtAtoi is NULL-guarded).
 * The level startup scripts run "eload <file>.eo" (EventObject zones)
 * and "nload <file>.ai" (AI nav buoys). The original dispatches through
 * a name table @0x44b3xx; the rebuild hardcodes the commands in use. */
unsigned char *commandDispatch(intptr_t nCommand, LPCSTR pszCommand)
{
    if (pszCommand != NULL && strncmp(pszCommand, "action ", 7) == 0) {
        actionCmd(nCommand, pszCommand + 7); /* @0x4067c0 (table @0x44b308) */
    }
    if (pszCommand != NULL && strcmp(pszCommand, "kill") == 0) {
        killCmd(nCommand, NULL);             /* @0x407870 (table @0x44b384) */
    }
    if (pszCommand != NULL && strncmp(pszCommand, "run ", 4) == 0) {
        runCmd(0, pszCommand + 4);
    }
    else if (pszCommand != NULL && strncmp(pszCommand, "eload ", 6) == 0) {
        eloadCmd(0, pszCommand + 6);     /* @0x406cb0 */
    }
    else if (pszCommand != NULL && strncmp(pszCommand, "nload ", 6) == 0) {
        nloadCmd(0, pszCommand + 6);     /* @0x407e10 */
    }
    /* Record-table queries for the results-screen HUD (hud.c @0x412993
     * "get fshi/vahi<lvl>time<slot>", @0x412e72 "request fshi/vahiscore",
     * @0x412b26/@0x412b4c "get/set toplevel"): the original served these
     * from its live config tree; the rebuild serves record.c's decoded
     * config.mm table. Replies use one static buffer (callers consume the
     * text immediately via fmtAtoi). Anything else still returns NULL. */
    if (pszCommand != NULL &&
        (strncmp(pszCommand, "get fshi", 8) == 0 ||
         strncmp(pszCommand, "get vahi", 8) == 0)) {
        static unsigned char szRecReply[32];
        int isVahi = (pszCommand[4] == 'v');
        const char *pTime = strstr(pszCommand + 8, "time");
        int nLvl = atoi(pszCommand + 8);                 /* "get vahi<lvl>time<slot>" */
        int nSlot = (pTime != NULL) ? atoi(pTime + 4) : 0;
        snprintf((char *)szRecReply, sizeof(szRecReply), "%d",
                 recordGetTime(isVahi, nLvl, nSlot));
        return szRecReply;
    }
    if (pszCommand != NULL && strcmp(pszCommand, "get toplevel") == 0) {
        static unsigned char szTopReply[16];
        snprintf((char *)szTopReply, sizeof(szTopReply), "%d", recordGetTopLevel());
        return szTopReply;
    }
    if (pszCommand != NULL && strncmp(pszCommand, "set toplevel ", 13) == 0) {
        recordSetTopLevel(atoi(pszCommand + 13));
    }
    if (pszCommand != NULL &&
        (strncmp(pszCommand, "request fshiscore ", 18) == 0 ||
         strncmp(pszCommand, "request vahiscore ", 18) == 0)) {
        int nTime, nSlot, nFace, nLvl;
        if (sscanf(pszCommand + 18, "%d %d %d %d",
                   &nTime, &nSlot, &nFace, &nLvl) == 4) {
            recordSubmitScore((pszCommand[8] == 'v'), nTime, nSlot, nFace, nLvl);
        }
    }
    return NULL;
}

extern char g_szSceneDir[]; /* @0x45e950 defined in sen.c */
/* scenSetDir @0x432e60 and scenExpandNameList @0x432dd0 are implemented in
 * sen.c (moved out of stubs when the name-expansion mechanism landed). */

/* consoleHandleKey @0x4086e0 — the console line editor is out of scope and no
 * longer called: the offline rebuild never opens the console overlay
 * (g_nScrollText stays 0), so gameKeyHandler drops that branch. */

/* objTurretListFree @0x402b40 — free a turret sub-struct list (node +0x08
 * embedded list at +0x48). Turret objects are deferred; safe no-op. */
void objTurretListFree(int nMode) /* @0x402b40 */
{
    (void)nMode;
}

/* objTurretListFree2 @0x402b70 — free the second turret sub-struct list
 * (node +0x0c, embedded list at +0x18). Deferred; safe no-op. */
void objTurretListFree2(int nMode) /* @0x402b70 */
{
    (void)nMode;
}

/* musicModuleInit @0x437b10 — see stubs.h. The slot-allocator entry and its
 * callback chain stay unreconstructed; returning NULL keeps the offline
 * game music-silent (g_nMusicModuleHandle == 0, mciPlayCdaudio still runs
 * the CD track like the original). */
void *g_pMusicSlotAlloc;                                     /* @0x450f6c */

void *musicModuleInit(void *pModuleEntry) /* @0x437b10 */
{
    (void)pModuleEntry;
    return NULL;
}
