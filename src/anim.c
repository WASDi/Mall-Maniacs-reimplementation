#include "compat_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>

#include "anim.h"
#include "pool.h"
#include "util.h"
#include "scene.h"
#include "custom_helpers.h"

/* =====================================================================
 * .anm animation cluster — faithful reimplementation of maniac.exe
 * 0x433xxx/0x434xxx. See anim.h for map. Every function retains its
 * original address in the comment above its definition.
 *
 * On-disk .anm (versions 1 and 2, identical parsing):
 *   0: "ANM" + version byte + u16 trackNameCount + trackNameCount * {u8 len, bytes}
 *   + u16 channelCount + channelCount * {u8 len, bytes, u16 channelVal}
 *   + u16 nTracks + nTracks * {u16 nRecs, nRecs * records}
 * Records are typed 1..6, variable length on disk (see anmCalcSize).
 * Internal representation expands each to fixed 0x10/0x14/8 bytes so the
 * stepper can dispatch without further length decoding.
 *
 * Arena layout (one memPoolAlloc from g_pAnmCacheList):
 *   [0x00,0x38) AnmFile header
 *   [0x38, 0x38+nTrack*8) track table (nRecs, pRecs per track)
 *   [rest) record data (packed, variable stride per type)
 *
 * Pool use is faithful (memPoolCreate/Alloc/Free/Destroy) — the rebuild's
 * pool.c maps these to malloc/free internally, satisfying AGENTS.md's
 * "use malloc/free instead of memPool" while preserving call hierarchy.
 * ===================================================================== */

/* globals — mirrors maniac g_pAnmCacheList @0x45ebc8 / g_nAnmCacheCount @0x45ebcc */
void *g_pAnmCacheList = NULL;   /* @0x45ebc8 */
int   g_nAnmCacheCount = 0;     /* @0x45ebcc */
char  g_szAnim[] = "Anim";      /* @0x45113c pool tag (Ghidra shows 0x45113c "Anim") */
byte  g_abAnmMagic[] = "ANM";   /* @0x451144 magic (first 3 bytes) */

/* scene helpers are declared in scene.h (included via anim.h chain) */

/* -------------------------------------------------------------------
 * dataRead helpers @0x433ee0/0x433ef0/0x433f10 — unaligned little-endian.
 * Original reads via byte/word/dword pointer deref; rebuild uses memcpy
 * to avoid UBSAN on unaligned host reads (x86 allows it, but be clean).
 * ------------------------------------------------------------------- */
byte dataReadU8(byte *pData) /* @0x433ee0 */
{
    return *pData;
}
unsigned short dataReadU16(byte *pData) /* @0x433ef0 */
{
    unsigned short v;
    memcpy(&v, pData, sizeof(v));
    return v;
}
/* List/set aliasing contract (sceneObjectAnimStep/Interp take AnmSet* as
 * SceneObjAnimList*): mesh slots, the NULL terminator overlap, and the
 * state pointer must share offsets. C99 compile-time checks. */
typedef char AnmAliasCheckMesh[offsetof(AnmSet, pMesh) == offsetof(SceneObjAnimList, apObjs) ? 1 : -1];
typedef char AnmAliasCheckState[offsetof(AnmSet, pAnm) == offsetof(SceneObjAnimList, pState) ? 1 : -1];
typedef char AnmAliasCheckTerm[offsetof(AnmSet, nUnkC) == 3 * sizeof(void *) ? 1 : -1];

unsigned int dataReadU32(byte *pData) /* @0x433f10 */
{
    unsigned int v;
    memcpy(&v, pData, sizeof(v));
    return v;
}

/* -------------------------------------------------------------------
 * anmCalcSize @0x433f40 — sizing + allocation pass.
 * Scans header to compute arena size, allocates via memPoolAlloc(pPool,size),
 * returns header pointer + track/data pointers. Disasm: base = nTrack*8+0x38,
 * then per-track per-record	switch adds 0x10/0x14/8 for internal expansion.
 * 64-bit port: AnmTrack is 16 bytes (not 8) and type-3/4 records widened
 * (0x18/0x14, mesh slot is a native pointer) — the arena uses sizeof-based
 * strides so anmLoad's writer fits. Must stay in sync with the writer below.
 * ------------------------------------------------------------------- */
void anmCalcSize(void *pPool, byte *pData, AnmFile **ppOut, void **ppTrackData, void **ppRecordData) /* @0x433f40 */
{
    byte *p = pData + 4;
    unsigned short n;
    unsigned int u;
    int nTracks;
    size_t size;

    n = dataReadU16(p);
    p += 2;
    u = (unsigned int)n & 0xffffu;
    while (u != 0) {
        p += (unsigned int)*p + 1u;
        u--;
    }
    n = dataReadU16(p);
    p += 2;
    u = (unsigned int)n & 0xffffu;
    while (u != 0) {
        p += (unsigned int)*p + 3u;
        u--;
    }
    n = dataReadU16(p);
    nTracks = (int)((unsigned int)n & 0xffffu);
    p += 2;
    size = (size_t)nTracks * sizeof(AnmTrack) + sizeof(AnmFile);
    {
        int iTracks = nTracks;
        int perTrackSave = nTracks;
        (void)perTrackSave;
        while (iTracks != 0) {
            unsigned short nRecs = dataReadU16(p);
            unsigned int nR = (unsigned int)nRecs & 0xffffu;
            p += 2;
            while (nR != 0) {
                byte tp = *p;
                byte *np = p + 1;
                switch (tp) {
                case 1:
                case 2:
                    np = p + 0xdu;
                    size += 0x10u;
                    break;
                case 3:
                    np = p + 0xfu;
                    size += 0x18u; /* writer stride (native mesh pointer) */
                    break;
                case 4:
                    np = p + 9u;
                    size += 0x14u; /* writer stride (native mesh pointer) */
                    break;
                case 5:
                    np = p + 9u;
                    size += 8u;
                    break;
                case 6:
                    np = p + 0xfu;
                    size += 0x10u;
                    break;
                default:
                    break;
                }
                p = np;
                nR--;
            }
            iTracks--;
        }
    }
    {
        AnmFile *base = (AnmFile *)memPoolAlloc((int)(intptr_t)pPool, size);
        *ppOut = base;
        *ppTrackData = (void *)(base + 1); /* past native header */
        *ppRecordData = (void *)((char *)(base + 1) + nTracks * sizeof(AnmTrack));
    }
}

/* -------------------------------------------------------------------
 * anmLoad @0x433a90 — parse .anm buffer into one arena AnmFile.
 * Validates "ANM" (memcmp 3) + version 1|2, ensures g_pAnmCacheList pool,
 * calls anmCalcSize, then builds:
 *   - mesh-name id table (nMesh *4, via scenNameToIdEx)
 *   - channel tables (nChan*4 ids + nChan*4 channel byte values)
 *   - per-track {nRecs, pRecs} + expanded records (resolve ids via tables)
 * Frees temp tables, fills AnmFile header, bumps g_nAnmCacheCount.
 * ------------------------------------------------------------------- */
AnmFile *anmLoad(byte *pData, SceneNode *pMasterNode, SceneNode *pObj) /* @0x433a90 */
{
    int i;
    unsigned short nMesh, nChan, nTracks;
    byte *p;
    void **pMeshIds = NULL;
    void **pChanIds = NULL;
    byte *pChanVals = NULL; /* stored as byte array but alloced as int array in original */
    AnmFile *pAnm = NULL;
    AnmTrack *pTracks = NULL;
    byte *pRecBase = NULL;

    if (!pData) return NULL;
    if (pData[0] != 'A' || pData[1] != 'N' || pData[2] != 'M') return NULL;
    if (pData[3] != 1 && pData[3] != 2) return NULL;
    if (g_nAnmCacheCount == 0 && g_pAnmCacheList == NULL) {
        g_pAnmCacheList = (void *)(intptr_t)memPoolCreate(g_szAnim);
    }
    anmCalcSize(g_pAnmCacheList, pData, &pAnm, (void **)&pTracks, (void **)&pRecBase);
    if (!pAnm) return NULL;

    p = pData + 6;
    nMesh = dataReadU16(pData + 4);
    nMesh &= 0xffffu;
    if (nMesh) {
        pMeshIds = (void **)memPoolAlloc(0, (size_t)nMesh * sizeof(void *));
        for (i = 0; i < (int)nMesh; i++) {
            char tmp[256];
            byte len = dataReadU8(p);
            p++;
            memset(tmp, 0, sizeof(tmp));
            if (len) {
                size_t ll = (size_t)len;
                if (ll >= sizeof(tmp)) ll = sizeof(tmp) - 1;
                memcpy(tmp, p, ll);
                p += len;
            }
            pMeshIds[i] = scenNameToIdEx(tmp);
        }
    } else {
        p += 0; /* no mesh names, p already at +6 */
        /* pData+6 is already consumed; need to advance zero iterations */
        (void)pMeshIds;
    }
    /* p now at channelCount */
    nChan = dataReadU16(p);
    p += 2;
    nChan &= 0xffffu;
    if (nChan) {
        pChanIds = (void **)memPoolAlloc(0, (size_t)nChan * sizeof(void *));
        pChanVals = (byte *)memPoolAlloc(0, (size_t)nChan * 4u);
        for (i = 0; i < (int)nChan; i++) {
            char tmp[256];
            byte len = dataReadU8(p);
            p++;
            memset(tmp, 0, sizeof(tmp));
            if (len) {
                size_t ll = (size_t)len;
                if (ll >= sizeof(tmp)) ll = sizeof(tmp) - 1;
                memcpy(tmp, p, ll);
                p += len;
            }
            pChanIds[i] = scenNameToIdEx(tmp);
            pChanVals[i] = (byte)(dataReadU16(p) & 0xffffu);
            p += 2;
        }
    }
    nTracks = dataReadU16(p);
    p += 2;
    nTracks &= 0xffffu;
    pAnm->nFrameCount = (int)nTracks;
    pAnm->pTrackBase = pTracks;
    {
        byte *pRec = pRecBase;
        for (i = 0; i < (int)nTracks; i++) {
            unsigned short nRecs = dataReadU16(p);
            unsigned int nR = (unsigned int)nRecs & 0xffffu;
            p += 2;
            pTracks[i].nRecs = (int)nR;
            pTracks[i].pRecs = pRec;
            while (nR != 0) {
                byte tp = dataReadU8(p);
                byte *np = p + 1;
                *pRec = tp;
                switch (tp) {
                case 1:
                case 2: {
                    unsigned int a = dataReadU32(p + 1);
                    unsigned int b = dataReadU32(p + 5);
                    unsigned int c = dataReadU32(p + 9);
                    *(int *)(pRec + 4) = (int)a;
                    *(int *)(pRec + 8) = (int)b;
                    *(int *)(pRec + 0xc) = (int)c;
                    pRec += 0x10;
                    np = p + 0xd;
                    break;
                }
                case 3: {
                    unsigned short idx = dataReadU16(p + 1);
                    unsigned int a = dataReadU32(p + 3);
                    unsigned int b = dataReadU32(p + 7);
                    unsigned int c = dataReadU32(p + 0xb);
                    /* 64-bit port: mesh slot widened to a native pointer;
                     * followers shift +4, stride 0x14 -> 0x18. */
                    void *meshId = 0;
                    if (pMeshIds && (idx & 0xffffu) < (unsigned int)nMesh) meshId = pMeshIds[idx & 0xffffu];
                    memcpy(pRec + 4, &meshId, sizeof(meshId));
                    *(int *)(pRec + 12) = (int)a;
                    *(int *)(pRec + 16) = (int)b;
                    *(int *)(pRec + 20) = (int)c;
                    pRec += 0x18;
                    np = p + 0xf;
                    break;
                }
                case 4: {
                    unsigned short idx = dataReadU16(p + 1);
                    unsigned short s0 = dataReadU16(p + 3);
                    unsigned short s1 = dataReadU16(p + 5);
                    unsigned short s2 = dataReadU16(p + 7);
                    /* 64-bit port: mesh slot widened; stride 0x10 -> 0x14. */
                    void *meshId = 0;
                    if (pMeshIds && (idx & 0xffffu) < (unsigned int)nMesh) meshId = pMeshIds[idx & 0xffffu];
                    memcpy(pRec + 4, &meshId, sizeof(meshId));
                    *(short *)(pRec + 12) = (short)s0;
                    *(short *)(pRec + 14) = (short)s1;
                    *(short *)(pRec + 16) = (short)s2;
                    pRec += 0x14;
                    np = p + 9;
                    break;
                }
                case 5: {
                    unsigned short idx = dataReadU16(p + 1);
                    unsigned short s0 = dataReadU16(p + 3);
                    unsigned short s1 = dataReadU16(p + 5);
                    unsigned short s2 = dataReadU16(p + 7);
                    byte ch = 0;
                    if (pChanVals && (idx & 0xffffu) < (unsigned int)nChan) ch = pChanVals[idx & 0xffffu];
                    pRec[1] = ch;
                    *(short *)(pRec + 2) = (short)s0;
                    *(short *)(pRec + 4) = (short)s1;
                    *(short *)(pRec + 6) = (short)s2;
                    pRec += 8;
                    np = p + 9;
                    break;
                }
                case 6: {
                    unsigned int a = dataReadU32(p + 3);
                    unsigned int b = dataReadU32(p + 7);
                    unsigned int c = dataReadU32(p + 0xb);
                    /* p+1 is u16 ignored (original dataReadU16 at pbVar9) */
                    *(int *)(pRec + 4) = (int)a;
                    *(int *)(pRec + 8) = (int)b;
                    *(int *)(pRec + 0xc) = (int)c;
                    pRec += 0x10;
                    np = p + 0xf;
                    break;
                }
                default:
                    break;
                }
                p = np;
                nR--;
            }
        }
    }
    if (pChanVals) memPoolFree(0, pChanVals);
    if (pChanIds)  memPoolFree(0, pChanIds);
    if (pMeshIds)  memPoolFree(0, pMeshIds);
    pAnm->nFrame = 0;
    pAnm->pCurTrack = pAnm->pTrackBase;
    pAnm->nLoopStart = 0;
    pAnm->pPool = g_pAnmCacheList;
    pAnm->pMasterNode = pMasterNode;
    pAnm->pObj = pObj;
    g_nAnmCacheCount++;
    return pAnm;
}

/* anmLoadFile @0x433a50 — fileReadRaw wrapper */
AnmFile *anmLoadFile(LPCSTR pszPath, SceneNode *pMasterNode, SceneNode *pObj) /* @0x433a50 */
{
    byte *pData = (byte *)fileReadRaw(0, pszPath);
    AnmFile *pAnm;
    if (!pData) return NULL;
    pAnm = anmLoad(pData, pMasterNode, pObj);
    memPoolFree(0, pData);
    return pAnm;
}

/* anmFree @0x434050 — release arena, destroy pool when last */
void anmFree(AnmFile *pAnm) /* @0x434050 */
{
    if (!pAnm) return;
    memPoolFree((int)(intptr_t)g_pAnmCacheList, pAnm);
    g_nAnmCacheCount--;
    if (g_nAnmCacheCount == 0 && g_pAnmCacheList) {
        memPoolDestroy((int)(intptr_t)g_pAnmCacheList);
        g_pAnmCacheList = NULL;
    }
}

/* anmSetAlloc @0x4344d0 — 0x18 holder, pAnm at +0x14, slots zeroed */
AnmSet *anmSetAlloc(AnmFile *pAnm) /* @0x4344d0 */
{
    AnmSet *pSet = (AnmSet *)memPoolAlloc(0, sizeof(AnmSet));
    if (!pSet) return NULL;
    pSet->pMesh[0] = NULL;
    pSet->pMesh[1] = NULL;
    pSet->pMesh[2] = NULL;
    pSet->nUnkC = 0;
    pSet->nUnk10 = 0;
    pSet->_pad20 = 0; /* keeps apObjs[3] NULL when read as a list */
    pSet->pAnm = pAnm;
    return pSet;
}

/* anmSetFree @0x434500 — anmFree + free holder */
void anmSetFree(AnmSet *pSet) /* @0x434500 */
{
    if (!pSet) return;
    if (pSet->pAnm) anmFree(pSet->pAnm);
    memPoolFree(0, pSet);
}

/* anmSetMeshSlot @0x434530 — pSet->pMesh[nSlot] = pMesh */
void anmSetMeshSlot(AnmSet *pSet, void *pMesh, int nSlot) /* @0x434530 */
{
    if (!pSet) return;
    if (nSlot < 0 || nSlot >= 3) return;
    pSet->pMesh[nSlot] = pMesh;
}

/* -------------------------------------------------------------------
 * eventAnimReset @0x434270 — rewind to frame 0
 * ------------------------------------------------------------------- */
void eventAnimReset(AnmFile *pAnm) /* @0x434270 */
{
    if (!pAnm) return;
    pAnm->nFrame = 0;
    pAnm->pCurTrack = pAnm->pTrackBase;
}

/* -------------------------------------------------------------------
 * eventAnimStep @0x434090 — single-obj opcode step.
 * Reads current track (pCurTrack -> {nRecs,pRecs}), dispatches types 1-6:
 *   1: pos target (store +Y/Z negated), 2: facing target, 3: sceneObjSetPos,
 *   4: sceneObjSetPosOrient, 5: sceneObjSetSubPos (channel), 6: sceneObjSetPos
 * Then snaps pMasterNode to pos/facing, advances frame, loops if bLoop &1.
 * Returns 1 when stream ends (or looped), 0 otherwise. Mirrors disasm
 * at 0x434090 with Y/Z negation and sub-pos float placeholders.
 * ------------------------------------------------------------------- */
int eventAnimStep(AnmFile *pAnm, byte bLoop) /* @0x434090 */
{
    AnmTrack *trk;
    byte *rec;
    int n, i;

    if (!pAnm) return 0;
    if (pAnm->nFrame >= pAnm->nFrameCount) return 0;
    trk = (AnmTrack *)pAnm->pCurTrack;
    n = trk->nRecs;
    rec = (byte *)trk->pRecs;
    for (i = 0; i < n; i++) {
        byte tp = rec[0];
        switch (tp) {
        case 1:
            pAnm->nPosX = *(int *)(rec + 4);
            pAnm->nPosY = -*(int *)(rec + 8);
            pAnm->nPosZ = -*(int *)(rec + 0xc);
            rec += 0x10;
            break;
        case 2:
            pAnm->nFaceX = *(int *)(rec + 4);
            pAnm->nFaceY = -*(int *)(rec + 8);
            pAnm->nFaceZ = -*(int *)(rec + 0xc);
            rec += 0x10;
            break;
        case 3: {
            SceneNode *mesh = *(SceneNode **)(rec + 4);
            int ax = *(int *)(rec + 12);
            int ay = -*(int *)(rec + 16);
            int az = -*(int *)(rec + 20);
            if (pAnm->pObj) {
                sceneObjSetPos(pAnm->pObj, ax, ay, az, 2);
            } else if (mesh != 0) {
                sceneObjSetPos(mesh, ax, ay, az, 2);
            }
            rec += 0x18;
            break;
        }
        case 4: {
            SceneNode *obj = pAnm->pObj ? pAnm->pObj : *(SceneNode **)(rec + 4);
            short yaw = *(short *)(rec + 12);
            short pitch = *(short *)(rec + 14);
            short roll = *(short *)(rec + 16);
            sceneObjSetPosOrient(obj, yaw, (short)-pitch, (short)-roll, 2);
            rec += 0x14;
            break;
        }
        case 5: {
            if (pAnm->pObj) {
                int ch = (signed char)rec[1];
                short sx = *(short *)(rec + 2);
                short sy = *(short *)(rec + 4);
                short sz = *(short *)(rec + 6);
                sceneObjSetSubPos(pAnm->pObj, ch, sx, sy, sz, 2);
            }
            rec += 8;
            break;
        }
        case 6: {
            if (pAnm->pObj) {
                int ax = *(int *)(rec + 4);
                int ay = -*(int *)(rec + 8);
                int az = -*(int *)(rec + 0xc);
                sceneObjSetPos(pAnm->pObj, ax, ay, az, 2);
            }
            rec += 0x10;
            break;
        }
        default:
            rec += 0x10;
            break;
        }
    }
    if (pAnm->pMasterNode) {
        sceneObjSetPos(pAnm->pMasterNode, pAnm->nPosX, pAnm->nPosY, pAnm->nPosZ, 2);
        sceneNodeFacePos(pAnm->pMasterNode, 0,
                         (float)pAnm->nFaceX, (float)pAnm->nFaceY, (float)pAnm->nFaceZ, 2);
    }
    pAnm->nFrame++;
    if (pAnm->nFrame >= pAnm->nFrameCount) {
        if ((bLoop & 1) == 0) return 1;
        pAnm->nFrame = 0;
        pAnm->pCurTrack = pAnm->pTrackBase;
        return 1;
    }
    pAnm->pCurTrack = (void *)((char *)pAnm->pCurTrack + sizeof(AnmTrack)); /* @0x434090 +8 orig */
    return 0;
}

/* -------------------------------------------------------------------
 * eventAnimApply @0x434290 — interpolation apply variant.
 * Same op stream as eventAnimStep but does half-step movement:
 *   op3: midpoint via sceneNodeGetPosWorld -> sceneObjSetPos
 *   op4/5: sceneObjSetSubOrient (not PosOrient/SubPos)
 *   op6: midpoint for pObj
 * Not advancing frame; early-out if nFrame >= nFrameCount without loop.
 * ------------------------------------------------------------------- */
void eventAnimApply(AnmFile *pAnm, byte bLoop) /* @0x434290 */
{
    AnmTrack *trk;
    byte *rec;
    int n, i;

    if (!pAnm) return;
    if (pAnm->nFrame >= pAnm->nFrameCount) {
        if ((bLoop & 1) == 0) return;
        pAnm->nFrame = 0;
    }
    trk = (AnmTrack *)pAnm->pCurTrack;
    n = trk->nRecs;
    rec = (byte *)trk->pRecs;
    for (i = 0; i < n; i++) {
        byte tp = rec[0];
        switch (tp) {
        case 1:
            pAnm->nPosX = *(int *)(rec + 4);
            pAnm->nPosY = -*(int *)(rec + 8);
            pAnm->nPosZ = -*(int *)(rec + 0xc);
            rec += 0x10;
            break;
        case 2:
            pAnm->nFaceX = *(int *)(rec + 4);
            pAnm->nFaceY = -*(int *)(rec + 8);
            pAnm->nFaceZ = -*(int *)(rec + 0xc);
            rec += 0x10;
            break;
        case 3: {
            SceneNode *mesh = *(SceneNode **)(rec + 4);
            if (mesh != 0) {
                float out[3];
                int curX, curY, curZ;
                sceneNodeGetPosWorld(mesh, out, 2);
                memcpy(&curX, &out[0], sizeof(int));
                memcpy(&curY, &out[1], sizeof(int));
                memcpy(&curZ, &out[2], sizeof(int));
                {
                    int tx = *(int *)(rec + 12);
                    int ty = *(int *)(rec + 16);
                    int tz = *(int *)(rec + 20);
                    /* Asymmetric midpoint per disasm 0x434326-0x434383: X uses
                     * curX + (tx-curX)/2 but Y/Z use (ty+curY)/2 - curY (then
                     * negated). The forms truncate differently for odd sums —
                     * do NOT "symmetrize". */
                    int nx = curX + (tx - curX) / 2;
                    int ny = (ty + curY) / 2 - curY;
                    int nz = (tz + curZ) / 2 - curZ;
                    sceneObjSetPos(mesh, nx, -ny, -nz, 2);
                }
            }
            rec += 0x18;
            break;
        }
        case 4: {
            SceneNode *mesh = *(SceneNode **)(rec + 4);
            if (mesh != 0) {
                short sx = *(short *)(rec + 12);
                short sy = *(short *)(rec + 14);
                short sz = *(short *)(rec + 16);
                sceneObjSetSubOrient(mesh, 0, sx, (short)-sy, (short)-sz);
            }
            rec += 0x14;
            break;
        }
        case 5: {
            if (pAnm->pObj) {
                int ch = (signed char)rec[1];
                short sx = *(short *)(rec + 2);
                short sy = *(short *)(rec + 4);
                short sz = *(short *)(rec + 6);
                /* Original calls sceneObjSetSubOrient here (CALL 0x431110 at
                 * 0x4343da), not sceneObjSetSubPos as in eventAnimStep. */
                sceneObjSetSubOrient(pAnm->pObj, ch, sx, sy, sz);
            }
            rec += 8;
            break;
        }
        case 6: {
            if (pAnm->pObj) {
                float out[3];
                int curX, curY, curZ;
                sceneNodeGetPosWorld(pAnm->pObj, out, 2);
                memcpy(&curX, &out[0], sizeof(int));
                memcpy(&curY, &out[1], sizeof(int));
                memcpy(&curZ, &out[2], sizeof(int));
                {
                    int tx = *(int *)(rec + 4);
                    int ty = *(int *)(rec + 8);
                    int tz = *(int *)(rec + 0xc);
                    /* Same asymmetric midpoint as op3 (disasm 0x4343ee-0x434446). */
                    int nx = curX + (tx - curX) / 2;
                    int ny = (ty + curY) / 2 - curY;
                    int nz = (tz + curZ) / 2 - curZ;
                    sceneObjSetPos(pAnm->pObj, nx, -ny, -nz, 2);
                }
            }
            rec += 0x10;
            break;
        }
        default:
            rec += 0x10;
            break;
        }
    }
    if (pAnm->pMasterNode) {
        sceneObjSetPos(pAnm->pMasterNode, pAnm->nPosX, pAnm->nPosY, pAnm->nPosZ, 2);
        sceneNodeFacePos(pAnm->pMasterNode, 0,
                         (float)pAnm->nFaceX, (float)pAnm->nFaceY, (float)pAnm->nFaceZ, 2);
    }
}

/* -------------------------------------------------------------------
 * sceneObjectAnimStep @0x434540 — multi-obj variant.
 * pList = SceneObjAnimList: apObjs[0..4] are SceneNode* (0-terminated, max 5)
 * and pList->pState is a SceneObjAnimState (mirrors AnmFile layout, see
 * anim.h). Op dispatch iterates the object list for ops 3/4/5/6. Verified
 * against disasm 0x434540 (obj loaded via MOV EDX,[EDI] and pushed straight
 * to sceneObjSetPos/sceneObjSetPosOrient/sceneObjSetSubPos).
 * ------------------------------------------------------------------- */
int sceneObjectAnimStep(SceneObjAnimList *pList, byte bLoop) /* @0x434540 */
{
    SceneObjAnimState *st;
    AnmTrack *pTrk;
    byte *rec;
    int n, i;

    if (!pList) return 0;
    st = pList->pState;
    if (!st) return 0;
    if (st->nFrame >= st->nFrameCount) return 0;
    /* Original disasm reads {nRecs, pRecs} directly from pCurTrack, which
     * advances one AnmTrack per frame (+8 in the 32-bit original, native
     * sizeof here; no frame*stride re-indexing). Typed read: the old int*
     * view truncated the 64-bit pRecs pointer. */
    pTrk = st->pCurTrack;
    n = pTrk->nRecs;
    rec = (byte *)pTrk->pRecs;
    for (i = 0; i < n; i++) {
        byte tp = rec[0];
        switch (tp) {
        case 1:
            st->nPosX = *(int *)(rec + 4);
            st->nPosY = -*(int *)(rec + 8);
            st->nPosZ = -*(int *)(rec + 0xc);
            rec += 0x10;
            break;
        case 2:
            st->nFaceX = *(int *)(rec + 4);
            st->nFaceY = -*(int *)(rec + 8);
            st->nFaceZ = -*(int *)(rec + 0xc);
            rec += 0x10;
            break;
        case 3: {
            SceneNode *mesh = *(SceneNode **)(rec + 4);
            int ax = *(int *)(rec + 12);
            int ay = -*(int *)(rec + 16);
            int az = -*(int *)(rec + 20);
            if (pList->apObjs[0] == 0) {
                if (mesh != 0) sceneObjSetPos(mesh, ax, ay, az, 2);
            } else {
                int k = 0;
                while (k < 5 && pList->apObjs[k] != 0) {
                    sceneObjSetPos(pList->apObjs[k], ax, ay, az, 2);
                    k++;
                }
            }
            rec += 0x18;
            break;
        }
        case 4: {
            short sx = *(short *)(rec + 12);
            short sy = *(short *)(rec + 14);
            short sz = *(short *)(rec + 16);
            if (pList->apObjs[0] == 0) {
                SceneNode *mesh = *(SceneNode **)(rec + 4);
                sceneObjSetPosOrient(mesh, sx, (short)-sy, (short)-sz, 2);
            } else {
                int k = 0;
                while (k < 5 && pList->apObjs[k] != 0) {
                    sceneObjSetPosOrient(pList->apObjs[k], sx, (short)-sy, (short)-sz, 2);
                    k++;
                }
            }
            rec += 0x14; /* writer stride (native mesh pointer) */
            break;
        }
        case 5: {
            if (pList->apObjs[0] != 0) {
                int k = 0;
                while (k < 5 && pList->apObjs[k] != 0) {
                    int ch = (signed char)rec[1];
                    short sx = *(short *)(rec + 2);
                    short sy = *(short *)(rec + 4);
                    short sz = *(short *)(rec + 6);
                    sceneObjSetSubPos(pList->apObjs[k], ch, sx, sy, sz, 2);
                    k++;
                }
            }
            rec += 8;
            break;
        }
        case 6: {
            if (pList->apObjs[0] != 0) {
                int k = 0;
                while (k < 5 && pList->apObjs[k] != 0) {
                    int ax = *(int *)(rec + 4);
                    int ay = -*(int *)(rec + 8);
                    int az = -*(int *)(rec + 0xc);
                    sceneObjSetPos(pList->apObjs[k], ax, ay, az, 2);
                    k++;
                }
            }
            rec += 0x10;
            break;
        }
        default:
            rec += 0x10;
            break;
        }
    }
    if (st->pMasterNode != 0) {
        sceneObjSetPos(st->pMasterNode, st->nPosX, st->nPosY, st->nPosZ, 2);
        sceneNodeFacePos(st->pMasterNode, 0,
                         (float)st->nFaceX, (float)st->nFaceY, (float)st->nFaceZ, 2);
    }
    st->nFrame++;
    if (st->nFrame >= st->nFrameCount) {
        if ((bLoop & 1) == 0) return 1;
        st->nFrame = 0;
        st->pCurTrack = st->pLoopBase;
        return 1;
    }
    st->pCurTrack = (AnmTrack *)((char *)st->pCurTrack + sizeof(AnmTrack));
    return 0;
}

/* -------------------------------------------------------------------
 * sceneObjectAnimStepInterp @0x4347c0 — interpolation variant.
 * Op3/6 do midpoint lerp (asymmetric formula, see eventAnimApply op3),
 * op4/5 use SubOrient; op6 checks each list object for NULL before the
 * sceneNodeGetPosWorld midpoint (TEST EAX,EAX / JZ at 0x43495b). No frame-
 * advancement wrap beyond the early loop-reset at entry (if frame>=count
 * and bLoop&1, reset).
 * ------------------------------------------------------------------- */
void sceneObjectAnimStepInterp(SceneObjAnimList *pList, byte bLoop) /* @0x4347c0 */
{
    SceneObjAnimState *st;
    AnmTrack *pTrk;
    byte *rec;
    int n, i;

    if (!pList) return;
    st = pList->pState;
    if (!st) return;
    if (st->nFrame >= st->nFrameCount) {
        if ((bLoop & 1) == 0) return;
        st->nFrame = 0;
    }
    /* Typed track read (the int* view truncated 64-bit pRecs). */
    pTrk = st->pCurTrack;
    n = pTrk->nRecs;
    rec = (byte *)pTrk->pRecs;
    for (i = 0; i < n; i++) {
        byte tp = rec[0];
        switch (tp) {
        case 1:
            st->nPosX = *(int *)(rec + 4);
            st->nPosY = -*(int *)(rec + 8);
            st->nPosZ = -*(int *)(rec + 0xc);
            rec += 0x10;
            break;
        case 2:
            st->nFaceX = *(int *)(rec + 4);
            st->nFaceY = -*(int *)(rec + 8);
            st->nFaceZ = -*(int *)(rec + 0xc);
            rec += 0x10;
            break;
        case 3: {
            SceneNode *mesh = *(SceneNode **)(rec + 4);
            if (mesh != 0) {
                float out[3];
                int curX, curY, curZ;
                sceneNodeGetPosWorld(mesh, out, 2);
                memcpy(&curX, &out[0], sizeof(int));
                memcpy(&curY, &out[1], sizeof(int));
                memcpy(&curZ, &out[2], sizeof(int));
                {
                    /* Widened layout: native mesh pointer at +4 pushes the
                     * int targets to +12/+16/+20 (matches the writer). */
                    int tx = *(int *)(rec + 12);
                    int ty = *(int *)(rec + 16);
                    int tz = *(int *)(rec + 20);
                    /* Asymmetric midpoint per disasm 0x43485e-0x4348bd. */
                    int nx = curX + (tx - curX) / 2;
                    int ny = (ty + curY) / 2 - curY;
                    int nz = (tz + curZ) / 2 - curZ;
                    sceneObjSetPos(mesh, nx, -ny, -nz, 2);
                }
            }
            rec += 0x18;
            break;
        }
        case 4: {
            SceneNode *mesh = *(SceneNode **)(rec + 4);
            if (mesh != 0) {
                short sx = *(short *)(rec + 12);
                short sy = *(short *)(rec + 14);
                short sz = *(short *)(rec + 16);
                sceneObjSetSubOrient(mesh, 0, sx, (short)-sy, (short)-sz);
            }
            rec += 0x14;
            break;
        }
        case 5: {
            if (pList->apObjs[0] != 0) {
                int k = 0;
                while (k < 5 && pList->apObjs[k] != 0) {
                    int ch = (signed char)rec[1];
                    short sx = *(short *)(rec + 2);
                    short sy = *(short *)(rec + 4);
                    short sz = *(short *)(rec + 6);
                    sceneObjSetSubOrient(pList->apObjs[k], ch, sx, sy, sz);
                    k++;
                }
            }
            rec += 8;
            break;
        }
        case 6: {
            if (pList->apObjs[0] != 0) {
                int k = 0;
                while (k < 5 && pList->apObjs[k] != 0) {
                    SceneNode *obj = pList->apObjs[k];
                    if (obj != 0) {
                        float out[3];
                        int curX, curY, curZ;
                        sceneNodeGetPosWorld(obj, out, 2);
                        memcpy(&curX, &out[0], sizeof(int));
                        memcpy(&curY, &out[1], sizeof(int));
                        memcpy(&curZ, &out[2], sizeof(int));
                        {
                            int tx = *(int *)(rec + 4);
                            int ty = *(int *)(rec + 8);
                            int tz = *(int *)(rec + 0xc);
                            /* Same asymmetric midpoint (disasm 0x43496c-0x4349b6). */
                            int nx = curX + (tx - curX) / 2;
                            int ny = (ty + curY) / 2 - curY;
                            int nz = (tz + curZ) / 2 - curZ;
                            sceneObjSetPos(obj, nx, -ny, -nz, 2);
                        }
                    }
                    k++;
                }
            }
            rec += 0x10;
            break;
        }
        default:
            rec += 0x10;
            break;
        }
    }
    if (st->pMasterNode != 0) {
        sceneObjSetPos(st->pMasterNode, st->nPosX, st->nPosY, st->nPosZ, 2);
        sceneNodeFacePos(st->pMasterNode, 0,
                         (float)st->nFaceX, (float)st->nFaceY, (float)st->nFaceZ, 2);
    }
}

/* g_awWalkAnimTable @0x458138 — walk-anim lookup table filled by
 * roundStartInit @0x40a941..0x40a981, read by playerAnimOrientFromDir
 * @0x4336b0. */
short g_awWalkAnimTable[128][2];                               /* @0x458138 */

/* walkAnimTableEntryCalc @0x433980 — [VERIFIED 2026-08-20 + 2026-08-29; comparison
 * direction corrected 2026-09-06 against live differential capture (CartGrabAnimBug)]
 * disassembly: for candidate angles cand = nAngle + nStep (nStep from 0x4000 halving to
 * 1) compute the foot point on the walk circle (x = cos(a)*R + C, y = sin(a)*R, a =
 * cand*g_dblBdgToRad) and accept the candidate (nAngle = cand) when the foot distance
 * sqrt(x^2 + y^2) >= flNormSpeed*(C+R) (x87 FCOMPP dTarget,dist + TEST AH,0x41 + JZ-skip:
 * C0/C3 set == ST1 dTarget <= ST0 dist == accept). Afterwards pOut[0] =
 * (short)ftol(-(atan2(sin(A)*R, cos(A)*R + C) * g_dblRadToBdg)) with
 * A = nAngle*g_dblBdgToRad (FMUL @0x44b780 + FCHS), pOut[1] = (short)nAngle.
 * For flNormSpeed == 0 the divide produces NaN/inf exactly like the x87
 * path; the resulting ftol truncation is the same 0 as the original.
 *
 * NOTE: the previously "verified" condition (dTarget > dist) was inverted — it made
 * every entry with i < ~91 degenerate to nAngle = 0, which zeroed pOutWalk and fed
 * sinS=1/cosS=0 into playerAnimOrientFromDir, breaking the cart-grab arm aim
 * (CartGrabAnimBug.md). Live original check: entry 81 = (angle,-,18603). */
void walkAnimTableEntryCalc(short *pOut, float flNormSpeed,
                            int nCircleCenter, int nCircleRadius) /* @0x433980 */
{
    int nAngle = 0;
    int nStep = 0x4000;
    double dTarget = (double)(nCircleCenter + nCircleRadius) * (double)flNormSpeed;

    do {
        int nCand = nStep + nAngle;
        double dA = (double)nCand * g_dblBdgToRad;
        double dX = cos(dA) * (double)nCircleRadius + (double)nCircleCenter;
        double dY = sin(dA) * (double)nCircleRadius;
        if (dTarget <= sqrt(dX * dX + dY * dY)) {
            nAngle = nCand;
        }
        nStep /= 2;
    } while (nStep != 0);
    {
        double dA = (double)nAngle * g_dblBdgToRad;
        double dAtan = atan2(sin(dA) * (double)nCircleRadius,
                             cos(dA) * (double)nCircleRadius + (double)nCircleCenter);
        pOut[0] = (short)(int)(-dAtan * g_dblRadToBdg);      /* FMUL @0x44b780, FCHS */
        pOut[1] = (short)nAngle;
    }
}

/* playerAnimOrientFromDir @0x4336b0 — compute the 3-euler orientation
 * (1/100-degree shorts) that aims a scene object along (nDirX,nDirY,nDirZ),
 * blended with the walk-table limb swing (pWalkTable = g_awWalkAnimTable,
 * entry picked by the clamped direction length 0x10..0x7f; the entry angle
 * drives mathSinDeg/mathCosDeg(+0x4000) and the entry value becomes the
 * limb offset). Consumed by playerAnimSfxUpdate for the limb sub-meshes
 * 3/4/6/7 (the caller feeds pCharSceneObj + the move delta and steps
 * sub-mesh 6/3 with pOutAngles and 7/4 with pOutWalk).
 *
 * Signature verified against both playerAnimSfxUpdate call sites (9 cdecl
 * args, RET 0x24): (nDirX, nDirY, nDirZ, pOutAngles, pOutWalk, pUnused,
 * pWalkTable, nWalkGeom=0x1ea, nRoll=+-16000). pUnused is pushed by the
 * caller but never read (Ghidra/IDA saw it as a stray "flPitch" float).
 * Returns 1 (EAX).
 *
 * Verified against the original disassembly (0x4336b0..0x43396c): the four
 * matrix loops, the walk-table entry/clamp, and the yaw/pitch/roll tail
 * below match the FPU code (mathAtan2Deg @0x42d010, mathSinDeg @0x42d030,
 * mathCosDeg @0x42d050, __ftol @0x43dd10, and the FMUL/FDIVR constants
 * 127.0 @0x44b7b0 / 1.0 @0x44b288). */
int playerAnimOrientFromDir(int nDirX, int nDirY, int nDirZ,
                            short *pOutAngles, short *pOutWalk, void *pUnused,
                            const short *pWalkTable, int nWalkGeom,
                            short nRoll) /* @0x4336b0 */
{
    static const double g_dblWalkScale = 127.0; /* @0x44b7b0 (bytes 00 00 59 40) */
    static const double g_dblOne = 1.0;         /* @0x44b288 (bytes 00 00 F0 3F) */
    float afM[12];          /* 3x3 orientation matrix rows + 3 spare floats
                             * (local_30 in the decompile; rows 0/1 seeded
                             * {0,0,1}/{0,1,0}, rows 2/3 built in place) */
    float flLen;
    float fA;
    float fB;
    float fC;
    int nI;
    short sWalk;
    float flSin;
    float flCosWalk;
    float flSinR;
    float flCosR;

    (void)pUnused;
    afM[0] = 0.0f; afM[1] = 0.0f; afM[2] = 1.0f;           /* forward = +Z */
    afM[3] = 0.0f; afM[4] = 1.0f; afM[5] = 0.0f;           /* up = +Y */
    afM[6] = afM[7] = afM[8] = afM[9] = afM[10] = afM[11] = 0.0f;
    flLen = sqrtf((float)(nDirY * nDirY + nDirZ * nDirZ + nDirX * nDirX)); /* @0x4336fd */
    nI = (int)(flLen * g_dblWalkScale / (double)nWalkGeom); /* @0x433709..0x43371d */
    if (nI > 0x7f) {                                       /* @0x433724 */
        nI = 0x7f;                                         /* @0x433729 */
    } else if (nI < 0x10) {                                /* @0x433733 */
        nI = 0x10;                                         /* @0x433735 */
    }
    sWalk = pWalkTable[1 + nI * 2];                        /* @0x433745 entry[idx].walk */
    pOutWalk[1] = 0;                                       /* @0x43374a */
    pOutWalk[0] = sWalk;                                   /* @0x43374e */
    pOutWalk[2] = 0;                                       /* @0x433751 */
    flSin = mathSinDeg((short)(pWalkTable[nI * 2] + 0x4000));   /* @0x43375e */
    flCosWalk = mathCosDeg((short)(pWalkTable[nI * 2] + 0x4000)); /* @0x433771 */
    /* loop 1: rotate the two seed rows by the walk-swing angle into rows 2/3 */
    for (nI = 0; nI < 0x18; nI += 0xc) {                   /* @0x43377c..0x4337b2 */
        int k = nI / 4;
        afM[k + 6] = afM[k];                               /* copy */
        afM[k + 7] = flCosWalk * afM[k + 1] - flSin * afM[k + 2];
        afM[k + 8] = flCosWalk * afM[k + 2] + flSin * afM[k + 1];
    }
    flSinR = mathSinDeg(nRoll);                            /* @0x4337bb */
    flCosR = mathCosDeg(nRoll);                            /* @0x4337c5 */
    /* loop 2: fold the roll angle back into rows 0/1 */
    for (nI = 0; nI < 0x18; nI += 0xc) {                   /* @0x4337cf..0x433805 */
        int k = nI / 4;
        afM[k] = flCosR * afM[k + 6] - flSinR * afM[k + 7];
        afM[k + 1] = flCosR * afM[k + 7] + flSinR * afM[k + 6];
        afM[k + 2] = afM[k + 8];
    }
    /* loop 3: tilt rows 2/3 toward the direction (y over len, horiz over len) */
    fA = (float)nDirY / flLen;                             /* @0x433809 */
    fB = sqrtf((float)(nDirZ * nDirZ + nDirX * nDirX));    /* @0x433821 */
    fC = fB / flLen;
    for (nI = 0; nI < 0x18; nI += 0xc) {                   /* @0x433829..0x43385f */
        int k = nI / 4;
        afM[k + 6] = afM[k];                               /* copy */
        afM[k + 7] = fA * afM[k + 2] + fC * afM[k + 1];
        afM[k + 8] = fC * afM[k + 2] - fA * afM[k + 1];
    }
    /* loop 4: finish rows 0/1 from the z/x components over the horiz length */
    for (nI = 0; nI < 0x18; nI += 0xc) {                   /* @0x433873..0x4338a9 */
        int k = nI / 4;
        afM[k] = (float)nDirZ / fB * afM[k + 6] +
                 (float)nDirX / fB * afM[k + 8];
        afM[k + 1] = afM[k + 7];
        afM[k + 2] = (float)nDirZ / fB * afM[k + 8] -
                     (float)nDirX / fB * afM[k + 6];
    }
    /* yaw/pitch/roll extraction (verified: 0x4338ad..0x43396c; the original
     * keeps inv=1/s on the FPU stack, so fA/fB are normalized by s) */
    fC = (float)g_dblOne / sqrtf(afM[0] * afM[0] + afM[2] * afM[2]); /* @0x4338ad FDIVR 1.0 */
    fA = fC * afM[0];                                      /* @0x4338c9 = m0/s */
    fB = fC * afM[2];                                      /* @0x4338d1 = m2/s */
    {   /* fU = fB*m2 + fA*m0 = sqrt(m0^2+m2^2), recomputed implicitly @0x4338e3..0x4338f9 */
        float fU = fB * afM[2] + fA * afM[0];
        pOutAngles[0] = (short)mathAtan2Deg(-afM[1], fU);  /* @0x433903 yaw */
        pOutAngles[1] = (short)mathAtan2Deg(fA, fB);       /* @0x433919 pitch */
        pOutAngles[2] = (short)mathAtan2Deg(               /* @0x433960 roll */
            fA * afM[5] - fB * afM[3],
            flSinR * (fB * afM[5] + fA * afM[3]) + fU * afM[4]);
    }
    return 1;                                              /* @0x43396c */
}
