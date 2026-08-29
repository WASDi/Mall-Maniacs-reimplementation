#ifndef ZONE_H
#define ZONE_H

#include "obj.h"

struct SceneObjRenderInfo;  /* scene.h */

/* zone.h — AR/IN zone-connection nodes (maniac.exe 0x42b410..0x42b890).
 * roundStartInit creates one per "AR%02.2d"/"IN%02.2d" EventObject pair
 * after levelSetup; the nodes record which detail-meshes of the level
 * scene touch each room-entering AR zone so room interiors can be culled
 * while the player is elsewhere (zoneConnUpdateCulling @0x42b8f0).
 * Freed by roundTeardown via zoneConnUnlink @0x42b890. */

/* 0x1c-byte zone-connection node (layout from zoneConnCtor @0x42b410
 * disassembly). Head-inserted into the g_pZoneConnHead list. */
typedef struct ZoneConn {
    int              field_00;         /* +0x00 */
    struct ZoneConn *pPrev;            /* +0x04 previous head (list is LIFO) */
    struct ZoneConn *pNext;            /* +0x08 next node */
    int              nCount;           /* +0x0c detail-mesh indices collected */
    int             *pMeshIdx;         /* +0x10 nCount ints (memPoolAlloc) */
    EventObject     *pInZone;          /* +0x14 the IN-%02.2d zone object */
    void            *pDetailLevels;    /* +0x18 SceneDetailGrid* */
} ZoneConn;                            /* 0x1c */

/* 0x3c-byte nav-edge/connection record (layout from aiNavEdgeCtor
 * @0x428c70, aiNavNodeAddEdge @0x429ba0, zoneConnLink @0x429c90,
 * zoneWallPointSide @0x42a870). One record kind serves two lists: the
 * node's walk-edge list pEdgeList (filled by aiNavNodeAddEdge) and the
 * cross-mesh connection list pConnList (filled by zoneConnLink, which
 * sets pPeerMesh). Ints +0x24..+0x38 are the raw endpoints passed to
 * aiNavEdgeCtor; the float block +0x0c..+0x20 is derived from them after
 * linking: flV0Z=(float)nV0z, flV0X=(float)nV0x, flV1Z=(float)nV1z,
 * flV1X=(float)nV1x, and (+0x1c,+0x20) the unit direction of V0->V1 in
 * (X,Z): (dX/len, -dZ/len). */
typedef struct __attribute__((packed)) AiNavEdge {
    struct AiNavEdge *pNext;      /* +0x00 */
    struct AiNavEdge *pPrev;      /* +0x04 */
    struct AiNavNode  *pPeerMesh; /* +0x08 peer mesh (cross-mesh conn list only) */
    float flV0Z;                  /* +0x0c (float)nV0z */
    float flV0X;                  /* +0x10 (float)nV0x */
    float flV1Z;                  /* +0x14 (float)nV1z */
    float flV1X;                  /* +0x18 (float)nV1x */
    float flUnitX;                /* +0x1c polar dir X of V0->V1 */
    float flUnitNegZ;             /* +0x20 polar dir -Z of V0->V1 */
    int nV0x;                     /* +0x24 */
    int nV0y;                     /* +0x28 */
    int nV0z;                     /* +0x2c */
    int nV1x;                     /* +0x30 */
    int nV1y;                     /* +0x34 */
    int nV1z;                     /* +0x38 */
} AiNavEdge;                      /* 0x3c */

/* 0x48-byte AI nav node (layout from aiNavNodeCtor @0x428cb0,
 * aiNavNodeUpdate @0x428d50, zoneWallCalcPlane @0x42a1c0,
 * zoneAvoidWalls @0x402320, sceneRayFindNearest @0x42a750). One per
 * "FLOOR" scene object (levelSceneTexturesLoad); doubly linked into
 * g_pNavNodeList, re-sorted by flAvgY in zoneWallListBuild. Walkable-plane
 * test used by sceneRayFindNearest:
 *   y = (x - nRefX) * flSlopeX + (z - nRefZ) * flSlopeZ + nRefY. */
typedef struct __attribute__((packed)) AiNavNode {
    float flAvgY;                /* +0x00 floor height: ctor (float)nPosY, recomputed by zoneWallCalcPlane */
    float flSlopeZ;              /* +0x04 plane slope dY/dZ (zoneWallCalcPlane) */
    float flSlopeX;              /* +0x08 plane slope dY/dX */
    int   nRefX;                 /* +0x0c plane reference point (first conn nV0x) */
    int   nRefY;                 /* +0x10 plane reference height (first conn nV0y) */
    int   nRefZ;                 /* +0x14 plane reference point (first conn nV0z) */
    float flNormalX;             /* +0x18 walkable-surface unit normal */
    float flNormalY;             /* +0x1c vertical component; walkable when >= 0.6 */
    float flNormalZ;             /* +0x20 */
    int   dwReserved_24;         /* +0x24 unreferenced */
    short wReserved_28;          /* +0x28 unreferenced */
    int   nPosX;                 /* +0x2a sceneNodeGetPos ints (unaligned by design) */
    int   nPosY;                 /* +0x2e */
    int   nPosZ;                 /* +0x32 */
    short wPadding_36;           /* +0x36 */
    struct AiNavEdge *pConnList; /* +0x38 cross-mesh connection list (zoneConnLink) */
    struct AiNavEdge *pEdgeList; /* +0x3c walk-edge list (aiNavNodeAddEdge) */
    struct AiNavNode *pNext;     /* +0x40 */
    struct AiNavNode *pPrev;     /* +0x44 */
} AiNavNode;                     /* 0x48 */

/* List head (aiNavNodeUpdate/aiNavNodeCtor link here; zoneWallListBuild
 * re-sorts it by flAvgY). */
extern AiNavNode *g_pNavNodeList;   /* @0x45e5e0 */

/* --- AI nav-mesh scan state (aiNavNodeCtorScene @0x428cf0 /
 * aiNavNodeUpdate @0x428d50). g_pNavMeshData is the current FLOOR mesh's
 * SceneObjRenderInfo; g_pNavTriCur/g_pNavTriEnd walk one SceneMeshPrim's
 * fan stream (reset to NULL per prim, stepped bFanIdxCount*2 bytes);
 * g_nNavTriIdx indexes the pPolyA pointer array. */
extern struct SceneObjRenderInfo *g_pNavMeshData; /* @0x45e5d4 */
extern byte *g_pNavTriCur;                 /* @0x45e5d0 */
extern byte *g_pNavTriEnd;                 /* @0x45e5d8 */
extern int   g_nNavTriIdx;                 /* @0x45e5cc */

/* --- nav-point list (nloadCmd @0x407e10 fills it from the level's .ai
 * file; 0x60-byte points linked through pNext). The navpoint subsystem
 * (navPoint* @0x425760..0x425c90, Dijkstra pathfinding) is deferred —
 * structs kept here for the milestone-3 loader. */
#define NAVPOINT_LINKS 8
typedef struct __attribute__((packed)) NavPoint {
    int   nId;             /* +0x00 -1 until registered */
    struct NavPoint *pALink[8];         /* +0x04 up to 8 links */
    float pALinkDist[8];   /* +0x24 link distances */
    float flUnknown;       /* +0x44 */
    int   nLinkCount;      /* +0x48 */
    int   nPosX;           /* +0x4c */
    int   nPosY;           /* +0x50 */
    int   nPosZ;           /* +0x54 world position (height = avg floor y) */
    int   nFlags;          /* +0x58 */
    struct NavPoint *pNext;/* +0x5c g_pNavPointHead/Tail list */
} NavPoint;                             /* 0x60 */

extern NavPoint *g_pNavPointHead;  /* @0x45d4d0 */
extern NavPoint *g_pNavPointTail;  /* @0x45d4d4 */
extern int   g_nNavPointCount;     /* @0x45d4d8 */
extern NavPoint *g_pNavPointSel;   /* @0x45e480 */

/* List heads (zoneConnCtor writes them directly). */
extern void *g_pZoneConnHead;   /* @0x45e5e8 */
extern void *g_pZoneConnTail;   /* @0x45e5ec */
extern void *g_zoneConn;        /* @0x45e5e4 scratch cleared at ctor start */

/* zoneConnCtor @0x42b410 — build a 0x1c-byte connection node: collect the
 * intersecting detail-mesh indices (counting pass), allocate the index
 * array sized to that count, refill it, then prepend to the list.
 * Returns the node. Original __thiscall RET 0xc. */
void *zoneConnCtor(ZoneConn *pConn, EventObject *pInZone, EventObject *pArZone,
                   void *pDetailLevels);

/* zoneConnCollectMeshes @0x42b490 — walk the detail grid's filled column
 * cells (pDetailLevels->nColsFilled entries of pCells), test each mesh's
 * polygon vertices against the AR zone (objContainsPoint3D) and count
 * (pFill != 0) or store (pFill == 0) the matching cell indices. */
void zoneConnCollectMeshes(ZoneConn *pConn, EventObject *pArZone, int nFill);

#endif /* ZONE_H */
