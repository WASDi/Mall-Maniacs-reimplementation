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
 * and level startup scripts' "eload <file>.eo" to eloadCmd (src/obj.c). */
void unloadGameWorld(void);                                  /* gameplay teardown */
unsigned char *commandDispatch(int nCommand, LPCSTR pszCommand); /* @0x408b60 */
void sndEmitterUpdateAll(void);                              /* @0x42bf40 */
void playerAiUpdate(AiController *pCtrl);                    /* @0x401160 */
void movieFrameUpdate(void);                                 /* @0x40af80 */
void roundLogicUpdate(void);                                 /* @0x40beb0 */
void netGameUpdate(void);                                    /* @0x414fa0 */
void zoneConnUpdateCulling(void);                            /* @0x42b8f0 */
void sceneDetailGridUpdate(void *pDetailGrid);               /* @0x42b1d0 */
void sndStopAllVoices(void *pVoiceList);                     /* @0x438100 */
void renderGameHud(void);                                    /* @0x412810 */

/* netIsActive @0x426ed0 — g_nNetIsClient | g_nNetIsServer. The offline
 * rebuild keeps both flags 0 (set by the deferred net subsystem). */
int netIsActive(void);                                       /* @0x426ed0 */
extern int g_nNetIsClient;                                   /* @0x45e59c */
extern int g_nNetIsServer;                                   /* @0x45e598 */

/* World-object sub-lists freed by objDtor @0x402ab0 (obj.c). The shot and
 * turret object models are deferred; the player round-setup sub-objects
 * never populate these fields, so the stubs are safe no-ops. */
void objShotListFree(void *pShotList);                       /* @0x406110 */
void objTurretListFree(int nMode);                           /* @0x402b40 */
void objTurretListFree2(int nMode);                          /* @0x402b70 */

/* Early-init deferred stubs — keep call hierarchy intact for
 * TrackRebuildDetailed. Real implementations will replace these when
 * the scene / string subsystems land. */

/* scenSetDir @0x432e60 — set scene base directory g_szSceneDir[64].
 * Stub: no copy, returns 1. */
int scenSetDir(LPCSTR pszDir);

/* aiNavNodeCtorScene @0x428cf0 — documented TODO stub (see stubs.c). The
 * original is a thiscall ctor over a 0x48-byte node allocated by
 * levelSceneTexturesLoad for every "FLOOR" mesh of the ph scene: zeroes
 * pConnList/pEdgeList/pNext/pPrev, reads the node position into
 * +0x2a..+0x34 and flAvgY, grabs the mesh triangle data and runs
 * aiNavNodeUpdate. The AI navigation subsystem is deferred; the node is
 * intentionally left unlinked (the original links it through
 * zoneWallListBuild's g_pNavNodeList pass). */
void aiNavNodeCtorScene(AiNavNode *pNavNode, int nSceneNode);

/* zoneWallListBuild @0x42a650 — documented TODO stub (see stubs.c). The
 * original walks g_pNavNodeList (+0x40 next) running zoneConnMergeDupes
 * InMesh, zoneWallCalcPlane, zoneConnMergeDupesCrossMesh, a link-list
 * resort and zoneWallMergeDupesSameDir. Deferred with the zone/AI
 * subsystem; safe no-op while the nav-node list stays empty. */
void zoneWallListBuild(void);

/* zoneAvoidWalls @0x4023e0 — documented TODO stub (see stubs.c). The
 * original pushes pPoint away from the zone-wall segment list built by
 * zoneWallListBuild (needs the wall lists + zoneWallCalcPlane cluster).
 * Called from cameraFollowUpdate @0x4020d0 with (pPoint, pRef, flRadius)
 * where flRadius is the camera height; must return nonzero after moving
 * pPoint. Returns 0 (no wall contact) until the wall lists exist. */
int zoneAvoidWalls(GxVec2 *pPoint, GxVec2 *pRef, float flRadius);

/* objSegListIntersectTest @0x414ce0 — documented TODO stub (see stubs.c).
 * The original tests the (x1,y1)->(x2,y2) segment against an EventObject's
 * zone line segments (c_di camera-distance limiter). Called from
 * cameraFollowUpdate @0x4020d0 to lower the camera height; must return
 * nonzero on a crossing. Returns 0 (no crossing) until zone line lists
 * are loaded for c_di objects. */
int objSegListIntersectTest(EventObject *pObj, float flX1, float flY1,
                            float flX2, float flY2);

#endif /* STUBS_H */
