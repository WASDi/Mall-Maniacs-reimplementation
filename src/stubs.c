#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "stubs.h"
#include "time.h"
#include "menu.h"
#include "gx.h"
#include "pool.h"
#include "player.h"
#include "obj.h"
#include "nav.h"
#include "custom_helpers.h"

extern HWND g_hWnd;

/* =====================================================================
 * TODO stubs — see stubs.h for contracts. Safe, logged no-op or placeholder
 * transitions let future milestones wire real implementations without
 * changing callers. Original addresses noted in stubs.h.
 * ===================================================================== */

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

/* unloadGameWorld @0x41a670 — world/round teardown, called by stateLevelInit0
 * (each frame while it is the state func) and by the original roundTeardown.
 * VERIFIED 2026-08-30 vs disassembly @0x41a670..0x41a72a: when g_nMenuInit
 * @0x45a658 is nonzero it logs "menu exit" @0x4507b8, clears the deferred
 * resume slot g_pResumeStateFunc @0x45a710, resets g_nMenuInit = 0 (the next
 * gameFrameUpdate re-runs menuInit @0x419c20 and lands back on menuUpdate),
 * then frees the two menu anim blocks (@0x45a6dc/@0x45a6d8 via anmFree
 * @0x434050), unloads the sprite/font handles (@0x419a60), clears/flips, and
 * tears the scene/sound down (@0x431e00/@0x42f180/@0x437cb0/@0x408fc0/
 * @0x419bb0/winmmRestoreTimerRes/gxSetMode @0x416c80). The resource-freeing
 * tail is deferred (TODO) — menuInit re-loads everything it needs on the
 * next menuInit pass — but the g_nMenuInit reset is the live contract that
 * makes killCmd @0x407870 ("J" on the quit prompt) return to the main menu. */
void unloadGameWorld(void) /* @0x41a670 */
{
    if (g_nMenuInit == 0) {
        return;                                    /* @0x41a677 */
    }
    nopDebugStub();                                /* "menu exit" @0x4507b8 @0x41a698 */
    g_pResumeStateFunc = NULL;                     /* @0x45a710 @0x41a684 */
    g_nMenuInit = 0;                               /* @0x45a658 @0x41a68e */
    /* TODO: anmFree(@0x45a6dc/@0x45a6d8), sprite/font unload (@0x419a60),
     * gxClearScreen/gxFlip cycle, scene + sound teardown, gxSetMode. */
}

/* roundTeardown @0x40aa10 — round-end teardown: per-level director Cleanup
 * dispatch (jump table @0x40ad60; L0_Cleanup lives in level0.c, L1..L4 are
 * stubs) plus quest/thrown-item/scene-object world unload. The full teardown
 * sequence is not reconstructed yet; the stub is a safe no-op so killCmd
 * @0x407870 ends the round (g_bGameActive = 0) without the freed-resource
 * steps. TODO: replace with the real jump-table teardown. */
void roundTeardown(void) /* @0x40aa10 */
{
}

/* consoleHandleKey @0x4086e0 — console line editor. Out of scope: the
 * offline rebuild never opens the console overlay (g_nScrollText stays 0),
 * so gameKeyHandler can never reach this. Safe no-op preserving the
 * original call hierarchy. */
void consoleHandleKey(int nKey) /* @0x4086e0 */
{
    (void)nKey;
}


/* movieFrameUpdate @0x40af80 — records or replays per-player input against
 * the original config-node database. The offline game has neither movie data
 * nor config-node ownership yet, so this safe TODO preserves idle playback. */
void movieFrameUpdate(void)
{
}

/* netGameUpdate @0x414fa0 — performs client/server state replication only
 * while a network session is active. Networking is out of scope for the
 * offline rebuild, so the no-op preserves the original frame-stage boundary
 * without creating a network session or mutating local player state. */
void netGameUpdate(void)
{
}

/* zoneConnUpdateCulling @0x42b8f0 — updates visibility across AR/IN zone
 * connections. Level objects and zone connections are not loaded yet, so the
 * empty list is a safe rendering-stage boundary. */
void zoneConnUpdateCulling(void)
{
}

/* sceneDetailGridUpdate @0x42b1d0 — rebuilds the visible-cell list for the
 * current scene detail grid. The level loader will provide this owner later;
 * until then a NULL grid deliberately has no visible cells. */
void sceneDetailGridUpdate(void *pDetailGrid)
{
    (void)pDetailGrid;
}

/* sndStopAllVoices @0x438100 — culls positional voices no longer in a
 * rendered zone. Positional voice ownership is deferred, so there is nothing
 * to stop in the offline gameplay slice. */
void sndStopAllVoices(void *pVoiceList)
{
    (void)pVoiceList;
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

/* objSegListIntersectTest @0x414ce0 — documented TODO stub, see stubs.h.
 * Original tests the (flX1,flY1)->(flX2,flY2) segment against pObj's zone
 * line segments (+0x04 line list) and returns nonzero on a crossing.
 * Deferred until c_di objects carry line lists; cameraFollowUpdate treats
 * a 0 return as "no height clamp". */
int objSegListIntersectTest(EventObject *pObj, float flX1, float flY1,
                            float flX2, float flY2) /* @0x414ce0 */
{
    (void)pObj; (void)flX1; (void)flY1; (void)flX2; (void)flY2;
    return 0;
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

/* levelEventDirector_L1..L4 — see stubs.h. The L0 director is implemented
 * in level0.c; the remaining per-level directors stay no-op stubs. */
void levelEventDirector_L1(void) /* @0x4178c0 */
{
}

void levelEventDirector_L2(void) /* @0x418000 */
{
}

void levelEventDirector_L3(void) /* @0x418a30 */
{
}

void levelEventDirector_L4(void) /* @0x4192a0 */
{
}

/* levelEventDirector_L1_Init..L4_Init — see stubs.h. L0_Init is implemented
 * in level0.c; the remaining per-level director inits stay no-op stubs. */
void levelEventDirector_L1_Init(void) /* @0x4175e0 */
{
}

void levelEventDirector_L2_Init(void) /* @0x417dc0 */
{
}

void levelEventDirector_L3_Init(void) /* @0x4186c0 */
{
}

void levelEventDirector_L4_Init(void) /* @0x418f30 */
{
}
