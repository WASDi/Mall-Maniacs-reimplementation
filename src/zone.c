#include <windows.h>
#include <stdlib.h>
#include "zone.h"
#include "scene.h"
#include "pool.h"
#include "stubs.h"
#include "util.h"

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
