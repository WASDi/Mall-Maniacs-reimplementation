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
        SceneObjTypeDef *pMesh = (SceneObjTypeDef *)(size_t)sceneNodeGetMesh(pNode);
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
