#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    size = (size_t)nTracks * 8u + 0x38u;
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
                    size += 0x14u;
                    break;
                case 4:
                    np = p + 9u;
                    size += 0x10u;
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
        *ppTrackData = (void *)(base + 1); /* +0x38 */
        *ppRecordData = (void *)((char *)(base + 1) + nTracks * 8);
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
    int *pMeshIds = NULL;
    int *pChanIds = NULL;
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
        pMeshIds = (int *)memPoolAlloc(0, (size_t)nMesh * 4u);
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
        pChanIds = (int *)memPoolAlloc(0, (size_t)nChan * 4u);
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
                    int meshId = 0;
                    if (pMeshIds && (idx & 0xffffu) < (unsigned int)nMesh) meshId = pMeshIds[idx & 0xffffu];
                    *(int *)(pRec + 4) = meshId;
                    *(int *)(pRec + 8) = (int)a;
                    *(int *)(pRec + 0xc) = (int)b;
                    *(int *)(pRec + 0x10) = (int)c;
                    pRec += 0x14;
                    np = p + 0xf;
                    break;
                }
                case 4: {
                    unsigned short idx = dataReadU16(p + 1);
                    unsigned short s0 = dataReadU16(p + 3);
                    unsigned short s1 = dataReadU16(p + 5);
                    unsigned short s2 = dataReadU16(p + 7);
                    int meshId = 0;
                    if (pMeshIds && (idx & 0xffffu) < (unsigned int)nMesh) meshId = pMeshIds[idx & 0xffffu];
                    *(int *)(pRec + 4) = meshId;
                    *(short *)(pRec + 8) = (short)s0;
                    *(short *)(pRec + 10) = (short)s1;
                    *(short *)(pRec + 0xc) = (short)s2;
                    pRec += 0x10;
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
    AnmSet *pSet = (AnmSet *)memPoolAlloc(0, 0x18);
    if (!pSet) return NULL;
    pSet->pMesh[0] = NULL;
    pSet->pMesh[1] = NULL;
    pSet->pMesh[2] = NULL;
    pSet->nUnkC = 0;
    pSet->nUnk10 = 0;
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
            int ax = *(int *)(rec + 8);
            int ay = -*(int *)(rec + 0xc);
            int az = -*(int *)(rec + 0x10);
            if (pAnm->pObj) {
                sceneObjSetPos(pAnm->pObj, ax, ay, az, 2);
            } else if (mesh != 0) {
                sceneObjSetPos(mesh, ax, ay, az, 2);
            }
            rec += 0x14;
            break;
        }
        case 4: {
            SceneNode *obj = pAnm->pObj ? pAnm->pObj : *(SceneNode **)(rec + 4);
            short yaw = *(short *)(rec + 8);
            short pitch = *(short *)(rec + 10);
            short roll = *(short *)(rec + 0xc);
            sceneObjSetPosOrient(obj, yaw, (short)-pitch, (short)-roll, 2);
            rec += 0x10;
            break;
        }
        case 5: {
            if (pAnm->pObj) {
                int ch = (signed char)rec[1];
                short sx = *(short *)(rec + 2);
                short sy = *(short *)(rec + 4);
                short sz = *(short *)(rec + 6);
                /* original passes extra floats (unaff) for sceneObjSetSubPos — pass 0.
                 * Verified @0x430a90: mode 2 never reads flPitch/nUnk/flFwd/flSide
                 * (only mode 5 does), so 0 is behaviorally identical to the
                 * original's stack garbage. */
                sceneObjSetSubPos(pAnm->pObj, ch, sx, sy, sz, 2, 0.0f, 0, 0.0f, 0.0f);
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
    pAnm->pCurTrack = (void *)((char *)pAnm->pCurTrack + 8);
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
                    int tx = *(int *)(rec + 8);
                    int ty = *(int *)(rec + 0xc);
                    int tz = *(int *)(rec + 0x10);
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
            rec += 0x14;
            break;
        }
        case 4: {
            SceneNode *mesh = *(SceneNode **)(rec + 4);
            if (mesh != 0) {
                short sx = *(short *)(rec + 8);
                short sy = *(short *)(rec + 10);
                short sz = *(short *)(rec + 0xc);
                sceneObjSetSubOrient(mesh, 0, sx, (short)-sy, (short)-sz);
            }
            rec += 0x10;
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
    int *pCur;
    byte *rec;
    int n, i;

    if (!pList) return 0;
    st = pList->pState;
    if (!st) return 0;
    if (st->nFrame >= st->nFrameCount) return 0;
    /* Original disasm reads {nRecs, pRecs} directly from pCurTrack, which
     * advances +8 per frame (no frame*8 re-indexing). */
    pCur = (int *)st->pCurTrack;
    n = pCur[0];
    rec = (byte *)pCur[1];
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
            int ax = *(int *)(rec + 8);
            int ay = -*(int *)(rec + 0xc);
            int az = -*(int *)(rec + 0x10);
            if (pList->apObjs[0] == 0) {
                if (mesh != 0) sceneObjSetPos(mesh, ax, ay, az, 2);
            } else {
                int k = 0;
                while (k < 5 && pList->apObjs[k] != 0) {
                    sceneObjSetPos(pList->apObjs[k], ax, ay, az, 2);
                    k++;
                }
            }
            rec += 0x14;
            break;
        }
        case 4: {
            short sx = *(short *)(rec + 8);
            short sy = *(short *)(rec + 10);
            short sz = *(short *)(rec + 0xc);
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
            rec += 0x10;
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
                    /* extra floats pass 0 — see eventAnimStep op5 note */
                    sceneObjSetSubPos(pList->apObjs[k], ch, sx, sy, sz, 2, 0.0f, 0, 0.0f, 0.0f);
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
    st->pCurTrack = (AnmTrack *)((char *)st->pCurTrack + 8);
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
    int *pCur;
    byte *rec;
    int n, i;

    if (!pList) return;
    st = pList->pState;
    if (!st) return;
    if (st->nFrame >= st->nFrameCount) {
        if ((bLoop & 1) == 0) return;
        st->nFrame = 0;
    }
    pCur = (int *)st->pCurTrack;
    n = pCur[0];
    rec = (byte *)pCur[1];
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
                    int tx = *(int *)(rec + 8);
                    int ty = *(int *)(rec + 0xc);
                    int tz = *(int *)(rec + 0x10);
                    /* Asymmetric midpoint per disasm 0x43485e-0x4348bd. */
                    int nx = curX + (tx - curX) / 2;
                    int ny = (ty + curY) / 2 - curY;
                    int nz = (tz + curZ) / 2 - curZ;
                    sceneObjSetPos(mesh, nx, -ny, -nz, 2);
                }
            }
            rec += 0x14;
            break;
        }
        case 4: {
            SceneNode *mesh = *(SceneNode **)(rec + 4);
            if (mesh != 0) {
                short sx = *(short *)(rec + 8);
                short sy = *(short *)(rec + 10);
                short sz = *(short *)(rec + 0xc);
                sceneObjSetSubOrient(mesh, 0, sx, (short)-sy, (short)-sz);
            }
            rec += 0x10;
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
