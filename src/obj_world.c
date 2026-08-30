#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "obj_world.h"
#include "obj_collision.h"
#include "gx.h"
#include "scene.h"
#include "scene_system.h"
#include "config.h"
#include "util.h"
#include "pool.h"
#include "time.h"
#include "zone.h"
#include "player_physics.h"
#include "stubs.h"
#include "sound.h"

/* =====================================================================
 * obj_world.c — WorldNode + turret/child-mesh + update passes
 * World-node cluster — WorldNode lifecycle, turret/child-mesh,
 * helpers and physics/fire passes
 * ===================================================================== */

WorldNode *g_pObjHead;  /* @0x4550d4 world-object list head */

static const float g_flPi = 3.1415925f;        /* @0x44b2d0 (bytes D0 0F 49 40) */
static const double g_dblAngleScale = 3.0547e-05; /* @0x44b2c8 (bytes 10 00 10 00 10 00 00 3F) */


/* objUpdateAll @0x4055f0 — clear every world node's per-frame state
 * (+0x40 scratch and +0x18 channels-dirty flag), then run the world
 * physics/fire passes (physics twice, matching the original order; the
 * g_nMovieFrame 399..500 guards only wrap nopDebugStub no-ops). */
void objUpdateAll(void) /* @0x4055f0 */
{
    WorldNode *pNode;

    for (pNode = g_pObjHead; pNode != NULL; pNode = pNode->pPrev) { /* +0x00 walk toward tail @0x4055f6 */
        pNode->flImpulse = 0.0f;  /* +0x40 @0x4055fc */
        pNode->field_18 = 0;  /* +0x18 @0x405601 */
    }
    objUpdatePhysics();       /* @0x405680 @0x405611 */
    objUpdateFire();          /* @0x405e10 @0x405621 */
    objUpdatePhysics();       /* @0x40562c */
}

/* objListPush @0x4055c0 — head-insert a world node into g_pObjHead.
 * The node's +0x00/+0x04 links form the list chain. */
void objListPush(WorldNode *pNode) /* @0x4055c0 */
{
    if (g_pObjHead != NULL) {
        g_pObjHead->pNext = pNode;
        pNode->pPrev = g_pObjHead;
        g_pObjHead = pNode;
        return;
    }
    g_pObjHead = pNode;
}

/* worldNodeCtor @0x402a20 — init a 0x48-byte world node: zero the link and
 * sub-struct fields, vPos = {(float)nZ, (float)nX} (ground plane stored as
 * {z, x}), copy it to vPosB, scale = nScale*g_flPi*g_dblAngleScale into
 * +0x30/+0x34/+0x38, parent at +0x44, then objListPush. */
WorldNode *worldNodeCtor(void *pMem, int nX, int nY, int nZ, short nScale,
                          void *pParent) /* @0x402a20 */
{
    WorldNode *pNode = (WorldNode *)pMem;
    (void)nY;
    float flScale;

    gxVec2SetAngleZero(&pNode->vPos);       /* +0x20 @0x434f90 @0x402a2a */
    gxVec2SetAngleZero(&pNode->vPosB);      /* +0x28 @0x402a34 */
    pNode->vPos.x = (float)nZ;              /* @0x402a45 */
    pNode->pPrev = NULL;                    /* @0x402a40 */
    pNode->pNext = NULL;                    /* @0x402a42 */
    pNode->vPos.y = (float)nX;              /* +0x24 @0x402a57 */
    pNode->pShotList = NULL;                /* +0x10 @0x402a54 */
    pNode->pChildMeshHead = NULL;            /* +0x08 @0x402a51 */
    pNode->pTurretHead = NULL;               /* +0x0c @0x402a4e */
    pNode->field_3c = 0;                    /* @0x402a4b */
    pNode->field_14 = 0;                    /* @0x402a75 */
    pNode->field_1c = 0;                    /* @0x402a7b */
    pNode->vPosB.x = pNode->vPos.x;         /* @0x402a5f */
    pNode->vPosB.y = pNode->vPos.y;         /* @0x402a5c..0x402a61 */
    pNode->pParent = pParent;               /* +0x44 @0x402a78 */
    flScale = (float)nScale * g_flPi * (float)g_dblAngleScale; /* @0x44b2d0/0x44b2c8 @0x402a7e */
    pNode->flScaleA = flScale;              /* +0x30 @0x402a8a */
    pNode->flScaleB = flScale;              /* +0x38 @0x402a8d */
    pNode->flScaleC = flScale;              /* +0x34 @0x402a90 */
    objListPush(pNode);                     /* @0x4055c0 @0x402a93 */
    return pNode;
}

/* objDtor @0x402ab0 — unlink the node from g_pObjHead, free the shot list
 * at +0x10 (objShotListFree @0x406110 + memFreeDirect) and the two turret
 * sub-structs at +0x08/+0x0c (objTurretListFree @0x402b40 /
 * objTurretListFree2 @0x402b70 on their embedded lists) when present. */
void objDtor(WorldNode *pNode) /* @0x402ab0 */
{
    void *pSub;

    if (g_pObjHead == pNode) {                       /* @0x4550d4 @0x402ab8 */
        g_pObjHead = pNode->pPrev;                   /* @0x402abc */
    } else if (pNode->pNext != NULL) {               /* @0x402ac5 */
        pNode->pNext->pPrev = pNode->pPrev;          /* @0x402acc */
    }
    if (pNode->pPrev != NULL) {                      /* @0x402ad0 */
        pNode->pPrev->pNext = pNode->pNext;          /* @0x402ad6 */
    }
    if (pNode->pShotList != NULL) {                  /* +0x10 @0x402add */
        objShotListFree(pNode->pShotList);           /* @0x406110 @0x402ae6 */
        memFreeDirect(pNode->pShotList);             /* @0x43dd37 @0x402aeb */
    }
    pSub = pNode->pChildMeshHead;                    /* +0x08 @0x402af4 */
    if (pSub != NULL) {
        if (*(void **)((char *)pSub + 0x48) != NULL) { /* @0x402afb */
            objTurretListFree(1);                    /* @0x402b40 @0x402b04 */
        }
        memFreeDirect(pSub);                         /* @0x402b0a */
    }
    pSub = pNode->pTurretHead;                       /* +0x0c @0x402b12 */
    if (pSub != NULL) {
        if (*(void **)((char *)pSub + 0x18) != NULL) { /* @0x402b1a */
            objTurretListFree2(1);                   /* @0x402b70 @0x402b23 */
        }
        memFreeDirect(pSub);                         /* @0x402b29 */
    }
}

/* --- per-node queries (playerUpdateAI / pickup / camera paths) --- */

static const float g_flZero = 0.0f; /* @0x44b244 */
static const float g_flOne = 1.0f;  /* @0x44b260 */

/* objDistTo @0x404fe0 — Euclidean ground-plane distance between two nodes'
 * vPos (x vs x, y vs y slots). */
float objDistTo(WorldNode *pA, WorldNode *pB) /* @0x404fe0 */
{
    float flDx = pB->vPos.y - pA->vPos.y; /* +0x24 @0x404fe7 */
    float flDz = pB->vPos.x - pA->vPos.x; /* +0x20 @0x404fee */
    return sqrtf(flDx * flDx + flDz * flDz); /* @0x404ff5..0x404ffb */
}

/* objAngleTo @0x405010 — polar angle (atan2) of the vector from pA's vPos
 * to pB's vPos (mathVec2Polar .y over the {x, y} component delta). */
float objAngleTo(WorldNode *pA, WorldNode *pB) /* @0x405010 */
{
    GxVec2 vDelta;
    GxVec2 vPolar;

    gxVec2Set(&vDelta, pB->vPos.x - pA->vPos.x, pB->vPos.y - pA->vPos.y);
    mathVec2Polar(&vPolar, &vDelta); /* .y keeps the angle @0x405028 */
    return vPolar.y;
}

/* objAngleToPoint @0x405080 — polar angle (atan2) from pNode's vPos to the
 * point {flA, flB} (flA vs the +0x20 slot, flB vs the +0x24 slot). */
float objAngleToPoint(WorldNode *pNode, float flA, float flB) /* @0x405080 */
{
    GxVec2 vDelta;
    GxVec2 vPolar;

    gxVec2Set(&vDelta, flA - pNode->vPos.x, flB - pNode->vPos.y);
    mathVec2Polar(&vPolar, &vDelta); /* @0x405098 */
    return vPolar.y;
}

/* objMovePolar @0x404f10 — save vPos into vPosB, advance vPos by
 * fromPolar({flLen, flAng}) and store flLen/flAng into +0x3c/+0x38. */
void objMovePolar(WorldNode *pNode, float flLen, float flAng) /* @0x404f10 */
{
    GxVec2 vPolar;
    GxVec2 vCart;
    GxVec2 vSum;

    pNode->vPosB = pNode->vPos; /* +0x28 @0x404f24 */
    gxVec2Set(&vPolar, flLen, flAng);
    gxVec2FromPolar(&vCart, &vPolar);                 /* @0x404f2b */
    gxVec2Add(&vSum, &pNode->vPos, &vCart);           /* @0x404f3b */
    pNode->vPos = vSum;                               /* @0x404f45 */
    pNode->field_3c = flLen;   /* +0x3c @0x404f55 (raw float store) */
    pNode->flScaleC = flAng;   /* +0x38 @0x404f58 (raw float store) */
}

/* objSetAngle @0x404f70 — set the node heading: flScaleB = flScaleA,
 * flScaleA = flAngle, then recompute every child mesh's vWorldA from
 * {vPolar.x, vPolar.y + flScaleA} (saving the old one into vWorldB). */
void objSetAngle(WorldNode *pNode, float flAngle) /* @0x404f70 */
{
    ObjChildMesh *pMesh;
    GxVec2 vIn;

    pNode->flScaleB = pNode->flScaleA; /* +0x34 @0x404f83 */
    pNode->flScaleA = flAngle;         /* +0x30 @0x404f86 */
    for (pMesh = (ObjChildMesh *)pNode->pChildMeshHead; pMesh != NULL;
         pMesh = pMesh->pNext) {                       /* +0x48 @0x404fc3 */
        pMesh->vWorldB = pMesh->vWorldA;               /* +0x34 @0x404f94 */
        gxVec2Set(&vIn, pMesh->vPolar.x, pMesh->vPolar.y + pNode->flScaleA);
        gxVec2FromPolar(&pMesh->vWorldA, &vIn);        /* +0x2c @0x404fb3 */
    }
}

/* objPolarPosLookup @0x405140 — find pNode's turret entry whose nTypeId
 * matches nTypeId and return (int) of the world x component of
 * fromPolar({vPolar.x, flScaleA + vPolar.y}) + vPos (the +0x24 slot). */
int objPolarPosLookup(WorldNode *pNode, int nTypeId) /* @0x405140 */
{
    ObjTurret *pTurret;
    GxVec2 vIn;
    GxVec2 vCart;
    GxVec2 vSum;

    for (pTurret = (ObjTurret *)pNode->pTurretHead; pTurret != NULL;
         pTurret = pTurret->pNext) {                   /* +0x18 @0x405155 */
        if (pTurret->nTypeId == nTypeId) {             /* @0x405151 */
            break;
        }
    }
    if (pTurret == NULL) {                             /* @0x40514b */
        return 0;                                      /* EAX=0 @0x40515c */
    }
    gxVec2Set(&vIn, pTurret->vPolar.x, pNode->flScaleA + pTurret->vPolar.y);
    gxVec2FromPolar(&vCart, &vIn);                     /* @0x405181 */
    gxVec2Add(&vSum, &vCart, &pNode->vPos);            /* @0x405194 */
    return (int)vSum.y;                                /* ftol @0x4051ad */
}

/* objPolarPosLookup2 @0x4051c0 — same as objPolarPosLookup but returns the
 * world z component (the +0x20 slot of the sum). */
int objPolarPosLookup2(WorldNode *pNode, int nTypeId) /* @0x4051c0 */
{
    ObjTurret *pTurret;
    GxVec2 vIn;
    GxVec2 vCart;
    GxVec2 vSum;

    for (pTurret = (ObjTurret *)pNode->pTurretHead; pTurret != NULL;
         pTurret = pTurret->pNext) {                   /* @0x4051d5 */
        if (pTurret->nTypeId == nTypeId) {             /* @0x4051d1 */
            break;
        }
    }
    if (pTurret == NULL) {                             /* @0x4051cb */
        return 0;                                      /* @0x4051dc */
    }
    gxVec2Set(&vIn, pTurret->vPolar.x, pNode->flScaleA + pTurret->vPolar.y);
    gxVec2FromPolar(&vCart, &vIn);                     /* @0x405201 */
    gxVec2Add(&vSum, &vCart, &pNode->vPos);            /* @0x405214 */
    return (int)vSum.x;                                /* ftol @0x40522d */
}

/* objListFindFloat @0x405240 — find pNode's turret entry whose nTypeId
 * matches nTypeId and return its absolute heading scaled to the 15-bit
 * binary-angle unit: (int)((flScaleA + flAngle) * (65536/π) * 0.5). */
int objListFindFloat(WorldNode *pNode, int nTypeId) /* @0x405240 */
{
    static const float g_flRadToBin = 20860.455078125f; /* @0x44b2f0 (65536/π) */
    static const double g_dblHalf = 0.5;                /* @0x44b2e8 */
    ObjTurret *pTurret;

    for (pTurret = (ObjTurret *)pNode->pTurretHead; pTurret != NULL;
         pTurret = pTurret->pNext) {                   /* @0x40524f */
        if (pTurret->nTypeId == nTypeId) {             /* @0x40524b */
            return (int)((pNode->flScaleA + pTurret->flAngle) * g_flRadToBin *
                         g_dblHalf);                   /* @0x40526e */
        }
    }
    return 0;                                          /* XOR AX,AX @0x405256 */
}

/* objDistToPoint @0x405050 — Euclidean ground-plane distance from the
 * node's vPos {x=z, y=x} to {flA, flB} (components pair in vPos order). */
float objDistToPoint(WorldNode *pNode, float flA, float flB) /* @0x405050 */
{
    float flDx = flA - pNode->vPos.x; /* @0x405050 */
    float flDy = flB - pNode->vPos.y; /* @0x405057 */
    return sqrtf(flDx * flDx + flDy * flDy); /* @0x40505e..0x405068 */
}

/* nodeChannelAvgFloat @0x4050c0 — average flHeight over pNode's child-mesh
 * entries whose nChannelKey matches nChannelKey; 0.0f when there is none. */
float nodeChannelAvgFloat(WorldNode *pNode, int nChannelKey) /* @0x4050c0 */
{
    ObjChildMesh *pMesh;
    float flSum = 0.0f;    /* @0x4050c3 */
    float flCount = 0.0f;  /* @0x4050c9 */

    for (pMesh = (ObjChildMesh *)pNode->pChildMeshHead; pMesh != NULL;
         pMesh = pMesh->pNext) {                         /* @0x4050d3 */
        if (pMesh->nChannelKey == nChannelKey) {         /* @0x4050d7 */
            flSum += pMesh->flHeight;                    /* +0x40 @0x4050dc */
            flCount += g_flOne;                          /* @0x4050e1 */
        }
    }
    if (flCount == g_flZero) {                           /* @0x4050f2 */
        return g_flZero;                                 /* @0x405103 */
    }
    return flSum / flCount;                              /* FDIVRP @0x40510c */
}

/* objFindTurret @0x405120 — return the first child-mesh entry of pNode
 * whose nChannelKey matches nChannelKey (despite the name this walks the
 * +0x08 ObjChildMesh list, not the turret list), or NULL. */
ObjChildMesh *objFindTurret(WorldNode *pNode, int nChannelKey) /* @0x405120 */
{
    ObjChildMesh *pMesh;

    for (pMesh = (ObjChildMesh *)pNode->pChildMeshHead; pMesh != NULL;
         pMesh = pMesh->pNext) {                         /* @0x40512b */
        if (pMesh->nChannelKey == nChannelKey) {         /* @0x40512e */
            return pMesh;                                /* @0x405139 */
        }
    }
    return NULL;                                         /* @0x405137 */
}

/* --- player collision clusters (levelObjectsCartsCameraInit @0x411b70) --- */


/* objTurretAdd @0x405280 — allocate a 0x1c turret entry, zero both polar
 * vectors, store the polarized {nPosZ, nPosX} position twice, scale nAngle
 * to radians (nAngle*g_flPi*g_dblAngleScale, i.e. 15-bit binary angle) and
 * head-insert into pNode's +0x0c turret list. nTypeId is the caller's id
 * (levelObjectsCartsCameraInit passes the record's scene node). */
void objTurretAdd(WorldNode *pNode, int nPosX, int nPosY, int nPosZ,
                  short nAngle, int nTypeId) /* @0x405280 */
{
    ObjTurret *pTurret = (ObjTurret *)malloc(sizeof(ObjTurret)); /* operator_new @0x43dd42 */
    GxVec2 vIn;

    (void)nPosY;
    if (pTurret != NULL) {
        gxVec2SetAngleZero(&pTurret->vPolar);                /* @0x434f90 @0x4052ba */
        gxVec2SetAngleZero(&pTurret->vPos);                  /* @0x4052c2 */
    }
    gxVec2Set(&vIn, (float)nPosZ, (float)nPosX);             /* @0x434fa0 @0x4052f4 */
    pTurret->vPos = vIn;                                     /* @0x4052fc */
    mathVec2Polar(&pTurret->vPolar, &vIn);                   /* @0x435060 @0x405314 */
    pTurret->flAngle = (float)(int)nAngle * g_flPi * (float)g_dblAngleScale; /* @0x405338 */
    pTurret->nTypeId = nTypeId;                              /* @0x40533e */
    pTurret->pNext = (ObjTurret *)pNode->pTurretHead;        /* @0x405349 */
    pNode->pTurretHead = pTurret;                            /* @0x405353 */
}

/* objTurretSetValue @0x405370 — walk pNode's child-mesh list and write
 * nValue1/nValue2 into every entry whose nChannelKey matches nKey. */
void objTurretSetValue(WorldNode *pNode, int nKey, int nValue1, int nValue2) /* @0x405370 */
{
    ObjChildMesh *pMesh;

    for (pMesh = (ObjChildMesh *)pNode->pChildMeshHead; pMesh != NULL;
         pMesh = pMesh->pNext) {                             /* @0x40538f */
        if (pMesh->nChannelKey == nKey) {                    /* @0x405384 */
            pMesh->nValue1 = nValue1;                        /* @0x405389 */
            pMesh->nValue2 = nValue2;                        /* @0x40538c */
        }
    }
}

/* nodeAddChildMesh @0x4053a0 — allocate a 0x4c child-mesh entry, zero the
 * four polar vectors, store the polarized {nKeyZ, nX} position twice plus
 * its polar form, the raw extents/channel key/mesh tag, then head-insert
 * into pNode's +0x08 list. Finally the world coords are fromPolar of the
 * entry's polar position rotated by pNode->flScaleA, stored twice. */
void nodeAddChildMesh(WorldNode *pNode, int nX, int nY, int nKeyZ,
                      float flExtentA, float flExtentB, int nChannelKey,
                      float flExtentC, int nMeshId) /* @0x4053a0 */
{
    ObjChildMesh *pMesh = (ObjChildMesh *)malloc(sizeof(ObjChildMesh)); /* @0x43dd42 */
    GxVec2 vIn;
    GxVec2 vOut;

    (void)nY;
    if (pMesh != NULL) {
        gxVec2SetAngleZero(&pMesh->vPolar);                  /* @0x4053da */
        gxVec2SetAngleZero(&pMesh->vPos);                    /* @0x4053e2 */
        gxVec2SetAngleZero(&pMesh->vWorldA);                 /* @0x4053ea */
        gxVec2SetAngleZero(&pMesh->vWorldB);                 /* @0x4053f2 */
    }
    gxVec2Set(&vIn, (float)nKeyZ, (float)nX);                /* @0x405424 */
    pMesh->vPos = vIn;                                       /* @0x40542c */
    mathVec2Polar(&pMesh->vPolar, &vIn);                     /* @0x405444 */
    pMesh->flExtentA = flExtentA;                            /* +0x14 @0x405463 */
    pMesh->flExtentB = flExtentB;                            /* +0x10 @0x40546a */
    pMesh->flExtentC = flExtentC;                            /* +0x18 @0x40546d */
    pMesh->nChannelKey = nChannelKey;                        /* +0x0c @0x405476 */
    pMesh->flVertVel = 0.0f;                                 /* +0x44 @0x405479 */
    pMesh->nMeshId = nMeshId;                                /* +0x00 @0x40547c */
    pMesh->nValue1 = 0;                                      /* +0x04 @0x40547e */
    pMesh->nValue2 = 0;                                      /* +0x08 @0x405481 */
    pMesh->pNext = (ObjChildMesh *)pNode->pChildMeshHead;    /* @0x405488 */
    pNode->pChildMeshHead = pMesh;                           /* @0x40548b */
    gxVec2Set(&vIn, pMesh->vPolar.x, pMesh->vPolar.y + pNode->flScaleA); /* @0x40549f */
    gxVec2FromPolar(&vOut, &vIn);                            /* @0x434fc0 @0x4054a9 */
    pMesh->vWorldA = vOut;                                   /* @0x4054b1 */
    pMesh->vWorldB = vOut;                                   /* @0x4054bc */
}

/* nodeSetTransformFromChannels @0x404e00 — place pNode at {nPosZ, nPosX}
 * (vPos and the vPosB copy), store nUnk at +0x3c, rotate by nAngle (same
 * 15-bit scaling as objTurretAdd) into flScaleA/flScaleB, then recompute
 * every child mesh's world coords from its polar form and raycast the
 * floor with sceneRayFindNearest @0x42a750 (1000.0 search distance,
 * flExtentA as the wall radius). */
void nodeSetTransformFromChannels(WorldNode *pNode, int nPosX, int nPosY,
                                  int nPosZ, int nUnk, short nAngle) /* @0x404e00 */
{
    ObjChildMesh *pMesh;
    GxVec2 vIn;
    GxVec2 vOut;

    pNode->vPos.x = (float)nPosZ;                            /* +0x20 @0x404e18 */
    pNode->field_3c = nUnk;                                  /* +0x3c @0x404e22 */
    pNode->vPos.y = (float)nPosX;                            /* +0x24 @0x404e27 */
    pNode->vPosB = pNode->vPos;                              /* @0x404e34 */
    pNode->flScaleA = (float)(int)nAngle * g_flPi * (float)g_dblAngleScale; /* @0x404e46 */
    pNode->flScaleB = pNode->flScaleA;                       /* +0x34 @0x404e49 */
    for (pMesh = (ObjChildMesh *)pNode->pChildMeshHead; pMesh != NULL;
         pMesh = pMesh->pNext) {                             /* @0x404e85 */
        gxVec2Set(&vIn, pMesh->vPolar.x, pMesh->vPolar.y + pNode->flScaleA); /* @0x404e60 */
        gxVec2FromPolar(&vOut, &vIn);                        /* @0x404e6a */
        pMesh->vWorldA = vOut;                               /* @0x404e71 */
        pMesh->vWorldB = vOut;                               /* @0x404e7c */
    }
    for (pMesh = (ObjChildMesh *)pNode->pChildMeshHead; pMesh != NULL;
         pMesh = pMesh->pNext) {                             /* @0x404edc */
        pMesh->pSurface = sceneRayFindNearest(               /* @0x42a750 @0x404ebe */
            pMesh->vWorldA.x + pNode->vPos.x,
            pMesh->vWorldA.y + pNode->vPos.y,
            (float)nPosY, 1000.0f, pMesh->flExtentA);        /* @0x447a000=1000.0 @0x404eaa */
        pMesh->flHeight = (float)nPosY;                      /* +0x40 @0x404eca */
        pMesh->flVertVel = 0.0f;                             /* +0x44 @0x404ecd */
    }
}

static const float g_fl1_2e6 = 1.2e-06f;  /* @0x44b2f4 (bytes 57 5C 1A 36) */
static const float g_fl1_5e6 = 1.5e-06f;  /* @0x44b300 (bytes AB A5 43 36) */

/* objSetPos @0x404ef0 — move a world node on the ground plane: the old
 * vPos is copied to vPosB (+0x28/+0x2c) and vPos (+0x20/+0x24) becomes
 * (flX, flZ). Used by objUpdatePhysics/objUpdateFire to place a node on
 * its collision shot (two calls in objUpdatePhysics leave vPosB on the
 * shot's from-position and vPos on its to-position). */
void objSetPos(WorldNode *pNode, float flX, float flZ) /* @0x404ef0 */
{
    pNode->vPosB.x = pNode->vPos.x;                      /* +0x28 @0x404ef2 */
    pNode->vPosB.y = pNode->vPos.y;                      /* +0x2c @0x404ef5 */
    pNode->vPos.x = flX;                                 /* +0x20 @0x404ef8 */
    pNode->vPos.y = flZ;                                 /* +0x24 @0x404efb */
}

/* objShotListClear @0x405590 — release the node's shot list built by
 * objShotCollide (objShotCollide walk) / objCollideCheck: objShotListFree
 * the chain, free the head block, zero the list head (+0x10) and the shot
 * count (+0x14). */
void objShotListClear(WorldNode *pNode) /* @0x405590 */
{
    if (pNode->pShotList != NULL) {                      /* @0x405594 */
        objShotListFree(pNode->pShotList);               /* @0x406110 @0x40559b */
        memFreeDirect(pNode->pShotList);                 /* @0x4055a5 */
    }
    pNode->pShotList = NULL;                             /* +0x10 @0x4055ab */
    pNode->field_14 = 0;                                 /* +0x14 @0x4055af */
}

/* objUpdatePhysics @0x405680 — collision-response pass 1 (objUpdateAll
 * calls it twice per frame). Per enabled node, 40 times per frame
 * (@0x405dd9): objShotListClear, objShotCollide (rebuilds the shot list),
 * then apply the response. Single shot (field_14 == 1): objSetPos onto the
 * shot's from-position and then its to-position (vPosB = from, vPos = to),
 * flScaleC = shot->field_1c, flScaleB smoothed toward flScaleA by
 * flBlend/(field_18+flBlend), and flImpulse = (dir - from)·slope *
 * shot->flScale * 1.2e-06 (@0x44b2f4); impact sfx bank 1 idx
 * pSfxSrc[1] vol 65000 id 1 with nFlags 0x24. Multiple shots: the same
 * response using the shot with the lowest flBlend/(field_18+flBlend)
 * ratio (@0x405bce). The original's g_nMovieFrame 400..500 debug trace
 * blocks call the (empty) nopDebugStub logger and are omitted. */
void objUpdatePhysics(void) /* @0x405680 */
{
    WorldNode *pNode;
    ShotObj *pShot;
    ShotObj *pBest;
    void *pEmitter;
    int nIter;
    int nX;
    int nZ;

    for (pNode = g_pObjHead; pNode != NULL; pNode = pNode->pPrev) {  /* +0x00 walk @0x405de6 */
        if (pNode->field_1c == 0) {                      /* @0x4056b0 */
            continue;
        }
        for (nIter = 0; nIter < 0x28; nIter++) {         /* 40 substeps @0x405dd9 */
            objShotListClear(pNode);                     /* @0x405590 @0x4056c5 */
            objShotCollide(pNode);                       /* @0x4035e0 @0x4056cc */
            if (pNode->field_14 == 1 && pNode->pShotList != NULL) {  /* @0x405721 */
                pShot = (ShotObj *)pNode->pShotList;
                objSetPos(pNode, pShot->v0.x, pShot->v0.y);      /* @0x405894 */
                objSetPos(pNode, pShot->v2.x, pShot->v2.y);          /* @0x4058a6 */
                pNode->flScaleC = pShot->v1.y;                       /* +0x38 @0x4058c3 */
                pNode->flScaleB += (pNode->flScaleA - pNode->flScaleB) *
                                   pShot->v3.x /
                                   (pShot->v1.x + pShot->v3.x);       /* +0x34 @0x4058c9 */
                pNode->flImpulse =                                   /* +0x40 @0x4058ee */
                    ((pShot->v4.y - pShot->v0.y) * pShot->v5.y +
                     (pShot->v4.x - pShot->v0.x) * pShot->v5.x) *
                    pShot->flScale * g_fl1_2e6;                      /* @0x44b2f4 @0x4058f4 */
                pEmitter = malloc(0x1c);                             /* operator_new @0x43dd42 @0x4058fd */
                if (pEmitter != NULL) {                              /* @0x40590d */
                    nZ = (int)pNode->vPos.x;                         /* ftol +0x20 @0x40591a */
                    nX = (int)pNode->vPos.y;                         /* ftol +0x24 @0x405924 */
                    sndPlaySfx3D(pEmitter, 1,                        /* @0x42bcd0 @0x405941 */
                                 (unsigned int)pShot->pSrc[1], 65000, 1, 0, 0,
                                 nX, 0, nZ, 0x24);
                }
            } else if (pNode->field_14 > 1 && pNode->pShotList != NULL) {  /* @0x405a20 */
                pBest = (ShotObj *)pNode->pShotList;
                for (pShot = pBest->pNext; pShot != NULL; pShot = pShot->pNext) {  /* @0x405beb */
                    if (pShot->v3.x / (pShot->v1.x + pShot->v3.x) <
                        pBest->v3.x / (pBest->v1.x + pBest->v3.x)) {               /* @0x405bce */
                        pBest = pShot;
                    }
                }
                objSetPos(pNode, pBest->v0.x, pBest->v0.y);          /* @0x405c4b */
                objSetPos(pNode, pBest->v2.x, pBest->v2.y);          /* @0x405c5a */
                pNode->flScaleC = pBest->v1.y;                       /* +0x38 @0x405c6f */
                pNode->flScaleB += (pNode->flScaleA - pNode->flScaleB) *
                                   pBest->v3.x /
                                   (pBest->v1.x + pBest->v3.x);       /* +0x34 @0x405c78 */
                pNode->flImpulse =                                   /* +0x40 @0x405c9f */
                    ((pBest->v4.y - pBest->v0.y) * pBest->v5.y +
                     (pBest->v4.x - pBest->v0.x) * pBest->v5.x) *
                    pBest->flScale * g_fl1_2e6;                      /* @0x405ca5 */
                pEmitter = malloc(0x1c);                             /* @0x405cae */
                if (pEmitter != NULL) {                              /* @0x405cbc */
                    nZ = (int)pNode->vPos.x;                         /* ftol +0x20 @0x405ccd */
                    nX = (int)pNode->vPos.y;                         /* ftol +0x24 @0x405cd8 */
                    sndPlaySfx3D(pEmitter, 1,                        /* @0x42bcd0 @0x405cf4 */
                                 (unsigned int)pBest->pSrc[1], 65000, 1, 0, 0,
                                 nX, 0, nZ, 0x24);
                }
            }
            pNode->field_18++;                               /* +0x18 @0x405dcd */
        }
    }
}

/* objUpdateFire @0x405e10 — fire/hazard pass between the two
 * objUpdatePhysics runs. Pass 1 (@0x405e31): clear + rebuild each enabled
 * node's shot list with objCollideCheck @0x404ac0. Pass 2 (@0x405e5c):
 * apply the response — single shot: objSetPos onto the shot's to-position
 * (vPosB keeps the previous vPos), flScaleB = flScaleA (snap), field_3c =
 * shot->field_18, flScaleC = shot->field_1c, flImpulse = (dir - to)·slope *
 * flScale * 1.5e-06 (@0x44b300); fire sfx bank 1 idx pSfxSrc[2] vol 65000
 * id 0 with nFlags 0x24; then objWalkAnimSync(pParent, *(pAnimTarget+0x44))
 * (the victim node's pParent record) for the node's parent. Multiple shots: the same response with the shot
 * of maximum field_18 (@0x405f5a); the walk-anim sync always reads the
 * list head's pAnimTarget (@0x40600f). */
void objUpdateFire(void) /* @0x405e10 */
{
    WorldNode *pNode;
    ShotObj *pShot;
    ShotObj *pBest;
    void *pEmitter;
    int nX;
    int nZ;
    struct PlayerRecord *pRec;

    for (pNode = g_pObjHead; pNode != NULL; pNode = pNode->pPrev) {  /* @0x405e46 */
        if (pNode->field_1c != 0) {                      /* @0x405e31 */
            objShotListClear(pNode);                     /* @0x405590 @0x405e3a */
            objCollideCheck(pNode);                      /* @0x404ac0 @0x405e41 */
        }
    }
    for (pNode = g_pObjHead; pNode != NULL; pNode = pNode->pPrev) {  /* @0x405e46/0x406025 */
        if (pNode->field_1c == 0) {                      /* @0x405e5c */
            continue;
        }
        if (pNode->field_14 == 1 && pNode->pShotList != NULL) {  /* @0x405e67 */
            pShot = (ShotObj *)pNode->pShotList;
            objSetPos(pNode, pShot->v2.x, pShot->v2.y);            /* @0x405e7e */
            pNode->flScaleB = pNode->flScaleA;                     /* +0x34 @0x405e94 */
            pNode->field_3c = pShot->v1.x;                         /* +0x3c @0x405e9d */
            pNode->flScaleC = pShot->v1.y;                         /* +0x38 @0x405ea3 */
            pNode->flImpulse =                                     /* +0x40 @0x405eba */
                ((pShot->v4.y - pShot->v2.y) * pShot->v5.y +
                 (pShot->v4.x - pShot->v2.x) * pShot->v5.x) *
                pShot->flScale * g_fl1_5e6;                        /* @0x44b300 @0x405ec0 */
            pEmitter = malloc(0x1c);                               /* @0x405ec9 */
            if (pEmitter != NULL) {                                /* @0x405ed7 */
                nZ = (int)pNode->vPos.x;                           /* ftol +0x20 @0x405ee8 */
                nX = (int)pNode->vPos.y;                           /* ftol +0x24 @0x405ef3 */
                sndPlaySfx3D(pEmitter, 1,                          /* @0x42bcd0 @0x405f12 */
                             (unsigned int)pShot->pSrc[2], 65000, 0, 0, 0,
                             nX, 0, nZ, 0x24);
            }
            if (pNode->pParent != NULL) {                          /* +0x44 @0x405f17 */
                pRec = NULL;
                if (pShot->pAnimTarget != NULL) {                  /* +0x08 @0x405f2d */
                    pRec = *(struct PlayerRecord **)((char *)pShot->pAnimTarget + 0x44); /* @0x405f30 */
                }
                if (pRec != NULL) {                                /* @0x405f33 */
                    objWalkAnimSync((struct PlayerRecord *)pNode->pParent, pRec); /* @0x409b10 @0x40601c */
                }
            }
            pNode->field_18++;                                     /* +0x18 @0x406022 */
        } else if (pNode->field_14 > 1 && pNode->pShotList != NULL) {  /* @0x405f43 */
            pBest = (ShotObj *)pNode->pShotList;
            for (pShot = pBest->pNext; pShot != NULL; pShot = pShot->pNext) {  /* @0x405f54 */
                if (pBest->v1.x < pShot->v1.x) {                   /* @0x405f62 */
                    pBest = pShot;
                }
            }
            objSetPos(pNode, pBest->v2.x, pBest->v2.y);            /* @0x405f6f */
            pNode->flScaleB = pNode->flScaleA;                     /* +0x34 @0x405f81 */
            pNode->field_3c = pBest->v1.x;                         /* +0x3c @0x405f89 */
            pNode->flScaleC = pBest->v1.y;                         /* +0x38 @0x405f8f */
            pNode->flImpulse =                                     /* +0x40 @0x405fa6 */
                ((pBest->v4.x - pBest->v2.x) * pBest->v5.x +
                 (pBest->v4.y - pBest->v2.y) * pBest->v5.y) *
                pBest->flScale * g_fl1_5e6;                        /* @0x405fac */
            pEmitter = malloc(0x1c);                               /* @0x405fb5 */
            if (pEmitter != NULL) {                                /* @0x405fc3 */
                nZ = (int)pNode->vPos.x;                           /* @0x405fd4 */
                nX = (int)pNode->vPos.y;                           /* @0x405fdf */
                sndPlaySfx3D(pEmitter, 1,                          /* @0x42bcd0 @0x405ffb */
                             (unsigned int)pBest->pSrc[2], 65000, 0, 0, 0,
                             nX, 0, nZ, 0x24);
            }
            if (pNode->pParent != NULL) {                          /* @0x406000 */
                pRec = NULL;
                pShot = (ShotObj *)pNode->pShotList;               /* head @0x406012 */
                if (pShot->pAnimTarget != NULL) {                  /* +0x08 @0x406012 */
                    pRec = *(struct PlayerRecord **)((char *)pShot->pAnimTarget + 0x44); /* @0x406015 */
                }
                if (pRec != NULL) {                                /* @0x40601a */
                    objWalkAnimSync((struct PlayerRecord *)pNode->pParent, pRec); /* @0x409b10 @0x40601d */
                }
            }
            pNode->field_18++;                                     /* @0x406022 */
        }
    }
}