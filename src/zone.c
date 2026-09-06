#include <windows.h>
#include <stdlib.h>
#include "zone.h"
#include "scene.h"
#include "pool.h"
#include "stubs.h"
#include "util.h"
#include "player.h"

/* =====================================================================
 * zone.c — AR/IN zone-connection nodes.
 * Reimplemented from the verified Ghidra decompilations and disassembly
 * of zoneConnCtor @0x42b410 and zoneConnCollectMeshes @0x42b490.
 * ===================================================================== */

AiNavNode *g_pNavNodeList; /* @0x45e5e0 AI nav-node list head (aiNavNodeCtorScene links; zoneWallListBuild re-sorts) */
void *g_pZoneConnHead;   /* @0x45e5e8 */
void *g_pZoneConnTail;   /* @0x45e5ec */
void *g_zoneConn;        /* @0x45e5e4 */

/* Shared polygon-walk body of zoneConnCollectMeshes (tri: 3 vertices per
 * fan, quad: 4). Vertex component -> world mapping verified in the
 * disassembly: objContainsPoint3D receives (v[2]+pos[2], v[0]+pos[0],
 * v[1]+pos[1]) — i.e. (flX, flY, flZ-vertical) = (worldZ, worldX,
 * worldY). Returns 1 when any vertex is inside the AR zone; stores the
 * cell index into pConn when the fill pass is active. */
static int zoneConnTestPrim(ZoneConn *pConn, EventObject *pArZone,
                            byte *prim, const short *pVerts,
                            const int *pnPos, int nCell, int nFill) /* @0x42b55c.. */
{
    int nFans = prim[0];
    int nIdxPerFan = prim[6];
    byte *pCur = prim + 8;
    byte *pEnd = prim + 8 + nFans * nIdxPerFan * 2;
    int nVerts = (prim[1] == 3) ? 3 : 4;
    int k;

    for (; pCur < pEnd; pCur += nIdxPerFan * 2) {
        for (k = 0; k < nVerts; k++) {
            const short *v = pVerts + pCur[k] * 4;   /* idx * 8 bytes */
            if (objContainsPoint3D(pArZone,
                                   (float)(int)v[2] + (float)pnPos[2],
                                   (float)(int)v[0] + (float)pnPos[0],
                                   (float)(int)v[1] + (float)pnPos[1]) != 0) {
                if (nFill == 0) {
                    pConn->pMeshIdx[pConn->nCount] = nCell;
                }
                pConn->nCount++;
                return 1;
            }
        }
    }
    return 0;
}

/* zoneConnCollectMeshes @0x42b490 — pass 1 (nFill=1): count the grid
 * cells whose mesh touches the AR zone; pass 2 (nFill=0): store their
 * indices into pConn->pMeshIdx (allocated between the passes with the
 * pass-1 count). The mesh data comes from SceneObjRenderInfo (+0x14 poly
 * count, +0x18 primitive pointer array, +0x08 vertex table). Primitive
 * types below 3 hit the original's fatalError(NULL) branch. */
void zoneConnCollectMeshes(ZoneConn *pConn, EventObject *pArZone, int nFill) /* @0x42b490 */
{
    SceneDetailGrid *pGrid = (SceneDetailGrid *)pConn->pDetailLevels;
    int i;

    pConn->nCount = 0;                                   /* 0x42b4a5 */
    if (pGrid->nColsFilled <= 0) {
        return;
    }
    for (i = 0; i < pGrid->nColsFilled; i++) {           /* 0x42b4c8..0x42b851 */
        SceneNode *pNode = (SceneNode *)(size_t)pGrid->pCells[i];
        SceneObjTypeDef *pMesh = sceneNodeGetMesh(pNode);
        SceneObjRenderInfo *pRender = pMesh->pRender;    /* mesh +0x14 */
        int anPos[3];
        int bFound = 0;
        int j;

        sceneNodeGetPos(pNode, 0, anPos, 4);             /* @0x431270 */
        for (j = 0; j < pRender->nPolyA && !bFound; j++) {
            byte *prim = ((byte **)pRender->pPolyA)[j];
            switch (prim[1]) {
            case 0:
            case 1:
            case 2:
                /* Original: fatalError(NULL) @0x42b583 (NULL format, never
                 * reached with valid scene data). */
                fatalError("");
                break;
            case 3:
            case 4:
                bFound = zoneConnTestPrim(pConn, pArZone, prim,
                                          (const short *)pRender->pVerts, anPos, i, nFill);
                break;
            }
        }
    }
}

/* zoneConnCtor @0x42b410 — build the connection node (see zone.h). The
 * g_zoneConn scratch global is cleared first; the node is prepended to
 * the g_pZoneConnHead list (+0x4 = previous head, +0x8 = next). */
void *zoneConnCtor(ZoneConn *pConn, EventObject *pInZone, EventObject *pArZone,
                   void *pDetailLevels) /* @0x42b410 */
{
    g_zoneConn = NULL;                                   /* 0x42b420 */
    pConn->pDetailLevels = pDetailLevels;                /* +0x18 */
    pConn->pInZone = pInZone;                            /* +0x14 */
    zoneConnCollectMeshes(pConn, pArZone, 1);            /* counting pass */
    pConn->pMeshIdx = (int *)memPoolAlloc(0, (size_t)pConn->nCount * 4);  /* 0x42b443 */
    zoneConnCollectMeshes(pConn, pArZone, 0);            /* fill pass */
    if (g_pZoneConnHead != NULL) {
        ((ZoneConn *)g_pZoneConnHead)->pNext = pConn;    /* 0x42b461 */
    } else {
        g_pZoneConnTail = pConn;                         /* 0x42b46b */
    }
    pConn->pPrev = (ZoneConn *)g_pZoneConnHead;          /* 0x42b471 */
    g_pZoneConnHead = pConn;                             /* 0x42b474 */
    pConn->pNext = NULL;                                 /* 0x42b47a */
    return pConn;
}

/* --- nav-mesh wall tests (sceneRayFindNearest @0x42a750 helpers) --- */

static const double g_dblWallTol = 2.0; /* @0x44b728 (bytes 00 00 00 00 00 00 00 40) */
static const double g_dblHitTol = 1.0;  /* @0x44b288 (bytes 00 00 00 00 00 00 F0 3F) */

/* zoneWallPointSide @0x42a870 — nearest-wall side test over both edge
 * lists of pMesh (pEdgeList +0x3c first, then pConnList +0x38). The
 * nearest-edge search state (flBest/nResult) is shared across both
 * passes, matching the original's single local pair. */
int zoneWallPointSide(AiNavNode *pMesh, float flX, float flZ) /* @0x42a870 */
{
    AiNavEdge *pEdge;
    int nResult = 0;
    float flBest = -1.0f;
    GxVec2 vDir;
    float flXAt;
    float flDistSq;
    int nPass;

    gxVec2SetAngleZero(&vDir);                           /* @0x434f90 @0x42a87f (dead store, kept for call fidelity) */

    for (nPass = 0; nPass < 2; nPass++) {
        pEdge = (nPass == 0) ? pMesh->pEdgeList : pMesh->pConnList; /* @0x42a88a @0x42a99a */
        for (; pEdge != NULL; pEdge = pEdge->pNext) {    /* @0x42a990 */
            float flSlope;

            if (pEdge->flV0X == pEdge->flV1X) {          /* @0x42a895 */
                continue;
            }
            if ((pEdge->flV0X <= flZ + (float)g_dblWallTol || /* @0x42a8a6 */
                 pEdge->flV1X <= flZ - (float)g_dblWallTol) &&
                (flZ - (float)g_dblWallTol <= pEdge->flV0X || /* @0x42a8d6 */
                 flZ + (float)g_dblWallTol <= pEdge->flV1X)) {
                if (pEdge->flV0Z == pEdge->flV1Z) {      /* @0x42a902 */
                    flXAt = pEdge->flV0Z;
                } else {
                    flSlope = (pEdge->flV1X - pEdge->flV0X) / (pEdge->flV1Z - pEdge->flV0Z);
                    flXAt = (flZ - (pEdge->flV1X - flSlope * pEdge->flV1Z)) / flSlope;
                }
                flDistSq = (flX - flXAt) * (flX - flXAt); /* @0x42a93a */
                if (flBest < 0.0f || flDistSq < flBest) { /* @0x42a940 */
                    flBest = flDistSq;
                    if ((flX - pEdge->flV0Z) * pEdge->flUnitX +        /* @0x42a95c */
                        (flZ - pEdge->flV0X) * pEdge->flUnitNegZ <= (float)g_dblWallTol) {
                        nResult = 1;                     /* @0x42a97f */
                    } else {
                        nResult = 0;                     /* @0x42a986 */
                    }
                }
            }
        }
    }
    (void)vDir;
    return nResult;
}

/* zoneWallCircleHit @0x42aac0 — does point (flX, flZ) with radius
 * flRadius touch pMesh's walls? Inside-side test first, then per
 * connection: endpoint-distance and ray/segment intersection (±1.0 on
 * both axes of the hit point). */
int zoneWallCircleHit(AiNavNode *pMesh, float flX, float flZ, float flRadius) /* @0x42aac0 */
{
    AiNavEdge *pConn;
    GxVec2 vOut;
    GxVec2 vDirPt;
    GxVec2 vPt;
    GxVec2 vV1;
    GxVec2 vV0;

    gxVec2SetAngleZero(&vOut);                           /* @0x434f90 @0x42aacc */
    if (zoneWallPointSide(pMesh, flX, flZ) != 0) {       /* @0x42a870 @0x42aadd */
        return 1;
    }
    for (pConn = pMesh->pConnList; pConn != NULL; pConn = pConn->pNext) { /* @0x42aac0 */
        float flDx = pConn->flV0Z - flX;                 /* @0x42aaff */
        float flDz = pConn->flV0X - flZ;                 /* @0x42ab0a */
        float flDot = pConn->flUnitX * flDx + pConn->flUnitNegZ * flDz; /* @0x42ab23 */

        if (flDot <= flRadius && -flRadius <= flDot) {   /* @0x42ab25 */
            if (flDx * flDx + flDz * flDz < flRadius * flRadius) { /* @0x42ab49 */
                return 1;
            }
            if ((pConn->flV1Z - flX) * (pConn->flV1Z - flX) +     /* @0x42ab7a */
                (pConn->flV1X - flZ) * (pConn->flV1X - flZ) <
                flRadius * flRadius) {
                return 1;
            }
            gxVec2Set(&vDirPt, flX + pConn->flUnitX,    /* @0x434fa0 @0x42abc4 */
                      flZ + pConn->flUnitNegZ);
            gxVec2Set(&vPt, flX, flZ);                  /* @0x42abd6 */
            gxVec2Set(&vV1, pConn->flV1Z, pConn->flV1X); /* @0x42abee */
            gxVec2Set(&vV0, pConn->flV0Z, pConn->flV0X); /* @0x42ac06 */
            if (mathSegIntersect(vV0.x, vV0.y, vV1.x, vV1.y, vPt.x, vPt.y,
                                 vDirPt.x, vDirPt.y, &vOut.x) != 0) { /* @0x406130 @0x42ac12 */
                if ((pConn->flV0Z - (float)g_dblHitTol <= vOut.x && /* @0x42ac22 */
                     vOut.x <= pConn->flV1Z + (float)g_dblHitTol) ||
                    (vOut.x <= pConn->flV0Z + (float)g_dblHitTol &&
                     pConn->flV1Z - (float)g_dblHitTol <= vOut.x)) {
                    if (pConn->flV0X - (float)g_dblHitTol <= vOut.y && /* @0x42ac7a */
                        vOut.y <= pConn->flV1X + (float)g_dblHitTol) {
                        return 1;
                    }
                    if (vOut.y <= pConn->flV0X + (float)g_dblHitTol &&
                        pConn->flV1X - (float)g_dblHitTol <= vOut.y) {
                        return 1;
                    }
                }
            }
        }
    }
    return 0;                                            /* @0x42acea */
}

/* =====================================================================
 * Nav-mesh wall-list passes (zoneWallListBuild @0x42a650 cluster over
 * the AiNavNode list built by aiNavNodeUpdate @0x428d50).
 * ===================================================================== */

static const float g_flHalfPi = 1.5707964f;      /* @0x44b270 */
static const float g_flPlaneSentinel = 1.17549435e-38f; /* @0x44b720 (FLT_MIN, value-compared) */

/* zoneConnLink @0x429c90 — insert pConn into pMesh's cross-mesh
 * connection list (+0x38, head insert, pPrev = NULL) with pPeerMesh set,
 * then derive the float block exactly like aiNavNodeAddEdge: flV0Z/
 * flV0X/flV1Z/flV1X from the raw coords and (+0x1c/+0x20) the unit
 * direction of V0->V1 in (Z,X) rotated -90 degrees (polar angle -= pi/2,
 * polar length forced to 1.0). Called by zoneConnMergeDupesCrossMesh. */
void zoneConnLink(AiNavNode *pMesh, AiNavNode *pPeerMesh, AiNavEdge *pConn) /* @0x429c90 */
{
    GxVec2 vDir;
    GxVec2 vPolar;
    GxVec2 vUnit;

    pConn->pPrev = NULL;                              /* +0x04 */
    pConn->pPeerMesh = pPeerMesh;                     /* +0x08 */
    pConn->pNext = pMesh->pConnList;                  /* +0x00 */
    if (pMesh->pConnList != NULL) {
        pMesh->pConnList->pPrev = pConn;
    }
    pMesh->pConnList = pConn;
    pConn->flV0Z = (float)pConn->nV0z;                /* +0x0c */
    pConn->flV0X = (float)pConn->nV0x;                /* +0x10 */
    pConn->flV1Z = (float)pConn->nV1z;                /* +0x14 */
    pConn->flV1X = (float)pConn->nV1x;                /* +0x18 */
    gxVec2Set(&vDir, pConn->flV1Z - pConn->flV0Z,
              pConn->flV1X - pConn->flV0X);           /* @0x434fa0 */
    mathVec2Polar(&vPolar, &vDir);                    /* @0x435060 */
    vPolar.y -= g_flHalfPi;
    vPolar.x = 1.0f;
    gxVec2FromPolar(&vUnit, &vPolar);                 /* @0x434fc0 */
    pConn->flUnitX = vUnit.x;                         /* +0x1c */
    pConn->flUnitNegZ = vUnit.y;                      /* +0x20 */
}

/* Endpoints of two nav edges overlap within nTol on X/Z and nYTol on Y
 * (12-comparison AABB test from zoneConnMergeDupesInMesh; the endpoint
 * order is orientation-independent). */
static int zoneConnBoxesOverlap(AiNavEdge *pA, AiNavEdge *pB, int nTol, int nYTol)
{
    return pA->nV1x <= pB->nV0x + nTol &&
           pA->nV1y <= pB->nV0y + nYTol &&
           pA->nV1z <= pB->nV0z + nTol &&
           pB->nV0x - nTol <= pA->nV1x &&
           pB->nV0y - nYTol <= pA->nV1y &&
           pB->nV0z - nTol <= pA->nV1z &&
           pA->nV0x <= pB->nV1x + nTol &&
           pA->nV0y <= pB->nV1y + nYTol &&
           pA->nV0z <= pB->nV1z + nTol &&
           pB->nV1x - nTol <= pA->nV0x &&
           pB->nV1y - nYTol <= pA->nV0y &&
           pB->nV1z - nTol <= pA->nV0z;
}

/* zoneConnMergeDupesInMesh @0x429d60 — merge duplicate AABB-overlapping
 * edges (tolerance +-2 on all three axes) within one mesh's pEdgeList
 * (+0x3c): both records are unlinked (doubly-linked, head fixup) and
 * freed; the outer scan continues from the pre-captured successor. */
void zoneConnMergeDupesInMesh(AiNavNode *pMesh) /* @0x429d60 */
{
    AiNavEdge *pA = pMesh->pEdgeList;

    while (pA != NULL) {
        AiNavEdge *pNextA = pA->pNext;
        AiNavEdge *pB;

        for (pB = pNextA; pB != NULL; pB = pB->pNext) {
            if (zoneConnBoxesOverlap(pA, pB, 2, 2)) {
                if (pA->pPrev != NULL) {              /* unlink pA */
                    pA->pPrev->pNext = pA->pNext;
                }
                if (pA->pNext != NULL) {
                    pA->pNext->pPrev = pA->pPrev;
                }
                if (pA == pMesh->pEdgeList) {
                    pMesh->pEdgeList = pA->pNext;
                }
                if (pB->pPrev != NULL) {              /* unlink pB */
                    pB->pPrev->pNext = pB->pNext;
                }
                if (pB->pNext != NULL) {
                    pB->pNext->pPrev = pB->pPrev;
                }
                if (pB == pMesh->pEdgeList) {
                    pMesh->pEdgeList = pB->pNext;
                }
                free(pA);                             /* memFreeDirect @0x43dd37 */
                free(pB);
                break;
            }
        }
        pA = pNextA;                                  /* pre-captured before the merge */
    }
}

/* zoneConnMergeDupesCrossMesh @0x429e90 — two sweeps over this mesh's
 * pEdgeList against every other mesh's pEdgeList: sweep 1 with +-2 on
 * all axes, sweep 2 with +-2 on X/Z but +-2500 (0x9c4) on Y (floors one
 * ramp-step apart). On a match both records are unlinked (NOT freed)
 * and re-linked through zoneConnLink into the owners' +0x38 conn lists
 * with the peer mesh recorded. */
void zoneConnMergeDupesCrossMesh(AiNavNode *pMesh) /* @0x429e90 */
{
    int nPass;

    for (nPass = 0; nPass < 2; nPass++) {
        int nYTol = (nPass == 0) ? 2 : 2500;
        AiNavEdge *pA = pMesh->pEdgeList;

        while (pA != NULL) {
            AiNavEdge *pNextA = pA->pNext;
            AiNavNode *pPeer;
            int bMerged = 0;

            for (pPeer = g_pNavNodeList; pPeer != NULL && !bMerged;
                 pPeer = pPeer->pNext) {
                AiNavEdge *pB;

                if (pPeer == pMesh) {
                    continue;
                }
                for (pB = pPeer->pEdgeList; pB != NULL; pB = pB->pNext) {
                    if (zoneConnBoxesOverlap(pA, pB, 2, nYTol)) {
                        if (pA->pPrev != NULL) {      /* unlink pA from pMesh */
                            pA->pPrev->pNext = pA->pNext;
                        }
                        if (pA->pNext != NULL) {
                            pA->pNext->pPrev = pA->pPrev;
                        }
                        if (pA == pMesh->pEdgeList) {
                            pMesh->pEdgeList = pA->pNext;
                        }
                        if (pB->pPrev != NULL) {      /* unlink pB from pPeer */
                            pB->pPrev->pNext = pB->pNext;
                        }
                        if (pB->pNext != NULL) {
                            pB->pNext->pPrev = pB->pPrev;
                        }
                        if (pB == pPeer->pEdgeList) {
                            pPeer->pEdgeList = pB->pNext;
                        }
                        zoneConnLink(pMesh, pPeer, pA);   /* @0x429c90 */
                        zoneConnLink(pPeer, pMesh, pB);
                        bMerged = 1;
                        break;
                    }
                }
            }
            pA = pNextA;
        }
    }
}

/* zoneWallCalcPlane @0x42a1c0 — compute pMesh's walkable plane from its
 * pEdgeList: reference point (+0xc/+0x10/+0x14) = V0 of the first edge
 * whose endpoints differ in Y (first edge's V0 otherwise), flAvgY
 * (+0x00) = mean of the per-edge (nV1y+nV0y)/2 midpoints, and the
 * slopes +0x04/+0x08 = tan(polar(normal.*, normal.y) - pi/2) per axis
 * (i.e. -Nz/Ny and -Nx/Ny via the gxVec2 polar round-trip), each guarded
 * by the rotated unit vector's X being non-zero. Returns without
 * touching the slopes when the list is empty or every edge is vertical
 * (the 1.17549435e-38f sentinel @0x44b720 is value-compared). */
void zoneWallCalcPlane(AiNavNode *pMesh) /* @0x42a1c0 */
{
    AiNavEdge *pEdge = pMesh->pEdgeList;
    AiNavEdge *pCur;
    float flFound;
    float flCount;
    GxVec2 vDir;
    GxVec2 vPolarA;
    GxVec2 vPolarB;
    GxVec2 vUnitA;
    GxVec2 vUnitB;

    if (pEdge == NULL) {
        return;
    }
    pMesh->nRefX = pEdge->nV0x;                       /* +0x0c @0x42a1da */
    pMesh->nRefY = pEdge->nV0y;                       /* +0x10 */
    pMesh->nRefZ = pEdge->nV0z;                       /* +0x14 */
    flFound = 1.17549435e-38f;                        /* sentinel @0x44b720 */
    for (pCur = pEdge; pCur != NULL; pCur = pCur->pNext) {
        if (pCur->nV1y != pCur->nV0y) {               /* first non-vertical edge @0x42a205 */
            pMesh->nRefX = pCur->nV0x;
            pMesh->nRefY = pCur->nV0y;
            pMesh->nRefZ = pCur->nV0z;
            flFound = (float)pCur->nV1y;
            break;
        }
    }
    pMesh->flAvgY = 0.0f;                             /* @0x42a224 */
    flCount = 0.0f;
    for (pCur = pEdge; pCur != NULL; pCur = pCur->pNext) {
        int nMid = (pCur->nV1y + pCur->nV0y) / 2;     /* signed div, trunc toward 0 */

        pMesh->flAvgY += (float)nMid;
        flCount += 1.0f;                              /* @0x44b260 */
    }
    pMesh->flAvgY = pMesh->flAvgY / flCount;          /* @0x42a24e */
    pMesh->flSlopeZ = 0.0f;                           /* +0x04 @0x42a252 */
    pMesh->flSlopeX = 0.0f;                           /* +0x08 @0x42a255 */
    if (flFound == g_flPlaneSentinel) {               /* all edges vertical @0x42a25c */
        return;
    }
    gxVec2Set(&vDir, pMesh->flNormalZ, pMesh->flNormalY); /* (Nz, Ny) @0x42a26d */
    mathVec2Polar(&vPolarA, &vDir);                   /* @0x435060 */
    vPolarA.y -= g_flHalfPi;
    gxVec2Set(&vDir, pMesh->flNormalX, pMesh->flNormalY); /* (Nx, Ny) @0x42a288 */
    mathVec2Polar(&vPolarB, &vDir);
    vPolarB.y -= g_flHalfPi;
    gxVec2FromPolar(&vUnitA, &vPolarA);               /* @0x434fc0 guard probe */
    if (vUnitA.x != 0.0f) {
        gxVec2FromPolar(&vUnitA, &vPolarA);
        gxVec2FromPolar(&vUnitB, &vPolarA);
        pMesh->flSlopeZ = vUnitA.y / vUnitB.x;        /* = -Nz/Ny via tan @0x42a2ff */
    }
    gxVec2FromPolar(&vUnitA, &vPolarB);
    if (vUnitA.x != 0.0f) {
        gxVec2FromPolar(&vUnitA, &vPolarB);
        gxVec2FromPolar(&vUnitB, &vPolarB);
        pMesh->flSlopeX = vUnitA.y / vUnitB.x;        /* = -Nx/Ny via tan @0x42a343 */
    }
}

/* zoneWallMergeDupesSameDir @0x42a360 — merge pEdgeList (+0x3c) and then
 * pConnList (+0x38) records that share the same polar direction
 * (+0x1c/+0x20 float-compared; the conn pass also requires the same
 * pPeerMesh) and have touching endpoints (tolerance +-1): the survivor
 * extends over the duplicate (case 1: takes the duplicate's V0, case 2:
 * its V1, float endpoint fields updated alongside) and the duplicate is
 * unlinked and freed. The outer cursor always advances to the
 * pre-captured successor. */
void zoneWallMergeDupesSameDir(AiNavNode *pMesh) /* @0x42a360 */
{
    int nPass;

    for (nPass = 0; nPass < 2; nPass++) {
        AiNavEdge *pA = (nPass == 0) ? pMesh->pEdgeList : pMesh->pConnList;

        while (pA != NULL) {
            AiNavEdge *pNextA = pA->pNext;
            AiNavEdge *pB;

            for (pB = pNextA; pB != NULL; pB = pB->pNext) {
                if (pA->flUnitX == pB->flUnitX &&
                    pA->flUnitNegZ == pB->flUnitNegZ &&
                    (nPass == 0 || pA->pPeerMesh == pB->pPeerMesh)) {
                    int bMerged = 0;

                    if (pA->nV1x <= pB->nV0x + 1 &&   /* case 1: A.v1 near B.v0 */
                        pA->nV1y <= pB->nV0y + 1 &&
                        !(pB->nV0z + 1 < pA->nV1z) &&
                        !(pA->nV1x < pB->nV0x - 1) &&
                        !(pA->nV1y < pB->nV0y - 1) &&
                        !(pA->nV1z < pB->nV0z - 1)) {
                        pB->nV0x = pA->nV0x;          /* survivor takes A's V0 */
                        pB->nV0y = pA->nV0y;
                        pB->nV0z = pA->nV0z;
                        pB->flV0Z = pA->flV0Z;
                        pB->flV0X = pA->flV0X;
                        bMerged = 1;
                    } else if (pA->nV0x <= pB->nV1x + 1 &&   /* case 2: A.v0 near B.v1 */
                               pA->nV0y <= pB->nV1y + 1 &&
                               pA->nV0z <= pB->nV1z + 1 &&
                               pB->nV1x - 1 <= pA->nV0x &&
                               pB->nV1y - 1 <= pA->nV0y &&
                               pB->nV1z - 1 <= pA->nV0z) {
                        pB->nV1x = pA->nV1x;          /* survivor takes A's V1 */
                        pB->nV1y = pA->nV1y;
                        pB->nV1z = pA->nV1z;
                        pB->flV1Z = pA->flV1Z;
                        pB->flV1X = pA->flV1X;
                        bMerged = 1;
                    }
                    if (bMerged) {
                        if (pA->pPrev != NULL) {      /* unlink the duplicate */
                            pA->pPrev->pNext = pA->pNext;
                        }
                        if (pA->pNext != NULL) {
                            pA->pNext->pPrev = pA->pPrev;
                        }
                        if (nPass == 0) {
                            if (pA == pMesh->pEdgeList) {
                                pMesh->pEdgeList = pA->pNext;
                            }
                        } else {
                            if (pA == pMesh->pConnList) {
                                pMesh->pConnList = pA->pNext;
                            }
                        }
                        free(pA);                     /* memFreeDirect @0x43dd37 */
                        break;
                    }
                }
            }
            pA = pNextA;
        }
    }
}

/* zoneWallListBuild @0x42a650 — the post-load pass over g_pNavNodeList:
 * zoneConnMergeDupesInMesh per node, zoneWallCalcPlane per node,
 * zoneConnMergeDupesCrossMesh per node, an ascending insertion sort of
 * the doubly-linked node list by flAvgY (pulled out of the list and
 * re-linked after the nearest predecessor with flAvgY <= its own, head
 * insert when none), then zoneWallMergeDupesSameDir per node. */
void zoneWallListBuild(void) /* @0x42a650 */
{
    AiNavNode *pNode;

    for (pNode = g_pNavNodeList; pNode != NULL; pNode = pNode->pNext) {
        zoneConnMergeDupesInMesh(pNode);              /* @0x429d60 */
    }
    for (pNode = g_pNavNodeList; pNode != NULL; pNode = pNode->pNext) {
        zoneWallCalcPlane(pNode);                     /* @0x42a1c0 */
    }
    for (pNode = g_pNavNodeList; pNode != NULL; pNode = pNode->pNext) {
        zoneConnMergeDupesCrossMesh(pNode);           /* @0x429e90 */
    }
    {
        AiNavNode *pIns = (g_pNavNodeList != NULL) ? g_pNavNodeList->pNext : NULL;

        while (pIns != NULL) {                        /* sort @0x42a69d..0x42a72d */
            AiNavNode *pNextIns = pIns->pNext;
            AiNavNode *pScan = pIns->pPrev;
            int bMoved = 0;

            if (pScan != NULL) {
                while (pIns->flAvgY < pScan->flAvgY) {
                    if (pScan->pPrev == NULL) {
                        /* pScan is the list head: insert before it. */
                        if (pIns->pPrev != NULL) {
                            pIns->pPrev->pNext = pIns->pNext;
                        }
                        if (pIns->pNext != NULL) {
                            pIns->pNext->pPrev = pIns->pPrev;
                        }
                        g_pNavNodeList = pIns;
                        pScan->pPrev = pIns;
                        pIns->pNext = pScan;
                        pIns->pPrev = NULL;
                        bMoved = 1;
                        break;
                    }
                    pScan = pScan->pPrev;
                }
                if (!bMoved && pIns->pPrev != pScan) {
                    /* Insert after pScan (the first node whose flAvgY is
                     * <= pIns's walking back from pIns's old position). */
                    if (pIns->pPrev != NULL) {
                        pIns->pPrev->pNext = pIns->pNext;
                    }
                    if (pIns->pNext != NULL) {
                        pIns->pNext->pPrev = pIns->pPrev;
                    }
                    if (pScan->pNext != NULL) {
                        pScan->pNext->pPrev = pIns;
                    }
                    pIns->pPrev = pScan;
                    pIns->pNext = pScan->pNext;
                    pScan->pNext = pIns;
                }
            }
            pIns = pNextIns;
        }
    }
    for (pNode = g_pNavNodeList; pNode != NULL; pNode = pNode->pNext) {
        zoneWallMergeDupesSameDir(pNode);             /* @0x42a360 */
    }
}

/* zoneConnUnlink @0x42b890 — free the node's mesh-index array (+0x10,
 * memPoolFree pool 0) when set, fix the list head (g_pZoneConnHead
 * @0x45e5e8) / tail (g_pZoneConnTail @0x45e5ec) when they point at this
 * node, and splice the neighbors together (+0x04 = toward the tail, +0x08
 * = toward the head). The caller frees the node record itself. */
void zoneConnUnlink(ZoneConn *pConn) /* @0x42b890 */
{
    if (pConn->pMeshIdx != NULL) {                       /* @0x42b894 */
        memPoolFree(0, pConn->pMeshIdx);                 /* @0x42b899 */
    }
    if ((ZoneConn *)g_pZoneConnHead == pConn) {          /* @0x42b89b */
        g_pZoneConnHead = pConn->pPrev;                  /* @0x42b8a1 */
    }
    if ((ZoneConn *)g_pZoneConnTail == pConn) {          /* @0x42b8a5 */
        g_pZoneConnTail = pConn->pNext;                  /* @0x42b8af */
    }
    if (pConn->pPrev != NULL) {                          /* @0x42b8b4 */
        pConn->pPrev->pNext = pConn->pNext;              /* @0x42b8be */
    }
    if (pConn->pNext != NULL) {                          /* @0x42b8c2 */
        pConn->pNext->pPrev = pConn->pPrev;              /* @0x42b8c8 */
    }
}

/* zoneWallListFree @0x42a150 — free the whole AiNavNode list (see zone.h).
 * Per node the walk-edge list (+0x3c) is released first, then the
 * cross-mesh connection list (+0x38), then the node itself; iteration
 * follows pNext (+0x40) and the head g_pNavNodeList @0x45e5e0 ends NULL
 * (both the empty-list and post-loop paths store 0). */
void zoneWallListFree(void) /* @0x42a150 */
{
    AiNavNode *pNode = g_pNavNodeList;                   /* @0x42a151 */
    AiNavNode *pNext;
    AiNavEdge *pEdge;
    AiNavEdge *pNextEdge;

    if (pNode == NULL) {                                 /* @0x42a159 */
        g_pNavNodeList = NULL;                           /* @0x42a1a9 */
        return;
    }
    for (; pNode != NULL; pNode = pNext) {               /* @0x42a15d */
        pNext = pNode->pNext;                            /* +0x40 @0x42a160 */
        pEdge = pNode->pEdgeList;                        /* +0x3c @0x42a15d */
        for (; pEdge != NULL; pEdge = pNextEdge) {       /* @0x42a167 next saved before free */
            pNextEdge = pEdge->pNext;                    /* +0x00 @0x42a167 MOV ESI,[EAX] */
            memFreeDirect(pEdge);                        /* @0x43dd37 @0x42a16a */
        }
        pEdge = pNode->pConnList;                        /* +0x38 @0x42a178 */
        for (; pEdge != NULL; pEdge = pNextEdge) {       /* @0x42a17f next saved before free */
            pNextEdge = pEdge->pNext;                    /* +0x00 @0x42a17f MOV ESI,[EAX] */
            memFreeDirect(pEdge);                        /* @0x43dd37 @0x42a182 */
        }
        memFreeDirect(pNode);                            /* @0x43dd37 @0x42a191 */
    }
    g_pNavNodeList = NULL;                               /* @0x42a19f */
}

/* --- zoneAvoidWalls @0x4023e0 — camera wall-avoidance push (verified
 * 2026-08-30 against the disassembly). --- */
static const float g_flZero = 0.0f;           /* @0x44b244 (bytes 00 00 00 00) */
static const float g_flZoneMinusOne = -1.0f;  /* @0x44b25c (bytes 00 00 80 BF) */
static const float g_flZoneHalf = 0.5f;       /* @0x44b274 (bytes 00 00 00 3F) */
static const double g_dblZoneMinusOne = -1.0; /* @0x44b280 (loop-2 sentinel compare) */
static const double g_dblZonePushPad = 100.0; /* @0x44b2b8 (bytes 00 00 59 40) */
static const float g_flZoneBand = 4000.0f;    /* @0x44b2c0 (bytes 00 00 7A 45) */

/* zoneAvoidWalls @0x4023e0 — push the camera point out of nearby zone walls.
 * For every nav mesh, walks the walk-edge list (pEdgeList) and the
 * cross-mesh connection list (pConnList). Edge gates:
 *   walk edges  — the average plane height of the edge endpoints must lie
 *                 in [flRadius, flRadius + 4000.0); the average is
 *                 ((V0X-nRefX)*slopeX + (V1Z-nRefZ)*slopeZ +
 *                  (V1X-nRefX)*slopeX + (V0Z-nRefZ)*slopeZ + nRefY*2)*0.5;
 *   conn edges  — same own average >= flRadius and the peer mesh's
 *                 average <= flRadius (same formula over the peer plane).
 * Both then require (pPoint - pRef) . unit(Edge) >= 0 (moving toward the
 * wall) and a bounded segment intersection of the pPoint->pRef segment
 * with the edge, tested in both argument orders. Each hit is polarized
 * (mathVec2Polar of hit - pPoint); the shortest length wins
 * (vBest = {len, angle}, -1.0 sentinel), except hits inside the camera
 * blocker zone (objContainsPoint on OBJ_ID_C_NC, 'c_nc' @0x44e214) which
 * are skipped. On a
 * hit, pPoint += gxVec2FromPolar({len + 100.0, angle}) and 1 is returned;
 * 0 = no wall contact. Used by cameraFollowUpdate (both wall-avoid
 * passes). */
int zoneAvoidWalls(GxVec2 *pPoint, GxVec2 *pRef, float flRadius) /* @0x4023e0 */
{
    GxVec2 vBest = { -1.0f, -1.0f };   /* {len, angle}, -1.0 sentinel */
    GxVec2 vA;
    GxVec2 vB;
    GxVec2 vOut;
    GxVec2 vDelta;
    GxVec2 vPush;
    GxVec2 vPushSum;
    EventObject *pCamObj;
    AiNavNode *pMesh;
    AiNavEdge *pEdge;

    pCamObj = objFindById(OBJ_ID_C_NC, 0);                       /* @0x402415 c_nc @0x44e214 */
    for (pMesh = g_pNavNodeList; pMesh != NULL; pMesh = pMesh->pNext) {
        for (pEdge = pMesh->pEdgeList; pEdge != NULL; pEdge = pEdge->pNext) { /* @0x402449 */
            float flAvg = ((pEdge->flV0X - pMesh->nRefX) * pMesh->flSlopeX +
                           (pEdge->flV1Z - pMesh->nRefZ) * pMesh->flSlopeZ +
                           (pEdge->flV1X - pMesh->nRefX) * pMesh->flSlopeX +
                           (pEdge->flV0Z - pMesh->nRefZ) * pMesh->flSlopeZ +
                           pMesh->nRefY + pMesh->nRefY) * g_flZoneHalf; /* @0x402454 */
            if (flAvg < flRadius || flRadius + g_flZoneBand < flAvg) { /* @0x40249f..0x4024c9: FCOMPP skips only on strict greater (equal passes) */
                continue;
            }
            if ((pPoint->y - pRef->y) * pEdge->flUnitNegZ +
                (pPoint->x - pRef->x) * pEdge->flUnitX < g_flZero) { /* @0x4024cf */
                continue;                                        /* @0x4024ec */
            }
            gxVec2Set(&vA, pEdge->flV0Z, pEdge->flV0X);          /* @0x4024fa */
            gxVec2Set(&vB, pEdge->flV1Z, pEdge->flV1X);          /* @0x40250b */
            gxVec2SetAngleZero(&vOut);                           /* @0x402514 */
            if (mathSegIntersectBounded(pPoint->x, pPoint->y, pRef->x, pRef->y,
                                        vA.x, vA.y, vB.x, vB.y,
                                        &vOut.x) == 0) {         /* @0x402540 */
                continue;                                        /* @0x40254a */
            }
            if (mathSegIntersectBounded(vA.x, vA.y, vB.x, vB.y,
                                        pPoint->x, pPoint->y, pRef->x, pRef->y,
                                        &vOut.x) == 0) {         /* @0x402577 */
                continue;
            }
            gxVec2Set(&vDelta, vOut.x - pPoint->x, vOut.y - pPoint->y); /* @0x402599 */
            mathVec2Polar(&vPush, &vDelta);                      /* @0x4025b0 */
            if (vBest.x != g_flZoneMinusOne && vBest.x <= vPush.x) { /* @0x4025b9..0x4025db */
                continue;                                        /* @0x4025e0 */
            }
            if (pCamObj != NULL &&                               /* @0x4025e2 */
                objContainsPoint(pCamObj, vOut.x, vOut.y) != 0) {
                continue;                                        /* @0x4025fb */
            }
            vBest.x = vPush.x;                                   /* @0x4025fd */
            vBest.y = vPush.y;                                   /* @0x402601 */
        }
        for (pEdge = pMesh->pConnList; pEdge != NULL; pEdge = pEdge->pNext) { /* @0x40261b */
            float flAvg;
            AiNavNode *pPeer = pEdge->pPeerMesh;
            if (pPeer == NULL) {
                continue;
            }
            flAvg = ((pEdge->flV0X - pMesh->nRefX) * pMesh->flSlopeX +
                     (pEdge->flV1Z - pMesh->nRefZ) * pMesh->flSlopeZ +
                     (pEdge->flV1X - pMesh->nRefX) * pMesh->flSlopeX +
                     (pEdge->flV0Z - pMesh->nRefZ) * pMesh->flSlopeZ +
                     pMesh->nRefY + pMesh->nRefY) * g_flZoneHalf; /* @0x402626 */
            if (flAvg < flRadius) {
                continue;                                        /* @0x40266f */
            }
            if (pPeer->nRefY * 2 +
                (pEdge->flV1Z - pPeer->nRefZ) * pPeer->flSlopeZ +
                (pEdge->flV0X - pPeer->nRefX) * pPeer->flSlopeX +
                (pEdge->flV0Z - pPeer->nRefZ) * pPeer->flSlopeZ +
                (pEdge->flV1X - pPeer->nRefX) * pPeer->flSlopeX >
                flRadius * 2) {
                continue;                                        /* @0x4026cc */
            }
            if ((pPoint->y - pRef->y) * pEdge->flUnitNegZ +
                (pPoint->x - pRef->x) * pEdge->flUnitX < g_flZero) { /* @0x4026d2 */
                continue;                                        /* @0x4026ef */
            }
            gxVec2Set(&vA, pEdge->flV0Z, pEdge->flV0X);          /* @0x4026fd */
            gxVec2Set(&vB, pEdge->flV1Z, pEdge->flV1X);
            gxVec2SetAngleZero(&vOut);                           /* @0x402737 */
            if (mathSegIntersectBounded(pPoint->x, pPoint->y, pRef->x, pRef->y,
                                        vA.x, vA.y, vB.x, vB.y,
                                        &vOut.x) == 0) {         /* @0x40275f */
                continue;
            }
            if (mathSegIntersectBounded(vA.x, vA.y, vB.x, vB.y,
                                        pPoint->x, pPoint->y, pRef->x, pRef->y,
                                        &vOut.x) == 0) {         /* @0x402792 */
                continue;
            }
            gxVec2Set(&vDelta, vOut.x - pPoint->x, vOut.y - pPoint->y); /* @0x40279e */
            mathVec2Polar(&vPush, &vDelta);                      /* @0x4027c1 */
            if ((double)vBest.x != g_dblZoneMinusOne &&          /* @0x4027ca */
                vBest.x <= vPush.x) {
                continue;                                        /* @0x4027f1 */
            }
            if (pCamObj != NULL &&                               /* @0x4027f3 */
                objContainsPoint(pCamObj, vOut.x, vOut.y) != 0) {
                continue;                                        /* @0x40280c */
            }
            vBest.x = vPush.x;                                   /* @0x40280e */
            vBest.y = vPush.y;                                   /* @0x402812 */
        }
    }
    if ((double)vBest.x == g_dblZoneMinusOne) {                  /* @0x402839 */
        return 0;                                                /* @0x40288e */
    }
    vBest.x += g_dblZonePushPad;                                 /* @0x40284e */
    gxVec2FromPolar(&vPush, &vBest);                             /* @0x402861 */
    gxVec2Add(&vPushSum, pPoint, &vPush);                        /* @0x40286d */
    pPoint->x = vPushSum.x;                                      /* @0x402877 */
    pPoint->y = vPushSum.y;                                      /* @0x40287c */
    return 1;                                                    /* @0x40287c */
}
