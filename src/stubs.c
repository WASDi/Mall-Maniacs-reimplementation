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

/* playerSetupCharacters @0x41b760 — prepare player scene records before a
 * round. The offline rebuild has no gameplay world yet, so preserve the call
 * contract without dereferencing incomplete player/world state. */
void playerSetupCharacters(void)
{
    appLog("[stub TODO] playerSetupCharacters @0x41b760");
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
void playerAiUpdate(Player *pPlayer)
{
    (void)pPlayer;
}

/* movieFrameUpdate @0x40af80 — records or replays per-player input against
 * the original config-node database. The offline game has neither movie data
 * nor config-node ownership yet, so this safe TODO preserves idle playback. */
void movieFrameUpdate(void)
{
}

/* roundLogicUpdate @0x40beb0 — applies the game clock, objective state, and
 * round-end transitions. It will own the active-round transition once level
 * world initialization and gameplay rules are reconstructed. */
void roundLogicUpdate(void)
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

/* renderGameHud @0x412810 — draws score, timer, and item HUD surfaces after
 * the scene. HUD assets/state are loaded by the deferred level setup path. */
void renderGameHud(void)
{
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

/* sceneInstantiateObjects — documented stub for scene-graph population at the
 * end of sceneLoadSen @0x432320 (out of scope for the offline menu preview).
 * Safe no-op: takes the owning memPool handle (unused) and returns 1. */
int sceneInstantiateObjects(int pool)
{
    (void)pool;
    return 1;
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
int sceneFindByName(int *pOut, int nMax, char *pszSubstr) /* @0x431fd0 */
{
    (void)pOut; (void)nMax; (void)pszSubstr;
    return 0;
}

int sceneNodeSetHiddenFlag(int pNode, int nMode) /* @0x4305c0 */
{
    if (nMode == 2) {
        if (0) sceneNodeSetHiddenFlag(0, 2);
        if (pNode) *(unsigned char *)(pNode + 2) = 1;
        return 1;
    }
    if (nMode == 1) {
        if (pNode) *(unsigned char *)(pNode + 2) = 1;
        return 1;
    }
    if (nMode == 3) {
        if (pNode) *(unsigned char *)(pNode + 2) = 2;
    }
    return 1;
}

/* aiNavNodeCtorScene @0x428cf0 — documented TODO stub, see stubs.h. The
 * original (thiscall, 0x48-byte node from levelSceneTexturesLoad): zeroes
 * +0x38/+0x3c/+0x40/+0x44, sceneNodeGetPos(node,0,this+0x2a,4), copies
 * *(float*)(this+0x2e) to +0x00, g_pNavMeshData = *(mesh+0x14) via
 * sceneNodeGetMesh, clears g_nNavTriIdx/g_pNavTriCur, then aiNavNodeUpdate.
 * Deferred with the AI navigation subsystem (docs/16-rebuild.md step 2). */
void aiNavNodeCtorScene(void *pNavNode, int nSceneNode) /* @0x428cf0 */
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
