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
 * SEN scene-file loader cluster. Faithful reimplementation of maniac.exe
 * 0x432xxx .SEN pipeline. See sen.h for map. Plate comments note
 * original addrs per AGENTS.md. Assembly verified (REP SCAS/MOVSD).
 *
 * For incremental milestone: file parsing + MESH/EMAN handling + mesh fixup
 * are faithful so CHARACTERS.SEN loads without fault. The post-fixup
 * object-instantiation stream (OBJI/TANI/MAPI expansion into live scene
 * nodes/emitters via sceneryObjAlloc/musicEmitterAlloc) is deferred —
 * sceneInstantiateObjects in stubs.c is a documented no-op that returns 1.
 * See docs/16-rebuild.md — gameplay remains after GUI states.
 * ===================================================================== */

/* --- scene-load globals (original addrs in comments) --- */
char   g_szSceneDir[256];               /* @0x45e950 scene dir (empty for menu) */
static char   g_meshNameBuf[8192];             /* backing for mesh-name strings */
static char  *g_pMeshNameStr = g_meshNameBuf;  /* @0x45eab4 running name cursor */
static unsigned char g_meshTableMem[256 * 8];  /* 8B entries {name*,data*} */
void  *g_pMeshTable   = g_meshTableMem;       /* @0x45e930 */
int   g_nMeshTableCount = 0;                  /* @0x45e994 (@0x45e944 alias) */
static void  *g_pMeshTableWr;                 /* @0x45e948 write cursor */
static void  *g_pMeshTableStart;              /* @0x45eab8 first new entry this load */
static char   g_sceneNameBuf[16384];          /* @0x45e94c buffer */
static char  *g_pSceneNameBufPos = g_sceneNameBuf; /* @0x45e94c pos */
static char  *g_pObjNameList = NULL;          /* @0x45e934 */
char  *g_pObjNameTable = NULL;         /* @0x45e990 */
int    g_nObjNameTableSize = 0;        /* @0x45eaa8 */
static char  *g_pObjInstances = NULL;         /* @0x45eaa4 */
static int    g_nObjInstanceCount = 0;        /* @0x45eaac */
static char  *g_pTextAnimData = NULL;         /* @0x45e940 */
static int    g_nTextAnimSize = 0;            /* @0x45eab0 */
int   *g_pMapGeom = NULL;              /* @0x45eb20 */
int    g_nMapGeomCount = 0;            /* @0x45eb24 */
static char  *g_pKeepChunk = NULL;            /* @0x45eaa0 */
char  *g_pSubObjData = NULL;           /* @0x45eb30 */
int    g_nColsCount = 0;               /* @0x45eb2c (count) */
char  *g_pColsData = NULL;             /* @0x45eb28 (data) */
static int    g_nSceneLoadCount = 0;          /* @0x45e938 */
static int    g_scenesceneLoadSen = 0;        /* @0x45e99c */
static int    g_nSceneMeshMaxSize = 0;        /* @0x45eb14 */
static int    g_scenesceneMeshFixup = 0;      /* @0x45eb18 */

/* sceneInstantiateObjects — deferred scene-graph population stub (see stubs.c).
 * Declared here for sceneLoadSen's tail call; real body will allocate
 * scenery/musics nodes from g_pObjInstances etc. when gameplay lands. */
extern int sceneInstantiateObjects(int pool);
extern int scenExpandNameList(char *pList, void *pEnd, char *pszDir); /* @0x432dd0 */
extern void sceneTextAnimAdd(void *pvPool, int *pMapGeom, char *pData, int nSize); /* @0x434a90 helper */

/* ---------------------------------------------------------------------
 * senChunkParse @0x432c00 — parse a chain of nested .sen chunk records
 * between pData and pDataEnd. Each record: {int tag, int size, data}.
 * Faithful tag dispatch; original uses REPNE SCASB + REP MOVSD/MOVSB.
 * Rebuild uses memcpy for UBSAN-clean unaligned safety (see src/anim.c
 * dataReadU8/16/32 precedent) with comment noting the inline rep.
 * ------------------------------------------------------------------- */
int senChunkParse(byte *pData, byte *pDataEnd) /* @0x432c00 */
{
    char *pSub;

    if (pDataEnd <= pData) return 1;
    do {
        int tag = *(int *)pData;
        unsigned int size = *(unsigned int *)(pData + 4);
        byte *pChunk = pData + 8;
        pSub = g_pSubObjData;

        if (tag < 0x4950414e) {
            if (tag == 0x4950414d) {                 /* MAPI */
                g_nMapGeomCount = size >> 4;
                g_pMapGeom = (int *)pChunk;
            } else if (tag < 0x494a4250) {
                if (tag == 0x494a424f) {             /* OBJI — 'OBJI' little-endian 0x494a424f */
                    g_nObjInstanceCount = size >> 5;
                    g_pObjInstances = (char *)pChunk;
                } else if (tag == 0x454d414e) {      /* EMAN (mesh name) */
                    if (g_pMeshTableStart < g_pMeshTableWr) {
                        *(char **)((int)g_pMeshTableWr - 8) = g_pMeshNameStr;
                    }
                    /* Original: REPNE SCASB to get len of g_szSceneDir, then REP MOVSD/MOVSB */
                    {
                        size_t d = strlen(g_szSceneDir);
                        if (d) { memcpy(g_pMeshNameStr, g_szSceneDir, d); g_pMeshNameStr += d; }
                        size_t nm = strlen((char *)pChunk);
                        memcpy(g_pMeshNameStr, pChunk, nm); g_pMeshNameStr += nm;
                        *g_pMeshNameStr++ = '\0';
                    }
                    pSub = g_pSubObjData;
                } else if (tag == 0x4853454d) {      /* MESH */
                    *(byte **)((int)g_pMeshTableWr + 4) = pChunk;
                    *(int *)g_pMeshTableWr = 0;
                    g_pMeshTableWr = (void *)((int)g_pMeshTableWr + 8);
                    pSub = g_pSubObjData;
                }
            } else if (tag == 0x494e4154) {          /* TANI */
                g_pTextAnimData = (char *)pChunk;
                g_nTextAnimSize = size;
            }
        } else if (tag < 0x4f425554) {
            pSub = (char *)pChunk;
            if (tag != 0x4f425553) {                 /* not SUBO */
                if (tag == 0x4d414e4f) {             /* ONAM */
                    g_pObjNameList = g_pSceneNameBufPos;
                    /* Original: SHR ECX,2; REP MOVSD; AND 3; REP MOVSB */
                    memcpy(g_pSceneNameBufPos, pChunk, size);
                    g_pSceneNameBufPos = (void *)((int)g_pSceneNameBufPos + size);
                    pSub = g_pSubObjData;
                } else {                              /* TNAM */
                    pSub = g_pSubObjData;
                    if (tag == 0x4d414e54) {
                        g_pObjNameTable = (char *)pChunk;
                        g_nObjNameTableSize = size;
                    }
                }
            }
        } else if (tag == 0x534c4f43) {              /* COLS */
            g_nColsCount = size >> 2;
            g_pColsData = (char *)pChunk;
        }
        g_pSubObjData = pSub;
        pData = pChunk + size;
    } while (pDataEnd > pData);
    return 1;
}

/* ---------------------------------------------------------------------
 * sceneMeshFixup @0x4320f0 — relocate pointer fields of a loaded mesh
 * node. pMesh = node base, pNames = object-name table, pMapGeom =
 * &g_pMapGeom (or 0). Relocates +0xc/+0x10/+0x14/+0x20/+0x28/+0x30 and
 * per-subobj at pRender+8 stride 0x30. Original asm at 0x4320f0.
 * Returns 1 (original checks CMP EAX,1). Faithful to disassembly
 * @0x004320f0 (PUSH/POP, LEA, TEST/JNZ branches preserved in C).
 * ------------------------------------------------------------------- */
int sceneMeshFixup(int pMesh, void *pNames, int pMapGeom) /* @0x4320f0 */
{
    int idx;
    int tmp;
    int *pRender;

    (void)pNames; /* only forwarded to sceneCreateTextureSurfaces when pMapGeom empty */

    if (*(int *)(pMesh + 8) > 0) {
        *(int *)(pMesh + 0xc) = *(int *)(pMesh + 0xc) + pMesh;
        *(int *)(pMesh + 0x10) = *(int *)(pMesh + 0x10) + pMesh;
    }
    tmp = *(int *)(pMesh + 0x14) + pMesh;
    *(int *)(pMesh + 0x14) = tmp;

    if (pMapGeom == 0) {
        *(int *)(pMesh + 0x20) = *(int *)(pMesh + 0x20) + pMesh;
        *(int *)(pMesh + 0x28) = *(int *)(pMesh + 0x28) + pMesh;
    } else {
        if (*(int *)pMapGeom == 0) {
            *(int *)(pMesh + 0x20) = *(int *)(pMesh + 0x20) + pMesh;
        } else {
            *(int *)(pMesh + 0x20) = *(int *)(pMesh + 0x20) + *(int *)pMapGeom;
        }
        if (*(int *)(pMapGeom + 8) == 0) {
            *(int *)(pMesh + 0x28) = *(int *)(pMesh + 0x28) + pMesh;
        } else {
            *(int *)(pMesh + 0x28) = *(int *)(pMesh + 0x28) + *(int *)(pMapGeom + 8);
        }
    }

    idx = 0;
    *(int *)(pMesh + 0x30) = *(int *)(pMesh + 0x30) + pMesh;

    if (*(int *)(pMesh + 4) > 0) {
        pRender = (int *)(tmp + 8);
        do {
            *pRender = *pRender + pMesh;
            if (pRender[1] != 0) {
                pRender[2] = pRender[2] + pMesh;
            }
            if (pRender[8] != 0) {
                pRender[9] = pRender[9] + pMesh;
            }
            pRender[4] = pRender[4] + pMesh;
            pRender[6] = pRender[6] + pMesh;
            {
                int *pVerts = (int *)pRender[4];
                int nFix = pRender[3] + pRender[7];
                int base;
                if (pMapGeom == 0) {
                    base = pMesh;
                } else if (*(int *)(pMapGeom + 0x10) == 0) {
                    base = pMesh;
                } else {
                    base = *(int *)(pMapGeom + 0x10);
                }
                if (nFix > 0) {
                    int k = 0;
                    do {
                        *pVerts = *pVerts + base;
                        pVerts++;
                        k++;
                    } while (k < nFix);
                }
            }
            idx++;
            pRender += 0xc; /* 0x30 bytes = 12 ints */
        } while (idx < *(int *)(pMesh + 4));
    }

    if ((pMapGeom == 0) || (*(int *)pMapGeom == 0)) {
        sceneCreateTextureSurfaces(*(int **)(pMesh + 0x20),
                                   *(int *)(pMesh + 0x1c), (char *)pNames);
    }

    tmp = *(int *)(*(int *)(pMesh + 0x14) + 4);
    if (g_nSceneMeshMaxSize < tmp) g_nSceneMeshMaxSize = tmp;
    if (tmp > 0x100) g_scenesceneMeshFixup++;
    return 1;
}

/* ---------------------------------------------------------------------
 * sceneCreateTextureSurfaces @0x432260 — bind texture ids to surfaces.
 * Original: scan pTexIdList (stride 0x10) for max id, create max+1 surfaces
 * via gxCreateSurface walking pszFilenames (packed NUL list), then remap ids.
 * Returns 1 on success, 0 on gxCreateSurface failure. Faithful to disasm:
 * REPNE SCASB to walk packed names, stride 0x10 scan, stack surfTab[64] at
 * [ESP+0x14]. No on-demand TPG fallback — caller must have loaded textures
 * via gxLoadTexture/gxLoadTpgFile before this call.
 * ------------------------------------------------------------------- */
int sceneCreateTextureSurfaces(int *pTexIdList, int nCount, char *pszFilenames) /* @0x432260 */
{
    int maxId = -1;

    if (nCount > 0) {
        int *p = pTexIdList;
        int rem = nCount;
        do {
            if (maxId < *p) maxId = *p;
            rem--;
            p = (int *)((char *)p + 0x10);
        } while (rem != 0);
    }


    /* stack surf table: original SUB ESP,0x104 with 64 entries at [ESP+0x14] */
    {
        int nSurfs = maxId + 1;
        int surfTab[64];
        char *psz = pszFilenames;
        int i = 0;

        if (nSurfs > 0) {
            int *pTab = surfTab;
            if (nSurfs > 64) nSurfs = 64;
            do {
                int surf = gxCreateSurface(psz);
                *pTab = surf;
                if (surf == 0) return 0;
                /* REPNE SCASB: walk to next NUL in packed list */
                if (psz) {
                    size_t l = strlen(psz);
                    psz += l + 1;
                }
                pTab++;
                i++;
            } while (i < nSurfs);
        }

        if (nCount > 0) {
            int *p = pTexIdList;
            int rem = nCount;
            do {
                int id = *p;
                if (id >= 0 && id < 64 && id <= maxId) {
                    *p = surfTab[id];
                }
                rem--;
                p = (int *)((char *)p + 0x10);
            } while (rem != 0);
        }
    }
    return 1;
}

/* ---------------------------------------------------------------------
 * sceneLoadSen @0x432320 — open REV2 .SEN, iterate chunks, fixup meshes.
 * Incremental: faithful through REV2 check + chunk walk + mesh fixup +
 * file-close. Object instantiation (OBJI/scene graph) is deferred to
 * sceneInstantiateObjects stub (returns 1) so CHARACTERS.SEN (MESH+EMAN only)
 * completes without requiring sceneryObjAlloc path.
 * ------------------------------------------------------------------- */
int sceneLoadSen(LPCSTR pszPath, int *pOut) /* @0x432320 */
{
    FILE *fp;
    void *pvPool;
    int hdr[2];               /* [0]=magic REV2, [1]=total */
    int chunk[2];             /* [0]=tag, [1]=size */
    char local_100[256];
    int local_124 = 0;
    char *pcVar5;

    (void)pOut; /* unused for menu preview; original writes to it for type==1 OBJI */

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

    fp = (FILE *)(size_t)fileOpenMode(pszPath, 0);
    if (fp == (FILE *)0xffffffff) return 0;

    snprintf(local_100, sizeof(local_100), "SCENERY %d", g_nSceneLoadCount);
    g_nSceneLoadCount++;
    pvPool = (void *)(size_t)memPoolCreate(local_100);

    /* Mesh table cursors — original scans g_pMeshTable for first zero entry
     * (004323e3-0043240d). Rebuild uses count-derived cursor but keeps same
     * semantics: Wr = base + firstFree*8, Start = Wr on entry. */
    {
        int firstFree = 0;
        if (g_nMeshTableCount > 0) {
            int *p = (int *)((char *)g_pMeshTable + 4);
            while (firstFree < g_nMeshTableCount) {
                if (*p == 0) break;
                firstFree++;
                p += 2;
            }
        }
        g_pMeshTableWr    = (void *)((char *)g_pMeshTable + firstFree * 8);
        g_pMeshTableStart = g_pMeshTableWr;
    }

    fileReadN(fp, (char *)hdr, 8);
    if (hdr[0] == 0x32564552) {                 /* "REV2" */
        local_124 = 0;
        if (hdr[1] > 0) {
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
                        if (tag == 0x494a424f) {            /* OBJI — deferred but load bytes */
                            pcVar5 = memPoolAlloc((int)(intptr_t)pvPool, size);
                            if (pcVar5) {
                                fileReadN(fp, pcVar5, size);
                                g_pObjInstances = pcVar5;
                                g_nObjInstanceCount = size >> 5;
                            }
                        } else if (tag == 0x454d414e) {     /* EMAN */
                            char nbuf[256];
                            fileReadN(fp, nbuf, size);
                            if (g_pMeshTableStart < g_pMeshTableWr) {
                                *(char **)((int)g_pMeshTableWr - 8) = g_pMeshNameStr;
                            }
                            {
                                size_t d = strlen(g_szSceneDir);
                                if (d) { memcpy(g_pMeshNameStr, g_szSceneDir, d); g_pMeshNameStr += d; }
                                size_t nm = strlen(nbuf);
                                if (nm > 255) nm = 255;
                                memcpy(g_pMeshNameStr, nbuf, nm); g_pMeshNameStr += nm;
                                *g_pMeshNameStr++ = '\0';
                            }
                        } else if (tag == 0x4853454d) {     /* MESH */
                            pcVar5 = memPoolAlloc((int)(intptr_t)pvPool, size);
                            if (pcVar5) {
                                fileReadN(fp, pcVar5, size);
                                *(char **)((int)g_pMeshTableWr + 4) = pcVar5;
                                *(int *)g_pMeshTableWr = 0;
                                g_pMeshTableWr = (void *)((int)g_pMeshTableWr + 8);
                                g_nMeshTableCount++;
                            }
                        }
                    } else {
                        if (tag == 0x494e4154) {            /* TANI */
                            pcVar5 = memPoolAlloc((int)(intptr_t)pvPool, size);
                            if (pcVar5) {
                                g_pTextAnimData = pcVar5;
                                g_nTextAnimSize = size;
                                fileReadN(fp, pcVar5, size);
                            }
                        } else if (tag == 0x4950414d) {     /* MAPI */
                            g_pMapGeom = memPoolAlloc((int)(intptr_t)pvPool, size);
                            if (g_pMapGeom) {
                                fileReadN(fp, (char *)g_pMapGeom, size);
                                g_nMapGeomCount = size >> 4;
                            }
                        }
                    }
                } else if (tag < 0x5045454c) {
                    if (tag == 0x5045454b) {                /* KEEP */
                        g_pKeepChunk = memPoolAlloc((int)(intptr_t)pvPool, size);
                        if (g_pKeepChunk) {
                            fileReadN(fp, g_pKeepChunk, size);
                            senChunkParse((byte *)g_pKeepChunk, (byte *)(g_pKeepChunk + size));
                        }
                    } else {
                        if (tag == 0x4d414e54) {            /* TNAM — pool 0 per original */
                            g_pObjNameTable = memPoolAlloc(0, size);
                            if (g_pObjNameTable) {
                                g_nObjNameTableSize = size;
                                fileReadN(fp, g_pObjNameTable, size);
                            }
                        } else if (tag == 0x4f425553) {      /* SUBO */
                            g_pSubObjData = memPoolAlloc((int)(intptr_t)pvPool, size);
                            if (g_pSubObjData) fileReadN(fp, g_pSubObjData, size);
                        } else {
                            pcVar5 = memPoolAlloc((int)(intptr_t)pvPool, size);
                            if (pcVar5) fileReadN(fp, pcVar5, size);
                        }
                    }
                } else if (tag == 0x504d4554) {             /* TEMP — pool 0 */
                    byte *pData = memPoolAlloc(0, size);
                    if (pData) {
                        fileReadN(fp, (char *)pData, size);
                        senChunkParse(pData, pData + size);
                        memPoolFree(0, pData);
                    }
                } else if (tag == 0x534c4f43) {             /* COLS */
                    g_pColsData = memPoolAlloc((int)(intptr_t)pvPool, size);
                    if (g_pColsData) {
                        fileReadN(fp, g_pColsData, size);
                        g_nColsCount = size >> 2;
                    }
                } else {
                    fileSeekTell(fp, size, 1);              /* unknown: skip */
                }
                local_124 += 8 + size;
            } while (local_124 < hdr[1]);
        }
        fileCloseStream(fp);
        /* scenExpandNameList @0x432dd0 — original expands ONAM using g_szSceneDir
         * if both present (disasm @0x0043281e). Faithful. */
        if (g_pObjNameList != NULL && g_szSceneDir[0] != '\0') {
            int n = scenExpandNameList(g_pObjNameList, g_pSceneNameBufPos, g_szSceneDir);
            g_pSceneNameBufPos = (void *)((int)g_pSceneNameBufPos + n);
        }
        /* Relocate each newly loaded mesh. Original passes 0x45eb20 (address
         * of the contiguous global block {g_pMapGeom, g_nMapGeomCount,
         * g_pColsData, g_nColsCount, g_pSubObjData} at 0x45eb20..0x45eb30).
         * The rebuild keeps these as separate C globals, so pack them into
         * a local struct that mirrors the original memory layout before
         * passing its address — sceneMeshFixup reads +0x00 (MAPI),
         * +0x08 (COLS), +0x10 (SUBO) from this pointer.
         * Disasm @0x00432857 checks return ==1. */
        {
            int n = ((int)g_pMeshTableWr - (int)g_pMeshTableStart) >> 3;
            int i;
            struct { int *pMapGeom; int nMapGeomCount; char *pColsData; int nColsCount; char *pSubObjData; } geom;
            geom.pMapGeom = g_pMapGeom;
            geom.nMapGeomCount = g_nMapGeomCount;
            geom.pColsData = g_pColsData;
            geom.nColsCount = g_nColsCount;
            geom.pSubObjData = g_pSubObjData;
            for (i = 0; i < n; i++) {
                int pMesh = *(int *)((int)g_pMeshTableStart + i * 8 + 4);
                if (pMesh) {
                    int r = sceneMeshFixup(pMesh, g_pObjNameTable, (int)&geom);
                    if (r != 1) {
                        memPoolDestroy((int)(intptr_t)pvPool);
                        return 0;
                    }
                }
            }
        }
        /* Global MAPI texture binding @0x004328a5:
         * if (_g_pMapGeom==0 || sceneCreateTextureSurfaces(_g_pMapGeom,_g_nMapGeomCount,g_pObjNameTable)!=0)
         * then proceed. This is the TNAM→TPG step for CHARACTERS.SEN (MERGED00-10).
         * For verification we log counts and handle failure faithfully. */
        if (g_pMapGeom != NULL) {
            int ok = sceneCreateTextureSurfaces(g_pMapGeom, g_nMapGeomCount, g_pObjNameTable);
            if (!ok) {
                appLog("[sen] sceneCreateTextureSurfaces failed for '%s' MAPI %d TNAM %d", pszPath, g_nMapGeomCount, g_nObjNameTableSize);
                memPoolDestroy((int)(intptr_t)pvPool);
                return 0;
            }
            appLog("[sen] MAPI textures bound: %d entries via TNAM %d bytes (%s)", g_nMapGeomCount, g_nObjNameTableSize, pszPath);
        }
        if (g_pTextAnimData != NULL && g_pMapGeom != NULL) {
            sceneTextAnimAdd(pvPool, g_pMapGeom, g_pTextAnimData, g_nTextAnimSize);
        }
        /* Verification logging for SEN+TPG step (pre-render): mesh count,
         * MAPI/COLS/SUBO sizes. Original would continue to OBJI instantiation
         * (sceneryObjAlloc loop @0x0043293d) — deferred for menu preview. */
        {
            int nMesh = ((int)g_pMeshTableWr - (int)g_pMeshTableStart) >> 3;
            int suboSize = g_pSubObjData ? 0 : 0; /* SUBO presence, size tracked via g_pMapGeomCount handling */
            /* derive SUBO size via file header: total - known chunks is not tracked; log presence */
            appLog("[sen] loaded '%s' REV2 %d bytes: %d MESH, MAPI %d (16B), COLS %d, SUBO %s, TNAM %dB, OBJI %d", pszPath, hdr[1], nMesh, g_nMapGeomCount, g_nColsCount, g_pSubObjData?"present":"none", g_nObjNameTableSize, g_nObjInstanceCount);
            (void)suboSize;
        }
        /* Deferred instantiate — keep no-op for CHARACTERS.SEN's OBJI (36)
         * until render milestone, but call stub to preserve hierarchy. */
        sceneInstantiateObjects((int)(intptr_t)pvPool);
        return (int)(intptr_t)pvPool;
    }
    fileCloseStream(fp);
    memPoolDestroy((int)(intptr_t)pvPool);
    return 0;
}
