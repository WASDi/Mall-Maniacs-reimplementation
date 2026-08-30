#include <stdio.h>
#include <windows.h>
#include "stubs.h"
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
 * <file>.ai" (AI nav buoys). The original dispatches through a name table
 * @0x44b3xx; the rebuild hardcodes the commands in use. */
unsigned char *commandDispatch(int nCommand, LPCSTR pszCommand)
{
    if (pszCommand != NULL && strncmp(pszCommand, "action ", 7) == 0) {
        actionCmd(nCommand, pszCommand + 7); /* @0x4067c0 (table @0x44b308) */
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

/* sndPlaySfx3D @0x42bcd0 — 3D positional sound emitter. Documented TODO
 * stub: the 0x1c emitter block handed over by the caller is linked into
 * g_pSndEmitterHead/Tail @0x45e5f0/.f4 and driven by the music module
 * (musicEmitterAlloc), which is deferred with the audio subsystem. The
 * stub frees the block immediately so the fire-and-forget callers (walk
 * physics hazard sfx) do not leak; no sound plays. The original also
 * takes a 10th nFlags argument that only reaches the emitter setup. */
int sndPlaySfx3D(void *pEmitter, unsigned int nBank, unsigned int nIdx,
                 unsigned int nVol, int nSndId, int pPosNode,
                 int nEmitParam6, int nX, int nY, int nZ) /* @0x42bcd0 */
{
    (void)nBank; (void)nIdx; (void)nVol; (void)nSndId; (void)pPosNode;
    (void)nEmitParam6; (void)nX; (void)nY; (void)nZ;
    memFreeDirect(pEmitter);
    return 0;
}

/* objUpdatePhysics @0x405680 — world-item physics pass called twice by
 * objUpdateAll @0x4055f0. Documented TODO stub: the world-item physics
 * subsystem (loose item bobbing/settling) is the next rebuild step; the
 * per-frame world-node state it consumes is zeroed by objUpdateAll, so a
 * no-op is safe. */
void objUpdatePhysics(void) /* @0x405680 */
{
}

/* objUpdateFire @0x405e10 — world-item fire/hazard pass called between the
 * two objUpdatePhysics runs. Documented TODO stub, same contract as
 * objUpdatePhysics. */
void objUpdateFire(void) /* @0x405e10 */
{
}

/* thrownItemFree @0x40f8d0 — TODO: unlink a thrown-item record from the
 * g_pThrownItemHead/Tail list and release its WorldNode mesh (the item
 * cluster is the next rebuild increment; the record is left allocated and
 * detached so callers do not dereference freed memory). */
void thrownItemFree(ThrownItemStub *pItem) /* @0x40f8d0 */
{
    (void)pItem;
}

/* playerAnimSfxUpdate @0x40c800 — TODO stub (next-step #4). */
void playerAnimSfxUpdate(void) /* @0x40c800 */
{
}

/* playerThrowItemCtor @0x40f720 — TODO stub: allocates nothing; the AI
 * drop-item path simply produces no projectile until the item cluster
 * lands (contract in stubs.h). */
void *playerThrowItemCtor(void *pItem, int nItemId) /* @0x40f720 */
{
    (void)nItemId;
    return pItem;
}

/* itemThrowUpdate @0x40f950 — step one thrown item (g_pThrownItemHead
 * list, next at +0x04). Documented TODO stub: the thrown-item cluster
 * (playerThrowItemCtor/itemThrowUpdate/itemMeshFollowUpdate) is the next
 * rebuild step; the list is empty until then, so gameUpdate never
 * reaches this stub. */
void itemThrowUpdate(void *pItem) /* @0x40f950 */
{
    (void)pItem;
}

/* itemMeshFollowUpdate @0x40fde0 — second thrown-item pass (mesh follow).
 * Documented TODO stub, same contract as itemThrowUpdate. */
void itemMeshFollowUpdate(void *pItem) /* @0x40fde0 */
{
    (void)pItem;
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
