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

/* unloadGameWorld @0x41a670 — world/round teardown, called by stateLevelInit0
 * (each frame while it is the state func) and by the original roundTeardown.
 * VERIFIED 2026-08-31 vs disassembly @0x41a670..0x41a72a: when g_nMenuInit
 * @0x45a658 is nonzero it logs "menu exit" @0x4507b8, clears the deferred
 * resume slot g_pResumeStateFunc @0x45a710, resets g_nMenuInit = 0 (the next
 * gameFrameUpdate re-runs menuInit @0x419c20 and lands back on menuUpdate),
 * then frees the two char-select anim blocks (g_pCharAnimPrev @0x45a6dc /
 * g_pCharAnim @0x45a6d8 via anmFree @0x434050 when nonzero), the two anim
 * data blocks (g_pCharSelAnimData @0x45a6d0 / g_pThrowAnimData @0x45a6d4,
 * memPoolFree 0, unconditional), does the gxFlip/gxClearScreen(1,
 * g_nClearColor @0x45892c) x2 cycle, then tears the systems down:
 * scenNameTableFree @0x431e00, sceneSystemClose @0x42f180, sndShutdown
 * @0x437cb0, fontPoolDestroy @0x408fc0, memPoolSystemShutdown @0x419bb0,
 * winmmRestoreTimerRes @0x40e030 and the tail-jmp mciStopCdaudio
 * @0x416c80. roundStartInit re-inits every one of these pools/systems
 * behind its gxInit/memPoolSystemInit/sceneSystemInit cycle. */
void unloadGameWorld(void) /* @0x41a670 */
{
    if (g_nMenuInit == 0) {
        return;                                    /* @0x41a677 */
    }
    g_pResumeStateFunc = NULL;                     /* @0x45a710 @0x41a684 */
    g_nMenuInit = 0;                               /* @0x45a658 @0x41a68e */
    nopDebugStub();                                /* "menu exit" @0x4507b8 @0x41a698 */
    if (g_pCharAnimPrev != NULL) {                 /* @0x45a6dc @0x41a6a5 */
        anmFree(g_pCharAnimPrev);                  /* @0x434050 @0x41a6aa */
    }
    if (g_pCharAnim != NULL) {                     /* @0x45a6d8 @0x41a6b2 */
        anmFree(g_pCharAnim);                      /* @0x41a6bc */
    }
    memPoolFree(0, g_pCharSelAnimData);            /* @0x45a6d0 @0x41a6cc */
    memPoolFree(0, g_pThrowAnimData);              /* @0x45a6d4 @0x41a6da */
    gxFlip();                                      /* @0x433340 @0x41a6df */
    gxClearScreen(1, g_nClearColor);               /* @0x433350 @0x41a6ed */
    gxFlip();                                      /* @0x41a6f2 */
    gxClearScreen(1, g_nClearColor);               /* @0x41a6ff */
    scenNameTableFree();                           /* @0x431e00 @0x41a707 */
    sceneSystemClose();                            /* @0x42f180 @0x41a70c */
    sndShutdown();                                 /* @0x437cb0 @0x41a711 */
    fontPoolDestroy();                             /* @0x408fc0 @0x41a716 */
    memPoolSystemShutdown();                       /* @0x419bb0 @0x41a71b */
    winmmRestoreTimerRes();                        /* @0x40e030 @0x41a720 */
    mciStopCdaudio();                              /* tail-jmp @0x416c80 @0x41a725 */
}

/* roundTeardown @0x40aa10 — implemented in gameplay.c (full teardown
 * sequence); the declaration lives in gameplay.h. */

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

/* levelEventDirector_L2_Cleanup..L4_Cleanup @0x417fa0/0x4189d0/
 * 0x419240 — per-level director anim teardown, dispatched by roundTeardown
 * @0x40aa10 (jump table @0x40ad60) on g_nLevelIdx 2..4 (L0_Cleanup lives in
 * level0.c, L1_Cleanup in level1.c). Each frees its level's seven event
 * anims (anmFree) and resets the director step state; the director bodies
 * are deferred, so these stay documented no-op stubs preserving the
 * dispatch call sites. */

void levelEventDirector_L2_Cleanup(void) /* @0x417fa0 */
{
}

void levelEventDirector_L3_Cleanup(void) /* @0x4189d0 */
{
}

void levelEventDirector_L4_Cleanup(void) /* @0x419240 */
{
}

/* levelEventDirector_L2..L4 — see stubs.h. The L0/L1 directors are
 * implemented in level0.c/level1.c; the remaining per-level directors
 * stay no-op stubs. */

void levelEventDirector_L2(void) /* @0x418000 */
{
}

void levelEventDirector_L3(void) /* @0x418a30 */
{
}

void levelEventDirector_L4(void) /* @0x4192a0 */
{
}

/* levelEventDirector_L2_Init..L4_Init — see stubs.h. L0_Init/L1_Init are
 * implemented in level0.c/level1.c; the remaining per-level director
 * inits stay no-op stubs. */

void levelEventDirector_L2_Init(void) /* @0x417dc0 */
{
}

void levelEventDirector_L3_Init(void) /* @0x4186c0 */
{
}

void levelEventDirector_L4_Init(void) /* @0x418f30 */
{
}
