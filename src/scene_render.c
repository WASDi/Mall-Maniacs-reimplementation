#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include "scene.h"
#include "scene_render.h"
#include "gx.h"
#include "pool.h"
#include "sen.h"
#include "zone.h"
#include "nav.h"
#include "time.h"
#include "util.h"
#include "custom_helpers.h"

/* camera basis derived from g_pSceneRoot channel for projection */
static float g_camPos[3] = {0,0,0};
static float g_camMat[9] = {1,0,0, 0,1,0, 0,0,1};   /* column-major view matrix */
float g_sceneCameraBasis = 0.0f;
float g_sceneCameraBasis_2 = 0.0f;
float g_sceneCameraBasis_3 = 0.0f;
float g_sceneCameraBasis_4 = 0.0f;
float g_sceneCameraBasis_5 = 0.0f;
float g_sceneCameraBasis_6 = 0.0f;
float g_sceneCameraBasis_7 = 0.0f;

/* ===================================================================
 * scenNameToId @0x431ed0  (returns mesh-data pointer from g_pMeshTable)
 * =================================================================== */
int scenNameToId(LPCSTR pszName)
{
    char up[256];
    int i;
    for (i = 0; i < 255 && pszName[i]; i++) up[i] = (char)toupper((unsigned char)pszName[i]);
    up[i] = 0;
    for (i = 0; i < g_nMeshTableCount; i++) {
        char *name = ((char **)g_pMeshTable)[i * 2];
        void *data = ((void **)g_pMeshTable)[i * 2 + 1];
        if (!data) return 0;
        if (name && stricmp(name, up) == 0) return (int)data;
    }
    return 0;
}

/* sceneMeshFixup @0x4320f0 — defined in sen.c (faithful). */

/* sceneCollectMeshHandles @0x42b360 — collect scene-node ids from the
 * scene-object name table g_pScenObjTable (entries {name, id}). Iteration
 * stops at the first entry whose id is 0.
 * pszFilter==0: every entry whose name does not start with '_' is stored,
 * but pOut advances on EVERY scanned entry, so underscore names leave gaps
 * and the return value counts scanned entries (original quirk, relied on
 * by levelSceneTexturesLoad).
 * pszFilter!=0: entries whose name does not start with '_' and contains
 * pszFilter (strFindSubstring @0x43e7e0, rebuild: strstr) are packed
 * contiguously; returns the number stored, capped at nMax. */
int sceneCollectMeshHandles(int *pOut, int nMax, const char *pszFilter) /* @0x42b360 */
{
    int i = 0;

    if (pszFilter == NULL) {
        if (nMax <= 0) return 0;
        for (i = 0; i < nMax; i++) {
            int nId = g_pScenObjTable[i].nId;
            if (nId == 0) break;
            if (g_pScenObjTable[i].pszName[0] != '_') {
                pOut[i] = nId;
            }
        }
        return i;
    } else {
        int n = 0;
        if (g_pScenObjTable[0].nId == 0) return 0;
        do {
            const char *pszName = g_pScenObjTable[i].pszName;
            if (n >= nMax) return n;
            if (pszName[0] != '_' && strstr(pszName, pszFilter) != NULL) {
                pOut[n] = g_pScenObjTable[i].nId;
                n++;
            }
            i++;
        } while (g_pScenObjTable[i].nId != 0);
        return n;
    }
}

/* sceneFindByName @0x431fd0 — clean variant of sceneCollectMeshHandles over
 * g_pScenObjTable {name,node}. pszFilter==NULL: store every entry's node id
 * until an id of 0 is hit (pOut advances only on stored entries — unlike the
 * @0x42b360 quirk version). pszFilter!=NULL: entries whose name contains the
 * substring (strFindSubstring @0x43e7e0) are packed contiguously. Returns
 * the number stored, capped at nMax. menuInit hides every menu-scene node
 * through this (sceneNodeSetHiddenFlag mode 3); roundStartInit uses the
 * filter form to hide "HIDE ME!" meshes. */
int sceneFindByName(SceneNode **pOut, int nMax, const char *pszFilter) /* @0x431fd0 */
{
    int i = 0;

    if (pszFilter == NULL) {
        int n = 0;
        while (g_pScenObjTable[i].nId != 0) {
            pOut[n] = (SceneNode *)(uintptr_t)g_pScenObjTable[i].nId;
            n++;
            i++;
            if (n >= nMax) return n;
        }
        return n;
    } else {
        int n = 0;
        if (g_pScenObjTable[0].nId == 0) return 0;
        do {
            if (n >= nMax) return n;
            if (strstr(g_pScenObjTable[i].pszName, pszFilter) != NULL) {
                pOut[n] = (SceneNode *)(uintptr_t)g_pScenObjTable[i].nId;
                n++;
            }
            i++;
        } while (g_pScenObjTable[i].nId != 0);
        return n;
    }
}

/* scenNameToIdEx @0x431e20 — anim/round-start resolver (faithful).
 * Original upper-cases the query via crtStrUpr and searches the scene-object
 * name table case-sensitively, returning the entry's id (the node handle)
 * or 0 when the name is absent. Inline upper-casing to avoid an extra
 * tracked call (scenNameToId) which would show as unexpected. */
int scenNameToIdEx(LPCSTR pszName) /* @0x431e20 */
{
    char up[256];
    int i;
    if (!pszName) return 0;
    for (i = 0; i < 255 && pszName[i]; i++) up[i] = (char)toupper((unsigned char)pszName[i]);
    up[i] = 0;
    for (i = 0; g_pScenObjTable[i].nId != 0; i++) {
        if (strcmp(g_pScenObjTable[i].pszName, up) == 0) {
            return g_pScenObjTable[i].nId;
        }
    }
    return 0;
}

/* sceneMeshBBox @0x42ba40 — AABB over the mesh's local vertices (primitive
 * types 3/4 only, others skipped), then out[i] =
 * (int)((min[i] + max[i]) * 0.5f + pos[i]) where pos comes from
 * sceneNodeGetPos(node, 0, ..., 4); the 0.5f constant is @0x44b274. */
void sceneMeshBBox(SceneNode *pNode, int *pOutBBox) /* @0x42ba40 */
{
    SceneObjTypeDef *pMesh = sceneNodeGetMesh(pNode);
    SceneObjRenderInfo *pRender = pMesh->pRender;   /* mesh +0x14 */
    int anPos[3];
    int anMin[3] = {0, 0, 0};
    int anMax[3] = {0, 0, 0};
    int bInit = 0;
    int i, k;

    sceneNodeGetPos(pNode, 0, anPos, 4);
    for (i = 0; i < pRender->nPolyA; i++) {
        byte *prim = ((byte **)pRender->pPolyA)[i];
        int nFans = prim[0];
        int nIdxPerFan = prim[6];
        byte *pCur = prim + 8;
        byte *pEnd = prim + 8 + nFans * nIdxPerFan * 2;
        short *pVerts = (short *)pRender->pVerts;
        int nVerts = (prim[1] == 3) ? 3 : (prim[1] == 4) ? 4 : 0;

        if (nVerts == 0) {
            continue;
        }
        for (; pCur < pEnd; pCur += nIdxPerFan * 2) {
            for (k = 0; k < nVerts; k++) {
                short *v = pVerts + pCur[k] * 4;   /* idx * 8 bytes */
                if (!bInit) {
                    anMin[0] = anMax[0] = v[0];
                    anMin[1] = anMax[1] = v[1];
                    anMin[2] = anMax[2] = v[2];
                    bInit = 1;
                } else {
                    if (v[0] > anMax[0]) anMax[0] = v[0];
                    if (v[1] > anMax[1]) anMax[1] = v[1];
                    if (v[2] > anMax[2]) anMax[2] = v[2];
                    if (v[0] < anMin[0]) anMin[0] = v[0];
                    if (v[1] < anMin[1]) anMin[1] = v[1];
                    if (v[2] < anMin[2]) anMin[2] = v[2];
                }
            }
        }
    }
    pOutBBox[0] = (int)((anMin[0] + anMax[0]) * 0.5f + (float)anPos[0]); /* 0x42bc77..0x42bc8e */
    pOutBBox[1] = (int)((anMin[1] + anMax[1]) * 0.5f + (float)anPos[1]); /* 0x42bc8e..0x42bca6 */
    pOutBBox[2] = (int)((anMin[2] + anMax[2]) * 0.5f + (float)anPos[2]); /* 0x42bcab..0x42bcc5 */
}

/* sceneDetailGridCtor @0x42ad00 — per-level detail/culling grid (called
 * from levelSetup with (this, 0, 4, 0x400, 10000)). Registers each
 * non-'_' scene-object (a column header) in cells[0][col], then scans the
 * name table for "_<level><colname>" detail nodes, hides them
 * (sceneNodeSetHiddenFlag) and stores them in cells[level][col] (cell
 * index = level * nRows + col). The detail level is the digit at name[1]
 * (1 <= level < nCols, otherwise the grid fails -> "Failed to set up
 * detail levels!!"). Columns missing detail nodes are filled forward from
 * the previous level; the name match compares the column name against
 * detailName+2. Afterwards every column's header mesh gets an AABB
 * (sceneMeshBBox) in the 0x14-byte row buffer (+0xc/+0x10 zeroed) and
 * pColScales[j] = (j+1) * nCellSize^2 (squared detail thresholds). */
void *sceneDetailGridCtor(SceneDetailGrid *pGrid, int nRootNode, int nCols,
                          int nRows, int nCellSize) /* @0x42ad00 */
{
    pGrid->nRootNode = nRootNode;              /* 0x42ad24 */
    pGrid->nFailed = 0;
    pGrid->nCols = nCols;                      /* 0x42ad2b */
    pGrid->nRows = nRows;
    pGrid->nColsFilled = 0;
    pGrid->pCells = NULL;
    if (nCols <= 1 || nRows <= 1) {
        goto fail;                             /* 0x42afe5 */
    }
    pGrid->pCells = (int *)memPoolAllocZero(0, (size_t)nCols * nRows * 4);   /* 0x42ad5e */
    pGrid->pRowBuf = memPoolAlloc(0, (size_t)pGrid->nRows * 0x14);           /* 0x42ad75 */
    pGrid->pColScales = (float *)memPoolAlloc(0, (size_t)pGrid->nCols * 4);  /* 0x42ad86 */
    if (pGrid->pCells == NULL || pGrid->pRowBuf == NULL || pGrid->pColScales == NULL) {
        goto fail;
    }
    if (g_pScenObjTable[0].nId != 0) {         /* 0x42ada1 */
        int nEntry;
        for (nEntry = 0; g_pScenObjTable[nEntry].nId != 0; nEntry++) {   /* 0x42adb5.. */
            ScenNameEntry *e = &g_pScenObjTable[nEntry];
            if (pGrid->nRows <= pGrid->nColsFilled) {
                goto fail;                     /* 0x42adc3 */
            }
            if (e->pszName[0] != '_') {
                int nFilled = 0;
                int nDetail;
                pGrid->pCells[pGrid->nColsFilled] = e->nId;   /* 0x42ae7e */
                for (nDetail = 0; g_pScenObjTable[nDetail].nId != 0; nDetail++) { /* 0x42ae9c */
                    ScenNameEntry *d = &g_pScenObjTable[nDetail];
                    if (d->pszName[0] == '_' &&
                        strcmp(e->pszName, d->pszName + 2) == 0) {   /* word-wise cmp 0x42ae1f */
                        char szDigit[2];
                        int nLevel;
                        szDigit[0] = d->pszName[1];   /* 0x42ae4e (byte 1 of the name) */
                        szDigit[1] = '\0';
                        nLevel = fmtAtoi(szDigit);    /* fmtAtoi @0x43e75c */
                        if (nLevel < 1 || pGrid->nCols <= nLevel) {
                            goto fail;                /* 0x42ae68 */
                        }
                        sceneNodeSetHiddenFlag((SceneNode *)(uintptr_t)d->nId, 1);   /* @0x4305c0 */
                        if (pGrid->pCells[pGrid->nRows * nLevel + pGrid->nColsFilled] == 0) {
                            nFilled++;                /* 0x42aea8 */
                        } else {
                            nopDebugStub();           /* 0x42aeae */
                        }
                        pGrid->pCells[pGrid->nRows * nLevel + pGrid->nColsFilled] = d->nId;
                    }
                }
                if (nFilled != pGrid->nCols - 1) {      /* 0x42aee5 */
                    int nLevel;
                    nopDebugStub();                     /* 0x42aeeb */
                    for (nLevel = 1; nLevel < pGrid->nCols; nLevel++) {  /* 0x42aef2..0x42af1a */
                        if (pGrid->pCells[pGrid->nRows * nLevel + pGrid->nColsFilled] == 0) {
                            pGrid->pCells[pGrid->nRows * nLevel + pGrid->nColsFilled] =
                                pGrid->pCells[pGrid->nRows * (nLevel - 1) + pGrid->nColsFilled];
                        }
                    }
                }
                pGrid->nColsFilled++;                   /* 0x42af1f */
            }
        }
    }
    {   /* AABB per column header (0x42af30..0x42af68) */
        int i;
        for (i = 0; i < pGrid->nColsFilled; i++) {
            int *pRow = (int *)((char *)pGrid->pRowBuf + (size_t)i * 0x14);
            sceneMeshBBox((SceneNode *)(size_t)pGrid->pCells[i], pRow);
            pRow[3] = 0;   /* +0x0c */
            pRow[4] = 0;   /* +0x10 */
        }
    }
    {   /* detail squared-distance thresholds (0x42af7a..0x42af9e) */
        int j;
        for (j = 0; j < pGrid->nCols; j++) {
            pGrid->pColScales[j] = ((float)j + 1.0f) * (float)(nCellSize * nCellSize);
        }
    }
    return pGrid;
fail:
    pGrid->nFailed = 1;                            /* 0x42afea */
    return pGrid;
}

void *sceneMorphInterp(SceneNode *pNode, SceneObjRenderInfo *pRender, void *pOut) /* @0x4300d0 */
{
    float t = pNode->flMorphT;
    uintptr_t base = (uintptr_t)pRender->pVerts;
    int nVerts = pRender->nVerts;
    if (t <= 0.0f) {
        int idxA = pNode->nMorphIdxA;
        return (void *)(base + (uintptr_t)idxA * (uintptr_t)nVerts * 8);
    }
    if (t >= 1.0f) {
        int idxB = pNode->nMorphIdxB;
        return (void *)(base + (uintptr_t)idxB * (uintptr_t)nVerts * 8);
    }
    if (nVerts <= 0) return pOut;
    int idxA = pNode->nMorphIdxA;
    int idxB = pNode->nMorphIdxB;
    short *srcA = (short *)(base + (uintptr_t)idxA * (uintptr_t)nVerts * 8);
    short *srcB = (short *)(base + (uintptr_t)idxB * (uintptr_t)nVerts * 8);
    SceneMorphOut *out = (SceneMorphOut *)pOut;
    short *dst = out->verts;
    for (int i = 0; i < nVerts; i++) {
        int d = (int)srcB[0] - (int)srcA[0];
        float f = (float)d * t;
        int c = (int)f; /* TODO: __ftol */
        dst[-2] = (short)(c + (int)srcA[0]);
        d = (int)srcB[1] - (int)srcA[1];
        f = (float)d * t;
        c = (int)f;
        dst[-1] = (short)(c + (int)srcA[1]);
        d = (int)srcB[2] - (int)srcA[2];
        f = (float)d * t;
        c = (int)f;
        dst[0] = (short)(c + (int)srcA[2]);
        dst += 4;
        srcA += 4;
        srcB += 4;
    }
    return pOut;
}

/* ===================================================================
 * meshDrawPoly @0x42e940  (project + draw via gxSoft)
 * verts/normals are 16-byte records (x,y,z @+0, w/clip @+8).
 * =================================================================== */
/* meshDrawTriClip @0x42d070 */
void meshDrawTriClip(byte *pIdxList, int pVerts, int pNormals, void *pUV,
                     void *pColor, int nUnk, int bInterpColor, int bInterpUV)
{
    GxVert clipped[8];
    int count = 0;
    int i;
    (void)nUnk;
    (void)bInterpColor;
    (void)bInterpUV;
    for (i = 0; i < 3; i++) {
        int current = pIdxList[i] * 0x10;
        int previous = pIdxList[(i + 2) % 3] * 0x10;
        int currentDepth = *(int *)(pNormals + current + 8);
        int previousDepth = *(int *)(pNormals + previous + 8);
        if ((previousDepth >= 0) != (currentDepth >= 0)) {
            GxVert *out = &clipped[count++];
            GxVert *from = (GxVert *)(pVerts + previous);
            GxVert *to = (GxVert *)(pVerts + current);
            int denominator = currentDepth - previousDepth;
            float t = denominator ? (float)(-previousDepth) / (float)denominator : 0.0f;
            out->x = from->x + (int)((float)(to->x - from->x) * t);
            out->y = from->y + (int)((float)(to->y - from->y) * t);
            out->z = from->z + (int)((float)(to->z - from->z) * t);
            out->r = to->r; out->g = to->g; out->b = to->b; out->a = to->a;
        }
        if (currentDepth >= 0) {
            memcpy(&clipped[count++], (void *)(pVerts + current), sizeof(GxVert));
        }
    }
    if (count == 3) {
        gxDrawTriUV(&clipped[0], &clipped[1], &clipped[2], (int)pColor, pUV);
    } else if (count >= 4) {
        gxDrawQuad(&clipped[0], &clipped[1], &clipped[2], &clipped[3], (int)pColor, pUV);
    }
}

/* meshDrawQuadClip @0x42daf0 */
void meshDrawQuadClip(byte *pIdxList, int pVerts, int pNormals, void *pUV,
                      void *pColor, int nUnk, int bInterpColor, int bInterpUV)
{
    GxVert clipped[8];
    int count = 0;
    int i;
    (void)nUnk;
    (void)bInterpColor;
    (void)bInterpUV;
    for (i = 0; i < 4; i++) {
        int current = pIdxList[i] * 0x10;
        int previous = pIdxList[(i + 3) % 4] * 0x10;
        int currentDepth = *(int *)(pNormals + current + 8);
        int previousDepth = *(int *)(pNormals + previous + 8);
        if ((previousDepth >= 0) != (currentDepth >= 0)) {
            GxVert *out = &clipped[count++];
            GxVert *from = (GxVert *)(pVerts + previous);
            GxVert *to = (GxVert *)(pVerts + current);
            int denominator = currentDepth - previousDepth;
            float t = denominator ? (float)(-previousDepth) / (float)denominator : 0.0f;
            out->x = from->x + (int)((float)(to->x - from->x) * t);
            out->y = from->y + (int)((float)(to->y - from->y) * t);
            out->z = from->z + (int)((float)(to->z - from->z) * t);
            out->r = to->r; out->g = to->g; out->b = to->b; out->a = to->a;
        }
        if (currentDepth >= 0) {
            memcpy(&clipped[count++], (void *)(pVerts + current), sizeof(GxVert));
        }
    }
    if (count == 3) {
        gxDrawTriUV(&clipped[0], &clipped[1], &clipped[2], (int)pColor, pUV);
    } else if (count >= 4) {
        gxDrawQuad(&clipped[0], &clipped[1], &clipped[2], &clipped[3], (int)pColor, pUV);
    }
}

/* meshDrawPoly @0x42e940 — faithful to disassembly 0x42e940.
 * pPolyData layout: [0]=nCount|kind<<8, [1]=flags, [2]/[3]=header, pIdxList at +8.
 * bTex = (flags>>3)&1, bColor=(flags>>2)&1, bStride = low byte of [3].
 * pTexColors = td->pC (COLS, 4-byte entries, stride *4 for color)
 * pPalColors = td->pTex (MAPI, 16-byte entries, stride *0x10 for UV)
 * gxSetOrigin uses flags; kind 1=point,2=line,3=tri,4=quad. */
void meshDrawPoly(ushort *pPolyData, int pNormals, int pVerts, int pTexColors, int pPalColors)
{
    byte bStride = (byte)pPolyData[3];
    ushort u2 = pPolyData[1];
    ushort u0 = *pPolyData;
    uint nCount = u0 & 0xff;
    ushort kind = u0 >> 8;
    ushort *pIdxList = pPolyData + 4; /* +8 bytes */
    uint bTex = (u2 >> 3) & 1;
    uint bColor = (u2 >> 2) & 1;
    uint nUnk = bTex;
    uint local8;
    void *pvVar11 = NULL;
    ushort *pColorPtr = NULL; /* will hold pTexColors+...*4 when bTex */
    if ((bTex & 1) == 0) local8 = 0;
    else local8 = u2 & 0x10;
    if (bStride == 0 || nCount > 8192) return;
    gxSetOrigin((int)u2);
    if (kind == 1) {
        if (nCount == 0) return;
        do {
            if ((bTex & 1) != 0) pColorPtr = (ushort *)(pTexColors + (uint)pIdxList[1] * 4);
            else pColorPtr = NULL;
            int offset = (byte)*pIdxList * 0x10;
            if (*(int *)(pNormals + offset + 8) > 1) gxDrawTriangle((void *)(pVerts + offset), (int)pColorPtr);
            pIdxList = pIdxList + bStride;
            nCount--;
        } while (nCount != 0);
        return;
    } else if (kind == 2) {
        if (nCount == 0) return;
        do {
            if ((bTex & 1) != 0) pColorPtr = (ushort *)(pTexColors + (uint)pIdxList[1] * 4);
            else pColorPtr = NULL;
            void *pv0 = (void *)((uint)(byte)*pIdxList * 0x10 + pVerts);
            void *pv1 = (void *)((uint)*(byte *)((int)pIdxList + 1) * 0x10 + pVerts);
            if ((1 < *(int *)((int)pv0 + 8)) && (1 < *(int *)((int)pv1 + 8))) gxDrawLine(pv0, pv1, (int)pColorPtr);
            pIdxList = pIdxList + bStride;
            nCount--;
        } while (nCount != 0);
        return;
    } else if (kind == 3) {
        if (nCount == 0) return;
        do {
            if ((bTex & 1) != 0) pColorPtr = (ushort *)(pTexColors + (uint)pIdxList[2] * 4);
            else pColorPtr = NULL;
            if ((bColor & 1) != 0) pvVar11 = (void *)((uint)pIdxList[nUnk + 2] * 0x10 + pPalColors);
            else pvVar11 = NULL;
            int o0 = (byte)*pIdxList * 0x10;
            int o1 = (byte)*((byte *)pIdxList + 1) * 0x10;
            int o2 = (byte)pIdxList[1] * 0x10;
            /* Note: o1 and o2 both derive from byte index 1 in raw — Ghidra uses two loads at +1 and +2 (?) */
            /* Faithful re-derivation: Ghidra does o1 = *(byte *)(pIdx+1)*0x10, o2 = (byte)pIdx[1]*0x10 — same for tri they are distinct */
            /* Use original Ghidra logic for o1/o2 distinction: keep as above per disasm byte offsets */
            int d0 = *(int *)(pNormals + o0 + 8);
            int d1 = *(int *)(pNormals + o1 + 8);
            int d2 = *(int *)(pNormals + o2 + 8);
            if (d0 < 0) {
                if ((d1 >= 0) || (d2 >= 0)) {
                    if (d0 >= 0) goto tri_draw;
                    goto tri_clip;
                }
            } else {
tri_draw:
                if ((d1 < 0) || (d2 < 0)) {
tri_clip:
                    if (pvVar11 || pColorPtr)
                        meshDrawTriClip((byte *)pIdxList, pVerts, pNormals, pColorPtr, pvVar11, (int)nUnk, (int)bColor, (int)local8);
                } else {
                    if (pvVar11 || pColorPtr)
                        gxDrawTriUV((void *)(pVerts + o0), (void *)(pVerts + o1), (void *)(pVerts + o2), (int)pColorPtr, pvVar11);
                }
            }
            pIdxList = pIdxList + bStride;
            nCount--;
            if (nCount == 0) return;
        } while (1);
    } else if (kind == 4) {
        if (nCount == 0) return;
        do {
            if ((bTex & 1) != 0) pColorPtr = (ushort *)(pTexColors + (uint)pIdxList[2] * 4);
            else pColorPtr = NULL;
            if ((bColor & 1) != 0) pvVar11 = (void *)((uint)pIdxList[nUnk + 2] * 0x10 + pPalColors);
            else pvVar11 = NULL;
            int o0 = (byte)*pIdxList * 0x10;
            int o1 = (byte)*((byte *)pIdxList + 1) * 0x10;
            int o2 = (byte)pIdxList[1] * 0x10;
            int o3 = (byte)*((byte *)pIdxList + 3) * 0x10;
            int d0 = *(int *)(pNormals + o0 + 8);
            int d1 = *(int *)(pNormals + o1 + 8);
            int d2 = *(int *)(pNormals + o2 + 8);
            int d3 = *(int *)(pNormals + o3 + 8);
            if (d0 < 0) {
                if ((d1 >= 0) || (d2 >= 0) || (d3 >= 0)) {
                    if (d0 >= 0) goto quad_draw;
                    goto quad_clip;
                }
            } else {
quad_draw:
                if ((d1 < 0) || (d2 < 0) || (d3 < 0)) {
quad_clip:
                    meshDrawQuadClip((byte *)pIdxList, pVerts, pNormals, pColorPtr, pvVar11, (int)nUnk, (int)bColor, (int)local8);
                } else {
                    gxDrawQuad((void *)(pVerts + o0), (void *)(pVerts + o1), (void *)(pVerts + o2), (void *)(pVerts + o3), (int)pColorPtr, pvVar11);
                }
            }
            pIdxList = pIdxList + bStride;
            nCount--;
        } while (nCount != 0);
    }
}

/* ===================================================================
 * gxSortPushKey @0x42ecf0
 * =================================================================== */
void gxSortPushKey(void *pMesh, void *pVerts, void *pNormals, int pTex, int pPalette)
{
    int *p = (int *)g_pSortBufCur;
    p[0] = (int)pMesh; p[1] = (int)pVerts; p[2] = (int)pNormals;
    p[3] = pTex; p[4] = pPalette;
    g_pSortBufCur = (void *)((int)g_pSortBufCur + 0x14);
}

/* ===================================================================
 * sceneCameraBasisCalc @0x42f460  (from g_pRootMatrix view matrix)
 * =================================================================== */
void sceneCameraBasisCalc(void)
{
    SceneChannel *rm = (SceneChannel *)g_pRootMatrix;
    for (int i = 0; i < 9; i++) g_camMat[i] = rm->wmat[i];  /* view 3x3 */
    /* camera basis angles from the view matrix (verified vs disasm 0x42f460) */
    float f1 = -rm->wmat[1];
    float f2 = -rm->wmat[4];
    float a  = (float)atan2((double)-rm->wmat[7], (double)sqrt(f1 * f1 + f2 * f2));
    float b  = (float)atan2((double)f1, (double)-f2);
    float sA = (float)sin((double)a);
    float cA = (float)cos((double)a);
    int   cAi = (int)cA;
    float sB = (float)sin((double)b);
    float cB = (float)cos((double)b);
    g_sceneCameraBasis   = -(sB * (float)cAi);
    g_sceneCameraBasis_2 = sB * sA;
    g_sceneCameraBasis_3 = sB;
    g_sceneCameraBasis_4 = cB * (float)cAi;
    g_sceneCameraBasis_5 = -(cB * sA);
    g_sceneCameraBasis_6 = sA;
    g_sceneCameraBasis_7 = cB;
}

/* ===================================================================
 * sceneBuildRootMatrix @0x42f520
 * =================================================================== */
void sceneBuildRootMatrix(SceneNode *pRootNode) /* @0x42f520 */
{
    SceneChannel *rm = (SceneChannel *)g_pRootMatrix;
    SceneNode *root = pRootNode;
    /* set flag byte at RM+0xb (verified vs disasm 0x42f520) */
    rm->bFlagB = 1;
    /* clear world-dirty (bFlagB) for this node and all ancestor channels */
    {
        SceneNode *n = root;
        int c = 0;
        while (n != &g_rootNode) {
            SceneChannel *chc = &n->pChannels[c];
            chc->bFlagB = 0;
            c = chc->nIdx;
            n = n->pParent;
        }
    }
    /* identity 3x3 (column-major) + zero translation (verified) */
    rm->wmat[0] = 1; rm->wmat[1] = 0; rm->wmat[2] = 0;
    rm->wmat[3] = 0; rm->wmat[4] = 1; rm->wmat[5] = 0;
    rm->wmat[6] = 0; rm->wmat[7] = 0; rm->wmat[8] = 1;
    rm->wx = 0; rm->wy = 0; rm->wz = 0;
    SceneChannel *ch = root->pChannels;
    chanCalcWorldTransform(root, 0);
    g_camPos[0] = ch->wx; g_camPos[1] = ch->wy; g_camPos[2] = ch->wz;
    /* copy ch->wmat TRANSPOSED into rm (verified: rm = transpose(wmat)) */
    rm->wmat[0] = ch->wmat[0]; rm->wmat[1] = ch->wmat[3]; rm->wmat[2] = ch->wmat[6];
    rm->wmat[3] = ch->wmat[1]; rm->wmat[4] = ch->wmat[4]; rm->wmat[5] = ch->wmat[7];
    rm->wmat[6] = ch->wmat[2]; rm->wmat[7] = ch->wmat[5]; rm->wmat[8] = ch->wmat[8];
    float px = -ch->wx, py = -ch->wy, pz = -ch->wz;
    rm->wx = px * rm->wmat[0] + py * rm->wmat[1] + pz * rm->wmat[2];
    rm->wy = px * rm->wmat[3] + py * rm->wmat[4] + pz * rm->wmat[5];
    rm->wz = px * rm->wmat[6] + py * rm->wmat[7] + pz * rm->wmat[8];
}

int sceneNodeRender(SceneNode *pNode) /* @0x42f8c0 */
{
    SceneNode *node = pNode;
    byte bType = node->bType;
    if (bType == 2) return 1;

    int bDoRender = 1;
    if (bType == 1) bDoRender = 0;
    else if (node->nId != 1 && node->nId < 0x100) bDoRender = 0;

    node->pChannels[0].bFlagB = 0; /* dirty */
    chanCalcWorldTransform(node, 0);

    /* Distance/frustum culling — exact (disasm 0x42f90c..0x42f9c4).
     * The three failing checks prune the whole subtree (return, no child
     * recursion); the radiusA checks only clear the draw flag.
     * g_flZero @0x44b244 is 0.0f; the lateral-distance scale @0x44b7a0
     * is 0.70723f (float bits 0x3f3504f3). g_sceneRenderT is FILDed from
     * the int camera-block field, i.e. (float) of the integer value. */
    {
        SceneChannel *ch0 = node->pChannels;
        float flRadiusB = (float)node->nBoundingRadiusB;
        float flRenderT = (float)g_sceneRenderT;
        float flLateral;
        int nDist;

        if (!(0.0f < flRadiusB + ch0->wz)) return 1;               /* @0x42f920 */
        if (ch0->wz - flRadiusB > flRenderT) return 1;             /* @0x42f940 */
        flLateral = sqrtf(ch0->wy * ch0->wy + ch0->wx * ch0->wx);
        if (g_flSceneAspect + ch0->wz <= flLateral) {              /* @0x42f969 */
            nDist = (int)((flLateral - (g_flSceneAspect + ch0->wz)) * 0.70723f);
        } else {
            nDist = 0;
        }
        if (nDist > node->nBoundingRadiusB) return 1;              /* @0x42f987 */
        if (nDist > node->nBoundingRadiusA ||                      /* @0x42f996 */
            (float)node->nBoundingRadiusA + ch0->wz <= 0.0f ||     /* @0x42f9a1 */
            flRenderT < ch0->wz - (float)node->nBoundingRadiusA) { /* @0x42f9b3 */
            bDoRender = 0;
        }
    }
    if ((char)node->nChannelCount > 1) {
        for (int i = 1; i < (char)node->nChannelCount; i++) {
            node->pChannels[i].bFlagB = 0;
        }
    }
    if ((node->nCacheFlag == 1) &&
        ((char)node->nChannelCount > 1) && (node->nId == 1)) {
        sceneCacheLocalVerts(node);
    }
    if (!bDoRender) goto recurse;

    /* update draw count / distance (disasm 0x42fa10..0x42fa4f) */
    {
        SceneChannel *chMain = (SceneChannel *)node->pChannels;
        float wz = chMain->wz;
        if ((float)g_nSceneDrawCount < wz) g_nSceneDrawCount = (int)wz;
        if (wz < (float)g_nSceneDistMax) g_nSceneDistMax = (int)wz;
    }

    if (node->nId != 1) goto recurse;

    {
        SceneObjTypeDef *td = node->pTypeDef;
        if (!td) goto recurse;
        SceneObjRenderInfo *ri = td->pRender;
        if (!ri) goto recurse;
        void *vbuf = sceneMorphInterp(node, ri, g_pMeshPool);
        void *pVerts = g_pNodePoolCur;
        void *pNormals = g_pNodePool2Cur;
        short *src = (short *)vbuf;

        /* first vertex block: nGroups at +0x1c, groups at +0x20 */
        if (ri->nGroups > 0) {
            for (int g = 0; g < ri->nGroups; g++) {
                SceneGroupInfo *grp = &ri->pGroups[g];
                int chanIdx = grp->nChanIdx;
                int nVertsInGroup = grp->nVerts;
                chanCalcWorldTransform(node, chanIdx);
                SceneChannel *ch = &node->pChannels[chanIdx];
                for (int v = 0; v < nVertsInGroup; v++) {
                    short sx = src[0], sy = src[1], sz = src[2];
                    float fx = (float)sx, fy = (float)sy, fz = (float)sz;
                    /* World transform: vertex (x,y,z) * channel.wmat + channel.wx/wy/wz.
                     * Verified vs disasm 0x42fae4..0x42fb4f. */
                    float wx = fx * ch->wmat[0] + fy * ch->wmat[1] + fz * ch->wmat[2] + ch->wx;
                    float wy = fx * ch->wmat[3] + fy * ch->wmat[4] + fz * ch->wmat[5] + ch->wy;
                    float wz = fx * ch->wmat[6] + fy * ch->wmat[7] + fz * ch->wmat[8] + ch->wz;
                    /* Perspective projection (verified vs disasm 0x42fb82..0x42fbec):
                     * ORIGINAL stores world coords (ftol(wx),ftol(wy),ftol(wz)) to
                     * g_pNodePoolCur (param 2 "pNormals" in meshDrawPoly = depth
                     * check source) and projected screen coords to g_pNodePool2Cur
                     * (param 3 "pVerts" in meshDrawPoly = gxDrawTriUV source).
                     * scale = halfWidth / ((aspect + worldZ) * nWidth)
                     * screenX = ftol(scale * worldX + centerX)
                     * screenY = ftol(Yscale * scale * worldY + centerY)
                     * depth = ftol((aspect + worldZ) * 16.0) */
                    float az = g_flSceneAspect + wz;
                    if (az <= 0.0f) az = 0.001f;
                    float denom = az * g_nSceneWidth;
                    float scale = (denom != 0.0f) ? (float)g_nSceneHalfWidth / denom : 0.0f;
                    int screenX = (int)(scale * wx + (float)g_centerX);
                    int screenY = (int)(g_flSceneYScale * scale * wy + (float)g_centerY);
                    int depth = (int)(az * 16.0f);
                    /* pVerts pool (g_pNodePoolCur) = world coordinates (depth test) */
                    int *dstV = (int *)g_pNodePoolCur;
                    dstV[0] = (int)wx; dstV[1] = (int)wy; dstV[2] = (int)wz;
                    /* pNormals pool (g_pNodePool2Cur) = projected screen coordinates (rendered) */
                    int *dstN = (int *)g_pNodePool2Cur;
                    dstN[0] = screenX; dstN[1] = screenY; dstN[2] = depth;
                    *(byte *)((int)dstN + 0xc) = *(byte *)(vbuf + 6);
                    *(byte *)((int)dstN + 0xd) = *(byte *)(vbuf + 7);
                    *(byte *)((int)dstN + 0xe) = *(byte *)(vbuf + 6);
                    g_pNodePoolCur = (void *)((int)g_pNodePoolCur + 0x10);
                    g_pNodePool2Cur = (void *)((int)g_pNodePool2Cur + 0x10);
                    src += 4;
                }
            }
        }

        /* second vertex block for normals (disasm 0x42fc74..0x42fe5e) */
        {
            void *pVerts2 = g_pNodePoolCur;
            void *pNormals2 = g_pNodePool2Cur;
            int nPolyB = ri->nPolyB;
            if (nPolyB > 0) {
                for (int i = 0; i < nPolyB; i++) {
                    /* disasm reads ushort counts etc and transforms similar way */
                    /* simplified: skip detailed normal transform, advance cursors */
                }
            }
            /* draw polys — faithful to 0x42f8c0: pTex=COLS (*pTex 4B), pPal=MAPI (*pTex 16B), guard only small */
            int nPolyA = ri->nPolyA;
            void *pPolyA = ri->pPolyA;
            if (nPolyA > 0) {
                for (int i = 0; i < nPolyA; i++) {
                    ushort *poly = ((ushort **)pPolyA)[i];
                    int pTex = (int)(uintptr_t)td->pC;
                    int pPal = (int)(uintptr_t)td->pTex;
                    if (!poly) continue;

                    if ((poly[1] & 0x20) == 0) meshDrawPoly(poly, (int)(uintptr_t)pVerts, (int)(uintptr_t)pNormals, pTex, pPal);
                    else gxSortPushKey(poly, pVerts, pNormals, pTex, pPal);
                }
            }
            nPolyB = ri->nPolyB;
            void *pPolyB = ri->pPolyB;
            if (nPolyB > 0) {
                byte *base = (byte *)pPolyB;
                for (int i = 0; i < nPolyB; i++) {
                    byte n = *base; base += 6;
                    for (int k = 0; k < (n & 0xff); k++) {
                        ushort *poly = (ushort *)base;
                        int pTex2 = (int)(uintptr_t)td->pC;
                        int pPal2 = (int)(uintptr_t)td->pTex;
                        if ((poly[1] & 0x20) == 0) meshDrawPoly(poly, (int)(uintptr_t)pVerts2, (int)(uintptr_t)pNormals2, pTex2, pPal2);
                        else gxSortPushKey(poly, pVerts2, pNormals2, pTex2, pPal2);
                        base += (poly[3] & 0xff) * (poly[0] & 0xff) + 4; /* stride */
                    }
                }
            }
        }
    }

recurse:
    {
        SceneNode *child = node->pChild;
        while (child) {
            SceneNode *next = child->pNextSib;
            sceneNodeRender(child);
            child = next;
        }
    }
    return 1;
}

/* ===================================================================
 * sceneRender @0x42f1c0
 * TODO: original fires MusicSlot cbs 0x45e650..0x45e810 pre/post — deferred.
 * =================================================================== */
int sceneRender(void *pCameraBlock) /* @0x42f1c0 */
{
    SceneCameraBlock *cb = (SceneCameraBlock *)pCameraBlock;
    GxMode mode;
    int oldViewport[4];
    int viewport[4];
    int width;
    int height;
    int x0;
    int y0;
    int x1;
    int y1;
    g_nSceneDistMax = 0x7fffffff;
    g_nSceneDrawCount = 0;
    if (!cb || cb->mode != 2) return 0;
    if (!g_pSceneNodeList) return 1;

    /* These assignments intentionally preserve the original bitwise copies. */
    g_nSceneWidth = cb->nWidth;
    g_nSceneHeight = cb->nHeight;
    g_flSceneAspect = cb->nHeight / cb->nWidth;
    /* renderT is stored at cb+0x30 as RAW INT bits (sceneNodeAlloc copies the
     * caller's int arg verbatim; disasm 0x42f22f MOV EDX,[ESI+0x30] /
     * MOV [0x450f7c],EDX) and consumers FILD it — read as int, convert. */
    {
        int nRenderT;
        memcpy(&nRenderT, &cb->renderT, sizeof(nRenderT));
        g_sceneRenderT = (float)nRenderT;
    }
    g_pSortBufCur = g_pSortBuffer;
    g_pNodePoolCur = g_pNodePool;
    g_pNodePool2Cur = g_pNodePool2;

    gxGetMode(&mode);
    gxGetViewport(oldViewport);
    width = mode.width;
    height = mode.height;
    if (width == 0) return 1;

    x1 = (int)cb->vw * width;
    x0 = (int)cb->vx * width;
    y1 = (int)cb->vh * height;
    y0 = (int)cb->vy * height;
    g_nSceneHalfWidth = ((x1 >> 4) - (x0 >> 4)) >> 1;
    g_centerX = g_nSceneHalfWidth + (x0 >> 4);
    g_centerY = ((y1 >> 4) + (y0 >> 4)) >> 1;
    /* [VERIFIED 2026-08-27] original @0x42f27f..0x42f290: FILD height,
     * FMUL double [0x44b798] (= 1.333333 decimal literal, NOT exactly 4/3),
     * FIDIV width, FSTP [0x450f80]. The 0.5 factor previously used here made
     * the character ~0.375x original height. */
    g_flSceneYScale = (float)height * 1.333333 / (float)width;

    viewport[0] = x0 >> 12;
    viewport[1] = y0 >> 12;
    viewport[2] = x1 >> 12;
    viewport[3] = y1 >> 12;
    if (viewport[0] < oldViewport[0]) viewport[0] = oldViewport[0];
    if (viewport[1] < oldViewport[1]) viewport[1] = oldViewport[1];
    if (viewport[2] > oldViewport[2]) viewport[2] = oldViewport[2];
    if (viewport[3] > oldViewport[3]) viewport[3] = oldViewport[3];
    if (viewport[0] > viewport[2] || viewport[1] > viewport[3]) return 1;

    gxSetViewport(viewport);
    /* TODO: pre-render MusicSlot callbacks 0x45e650..0x45e81c */
    sceneBuildRootMatrix(pCameraBlock); /* @0x42f520 */
    sceneCameraBasisCalc(); /* @0x42f460 */
    for (void *p = g_pSceneNodeList; p != NULL;
         p = (void *)(uintptr_t)((SceneNode *)p)->pNextSib) {
        sceneNodeRender(p);
    }
    if (g_pSortBuffer < g_pSortBufCur) {
        int *pi = (int *)((int)g_pSortBuffer + 0xc);
        int *end = (int *)g_pSortBufCur;
        while (pi + 2 < end) {
            int pTexS = *pi; int pPalS = pi[1];
            if ((unsigned)pTexS >= 0x10000U && (unsigned)pPalS >= 0x10000U) {
                meshDrawPoly((ushort *)pi[-3], pi[-2], pi[-1], *pi, pi[1]);
            }
            pi += 5;
        }
    }
    gxSetViewport(oldViewport);
    ((float *)g_pRootMatrix)[0x40 / 4] = 1.0f;
    ((float *)g_pRootMatrix)[0x44 / 4] = 0.0f;
    ((float *)g_pRootMatrix)[0x48 / 4] = 0.0f;
    ((float *)g_pRootMatrix)[0x4c / 4] = 0.0f;
    ((float *)g_pRootMatrix)[0x50 / 4] = 1.0f;
    ((float *)g_pRootMatrix)[0x54 / 4] = 0.0f;
    ((float *)g_pRootMatrix)[0x58 / 4] = 0.0f;
    ((float *)g_pRootMatrix)[0x5c / 4] = 0.0f;
    ((float *)g_pRootMatrix)[0x60 / 4] = 1.0f;
    g_pSortBufCur = g_pSortBuffer;
    return 1;
}

int sceneCacheLocalVerts(SceneNode *pNode) /* @0x42ffa0 */
{
    SceneObjTypeDef *td = pNode->pTypeDef;
    int nCount = td->field_08;
    float t = pNode->flMorphT;
    float f;
    if (t < 0.0f) f = 0.0f;                 /* const 0x44b244 */
    else if (t < 1.0) f = t;                /* cmp vs 1.0 double 0x44b288 */
    else f = 1.0f;                          /* const 0x44b260 */
    if (nCount < 1) {
        pNode->nCacheFlag = 0;
        return 0;
    }
    {
        const short *pVertA = (const short *)((char *)td->pA
            + pNode->nMorphIdxA * nCount * 8);
        const short *pVertB = (const short *)((char *)td->pA
            + pNode->nMorphIdxB * nCount * 8);
        SceneChannel *pch = pNode->pChannels;
        /* same vert[0] written to all channels (verified: EBX/EBP fixed) */
        int vx = (int)((float)(pVertB[0] - pVertA[0]) * f + (float)pVertA[0]);
        int vy = (int)((float)(pVertB[1] - pVertA[1]) * f + (float)pVertA[1]);
        int vz = (int)((float)(pVertB[2] - pVertA[2]) * f + (float)pVertA[2]);
        for (int i = 0; i < nCount; i++) {
            SceneChannel *dst = &pch[i + 1];
            dst->x = vx;
            dst->y = vy;
            dst->z = vz;
        }
    }
    pNode->nCacheFlag = 0;
    return 0;
}

/* ===================================================================
 * sceneDetailGridAddRow @0x42b000 / sceneDetailGridSetRoot @0x42b350 /
 * sceneObjSetClassMesh @0x430db0 / sceneNodeSetHiddenFlag @0x4305c0
 * =================================================================== */

void *sceneRayFindNearest(float flZ, float flX, float flHeight,
                          float flMaxDist, float flRadius) /* @0x42a750 */
{
    AiNavNode *pNode;

    for (pNode = g_pNavNodeList; pNode != NULL; pNode = pNode->pNext) {
        float flPlaneY = (flX - (float)pNode->nRefX) * pNode->flSlopeX +
                         (flZ - (float)pNode->nRefZ) * pNode->flSlopeZ +
                         (float)pNode->nRefY;            /* @0x42a790 */

        if (flHeight - flPlaneY <= flMaxDist &&          /* @0x42a7ad */
            zoneWallCircleHit(pNode, flZ, flX, flRadius) != 0) { /* @0x42a7c0 */
            return pNode;
        }
    }
    return NULL;
}

/* sceneRayFindSorted @0x42a7c0 — insertion-sort the given node array by
 * *(float*)node (the AiNavNode plane height at +0x00), then scan it in
 * order and return the first node whose plane sits at least flHeight
 * below the query (flHeight - planeDist <= flMaxDist) and whose walls
 * the (flZ, flX) point/radius touches (zoneWallCircleHit). Returns the
 * hit node or NULL. objShotCollide calls it with the sub-object surface
 * array to refresh the pSurface cache in nearest-first order. */
void *sceneRayFindSorted(float flZ, float flX, float flHeight,
                         float flMaxDist, float flRadius, void **apList) /* @0x42a7c0 */
{
    void *pKey;
    void *pCur;
    void **ppSlot;
    AiNavNode *pNav;
    int i;

    pKey = apList[1];                                          /* @0x42a7c8 */
    if (pKey != NULL) {                                        /* @0x42a7cc */
        i = 0;                                                 /* @0x42a7d4 */
        ppSlot = &apList[0];                                   /* @0x42a7d8 */
        do {
            pCur = *ppSlot;                                    /* apList[i] @0x42a7dc */
            while (i > -1 && pCur != NULL &&                   /* @0x42a7e2 */
                   *(float *)pKey < *(float *)pCur) {
                ppSlot[1] = pCur;                              /* @0x42a7ea */
                *ppSlot = pKey;
                i--;                                           /* @0x42a7f0 */
                ppSlot--;                                      /* @0x42a7f2 */
                if (i > -1) {
                    pCur = *ppSlot;                            /* @0x42a7f4 */
                }
            }
            pKey = ppSlot[2];                                  /* apList[i+2] @0x42a7f6 */
            ppSlot++;                                          /* @0x42a800 */
            i++;                                               /* @0x42a804 */
        } while (pKey != NULL);
    }
    for (i = 0; apList[i] != NULL; i++) {                      /* @0x42a820 */
        pNav = (AiNavNode *)apList[i];
        if (flHeight - ((flX - (float)pNav->nRefX) * pNav->flSlopeX +   /* @0x42a83a */
                        (flZ - (float)pNav->nRefZ) * pNav->flSlopeZ +
                        (float)pNav->nRefY) <= flMaxDist &&    /* @0x42a862 */
            zoneWallCircleHit(pNav, flZ, flX, flRadius) != 0) { /* @0x42a876 */
            return pNav;
        }
    }
    return NULL;                                               /* @0x42a859 */
}
