#ifndef OBJ_COLLISION_H
#define OBJ_COLLISION_H

#include "gx.h"

/* obj_collision.h — ShotObj + shot-collision subsystem
 * (maniac.exe 0x402ba0/0x4035e0/0x404ac0/0x4054e0/0x406050/0x406110).
 * Cluster C of the obj subsystem. */

struct WorldNode;
struct ObjChildMesh;

/* ShotObj — one collision-response record built by objShotAdd (filled by
 * objShotCollide @0x4035e0 / objCollideCheck @0x404ac0, stored in the
 * node's +0x10 list; 0x44 bytes per the objShotAdd allocation). Layout
 * verified against the objShotCtor @0x406050 disassembly and the
 * objUpdatePhysics @0x405680 / objUpdateFire @0x405e10 consumers. */
typedef struct ShotObj {
    struct ShotObj *pNext;   /* +0x00 list link */
    int field_04;            /* +0x04 zeroed by objShotCtor */
    void *pAnimTarget;       /* +0x08 victim block whose +0x44 (WorldNode.pParent)
                              * feeds objWalkAnimSync; objShotCollide stores 0,
                              * objCollideCheck stores the other WorldNode */
    int *pSrc;               /* +0x0c sfx table ([1] = physics idx, [2] = fire idx) */
    GxVec2 v0;               /* +0x10 hit - vWorldA: objSetPos pass 1 (sub-object
                              * on the hit point); objCollideCheck: B - vWorldA */
    GxVec2 v1;               /* +0x18 {dA*k, reflected angle} (k = 0.2/0.5) or
                              * the objCollideCheck polar sum; fire picks the
                              * max v1.x weight, physics blends by it */
    GxVec2 v2;               /* +0x20 (hit + reflected dir) - vWorldA: objSetPos
                              * pass 2; objCollideCheck: midpoint - vWorldA */
    GxVec2 v3;               /* +0x28 {dB, angle(hit-B)} polar (objCollideCheck:
                              * copy of v1); physics picks the min
                              * v3.x/(v1.x+v3.x) ratio */
    GxVec2 v4;               /* +0x30 hit point (objCollideCheck: B) */
    GxVec2 v5;               /* +0x38 unit direction of the wall/edge segment
                              * (fromPolar({1.0, angle(V1-V0)})) */
    float flScale;           /* +0x40 impulse scale (= v1.x) */
} ShotObj;                   /* 0x44 */

/* objShotCtor @0x406050 — initialize a 0x44 ShotObj: six SetAngleZero
 * vec2s, then v3/v0/v2/v1/v4/v5/flScale (+0x28/+0x10/+0x20/+0x18/+0x30/
 * +0x38/+0x40), pSrc (+0x0c), pAnimTarget (+0x08), pNext/field_04 = 0.
 * Original __thiscall RET 0x3c. */
ShotObj *objShotCtor(ShotObj *pShot, int *pSrc, float flV0x, float flV0y,
                     float flV2x, float flV2y, float flV1x, float flV1y,
                     float flV4x, float flV4y, float flV5x, float flV5y,
                     float flSpeed, float flV3x, float flV3y,
                     void *pAnimTarget);                             /* @0x406050 */

/* objShotAdd @0x4054e0 — operator_new a 0x44 ShotObj, objShotCtor it with
 * the same 15 args, push onto pNode->pShotList (+0x10) head and bump the
 * shot count (+0x14). Original __thiscall RET 0x3c. */
void objShotAdd(struct WorldNode *pNode, int *pSrc, float flV0x, float flV0y,
                float flV2x, float flV2y, float flV1x, float flV1y,
                float flV4x, float flV4y, float flV5x, float flV5y,
                float flSpeed, float flV3x, float flV3y, void *pAnimTarget);

/* objShotListFree @0x406110 — recursively free a shot list: recurse on
 * pNext, then memFreeDirect the next node (the head itself is freed by
 * objShotListClear). Original __fastcall. */
void objShotListFree(ShotObj *pShot);                             /* @0x406110 */

/* objSegCollideCollect @0x402ba0 — extend the surface array (NULL
 * terminated, apList) with the pPeerMesh targets of every connection
 * edge (+0x38 list) of the listed surfaces whose segment geometry
 * touches the moving A/B segment of pSrc: normal-ray intersection from
 * A and B, a reflected-ray test and a perpendicular circle test (all
 * ±1.0 boxes), deduped against the existing entries. */
void objSegCollideCollect(struct WorldNode *pNode, struct ObjChildMesh *pSrc,
                          void **apList);                         /* @0x402ba0 */

/* objShotCollide @0x4035e0 — rebuild pNode's shot list (+0x10) for the
 * objUpdatePhysics response. Per enabled +0x08 sub-object: world
 * positions A (vWorldA + vPos) and B (vWorldB + vPosB), raycast the
 * surfaces (sceneRayFindNearest 500.0, pSurface cache), extend the list
 * via objSegCollideCollect, then four hit phases over the surfaces'
 * pEdgeList (+0x3c) and pConnList (+0x38) edges — segment intersection
 * and perpendicular-circle tests with reflected-direction records —
 * objShotAdd'ing each hit, and refresh the cache with
 * sceneRayFindSorted. Original __fastcall. */
void objShotCollide(struct WorldNode *pNode);                     /* @0x4035e0 */

/* objCollideCheck @0x404ac0 — the objUpdateFire counterpart: per enabled
 * sub-object, walk the g_pObjHead nodes and their +0x08 sub-objects and
 * objShotAdd a shot whenever the two radius circles overlap (height band
 * ±500 @0x44b2e4), pAnimTarget = the other node. Original __fastcall. */
void objCollideCheck(struct WorldNode *pNode);                    /* @0x404ac0 */

#endif /* OBJ_COLLISION_H */
