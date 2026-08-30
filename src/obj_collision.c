#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "obj_collision.h"
#include "obj_world.h"
#include "obj_event.h"
#include "gx.h"
#include "scene.h"
#include "config.h"
#include "util.h"
#include "pool.h"
#include "time.h"
#include "zone.h"
#include "player_physics.h"
#include "stubs.h"
#include "sound.h"

/* ===================================================================
 * shot-collision subsystem (objShotCtor @0x406050, objShotAdd @0x4054e0,
 * objShotListFree @0x406110, objSegCollideCollect @0x402ba0,
 * objShotCollide @0x4035e0, objCollideCheck @0x404ac0)
 * =================================================================== */

static const float g_flHalfPi = 1.5707964f;  /* @0x44b270 (bytes DB 0F C9 3F) */
static const float g_flOne = 1.0f;  /* @0x44b260 */

/* Shot-phase constants (read from the binary). */
static const float g_fl_0_2 = 0.2f;           /* @0x44b2d4 */
static const float g_flShotHalf = 0.5f;       /* @0x44b274 */
static const float g_fl_0_125 = 0.125f;       /* @0x44b2e0 */
static const float g_fl_0_333 = 0.33333334f;  /* @0x44b2dc */
static const float g_fl_0_25 = 0.25f;         /* @0x44b2d8 */
static const float g_fl_500 = 500.0f;         /* @0x44b2e4 */
static const double g_dbl_1 = -1.0;           /* @0x44b280 (perp-proj negate) */
static const double g_dbl_1_2 = 1.0;          /* @0x44b288 (phases 2-4 bounds) */

/* OBJ_HIT_IN_BOX — the ±flTol axis-aligned box test the original inlines
 * at 0x4038fd..0x403a54 (objShotCollide phase bounds) and 0x402e40..
 * (objSegCollideCollect): pHit must lie within
 * [min(pBoxA,pBoxB)-flTol, max(pBoxA,pBoxB)+flTol] on both components. */
#define OBJ_HIT_IN_BOX(pHit, pBoxA, pBoxB, flTol)                               \
    (((((pHit)->x <= (pBoxA)->x + (flTol)) &&                                   \
       ((pBoxB)->x - (flTol) <= (pHit)->x)) ||                                  \
      (((pHit)->x <= (pBoxB)->x + (flTol)) &&                                   \
       ((pBoxA)->x - (flTol) <= (pHit)->x))) &&                                 \
     ((((pHit)->y <= (pBoxA)->y + (flTol)) &&                                   \
       ((pBoxB)->y - (flTol) <= (pHit)->y)) ||                                  \
      (((pHit)->y <= (pBoxB)->y + (flTol)) &&                                   \
       ((pBoxA)->y - (flTol) <= (pHit)->y))))

/* OBJ_HIT_IN_BOX_XY — same box test with the box given as four scalar
 * components (the AiNavEdge V0/V1 pairs are separate fields). */
#define OBJ_HIT_IN_BOX_XY(pHit, flAx, flAy, flBx, flBy, flTol)                  \
    (((((pHit)->x <= (flAx) + (flTol)) &&                                       \
       ((flBx) - (flTol) <= (pHit)->x)) ||                                      \
      (((pHit)->x <= (flBx) + (flTol)) &&                                       \
       ((flAx) - (flTol) <= (pHit)->x))) &&                                     \
     ((((pHit)->y <= (flAy) + (flTol)) &&                                       \
       ((flBy) - (flTol) <= (pHit)->y)) ||                                      \
      (((pHit)->y <= (flBy) + (flTol)) &&                                       \
       ((flAy) - (flTol) <= (pHit)->y))))

/* objShotCtor @0x406050 — initialize a 0x44 ShotObj: six SetAngleZero
 * vec2s (+0x10..+0x38), then v3, v0, v2, v1, v4, v5, flScale, pSrc
 * (+0x0c), pAnimTarget (+0x08) and pNext/field_04 = 0 (store order per
 * the disassembly; v3 first). Original __thiscall RET 0x3c. */
ShotObj *objShotCtor(ShotObj *pShot, int *pSrc, float flV0x, float flV0y,
                     float flV2x, float flV2y, float flV1x, float flV1y,
                     float flV4x, float flV4y, float flV5x, float flV5y,
                     float flSpeed, float flV3x, float flV3y,
                     void *pAnimTarget) /* @0x406050 */
{
    gxVec2SetAngleZero(&pShot->v0);                    /* +0x10 @0x40605b */
    gxVec2SetAngleZero(&pShot->v1);                    /* +0x18 @0x406065 */
    gxVec2SetAngleZero(&pShot->v2);                    /* +0x20 @0x40606f */
    gxVec2SetAngleZero(&pShot->v3);                    /* +0x28 @0x406077 */
    gxVec2SetAngleZero(&pShot->v4);                    /* +0x30 @0x40607f */
    gxVec2SetAngleZero(&pShot->v5);                    /* +0x38 @0x406087 */
    pShot->v3.x = flV3x;                               /* +0x28 @0x406098 */
    pShot->v3.y = flV3y;                               /* +0x2c @0x40609f */
    pShot->v0.x = flV0x;                               /* +0x10 @0x4060a6 */
    pShot->v0.y = flV0y;                               /* +0x14 @0x4060ac */
    pShot->v2.x = flV2x;                               /* +0x20 @0x4060b3 */
    pShot->v2.y = flV2y;                               /* +0x24 @0x4060ba */
    pShot->v1.x = flV1x;                               /* +0x18 @0x4060c1 */
    pShot->v1.y = flV1y;                               /* +0x1c @0x4060c7 */
    pShot->v4.x = flV4x;                               /* +0x30 @0x4060ce */
    pShot->v4.y = flV4y;                               /* +0x34 @0x4060d5 */
    pShot->v5.x = flV5x;                               /* +0x38 @0x4060dc */
    pShot->v5.y = flV5y;                               /* +0x3c @0x4060e3 */
    pShot->flScale = flSpeed;                          /* +0x40 @0x4060ea */
    pShot->pSrc = pSrc;                                /* +0x0c @0x4060ed */
    pShot->pAnimTarget = pAnimTarget;                  /* +0x08 @0x4060f0 */
    pShot->pNext = NULL;                               /* +0x00 @0x4060f3 */
    pShot->field_04 = 0;                               /* +0x04 @0x4060f9 */
    return pShot;                                      /* @0x406100 */
}

/* objShotAdd @0x4054e0 — operator_new a 0x44 ShotObj (@0x43dd42) and
 * objShotCtor it with the same 15 args, then push onto pNode->pShotList
 * (+0x10) and bump the shot count (+0x14). The original dereferences the
 * allocation even when operator_new fails (NULL-deref on OOM); the
 * rebuild keeps the same shape. Original __thiscall RET 0x3c. */
void objShotAdd(WorldNode *pNode, int *pSrc, float flV0x, float flV0y,
                float flV2x, float flV2y, float flV1x, float flV1y,
                float flV4x, float flV4y, float flV5x, float flV5y,
                float flSpeed, float flV3x, float flV3y, void *pAnimTarget) /* @0x4054e0 */
{
    ShotObj *pShot;

    pShot = malloc(0x44);                              /* operator_new @0x43dd42 @0x4054fb */
    if (pShot != NULL) {                               /* @0x405507 */
        objShotCtor(pShot, pSrc, flV0x, flV0y, flV2x, flV2y, flV1x, flV1y,
                    flV4x, flV4y, flV5x, flV5y, flSpeed, flV3x, flV3y,
                    pAnimTarget);                      /* @0x406050 @0x405560 */
    }
    pShot->pNext = pNode->pShotList;                   /* +0x00 @0x405570 */
    pNode->pShotList = pShot;                          /* +0x10 @0x405572 */
    pNode->field_14++;                                 /* +0x14 @0x405575 */
}

/* objShotListFree @0x406110 — recursively free a shot list: recurse on
 * pNext, then memFreeDirect the next node. The head block itself is
 * freed by objShotListClear, which then zeroes the list head and count.
 * Original __fastcall. */
void objShotListFree(ShotObj *pShot) /* @0x406110 */
{
    ShotObj *pNext;

    pNext = pShot->pNext;                              /* +0x00 @0x406111 */
    if (pNext != NULL) {                               /* @0x406115 */
        objShotListFree(pNext);                        /* @0x406119 */
        memFreeDirect(pNext);                          /* @0x43dd37 @0x40611f */
    }
}

/* objSegCollideCollect @0x402ba0 — extend the surface array (NULL
 * terminated) with the pPeerMesh target of every connection edge
 * (+0x38 list) of the listed surfaces whose segment geometry touches
 * the moving A/B segment of pSrc, deduped against the existing entries:
 *  - near test: the edge V0 plane distance from A or B <= radius;
 *  - phase A/B: intersect the wall with a unit ray from A (then B)
 *    along the edge direction, ±1.0 box;
 *  - else/all-far: reflected-ray test with unit(-n)*radius extension
 *    (±1.0 on both boxes), then a perpendicular circle test
 *    (project A onto the edge normal, chord-shift the hit along A->B).
 * Original __thiscall. */
void objSegCollideCollect(WorldNode *pNode, ObjChildMesh *pSrc, void **apList) /* @0x402ba0 */
{
    GxVec2 vA;                 /* local_12c: vWorldA + vPos */
    GxVec2 vB;                 /* local_124: vWorldB + vPosB */
    GxVec2 vRay;               /* local_50/40: {flUnitX, flUnitNegZ} ray dir */
    GxVec2 vExt;               /* local_c8/d8: fromPolar({radius, angle(-n)}) */
    GxVec2 vPolar;             /* polar scratch local_a8/c0 */
    GxVec2 vS1;                /* edge/extended start local_11c/f8 */
    GxVec2 vS2;                /* edge/extended end local_f8 */
    GxVec2 vOutA;              /* phase-A intersection out local_104 */
    GxVec2 vOutB;              /* phase-B intersection out local_110 */
    GxVec2 vHit;               /* intersection local_134 */
    GxVec2 vPerpIn;            /* {1.0, angle+PI/2} local_f0 */
    GxVec2 vPerp;              /* local_b8 -> local_d0/cc */
    float flRad2;              /* local_114: radius^2 */
    float flProj;              /* local_e0: perp projection */
    int i;                     /* surface index */
    int j;                     /* dedup scan */
    AiNavEdge *pEdge;
    AiNavNode *pSurf;

    gxVec2SetAngleZero(&vA);                               /* local_12c @0x402c52 */
    gxVec2SetAngleZero(&vB);                               /* local_124 @0x402c58 */
    gxVec2SetAngleZero(&vOutA);                            /* local_104 @0x402c5e */
    gxVec2SetAngleZero(&vOutB);                            /* local_110 @0x402c63 */
    gxVec2SetAngleZero(&vPolar);                           /* local_b0 @0x402c68 */
    gxVec2SetAngleZero(&vPerpIn);                          /* local_f0 @0x402c6d */
    gxVec2SetAngleZero(&vHit);                             /* local_134 @0x402c72 */
    gxVec2SetAngleZero(&vExt);                             /* local_d8 @0x402c77 */
    gxVec2SetAngleZero(&vExt);                             /* local_c8 @0x402c7c */
    gxVec2SetAngleZero(&vPerp);                            /* local_d0 @0x402c81 */
    gxVec2SetAngleZero(&vS1);                              /* local_11c @0x402c86 */
    gxVec2SetAngleZero(&vS2);                              /* local_f8 @0x402c8a */
    flRad2 = pSrc->flExtentA * pSrc->flExtentA;            /* @0x402ba5 */
    gxVec2Add(&vA, &pSrc->vWorldA, &pNode->vPos);          /* @0x402c08 */
    gxVec2Add(&vB, &pSrc->vWorldB, &pNode->vPosB);         /* @0x402c28 */
    for (i = 0; apList[i] != NULL; i++) {                  /* @0x402c4c */
        pSurf = (AiNavNode *)apList[i];
        for (pEdge = pSurf->pConnList; pEdge != NULL;      /* @0x402c68 */
             pEdge = pEdge->pNext) {
            if (((pEdge->flV0Z - vA.x) * pEdge->flUnitX +
                 (pEdge->flV0X - vA.y) * pEdge->flUnitNegZ <=
                 pSrc->flExtentA) ||                       /* @0x402c92 */
                ((pEdge->flV0Z - vB.x) * pEdge->flUnitX +
                 (pEdge->flV0X - vB.y) * pEdge->flUnitNegZ <=
                 pSrc->flExtentA)) {                       /* @0x402cdc */
                /* Phase A — unit-direction ray from A. */
                gxVec2Set(&vRay, pEdge->flUnitX, pEdge->flUnitNegZ); /* @0x402cfb */
                gxVec2Add(&vS2, &vA, &vRay);               /* local_90 @0x402d0d */
                mathSegIntersect(pEdge->flV0Z, pEdge->flV0X,          /* @0x406130 @0x402d4c */
                                 pEdge->flV1Z, pEdge->flV1X,
                                 vA.x, vA.y, vS2.x, vS2.y, &vOutA.x);
                if (OBJ_HIT_IN_BOX_XY(&vOutA, pEdge->flV0Z, pEdge->flV0X, /* @0x402d8c */
                                      pEdge->flV1Z, pEdge->flV1X,
                                      (float)g_dbl_1_2)) {
                    goto objSegAppend;                     /* @0x403570 */
                }
                /* Phase B — same ray from B. */
                gxVec2Set(&vRay, pEdge->flUnitX, pEdge->flUnitNegZ); /* @0x402e10 */
                gxVec2Add(&vS2, &vB, &vRay);               /* local_70 @0x402e22 */
                mathSegIntersect(pEdge->flV0Z, pEdge->flV0X,          /* @0x402e61 */
                                 pEdge->flV1Z, pEdge->flV1X,
                                 vB.x, vB.y, vS2.x, vS2.y, &vOutB.x);
                if (OBJ_HIT_IN_BOX_XY(&vOutB, pEdge->flV0Z, pEdge->flV0X, /* @0x402ec0 */
                                      pEdge->flV1Z, pEdge->flV1X,
                                      (float)g_dbl_1_2) ||
                    !(((pNode->vPos.x - pEdge->flV0Z) *
                       (pNode->vPos.x - pEdge->flV0Z) +
                       (pNode->vPos.y - pEdge->flV0X) *
                       (pNode->vPos.y - pEdge->flV0X) > flRad2) && /* @0x40301a */
                      ((pNode->vPos.x - pEdge->flV1Z) *
                       (pNode->vPos.x - pEdge->flV1Z) +
                       (pNode->vPos.y - pEdge->flV1X) *
                       (pNode->vPos.y - pEdge->flV1X) > flRad2) && /* @0x40304a */
                      ((pNode->vPosB.x - pEdge->flV0Z) *
                       (pNode->vPosB.x - pEdge->flV0Z) +
                       (pNode->vPosB.y - pEdge->flV0X) *
                       (pNode->vPosB.y - pEdge->flV0X) > flRad2) && /* @0x40307a */
                      ((pNode->vPosB.x - pEdge->flV1Z) *
                       (pNode->vPosB.x - pEdge->flV1Z) +
                       (pNode->vPosB.y - pEdge->flV1X) *
                       (pNode->vPosB.y - pEdge->flV1X) > flRad2))) { /* @0x4030aa */
                    goto objSegAppend;                 /* @0x403570 */
                }
                /* fall through to the far-side phase C tests */
            }
            /* Phase C1 — reflected-ray test from the A side. */
            if ((vA.x - vB.x) * pEdge->flUnitX +                   /* @0x4030c0 */
                (vA.y - vB.y) * pEdge->flUnitNegZ >= 0.0f &&
                (pEdge->flV0Z - vA.x) * pEdge->flUnitX +           /* @0x4030e6 */
                (pEdge->flV0X - vA.y) * pEdge->flUnitNegZ <=
                pSrc->flExtentA) {
                gxVec2Set(&vRay, -pEdge->flUnitX, -pEdge->flUnitNegZ); /* @0x403100 */
                mathVec2Polar(&vPolar, &vRay);                     /* @0x435060 @0x403105 */
                gxVec2Set(&vExt, pSrc->flExtentA, vPolar.y);       /* local_d8 */
                gxVec2FromPolar(&vExt, &vExt);                     /* @0x434fc0 @0x403129 */
                gxVec2Set(&vS1, pEdge->flV0Z, pEdge->flV0X);
                gxVec2Add(&vS1, &vS1, &vExt);                      /* local_11c @0x40313b */
                gxVec2Set(&vS2, pEdge->flV1Z, pEdge->flV1X);
                gxVec2Add(&vS2, &vS2, &vExt);                      /* local_f8 @0x40315f */
                mathSegIntersect(vA.x, vA.y, vB.x, vB.y,           /* @0x406130 @0x403182 */
                                 vS1.x, vS1.y, vS2.x, vS2.y, &vHit.x);
                if (OBJ_HIT_IN_BOX(&vHit, &vA, &vB, (float)g_dbl_1_2) && /* @0x4031ae */
                    OBJ_HIT_IN_BOX(&vHit, &vS1, &vS2, (float)g_dbl_1_2)) {
                    goto objSegAppend;                 /* @0x403570 */
                }
            }
            /* Phase C2 — perpendicular circle test. */
            gxVec2Set(&vRay, vB.x - vA.x, vB.y - vA.y);            /* local_58 @0x4031d6 */
            mathVec2Polar(&vPolar, &vRay);                         /* local_c0 @0x4031a4 */
            gxVec2Set(&vPerpIn, 1.0f, vPolar.y + g_flHalfPi);      /* local_f0 @0x4031c2 */
            gxVec2FromPolar(&vPerp, &vPerpIn);                     /* local_b8 @0x4031d2 */
            flProj = (vA.x - pEdge->flV0Z) * vPerp.x +             /* local_e0 @0x4031e6 */
                     (vA.y - pEdge->flV0X) * vPerp.y;
            if (flProj < 0.0f) {                                   /* @0x403206 */
                flProj = (float)((double)flProj * g_dbl_1);        /* @0x44b280 @0x403212 */
            }
            if (flProj <= pSrc->flExtentA) {                       /* @0x403224 */
                gxVec2Set(&vS1, pEdge->flV0Z, pEdge->flV0X);       /* local_48 @0x403232 */
                gxVec2Set(&vS2, pEdge->flV0Z - vPerp.x,            /* local_38 @0x40324c */
                          pEdge->flV0X - vPerp.y);
                mathSegIntersect(vA.x, vA.y, vB.x, vB.y,           /* @0x406130 @0x403270 */
                                 vS1.x, vS1.y, vS2.x, vS2.y, &vHit.x);
                gxVec2Set(&vExt, sqrtf(flRad2 - flProj * flProj),  /* local_28 @0x4033f4 */
                          vPolar.y);
                gxVec2FromPolar(&vExt, &vExt);                     /* local_18 @0x403414 */
                gxVec2Add(&vHit, &vHit, &vExt);                    /* local_8 @0x403420 */
                if (OBJ_HIT_IN_BOX(&vHit, &vA, &vB, (float)g_dbl_1_2)) { /* @0x403430 */
objSegAppend:
                    for (j = 0; apList[j] != NULL; j++) {          /* @0x403578 */
                        if (apList[j] == (void *)pEdge->pPeerMesh) { /* @0x4035a2 */
                            break;                     /* dedup skip @0x4035aa */
                        }
                    }
                    if (apList[j] == NULL) {
                        apList[j] = (void *)pEdge->pPeerMesh;      /* @0x403585 */
                        apList[j + 1] = NULL;                      /* @0x40358b */
                    }
                }
            }
        }
    }
}

/* objShotCollide @0x4035e0 — rebuild pNode's shot list (+0x10) for the
 * objUpdatePhysics response. Per enabled +0x08 sub-object: world
 * positions A (vWorldA + vPos) and B (vWorldB + vPosB) must differ;
 * raycast the surfaces (sceneRayFindNearest 500.0, pSurface cache),
 * extend the list via objSegCollideCollect, then four hit phases over
 * the surfaces' pEdgeList (+0x3c) and pConnList (+0x38) edges, each
 * objShotAdd'ing a record with:
 *   v0 = hit - vWorldA, v2 = (hit + refl) - vWorldA,
 *   v1 = {dA*k, reflected angle} (k = 0.2 phases 1/2, 0.5 phases 3/4),
 *   v4 = hit, v5 = unit(V1-V0), flScale = dA*k,
 *   v3 = polar(hit - B) = {dB, angle}, pAnimTarget = NULL.
 * Phases 1/3 test the walk edges, 2/4 the connection walls (adding the
 * pPeerMesh plane test at the wall probe / edge V0 point). Phases 1/2
 * intersect the extended edge, 3/4 the inward-shifted edge with a
 * perpendicular-circle chord adjustment (g_dbl_1 negate @0x44b280).
 * Finally sceneRayFindSorted re-sorts the array and refreshes the
 * pSurface cache. Original __fastcall. */
void objShotCollide(WorldNode *pNode) /* @0x4035e0 */
{
    ObjChildMesh *pSrc;
    void *apSurfaces[20];      /* local_230..local_1e0 (0x50 bytes) */
    GxVec2 vA;                 /* local_31c: shooter A world pos */
    GxVec2 vB;                 /* local_314: shooter B world pos */
    GxVec2 vRay;               /* -n / line-dir polar-in scratch */
    GxVec2 vPolar;             /* polar scratch local_238/278/1e0/1d8 */
    GxVec2 vPolarAB;           /* polar(B - A) local_2a8 (persistent) */
    GxVec2 vPolarExt;          /* {radius, angle(-n)} local_2ec */
    GxVec2 vExt;               /* unit(-n) * radius local_2d4 */
    GxVec2 vS1;                /* edge start (extended) local_30c */
    GxVec2 vS2;                /* edge end (extended) local_300 */
    GxVec2 vHit;               /* intersection local_324 */
    GxVec2 vRefl;              /* reflected dir local_180 */
    GxVec2 vV4;                /* hit + refl local_240 */
    GxVec2 vPolarB;            /* polar(hit - B) local_258 */
    GxVec2 vPolarA;            /* polar(hit - A), then reflected local_2c4 */
    GxVec2 vPolarDir;          /* polar(V1 - V0) local_288 */
    GxVec2 vDirIn;             /* {1.0, angle + PI/2} local_2b4 */
    GxVec2 vPerp;              /* perp unit local_2bc */
    GxVec2 vProbe;             /* hit + extend local_90 (phase 2) */
    GxVec2 vTgt;               /* wall probe target local_30c (phase 2) */
    GxVec2 vOff;               /* chord offset local_178 (phases 3/4) */
    float flRad2;              /* radius^2 local_2ac */
    float flProj;              /* perp projection local_304 */
    float flPlanePeer;         /* wall target plane value + flExtentC */
    float flPlaneSurf;         /* cached surface plane value */
    GxVec2 vV0;                /* shot v0: gxVec2Sub(hit, vWorldA) */
    GxVec2 vV2;                /* shot v2: gxVec2Sub(v4, vWorldA) */
    int i;
    AiNavEdge *pEdge;
    AiNavNode *pPeer;

    gxVec2SetAngleZero(&vA);                               /* local_31c @0x40361e */
    gxVec2SetAngleZero(&vB);                               /* local_314 @0x403624 */
    gxVec2SetAngleZero(&vHit);                             /* local_324 @0x40362a */
    gxVec2SetAngleZero(&vPolarExt);                        /* local_2ec @0x403630 */
    gxVec2SetAngleZero(&vExt);                             /* local_2d4 @0x403636 */
    gxVec2SetAngleZero(&vS1);                              /* local_30c @0x40363c */
    gxVec2SetAngleZero(&vS2);                              /* local_300 @0x403640 */
    for (pSrc = (ObjChildMesh *)pNode->pChildMeshHead; pSrc != NULL;
         pSrc = pSrc->pNext) {                                 /* @0x403644 */
        if (pSrc->nMeshId == 0) {                              /* @0x40365a */
            continue;
        }
        gxVec2Add(&vA, &pSrc->vWorldA, &pNode->vPos);          /* @0x40368c */
        gxVec2Add(&vB, &pSrc->vWorldB, &pNode->vPosB);         /* @0x4036a4 */
        if (vA.x == vB.x && vA.y == vB.y) {                    /* @0x4036c0 */
            continue;
        }
        if (pSrc->pSurface == NULL) {                          /* @0x4036e0 */
            pSrc->pSurface = sceneRayFindNearest(vA.x, vA.y,   /* @0x42a750 @0x40370b */
                                                 pSrc->flExtentB, 500.0f,
                                                 pSrc->flExtentA);
            if (pSrc->pSurface == NULL) {                      /* @0x403730 */
                pSrc->pSurface = sceneRayFindNearest(vB.x, vB.y, /* @0x40374e */
                                                     pSrc->flExtentB, 500.0f,
                                                     pSrc->flExtentA);
                if (pSrc->pSurface == NULL) {                  /* @0x40377a */
                    continue;      /* next sub-object: no phases, no sorted pass */
                }
                apSurfaces[1] = NULL;                          /* @0x403785 */
                apSurfaces[0] = pSrc->pSurface;
            } else {
                apSurfaces[0] = pSrc->pSurface;                /* @0x40379d */
                apSurfaces[1] = sceneRayFindNearest(vB.x, vB.y, /* @0x4037a8 */
                                                    pSrc->flExtentB, 500.0f,
                                                    pSrc->flExtentA);
                apSurfaces[2] = NULL;
            }
        } else {
            apSurfaces[0] = pSrc->pSurface;                    /* @0x4037c8 */
            apSurfaces[1] = NULL;
        }
        objSegCollideCollect(pNode, pSrc, apSurfaces);         /* @0x402ba0 @0x4037e0 */

        /* Phase 1 — walk edges (+0x3c), segment-intersect, scale 0.2. */
        for (i = 0; apSurfaces[i] != NULL; i++) {              /* @0x40380a */
            for (pEdge = ((AiNavNode *)apSurfaces[i])->pEdgeList;
                 pEdge != NULL; pEdge = pEdge->pNext) {        /* @0x403828 */
                if ((vA.y - vB.y) * pEdge->flUnitNegZ +        /* @0x403860 */
                    (vA.x - vB.x) * pEdge->flUnitX >= 0.0f &&
                    (pEdge->flV0Z - vA.x) * pEdge->flUnitX +   /* @0x40388a */
                    (pEdge->flV0X - vA.y) * pEdge->flUnitNegZ <=
                    pSrc->flExtentA) {
                    gxVec2Set(&vRay, -pEdge->flUnitX, -pEdge->flUnitNegZ); /* @0x4038ba */
                    mathVec2Polar(&vPolar, &vRay);             /* @0x435060 @0x4038c4 */
                    gxVec2Set(&vPolarExt, pSrc->flExtentA, vPolar.y); /* local_2ec */
                    gxVec2FromPolar(&vExt, &vPolarExt);        /* local_2d4 @0x4038e6 */
                    gxVec2Set(&vS1, pEdge->flV0Z, pEdge->flV0X);
                    gxVec2Add(&vS1, &vS1, &vExt);              /* @0x403908 */
                    gxVec2Set(&vS2, pEdge->flV1Z, pEdge->flV1X);
                    gxVec2Add(&vS2, &vS2, &vExt);              /* @0x403930 */
                    mathSegIntersect(vA.x, vA.y, vB.x, vB.y,   /* @0x406130 @0x403952 */
                                     vS1.x, vS1.y, vS2.x, vS2.y, &vHit.x);
                    if (OBJ_HIT_IN_BOX(&vHit, &vA, &vB, g_flOne) &&  /* @0x4038fd */
                        OBJ_HIT_IN_BOX(&vHit, &vS1, &vS2, g_flOne)) {
                        gxVec2Set(&vPolarB, vHit.x - vB.x, vHit.y - vB.y); /* local_190 @0x40398a */
                        mathVec2Polar(&vPolarB, &vPolarB);     /* @0x40399c */
                        gxVec2Set(&vPolarA, vHit.x - vA.x, vHit.y - vA.y); /* local_20 @0x4039b4 */
                        mathVec2Polar(&vPolarA, &vPolarA);     /* @0x4039c6 */
                        vPolarA.y = vPolarA.y -                /* reflect @0x4039dc */
                            ((vPolarA.y - vPolarExt.y) + (vPolarA.y - vPolarExt.y));
                        vPolarA.x = vPolarA.x * g_fl_0_2;      /* @0x44b2d4 @0x4039ec */
                        gxVec2FromPolar(&vRefl, &vPolarA);     /* local_180 @0x4039f4 */
                        gxVec2Add(&vV4, &vHit, &vRefl);        /* local_240 @0x403a06 */
                        gxVec2Set(&vPolarDir, pEdge->flV1Z - pEdge->flV0Z, /* local_170 @0x403a18 */
                                  pEdge->flV1X - pEdge->flV0X);
                        mathVec2Polar(&vPolarDir, &vPolarDir); /* @0x403a3e */
                        vPolarDir.x = 1.0f;                    /* @0x403a34 */
                        gxVec2FromPolar(&vExt, &vPolarDir);    /* V5 @0x403af4 */
                        objShotAdd(pNode, (int *)pSrc,                /* @0x4054e0 @0x403bde */
                                   vHit.x - pSrc->vWorldA.x,   /* v0 @0x403bcf */
                                   vHit.y - pSrc->vWorldA.y,
                                   vV4.x - pSrc->vWorldA.x,    /* v2 @0x403bb2 */
                                   vV4.y - pSrc->vWorldA.y,
                                   vPolarA.x, vPolarA.y,       /* v1 @0x403af4 */
                                   vHit.x, vHit.y,             /* v4 @0x403b17 */
                                   vExt.x, vExt.y,             /* v5 @0x403b7d */
                                   vPolarA.x,                  /* flScale @0x403af0 */
                                   vPolarB.x, vPolarB.y,       /* v3 @0x403b91 */
                                   NULL);
                    }
                }
            }
        }

        /* Phase 2 — connection walls (+0x38), scale 0.2, plus the
         * pPeerMesh plane test at the wall probe point. */
        for (i = 0; apSurfaces[i] != NULL; i++) {
            for (pEdge = ((AiNavNode *)apSurfaces[i])->pConnList;
                 pEdge != NULL; pEdge = pEdge->pNext) {        /* @0x403c40 */
                if (pEdge->pPeerMesh == NULL) {                /* @0x403c31 */
                    continue;
                }
                if ((vA.y - vB.y) * pEdge->flUnitNegZ +
                    (vA.x - vB.x) * pEdge->flUnitX < 0.0f ||
                    (pEdge->flV0Z - vA.x) * pEdge->flUnitX +
                    (pEdge->flV0X - vA.y) * pEdge->flUnitNegZ >
                    pSrc->flExtentA) {                         /* @0x403c52 */
                    continue;
                }
                gxVec2Set(&vRay, -pEdge->flUnitX, -pEdge->flUnitNegZ); /* @0x403c56 */
                mathVec2Polar(&vPolar, &vRay);                 /* @0x403c60 */
                gxVec2Set(&vPolarExt, pSrc->flExtentA, vPolar.y);
                gxVec2FromPolar(&vExt, &vPolarExt);            /* @0x403c78 */
                gxVec2Set(&vS1, pEdge->flV0Z, pEdge->flV0X);
                gxVec2Add(&vS1, &vS1, &vExt);                  /* @0x403c9a */
                gxVec2Set(&vS2, pEdge->flV1Z, pEdge->flV1X);
                gxVec2Add(&vS2, &vS2, &vExt);                  /* @0x403cbc */
                mathSegIntersect(vA.x, vA.y, vB.x, vB.y,       /* @0x403f80 */
                                 vS1.x, vS1.y, vS2.x, vS2.y, &vHit.x);
                if (OBJ_HIT_IN_BOX(&vHit, &vA, &vB, (float)g_dbl_1_2) && /* @0x403d92 */
                    OBJ_HIT_IN_BOX(&vHit, &vS1, &vS2, (float)g_dbl_1_2)) {
                    gxVec2Add(&vProbe, &vHit, &vExt);          /* local_90 @0x403fd8? */
                    mathSegIntersect(vHit.x, vHit.y, vProbe.x, vProbe.y, /* @0x403f80 */
                                     pEdge->flV0Z, pEdge->flV0X,
                                     pEdge->flV1Z, pEdge->flV1X, &vTgt.x);
                    pPeer = (AiNavNode *)pEdge->pPeerMesh;     /* @0x403f93 */
                    if (pSrc->pSurface != NULL) {              /* @0x403f85 */
                        flPlanePeer =                          /* @0x403f96 */
                            (vTgt.x - (float)pPeer->nRefZ) * pPeer->flSlopeZ +
                            (vTgt.y - (float)pPeer->nRefX) * pPeer->flSlopeX +
                            (float)pPeer->nRefY + pSrc->flExtentC;
                        flPlaneSurf =                          /* @0x403fb2 */
                            (vTgt.x - (float)((AiNavNode *)pSrc->pSurface)->nRefZ) *
                            ((AiNavNode *)pSrc->pSurface)->flSlopeZ +
                            (vTgt.y - (float)((AiNavNode *)pSrc->pSurface)->nRefX) *
                            ((AiNavNode *)pSrc->pSurface)->flSlopeX +
                            (float)((AiNavNode *)pSrc->pSurface)->nRefY;
                        if (flPlanePeer < flPlaneSurf &&       /* @0x403fcb */
                            flPlanePeer < pSrc->flHeight) {    /* @0x403ff4 */
                            gxVec2Set(&vPolarB, vHit.x - vB.x, vHit.y - vB.y);
                            mathVec2Polar(&vPolarB, &vPolarB); /* @0x404030 */
                            gxVec2Set(&vPolarA, vHit.x - vA.x, vHit.y - vA.y);
                            mathVec2Polar(&vPolarA, &vPolarA); /* @0x40405c */
                            vPolarA.y = vPolarA.y -            /* @0x404077 */
                                ((vPolarA.y - vPolarExt.y) + (vPolarA.y - vPolarExt.y));
                            vPolarA.x = vPolarA.x * g_fl_0_2;  /* @0x404083 */
                            gxVec2FromPolar(&vRefl, &vPolarA); /* @0x40408d */
                            gxVec2Add(&vV4, &vHit, &vRefl);    /* @0x4040a0 */
                            gxVec2Set(&vPolarDir, pEdge->flV1Z - pEdge->flV0Z,
                                      pEdge->flV1X - pEdge->flV0X);
                            mathVec2Polar(&vPolarDir, &vPolarDir); /* @0x4040e2 */
                            vPolarDir.x = 1.0f;                /* @0x404108 */
                            gxVec2FromPolar(&vExt, &vPolarDir); /* v5 @0x404116 */
                            objShotAdd(pNode, (int *)pSrc,            /* @0x4054e0 @0x40416f */
                                       vHit.x - pSrc->vWorldA.x,
                                       vHit.y - pSrc->vWorldA.y,
                                       vV4.x - pSrc->vWorldA.x, vV4.y - pSrc->vWorldA.y,
                                       vPolarA.x, vPolarA.y,
                                       vHit.x, vHit.y,
                                       vExt.x, vExt.y,
                                       vPolarA.x,
                                       vPolarB.x, vPolarB.y,
                                       NULL);
                        }
                    }
                }
            }
        }

        /* Perpendicular setup for the circle phases. */
        gxVec2Set(&vPolarDir, vB.x - vA.x, vB.y - vA.y);       /* local_1b8 @0x4041f4 */
        mathVec2Polar(&vPolarAB, &vPolarDir);                  /* local_2a8 @0x4041fe */
        gxVec2Set(&vDirIn, 1.0f, vPolarAB.y + g_flHalfPi);     /* local_2b4 @0x4041ea */
        gxVec2FromPolar(&vPerp, &vDirIn);                      /* local_2bc @0x40421a */
        flRad2 = pSrc->flExtentA * pSrc->flExtentA;            /* local_2ac @0x40421c */

        /* Phase 3 — walk edges, perpendicular-circle test, scale 0.5. */
        for (i = 0; apSurfaces[i] != NULL; i++) {
            for (pEdge = ((AiNavNode *)apSurfaces[i])->pEdgeList;
                 pEdge != NULL; pEdge = pEdge->pNext) {        /* @0x40423a */
                flProj = (vA.x - pEdge->flV0Z) * vPerp.x +     /* @0x40425c */
                         (vA.y - pEdge->flV0X) * vPerp.y;
                if (flProj < 0.0f) {                           /* @0x404272 */
                    flProj = (float)((double)flProj * g_dbl_1); /* @0x44b280 @0x404282 */
                }
                if (flProj <= pSrc->flExtentA) {               /* @0x404290 */
                    gxVec2Set(&vS1, pEdge->flV0Z, pEdge->flV0X); /* local_1a8 @0x40428c */
                    gxVec2Set(&vS2, pEdge->flV0Z - vPerp.x,    /* local_198 @0x404246 */
                              pEdge->flV0X - vPerp.y);
                    mathSegIntersect(vA.x, vA.y, vB.x, vB.y,   /* @0x406130 @0x4042b0 */
                                     vS1.x, vS1.y, vS2.x, vS2.y, &vHit.x);
                    gxVec2Set(&vOff, sqrtf(flRad2 - flProj * flProj), /* local_188 @0x4042f0 */
                              vPolarAB.y);
                    gxVec2FromPolar(&vOff, &vOff);             /* local_178 @0x404304 */
                    gxVec2Add(&vHit, &vHit, &vOff);            /* local_168 @0x404310 */
                    if (OBJ_HIT_IN_BOX(&vHit, &vA, &vB, (float)g_dbl_1_2)) { /* @0x404336 */
                        gxVec2Set(&vPolar, pEdge->flV0Z - vHit.x, /* local_158 @0x40435a */
                                  pEdge->flV0X - vHit.y);
                        mathVec2Polar(&vPolar, &vPolar);       /* local_1e0 @0x404372 */
                        gxVec2Set(&vPolarB, vHit.x - vB.x, vHit.y - vB.y); /* local_148 @0x404386 */
                        mathVec2Polar(&vPolarB, &vPolarB);     /* local_298 @0x40439a */
                        gxVec2Set(&vPolarA, vHit.x - vA.x, vHit.y - vA.y); /* local_138 @0x4043ae */
                        mathVec2Polar(&vPolarA, &vPolarA);     /* local_2dc @0x4043c0 */
                        vPolarA.y = vPolarA.y -                /* @0x4043e2 */
                            ((vPolarA.y - vPolar.y) + (vPolarA.y - vPolar.y));
                        vPolarA.x = vPolarA.x * g_flShotHalf;  /* @0x4043f2 */
                        gxVec2FromPolar(&vRefl, &vPolarA);     /* local_128 @0x4043fa */
                        gxVec2Add(&vV4, &vHit, &vRefl);        /* local_280 @0x40440c */
                        gxVec2Set(&vPolarDir, pEdge->flV1Z - pEdge->flV0Z, /* local_108 @0x40441e */
                                  pEdge->flV1X - pEdge->flV0X);
                        mathVec2Polar(&vPolarDir, &vPolarDir); /* local_290 @0x40444e */
                        vPolarDir.x = 1.0f;
                        gxVec2FromPolar(&vExt, &vPolarDir);    /* v5 @0x40445c */
                        gxVec2Sub(&vV0, &vHit, &pSrc->vWorldA); /* @0x4045d7 */
                        gxVec2Sub(&vV2, &vV4, &pSrc->vWorldA); /* @0x4045a2 */
                        objShotAdd(pNode, (int *)pSrc,                /* @0x4054e0 @0x4045e3 */
                                   vV0.x, vV0.y,
                                   vV2.x, vV2.y,
                                   vPolarA.x, vPolarA.y,
                                   vHit.x, vHit.y,
                                   vExt.x, vExt.y,
                                   vPolarA.x,
                                   vPolarB.x, vPolarB.y,
                                   NULL);
                    }
                }
            }
        }

        /* Phase 4 — connection walls, perpendicular-circle test, scale
         * 0.5, plus the pPeerMesh plane test at the edge V0 point. */
        for (i = 0; apSurfaces[i] != NULL; i++) {
            for (pEdge = ((AiNavNode *)apSurfaces[i])->pConnList;
                 pEdge != NULL; pEdge = pEdge->pNext) {        /* @0x40463c */
                if (pEdge->pPeerMesh == NULL) {                /* @0x404650 */
                    continue;
                }
                flProj = (vA.x - pEdge->flV0Z) * vPerp.x +     /* @0x404658 */
                         (vA.y - pEdge->flV0X) * vPerp.y;
                if (flProj < 0.0f) {                           /* @0x40468a */
                    flProj = (float)((double)flProj * g_dbl_1);
                }
                if (flProj > pSrc->flExtentA) {                /* @0x404696 */
                    continue;
                }
                gxVec2Set(&vS1, pEdge->flV0Z, pEdge->flV0X);   /* local_d8 @0x4046a0 */
                gxVec2Set(&vS2, pEdge->flV0Z - vPerp.x,        /* local_c8 @0x4046ba */
                          pEdge->flV0X - vPerp.y);
                mathSegIntersect(vA.x, vA.y, vB.x, vB.y,       /* @0x406130 @0x4046de */
                                 vS1.x, vS1.y, vS2.x, vS2.y, &vHit.x);
                gxVec2Set(&vOff, sqrtf(flRad2 - flProj * flProj), /* local_b8 @0x404700 */
                          vPolarAB.y);
                gxVec2FromPolar(&vOff, &vOff);                 /* local_a8 @0x404748 */
                gxVec2Add(&vHit, &vHit, &vOff);                /* local_98 @0x404754 */
                if (OBJ_HIT_IN_BOX(&vHit, &vA, &vB, (float)g_dbl_1_2) && /* @0x40477a */
                    pSrc->pSurface != NULL) {                  /* @0x4047a6 */
                    pPeer = (AiNavNode *)pEdge->pPeerMesh;
                    flPlanePeer =                              /* @0x4047b4 */
                        (vS1.x - (float)pPeer->nRefZ) * pPeer->flSlopeZ +
                        (vS1.y - (float)pPeer->nRefX) * pPeer->flSlopeX +
                        (float)pPeer->nRefY + pSrc->flExtentC;
                    flPlaneSurf =
                        (vS1.x - (float)((AiNavNode *)pSrc->pSurface)->nRefZ) *
                        ((AiNavNode *)pSrc->pSurface)->flSlopeZ +
                        (vS1.y - (float)((AiNavNode *)pSrc->pSurface)->nRefX) *
                        ((AiNavNode *)pSrc->pSurface)->flSlopeX +
                        (float)((AiNavNode *)pSrc->pSurface)->nRefY;
                    if (flPlanePeer < flPlaneSurf &&           /* @0x4047f2 */
                        flPlanePeer < pSrc->flHeight) {        /* @0x40481c */
                        gxVec2Set(&vPolar, pEdge->flV0Z - vHit.x, /* local_88 @0x404856 */
                                  pEdge->flV0X - vHit.y);
                        mathVec2Polar(&vPolar, &vPolar);       /* local_1d8 @0x40486e */
                        gxVec2Set(&vPolarB, vHit.x - vB.x, vHit.y - vB.y); /* local_78 @0x404882 */
                        mathVec2Polar(&vPolarB, &vPolarB);     /* local_270 @0x40489c */
                        gxVec2Set(&vPolarA, vHit.x - vA.x, vHit.y - vA.y); /* local_68 @0x4048b0 */
                        mathVec2Polar(&vPolarA, &vPolarA);     /* local_2cc @0x4048d6 */
                        vPolarA.y = vPolarA.y -                /* @0x404906 */
                            ((vPolarA.y - vPolar.y) + (vPolarA.y - vPolar.y));
                        vPolarA.x = vPolarA.x * g_flShotHalf;  /* @0x40491a */
                        gxVec2FromPolar(&vRefl, &vPolarA);     /* local_58 @0x404922 */
                        gxVec2Add(&vV4, &vHit, &vRefl);        /* local_250 @0x404934 */
                        gxVec2Set(&vPolarDir, pEdge->flV1Z - pEdge->flV0Z, /* local_38 @0x404946 */
                                  pEdge->flV1X - pEdge->flV0X);
                        mathVec2Polar(&vPolarDir, &vPolarDir); /* local_260 @0x40497a */
                        vPolarDir.x = 1.0f;
                        gxVec2FromPolar(&vExt, &vPolarDir);    /* v5 @0x404988 */
                        gxVec2Sub(&vV0, &vHit, &pSrc->vWorldA); /* @0x404a41 */
                        gxVec2Sub(&vV2, &vV4, &pSrc->vWorldA); /* @0x404a0e */
                        objShotAdd(pNode, (int *)pSrc,                /* @0x4054e0 @0x404a4d */
                                   vV0.x, vV0.y,
                                   vV2.x, vV2.y,
                                   vPolarA.x, vPolarA.y,
                                   vHit.x, vHit.y,
                                   vExt.x, vExt.y,
                                   vPolarA.x,
                                   vPolarB.x, vPolarB.y,
                                   NULL);
                    }
                }
            }
        }
        pSrc->pSurface = sceneRayFindSorted(vA.x, vA.y,        /* @0x42a7c0 @0x404a80 */
                                            pSrc->flExtentB, 500.0f,
                                            pSrc->flExtentA, apSurfaces);
    }
}

/* objCollideCheck @0x404ac0 — the objUpdateFire counterpart of
 * objShotCollide: per enabled +0x08 sub-object, walk the g_pObjHead
 * nodes (skipping pNode itself and disabled ones) and their +0x08
 * sub-objects, and objShotAdd a record whenever the other sub-object's
 * world circle overlaps pSrc's (radius = flExtentA + flExtentA, height
 * band ±500 @0x44b2e4):
 *   mid = B + unit(A - other) * (r - dist) / 2,
 *   ratio = (flExtentB * 0.5) / other->flExtentB,
 *   v1 = polarSum(polarSum({(r - dist) / ratio * 0.125, angle(A - other)},
 *                          {ratio * pNode->field_3c * 0.5, pNode->flScaleC}),
 *                 {pW->field_3c / ratio * 0.333, pW->flScaleC}),
 *   v5 = fromPolar({1.0, pW->flScaleC - PI/2}), flScale = pW->field_3c * 0.25,
 *   v0 = B - vWorldA, v2 = mid - vWorldA, v4 = B, v3 = v1,
 *   pAnimTarget = pW (its +0x44 pParent feeds objWalkAnimSync).
 * Original __fastcall. */
void objCollideCheck(WorldNode *pNode) /* @0x404ac0 */
{
    ObjChildMesh *pSrc;
    ObjChildMesh *pOther;
    WorldNode *pW;
    GxVec2 vA;                 /* local_84: vWorldA + vPos */
    GxVec2 vB;                 /* local_8c: vWorldB + vPosB */
    GxVec2 vC;                 /* local_94: other vWorldA + pW->vPos */
    GxVec2 vDir;               /* A - C polar-in local_40 */
    GxVec2 vPolar;             /* {dist, angle} local_a8 */
    GxVec2 vExt;               /* fromPolar({rem * 0.5, angle}) local_20 */
    GxVec2 vMid;               /* B + vExt local_78 */
    GxVec2 vT1;                /* first polar sum local_38 */
    GxVec2 vT2;                /* second polar sum local_28 -> v1 */
    GxVec2 vS1;                /* {pW-ratio, pW-angle} local_58 */
    GxVec2 vS2;                /* {pNode-ratio, pNode-angle} local_48 */
    GxVec2 vUnit;              /* fromPolar({1.0, pW->flImpulse - PI/2}) local_70 */
    float flRadius;            /* local_10: r = flExtentA + flExtentA */
    float flDist2;             /* dx*dx + dy*dy */
    float flDist;              /* local_7c: sqrt */
    float flRem;               /* r - dist */
    GxVec2 vV0;                /* shot v0: gxVec2Sub(B, vWorldA) */
    GxVec2 vV2;                /* shot v2: gxVec2Sub(mid, vWorldA) */
    float flRatio;             /* (flExtentB * 0.5) / other->flExtentB */
    float flSpeed;             /* pW->field_3c * 0.25 */

    gxVec2SetAngleZero(&vPolar);                           /* local_a0 @0x404acc */
    gxVec2SetAngleZero(&vA);                               /* local_84 @0x404ad5 */
    gxVec2SetAngleZero(&vB);                               /* local_8c @0x404ade */
    gxVec2SetAngleZero(&vC);                               /* local_94 @0x404ae7 */
    gxVec2SetAngleZero(&vUnit);                            /* local_68 @0x404af0 */
    for (pSrc = (ObjChildMesh *)pNode->pChildMeshHead; pSrc != NULL;
         pSrc = pSrc->pNext) {                                 /* @0x404af9 */
        if (pSrc->nMeshId == 0) {                              /* @0x404b04 */
            continue;
        }
        gxVec2Add(&vA, &pSrc->vWorldA, &pNode->vPos);          /* local_84 @0x404b1d */
        gxVec2Add(&vB, &pSrc->vWorldB, &pNode->vPosB);         /* local_8c @0x404b3f */
        for (pW = g_pObjHead; pW != NULL; pW = pW->pPrev) {    /* @0x404b46 */
            if (pW == pNode || pW->field_1c == 0) {            /* @0x404b62 */
                continue;
            }
            for (pOther = (ObjChildMesh *)pW->pChildMeshHead; pOther != NULL;
                 pOther = pOther->pNext) {                     /* @0x404b75 */
                if (pSrc->flExtentB > pOther->flHeight + g_fl_500 || /* @0x404b80 */
                    pOther->flHeight - g_fl_500 > pSrc->flExtentB) {
                    continue;
                }
                gxVec2Add(&vC, &pOther->vWorldA, &pW->vPos);   /* local_94 @0x404bbe */
                flRadius = pOther->flExtentA + pSrc->flExtentA; /* @0x404bd3 */
                flDist2 = (vA.x - vC.x) * (vA.x - vC.x) +
                          (vA.y - vC.y) * (vA.y - vC.y);       /* @0x404bf5 */
                if (flDist2 > flRadius * flRadius) {           /* @0x404c03 */
                    continue;
                }
                flDist = sqrtf(flDist2);                       /* local_7c @0x404c1a */
                gxVec2Set(&vDir, vA.x - vC.x, vA.y - vC.y);    /* local_40 @0x404c31 */
                mathVec2Polar(&vPolar, &vDir);                 /* local_a8 @0x404c3b */
                flRem = flRadius - flDist;                     /* @0x404c40 */
                gxVec2Set(&vExt, flRem * g_flShotHalf, vPolar.y); /* @0x404c64 */
                gxVec2FromPolar(&vExt, &vExt);                 /* local_20 @0x404c72 */
                gxVec2Add(&vMid, &vB, &vExt);                  /* local_78 @0x404c82 */
                flRatio = (pSrc->flExtentB * g_flShotHalf) /
                          pOther->flExtentB;                   /* @0x404c87 */
                gxVec2Set(&vT1, (flRem / flRatio) * g_fl_0_125, /* local_a8 @0x404cb0 */
                          vPolar.y);
                gxVec2Set(&vS1, (pW->field_3c / flRatio) * g_fl_0_333, /* local_58 @0x404cd2 */
                          pW->flScaleC);
                gxVec2Set(&vS2, flRatio * pNode->field_3c * g_flShotHalf, /* local_48 @0x404cf4 */
                          pNode->flScaleC);
                gxVec2RotateAdd(&vT1, &vT1, &vS2);             /* local_38 @0x404d07 */
                gxVec2RotateAdd(&vT2, &vT1, &vS1);             /* local_28 @0x404d18 */
                gxVec2Set(&vUnit, 1.0f, pW->flScaleC - g_flHalfPi); /* local_70 @0x404d3b */
                gxVec2FromPolar(&vUnit, &vUnit);               /* v5 @0x404d69 */
                flSpeed = pW->field_3c * g_fl_0_25;            /* +0x3c @0x404d4f */
                gxVec2Sub(&vV0, &vB, &pSrc->vWorldA);          /* @0x435020 @0x404db3 */
                gxVec2Sub(&vV2, &vMid, &pSrc->vWorldA);        /* @0x404d8a */
                objShotAdd(pNode, (int *)pSrc,                        /* @0x4054e0 @0x404dc5 */
                           vV0.x, vV0.y,
                           vV2.x, vV2.y,
                           vT2.x, vT2.y,                       /* v1 = v3 @0x404d2b */
                           vB.x, vB.y,                         /* v4 @0x404d6e */
                           vUnit.x, vUnit.y,                   /* v5 @0x404d69 */
                           flSpeed,                            /* @0x404d52 */
                           vT2.x, vT2.y,                       /* v3 @0x404d47 */
                           pW);
            }
        }
    }
}