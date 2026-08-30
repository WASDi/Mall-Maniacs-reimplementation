#ifndef STUBS_H
#define STUBS_H

#include <windows.h>

#include "gameplay.h"
#include "player.h"
#include "zone.h"

/* =====================================================================
 * Declared interfaces for unfinished behavior.
 * Each is a TODO stub: logs and performs a documented no-op or safe
 * placeholder transition so the vertical slice proceeds; callers keep their
 * contract unchanged when the stub body is later replaced. Original
 * target/address noted where one exists.
 * ===================================================================== */

/* Main-menu row targets (menu.c dispatch table g_kMenuRowTarget, entered
 * from menuUpdate @0x41b0b0 on Enter). Contract: state-func convention
 * (nType 0 = frame update, nType 1 + nKeyType 2 = keydown); each sets
 * g_pStateFunc to the next state. TODO stubs: log once + return to the
 * menu. Interfaces stay fixed when the real bodies replace them.
 * gotoOptions @0x41d300 is now implemented in options.c (Alternativ ->
 * Svårighetsgrad, Grafik ignored). */
int stateNetworkMenu(int nType, int nKey, int nKeyType);     /* @0x420190 */

/* Gameplay entry contracts used by stateLevelInit0..4. Player/world setup is
 * still deferred; commandDispatch routes the original run command to runCmd
 * and level startup scripts' "eload <file>.eo" to eloadCmd (src/obj_event.c). */
void unloadGameWorld(void);                                  /* gameplay teardown */
unsigned char *commandDispatch(int nCommand, LPCSTR pszCommand); /* @0x408b60 */
void playerAiUpdate(AiController *pCtrl);                    /* @0x401160 */
void movieFrameUpdate(void);                                 /* @0x40af80 */
void netGameUpdate(void);                                    /* @0x414fa0 */
void zoneConnUpdateCulling(void);                            /* @0x42b8f0 */
void sceneDetailGridUpdate(void *pDetailGrid);               /* @0x42b1d0 */
void sndStopAllVoices(void *pVoiceList);                     /* @0x438100 */

/* netIsActive @0x426ed0 — g_nNetIsClient | g_nNetIsServer. The offline
 * rebuild keeps both flags 0 (set by the deferred net subsystem). */
int netIsActive(void);                                       /* @0x426ed0 */
extern int g_nNetIsClient;                                   /* @0x45e59c */
extern int g_nNetIsServer;                                   /* @0x45e598 */

/* World-object sub-lists freed by objDtor @0x402ab0 (obj_world). The turret
 * object model is deferred; the player round-setup sub-objects never
 * populate these fields, so the stubs are safe no-ops. */
void objTurretListFree(int nMode);                           /* @0x402b40 */
void objTurretListFree2(int nMode);                          /* @0x402b70 */

/* Early-init deferred stubs — keep call hierarchy intact for
 * TrackRebuildDetailed. Real implementations will replace these when
 * the scene / string subsystems land. */

/* scenSetDir @0x432e60 — set scene base directory g_szSceneDir[64].
 * Stub: no copy, returns 1. */
int scenSetDir(LPCSTR pszDir);


/* objSegListIntersectTest @0x414ce0 — documented TODO stub (see stubs.c).
 * The original tests the (x1,y1)->(x2,y2) segment against an EventObject's
 * zone line segments (c_di camera-distance limiter). Called from
 * cameraFollowUpdate @0x4020d0 to lower the camera height; must return
 * nonzero on a crossing. Returns 0 (no crossing) until zone line lists
 * are loaded for c_di objects. */
int objSegListIntersectTest(EventObject *pObj, float flX1, float flY1,
                            float flX2, float flY2);

/* netServerSendSubCmd @0x415d20 — server broadcast of a sub-command packet
 * (nSubCmd plus six payload dwords). The offline rebuild keeps no network
 * session (netIsActive() == 0), so every call site is dead; the stub only
 * preserves the call hierarchy for the net paths of roundLogicUpdate and
 * the lobby states. Real implementation is deferred with networking. */
void netServerSendSubCmd(int nSubCmd, int nArg1, int nArg2, int nArg3,
                         int nArg4, int nArg5, int nArg6);      /* @0x415d20 */

/* netClientSendSubCmd @0x415cb0 — client send of a sub-command packet
 * (nSubCmd plus six payload dwords) to the server. Same contract and
 * deferral as netServerSendSubCmd. */
void netClientSendSubCmd(int nSubCmd, int nArg1, int nArg2, int nArg3,
                         int nArg4, int nArg5, int nArg6);      /* @0x415cb0 */

/* musicModuleInit @0x437b10 — init the music/streaming module: calls the
 * slot-allocator entry behind g_pMusicSlotAlloc @0x450f6c (original target
 * 0x431c30) with five callbacks (musicCbInitEmitter @0x437b40,
 * musicCbRet1 @0x437bf0, musicModulePosCheck @0x437b60, musicMixCb
 * @0x437c00) and sample rate 15000, returning the module handle which
 * roundStartInit stores in g_nMusicModuleHandle @0x4580bc. The module
 * entry chain and its callbacks are not reconstructed yet, so the stub
 * returns NULL and the rebuild stays music-silent. */
extern void *g_pMusicSlotAlloc;                              /* @0x450f6c */
void *musicModuleInit(void *pModuleEntry);                   /* @0x437b10 */

/* levelEventDirector_L1..L4 @0x4178c0/0x418000/0x418a30/0x4192a0 — the
 * remaining per-level event directors, stepped once per roundLogicUpdate
 * (dispatch on g_nLevelIdx @0x40c7a6..0x40c7c7). L0 is implemented in
 * level0.c. Each remaining director runs its level's "MCDMAN bonus task"
 * presenter: ambient sign sub-mesh bobbing (L2/L3), a 9-step anim state
 * machine on a 10 s idle gate that dresses the mascot with the bonus prop,
 * throws it and spawns a class-0x1f bonus EventObject via sceneObjCtor3 +
 * objHashRegister, plus the cashier s_winner/cash_sit ambient anim
 * (results screen aware); L2 additionally beeps sfx 0x23 while the local
 * player stands in the cash-zone object. Documented TODO stubs; safe no-ops
 * while the round clock, countdown and win detection stay fully live
 * without them. */
void levelEventDirector_L1(void);                            /* @0x4178c0 */
void levelEventDirector_L2(void);                            /* @0x418000 */
void levelEventDirector_L3(void);                            /* @0x418a30 */
void levelEventDirector_L4(void);                            /* @0x4192a0 */

/* levelEventDirector_L1_Init..L4_Init @0x4175e0/0x417dc0/0x4186c0/
 * 0x418f30 — per-level director reset, dispatched by levelDirectorInits
 * @0x40bdf0 at round start (L0_Init is implemented in level0.c).
 * Documented TODO stubs; safe no-ops until the director subsystem lands. */
void levelEventDirector_L1_Init(void);                       /* @0x4175e0 */
void levelEventDirector_L2_Init(void);                       /* @0x417dc0 */
void levelEventDirector_L3_Init(void);                       /* @0x4186c0 */
void levelEventDirector_L4_Init(void);                       /* @0x418f30 */

/* levelEventDirector_L1_Cleanup..L4_Cleanup @0x417860/0x417fa0/0x4189d0/
 * 0x419240 — per-level director anim teardown, dispatched by roundTeardown
 * @0x40aa10 (jump table 0x40ad60) on g_nLevelIdx 1..4 (L0_Cleanup lives in
 * level0.c). The stub roundTeardown below is a safe no-op until the real
 * teardown sequence (director Cleanup dispatch + world/scene unload) lands,
 * so the L1..L4 Cleanups still have no rebuild-side callers. */
void roundTeardown(void);                                    /* @0x40aa10 */

/* consoleHandleKey @0x4086e0 — console line editor (backspace/tab-completion/
 * enter/esc/history). The in-game console is out of scope for the offline
 * rebuild and g_nScrollText @0x4580ec is never set nonzero here, so the only
 * caller (gameKeyHandler @0x40db80 nKeyType==0 branch) can never reach it;
 * the stub exists only to keep the original call hierarchy intact. */
void consoleHandleKey(int nKey);                             /* @0x4086e0 */



#endif /* STUBS_H */
