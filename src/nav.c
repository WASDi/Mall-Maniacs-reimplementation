#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "nav.h"
#include "config.h"
#include "gx.h"
#include "util.h"
#include "pool.h"
#include "stubs.h"
#include "time.h"

/* =====================================================================
 * nav.c — AI nav-mesh + navpoint ("NavBuoy") subsystem.
 * Reimplemented from the verified Ghidra decompilations and disassembly
 * of aiNavEdgeCtor/aiNavNodeCtor/aiNavNodeCtorScene/aiNavNodeUpdate/
 * aiNavNodeAddEdge @0x428c70..0x429b8f, the navPoint cluster
 * @0x425760..0x425c50 and nloadCmd @0x407e10.
 * ===================================================================== */

SceneObjRenderInfo *g_pNavMeshData;   /* @0x45e5d4 */
byte *g_pNavTriCur;                   /* @0x45e5d0 */
byte *g_pNavTriEnd;                   /* @0x45e5d8 */
int   g_nNavTriIdx;                   /* @0x45e5cc */

NavPoint *g_pNavPointHead;            /* @0x45d4d0 */
NavPoint *g_pNavPointTail;            /* @0x45d4d4 */
int   g_nNavPointCount;               /* @0x45d4d8 */
NavPoint *g_pNavPointSel;             /* @0x45e480 */
void *g_pEditorHover;                 /* @0x45e498 editor hover selection (cleared by roundTeardown) */
void *g_pEditorDrag;                  /* @0x45e49c editor drag selection (cleared by roundTeardown) */
extern int g_nNavPtSearchCount;       /* @0x45d4dc via player_ai */

static const float g_flHalfPi = 1.5707964f;  /* @0x44b270 (bytes DB 0F C9 3F) */

/* aiNavEdgeCtor @0x428c70 — zero the link fields, store the six raw
 * endpoint coords (+0x24..+0x38). The derived float block +0x0c..+0x20
 * is filled by aiNavNodeAddEdge / zoneConnLink after linking. */
void aiNavEdgeCtor(AiNavEdge *pEdge, int nV0x, int nV0y, int nV0z,
                   int nV1x, int nV1y, int nV1z) /* @0x428c70 */
{
    pEdge->pNext = NULL;                              /* +0x00 */
    pEdge->pPrev = NULL;                              /* +0x04 */
    pEdge->pPeerMesh = NULL;                          /* +0x08 */
    pEdge->nV0x = nV0x;
    pEdge->nV0y = nV0y;
    pEdge->nV0z = nV0z;
    pEdge->nV1x = nV1x;
    pEdge->nV1y = nV1y;
    pEdge->nV1z = nV1z;
}

/* aiNavNodeCtor @0x428cb0 — zero the link fields, sceneNodeGetPos(node,0,
 * &pos,4) -> +0x2a.., flAvgY = (float)nPosY, then aiNavNodeUpdate
 * builds the walk edges against the current nav mesh. */
AiNavNode *aiNavNodeCtor(AiNavNode *pNode, SceneNode *pSceneObj) /* @0x428cb0 */
{
    int anPos[3];

    pNode->pEdgeList = NULL;                          /* +0x3c */
    pNode->pConnList = NULL;                          /* +0x38 */
    pNode->pNext = NULL;                              /* +0x40 */
    pNode->pPrev = NULL;                              /* +0x44 */
    sceneNodeGetPos(pSceneObj, 0, anPos, 4);          /* @0x431270 */
    pNode->nPosX = anPos[0];                          /* +0x2a */
    pNode->nPosY = anPos[1];                          /* +0x2e */
    pNode->nPosZ = anPos[2];                          /* +0x32 */
    pNode->flAvgY = (float)pNode->nPosY;
    aiNavNodeUpdate(pNode, pSceneObj);                /* @0x428d50 */
    return pNode;
}

/* aiNavNodeCtorScene @0x428cf0 — aiNavNodeCtor plus the nav-mesh scan
 * setup: g_pNavMeshData = sceneNodeGetMesh(node)->pRender (+0x14) and
 * the fan cursor/prim index reset (g_pNavTriEnd is initialized lazily
 * on the first prim). Called per "FLOOR" scene object by
 * levelSceneTexturesLoad @0x410857. */
void aiNavNodeCtorScene(AiNavNode *pNode, SceneNode *pSceneObj) /* @0x428cf0 */
{
    SceneObjTypeDef *pMesh;
    int anPos[3];

    pNode->pEdgeList = NULL;
    pNode->pConnList = NULL;
    pNode->pNext = NULL;
    pNode->pPrev = NULL;
    sceneNodeGetPos(pSceneObj, 0, anPos, 4);          /* @0x431270 */
    pNode->nPosX = anPos[0];
    pNode->nPosY = anPos[1];
    pNode->nPosZ = anPos[2];
    pNode->flAvgY = (float)pNode->nPosY;
    pMesh = sceneNodeGetMesh(pSceneObj);              /* @0x431ae0 */
    g_pNavMeshData = pMesh->pRender;
    g_nNavTriIdx = 0;
    g_pNavTriCur = NULL;
    aiNavNodeUpdate(pNode, pSceneObj);                /* @0x428d50 */
}

/* aiNavNodeUpdate @0x428d50 — build this node's walk edges by scanning
 * g_pNavMeshData (SceneObjRenderInfo): loops g_nNavTriIdx over nPolyA
 * entries of pPolyA, walking each prim's fan stream with g_pNavTriCur/
 * g_pNavTriEnd (bFanIdxCount*2 bytes per fan; bType 3 = tri fans, 4 =
 * quad lists). Fan vertex indices (low bytes) index pVerts (short[4]
 * stride 8) relative to the node position (+0x2a..); the fan normal is
 * cross((v1-v0),(v2-v0)) normalized and epsilon-zeroed. Fans with
 * normal.Y >= 0.6 (@0x44b710) are walkable:
 *  - node has no edges yet: the fan defines the node normal (+0x18..)
 *    and its 3 (tri) / 4 (quad) edges go to this node;
 *  - node normal equals the fan normal exactly: edges to this node;
 *  - otherwise an existing g_pNavNodeList entry whose normal matches
 *    exactly AND whose first edge V0 re-derives the plane normal
 *    (cross of (v1-v0) with (edgeV0 - v0), |diff| <= 0.2 @0x44b708)
 *    absorbs the edges;
 *  - no match: this node links into g_pNavNodeList and a fresh
 *    aiNavNodeCtor(pSceneObj) node resumes the same mesh at the current
 *    cursor (returns).
 * When all prims are consumed the node links into g_pNavNodeList. */
void aiNavNodeUpdate(AiNavNode *pNode, SceneNode *pSceneObj) /* @0x428d50 */
{
    while (g_nNavTriIdx < g_pNavMeshData->nPolyA) {
        SceneMeshPrim *pPrim = g_pNavMeshData->pPolyA[g_nNavTriIdx];
        int bQuad;
        byte *pCur;
        byte *pEnd;

        if (g_pNavTriCur == NULL) {                   /* @0x428d98 */
            g_pNavTriCur = pPrim->abFans;             /* prim + 8 */
            g_pNavTriEnd = pPrim->abFans +
                (int)pPrim->bFans * (int)pPrim->bFanIdxCount * 2;
        }
        pCur = g_pNavTriCur;
        pEnd = g_pNavTriEnd;
        if (pPrim->bType != 3 && pPrim->bType != 4) { /* @0x428dc1 */
            g_nNavTriIdx++;                           /* @0x429aba next prim */
            g_pNavTriCur = NULL;
            continue;
        }
        if (pCur >= pEnd) {
            g_nNavTriIdx++;
            g_pNavTriCur = NULL;
            continue;
        }
        bQuad = (pPrim->bType == 4);

        while (pCur < pEnd) {                         /* @0x428ddc/@0x4294a5 per fan */
            const short *pVerts = (const short *)g_pNavMeshData->pVerts;
            float v0x = (float)(int)pVerts[pCur[0] * 4 + 0] + (float)pNode->nPosX;
            float v0y = (float)(int)pVerts[pCur[0] * 4 + 1] + (float)pNode->nPosY;
            float v0z = (float)(int)pVerts[pCur[0] * 4 + 2] + (float)pNode->nPosZ;
            float v1x = (float)(int)pVerts[pCur[1] * 4 + 0] + (float)pNode->nPosX;
            float v1y = (float)(int)pVerts[pCur[1] * 4 + 1] + (float)pNode->nPosY;
            float v1z = (float)(int)pVerts[pCur[1] * 4 + 2] + (float)pNode->nPosZ;
            float v2x = (float)(int)pVerts[pCur[2] * 4 + 0] + (float)pNode->nPosX;
            float v2y = (float)(int)pVerts[pCur[2] * 4 + 1] + (float)pNode->nPosY;
            float v2z = (float)(int)pVerts[pCur[2] * 4 + 2] + (float)pNode->nPosZ;
            float v3x = 0.0f, v3y = 0.0f, v3z = 0.0f;
            /* Nx/Ny are rounded to float; Nz is kept unrounded (x87 ST0).
             * volatile forces the rounded values to memory so the
             * exact-match and the candidate guard below compare the very
             * same float bits (without it GCC keeps the x87 register
             * value for one site and the rounded spill for the other,
             * which makes the guard pass where the exact-match failed). */
            volatile float flNy, flNx;
            volatile double flNz;                     /* normalized Nz, unrounded (x87 ST0) */
            float A = v2z - v0z;
            float B = v0x - v1x;
            float C = v0z - v1z;
            float D = v2x - v0x;
            float E = v2y - v0y;
            float F = v0y - v1y;
            float flLen;

            if (bQuad) {                              /* cur[3] = 4th fan vertex */
                v3x = (float)(int)pVerts[pCur[3] * 4 + 0] + (float)pNode->nPosX;
                v3y = (float)(int)pVerts[pCur[3] * 4 + 1] + (float)pNode->nPosY;
                v3z = (float)(int)pVerts[pCur[3] * 4 + 2] + (float)pNode->nPosZ;
            }

            /* Fan normal = cross((v1-v0),(v2-v0)) normalized and
             * epsilon-zeroed (x87 order verified: Ny = B*A - D*C,
             * Nx = E*C - F*A, Nz = F*D - E*B, len = sqrt(Nz^2+Nx^2+Ny^2),
             * |c| <= 1e-5 zeroes @0x44b718/0x44b71c).
             * The original stores Nx/Ny rounded to float but keeps the
             * normalized Nz in the x87 stack (ST0) and compares it against
             * the float-rounded stored node normal (@0x429065/@0x4291bd).
             * That asymmetry makes the exact-match fail for any fan whose
             * normal has a nonzero Z component, so sloped fans never merge
             * or absorb (each fan becomes its own node); only flat
             * (Nz == 0) coplanar fans merge. */
            {
                float flNyRaw = B * A - D * C;
                float flNxRaw = E * C - F * A;
                float flNzRaw = F * D - E * B;
                flLen = (float)sqrt((double)(flNzRaw * flNzRaw +
                                             flNxRaw * flNxRaw +
                                             flNyRaw * flNyRaw));
                flNx = flNxRaw / flLen;
                flNy = flNyRaw / flLen;
                flNz = (double)flNzRaw / (double)flLen; /* unrounded */
            }
            if (flNx <= 1.0e-05f && flNx >= -1.0e-05f) {
                flNx = 0.0f;
            }
            if (flNy <= 1.0e-05f && flNy >= -1.0e-05f) {
                flNy = 0.0f;
            }
            if (flNz <= 1.0e-05 && flNz >= -1.0e-05) {
                flNz = 0.0;
            }
            if ((double)flNy >= 0.6) {                /* walkable @0x429029/@0x4296ad */
                if (pNode->pEdgeList == NULL) {       /* first fan: define normal */
                    pNode->flNormalX = flNx;          /* +0x18 @0x4299be/@0x42936e */
                    pNode->flNormalY = flNy;          /* +0x1c */
                    pNode->flNormalZ = (float)flNz;   /* +0x20, rounded */
                    aiNavNodeAddEdge(pNode, (int)v0x, (int)v0y, (int)v0z,
                                     (int)v1x, (int)v1y, (int)v1z);
                    aiNavNodeAddEdge(pNode, (int)v1x, (int)v1y, (int)v1z,
                                     (int)v2x, (int)v2y, (int)v2z);
                    if (bQuad) {
                        aiNavNodeAddEdge(pNode, (int)v2x, (int)v2y, (int)v2z,
                                         (int)v3x, (int)v3y, (int)v3z);
                        aiNavNodeAddEdge(pNode, (int)v3x, (int)v3y, (int)v3z,
                                         (int)v0x, (int)v0y, (int)v0z);
                    } else {
                        aiNavNodeAddEdge(pNode, (int)v2x, (int)v2y, (int)v2z,
                                         (int)v0x, (int)v0y, (int)v0z);
                    }
                } else if (pNode->flNormalX == flNx &&          /* exact match @0x4296cd */
                           pNode->flNormalY == flNy &&
                           (double)pNode->flNormalZ == flNz) {
                    aiNavNodeAddEdge(pNode, (int)v0x, (int)v0y, (int)v0z,
                                     (int)v1x, (int)v1y, (int)v1z);
                    aiNavNodeAddEdge(pNode, (int)v1x, (int)v1y, (int)v1z,
                                     (int)v2x, (int)v2y, (int)v2z);
                    if (bQuad) {
                        aiNavNodeAddEdge(pNode, (int)v2x, (int)v2y, (int)v2z,
                                         (int)v3x, (int)v3y, (int)v3z);
                        aiNavNodeAddEdge(pNode, (int)v3x, (int)v3y, (int)v3z,
                                         (int)v0x, (int)v0y, (int)v0z);
                    } else {
                        aiNavNodeAddEdge(pNode, (int)v2x, (int)v2y, (int)v2z,
                                         (int)v0x, (int)v0y, (int)v0z);
                    }
                } else {
                    /* Search g_pNavNodeList for a candidate whose normal
                     * equals the fan normal exactly and whose first edge
                     * V0 re-derives the plane normal within +-0.2. */
                    AiNavNode *pCand = g_pNavNodeList;
                    AiNavNode *pMatch = NULL;

                    while (pCand != NULL) {           /* @0x42907d/@0x42983x */
                        if (pCand->pEdgeList != NULL) {
                            AiNavEdge *pEdge = pCand->pEdgeList;
                            float Dx = (float)pEdge->nV0x - v0x;
                            float Dy = (float)pEdge->nV0y - v0y;
                            float Dz = (float)pEdge->nV0z - v0z;
                            float B = v0x - v1x;
                            float C = v0z - v1z;
                            float F = v0y - v1y;
                            float flNy2 = Dz * B - Dx * C;
                            float flNx2 = Dy * C - Dz * F;
                            float flNz2 = Dx * F - Dy * B;
                            float flLen = (float)sqrt((double)(flNz2 * flNz2 +
                                                     flNx2 * flNx2 + flNy2 * flNy2));

                            flNx2 /= flLen;
                            flNy2 /= flLen;
                            flNz2 /= flLen;
                            if (flNx2 <= 1.0e-05f && flNx2 >= -1.0e-05f) {
                                flNx2 = 0.0f;
                            }
                            if (flNy2 <= 1.0e-05f && flNy2 >= -1.0e-05f) {
                                flNy2 = 0.0f;
                            }
                            if (flNz2 <= 1.0e-05f && flNz2 >= -1.0e-05f) {
                                flNz2 = 0.0f;
                            }
                            if (pNode->flNormalX == flNx &&
                                pNode->flNormalY == flNy &&
                                (double)pNode->flNormalZ == flNz &&
                                (double)flNx + 0.2 >= (double)flNx2 &&   /* @0x44b708 */
                                (double)flNx - 0.2 <= (double)flNx2 &&
                                (double)flNy + 0.2 >= (double)flNy2 &&
                                (double)flNy - 0.2 <= (double)flNy2 &&
                                (double)flNz + 0.2 >= (double)flNz2 &&
                                (double)flNz - 0.2 <= (double)flNz2) {
                                pMatch = pCand;       /* @0x429261/@0x4298e5 */
                                break;
                            }
                        }
                        pCand = pCand->pNext;         /* +0x40 */
                    }
                    if (pMatch != NULL) {
                        aiNavNodeAddEdge(pMatch, (int)v0x, (int)v0y, (int)v0z,
                                         (int)v1x, (int)v1y, (int)v1z);
                        aiNavNodeAddEdge(pMatch, (int)v1x, (int)v1y, (int)v1z,
                                         (int)v2x, (int)v2y, (int)v2z);
                        if (bQuad) {
                            aiNavNodeAddEdge(pMatch, (int)v2x, (int)v2y, (int)v2z,
                                             (int)v3x, (int)v3y, (int)v3z);
                            aiNavNodeAddEdge(pMatch, (int)v3x, (int)v3y, (int)v3z,
                                             (int)v0x, (int)v0y, (int)v0z);
                        } else {
                            aiNavNodeAddEdge(pMatch, (int)v2x, (int)v2y, (int)v2z,
                                             (int)v0x, (int)v0y, (int)v0z);
                        }
                    } else {
                        /* No match: link this node into g_pNavNodeList and
                         * continue the scan with a fresh node. */
                        pNode->pNext = g_pNavNodeList;      /* @0x429ae1/@0x429b22 */
                        if (g_pNavNodeList != NULL) {
                            g_pNavNodeList->pPrev = pNode;
                        }
                        g_pNavNodeList = pNode;
                        {
                            AiNavNode *pNext = (AiNavNode *)malloc(sizeof(AiNavNode));
                            if (pNext != NULL) {            /* operator_new @0x43dd42 */
                                aiNavNodeCtor(pNext, pSceneObj);  /* @0x428cb0 */
                            }
                        }
                        return;
                    }
                }
            }
            pCur += (int)pPrim->bFanIdxCount * 2;     /* @0x429a9f/@0x42947d */
            g_pNavTriCur = pCur;
        }
        g_nNavTriIdx++;                               /* @0x429aba prim done */
        g_pNavTriCur = NULL;
    }
    pNode->pNext = g_pNavNodeList;                    /* @0x429b63 final link */
    if (g_pNavNodeList != NULL) {
        g_pNavNodeList->pPrev = pNode;
    }
    g_pNavNodeList = pNode;
}

/* aiNavNodeAddEdge @0x429ba0 — allocate a 0x3c AiNavEdge (aiNavEdgeCtor
 * with the 6 raw coords), head-insert into pEdgeList (+0x3c), derive the
 * float block: flV0Z/flV0X/flV1Z/flV1X from the coords and (+0x1c/+0x20)
 * the unit direction of V0->V1 in (Z,X) rotated -90 degrees (polar
 * angle -= pi/2, polar length forced to 1.0). */
void aiNavNodeAddEdge(AiNavNode *pNode, int nV0x, int nV0y, int nV0z,
                      int nV1x, int nV1y, int nV1z) /* @0x429ba0 */
{
    AiNavEdge *pEdge = (AiNavEdge *)malloc(sizeof(AiNavEdge)); /* operator_new @0x43dd42 */
    GxVec2 vDir;
    GxVec2 vPolar;
    GxVec2 vUnit;

    if (pEdge != NULL) {
        aiNavEdgeCtor(pEdge, nV0x, nV0y, nV0z, nV1x, nV1y, nV1z); /* @0x428c70 */
    }
    pEdge->pNext = pNode->pEdgeList;
    if (pNode->pEdgeList != NULL) {
        pNode->pEdgeList->pPrev = pEdge;
    }
    pNode->pEdgeList = pEdge;
    pEdge->flV0Z = (float)pEdge->nV0z;                /* +0x0c */
    pEdge->flV0X = (float)pEdge->nV0x;                /* +0x10 */
    pEdge->flV1Z = (float)pEdge->nV1z;                /* +0x14 */
    pEdge->flV1X = (float)pEdge->nV1x;                /* +0x18 */
    gxVec2Set(&vDir, pEdge->flV1Z - pEdge->flV0Z,
              pEdge->flV1X - pEdge->flV0X);           /* @0x434fa0 */
    mathVec2Polar(&vPolar, &vDir);                    /* @0x435060 */
    vPolar.y -= g_flHalfPi;
    vPolar.x = 1.0f;
    gxVec2FromPolar(&vUnit, &vPolar);                 /* @0x434fc0 */
    pEdge->flUnitX = vUnit.x;                         /* +0x1c */
    pEdge->flUnitNegZ = vUnit.y;                      /* +0x20 */
}

/* --- NavBuoy points (nloadCmd) --- */

/* navPointCtor @0x425760 — nId = -1, 8 links zeroed, link count 0, pos 0,
 * flags 0, pNext NULL, flUnknown = -1.0f. */
NavPoint *navPointCtor(NavPoint *pPoint) /* @0x425760 */
{
    int i;

    pPoint->nId = -1;
    for (i = 0; i < NAVPOINT_LINKS; i++) {
        pPoint->pALink[i] = NULL;
    }
    pPoint->nLinkCount = 0;
    pPoint->nPosX = 0;
    pPoint->nPosY = 0;
    pPoint->nPosZ = 0;
    pPoint->nFlags = 0;
    pPoint->pNext = NULL;
    pPoint->flUnknown = -1.0f;
    return pPoint;
}

/* navPointListFreeAll @0x4257a0 — recursive pNext teardown (the head
 * itself is freed by the caller). Clears g_pNavPointHead/Tail when they
 * still point at pHead. */
void navPointListFreeAll(NavPoint *pHead) /* @0x4257a0 */
{
    NavPoint *pNext;

    if (g_pNavPointHead == pHead) {
        g_pNavPointHead = NULL;
    }
    if (g_pNavPointTail == pHead) {
        g_pNavPointTail = NULL;
    }
    pNext = pHead->pNext;
    if (pNext != NULL) {
        navPointListFreeAll(pNext);
        free(pNext);                                  /* memFreeDirect @0x43dd37 */
    }
}

/* navPointAddLink @0x4257f0 — append pLink (max 8 links) and store the
 * 2D XZ distance (polar length of (dZ, dX)) in pALinkDist. Returns 0,
 * -1 when the link array is full. */
int navPointAddLink(NavPoint *pPoint, NavPoint *pLink) /* @0x4257f0 */
{
    GxVec2 vDelta;
    GxVec2 vPolar;

    gxVec2SetAngleZero(&vDelta);                      /* @0x434f90 (dead store, kept for call fidelity) */
    gxVec2SetAngleZero(&vPolar);
    if (pPoint->nLinkCount > 7) {
        return -1;
    }
    pPoint->pALink[pPoint->nLinkCount] = pLink;
    vDelta.x = (float)(pLink->nPosZ - pPoint->nPosZ);
    vDelta.y = (float)(pLink->nPosX - pPoint->nPosX);
    mathVec2Polar(&vPolar, &vDelta);                  /* @0x435060 */
    pPoint->pALinkDist[pPoint->nLinkCount] = vPolar.x;
    pPoint->nLinkCount++;
    return 0;
}

/* navPointListAdd @0x4258e0 — append to the g_pNavPointHead/Tail list
 * (pNext chain), g_nNavPointCount++. Returns 0, -1 on NULL. */
int navPointListAdd(NavPoint *pPoint) /* @0x4258e0 */
{
    if (pPoint == NULL) {
        return -1;
    }
    if (g_pNavPointHead == NULL) {
        g_pNavPointHead = pPoint;
    }
    if (g_pNavPointTail != NULL) {
        g_pNavPointTail->pNext = pPoint;
    }
    g_pNavPointTail = pPoint;
    g_nNavPointCount++;
    return 0;
}

/* navPointListGetHead @0x425ad0 — list head accessor. */
NavPoint *navPointListGetHead(void) /* @0x425ad0 */
{
    return g_pNavPointHead;
}

/* navPointFindById @0x425c50 — linear scan of the navpoint list. */
NavPoint *navPointFindById(int nId) /* @0x425c50 */
{
    NavPoint *pPoint;

    for (pPoint = g_pNavPointHead; pPoint != NULL; pPoint = pPoint->pNext) {
        if (pPoint->nId == nId) {
            return pPoint;
        }
    }
    return NULL;
}

/* navPointGetNext @0x425ae0 — return pPoint->pNext (+0x5c). */
NavPoint *navPointGetNext(NavPoint *pPoint) /* @0x425ae0 */
{
    return pPoint->pNext;
}

/* navPointGetLinkList @0x4257e0 — write link-array start (this+4) to
 * *pOutList and return link count at +0x48. Uses struct field accessor
 * pPoint->pALink (array decays to NavPoint**). */
int navPointGetLinkList(NavPoint *pPoint, NavPoint ***pOutList) /* @0x4257e0 */
{
    *pOutList = pPoint->pALink;                              /* +0x04 @0x4257e4 pALink field */
    return pPoint->nLinkCount;                               /* +0x48 @0x4257e9 */
}

/* navPointRemoveLink @0x425880 — remove link to pTarget from pPoint's
 * array (pALink at +0x04, pALinkDist at +0x24, count at +0x48), shifting
 * remaining entries down. Returns 1 if removed, 0 if not found. */
int navPointRemoveLink(NavPoint *pPoint, NavPoint *pTarget) /* @0x425880 */
{
    int i;
    int nCount = pPoint->nLinkCount;

    for (i = 0; i < nCount; i++) {
        if (pPoint->pALink[i] == pTarget) {
            int j;
            for (j = i; j < nCount - 1; j++) {
                pPoint->pALink[j] = pPoint->pALink[j + 1];
                pPoint->pALinkDist[j] = pPoint->pALinkDist[j + 1];
            }
            pPoint->nLinkCount--;
            return 1;
        }
    }
    return 0;
}

/* navPointRemove @0x425920 — unlink pPoint from g_pNavPointHead/Tail
 * list (+0x5c), remove all links referencing it from every nav point,
 * free via navPointListFreeAll/memFreeDirect, decrement count. Returns
 * 0, -1 on NULL. */
int navPointRemove(NavPoint *pPoint) /* @0x425920 */
{
    NavPoint *pPrev = NULL;
    NavPoint *pCur;

    if (pPoint == NULL) {
        return -1;
    }
    for (pCur = g_pNavPointHead; pCur != NULL; pCur = pCur->pNext) {
        if (pCur == pPoint) {
            if (pPrev != NULL) {
                pPrev->pNext = pCur->pNext;
            }
            if (g_pNavPointHead == pCur) {
                g_pNavPointHead = pCur->pNext;
            }
            if (g_pNavPointTail == pCur) {
                g_pNavPointTail = pPrev;
            }
            pCur->pNext = NULL;
            break;
        }
        pPrev = pCur;
    }
    for (pCur = navPointListGetHead(); pCur != NULL; pCur = navPointGetNext(pCur)) {
        while (navPointRemoveLink(pCur, pPoint) != 0) {
        }
    }
    navPointListFreeAll(pPoint);
    free(pPoint);
    g_nNavPointCount--;
    return 0;
}

/* navPointResetAllFlags @0x425c70 — for every NavPoint set +0x58 flags=0
 * and +0x44 flUnknown=-1.0f (graph-search visited/cost reset). */
void navPointResetAllFlags(void) /* @0x425c70 */
{
    NavPoint *pCur;

    for (pCur = g_pNavPointHead; pCur != NULL; pCur = pCur->pNext) {
        pCur->nFlags = 0;                                /* +0x58 @0x425c80 */
        pCur->flUnknown = -1.0f;                         /* +0x44 @0x425c83 0xbf800000 */
    }
}

/* navPointRelaxCosts @0x425af0 — Dijkstra-style recursive cost relaxation.
 * Visited-marker is (float)nFlags at +0x58 (0 = unvisited), best-cost is
 * flUnknown at +0x44. If already visited with cheaper cost (float)nFlags <
 * flCost && nFlags != 0, return -1.0. Otherwise store flCost to nFlags,
 * then: if pPoint==pTarget optionally lower flUnknown and return flCost;
 * else recurse over links with acc cost (flCost + linkDist @+0x24) and
 * propagate the minimal returned cost into flUnknown. */
float navPointRelaxCosts(NavPoint *pPoint, NavPoint *pTarget, float flCost) /* @0x425af0 */
{
    float flVisited;
    int i;

    memcpy(&flVisited, &pPoint->nFlags, sizeof(float));  /* +0x58 */
    if (flVisited < flCost && flVisited != 0.0f) {       /* @0x425af6..@0x425b12 */
        return -1.0f;                                    /* g_flMinusOne @0x44b25c */
    }
    memcpy(&pPoint->nFlags, &flCost, sizeof(float));     /* @0x425b27 */

    if (pPoint != pTarget) {                             /* @0x425b2a */
        for (i = 0; i < pPoint->nLinkCount; i++) {       /* @0x425b62..@0x425bba */
            NavPoint *pLink = pPoint->pALink[i];
            float flEdge = pPoint->pALinkDist[i];        /* +0x24 */
            float fVar = navPointRelaxCosts(pLink, pTarget, flCost + flEdge); /* @0x425b7f */
            if (fVar != -1.0f) {                         /* @0x425b84 */
                int nFlUnknownBits;
                memcpy(&nFlUnknownBits, &pPoint->flUnknown, 4);
                if (nFlUnknownBits == (int)0xbf800000 || fVar < pPoint->flUnknown) { /* @0x425b94 */
                    pPoint->flUnknown = fVar;            /* @0x425ba7 */
                }
            }
        }
        return pPoint->flUnknown;                        /* @0x425bba */
    }
    /* pPoint == pTarget @0x425b2c */
    if (flCost != -1.0f) {                               /* @0x425b30 */
        int nBits;
        memcpy(&nBits, &pPoint->flUnknown, 4);
        if (nBits == (int)0xbf800000 || flCost < pPoint->flUnknown) { /* @0x425b3d */
            pPoint->flUnknown = flCost;                  /* @0x425b58 */
        }
    }
    return flCost;                                       /* @0x425b5b */
}

/* navPointPickCheapestLink @0x425bd0 — reset all flags, relax costs from
 * pPoint to pTarget, then return the NavPoint* among pPoint's links whose
 * best-cost (+0x44) is minimal. Used by aiPathfindToTarget. */
NavPoint *navPointPickCheapestLink(NavPoint *pPoint, NavPoint *pTarget) /* @0x425bd0 */
{
    float flBest = -1.0f;                                /* @0x425bd3 0xbf800000 */
    NavPoint *pBest = NULL;
    int i;

    navPointResetAllFlags();                             /* @0x425bdd */
    navPointRelaxCosts(pPoint, pTarget, 0.0f);           /* @0x425bed */

    for (i = 0; i < pPoint->nLinkCount; i++) {           /* @0x425bfe..@0x425c3a */
        NavPoint *pLink = pPoint->pALink[i];
        int nBits;
        memcpy(&nBits, &pLink->flUnknown, 4);
        if (nBits != (int)0xbf800000) {                  /* @0x425c05 */
            if (pLink->flUnknown < flBest || flBest == -1.0f) { /* @0x425c0e..@0x425c2b */
                flBest = pLink->flUnknown;
                pBest = pLink;                           /* @0x425c30 */
            }
        }
    }
    return pBest;                                        /* @0x425c3c */
}

/* navPointFindNearestToXY @0x425c90 — nearest NavPoint to (nZ,nX) by 2D
 * XZ distance (full-list scan via gxVec2Set + mathVec2Polar). Editor and
 * navPointEditorClick use. Param order matches original: param_1 is Z,
 * param_2 is X (so nPosZ - nZ, nPosX - nX). */
NavPoint *navPointFindNearestToXY(int nZ, int nX) /* @0x425c90 */
{
    NavPoint *pPoint;
    NavPoint *pBest = NULL;
    float flBest = 0.0f;

    for (pPoint = navPointListGetHead(); pPoint != NULL; pPoint = navPointGetNext(pPoint)) { /* @0x425c9f..@0x425d1b */
        GxVec2 vDelta;
        GxVec2 vPolar;
        gxVec2Set(&vDelta, (float)pPoint->nPosZ - (float)nZ, /* @0x425cc4 */
                  (float)pPoint->nPosX - (float)nX);         /* @0x425ce1 */
        mathVec2Polar(&vPolar, &vDelta);                     /* @0x425cee */
        if (pBest == NULL || vPolar.x < flBest) {            /* @0x425cf3 */
            flBest = vPolar.x;
            pBest = pPoint;
        }
    }
    return pBest;                                        /* @0x425d1d */
}

/* navPointFindNearestInYRange @0x4259d0 — nearest NavPoint to (nX,nZ)
 * whose nPosY is within (nYMin, nYMax) by 2D XZ Euclidean distance.
 * Early ABS pruning mirrors the x87 FCOMPP path. Increments
 * g_nNavPtSearchCount @0x45d4dc. Returns head if no candidate (original
 * head->next == NULL fast path). Used by aiPathfindToTarget. */
NavPoint *navPointFindNearestInYRange(int nX, int nZ, int nYMin, int nYMax) /* @0x4259d0 */
{
    float flBest = 3.4028235e+38f;                       /* @0x4259e3 0x7f7fffff */
    NavPoint *pHead = g_pNavPointHead;                   /* @0x4259dd */
    NavPoint *pBest = pHead;
    NavPoint *pCur;

    g_nNavPtSearchCount++;                               /* @0x4259e2..@0x4259f3 */

    if (pHead == NULL) {
        return NULL;
    }
    pCur = pHead->pNext;                                 /* @0x4259f9 */
    if (pCur == NULL) {                                  /* @0x4259fc */
        return pHead;                                    /* @0x425a8f */
    }
    for (; pCur != NULL; pCur = pCur->pNext) {           /* @0x425a80 */
        if (pCur->nPosY <= nYMin || pCur->nPosY >= nYMax) { /* @0x425a17..@0x425a1d */
            continue;
        }
        {
            float dZ = (float)pCur->nPosZ - (float)nZ;   /* @0x425a1f */
            if (fabsf(dZ) >= flBest) {                   /* @0x425a2a..@0x425a37 */
                continue;
            }
            float dX = (float)pCur->nPosX - (float)nX;   /* @0x425a39 */
            if (fabsf(dX) >= flBest) {                   /* @0x425a44..@0x425a51 */
                continue;
            }
            float flDist = sqrtf(dZ * dZ + dX * dX);     /* @0x425a53..@0x425a67 */
            if (flDist < flBest) {                       /* @0x425a6b */
                flBest = flDist;                         /* @0x425a76 */
                pBest = pCur;                            /* @0x425a7a */
            }
        }
    }
    return pBest;
}

/* navPointPickRandom @0x425aa0 — uniformly random NavPoint (rand() % count walk). */
NavPoint *navPointPickRandom(void) /* @0x425aa0 */
{
    NavPoint *pHead = g_pNavPointHead;                   /* @0x425aa1 */
    int nRand = rand();                                  /* @0x425aa7 _rand */
    int nCount = g_nNavPointCount;                       /* @0x45d4d8 */
    int nIdx;
    NavPoint *pCur;
    int i;

    if (pHead == NULL || nCount <= 0) {
        return pHead;
    }
    nIdx = nRand % nCount;                               /* @0x425aad IDIV */
    if (nIdx <= 0) {                                     /* @0x425ab7 JLE */
        return pHead;
    }
    pCur = pHead;
    for (i = 0; i < nIdx; i++) {                         /* @0x425ab9..@0x425ac5 */
        NavPoint *pNext = pCur->pNext;
        if (pNext == NULL) {                             /* @0x425abe */
            return pCur;
        }
        pCur = pNext;
    }
    return pCur;
}

/* nloadCmd @0x407e10 — "nload <file.ai>": parse the buoy config with the
 * .sol grammar, free the previous point list, then two passes over the
 * top-level blocks ("0001" ..): first creates one 0x60 NavPoint per
 * block from its ID/X/Y/Z children (configNodeGetId -> configFindNode ->
 * configEnvGetDouble, ftol-rounded), appended via navPointListAdd;
 * second resolves each block's "LINKS" values ("LINK = <id>") through
 * navPointFindById into navPointAddLink records. Returns 0. */
int nloadCmd(int nContext, LPCSTR pszArgs) /* @0x407e10 */
{
    ConfigEnv env;
    ConfigNode *pNode;
    NavPoint *pPoint;
    char szFile[100];

    (void)nContext;
    configEnvCtor(&env);                              /* @0x4357c0 */
    if (fmtSscanf(pszArgs, "%s", szFile) == -1) {     /* g_sz_s @0x44e70c @0x43e69d */
        nopDebugStub();                               /* "Syntax: nload <filename>" @0x44ed74 */
        if (env.pRoot != NULL) {
            configNodeDtor(env.pRoot);                /* @0x435700 */
            free(env.pRoot);                          /* memFreeDirect @0x43dd37 */
        }
        mStringFree(&env.name);
        return 0;
    }
    if (configParseFile(&env, szFile) < 0) {          /* @0x435890 */
        nopDebugStub();                               /* "Couldn't read file <%s>" @0x44e8a4 */
        if (env.pRoot != NULL) {
            configNodeDtor(env.pRoot);
            free(env.pRoot);
        }
        mStringFree(&env.name);
        return 0;
    }
    nopDebugStub();                                   /* "Cleaning NavBuoys..." @0x44ed5c */
    pPoint = navPointListGetHead();                   /* @0x425ad0 */
    if (pPoint != NULL) {
        navPointListFreeAll(pPoint);                  /* @0x4257a0 */
        free(pPoint);                                 /* memFreeDirect @0x43dd37 */
    }
    nopDebugStub();                                   /* "Loading NavBuoys..." @0x44ed48 */

    for (pNode = configNextNode(&env, NULL); pNode != NULL;   /* @0x436870 */
         pNode = configNextNode(&env, pNode)) {
        ConfigNode *pId = configNodeGetId(pNode);     /* @0x436850 */

        pPoint = (NavPoint *)malloc(sizeof(NavPoint));/* operator_new(0x60) @0x43dd42 */
        if (pPoint != NULL) {
            pPoint = navPointCtor(pPoint);            /* @0x425760 */
        }
        pPoint->nId = (int)configEnvGetDouble(        /* @0x4369f0, key "ID" @0x44ed44 */
            configFindNode(&env, pId, "ID"));         /* @0x4367e0 */
        pPoint->nPosX = (int)configEnvGetDouble(      /* key "X" @0x44ed40 */
            configFindNode(&env, pId, "X"));
        pPoint->nPosY = (int)configEnvGetDouble(      /* key "Y" @0x44ed3c */
            configFindNode(&env, pId, "Y"));
        pPoint->nPosZ = (int)configEnvGetDouble(      /* key "Z" @0x44ed38 */
            configFindNode(&env, pId, "Z"));
        navPointListAdd(pPoint);                      /* @0x4258e0 */
    }
    for (pNode = configNextNode(&env, NULL); pNode != NULL;
         pNode = configNextNode(&env, pNode)) {
        ConfigNode *pId = configNodeGetId(pNode);
        NavPoint *pFrom;
        ConfigNode *pLink;

        pFrom = navPointFindById((int)configEnvGetDouble(   /* @0x425c50 */
            configFindNode(&env, pId, "ID")));
        for (pLink = configEnvGetValue(&env, pId, "LINKS"); /* @0x436560 @0x44ed30 */
             pLink != NULL;
             pLink = configNextNode(&env, pLink)) {
            NavPoint *pTo = navPointFindById((int)configEnvGetDouble(pLink));

            navPointAddLink(pFrom, pTo);              /* @0x4257f0 */
        }
    }
    if (env.pRoot != NULL) {
        configNodeDtor(env.pRoot);
        free(env.pRoot);
    }
    mStringFree(&env.name);
    return 0;
}
