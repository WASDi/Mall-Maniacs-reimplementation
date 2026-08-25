#ifndef ANIM_H
#define ANIM_H

#include <windows.h>

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
 * AnmFile is a 0x38-byte header followed by nTrack*8 track table then record
 * data, all in one memPoolAlloc arena. See anmLoad disassembly for layout.
 */

#ifndef byte
typedef unsigned char byte;
#endif

/* --- AnmFile: 0x38 header (offsets verified vs 0x433a90/0x434090/0x434290) --- */
typedef struct AnmFile {
    int   nFrame;          /* +0x00 current frame index */
    int   nFrameCount;     /* +0x04 total frames (u16 from header) */
    void *pTrackBase;      /* +0x08 pointer to track array (AnmTrack*) */
    void *pCurTrack;       /* +0x0c current track pointer (advances +8 per frame) */
    int   nLoopStart;      /* +0x10 loop start (always 0 in loader) */
    void *pPool;           /* +0x14 owning memPool handle (g_pAnmCacheList) */
    void *pObj;            /* +0x18 primary scene object (for op 3/4/5/6) */
    void *pMasterNode;     /* +0x1c snap target for pos/facing (may be 0) */
    int   nPosX, nPosY, nPosZ;   /* +0x20 pos target (op1) */
    int   nFaceX, nFaceY, nFaceZ;/* +0x2c facing target (op2) */
} AnmFile; /* 0x38 */

/* Track: 8 bytes {int nRecs; void *pRecs} — pRecs points into arena record data */
typedef struct AnmTrack {
    int   nRecs;
    void *pRecs;
} AnmTrack;

/* AnmSet holder: 0x18 bytes. Mesh slots at +0,+4,+8, padding at +0xc,+0x10, pAnm at +0x14 */
typedef struct AnmSet {
    void   *pMesh[3];      /* +0x00, +0x04, +0x08 */
    int     nUnkC;         /* +0x0c */
    int     nUnk10;        /* +0x10 */
    AnmFile *pAnm;         /* +0x14 */
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

/* sizing pass — computes arena size = 0x38 + nTrack*8 + recordBytes, allocates via pPool */
void anmCalcSize(void *pPool, byte *pData, AnmFile **ppOut, void **ppTrackData, void **ppRecordData); /* @0x433f40 */

/* loader — validates "ANM" + version 1|2, builds track/record tables via scenNameToIdEx, returns AnmFile* */
AnmFile *anmLoad(byte *pData, void *pMasterNode, void *pObj); /* @0x433a90 */
AnmFile *anmLoadFile(LPCSTR pszPath, void *pMasterNode, void *pObj); /* @0x433a50 */
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
int  sceneObjectAnimStep(int *pObjList, byte bLoop);           /* @0x434540 pObjList = int[5] ids + int[?] state at +0x14 */
void sceneObjectAnimStepInterp(int *pObjList, byte bLoop);     /* @0x4347c0 interpolation variant */

#endif /* ANIM_H */
