#include <stdio.h>
#include <string.h>
#include <windows.h>
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
#include "scene_system.h"
#include "sound.h"
#include "custom_helpers.h"

extern HWND g_hWnd;

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

/* netExit @0x426b30 (thunk 0x414f60) — session teardown: nopDebugStub,
 * netShutdown (Winsock cleanup — out of scope for the offline rebuild),
 * then clears the client/server flags. The stub performs the observable
 * offline part (flag clear) and documents the omitted Winsock call. */
void netExit(void) /* @0x426b30 */
{
    nopDebugStub();                                /* @0x426b3a */
    g_nNetIsServer = 0;                            /* @0x45e598 @0x426b45 */
    g_nNetIsClient = 0;                            /* @0x45e59c @0x426b4d */
}

/* consoleHandleKey @0x4086e0 — console line editor. Out of scope: the
 * offline rebuild never opens the console overlay (g_nScrollText stays 0),
 * so gameKeyHandler can never reach this. Safe no-op preserving the
 * original call hierarchy. */
void consoleHandleKey(int nKey) /* @0x4086e0 */
{
    (void)nKey;
}

/* netGameUpdate @0x414fa0 — performs client/server state replication only
 * while a network session is active. Networking is out of scope for the
 * offline rebuild, so the no-op preserves the original frame-stage boundary
 * without creating a network session or mutating local player state. */
void netGameUpdate(void)
{
}

/* commandDispatch — original command/config query contract. Returning NULL
 * is safe for callers that only use the result as optional text. The level
 * startup scripts run "eload <file>.eo" (EventObject zones) and "nload
 * <file>.ai" (AI nav buoys). The original dispatches through a name table
 * @0x44b3xx; the rebuild hardcodes the commands in use. */
unsigned char *commandDispatch(int nCommand, LPCSTR pszCommand)
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
    return NULL;
}

extern char g_szSceneDir[]; /* @0x45e950 defined in sen.c */
/* scenSetDir @0x432e60 and scenExpandNameList @0x432dd0 are implemented in
 * sen.c (moved out of stubs when the name-expansion mechanism landed). */
/* netIsActive @0x426ed0 — client/server flags ORed; both stay 0 offline. */
int g_nNetIsClient;   /* @0x45e59c */
int g_nNetIsServer;   /* @0x45e598 */
int netIsActive(void) /* @0x426ed0 */
{
    return g_nNetIsClient | g_nNetIsServer;
}

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

/* netServerSendSubCmd @0x415d20 — see stubs.h. Dead offline (no session). */
void netServerSendSubCmd(int nSubCmd, int nArg1, int nArg2, int nArg3,
                         int nArg4, int nArg5, int nArg6) /* @0x415d20 */
{
    (void)nSubCmd; (void)nArg1; (void)nArg2; (void)nArg3;
    (void)nArg4; (void)nArg5; (void)nArg6;
}

/* netClientSendSubCmd @0x415cb0 — see stubs.h. Dead offline (no session). */
void netClientSendSubCmd(int nSubCmd, int nArg1, int nArg2, int nArg3,
                         int nArg4, int nArg5, int nArg6) /* @0x415cb0 */
{
    (void)nSubCmd; (void)nArg1; (void)nArg2; (void)nArg3;
    (void)nArg4; (void)nArg5; (void)nArg6;
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
