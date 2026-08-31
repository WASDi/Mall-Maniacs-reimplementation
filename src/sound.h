#ifndef SOUND_H
#define SOUND_H

#include <windows.h>

/* =====================================================================
 * Sound subsystem — reimplementation of the maniac sample-bank and
 * playback interface using the same DirectSound output path as the
 * original: DirectSoundCreate + SetCooperativeLevel + streaming buffer,
 * with per-frame Lock/Write/Unlock of free write regions and a software
 * mixer (docs/09-sound.md). MCI CD-audio (the original's music path) is
 * intentionally not reproduced. Original addresses noted per symbol.
 *
 * Functions implemented here (maniac addresses):
 *   sndInitSystem     0x437a30
 *   sndShutdown       0x437cb0
 *   sndLoadBankFromDir 0x437170
 *   sndLoadWav        @0x437420
 *   sndPlaySfx        @0x437cf0
 *   sndMixTick        @0x437c50
 *   sndGetSample      @0x4377d0
 *   sndFreeBank       @0x437120
 *   sndSampleAlloc    @0x437830
 *   sndRegisterSample @0x437850
 *   sndSample16To8    @0x4377f0
 *   sndPcmUnsignedToSigned @0x4378a0
 *   sndBuildEnvProfile @0x437900
 *   sndResetBanks      @0x4370c0
 *   sndFreeAllSamples  @0x4370e0
 *   sndInitVoices      @0x438160
 *   sndCreateMixBuffer @0x437ef0
 *   sndFreeMixBuffers  @0x438140
 *   dsoundRelease      @0x438ff0
 * ===================================================================== */

/* --- global state (maniac addresses) --- */

/* g_nSoundInit @0x4511cc — init flag; -1 = not initialized. */
extern int g_nSoundInit;
/* g_bSoundMute @0x45f0d8 — muted flag (byte in original). */
extern char g_bSoundMute;
/* g_apSndBank @0x45ec94 — 16x16 pointer table of loaded samples. */
extern void *g_apSndBank[0x100];
/* g_pSndQueue @0x461ee8 — 0x100 free voice-slot pointers. */
extern void *g_pSndQueue[0x100];
/* g_nSndQueueCount @0x4622e8 — number of free voice slots. */
extern int g_nSndQueueCount;
/* g_nMixRateDivisor @0x46235c — rate divisor handed to pitched voices. */
extern int g_nMixRateDivisor;

/* --- public interface --- */

/* sndInitSystem @0x437a30 — init the sample banks + voice queue + DirectSound
 * streaming output; failure sets g_bSoundMute (the original keeps running
 * muted). */
int sndInitSystem(unsigned int nMixStereoConfig, unsigned short nVoiceCap,
                  int nFrameRegions);

/* sndShutdown @0x437cb0 — release samples, the DirectSound objects, and the
 * software mix buffers. */
int sndShutdown(void);

/* sndLoadBankFromDir @0x437170 — load digit-prefixed .wav files in pszDir
 * into one bank (nBank==0 auto-selects a free bank). 1 on success. */
int sndLoadBankFromDir(int nBank, char *pszDir);

/* sndPlaySfx @0x437cf0 — queue a one-shot effect. nBank/nSfxIndex select the
 * sample, nVolume 0..0xffff, nPitch (0 = sample default), nFlags the low byte
 * stored as the voice flags (0x200 loop, 0x400 rate-divisor, 0x800 scale
 * pitch). nMixerVoice is 0 for plain cues or the owning MusicEmitter record
 * pointer for 3D emitter voices. Returns a handle or 0 when dropped. */
int sndPlaySfx(int nMixerVoice, unsigned int nBank, unsigned int nSfxIndex,
               unsigned int nVolume, int nPitch, unsigned int nFlags);

/* sndMixTick @0x437c50 — per-frame: build the active voice chain, lock free
 * DirectSound write regions, render voices into them, and recycle finished
 * voices. */
int sndMixTick(unsigned int nFrameCounter);

/* --- 3D positional SFX emitters (sndPlaySfx3D @0x42bcd0 cluster; verified
 * 2026-08-30 vs decompiles/disassembly). A 0x1c-byte emitter block is
 * allocated by the caller (operator_new) and handed to sndPlaySfx3D, which
 * fills it and links it into g_pSndEmitterHead/Tail. sndEmitterUpdateAll
 * (@0x42bf40, called from gameRunFrame) frees finished non-persistent
 * emitters via sndEmitterUpdateFree @0x42bf60; owners free persistent ones
 * with sndEmitterFree @0x42bec0 + memFreeDirect. --- */
typedef struct SndEmitter {
    int nSndId;                 /* +0x00 snd id (dedupe key) */
    struct SndEmitter *pPrev;   /* +0x04 toward the older emitter (head = newest) */
    struct SndEmitter *pNext;   /* +0x08 toward the newer emitter */
    void *pPosNode;             /* +0x0c positional node (param 6) */
    void *pMusicEmitter;        /* +0x10 music emitter owner record
                                 * (MusicEmitter in sound.c; freed by
                                 * sndEmitterFree) */
    int nSfxHandle;             /* +0x14 queued one-shot voice (sndPlaySfx) */
    unsigned int nFlags;        /* +0x18 nFlags arg (bit0 = never auto-free,
                                 * bit1 = keep pos node on free, bit2 = dedupe
                                 * scan, bit3 = replace duplicate, bit4 = loop,
                                 * bit5 = randomized pitch) */
} SndEmitter;                   /* 0x1c */

extern SndEmitter *g_pSndEmitterHead;   /* @0x45e5f0 (newest) */
extern SndEmitter *g_pSndEmitterTail;   /* @0x45e5f4 (oldest) */

/* sndPlaySfx3D @0x42bcd0 — fill + link a caller-allocated emitter block and
 * queue its sample (see sound.c). The 3D music-module registration
 * (musicEmitterAlloc) is reproduced as the minimal MusicEmitter owner
 * record, which the mixer chain build uses as the voice's live 3D position. */
SndEmitter *sndPlaySfx3D(SndEmitter *pEmitter, unsigned int nBank,
                         unsigned int nIdx, unsigned int nVol, int nSndId,
                         void *pPosNode, int nEmitParam6, int nX, int nY,
                         int nZ, unsigned int nFlags);            /* @0x42bcd0 */

/* sndEmitterFree @0x42bec0 — unlink an emitter (and release its voice);
 * the caller frees the block with memFreeDirect. */
void sndEmitterFree(SndEmitter *pEmitter);                        /* @0x42bec0 */

/* sndVoiceIsActive @0x437e10 / sndFreeVoiceByHandle @0x437e40 — voice-slot
 * liveness / release by handle (see sound.c). */
int sndVoiceIsActive(unsigned int nHandle);                       /* @0x437e10 */
int sndFreeVoiceByHandle(unsigned int nHandle);                   /* @0x437e40 */

/* sndEmitterUpdateAll @0x42bf40 — updates/frees positional SFX emitters
 * after each active game frame (frees finished non-persistent ones via
 * sndEmitterUpdateFree @0x42bf60). */
void sndEmitterUpdateAll(void);

#endif /* SOUND_H */