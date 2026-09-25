#ifndef ANIM_H
#define ANIM_H

#include "compat_types.h"

#include <stddef.h>

/* anim.h — .anm animation cluster (maniac.exe 0x433xxx/0x434xxx).
 * Reimplementation of the full loader + playback chain, verified against
 * disassembly and decompiler output.
 *
 *   dataReadU8/U16/U32   @0x433ee0/0x433ef0/0x433f10  stream helpers
 *   anmCalcSize          @0x433f40  sizing + allocation pass
 *   anmLoad              @0x433a90  parse .anm buffer -> AnmFile* (single arena)
 *   anmLoadFile          @0x433a50  fileReadRaw wrapper
 *   anmFree              @0x434050  release arena, destroy pool when last
 *   anmSetAlloc          @0x4344d0  0x18-byte holder (3 mesh slots + pAnm)
 *   anmSetFree           @0x434500  anmFree + memPoolFree holder
 *   anmSetMeshSlot       @0x434530  bind mesh pointer to holder slot
 *   eventAnimReset       @0x434270  rewind to frame 0
 *   eventAnimStep        @0x434090  single-obj opcode step (types 1-6)
 *   eventAnimApply       @0x434290  interpolation apply variant (midpoint)
 *   sceneObjectAnimStep  @0x434540  multi-obj list step
 *   sceneObjectAnimStepInterp @0x4347c0  interpolation variant (results screen)
 *
 * AnmFile is a native-size header followed by nTrack*sizeof(AnmTrack) track
 * table then record data, all in one memPoolAlloc arena (0x38 + nTrack*8 in
 * the 32-bit original). See anmLoad disassembly for layout.
 */

#ifndef byte
typedef unsigned char byte;
#endif

/* SceneNode is defined in scene.h (scene.h includes this header, so only a
 * forward declaration is possible here). The original stores scene-node
 * pointers in the anim state (AnmFile.pObj/pMasterNode) and in the record
 * stream (opcode 3/4 mesh field), verified vs disassembly of eventAnimStep
 * @0x434090 (0x43410c: obj dword pushed straight to sceneObjSetPos). */
typedef struct SceneNode SceneNode;

/* --- AnmFile: 0x38 header (offsets verified vs 0x433a90/0x434090/0x434290) --- */
typedef struct AnmFile {
    int   nFrame;          /* +0x00 current frame index */
    int   nFrameCount;     /* +0x04 total frames (u16 from header) */
    void *pTrackBase;      /* +0x08 pointer to track array (AnmTrack*) */
    void *pCurTrack;       /* +0x0c current track pointer (advances +8 per frame) */
    int   nLoopStart;      /* +0x10 loop start (always 0 in loader) */
    void *pPool;           /* +0x14 owning memPool handle (g_pAnmCacheList) */
    SceneNode *pObj;       /* +0x18 primary scene object (for op 3/4/5/6) */
    SceneNode *pMasterNode;/* +0x1c snap target for pos/facing (may be 0) */
    int   nPosX, nPosY, nPosZ;   /* +0x20 pos target (op1) */
    int   nFaceX, nFaceY, nFaceZ;/* +0x2c facing target (op2) */
} AnmFile; /* 0x38 */

/* Track: {int nRecs; void *pRecs} — 8 bytes in the 32-bit original, native
 * sizeof(AnmTrack) here; pRecs points into arena record data */
typedef struct AnmTrack {
    int   nRecs;
    void *pRecs;
} AnmTrack;

/* AnmSet holder. The original is 0x18 bytes (slots at +0,+4,+8, ints at
 * +0xc/+0x10, pAnm at +0x14). sceneObjectAnimStep/Interp take AnmSet* AS
 * SceneObjAnimList* (gameplay.c:240,1495ff): the list reader sees
 * apObjs[0..2]=pMesh slots, apObjs[3]=(nUnkC,nUnk10)=NULL terminator, and
 * pState=pAnm (AnmFile mirrors SceneObjAnimState). 64-bit port: explicit
 * padding keeps pAnm at the pState offset; the apObjs[3] NULL overlap is
 * preserved. See the AnmAliasCheck asserts in anim.c. */
typedef struct AnmSet {
    void   *pMesh[3];      /* +0x00/+0x08/+0x10 mesh slots (was +0/+4/+8) */
    int     nUnkC;         /* +0x18 (was +0x0c) */
    int     nUnk10;        /* +0x1c (was +0x10) */
    int     _pad20;        /* +0x20 padding (always 0) */
    AnmFile *pAnm;         /* +0x28 (was +0x14): aliases SceneObjAnimList.pState */
} AnmSet;

/* Owning pool globals — mirrors original g_pAnmCacheList @0x45ebc8 / g_nAnmCacheCount @0x45ebcc */
extern void *g_pAnmCacheList;   /* @0x45ebc8 memPool handle for AnmFile arenas */
extern int   g_nAnmCacheCount;  /* @0x45ebcc live AnmFile count */
extern char  g_szAnim[];        /* @0x45113c "Anim" pool tag */
extern byte  g_abAnmMagic[];    /* @0x451144 "ANM" magic */

/* stream helpers — unaligned little-endian reads (original dataRead* @0x433ee0 etc) */
byte  dataReadU8(byte *pData);                          /* @0x433ee0 */
unsigned short dataReadU16(byte *pData);                /* @0x433ef0 */
unsigned int   dataReadU32(byte *pData);                /* @0x433f10 */

/* sizing pass — computes arena size = sizeof(AnmFile) + nTrack*sizeof(AnmTrack)
 * + recordBytes (0x38 + nTrack*8 in the original), allocates via pPool */
void anmCalcSize(void *pPool, byte *pData, AnmFile **ppOut, void **ppTrackData, void **ppRecordData); /* @0x433f40 */

/* loader — validates "ANM" + version 1|2, builds track/record tables via scenNameToIdEx, returns AnmFile* */
AnmFile *anmLoad(byte *pData, SceneNode *pMasterNode, SceneNode *pObj); /* @0x433a90 */
AnmFile *anmLoadFile(LPCSTR pszPath, SceneNode *pMasterNode, SceneNode *pObj); /* @0x433a50 */
void anmFree(AnmFile *pAnm);                                   /* @0x434050 */

/* holder helpers */
AnmSet *anmSetAlloc(AnmFile *pAnm);                            /* @0x4344d0 */
void    anmSetFree(AnmSet *pSet);                              /* @0x434500 */
void    anmSetMeshSlot(AnmSet *pSet, void *pMesh, int nSlot); /* @0x434530 */

/* playback — opcode stream 1..6 (pos/facing, sceneObjSetPos, sceneObjSetPosOrient,
 * sceneObjSetSubPos, sceneObjSetPos list variant). eventAnim* is single-obj, sceneObjectAnim* is multi-obj. */
void eventAnimReset(AnmFile *pAnm);                            /* @0x434270 */
int  eventAnimStep(AnmFile *pAnm, byte bLoop);                 /* @0x434090 */
void eventAnimApply(AnmFile *pAnm, byte bLoop);                /* @0x434290 (void in Ghidra, takes AnmFile*) */

/* --- Multi-object anim state (sceneObjectAnimStep @0x434540). This struct is
 * NEVER allocated standalone: sceneObjectAnimStep/Interp receive an AnmSet*
 * as a SceneObjAnimList*, so pState IS the AnmFile* (see AnmSet above) and
 * every field below must sit at the same offset as its AnmFile counterpart
 * (verified by the AnmStateCheck asserts below). The 32-bit original used
 * one 0x38 layout for both; the 64-bit port widens the pointer fields, so
 * this struct mirrors AnmFile's widened layout field-for-field. A previous
 * revision kept the 32-bit offsets here (pMasterNode at +0x1c), which made
 * the steppers read AnmFile.pObj as pMasterNode and AnmFile.pMasterNode/
 * nPosX as nPosX/nPosY/nPosZ: every frame stomped character LOD slot 0's
 * local position with heap-pointer low bits (frustum-culled → invisible)
 * and broke the detail-grid distance for that row (stuck at lowest LOD).
 * Field roles verified vs disasm 0x434540: EBP+0x00 frame, +0x04 frameCount,
 * +0x08 loop reset target for +0x0c (pCurTrack, advances +8 per frame),
 * +0x1c pMasterNode, +0x20 pos target, +0x2c facing target (32-bit). --- */
typedef struct SceneObjAnimState {
    int        nFrame;       /* +0x00 current frame index (aliases AnmFile.nFrame) */
    int        nFrameCount;  /* +0x04 total frames */
    void      *pLoopBase;    /* +0x08 track-table base (aliases AnmFile.pTrackBase) */
    AnmTrack  *pCurTrack;    /* +0x10 current track pointer */
    int        nUnk18;       /* +0x18 unused (aliases AnmFile.nLoopStart) */
    int        _pad1c;       /* +0x1c alignment pad */
    void      *pUnk20;       /* +0x20 unused (aliases AnmFile.pPool) */
    void      *pUnk28;       /* +0x28 unused (aliases AnmFile.pObj) */
    SceneNode *pMasterNode;  /* +0x30 pos/facing snap target (may be 0) */
    int        nPosX, nPosY, nPosZ;     /* +0x38 pos target (op 1) */
    int        nFaceX, nFaceY, nFaceZ;  /* +0x44 facing target (op 2) */
} SceneObjAnimState;

/* The steppers view AnmFile memory through SceneObjAnimState: every used
 * field must alias exactly (C99 compile-time checks). */
typedef char AnmStateCheckFrame[offsetof(SceneObjAnimState, nFrame) == offsetof(AnmFile, nFrame) ? 1 : -1];
typedef char AnmStateCheckFrameCount[offsetof(SceneObjAnimState, nFrameCount) == offsetof(AnmFile, nFrameCount) ? 1 : -1];
typedef char AnmStateCheckLoopBase[offsetof(SceneObjAnimState, pLoopBase) == offsetof(AnmFile, pTrackBase) ? 1 : -1];
typedef char AnmStateCheckCurTrack[offsetof(SceneObjAnimState, pCurTrack) == offsetof(AnmFile, pCurTrack) ? 1 : -1];
typedef char AnmStateCheckMaster[offsetof(SceneObjAnimState, pMasterNode) == offsetof(AnmFile, pMasterNode) ? 1 : -1];
typedef char AnmStateCheckPosX[offsetof(SceneObjAnimState, nPosX) == offsetof(AnmFile, nPosX) ? 1 : -1];
typedef char AnmStateCheckPosY[offsetof(SceneObjAnimState, nPosY) == offsetof(AnmFile, nPosY) ? 1 : -1];
typedef char AnmStateCheckPosZ[offsetof(SceneObjAnimState, nPosZ) == offsetof(AnmFile, nPosZ) ? 1 : -1];
typedef char AnmStateCheckFaceX[offsetof(SceneObjAnimState, nFaceX) == offsetof(AnmFile, nFaceX) ? 1 : -1];
typedef char AnmStateCheckFaceY[offsetof(SceneObjAnimState, nFaceY) == offsetof(AnmFile, nFaceY) ? 1 : -1];
typedef char AnmStateCheckFaceZ[offsetof(SceneObjAnimState, nFaceZ) == offsetof(AnmFile, nFaceZ) ? 1 : -1];

/* Object list consumed by sceneObjectAnimStep/sceneObjectAnimStepInterp:
 * up to five scene nodes, 0-terminated, followed by the state pointer
 * (0x14/0x18 in the 32-bit original; native offsets here, kept compatible
 * with AnmSet above). Stored in the player record. */
typedef struct SceneObjAnimList {
    SceneNode         *apObjs[5]; /* 0-terminated object list */
    SceneObjAnimState *pState;    /* animation state (aliases AnmSet.pAnm) */
} SceneObjAnimList;

int  sceneObjectAnimStep(SceneObjAnimList *pList, byte bLoop);            /* @0x434540 */
void sceneObjectAnimStepInterp(SceneObjAnimList *pList, byte bLoop);      /* @0x4347c0 interpolation variant */

/* --- Linear keyframe interpolation (cross-platform port addition, no
 * original address). The original gameplay path snaps each stepped frame's
 * records straight onto the channels (sceneObjectAnimStep); only the
 * results-screen variant blends, and only at a fixed 0.5 midpoint with an
 * asymmetric integer formula. These helpers blend the effective keyframe
 * pose of the current frame with the effective pose of the next frame at
 * an explicit factor t in [0,1]:
 *   sceneObjectAnimStepLerp(pList, bLoop, t) — same frame cadence, loop
 *     handling and return contract as sceneObjectAnimStep, but type-5
 *     channel triples and the type-6 object position are applied as
 *     lerp(a, b, t) = trunc(a + (b - a) * t) (truncation toward zero,
 *     matching the original __ftol casts). Sparse tracks hold: a channel
 *     (or position) missing at one endpoint falls back to the endpoint
 *     that defines it; op1/op2 master targets and op3/op4 mesh records
 *     keep the original snap semantics. t is clamped to [0,1], so t=0
 *     reproduces the exact sceneObjectAnimStep pose of the current frame.
 *   Consumed by the player run/stand locomotion path in
 *   playerAnimSfxUpdate (t = 0.5, matching the original Interp midpoint
 *   convention). Action sets (pick/throw/grab/oops/winner) stay on the
 *   exact stepper so their state-machine timing is unchanged. */
float animClampT(float t);
short animLerpShort(short a, short b, float t);
int   animLerpInt(int a, int b, float t);
int   sceneObjectAnimStepLerp(SceneObjAnimList *pList, byte bLoop, float t);

/* g_awWalkAnimTable @0x458138 — 128 x {Bdg-orientation, search angle} shorts
 * precomputed by roundStartInit via walkAnimTableEntryCalc and consumed by
 * playerAnimOrientFromDir @0x4336b0. */
extern short g_awWalkAnimTable[128][2];                                /* @0x458138 */

/* walkAnimTableEntryCalc @0x433980 — [VERIFIED 2026-08-20] compute one entry
 * of g_awWalkAnimTable: binary-search the Bdg angle whose walk-circle foot
 * point distance from the origin reaches flNormSpeed*(center+radius), then
 * store pOut[0] = -(atan2 of the foot point) in Bdg units and
 * pOut[1] = the search angle. */
void walkAnimTableEntryCalc(short *pOut, float flNormSpeed,
                            int nCircleCenter, int nCircleRadius);  /* @0x433980 */

/* playerAnimOrientFromDir @0x4336b0 — 3-euler aim orientation + walk-table
 * limb swing (see anim.c; consumed by playerAnimSfxUpdate @0x40c800). */
int playerAnimOrientFromDir(int nDirX, int nDirY, int nDirZ,
                            short *pOutAngles, short *pOutWalk, void *pUnused,
                            const short *pWalkTable, int nWalkGeom,
                            short nRoll);                            /* @0x4336b0 */

#endif /* ANIM_H */
