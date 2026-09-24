#ifndef OBJ_WORLD_H
#define OBJ_WORLD_H

#include <stdint.h>
#include "gx.h"
#include "obj_event.h"

/* obj_world.h — WorldNode list + turret/child-mesh + physics/fire passes
 * (maniac.exe 0x402a20/0x404xxx/0x405xxx). Cluster B of the obj subsystem. */

/* WorldNode is the 0x48-byte world-position node built by worldNodeCtor
 * @0x402a20 and unlinked by objDtor @0x402ab0. All nodes live on the
 * doubly-linked g_pObjHead @0x4550d4 list (objListPush @0x4055c0). */
typedef struct WorldNode {
    struct WorldNode *pPrev;    /* +0x00 list link toward the head */
    struct WorldNode *pNext;    /* +0x04 list link toward the tail */
    void *pChildMeshHead;       /* +0x08 ObjChildMesh entry list (nodeAddChildMesh) */
    void *pTurretHead;          /* +0x0c ObjTurret entry list (objTurretAdd) */
    void *pShotList;            /* +0x10 shot list (objShotListFree) */
    int field_14;               /* +0x14 */
    int field_18;               /* +0x18 */
    int field_1c;               /* +0x1c */
    GxVec2 vPos;                /* +0x20 ground-plane position {x=z, y=x} */
    GxVec2 vPosB;               /* +0x28 copy of vPos */
    float flScaleA;             /* +0x30 nScale*g_flPi*g_dblAngleScale */
    float flScaleB;             /* +0x34 */
    float flScaleC;             /* +0x38 */
    float field_3c;             /* +0x3c (polar length written by objMovePolar) */
    float flImpulse;            /* +0x40 shot impulse (objUpdatePhysics/Fire, read
                                 * by player physics @0x42887c) */
    void *pParent;              /* +0x44 parent node (or the name MString ptr
                                 * as passed by playerSetupRound) */
} WorldNode;                    /* 0x48 */

#if MANIAC_32BIT_RUNTIME_LAYOUT /* runtime struct: native pointers widen it on 64-bit */
typedef char WorldNodeSizeMustBe0x48[(sizeof(WorldNode) == 0x48) ? 1 : -1];
#endif


/* ObjTurret is the 0x1c-byte turret entry built by objTurretAdd @0x405280
 * (levelObjectsCartsCameraInit collision cluster for each player's cart and
 * body). Layout from the objTurretAdd disassembly. */
/* 64-bit port (Phase 1.3): nTypeId/nChannelKey carry native scene-node /
 * mesh pointers as runtime keys (truncated ints in the 32-bit original).
 * They are runtime-only (malloc'd, never serialized), so they use
 * intptr_t to preserve full pointer bits on 64-bit hosts. */
typedef struct ObjTurret {
    intptr_t nTypeId;    /* +0x00 caller id (the owning record's scene node) */
    float flAngle;       /* +0x04 nAngle*g_flPi*g_dblAngleScale (radians) */
    GxVec2 vPolar;       /* +0x08 polar({flZ, flX}) of the position */
    GxVec2 vPos;         /* +0x10 {flZ, flX} copy */
    struct ObjTurret *pNext; /* +0x18 head at WorldNode.pTurretHead (+0x0c) */
} ObjTurret;             /* 0x1c */

#if MANIAC_32BIT_RUNTIME_LAYOUT /* runtime struct: native pointers widen it on 64-bit */
typedef char ObjTurretSizeMustBe0x1c[(sizeof(ObjTurret) == 0x1c) ? 1 : -1];
#endif


/* ObjChildMesh is the 0x4c-byte child-mesh/collision entry built by
 * nodeAddChildMesh @0x4053a0 and updated by nodeSetTransformFromChannels
 * @0x404e00. Layout from the nodeAddChildMesh disassembly. */
typedef struct ObjChildMesh {
    int nMeshId;         /* +0x00 caller mesh tag (1 in the player clusters) */
    int nValue1;         /* +0x04 written by objTurretSetValue */
    int nValue2;         /* +0x08 written by objTurretSetValue */
    intptr_t nChannelKey; /* +0x0c match key for objTurretSetValue (owner node) */
    float flExtentB;     /* +0x10 raw arg5 (75.0/A/B per row) */
    float flExtentA;     /* +0x14 raw arg4 (270/300/230/20 per row) */
    float flExtentC;     /* +0x18 raw arg7 (240.0/300.0) */
    GxVec2 vPolar;       /* +0x1c polar({flKeyZ, flX}) */
    GxVec2 vPos;         /* +0x24 {flKeyZ, flX} copy */
    GxVec2 vWorldA;      /* +0x2c fromPolar({polar.x, polar.y+flScaleA}) */
    GxVec2 vWorldB;      /* +0x34 copy of vWorldA */
    void *pSurface;      /* +0x3c sceneRayFindNearest hit node */
    float flHeight;      /* +0x40 raycast query height */
    float flVertVel;     /* +0x44 vertical velocity (float, walked by the
                          * support-point passes; zeroed by nodeAddChildMesh) */
    struct ObjChildMesh *pNext; /* +0x48 head at WorldNode.pChildMeshHead (+0x08) */
} ObjChildMesh;          /* 0x4c */

#if MANIAC_32BIT_RUNTIME_LAYOUT /* runtime struct: native pointers widen it on 64-bit */
typedef char ObjChildMeshSizeMustBe0x4c[(sizeof(ObjChildMesh) == 0x4c) ? 1 : -1];
#endif


extern WorldNode *g_pObjHead;  /* @0x4550d4 world-object list head */

/* objListPush @0x4055c0 — head-insert a world node into g_pObjHead. */
void objListPush(WorldNode *pNode);

/* objSetPos @0x404ef0 — move a world node on the ground plane: vPosB
 * (+0x28/+0x2c) = old vPos, vPos = (flX, flZ). */
void objSetPos(WorldNode *pNode, float flX, float flZ);      /* @0x404ef0 */

/* objShotListClear @0x405590 — free the node's shot list (+0x10) via
 * objShotListFree + memFreeDirect and zero the list head and count
 * (+0x10/+0x14). */
void objShotListClear(WorldNode *pNode);                     /* @0x405590 */

/* objUpdatePhysics @0x405680 / objUpdateFire @0x405e10 — world-object
 * collision-response passes driven by objUpdateAll @0x4055f0. Each runs
 * over the g_pObjHead list (nodes with +0x1c enabled): clear + rebuild the
 * shot list (objShotCollide / objCollideCheck), then apply the shot
 * impulse (objSetPos to the shot position, blend flScaleA->flScaleB, and
 * flImpulse from the shot direction · slope), with a 3D impact sfx. */
void objUpdatePhysics(void);                                 /* @0x405680 */
void objUpdateFire(void);                                    /* @0x405e10 */

/* objUpdateAll @0x4055f0 — clear every world node's +0x40 scratch and
 * +0x18 channels-dirty flag, then objUpdatePhysics/objUpdateFire/
 * objUpdatePhysics (stubs while the world-item subsystem is rebuilt). */
void objUpdateAll(void);

/* objTurretAdd @0x405280 — allocate a turret entry for pNode, polarize
 * {flPosZ, flPosX}, scale nAngle to radians and head-insert at +0x0c. */
void objTurretAdd(WorldNode *pNode, int nPosX, int nPosY, int nPosZ,
                  short nAngle, intptr_t nTypeId);

/* objTurretSetValue @0x405370 — for every child mesh of pNode whose
 * nChannelKey matches nKey, write nValue1/nValue2 over +0x04/+0x08. */
void objTurretSetValue(WorldNode *pNode, intptr_t nKey, int nValue1, int nValue2);

/* objDistToPoint @0x405050 — Euclidean ground-plane distance from the
 * node's vPos to {flA, flB} (components pair in vPos order: flA vs the
 * z-slot, flB vs the x-slot). */
float objDistToPoint(WorldNode *pNode, float flA, float flB);

/* nodeChannelAvgFloat @0x4050c0 — average flHeight over the child-mesh
 * entries whose nChannelKey matches nChannelKey; 0.0f when none match. */
float nodeChannelAvgFloat(WorldNode *pNode, intptr_t nChannelKey);

/* objFindTurret @0x405120 — first child-mesh entry whose nChannelKey
 * matches nChannelKey (walks the +0x08 ObjChildMesh list), or NULL. */
ObjChildMesh *objFindTurret(WorldNode *pNode, intptr_t nChannelKey);

/* objDistTo @0x404fe0 — Euclidean ground-plane distance between the two
 * nodes' vPos. */
float objDistTo(WorldNode *pA, WorldNode *pB);

/* objAngleTo @0x405010 — polar angle (atan2) from pA's vPos to pB's vPos. */
float objAngleTo(WorldNode *pA, WorldNode *pB);

/* objAngleToPoint @0x405080 — polar angle (atan2) from pNode's vPos to
 * {flA, flB}. */
float objAngleToPoint(WorldNode *pNode, float flA, float flB);

/* objMovePolar @0x404f10 — vPosB = vPos; vPos += fromPolar({flLen, flAng});
 * store flLen/flAng into +0x3c/+0x38. */
void objMovePolar(WorldNode *pNode, float flLen, float flAng);

/* objSetAngle @0x404f70 — flScaleB = flScaleA; flScaleA = flAngle; rebuild
 * every child mesh's vWorldA from {vPolar.x, vPolar.y + flScaleA}. */
void objSetAngle(WorldNode *pNode, float flAngle);

/* objPolarPosLookup @0x405140 — turret entry keyed by nTypeId; return the
 * (int) world x component of fromPolar({vPolar.x, flScaleA + vPolar.y})
 * + vPos. */
int objPolarPosLookup(WorldNode *pNode, intptr_t nTypeId);

/* objPolarPosLookup2 @0x4051c0 — same as objPolarPosLookup but returns the
 * world z component. */
int objPolarPosLookup2(WorldNode *pNode, intptr_t nTypeId);

/* objListFindFloat @0x405240 — turret entry keyed by nTypeId; return its
 * absolute heading as a 15-bit binary angle
 * (int)((flScaleA + flAngle) * (65536/π) * 0.5). */
int objListFindFloat(WorldNode *pNode, intptr_t nTypeId);

/* nodeAddChildMesh @0x4053a0 — allocate a 0x4c child-mesh entry for pNode:
 * polarized {flKeyZ, flX} position, raw extents, channel key and mesh tag,
 * plus the fromPolar world coords offset by pNode->flScaleA; head-insert
 * at +0x08. */
void nodeAddChildMesh(WorldNode *pNode, int nX, int nY, int nKeyZ,
                      float flExtentA, float flExtentB, intptr_t nChannelKey,
                      float flExtentC, int nMeshId);

/* nodeSetTransformFromChannels @0x404e00 — place pNode at {nPosZ, nPosX}
 * (vPos/vPosB), rotate by nAngle, then recompute every child mesh's world
 * coords and raycast its surface with sceneRayFindNearest @0x42a750. */
void nodeSetTransformFromChannels(WorldNode *pNode, int nPosX, int nPosY,
                                  int nPosZ, int nUnk, short nAngle);

/* worldNodeCtor @0x402a20 — init a 0x48-byte world node: zero links,
 * vPos = {z, x} (both copies), scale = nScale*g_flPi*g_dblAngleScale into
 * +0x30..+0x38, parent at +0x44, then objListPush. */
WorldNode *worldNodeCtor(void *pMem, int nX, int nY, int nZ, short nScale,
                          void *pParent);

/* objDtor @0x402ab0 — unlink from g_pObjHead, free the shot list (+0x10)
 * and the two turret sub-structs (+0x08/+0x0c) when present. */
void objDtor(WorldNode *pNode);

/* objSegCollideCollect needs WorldNode/ObjChildMesh forward decl; declare here
 * so obj_world.c can call it without including obj_collision.h directly if needed.
 * Full prototype lives in obj_collision.h. */
struct ObjChildMesh;
void objSegCollideCollect(WorldNode *pNode, struct ObjChildMesh *pSrc, void **apList);

#endif /* OBJ_WORLD_H */
