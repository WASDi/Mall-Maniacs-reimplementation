#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sen.h"
#include "gx.h"
#include "scene.h"
#include "custom_helpers.h"
#include "charselect.h"
#include "util.h"
#include "pool.h"
#include "scene_text.h"

/* =====================================================================
 * SEN scene-file loader cluster. Faithful reimplementation of maniac.exe
 * 0x432xxx .SEN pipeline. See sen.h for map. Plate comments note
 * original addrs per AGENTS.md. Assembly verified (REP SCAS/MOVSD).
 *
 * For incremental milestone: file parsing + MESH/EMAN handling + mesh fixup
 * are faithful so CHARACTERS.SEN loads without fault. The post-fixup
 * OBJI instantiation is implemented inline in sceneLoadSen (original loop
 * @0x00432900-0x00432ba1): scene nodes are created via sceneNodeAllocChild
 * (type 3) / sceneryObjAlloc (type 1) and {name,node} entries append to
 * g_pScenObjTable.
 * ===================================================================== */

/* --- scene-load globals (original addrs in comments) --- */
char   g_szSceneDir[256];               /* @0x45e950 scene dir (empty for menu) */
static char  *g_pMeshNameStr;                  /* @0x45eab4 running name cursor */
void  *g_pMeshTable;                            /* @0x45e930 */
int   g_nMeshTableCount = 0;                  /* @0x45e994 */
static void  *g_pMeshTableWr;                 /* @0x45e948 write cursor */
static void  *g_pMeshTableStart;              /* @0x45eab8 first new entry this load */
static char  *g_pSceneNameBufPos;             /* @0x45e94c write cursor into scene name arena */
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
static int    g_nSceneNamePool = -1;          /* @0x45e998 pool created by scenNameTableInit */
static char  *g_pMeshNameArena = NULL;        /* @0x45e93c mesh-name arena start */
static char  *g_pSceneNameArena = NULL;       /* @0x45eabc scene-name arena start */
static int    g_nScenObjCap = 0;              /* @0x45eac0 scene-obj table capacity */
ScenNameEntry *g_pScenObjTable = NULL;        /* @0x45eb10 {name,node} scene-object table */
/* Stat pad zeroed by scenNameTableInit @0x431db4 (16 dwords @0x45eac4 and
 * two ints @0x45eb08/0x45eb0c). The OBJI instantiation loop in sceneLoadSen
 * reads g_nSceneTblStatA/B as the NULL-parent fallback (@0x0043297e/
 * @0x00432988) and scans g_anSceneTblStat for the music-emitter check
 * (@0x004329df) — scenNameTableInit is the only writer of all three
 * (xref-verified), so at instantiation time they are always zero. */
static int    g_anSceneTblStat[16];           /* @0x45eac4 */
static int    g_nSceneTblStatA;               /* @0x45eb08 */
static int    g_nSceneTblStatB;               /* @0x45eb0c */
static int    g_scenesceneLoadSen = 0;        /* @0x45e99c */
static int    g_nSceneMeshMaxSize = 0;        /* @0x45eb14 */
static int    g_scenesceneMeshFixup = 0;      /* @0x45eb18 */

/* scenSetDir @0x432e60 — copy pszDir into g_szSceneDir @0x45e950 (REP
 * MOVSD/MOVSB over strlen+1); a NULL pointer clears it. Returns 1. */
int scenSetDir(LPCSTR pszDir) /* @0x432e60 */
{
    if (pszDir != NULL) {
        size_t d = strlen(pszDir) + 1;       /* SCASB length incl. NUL */
        if (d > 256) d = 256;
        memcpy(g_szSceneDir, pszDir, d);
    } else {
        g_szSceneDir[0] = '\0';
    }
    return 1;
}

/* scenExpandNameList @0x432dd0 — expand the packed ONAM copy in the scene
 * name arena by inserting pszDir in front of EVERY name (in-place backward
 * copy; each entry becomes "<dir><name>\0"). Returns the number of bytes
 * added (= strlen(dir) * name count); sceneLoadSen advances the arena
 * cursor by it. The OBJI name walk (@0x00432935) then navigates the
 * expanded list via its +dir-per-NUL stride, so the per-instance scen-obj
 * names come out dir-prefixed — roundStartInit's hide pass matches the
 * loaded objects.sen/characters.sen instances against "__HIDE_ME__"
 * (@0x44f23c) through exactly these names.
 * Verified against the original by simulating both the expansion and the
 * subsequent name walk on scene_ica CHARACTERS.SEN/OBJECTS.SEN: all 78
 * instances resolve to "__HIDE_ME__<name>". */
int scenExpandNameList(char *pList, void *pEnd, char *pszDir) /* @0x432dd0 */
{
    int d;
    int nNames;
    int nExpand;
    int i;
    char *pCur;
    char *pWrite;
    char *pRead;
    char *pPrev;
    char c;

    if (pszDir == NULL || pList == NULL || pEnd == NULL) return 0;
    d = strlen(pszDir);
    nNames = 0;
    for (pCur = pList; pCur < (char *)pEnd; pCur++) {
        if (*pCur == '\0') nNames++;
    }
    nExpand = d * nNames;
    pWrite = (char *)pEnd + nExpand - 1;
    pRead = (char *)pEnd - 1;
    while (pList <= pRead) {
        *pWrite = *pRead;                    /* the name's NUL */
        c = (pRead - 1 >= pList) ? pRead[-1] : '\0';
        pPrev = pRead;
        for (;;) {
            pRead = pPrev - 1;
            pWrite--;
            if (c == '\0' || pRead < pList) break;
            *pWrite = c;
            c = (pPrev - 2 >= pList) ? pPrev[-2] : '\0';
            pPrev = pRead;
        }
        for (i = d - 1; i >= 0; i--) {
            *pWrite = pszDir[i];
            pWrite--;
        }
    }
    return nExpand;
}

/* scenNameTableInit @0x431cb0 — create the pool-backed mesh/name tables used
 * by the SEN loader (pool name "SCENE" @0x450fa8). Four pool allocations in
 * original order: mesh table (nMeshCount*8 @0x45e930), scene-object table
 * ((nScenObjCap+1)*8 @0x45eb10), scene-name arena (nScenObjCap*32 @0x45eabc,
 * cursor @0x45e94c), mesh-name arena (nMeshCount*32 @0x45e93c, cursor
 * @0x45eab4). The +4 (node/handle) fields of mesh-table entries 1..count-1
 * and scene-obj entries 1..cap are cleared; the 0x45eac4 stat pad and
 * 0x45eb08/0x45eb0c are zeroed; g_nSceneLoadCount @0x45e938 resets. Any
 * allocation failure destroys the pool and returns 0. */
int scenNameTableInit(int nMeshCount, int nScenObjCap)
{
    int nPool;
    ScenNameEntry *pMeshTable;
    ScenNameEntry *pScenObjTable;
    char *pSceneNames;
    char *pMeshNames;

    nPool = memPoolCreate("SCENE");               /* @0x431cb8 */
    g_nSceneNamePool = nPool;
    if (nPool < 0) return 0;

    pMeshTable = (ScenNameEntry *)memPoolAlloc(nPool, (size_t)nMeshCount * 8);              /* @0x431cdc */
    pScenObjTable = (ScenNameEntry *)memPoolAlloc(nPool, (size_t)nScenObjCap * 8 + 8);      /* @0x431cf8 */
    pSceneNames = memPoolAlloc(nPool, (size_t)nScenObjCap * 32);           /* @0x431d0f */
    pMeshNames = memPoolAlloc(nPool, (size_t)nMeshCount * 32);             /* @0x431d26 */
    if (pMeshTable == NULL || pScenObjTable == NULL ||
        pSceneNames == NULL || pMeshNames == NULL) {                       /* @0x431d34 */
        memPoolDestroy(nPool);
        g_nSceneNamePool = -1;
        return 0;
    }

    g_pMeshTable = pMeshTable;              /* @0x45e930 */
    g_pScenObjTable = pScenObjTable;        /* @0x45eb10 */
    g_pSceneNameArena = pSceneNames;        /* @0x45eabc */
    g_pSceneNameBufPos = pSceneNames;       /* @0x45e94c */
    g_pMeshNameArena = pMeshNames;          /* @0x45e93c */
    g_pMeshNameStr = pMeshNames;            /* @0x45eab4 */
    g_nMeshTableCount = nMeshCount;         /* @0x45e994 */
    g_nScenObjCap = nScenObjCap;            /* @0x45eac0 */
    /* The original clears only the id (+4) fields of mesh entries 1..count-1
     * (@0x431d7c) and scene-obj entries 1..cap (@0x431d9e); entry 0 and the
     * name fields rely on the pool's underlying zero-filled allocation. The
     * rebuild's memPoolAlloc wraps malloc, so both tables are zeroed fully
     * for an identical starting state. */
    memset(pMeshTable, 0, (size_t)nMeshCount * 8);
    memset(pScenObjTable, 0, (size_t)nScenObjCap * 8 + 8);
    g_nSceneLoadCount = 0;                  /* @0x45e938 @0x431dc0 */
    memset(g_anSceneTblStat, 0, sizeof(g_anSceneTblStat));   /* @0x431dbb */
    g_nSceneTblStatA = 0;                   /* @0x45eb08 @0x431dc9 */
    g_nSceneTblStatB = 0;                   /* @0x45eb0c @0x431dcf */
    return 1;
}

/* scenNameTableFree @0x431e00 — destroy the scenNameTableInit pool
 * (handle @0x45e998). Returns 1. */
int scenNameTableFree(void) /* @0x431e00 */
{
    if (g_nSceneNamePool >= 0) {
        memPoolDestroy(g_nSceneNamePool);
        g_nSceneNamePool = -1;
    }
    return 1;
}

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
 * sceneLoadSen @0x432320 — open REV2 .SEN, iterate chunks, fixup meshes,
 * then instantiate the OBJI objects into the live scene graph (inline
 * loop @0x00432900-0x00432ba1).
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
                            /* Mesh name stored VERBATIM (no scene-dir prefix —
                             * dir expansion is ONAM-only via scenExpandNameList).
                             * Gives the previous mesh entry its name, then copies
                             * the chunk string into the mesh-name arena. */
                            char nbuf[256];
                            fileReadN(fp, nbuf, size);
                            if (g_pMeshTableStart < g_pMeshTableWr) {
                                *(char **)((int)g_pMeshTableWr - 8) = g_pMeshNameStr;
                            }
                            {
                                size_t nm = strlen(nbuf) + 1;
                                if (nm > 256) nm = 256;
                                memcpy(g_pMeshNameStr, nbuf, nm);
                                g_pMeshNameStr += nm;
                            }
                        } else if (tag == 0x4853454d) {     /* MESH */
                            pcVar5 = memPoolAlloc((int)(intptr_t)pvPool, size);
                            if (pcVar5) {
                                fileReadN(fp, pcVar5, size);
                                *(char **)((int)g_pMeshTableWr + 4) = pcVar5;
                                *(int *)g_pMeshTableWr = 0;
                                g_pMeshTableWr = (void *)((int)g_pMeshTableWr + 8);
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
         * MAPI/COLS/SUBO sizes. */
        {
            int nMesh = ((int)g_pMeshTableWr - (int)g_pMeshTableStart) >> 3;
            int suboSize = g_pSubObjData ? 0 : 0; /* SUBO presence, size tracked via g_pMapGeomCount handling */
            /* derive SUBO size via file header: total - known chunks is not tracked; log presence */
            appLog("[sen] loaded '%s' REV2 %d bytes: %d MESH, MAPI %d (16B), COLS %d, SUBO %s, TNAM %dB, OBJI %d", pszPath, hdr[1], nMesh, g_nMapGeomCount, g_nColsCount, g_pSubObjData?"present":"none", g_nObjNameTableSize, g_nObjInstanceCount);
            (void)suboSize;
        }
        /* OBJI instantiation — original inline loop @0x00432900-0x00432ba1.
         * Appends one {name,node} entry per instance to g_pScenObjTable
         * (persists across the loads of one scene-system cycle) and creates
         * the scene node: type 3 -> sceneNodeAllocChild @0x4319e0, type 1 ->
         * sceneryObjAlloc @0x430200 with the instance's mesh-table entry.
         * Instance layout (32 bytes): +0x00 ONAM string index, +0x04 type,
         * +0x08 parent node (0 -> g_nSceneTblStatA @0x45eb08, always zero),
         * +0x0c/+0x10/+0x14 float pos (__ftol), +0x18/+0x1a/+0x1c yaw/pitch/
         * roll shorts, +0x1e mesh index (type 1). pOut collects {node,x,y,z}
         * quads per type-1 instance (item spawn positions). */
        {
            size_t d = strlen(g_szSceneDir);
            byte *pInst = (byte *)g_pObjInstances;
            ScenNameEntry *pEntry;
            int nExisting = 0;
            int iInst;

            while (g_pScenObjTable[nExisting].nId != 0) nExisting++;   /* @0x00432900 */
            pEntry = &g_pScenObjTable[nExisting];
            for (iInst = 0; iInst < g_nObjInstanceCount;
                 iInst++, pInst += 0x20, pEntry++) {
                char *pName = g_pObjNameList + d;                      /* @0x00432959 */
                int nameIdx = *(int *)pInst;
                SceneNode *pNode = NULL;
                int nChanArg;

                if (nameIdx > 0) {                                     /* @0x0043295e */
                    do {
                        char c = *pName++;
                        if (c == '\0') pName += d;
                    } while (--nameIdx != 0);
                }
                pName -= d;                                            /* @0x0043296a */
                pEntry->pszName = pName;
                if (*(int *)(pInst + 8) == 0) {                        /* @0x00432978 */
                    *(int *)(pInst + 8) = g_nSceneTblStatA;            /* zero @0x45eb08 */
                    nChanArg = g_nSceneTblStatB;                       /* zero @0x45eb0c */
                } else {
                    nChanArg = 0;
                }
                if (*(int *)(pInst + 4) == 3) {                        /* @0x004329bc */
                    /* Music-emitter branch @0x004329cd matches the object
                     * name against the pattern strings @0x45e9a0 only for
                     * non-zero g_anSceneTblStat entries (@0x45eac4). The
                     * init zero-fill is their only writer (xref-verified),
                     * so the scan exits at index 0 and every type-3
                     * instance takes the child-node path @0x00432a5e. */
                    pNode = (SceneNode *)sceneNodeAllocChild(
                        (SceneNode *)(uintptr_t)*(int *)(pInst + 8),
                        (void *)(uintptr_t)nChanArg,
                        (void *)(uintptr_t)(int)(*(float *)(pInst + 0x0c)),
                        (void *)(uintptr_t)(int)(*(float *)(pInst + 0x10)),
                        (void *)(uintptr_t)(int)(*(float *)(pInst + 0x14)));
                    pEntry->nId = (int)(uintptr_t)pNode;
                    if (pNode != NULL) {
                        sceneObjSetPosOrient(pNode,                    /* @0x00432ab2 */
                                             *(short *)(pInst + 0x18),
                                             *(short *)(pInst + 0x1a),
                                             *(short *)(pInst + 0x1c), 2);
                    }
                } else if (*(int *)(pInst + 4) == 1) {                 /* @0x00432abf */
                    short nMeshIdx = *(short *)(pInst + 0x1e);
                    void *pMesh = *(void **)((char *)g_pMeshTableStart + nMeshIdx * 8 + 4);
                    pNode = (SceneNode *)sceneryObjAlloc(
                        (SceneNode *)(uintptr_t)*(int *)(pInst + 8), nChanArg,
                        (int)(*(float *)(pInst + 0x0c)), (int)(*(float *)(pInst + 0x10)),
                        (int)(*(float *)(pInst + 0x14)),
                        *(short *)(pInst + 0x18), *(short *)(pInst + 0x1a),
                        *(short *)(pInst + 0x1c), pMesh);
                    pEntry->nId = (int)(uintptr_t)pNode;
                    if (pOut != NULL) {                                /* @0x00432b2b */
                        int *pDst = *(int **)pOut;
                        pDst[0] = (int)(uintptr_t)pNode;
                        pDst[1] = (int)(*(float *)(pInst + 0x0c));
                        pDst[2] = (int)(*(float *)(pInst + 0x10));
                        pDst[3] = (int)(*(float *)(pInst + 0x14));
                        *(int **)pOut = pDst + 4;
                    }
                }
                if (pNode == NULL) {                                   /* @0x00432b7c */
                    memPoolDestroy((int)(intptr_t)pvPool);
                    return 0;
                }
            }
            appLog("[sen] instantiated %d OBJI objects (%s)", g_nObjInstanceCount, pszPath);
        }
        return (int)(intptr_t)pvPool;
    }
    fileCloseStream(fp);
    memPoolDestroy((int)(intptr_t)pvPool);
    return 0;
}
