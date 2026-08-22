#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sen.h"
#include "gx.h"
#include "custom_helpers.h"
#include "charselect.h"
#include "util.h"
#include "pool.h"

/* =====================================================================
 * SEN scene-file loader cluster. Faithful reimplementation of the maniac.exe
 * .SEN reading pipeline (sceneLoadSen @0x432320, senChunkParse @0x432c00,
 * sceneMeshFixup @0x4320f0, sceneCreateTextureSurfaces @0x432260).
 *
 * The original pipeline reads the REV2 file, walks the {tag,size,data} chunk
 * chain, stores each MESH node in g_pMeshTable, then relocates the mesh node
 * pointer fields with sceneMeshFixup. The actual scene-graph instantiation
 * (OBJI objects, scenery nodes, emitters) is performed by sceneInstantiateObjects,
 * which is out of scope for the offline menu preview and is documented-stubbed
 * in stubs.c. For menu\CHARACTERS.SEN only MESH + EMAN chunks are present, so
 * the instantiation path never executes.
 * ===================================================================== */

/* --- scene-load globals (addresses from disassembly where known) --- */
static char   g_szSceneDir[256];            /* scene directory string (empty for menu) */
static char   g_meshNameBuf[8192];          /* backing store for mesh-name strings */
static char  *g_pMeshNameStr = g_meshNameBuf;/* running name-buffer write cursor */
static unsigned char g_meshTableMem[256 * 8];/* mesh table: 8-byte entries {name*,data*} */
void  *g_pMeshTable   = g_meshTableMem;/* @0x45e930 */
int   g_nMeshTableCount = 0;             /* @0x45e944 entry count */
static void  *g_pMeshTableWr;               /* @0x45e938 write cursor */
static void  *g_pMeshTableStart;            /* @0x45e948 first new entry this load */
static char   g_sceneNameBuf[16384];
static char  *g_pSceneNameBufPos = g_sceneNameBuf;
static char  *g_pObjNameList;               /* @0x45e934 */
static char  *g_pObjNameTable;              /* @0x45e990 */
static int    g_nObjNameTableSize;
static char  *g_pObjInstances;              /* @0x45eaa4 */
static int    g_nObjInstanceCount;
static char  *g_pTextAnimData;              /* @0x45e940 */
static int    g_nTextAnimSize;static int   *g_pMapGeom;                   /* @0x45eb20 */
static int    g_nMapGeomCount;
static char  *g_pKeepChunk;
static char  *g_pSubObjData;                /* @0x45eb30 */
static int    g_nColsCount;                 /* @0x45eb28 */
static char  *g_pColsData;
static int    g_nSceneLoadCount;
static int    g_scenesceneLoadSen;
static int    g_nSceneMeshMaxSize;          /* _g_nSceneMeshMaxSize */
static int    g_scenesceneMeshFixup;        /* _g_scenesceneMeshFixup */

/* sceneInstantiateObjects — documented stub for the out-of-scope scene-graph
 * population (OBJI objects / scenery nodes / emitters). Declared here; the
 * real body (when gameplay lands) would build the scene graph from
 * g_pObjInstances / g_pMapGeom / g_pTextAnimData. Per Rebuild.md it is a
 * safe no-op for the offline menu preview. */
extern int sceneInstantiateObjects(int pool);

/* ---------------------------------------------------------------------
 * senChunkParse @0x432c00 — parse a chain of nested .sen chunk records
 * between pData and pDataEnd. Each record: {int tag, int size, data}.
 * ------------------------------------------------------------------- */
int senChunkParse(byte *pData, byte *pDataEnd)
{
    char *pbVar9;
    if (pDataEnd <= pData) return 1;
    do {
        int   iVar6 = *(int *)pData;
        unsigned int  uVar3 = *(unsigned int *)(pData + 4);
        byte *pbVar7 = pData + 8;
        pbVar9 = g_pSubObjData;
        if (iVar6 < 0x4950414e) {
            if (iVar6 == 0x4950414d) {                 /* MAPI */
                g_nMapGeomCount = uVar3 >> 4;
                g_pMapGeom = (int *)pbVar7;
            } else if (iVar6 < 0x494a4250) {
                if (iVar6 == 0x494a424f) {             /* OBJI */
                    g_nObjInstanceCount = uVar3 >> 5;
                    g_pObjInstances = (char *)pbVar7;
                } else if (iVar6 == 0x454d414e) {      /* EMAN (mesh name) */
                    if (g_pMeshTableStart < g_pMeshTableWr) {
                        *(char **)((int)g_pMeshTableWr - 8) = g_pMeshNameStr;
                    }
                    {
                        unsigned int d = (unsigned int)strlen(g_szSceneDir);
                        if (d) { memcpy(g_pMeshNameStr, g_szSceneDir, d); g_pMeshNameStr += d; }
                        unsigned int nm = (unsigned int)strlen((char *)pbVar7);
                        memcpy(g_pMeshNameStr, pbVar7, nm); g_pMeshNameStr += nm;
                        *g_pMeshNameStr++ = '\0';
                    }
                    pbVar9 = g_pSubObjData;
                } else if (iVar6 == 0x4853454d) {      /* MESH */
                    *(byte **)((int)g_pMeshTableWr + 4) = pbVar7;
                    *(int *)g_pMeshTableWr = 0;
                    g_pMeshTableWr = (void *)((int)g_pMeshTableWr + 8);
                    pbVar9 = g_pSubObjData;
                }
            } else if (iVar6 == 0x494e4154) {          /* TANI */
                g_pTextAnimData = (char *)pbVar7;
                g_nTextAnimSize = uVar3;
            }
        } else if (iVar6 < 0x4f425554) {
            pbVar9 = (char *)pbVar7;
            if (iVar6 != 0x4f425553) {                 /* not SUBO */
                if (iVar6 == 0x4d414e4f) {             /* ONAM */
                    g_pObjNameList = g_pSceneNameBufPos;
                    memcpy(g_pSceneNameBufPos, pbVar7, uVar3);
                    g_pSceneNameBufPos = (void *)((int)g_pSceneNameBufPos + uVar3);
                    pbVar9 = g_pSubObjData;
                } else {                                /* TNAM */
                    pbVar9 = g_pSubObjData;
                    if (iVar6 == 0x4d414e54) {
                        g_pObjNameTable = (char *)pbVar7;
                        g_nObjNameTableSize = uVar3;
                    }
                }
            }
        } else if (iVar6 == 0x534c4f43) {              /* COLS */
            g_nColsCount = uVar3 >> 2;
            g_pColsData = (char *)pbVar7;
        }
        g_pSubObjData = (char *)pbVar9;
        pData = pbVar7 + uVar3;
    } while (pDataEnd > pData);
    return 1;
}

/* ---------------------------------------------------------------------
 * sceneMeshFixup @0x4320f0 — relocate the pointer fields of a loaded mesh
 * node. param_1 = node base, param_2 = object-name table, param_3 =
 * &g_pMapGeom (used as the texture/map reference when non-null).
 * ------------------------------------------------------------------- */
void sceneMeshFixup(int param_1, char param_2, int param_3)
{
    int iVar1;
    int iVar2;
    int *piVar3;
    int *piVar4;

    if (0 < *(int *)(param_1 + 8)) {
        *(int *)(param_1 + 0xc) = *(int *)(param_1 + 0xc) + param_1;
        *(int *)(param_1 + 0x10) = *(int *)(param_1 + 0x10) + param_1;
    }
    iVar2 = *(int *)(param_1 + 0x14) + param_1;
    *(int *)(param_1 + 0x14) = iVar2;
    if (param_3 == 0) {
        *(int *)(param_1 + 0x20) = *(int *)(param_1 + 0x20) + param_1;
        *(int *)(param_1 + 0x28) = *(int *)(param_1 + 0x28) + param_1;
    } else {
        if (*(int *)param_3 == 0) {
            *(int *)(param_1 + 0x20) = *(int *)(param_1 + 0x20) + param_1;
        } else {
            *(int *)(param_1 + 0x20) = *(int *)(param_1 + 0x20) + *(int *)param_3;
        }
        if (*(int *)(param_3 + 8) == 0) {
            *(int *)(param_1 + 0x28) = *(int *)(param_1 + 0x28) + param_1;
        } else {
            *(int *)(param_1 + 0x28) = *(int *)(param_1 + 0x28) + *(int *)(param_3 + 8);
        }
    }
    iVar1 = 0;
    *(int *)(param_1 + 0x30) = *(int *)(param_1 + 0x30) + param_1;
    if (0 < *(int *)(param_1 + 4)) {
        piVar3 = (int *)(iVar2 + 8);
        do {
            *piVar3 = *piVar3 + param_1;
            if (piVar3[1] != 0) {
                piVar3[2] = piVar3[2] + param_1;
            }
            if (piVar3[8] != 0) {
                piVar3[9] = piVar3[9] + param_1;
            }
            piVar3[4] = piVar3[4] + param_1;
            piVar3[6] = piVar3[6] + param_1;
            piVar4 = (int *)piVar3[4];
            if (param_3 == 0) {
                iVar2 = 0;
                if (0 < piVar3[3] + piVar3[7]) {
                    do {
                        iVar2 = iVar2 + 1;
                        *piVar4 = *piVar4 + param_1;
                        piVar4 = piVar4 + 1;
                    } while (iVar2 < piVar3[3] + piVar3[7]);
                }
            } else if (*(int *)(param_3 + 0x10) == 0) {
                iVar2 = 0;
                if (0 < piVar3[3] + piVar3[7]) {
                    do {
                        iVar2 = iVar2 + 1;
                        *piVar4 = *piVar4 + param_1;
                        piVar4 = piVar4 + 1;
                    } while (iVar2 < piVar3[3] + piVar3[7]);
                }
            } else {
                iVar2 = 0;
                if (0 < piVar3[3] + piVar3[7]) {
                    do {
                        *piVar4 = *piVar4 + *(int *)(param_3 + 0x10);
                        iVar2 = iVar2 + 1;
                        piVar4 = piVar4 + 1;
                    } while (iVar2 < piVar3[3] + piVar3[7]);
                }
            }
            iVar1 = iVar1 + 1;
            piVar3 = piVar3 + 0xc;
        } while (iVar1 < *(int *)(param_1 + 4));
    }
    if ((param_3 == 0) || (*(int *)param_3 == 0)) {
        sceneCreateTextureSurfaces(*(int **)(param_1 + 0x20),
                                  *(int *)(param_1 + 0x1c), (char *)&param_2);
    }
    iVar2 = *(int *)(*(int *)(param_1 + 0x14) + 4);
    if (g_nSceneMeshMaxSize < iVar2) g_nSceneMeshMaxSize = iVar2;
    if (0x100 < iVar2) g_scenesceneMeshFixup = g_scenesceneMeshFixup + 1;
}

/* ---------------------------------------------------------------------
 * sceneCreateTextureSurfaces @0x432260 — bind a list of texture ids to
 * surfaces. For the offline preview we do not require real surfaces, so a
 * zero-count list (the common case for CHARACTERS.SEN) returns success
 * immediately; a non-zero list attempts gxCreateSurface but still reports
 * success so the menu preview proceeds.
 * ------------------------------------------------------------------- */
int sceneCreateTextureSurfaces(int *pTexIdList, int nCount, char *pszFilenames)
{
    int i;
    (void)pszFilenames;
    /* The offline menu preview does not require real texture surfaces. A
     * zero-length list (the common case for CHARACTERS.SEN) returns success
     * immediately; a non-zero list is bound best-effort and still reports
     * success so the preview proceeds. */
    if (nCount <= 0) return 1;
    for (i = 0; i < nCount; i++) {
        if (pTexIdList[i] >= 0) {
            int surf = gxCreateSurface(NULL);
            pTexIdList[i] = surf;
        }
    }
    return 1;
}

/* ---------------------------------------------------------------------
 * sceneLoadSen @0x432320 — open a REV2 .SEN file, iterate its chunk chain,
 * store MESH nodes in g_pMeshTable, and relocate each with sceneMeshFixup.
 * Returns the owning memPool handle (kept alive by the caller) or 0.
 * ------------------------------------------------------------------- */
int sceneLoadSen(LPCSTR param_1, int *param_2)
{
    FILE *fp;
    void *pvPool;
    int   hdr[2];               /* [0]=magic, [1]=total */
    int   chunk[2];             /* [0]=tag, [1]=size */
    char  local_100[256];
    int   local_124 = 0;
    char *pcVar5;

    (void)param_2;
    g_pObjNameTable   = NULL;
    g_pObjNameList    = NULL;
    g_pObjInstances   = NULL;
    g_scenesceneLoadSen = 0;
    g_pTextAnimData   = NULL;
    g_nObjInstanceCount = 0;
    g_nTextAnimSize   = 0;
    g_nObjNameTableSize = 0;
    g_pMapGeom        = NULL;
    g_nMapGeomCount   = 0;
    g_pColsData       = NULL;
    g_nColsCount      = 0;
    g_pSubObjData     = NULL;
    g_pKeepChunk      = NULL;

    fp = (FILE *)(size_t)fileOpenMode(param_1, 0);
    if (fp == (FILE *)0xffffffff) return 0;

    snprintf(local_100, sizeof(local_100), "SCENERY %d", g_nSceneLoadCount);
    g_nSceneLoadCount = g_nSceneLoadCount + 1;
    pvPool = (void *)(size_t)memPoolCreate(local_100);

    /* Reset the mesh table for this load. */
    memset(g_pMeshTable, 0, sizeof(g_meshTableMem));
    g_pMeshTableWr    = g_pMeshTable;
    g_pMeshTableStart = g_pMeshTable;

    fileReadN(fp, (char *)hdr, 8);
    if (hdr[0] == 0x32564552) {                 /* "REV2" */
        local_124 = 0;
        if (0 < hdr[1]) {
            do {
                fileReadN(fp, (char *)chunk, 8);
                int tag  = chunk[0];
                int size = chunk[1];
                if (tag < 0x4d414e50) {
                    if (tag == 0x4d414e4f) {            /* ONAM */
                        g_pObjNameList = g_pSceneNameBufPos;
                        fileReadN(fp, g_pSceneNameBufPos, size);
                        g_pSceneNameBufPos = (char *)((int)g_pSceneNameBufPos + size);
                    } else if (tag < 0x494a4250) {
                        if (tag == 0x494a424f) {            /* OBJI */
                            pcVar5 = memPoolAlloc((int)pvPool, size);
                            g_pObjInstances = pcVar5;
                            fileReadN(fp, pcVar5, size);
                            g_nObjInstanceCount = size >> 5;
                        } else if (tag == 0x454d414e) {     /* EMAN (mesh name) */
                    char nbuf[256];
                    fileReadN(fp, nbuf, size);
                    if (g_pMeshTableStart < g_pMeshTableWr) {
                                *(char **)((int)g_pMeshTableWr - 8) = g_pMeshNameStr;
                            }
                            {
                                unsigned int d = (unsigned int)strlen(g_szSceneDir);
                                if (d) { memcpy(g_pMeshNameStr, g_szSceneDir, d); g_pMeshNameStr += d; }
                                unsigned int nm = (unsigned int)strlen(nbuf);
                                if (nm > 255) nm = 255;
                                memcpy(g_pMeshNameStr, nbuf, nm); g_pMeshNameStr += nm;
                                *g_pMeshNameStr++ = '\0';
                            }
                        } else if (tag == 0x4853454d) {     /* MESH */
                            pcVar5 = memPoolAlloc((int)pvPool, size);
                            fileReadN(fp, pcVar5, size);
                            *(char **)((int)g_pMeshTableWr + 4) = pcVar5;
                            *(int *)g_pMeshTableWr = 0;
                            g_pMeshTableWr = (void *)((int)g_pMeshTableWr + 8);
                            g_nMeshTableCount = g_nMeshTableCount + 1;
                        }
                    } else {
                        if (tag == 0x494e4154) {            /* TANI */
                            pcVar5 = memPoolAlloc((int)pvPool, size);
                            g_pTextAnimData = pcVar5;
                            g_nTextAnimSize = size;
                            fileReadN(fp, pcVar5, size);
                        } else if (tag == 0x4950414d) {      /* MAPI */
                            g_pMapGeom = memPoolAlloc((int)pvPool, size);
                            fileReadN(fp, (char *)g_pMapGeom, size);
                            g_nMapGeomCount = size >> 4;
                        }
                    }
                } else if (tag < 0x5045454c) {
                    if (tag == 0x5045454b) {                /* KEEP */
                        g_pKeepChunk = memPoolAlloc((int)pvPool, size);
                        fileReadN(fp, g_pKeepChunk, size);
                        senChunkParse((byte *)g_pKeepChunk, (byte *)(g_pKeepChunk + size));
                    } else {
                        if (tag == 0x4d414e54) {            /* TNAM */
                            g_pObjNameTable = memPoolAlloc(0, size);
                            g_nObjNameTableSize = size;
                            fileReadN(fp, g_pObjNameTable, size);
                        } else if (tag == 0x4f425553) {      /* SUBO */
                            g_pSubObjData = memPoolAlloc((int)pvPool, size);
                            fileReadN(fp, g_pSubObjData, size);
                        } else {
                            pcVar5 = memPoolAlloc((int)pvPool, size);
                            fileReadN(fp, pcVar5, size);
                        }
                    }
                } else if (tag == 0x504d4554) {             /* TEMP */
                    byte *pData = memPoolAlloc(0, size);
                    fileReadN(fp, (char *)pData, size);
                    senChunkParse(pData, pData + size);
                } else if (tag == 0x534c4f43) {             /* COLS */
                    g_pColsData = memPoolAlloc((int)pvPool, size);
                    fileReadN(fp, g_pColsData, size);
                    g_nColsCount = size >> 2;
                } else {
                    fileSeekTell(fp, size, 1);              /* unknown: skip */
                }
                local_124 = local_124 + 8 + size;
            } while (local_124 < hdr[1]);
        }
        fileCloseStream(fp);

        /* Relocate each loaded mesh node. */
        {
            int iVar4 = 0;
            int n = ((int)g_pMeshTableWr - (int)g_pMeshTableStart) >> 3;
            while (iVar4 < n) {
                sceneMeshFixup(*(int *)((int)g_pMeshTableStart + iVar4 * 8 + 4),
                               (char)(int)g_pObjNameTable, (int)&g_pMapGeom);
                iVar4++;
            }
        }

        /* Scene-graph population is out of scope for the menu preview. */
        sceneInstantiateObjects((int)pvPool);
        return (int)pvPool;
    }
    fileCloseStream(fp);
    memPoolDestroy((int)pvPool);
    return 0;
}

