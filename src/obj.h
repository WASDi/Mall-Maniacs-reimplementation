#ifndef OBJ_H
#define OBJ_H

#include "gx.h"
#include "scene.h"

/* obj.h — EventObject registry + point-in-zone tests (maniac.exe
 * 0x414xxx). EventObjects are the named zone/trigger objects ("AR00",
 * "IN00", shopping zones, ...) whose 4-byte name acts as the integer id
 * used by objFindById (roundStartInit passes the first dword of the
 * formatted name, verified in its disassembly). Creation happens through
 * the level startup script: levels[%d]/startup runs "eload <file>.eo"
 * (eloadCmd @0x406cb0) which parses the DFF-format .eo file, builds one
 * EventObject per block via sceneObjCtor4 @0x414700 + eventObjAddLine
 * @0x4147c0 and registers it with objHashRegister @0x4148f0.
 * objHashFreeAll @0x414950 clears the set (roundTeardown @0x40ad1f and
 * at the top of every eload). */

typedef unsigned char byte;

/* One 0x20-byte boundary line of a zone polygon. Coordinates are relative
 * to the EventObject origin (+0x38/+0x3c). Layout from objContainsPoint
 * @0x414bb0 disassembly: x1,y1,x2,y2 (floats), nx,ny unit normal, then
 * the hash/list links. */
typedef struct ObjLine {
    float x1;        /* +0x00 */
    float y1;        /* +0x04 */
    float x2;        /* +0x08 */
    float y2;        /* +0x0c */
    float nx;        /* +0x10 unit normal x */
    float ny;        /* +0x14 unit normal y */
    struct ObjLine *pNext;   /* +0x18 */
    struct ObjLine *pPrev;   /* +0x1c */
} ObjLine;           /* 0x20 */

/* EventObject: 0x50-byte zone/trigger object. Layout from objContainsPoint
 * @0x414bb0, objContainsPoint3D @0x414b60, objFindById @0x414a90,
 * objHashRehash @0x4148c0 and sceneObjCtor4 @0x414730. */
typedef struct EventObject {
    int      field_00;      /* +0x00 */
    ObjLine *pLineList;     /* +0x04 zone polygon line list */
    int      nId;           /* +0x08 id (4-byte name tag), hash key */
    int      field_0c;      /* +0x0c */
    int      field_10;      /* +0x10 */
    int      field_14;      /* +0x14 */
    int      field_18;      /* +0x18 */
    int      field_1c;      /* +0x1c */
    int      field_20;      /* +0x20 */
    int      field_24;      /* +0x24 */
    int      field_28;      /* +0x28 */
    int      field_2c;      /* +0x2c */
    int      field_30;      /* +0x30 */
    int      field_34;      /* +0x34 */
    float    flOriginX;     /* +0x38 zone origin x */
    float    flOriginZ;     /* +0x3c zone origin z */
    float    flHeightA;     /* +0x40 vertical bound a */
    float    flHeightB;     /* +0x44 vertical bound b */
    struct EventObject *pHashNext;  /* +0x48 id-hash chain next */
    struct EventObject *pHashPrev;  /* +0x4c id-hash chain prev */
} EventObject;              /* 0x50 */

#define OBJ_HASH_BUCKETS 0xff

/* g_apGrabbMesh @0x4583b8 — SceneObjTypeDef table indexed by world item id
 * (0x2c-byte stride: index = id * 11 in the original dword addressing). */
extern SceneObjTypeDef g_apGrabbMesh[31];           /* @0x4583b8 */

/* objHashRemoveFree @0x414990 — unlink the EventObject from its id-hash
 * bucket (bucket head, +0x48 next and +0x4c prev links), then objHashDtor
 * and free the object. */
void objHashRemoveFree(EventObject *pObj);           /* @0x414990 */

/* objFindById @0x414a90 — return the (nIndex+1)-th EventObject with
 * id == nId in the g_apObjHashBuckets table (bucket = id % 0xff, chain
 * +0x48). Lazily zeroes the bucket table on first use. Returns NULL when
 * nIndex < 0 or no match. */
EventObject *objFindById(int nId, int nIndex);

/* objHashNextSame @0x414a40 — next EventObject with the SAME 4-byte id in
 * the id-hash chain, or NULL when the chain ends or the next object has a
 * different id. */
EventObject *objHashNextSame(EventObject *pObj);

/* objContainsPoint @0x414bb0 — 2D point-in-zone test over the polygon
 * line list: ray-casts a horizontal line at the point, finds the closest
 * crossing segment (squared distance) and tests its half-plane normal. */
int objContainsPoint(EventObject *pObj, float flX, float flY);

/* objContainsPoint3D @0x414b60 — vertical bound check (flZ against
 * +0x40/+0x44 with +/- 10) then objContainsPoint on (flX, flY). */
int objContainsPoint3D(EventObject *pObj, float flX, float flY, float flZ);

/* lineRecordNormal @0x414620 — unit normal (nx,ny) of the line segment,
 * the polar direction rotated by -90 degrees (g_flHalfPi @0x44b270). */
void lineRecordNormal(ObjLine *pLine);

/* lineRecordCtor @0x4145d0 — store x1,y1,x2,y2, clear the list links and
 * compute the normal. Returns pLine. */
ObjLine *lineRecordCtor(ObjLine *pLine, float flX1, float flY1, float flX2, float flY2);

/* objSubDtor @0x414680 — recursively free a line-record chain (+0x18). */
void objSubDtor(ObjLine *pLine);

/* objHashDtor @0x414760 — free an EventObject: clear its bucket head,
 * objSubDtor+free the line list, recurse the +0x48 hash chain. */
void objHashDtor(EventObject *pObj);

/* objHashRegister @0x4148f0 — head-insert into bucket id % 0xff of
 * g_apObjHashBuckets (lazily zeroed on first use, g_nObjHashInit). */
void objHashRegister(EventObject *pObj);

/* objHashFreeAll @0x414950 — objHashDtor + free every bucket chain and
 * zero the buckets (the init flag stays set). */
void objHashFreeAll(void);

/* sceneObjCtor4 @0x414700 — zero the 0x50-byte EventObject and set
 * nId (+0x08), origin (+0x38/+0x3c) and vertical bounds (+0x40/+0x44). */
/* sceneObjCtor3 @0x4146a0 — zero the 0x50-byte EventObject and set nId,
 * origin (+0x38/+0x3c) and both vertical bounds (+0x40/+0x44 = flHeight).
 * Landed thrown-item pickups (itemThrowUpdate) and level-event bonus
 * items use it. */
void sceneObjCtor3(EventObject *pObj, int nId, float flX, float flY,
                   float flHeight);

void sceneObjCtor4(EventObject *pObj, int nId, float flX, float flY,
                   float flHeight, float flHeight2);

/* eventObjAddLine @0x4147c0 — append a zone boundary line given in
 * absolute coordinates: the stored record is relative to the object
 * origin, head-inserted into the +0x04 line list. */
void eventObjAddLine(EventObject *pObj, float flX1, float flY1,
                     float flX2, float flY2);

/* eloadCmd @0x406cb0 — startup-script/console "eload <file>": parse the
 * DFF-format .eo file, objHashFreeAll, then one EventObject per block
 * ("_<digits>" keys -> numeric id via fmtAtoi, other keys use the first
 * dword of the 4-char key; id 0x1f is the skipped Burger object), with
 * its "line[%d]" polygon records and up to 5 "values" ints (+0x10..).
 * Returns 0. */
int eloadCmd(int nContext, LPCSTR pszArgs);

/* --- world-object list (playerSetupRound sub-objects) ---
 * WorldNode is the 0x48-byte world-position node built by worldNodeCtor
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

typedef char WorldNodeSizeMustBe0x48[(sizeof(WorldNode) == 0x48) ? 1 : -1];

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

/* objListPush @0x4055c0 — head-insert a world node into g_pObjHead. */
void objListPush(WorldNode *pNode);

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
void objShotAdd(WorldNode *pNode, int *pSrc, float flV0x, float flV0y,
                float flV2x, float flV2y, float flV1x, float flV1y,
                float flV4x, float flV4y, float flV5x, float flV5y,
                float flSpeed, float flV3x, float flV3y, void *pAnimTarget);

/* objShotListFree @0x406110 — recursively free a shot list: recurse on
 * pNext, then memFreeDirect the next node (the head itself is freed by
 * objShotListClear). Original __fastcall. */
void objShotListFree(ShotObj *pShot);                             /* @0x406110 */


/* objShotCollide @0x4035e0 — rebuild pNode's shot list (+0x10) for the
 * objUpdatePhysics response. Per enabled +0x08 sub-object: world
 * positions A (vWorldA + vPos) and B (vWorldB + vPosB), raycast the
 * surfaces (sceneRayFindNearest 500.0, pSurface cache), extend the list
 * via objSegCollideCollect, then four hit phases over the surfaces'
 * pEdgeList (+0x3c) and pConnList (+0x38) edges — segment intersection
 * and perpendicular-circle tests with reflected-direction records —
 * objShotAdd'ing each hit, and refresh the cache with
 * sceneRayFindSorted. Original __fastcall. */
void objShotCollide(WorldNode *pNode);                            /* @0x4035e0 */

/* objCollideCheck @0x404ac0 — the objUpdateFire counterpart: per enabled
 * sub-object, walk the g_pObjHead nodes and their +0x08 sub-objects and
 * objShotAdd a shot whenever the two radius circles overlap (height band
 * ±500 @0x44b2e4), pAnimTarget = the other node. Original __fastcall. */
void objCollideCheck(WorldNode *pNode);                           /* @0x404ac0 */

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

/* ObjTurret is the 0x1c-byte turret entry built by objTurretAdd @0x405280
 * (levelObjectsCartsCameraInit collision cluster for each player's cart and
 * body). Layout from the objTurretAdd disassembly. */
typedef struct ObjTurret {
    int nTypeId;         /* +0x00 caller id (the owning record's scene node) */
    float flAngle;       /* +0x04 nAngle*g_flPi*g_dblAngleScale (radians) */
    GxVec2 vPolar;       /* +0x08 polar({flZ, flX}) of the position */
    GxVec2 vPos;         /* +0x10 {flZ, flX} copy */
    struct ObjTurret *pNext; /* +0x18 head at WorldNode.pTurretHead (+0x0c) */
} ObjTurret;             /* 0x1c */

typedef char ObjTurretSizeMustBe0x1c[(sizeof(ObjTurret) == 0x1c) ? 1 : -1];

/* ObjChildMesh is the 0x4c-byte child-mesh/collision entry built by
 * nodeAddChildMesh @0x4053a0 and updated by nodeSetTransformFromChannels
 * @0x404e00. Layout from the nodeAddChildMesh disassembly. */
typedef struct ObjChildMesh {
    int nMeshId;         /* +0x00 caller mesh tag (1 in the player clusters) */
    int nValue1;         /* +0x04 written by objTurretSetValue */
    int nValue2;         /* +0x08 written by objTurretSetValue */
    int nChannelKey;     /* +0x0c match key for objTurretSetValue (owner node) */
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

typedef char ObjChildMeshSizeMustBe0x4c[(sizeof(ObjChildMesh) == 0x4c) ? 1 : -1];

/* objSegCollideCollect @0x402ba0 — extend the surface array (NULL
 * terminated, apList) with the pPeerMesh targets of every connection
 * edge (+0x38 list) of the listed surfaces whose segment geometry
 * touches the moving A/B segment of pSrc: normal-ray intersection from
 * A and B, a reflected-ray test and a perpendicular circle test (all
 * ±1.0 boxes), deduped against the existing entries. */
void objSegCollideCollect(WorldNode *pNode, ObjChildMesh *pSrc,
                          void **apList);                         /* @0x402ba0 */

/* objTurretAdd @0x405280 — allocate a turret entry for pNode, polarize
 * {flPosZ, flPosX}, scale nAngle to radians and head-insert at +0x0c. */
void objTurretAdd(WorldNode *pNode, int nPosX, int nPosY, int nPosZ,
                  short nAngle, int nTypeId);

/* objTurretSetValue @0x405370 — for every child mesh of pNode whose
 * nChannelKey matches nKey, write nValue1/nValue2 over +0x04/+0x08. */
void objTurretSetValue(WorldNode *pNode, int nKey, int nValue1, int nValue2);

/* objDistToPoint @0x405050 — Euclidean ground-plane distance from the
 * node's vPos to {flA, flB} (components pair in vPos order: flA vs the
 * z-slot, flB vs the x-slot). */
float objDistToPoint(WorldNode *pNode, float flA, float flB);

/* nodeChannelAvgFloat @0x4050c0 — average flHeight over the child-mesh
 * entries whose nChannelKey matches nChannelKey; 0.0f when none match. */
float nodeChannelAvgFloat(WorldNode *pNode, int nChannelKey);

/* objFindTurret @0x405120 — first child-mesh entry whose nChannelKey
 * matches nChannelKey (walks the +0x08 ObjChildMesh list), or NULL. */
ObjChildMesh *objFindTurret(WorldNode *pNode, int nChannelKey);

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
int objPolarPosLookup(WorldNode *pNode, int nTypeId);

/* objPolarPosLookup2 @0x4051c0 — same as objPolarPosLookup but returns the
 * world z component. */
int objPolarPosLookup2(WorldNode *pNode, int nTypeId);

/* objListFindFloat @0x405240 — turret entry keyed by nTypeId; return its
 * absolute heading as a 15-bit binary angle
 * (int)((flScaleA + flAngle) * (65536/π) * 0.5). */
int objListFindFloat(WorldNode *pNode, int nTypeId);

/* objFindByIdInRange @0x414af0 — (nIndex+1)-th EventObject whose id is in
 * [nIdMin, nIdMax], scanned in ascending id order; 0 when none. */
EventObject *objFindByIdInRange(int nIdMin, int nIdMax, int nIndex);

/* nodeAddChildMesh @0x4053a0 — allocate a 0x4c child-mesh entry for pNode:
 * polarized {flKeyZ, flX} position, raw extents, channel key and mesh tag,
 * plus the fromPolar world coords offset by pNode->flScaleA; head-insert
 * at +0x08. */
void nodeAddChildMesh(WorldNode *pNode, int nX, int nY, int nKeyZ,
                      float flExtentA, float flExtentB, int nChannelKey,
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

/* objUpdateAll @0x4055f0 — clear every world node's +0x40 scratch and
 * +0x18 channels-dirty flag, then objUpdatePhysics/objUpdateFire/
 * objUpdatePhysics (stubs while the world-item subsystem is rebuilt). */
void objUpdateAll(void);

#endif /* OBJ_H */
