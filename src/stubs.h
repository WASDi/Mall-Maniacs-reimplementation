#ifndef STUBS_H
#define STUBS_H

#include <windows.h>

#include "gameplay.h"
#include "player.h"

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
 * and level startup scripts' "eload <file>.eo" to eloadCmd (src/obj.c). */
void playerSetupCharacters(void);                            /* @0x41b760 */
void unloadGameWorld(void);                                  /* gameplay teardown */
unsigned char *commandDispatch(int nCommand, LPCSTR pszCommand); /* @0x408b60 */
void sndEmitterUpdateAll(void);                              /* @0x42bf40 */
void playerAiUpdate(Player *pPlayer);                        /* @0x401160 */
void movieFrameUpdate(void);                                 /* @0x40af80 */
void roundLogicUpdate(void);                                 /* @0x40beb0 */
void netGameUpdate(void);                                    /* @0x414fa0 */
void zoneConnUpdateCulling(void);                            /* @0x42b8f0 */
void sceneDetailGridUpdate(void *pDetailGrid);               /* @0x42b1d0 */
void sndStopAllVoices(void *pVoiceList);                     /* @0x438100 */
void renderGameHud(void);                                    /* @0x412810 */

/* sceneInstantiateObjects — documented stub for the out-of-scope scene-graph
 * population performed at the end of sceneLoadSen @0x432320 (OBJI objects,
 * scenery nodes, music emitters). For menu\CHARACTERS.SEN the object-instance
 * and map-geometry counts are zero, so this is never reached by the offline
 * menu preview; it is a safe no-op stub. When gameplay lands, the real body
 * rebuilds the scene graph from g_pObjInstances / g_pMapGeom / g_pTextAnimData.
 * Contract: takes the owning memPool handle (unused), returns 1 for success. */
int sceneInstantiateObjects(int pool);

/* Early-init deferred stubs — keep call hierarchy intact for
 * TrackRebuildDetailed. Real implementations will replace these when
 * the scene / string subsystems land. */

/* scenSetDir @0x432e60 — set scene base directory g_szSceneDir[64].
 * Stub: no copy, returns 1. */
int scenSetDir(LPCSTR pszDir);

/* sceneFindByName @0x431fd0 — find ids by substring via strFindSubstring.
 * Stub: returns 0 matches. */
int sceneFindByName(int *pOut, int nMax, char *pszSubstr);

/* sceneNodeSetHiddenFlag @0x4305c0 — set hidden flag at node+2, nMode
 * 1→1, 2→1+recurse children, 3→2. Stub: sets flag if node non-null,
 * guarded self-call preserves 1/1 recursion count without infinite loop. */
int sceneNodeSetHiddenFlag(int pNode, int nMode);

/* aiNavNodeCtorScene @0x428cf0 — documented TODO stub (see stubs.c). The
 * original is a thiscall ctor over a 0x48-byte node allocated by
 * levelSceneTexturesLoad for every "FLOOR" mesh of the ph scene: zeroes the
 * +0x38..+0x44 link fields, reads the node position into +0x2a/+0x00, grabs
 * the mesh triangle data and runs aiNavNodeUpdate. The AI navigation
 * subsystem is deferred; the node is intentionally left unlinked (the
 * original links it through zoneWallListBuild's g_pNavNodeList pass). */
void aiNavNodeCtorScene(void *pNavNode, int nSceneNode);

/* zoneWallListBuild @0x42a650 — documented TODO stub (see stubs.c). The
 * original walks g_pNavNodeList (+0x40 next) running zoneConnMergeDupes
 * InMesh, zoneWallCalcPlane, zoneConnMergeDupesCrossMesh, a link-list
 * resort and zoneWallMergeDupesSameDir. Deferred with the zone/AI
 * subsystem; safe no-op while the nav-node list stays empty. */
void zoneWallListBuild(void);

#endif /* STUBS_H */
