#ifndef OBJ_H
#define OBJ_H

#include "gx.h"

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
    int field_3c;               /* +0x3c */
    int _pad40;                 /* +0x40 untouched in worldNodeCtor */
    void *pParent;              /* +0x44 parent node (or the name MString ptr
                                 * as passed by playerSetupRound) */
} WorldNode;                    /* 0x48 */

typedef char WorldNodeSizeMustBe0x48[(sizeof(WorldNode) == 0x48) ? 1 : -1];

/* objListPush @0x4055c0 — head-insert a world node into g_pObjHead. */
void objListPush(WorldNode *pNode);

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
    int field_44;        /* +0x44 zeroed */
    struct ObjChildMesh *pNext; /* +0x48 head at WorldNode.pChildMeshHead (+0x08) */
} ObjChildMesh;          /* 0x4c */

typedef char ObjChildMeshSizeMustBe0x4c[(sizeof(ObjChildMesh) == 0x4c) ? 1 : -1];

/* objTurretAdd @0x405280 — allocate a turret entry for pNode, polarize
 * {flPosZ, flPosX}, scale nAngle to radians and head-insert at +0x0c. */
void objTurretAdd(WorldNode *pNode, int nPosX, int nPosY, int nPosZ,
                  short nAngle, int nTypeId);

/* objTurretSetValue @0x405370 — for every child mesh of pNode whose
 * nChannelKey matches nKey, write nValue1/nValue2 over +0x04/+0x08. */
void objTurretSetValue(WorldNode *pNode, int nKey, int nValue1, int nValue2);

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

#endif /* OBJ_H */
