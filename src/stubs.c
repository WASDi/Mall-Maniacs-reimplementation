#include <stdio.h>
#include <windows.h>
#include "stubs.h"
#include "menu.h"
#include "gx.h"
#include "pool.h"
#include "player.h"
#include "obj.h"
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

/* unloadGameWorld — gameplay-world teardown contract. */
void unloadGameWorld(void)
{
    appLog("[stub TODO] unloadGameWorld");
}

/* sndEmitterUpdateAll @0x42bf40 — the original walks the positional-emitter
 * list after rendering. The world/emitter ownership model is deferred, so an
 * empty list is the safe temporary contract. */
void sndEmitterUpdateAll(void)
{
}

/* playerAiUpdate @0x401160 — the original AI state machine needs world
 * objects, nav points, item rules, and player commands not reconstructed yet.
 * The dispatcher preserves its scheduling contract while this safe TODO keeps
 * active AI records unchanged. */
void playerAiUpdate(AiController *pCtrl)
{
    (void)pCtrl;
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
 * <file>.ai" (AI nav mesh, still deferred). */
unsigned char *commandDispatch(int nCommand, LPCSTR pszCommand)
{
    (void)nCommand;
    if (pszCommand != NULL && strncmp(pszCommand, "run ", 4) == 0) {
        runCmd(0, pszCommand + 4);
    }
    else if (pszCommand != NULL && strncmp(pszCommand, "eload ", 6) == 0) {
        eloadCmd(0, pszCommand + 6);     /* @0x406cb0 */
    }
    return NULL;
}

extern char g_szSceneDir[]; /* @0x45e950 defined in sen.c */
int scenSetDir(LPCSTR pszDir) /* @0x432e60 */
{
    if (pszDir) {
        strncpy(g_szSceneDir, pszDir, 255);
        g_szSceneDir[255]='\0';
    } else {
        g_szSceneDir[0]='\0';
    }
    return 1;
}
int scenExpandNameList(char *pList, void *pEnd, char *pszDir) /* @0x432dd0 */
{
    (void)pList; (void)pEnd; (void)pszDir;
    return 0;
}
/* netIsActive @0x426ed0 — client/server flags ORed; both stay 0 offline. */
int g_nNetIsClient;   /* @0x45e59c */
int g_nNetIsServer;   /* @0x45e598 */
int netIsActive(void) /* @0x426ed0 */
{
    return g_nNetIsClient | g_nNetIsServer;
}

/* objShotListFree @0x406110 — free a world node's shot list (+0x10). The
 * shot object model is deferred; playerSetupRound sub-objects never set
 * the field, so this is a safe no-op. */
void objShotListFree(void *pShotList) /* @0x406110 */
{
    (void)pShotList;
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

/* aiNavNodeCtorScene @0x428cf0 — documented TODO stub, see stubs.h. The
 * original (thiscall, 0x48-byte AiNavNode from levelSceneTexturesLoad):
 * zeroes pConnList/pEdgeList/pNext/pPrev, sceneNodeGetPos(node,0,
 * &node->nPosX,4), node->flAvgY = (float)node->nPosY, g_pNavMeshData =
 * *(mesh+0x14) via sceneNodeGetMesh, clears g_nNavTriIdx/g_pNavTriCur,
 * then aiNavNodeUpdate. Deferred with the AI navigation subsystem
 * (docs/16-rebuild.md step 2). */
void aiNavNodeCtorScene(AiNavNode *pNavNode, int nSceneNode) /* @0x428cf0 */
{
    (void)pNavNode; (void)nSceneNode;
}

/* zoneWallListBuild @0x42a650 — documented TODO stub, see stubs.h. Original
 * pass over g_pNavNodeList: zoneConnMergeDupesInMesh per node, zoneWall
 * CalcPlane per node, zoneConnMergeDupesCrossMesh per node, a doubly-linked
 * wall-list resort (+0x10/+0x44), zoneWallMergeDupesSameDir per node.
 * Deferred with the zone/AI subsystem; no-op while the nav list is empty. */
void zoneWallListBuild(void) /* @0x42a650 */
{
}

/* zoneAvoidWalls @0x4023e0 — documented TODO stub, see stubs.h. Original
 * walks the zone-wall segment list (zoneWallListBuild output) and pushes
 * pPoint out of any wall within flRadius of the pRef->pPoint path. Deferred
 * until the wall lists exist; cameraFollowUpdate treats a 0 return as "no
 * wall contact" and keeps the untouched point. */
int zoneAvoidWalls(GxVec2 *pPoint, GxVec2 *pRef, float flRadius) /* @0x4023e0 */
{
    (void)pPoint; (void)pRef; (void)flRadius;
    return 0;
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

/* levelEventDirector_L0..L4 — see stubs.h. No-op until the per-level
 * director subsystem is reconstructed. */
void levelEventDirector_L0(void) /* @0x416fd0 */
{
}

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

/* levelEventDirector_L0_Init..L4_Init — see stubs.h. No-op until the
 * per-level director subsystem is reconstructed. */
void levelEventDirector_L0_Init(void) /* @0x416db0 */
{
}

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
