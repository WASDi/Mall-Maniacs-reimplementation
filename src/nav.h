#ifndef NAV_H
#define NAV_H

#include <windows.h>
#include "zone.h"
#include "scene.h"

/* nav.h — AI nav-mesh and navpoint ("NavBuoy") subsystem.
 *
 * levelSceneTexturesLoad turns every "FLOOR" scene object of the level
 * into a 0x48-byte AiNavNode (aiNavNodeCtorScene @0x428cf0): the node's
 * walk-edge list is built by aiNavNodeUpdate @0x428d50 from the floor
 * mesh's SceneMeshPrim fan stream (bType 3 = tri fans, 4 = quad lists,
 * fans with normalized-Y >= 0.6 are walkable). zoneWallListBuild
 * (zone.c) then dedups/planes the lists and sceneRayFindNearest
 * (scene_render.c) queries them for floor heights.
 *
 * The "nload <mall>.ai" startup command (nloadCmd @0x407e10) loads the
 * level's navpoint buoy graph: one 0x60-byte NavPoint per config block
 * ("ID"/"X"/"Y"/"Z" keys) with up to 8 precomputed-distance links
 * ("LINKS" list). The Dijkstra pathfinding over the buoys
 * (navPointRelaxCosts @0x425af0 and friends) is deferred. */

/* --- AI nav-mesh scan state (aiNavNodeCtorScene/aiNavNodeUpdate) ---
 * g_pNavMeshData is the current FLOOR mesh's SceneObjRenderInfo;
 * g_pNavTriCur/g_pNavTriEnd walk one SceneMeshPrim's fan stream (reset
 * to NULL per prim, stepped bFanIdxCount*2 bytes); g_nNavTriIdx indexes
 * the pPolyA pointer array. The cursor survives the aiNavNodeCtor
 * recursion so a fresh node resumes the same mesh mid-scan. */
extern SceneObjRenderInfo *g_pNavMeshData; /* @0x45e5d4 */
extern byte *g_pNavTriCur;                 /* @0x45e5d0 */
extern byte *g_pNavTriEnd;                 /* @0x45e5d8 */
extern int   g_nNavTriIdx;                 /* @0x45e5cc */

/* --- navpoint list (nloadCmd fills it from the level's .ai file) --- */
extern NavPoint *g_pNavPointHead;          /* @0x45d4d0 */
extern NavPoint *g_pNavPointTail;          /* @0x45d4d4 */
extern int   g_nNavPointCount;             /* @0x45d4d8 */
extern NavPoint *g_pNavPointSel;           /* @0x45e480 editor selection (deferred) */

/* AI nav edges (0x3c AiNavEdge records, see zone.h). */
void aiNavEdgeCtor(AiNavEdge *pEdge, int nV0x, int nV0y, int nV0z,
                   int nV1x, int nV1y, int nV1z);                    /* @0x428c70 */
AiNavNode *aiNavNodeCtor(AiNavNode *pNode, SceneNode *pSceneObj);    /* @0x428cb0 */
void aiNavNodeCtorScene(AiNavNode *pNode, SceneNode *pSceneObj);     /* @0x428cf0 */
void aiNavNodeUpdate(AiNavNode *pNode, SceneNode *pSceneObj);        /* @0x428d50 */
void aiNavNodeAddEdge(AiNavNode *pNode, int nV0x, int nV0y, int nV0z,
                      int nV1x, int nV1y, int nV1z);                 /* @0x429ba0 */

/* NavBuoy points (0x60 NavPoint records, see zone.h). */
NavPoint *navPointCtor(NavPoint *pPoint);                            /* @0x425760 */
void navPointListFreeAll(NavPoint *pHead);                           /* @0x4257a0 */
int  navPointAddLink(NavPoint *pPoint, NavPoint *pLink);             /* @0x4257f0 */
int  navPointListAdd(NavPoint *pPoint);                              /* @0x4258e0 */
NavPoint *navPointListGetHead(void);                                 /* @0x425ad0 */
NavPoint *navPointFindById(int nId);                                 /* @0x425c50 */

/* "nload <file.ai>" startup-script command: parse the buoy config file
 * (configParseFile grammar), rebuild the NavPoint list. Returns 0. */
int  nloadCmd(int nContext, LPCSTR pszArgs);                         /* @0x407e10 */

#endif /* NAV_H */
