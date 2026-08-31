#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <windows.h>
#include <dsound.h>

#include "sound.h"
#include "scene.h"
#include "pool.h"
#include "custom_helpers.h"

/* =====================================================================
 * Sound subsystem — reimplementation of the maniac sample-bank and
 * playback interface using the same DirectSound output path as the
 * original: DirectSoundCreate + SetCooperativeLevel + streaming sound
 * buffer, with per-frame Lock/Write/Unlock of free write regions. A
 * software mixer renders the active voices into a stereo 16-bit scratch
 * which is then converted into the locked region in the negotiated
 * format. MCI CD-audio (the original's music path) is intentionally not
 * reproduced. See sound.h + docs/09-sound.md for the symbol map.
 * ===================================================================== */

/* --- sample record (maniac sndSampleAlloc @0x437830 layout) ---
 * 0x60-byte header + 8-bit signed sample data at +0x60
 *   +0x00 pData +0x04 nSamples +0x08 nLoopStart +0x0c nLoopEnd
 *   +0x10 envProfile[64] +0x50 nBits +0x54 nSampleRate
 *   +0x58 nEnvScale +0x5c nEnvShift
 */
typedef struct SndSample {
    unsigned char *pData;          /* +0x00 */
    int            nSamples;       /* +0x04 */
    int            nLoopStart;     /* +0x08 */
    int            nLoopEnd;       /* +0x0c */
    unsigned char  envProfile[64]; /* +0x10 */
    int            nBits;          /* +0x50 */
    int            nSampleRate;    /* +0x54 */
    int            nEnvScale;      /* +0x58 */
    int            nEnvShift;      /* +0x5c */
} SndSample;

/* ---- voice slot (0x2e bytes, maniac voice table @0x45f0e8) ---- */
typedef struct __attribute__((packed)) SndVoiceSlot {
    SndSample *pSample;    /* +0x00 [0] */
    int        nPos;       /* +0x04 [1] playback position (24.8) */
    int        nPosFrac;   /* +0x08 [2] fraction accumulator */
    int        nVolume;    /* +0x0c [3] gain 0..0xffff */
    int        nPitch;     /* +0x10 [4] !=0 while the voice is alive */
    int        nLoop;      /* +0x14 [5] remaining loops (0x7fffffff) */
    int        nFlags;     /* +0x18 [6] (char) flags */
    int        nHandle;    /* +0x1c [7] slot index + 0x10000 */
    int        nOwner;     /* +0x20 [8] mixer voice id */
    int       *pPos;       /* +0x24 [9] 3D position or centered marker */
    char       nVolL;      /* +0x28 [10] */
    char       nVolR;      /* +0x29 [11] */
    void      *pLink;      /* +0x2a chain link */
} SndVoiceSlot;                        /* total 0x2e */

/* ---- voice set (maniac g_snde0 @0x45f0e0) ---- */
typedef struct SndVoiceSet {
    SndVoiceSlot *pChainA;             /* +0x00 active chain */
    SndVoiceSlot *pChainB;             /* +0x04 (kept for layout) */
    unsigned char aSlots[0x100 * 0x2e];/* +0x08 the 0x100 voice slots */
    int           nMasterVol;          /* +0x2e08 (+0x320c in set) 0x10000 */
} SndVoiceSet;

/* Music-emitter owner record — minimal stand-in for the 0xa8 scene-node
 * emitter musicEmitterAlloc @0x431b00 allocates. The mixer chain build
 * (sndMixBuildVoiceChains @0x437fe0) reads owner+0x24 and, when it equals 1
 * (musicCbInitEmitter @0x437b40 clears it, musicModulePosCheck @0x437b60
 * sets it), aims the voice's 3D position at owner+0x28. The rebuild keeps
 * just those fields; the position cache is refreshed per frame in
 * sndEmitterUpdateAll (the original refreshes it through the streaming
 * module's per-emitter callback). Field offsets match the original so the
 * chain-build logic stays byte-compatible. */
typedef struct MusicEmitter {
    unsigned char aReserved[0x24]; /* +0x00 scene-node fields not needed */
    int           nActive;         /* +0x24 active flag (chain build: ==1) */
    int           anPos[3];        /* +0x28 cached emitter position */
} MusicEmitter;

/* =====================================================================
 * Global state (maniac addresses)
 * ===================================================================== */
int   g_nSoundInit      = -1;          /* @0x4511cc */
char  g_bSoundMute      = 0;           /* @0x45f0d8 */
void *g_apSndBank[0x100];              /* @0x45ec94 */
void *g_pSndQueue[0x100];              /* @0x461ee8 */
int   g_nSndQueueCount;                /* @0x4622e8 */
int   g_nMixRateDivisor;               /* @0x46235c */

static LPDIRECTSOUND       g_pDSoundObj;       /* @0x46237c */
static LPDIRECTSOUNDBUFFER g_pDsBufferPrimary; /* @0x4623d0 */
static int                 g_nDsBufferSel;     /* @0x462384 */
static int                 g_nDsBufferSize;    /* @0x4623d8 */
static int                 g_nDsFrameBytes;    /* @0x4623d4 */
static int                 g_nDsFrameCount;    /* @0x462394 */
static int                 g_nDsSampleRate;    /* @0x462388 */
static int                 g_nDsBits;          /* @0x46238c */
static int                 g_nDsChannels1;     /* @0x462390 */
static int                 g_nDsBlockAlign1;   /* @0x462398 */
static int                 g_nDsWriteBase;     /* @0x462368 */
static int                 g_nDsWriteLimit;    /* @0x4623ac */
static int                 g_nDsPlayCursor;    /* @0x462380 */
static int                 g_nDsWriteCursor;   /* @0x462370 */
static int                 g_nDsWriteLen;      /* @0x46236c */
static int                 g_nDsLockResult;    /* @0x4623b8 */
static void               *g_pDsLockPtr;       /* @0x462374 */
static int                 g_nDsLockSize;      /* @0x4623b0 */
static void               *g_pDsLockPtr2;      /* @0x462378 */
static int                 g_nDsLockSize2;     /* @0x4623b4 */
static int                 g_nDsPlaying;       /* @0x4623dc */

static int      g_nMixActive;         /* @0x4623e0 */
static int      g_nMixScratchSamples; /* @0x4622f8 */
static int      g_nMixPanMode;        /* @0x4622fc */
static void    *g_pMixRateTable;      /* @0x462304 */
static int      g_nMixRateStep;       /* @0x462308 */
static void    *g_pMixWaveTable;      /* @0x46230c */
static int      g_nMixScratchReady;   /* @0x462310 */
static void    *g_pMixScratch;        /* @0x462314 */
static int      g_nMix16Bit;          /* @0x462318 */
static int      g_nMixMasterVol;      /* @0x46231c */
static int      g_nMixVoiceCap;       /* @0x462320 */
static int      g_nMixBufferBytes;    /* @0x462334 */
static int      g_nMixFormat;         /* @0x462338 */
static void    *g_pMixScratch2;       /* @0x46233c */
static int      g_nMixSrcVoiceCap;    /* @0x462340 */
static int      g_nMixSrcSampleRate;  /* @0x462344 */
static int      g_nMixSrcBits;        /* @0x462348 */
static char     g_bMixSrcChannels;    /* @0x46234c */
static int      g_nMixSrcFrameCount;  /* @0x462350 */
static int      g_nMixSrcBlockAlign;  /* @0x462354 */
static int      g_nMixStereoConfig;   /* @0x462358 */
/* ---- software mixer core (maniac sndMixVoiceCore @0x4395b0) ----
 * The core mixes one voice through the wave table. Its 7-dword record
 * matches the local block sndVoiceRender @0x438b00 stacks (mirrors the
 * register-level handoff in the original). */
typedef struct SndMixRec {
    SndSample    *pSample;   /* +0x00 sample record */
    int           nPos;      /* +0x04 source byte position */
    int           nPosFrac;  /* +0x08 fixed-point fraction */
    int           nRate;     /* +0x0c pitch divisor (src rate) */
    short        *pWaveL;    /* +0x10 L wave-table row */
    short        *pWaveR;    /* +0x14 R wave-table row */
    int           nLoop;     /* +0x18 remaining loops */
} SndMixRec;

/* Mixer parameter block, maniac 0x462328: params[1] (+4) holds the source
 * sample rate divisor the core divides the voice pitch by. */
static int g_nMixParams[2]; /* @0x462328 */

/* Core state (maniac @0x4511d0+). */
static int          g_nMixCoreRemain;   /* @0x4511d0 */
static short       *g_pMixCoreVoice;    /* @0x4511d4 */
static SndMixRec   *g_pMixCoreWave;     /* @0x4511d8 */
static int         *g_pMixCoreScratch;  /* @0x4511dc */
static int          g_nMixCoreSteps;    /* @0x4511e0 */
static int          g_nMixCoreRateDiv;  /* @0x4511e4 */
static unsigned int g_nMixCoreFrac;     /* @0x4511e8 */
static int          g_nMixCoreSrcBase;  /* @0x4511ec */

/* DSMix write region descriptor (maniac @0x4623c0, a contiguous 4-dword
 * struct) — kept as one struct so sndMixRenderRegion's pRegion[0..3]
 * indexing follows the original memory layout. */
typedef struct SndMixRegion {
    int nStart;   /* +0x00 @0x4623c0 g_nMixRegionStart */
    int nSize;    /* +0x04 @0x4623c4 g_nMixRegionSize  (lockPtr2) */
    int nDiv1;    /* +0x08 @0x4623c8 g_nMixRegionDiv1 */
    int nDiv2;    /* +0x0c @0x4623cc g_nMixRegionDiv2 */
} SndMixRegion;
static SndMixRegion s_mixRegion;

static SndVoiceSet s_voiceSet;        /* maniac 0x45f0e0 */

static const char g_szWavExt[] = ".WAV";  /* @0x4511b4 */
static const char g_szDirGlob[]   = "\\*";   /* @0x4511bc tail */

/* =====================================================================
 * Math helpers
 * ===================================================================== */

/* mathFixedRecip @0x4370a0 — floor(2^32 / nDivisor). */
static unsigned int mathFixedRecip(unsigned int nDivisor)
{
    return (unsigned int)((1U % (unsigned long long)nDivisor << 0x20) /
                          (unsigned long long)nDivisor);
}

/* =====================================================================
 * Bank / sample management
 * ===================================================================== */

/* sndGetSample @0x4377d0 — fetch a bank slot pointer. */
void *sndGetSample(int nBank, int nSlot)
{
    return g_apSndBank[nBank * 0x10 + nSlot];
}

/* sndResetBanks @0x4370c0 — clear the bank table. */
int sndResetBanks(void)
{
    memset(g_apSndBank, 0, sizeof(g_apSndBank));
    return 1;
}

/* sndFreeBank @0x437120 — free one bank's 16 sample slots. */
int sndFreeBank(int nBank)
{
    int slot;
    if (nBank < 1 || nBank >= 0x11) return 0;
    for (slot = 0; slot < 0x10; slot++) {
        if (g_apSndBank[nBank * 0x10 + slot] != NULL) {
            free(g_apSndBank[nBank * 0x10 + slot]);
            g_apSndBank[nBank * 0x10 + slot] = NULL;
        }
    }
    return 1;
}

/* sndFreeAllSamples @0x4370e0 — free every loaded sample slot. */
int sndFreeAllSamples(void)
{
    int i;
    for (i = 0; i < 0x100; i++) {
        if (g_apSndBank[i] != NULL) { free(g_apSndBank[i]); g_apSndBank[i] = NULL; }
    }
    return 1;
}

/* sndSampleAlloc @0x437830 — allocate a sample record with 0x60-byte header. */
void *sndSampleAlloc(int nBytes)
{
    SndSample *s = (SndSample *)malloc((size_t)nBytes + 0x60);
    if (s == NULL) return NULL;
    s->pData       = (unsigned char *)s + 0x60;
    s->nSamples    = 0;
    s->nLoopStart  = 0;
    s->nLoopEnd    = 0;
    s->nBits       = 0;
    s->nSampleRate = 0;
    s->nEnvScale   = 0;
    s->nEnvShift   = 0;
    return s;
}

/* sndRegisterSample @0x437850 — store pSample into a bank slot. */
int sndRegisterSample(int nBank, int nSlot, void *pSample)
{
    int idx = nBank * 0x10 + nSlot;
    if (g_apSndBank[idx] != NULL) free(g_apSndBank[idx]);
    g_apSndBank[idx] = pSample;
    return 1;
}

/* sndSample16To8 @0x4377f0 — downsample 16-bit to 8-bit (keep high byte). */
void sndSample16To8(void *pSample)
{
    SndSample *s = (SndSample *)pSample;
    unsigned short *src = (unsigned short *)s->pData;
    int i;
    if (s->nSamples < 1) { s->nBits = 8; return; }
    for (i = 0; i < s->nSamples; i++) s->pData[i] = (unsigned char)(src[i] >> 8);
    s->nBits = 8;
}

/* sndPcmUnsignedToSigned @0x4378a0 — flip the sign bit for unsigned PCM. */
int sndPcmUnsignedToSigned(void *pSample)
{
    SndSample *s = (SndSample *)pSample;
    int i;
    if (s->nBits == 8) {
        for (i = 0; i < s->nSamples; i++) s->pData[i] ^= 0x80;
        return 1;
    }
    if (s->nBits != 0x10) return 0;
    for (i = 0; i < s->nSamples; i++) ((unsigned short *)s->pData)[i] ^= 0x8000;
    return 1;
}

/* sndBuildEnvProfile @0x437900 — build the 64-entry average-delta envelope
 * used by sndVoicePriorityUpdate when it culls voices over the mix cap. */
int sndBuildEnvProfile(void *pSample)
{
    SndSample *s = (SndSample *)pSample;
    int segment = s->nSamples - 1;
    int profile[64];
    int maxProfile = 0;
    int nShift = 0;
    int i;

    segment = (int)(segment + (segment >> 31 & 0x3f)) >> 6;
    if (segment < 1) segment = 1;
    s->nEnvShift = 8;
    s->nEnvScale = (int)mathFixedRecip((unsigned int)(segment + 1));

    for (i = 0; i < 64; i++) {
        int sum = 0;
        int j;
        if (s->nBits == 8) {
            for (j = 0; j < segment; j++) {
                int delta = (int)(signed char)s->pData[i * segment + j] -
                            (int)(signed char)s->pData[i * segment + j + 1];
                sum += (delta < 0) ? -delta : delta;
            }
            profile[i] = (sum << 8) / segment;
        } else {
            const short *pData16 = (const short *)s->pData;
            for (j = 0; j < segment; j++) {
                int delta = (int)pData16[i * segment + j] -
                            (int)pData16[i * segment + j + 1];
                sum += (delta < 0) ? -delta : delta;
            }
            profile[i] = sum / segment;
        }
        if (maxProfile < profile[i]) maxProfile = profile[i];
    }

    while (0xff < maxProfile) {
        maxProfile >>= 1;
        nShift++;
    }
    s->nEnvShift = 8 - nShift;
    for (i = 0; i < 64; i++) s->envProfile[i] = (unsigned char)(profile[i] >> nShift);
    return 1;
}

/* =====================================================================
 * WAV loader
 * ===================================================================== */

/* sndLoadWav @0x437420 — parse a RIFF WAV into bank/slot. 1 on success. The
 * RIFF header check is inlined here exactly as the original does it (the
 * original reads 12 bytes and compares the "RIFF"/"WAVE" magic before the
 * chunk walk). */
int sndLoadWav(LPCSTR pszFilename, int nBank, int nSlot)
{
    FILE          *fp;
    unsigned char  hdr[12];
    int            riffSize, pos;
    SndSample     *s = NULL;
    int            gotFmt = 0, nBits = 0, nRate = 0, nChans = 0, dataSize = 0;

    fp = fopen(pszFilename, "rb");
    if (fp == NULL) return 0;
    if (fread(hdr, 1, 12, fp) != 12) { fclose(fp); return 0; }
    if (hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F') { fclose(fp); return 0; }
    if (hdr[8] != 'W' || hdr[9] != 'A' || hdr[10] != 'V' || hdr[11] != 'E') { fclose(fp); return 0; }
    riffSize = (int)(hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | ((int)hdr[7] << 24));
    if (riffSize == 0) { fclose(fp); return 0; }

    pos = 4;
    while (pos < riffSize - 4) {
        unsigned char chunk[8];
        int  id, size;
        long next;
        if (fread(chunk, 1, 8, fp) != 8) break;
        id   = chunk[0] | (chunk[1] << 8) | (chunk[2] << 16) | ((int)chunk[3] << 24);
        size = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | ((int)chunk[7] << 24);
        next = ftell(fp) + (size + (size & 1));
        if (id == 0x20746d66) {                 /* "fmt " */
            unsigned char f[16];
            if (size >= 16 && fread(f, 1, 16, fp) == 16) {
                nChans = f[2] | (f[3] << 8);
                nRate  = f[4] | (f[5] << 8) | (f[6] << 16) | ((int)f[7] << 24);
                nBits  = f[0xe] | (f[0xf] << 8);
                gotFmt = (((int)f[0] | (int)f[1] << 8) == 1);
            }
        } else if (id == 0x61746164) {          /* "data" */
            dataSize = size;
            s = (SndSample *)sndSampleAlloc(size);
            if (s == NULL) { fclose(fp); return 0; }
            if (fread(s->pData, 1, (size_t)size, fp) != (size_t)size) {
                free(s); fclose(fp); return 0;
            }
        }
        if (next > ftell(fp)) { if (fseek(fp, next, SEEK_SET) != 0) break; }
        pos = (int)next;
    }
    fclose(fp);

    if (s == NULL || !gotFmt || nChans != 1) { if (s != NULL) free(s); return 0; }
    if (nBits == 0x10) {
        s->nSamples = dataSize / 2; s->nBits = 0x10; sndSample16To8(s);
    } else if (nBits == 8) {
        s->nSamples = dataSize; s->nBits = 8; sndPcmUnsignedToSigned(s);
    } else {
        free(s); return 0;
    }
    s->nSampleRate = nRate;
    s->nLoopStart  = 0;
    s->nLoopEnd    = s->nSamples;
    sndBuildEnvProfile(s);
    return sndRegisterSample(nBank, nSlot, s);
}

/* sndLoadBankFromDir @0x437170 — scan pszDir for digit-prefixed .wav files
 * and load them into one bank (nBank==0 auto-selects a free bank). The free
 * bank scan, leading-index parse, and ".WAV" suffix check are inlined here
 * exactly as the original does them (no separate helper functions). */
int sndLoadBankFromDir(int nBank, char *pszDir)
{
    char             search[MAX_PATH];
    char             full[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE           hFind;
    int              selBank = nBank;

    if (nBank < 0 || nBank > 0x10) return 0;
    if (nBank == 0) {
        int bank, slot;
        for (bank = 1; bank < 0x11; bank++) {
            int used = 0;
            for (slot = 0; slot < 0x10; slot++)
                if (g_apSndBank[bank * 0x10 + slot] != NULL) { used = 1; break; }
            if (!used) break;
        }
        selBank = bank;
        if (selBank > 0x10) return 0;
    } else {
        sndFreeBank(selBank);
    }
    strcpy(search, pszDir);
    while (strlen(search) > 0 &&
           (search[strlen(search) - 1] == '\\' || search[strlen(search) - 1] == '/')) {
        search[strlen(search) - 1] = '\0';
    }
    strcat(search, g_szDirGlob);

    hFind = FindFirstFileA(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;
    do {
        const char *name = fd.cFileName;
        const char *ext;
        int idx = 0, loaded;
        if ((fd.dwFileAttributes & 0x16) != 0) continue;
        if (name[0] < '0' || name[0] > '9') continue;
        {
            const char *p = name;
            while (p[0] >= '0' && p[0] <= '9') {
                idx = idx * 10 + (p[0] - '0'); p++;
            }
        }
        /* ".WAV" suffix check (case-insensitive extension compare). */
        ext = strrchr(name, '.');
        {
            int i, len;
            int isWav = 0;
            if (ext != NULL) {
                len = (int)strlen(ext);
                if (len == 4) {
                    isWav = 1;
                    for (i = 1; i < 4; i++) {
                        char c = ext[i];
                        if (c >= 'a' && c <= 'z') c = (char)(c - 0x20);
                        if (c != g_szWavExt[i]) { isWav = 0; break; }
                    }
                }
            }
            if (!isWav) continue;
        }
        snprintf(full, sizeof(full), "%s\\%s", pszDir, name);
        loaded = sndLoadWav(full, selBank, idx);
        if (loaded != 1) { FindClose(hFind); return 0; }
    } while (FindNextFileA(hFind, &fd) != 0);
    FindClose(hFind);
    return 1;
}

/* =====================================================================
 * Software mix buffer
 * ===================================================================== */

/* sndBuildWaveTable @0x4381c0 — build the software mixer's wavetable. */
static void sndBuildWaveTable(int nParam)
{
    int iVar1, iVar2, iVar3, iVar4, iVar5, iVar6, iVar7;
    short *t = (short *)g_pMixWaveTable;
    iVar1 = nParam * 2; iVar2 = 0; iVar4 = 0; iVar5 = 0;
    iVar3 = nParam * -0x100; nParam = 0x40;
    do {
        iVar6 = 0; iVar7 = 0x80;
        do { t[iVar2++] = (short)((unsigned int)iVar6 >> 16); iVar6 += iVar4; iVar7--; } while (iVar7 != 0);
        iVar7 = 0x80; iVar6 = iVar5;
        do { t[iVar2++] = (short)((unsigned int)iVar6 >> 16); iVar6 += iVar4; iVar7--; } while (iVar7 != 0);
        iVar4 += iVar1; iVar5 += iVar3; nParam--;
    } while (nParam != 0);
    iVar5 = 0; iVar4 = 0; nParam = 0x40;
    do {
        iVar6 = 0; iVar7 = 0x80;
        do { t[iVar2++] = -(short)((unsigned int)iVar6 >> 16); iVar6 += iVar5; iVar7--; } while (iVar7 != 0);
        iVar7 = 0x80; iVar6 = iVar4;
        do { t[iVar2++] = -(short)((unsigned int)iVar6 >> 16); iVar6 += iVar5; iVar7--; } while (iVar7 != 0);
        iVar5 += iVar1; iVar4 += iVar3; nParam--;
    } while (nParam != 0);
}

/* sndCreateMixBuffer @0x437ef0 — allocate the software mix buffers. */
int sndCreateMixBuffer(void)
{
    if (g_nMixSrcVoiceCap <= 1 || g_nMixSrcVoiceCap >= 0x101) return 0;
    g_nMixVoiceCap       = g_nMixSrcVoiceCap;
    g_nMixBufferBytes    = g_nMixSrcBlockAlign;
    g_nMix16Bit          = (int)g_bMixSrcChannels;
    g_nMixPanMode        = g_nMixStereoConfig;
    g_nMixFormat         = g_nMixSrcBits;
    g_nMixRateStep       = (g_nMixSrcSampleRate << 8) / g_nMixSrcFrameCount;
    g_nMixScratchSamples = g_nMixSrcFrameCount;
    if (g_nMixScratchSamples < 1) g_nMixScratchSamples = 1;

    g_pMixRateTable = malloc(0x10200);
    if (g_pMixRateTable == NULL) return 0;
    g_pMixWaveTable = (void *)(((int)g_pMixRateTable + 0x1ff) & ~0x1ff);
    sndBuildWaveTable(0x20000 / g_nMixVoiceCap);
    g_pMixScratch2 = malloc((size_t)g_nMixScratchSamples * 4);
    if (g_pMixScratch2 == NULL) {
        free(g_pMixRateTable); g_pMixRateTable = NULL;
        return 0;
    }
    g_pMixScratch = g_pMixScratch2;
    g_nMixScratchReady = 0;
    g_nMixParams[1]    = g_nMixSrcSampleRate;   /* core divider @0x46232c */
    return 1;
}

/* sndFreeMixBuffers @0x438140. */
int sndFreeMixBuffers(void)
{
    if (g_pMixScratch2 != NULL) { free(g_pMixScratch2); g_pMixScratch2 = NULL; }
    if (g_pMixRateTable != NULL) { free(g_pMixRateTable); g_pMixRateTable = NULL; }
    return 1;
}

/* =====================================================================
 * Voice set / queue
 * ===================================================================== */

/* sndInitVoices @0x438160 — link the free queue from the voice slots. The
 * original takes the voice-set address (0x45f0e0); the rebuild uses its own
 * static voice set, so the argument is accepted and ignored. */
int sndInitVoices(int pVoiceSet)
{
    int i;
    (void)pVoiceSet;
    memset(&s_voiceSet, 0, sizeof(s_voiceSet));
    g_nSndQueueCount = 0;
    for (i = 0; i < 0x100; i++) {
        SndVoiceSlot *v = (SndVoiceSlot *)s_voiceSet.aSlots + i;
        v->nPitch   = 0;
        v->nVolume  = 0;
        v->pSample  = NULL;
        v->pLink    = NULL;
        v->nHandle  = i + 0x10000;
        v->nOwner   = 0;
        v->pPos     = NULL;
        g_pSndQueue[i] = v;              /* queue == voice set +0x2e08 */
        g_nSndQueueCount++;
    }
    s_voiceSet.nMasterVol = 0x10000;
    g_nMixMasterVol = 0x10000;
    return 1;
}

/* =====================================================================
 * DirectSound output
 * ===================================================================== */

/* dsoundRelease @0x438ff0. */
int dsoundRelease(void)
{
    if (g_nMixActive != 1) return 0;
    g_nMixActive = 0;
    if (g_pDSoundObj != NULL) {
        if (g_pDsBufferPrimary != NULL) {
            IDirectSoundBuffer_Stop(g_pDsBufferPrimary);
            IDirectSoundBuffer_Release(g_pDsBufferPrimary);
            g_pDsBufferPrimary = NULL;
        }
        IDirectSound_Release(g_pDSoundObj);
        g_pDSoundObj = NULL;
    }
    return 0;
}

static int dsoundInitMixer(int nFrameRegions)
{
    static const struct Fmt { int ch; int bits; int rate; } kFmt[6] = {
        {2, 16, 44100}, {2, 16, 22050}, {2, 8, 22050},
        {1, 16, 44100}, {1, 16, 22050}, {1, 8, 22050}
    };
    HRESULT h;
    int i;

    if (g_nMixActive == 1) return 1;
    g_nDsWriteLimit = 0;
    g_nDsPlaying    = 0;
    g_nMixActive    = 0;
    g_nDsBufferSel  = 1;

    h = DirectSoundCreate(NULL, &g_pDSoundObj, NULL);
    if (h != DS_OK) { g_pDSoundObj = NULL; return 0; }
    h = IDirectSound_SetCooperativeLevel(g_pDSoundObj, GetForegroundWindow(),
                                         DSSCL_NORMAL);
    if (h != DS_OK) { dsoundRelease(); return 0; }

    for (i = 0; i < 6; i++) {
        WAVEFORMATEX  w;
        DSBUFFERDESC  dsc;
        int  ch   = kFmt[i].ch;
        int  bits = kFmt[i].bits;
        int  rate = kFmt[i].rate;
        int  nFrameByte  = ch * bits / 8;
        int  nRegionFrames = rate / 100;
        int  nBufferBytes = nRegionFrames * nFrameByte * (nFrameRegions + 10);

        w.wFormatTag      = WAVE_FORMAT_PCM;
        w.nChannels       = (WORD)ch;
        w.nSamplesPerSec  = (DWORD)rate;
        w.wBitsPerSample  = (WORD)bits;
        w.nBlockAlign     = (WORD)nFrameByte;
        w.nAvgBytesPerSec = (DWORD)(rate * nFrameByte);
        w.cbSize          = 0;

        memset(&dsc, 0, sizeof(dsc));
        dsc.dwSize        = sizeof(dsc);
        dsc.dwBufferBytes = (DWORD)nBufferBytes;
        dsc.lpwfxFormat   = &w;
        g_pDsBufferPrimary = NULL;
        h = IDirectSound_CreateSoundBuffer(g_pDSoundObj, &dsc,
                                           &g_pDsBufferPrimary, NULL);
        if (h != DS_OK || g_pDsBufferPrimary == NULL) {
            if (g_pDsBufferPrimary != NULL) {
                IDirectSoundBuffer_Release(g_pDsBufferPrimary);
                g_pDsBufferPrimary = NULL;
            }
            continue;
        }
        g_nDsSampleRate   = rate;
        g_nDsBits         = bits;
        g_nDsChannels1    = ch - 1;
        g_nDsBlockAlign1  = (bits / 8) - 1;
        g_nDsFrameBytes   = nFrameByte;
        g_nDsFrameCount   = nRegionFrames;
        g_nDsBufferSize   = nBufferBytes;
        g_nDsWriteBase    = nRegionFrames * nFrameByte * nFrameRegions;
        break;
    }
    if (g_pDsBufferPrimary == NULL) { dsoundRelease(); return 0; }

    /* Zero the DirectSound primary buffer (inlined from the original:
     * lock the whole buffer, clear both wrapped regions, unlock). */
    {
        void  *p1 = NULL, *p2 = NULL;
        DWORD  s1 = 0, s2 = 0;
        if (IDirectSoundBuffer_Lock(g_pDsBufferPrimary, 0, (DWORD)g_nDsBufferSize,
                                    &p1, &s1, &p2, &s2, 0) == DS_OK) {
            if (p1 != NULL) memset(p1, 0, (size_t)s1);
            if (p2 != NULL) memset(p2, 0, (size_t)s2);
            IDirectSoundBuffer_Unlock(g_pDsBufferPrimary, p1, s1, p2, s2);
        }
    }
    IDirectSoundBuffer_SetCurrentPosition(g_pDsBufferPrimary, 0);
    h = IDirectSoundBuffer_Play(g_pDsBufferPrimary, 0, 0, DSBPLAY_LOOPING);
    if (h != DS_OK) appLog("[sound] dsInit: Play failed hr=%08x", (unsigned)h);
    g_nDsPlaying = 1;
    g_nMixActive = 1;
    return 1;
}

/* sndGetWriteRegion @0x438cd0 — lock the next free DirectSound write region.
 * Returns &s_mixRegion {start, size, div1, div2} or NULL when no free
 * space. Mirrors the original: cursor = playCursor + writeBase, free-space
 * rejection per the write-limit fence, and buffer-lost restore. */
static void *sndGetWriteRegion(void)
{
    LPDIRECTSOUNDBUFFER b = g_pDsBufferPrimary;
    HRESULT h;
    if (g_nMixActive != 1 || b == NULL) return NULL;
    if (g_nDsPlaying == 0) {
        IDirectSoundBuffer_Play(b, 0, 0, DSBPLAY_LOOPING);
        g_nDsPlaying = 1;
    }
    if (IDirectSoundBuffer_GetCurrentPosition(b, (LPDWORD)&g_nDsPlayCursor,
                                              (LPDWORD)&g_nDsWriteCursor) != DS_OK) return NULL;
    g_nDsWriteCursor = g_nDsPlayCursor + g_nDsWriteBase;
    if (g_nDsWriteCursor >= g_nDsBufferSize) g_nDsWriteCursor -= g_nDsBufferSize;

    g_nDsWriteLen = g_nDsFrameBytes * g_nDsFrameCount + g_nDsWriteCursor;
    if (g_nDsWriteLen < g_nDsBufferSize) {
        if (g_nDsWriteLimit < g_nDsWriteLen && g_nDsWriteCursor <= g_nDsWriteLimit)
            return NULL;                                        /* not yet free */
    } else {
        g_nDsWriteLen = g_nDsWriteLen - g_nDsBufferSize;
        if (g_nDsWriteLimit < g_nDsWriteLen || g_nDsWriteCursor <= g_nDsWriteLimit)
            return NULL;                                        /* wrapped, not free */
    }

    h = IDirectSoundBuffer_Lock(b, (DWORD)g_nDsWriteLimit,
                                (DWORD)(g_nDsFrameBytes * g_nDsFrameCount),
                                &g_pDsLockPtr, (LPDWORD)&g_nDsLockSize,
                                &g_pDsLockPtr2, (LPDWORD)&g_nDsLockSize2, 0);
    g_nDsLockResult = (int)h;
    if (h == DSERR_BUFFERLOST) {                                /* restore buffer */
        IDirectSoundBuffer_Restore(b);
        IDirectSoundBuffer_Play(b, 0, 0, DSBPLAY_LOOPING);
        return NULL;
    }
    if (h != DS_OK) return NULL;

    s_mixRegion.nStart = (int)g_pDsLockPtr;
    s_mixRegion.nSize  = (int)g_pDsLockPtr2;
    s_mixRegion.nDiv1  = (int)g_nDsLockSize / g_nDsFrameBytes;
    s_mixRegion.nDiv2  = (int)g_nDsLockSize2 / g_nDsFrameBytes;
    return (void *)&s_mixRegion;
}

/* sndMixAdvanceUnlock @0x438f20 — unlock the region and advance the write
 * limit by one frame's bytes with wrap at the buffer length. */
static int sndMixAdvanceUnlock(void)
{
    if (g_nMixActive == 1) {
        IDirectSoundBuffer_Unlock(g_pDsBufferPrimary,
                                  g_pDsLockPtr, (DWORD)g_nDsLockSize,
                                  g_pDsLockPtr2, (DWORD)g_nDsLockSize2);
        g_nDsWriteLimit = g_nDsWriteLimit + g_nDsFrameBytes * g_nDsFrameCount;
        if (g_nDsBufferSize <= g_nDsWriteLimit) {
            do {
                g_nDsWriteLimit = g_nDsWriteLimit - g_nDsBufferSize;
            } while (g_nDsBufferSize <= g_nDsWriteLimit);
        }
    }
    return 0;
}

/* sndClearMixBuffer @0x438bb0 — stop playback and zero the DirectSound buffer. */
static int sndClearMixBuffer(void)
{
    if (g_nMixActive != 1) return 0;
    IDirectSoundBuffer_Stop(g_pDsBufferPrimary);
    /* Zero the DirectSound primary buffer (inlined; see sndClearMixBuffer
     * @0x438bb0 — lock the whole buffer, clear both wrapped regions). */
    {
        void  *p1 = NULL, *p2 = NULL;
        DWORD  s1 = 0, s2 = 0;
        if (IDirectSoundBuffer_Lock(g_pDsBufferPrimary, 0, (DWORD)g_nDsBufferSize,
                                    &p1, &s1, &p2, &s2, 0) == DS_OK) {
            if (p1 != NULL) memset(p1, 0, (size_t)s1);
            if (p2 != NULL) memset(p2, 0, (size_t)s2);
            IDirectSoundBuffer_Unlock(g_pDsBufferPrimary, p1, s1, p2, s2);
        }
    }
    g_nDsPlaying = 0;
    return 0;
}

/* =====================================================================
 * Software mixing
 * ===================================================================== */

/* sndMixScratchReset @0x4382b0 — zero the interleaved stereo scratch when it
 * is not ready; sets the ready flag. */
static void sndMixScratchReset(void)
{
    if (g_pMixScratch == NULL) return;
    if (g_nMixScratchReady == 0) {
        memset(g_pMixScratch, 0, (size_t)g_nMixScratchSamples * 4);
        g_nMixScratchReady = 1;
    }
}

/* sndFixedStepAdd @0x437ea0 — advance a fixed-point (int+frac) position by
 * (nStep<<8)/nRate with the running fraction. Mirrors the original's
 * fraction-carry math (sndFixedStepAdd). */
static void sndFixedStepAdd(int nStep, int *pPos, int *pFrac,
                            unsigned int nRate)
{
    unsigned int uStep = (unsigned int)(nStep << 8);
    unsigned int uOld, uNew, uSum, uCarry;

    if (nRate == 0) return;
    uNew = (unsigned int)(((unsigned long long)(uStep % nRate) << 0x20) /
                          (unsigned long long)nRate) & 0xfffffff0;
    uOld = (unsigned int)*pFrac;
    uSum = uOld + uNew;
    uCarry = (uSum < uOld) ? 1U : 0U;
    *pFrac = (int)uSum;
    *pPos += (int)(uStep / nRate + uCarry);
}

/* Render all active voices into the scratch for nFrames output samples. */
/* sndFixedMul @0x437ed0 — fixed-point multiply, high 32 bits of (a*b). */
static int sndFixedMul(unsigned int a, unsigned int b)
{
    return (int)(((unsigned long long)a * (unsigned long long)b) >> 0x20);
}

/* sndVolFromPos @0x4389a0 — 3D position -> L/R 16-bit volume attenuation.
 * Reads pPos[0..2], clamps each to +-0x7c17, computes
 * 0x640000/(sqrt(x^2+y^2+z^2)+0x64) and derives the two pan-weighted
 * volumes. Pan mode 0 = mono (both identical). Out-of-range -> 0/0. */
static void sndVolFromPos(short *pOutVol, int *pPos)
{
    int x = pPos[0] >> 4;
    int y = pPos[1] >> 4;
    int z = pPos[2] >> 4;
    int sq, dist, side;

    if (x < -0x7c17 || x > 0x7c17 ||
        y < -0x7c17 || y > 0x7c17 ||
        z < -0x7c17 || z > 0x7c17) {
        pOutVol[0] = 0;
        pOutVol[1] = 0;
        return;
    }
    sq = x*x + y*y + z*z;
    dist = (int)sqrt((double)sq);
    dist = 0x640000 / (dist + 0x64);
    if (g_nMixPanMode == 0) {
        pOutVol[0] = (short)dist;
        pOutVol[1] = (short)dist;
        return;
    }
    sq = x*x + z*z;
    side = (int)sqrt((double)sq);
    side = ((z < 0 ? -z : z) + 1) * 0x3fff / (side + 1);
    if (x >= 0) {
        pOutVol[0] = (short)side;
        pOutVol[1] = 0x3fff;
    } else {
        pOutVol[0] = 0x3fff;
        pOutVol[1] = (short)side;
    }
    if (g_nMixPanMode == 2 && z < 0) pOutVol[0] = (short)-pOutVol[0];
    pOutVol[0] = (short)((pOutVol[0] * dist) >> 16);
    pOutVol[1] = (short)((pOutVol[1] * dist) >> 16);
}

/* sndMixBuildVoiceChains @0x437fe0 — build the per-tick voice chains from the
 * 0x100 voice slot table: chain A = renderable voices, chain B = advance-only
 * voices. A voice with a live position pointer (pPos) always goes to chain A.
 * A position-less voice is backed by its owner music-emitter record: when the
 * owner is set and active (owner+0x24 == 1) the voice borrows the owner's
 * cached position (pPos = owner+0x28) and joins chain A; otherwise it goes to
 * chain B (silently advanced). The original takes the submitted voice-list
 * address; the rebuild uses its own static voice set, so the list argument is
 * accepted and ignored. */
static void sndMixBuildVoiceChains(int nFrameCounter)
{
    int i;
    SndVoiceSlot *pTailA = NULL;
    SndVoiceSlot *pTailB = NULL;
    (void)nFrameCounter;
    s_voiceSet.pChainA = NULL;
    s_voiceSet.pChainB = NULL;
    for (i = 0; i < 0x100; i++) {
        SndVoiceSlot *v = (SndVoiceSlot *)s_voiceSet.aSlots + i;
        MusicEmitter *pOwner;

        v->pLink = NULL;
        if (v->nPitch == 0) continue;
        if (v->pPos == NULL) {
            pOwner = (MusicEmitter *)v->nOwner;
            if (pOwner == NULL || pOwner->nActive != 1) {
                if (pTailB == NULL) s_voiceSet.pChainB = v;   /* chain B */
                else pTailB->pLink = v;
                pTailB = v;
                continue;
            }
            v->pPos = pOwner->anPos;            /* owner + 0x28 */
        }
        if (pTailA == NULL) s_voiceSet.pChainA = v;           /* chain A */
        else pTailA->pLink = v;
        pTailA = v;
    }
}

/* sndVoicePriorityUpdate @0x4387a0 — per-voice volume/priority update for
 * the active chain: sets each voice's 8-bit L/R volumes from its 3D position
 * (via sndVolFromPos), the mixer master volume, and the voice gain; computes
 * a loudness score (env-profile sample * pitch scaled by the volume and
 * flags); and, when more voices are active than g_nMixVoiceCap, drops the
 * quietest until only the cap remain. */
static void sndVoicePriorityUpdate(void)
{
    SndVoiceSlot *v;
    SndVoiceSlot *aList[0x100];
    int aScore[0x100];
    int n = 0;
    int i;

    for (v = s_voiceSet.pChainA; v != NULL; v = (SndVoiceSlot *)v->pLink) {
        SndSample *p = v->pSample;
        char cL, cR;
        short aPos[2];
        int aL, aR, am, iScore, iEnv;

        if (p == NULL || v->nPitch == 0) continue;
        if (v->pPos != NULL) {
            sndVolFromPos(aPos, v->pPos);
        } else {
            aPos[0] = 0x3fff;   /* centered */
            aPos[1] = 0x3fff;
        }
        cL = (char)(((aPos[0] * g_nMixMasterVol >> 16) * v->nVolume) >> 0x18);
        cR = (char)(((aPos[1] * g_nMixMasterVol >> 16) * v->nVolume) >> 0x18);
        v->nVolL = cL;
        v->nVolR = cR;

        aL = (int)(char)v->nVolL;
        aR = (int)(char)v->nVolR;
        if (aL < 0) aL = -aL;
        if (aR < 0) aR = -aR;
        am = (aL > aR) ? aL : aR;
        /* loudness: env-profile byte indexed by the fixed-mul of pos and the
         * env scale, scaled by pitch, the volume, and the flags */
        iEnv = (int)sndFixedMul((unsigned int)v->nPos,
                                (unsigned int)p->nEnvScale);
        iScore = ((int)((unsigned int)p->envProfile[iEnv & 0xff] *
                        (unsigned int)v->nPitch) >>
                  (p->nEnvShift & 0x1f));
        iScore = (iScore * (am >> 1) >> 8) * ((int)(char)v->nFlags + 0x80);
        aList[n] = v;
        aScore[n] = iScore;
        n++;
    }

    if (n > g_nMixVoiceCap) {
        /* drop the (n - cap) quietest; the rebuild uses a same-cost
         * selection over the scored list in place of the original's
         * repeated lowest-score unlink */
        int kept = 0;
        int iDrop;
        while (kept < n - g_nMixVoiceCap) {
            iDrop = 0;
            for (i = 1; i < n; i++)
                if (aScore[i] < aScore[iDrop]) iDrop = i;
            aScore[iDrop] = 0x7fffffff;
            aList[iDrop] = NULL;
            kept++;
        }
        s_voiceSet.pChainA = NULL;
        for (i = n - 1; i >= 0; i--) {
            if (aList[i] == NULL) continue;
            aList[i]->pLink = s_voiceSet.pChainA;
            s_voiceSet.pChainA = aList[i];
        }
    }
}

/* sndVoiceAdvancePosition @0x438730 — advance a voice's fixed-point playback
 * position by the rate divisor, then handle looping while past the loop end;
 * mark voice finished (nPitch = 0) when past the sample end. */
static void sndVoiceAdvancePosition(SndVoiceSlot *pVoice)
{
    while (pVoice != NULL) {
        SndSample *p = pVoice->pSample;
        if (p == NULL) { pVoice = (SndVoiceSlot *)pVoice->pLink; continue; }
        {
            int         nPos = pVoice->nPos;
            int         nFrac = pVoice->nPosFrac;
            int         nLoop = pVoice->nLoop;
            sndFixedStepAdd(pVoice->nPitch, &nPos, &nFrac,
                            (unsigned int)g_nMixRateStep);
            while (nLoop != 0) {
                int iEnd = p->nLoopStart + p->nLoopEnd;
                if (nPos < iEnd) break;
                nLoop--;
                nPos -= p->nLoopEnd;
            }
            pVoice->nPos = nPos;
            pVoice->nPosFrac = nFrac;
            pVoice->nLoop = nLoop;
            if (nPos >= p->nSamples) pVoice->nPitch = 0;
        }
        pVoice = (SndVoiceSlot *)pVoice->pLink;
    }
}

/* sndMixSamples @0x4396ea — mix `nSteps` source bytes from the voice's
 * current position into the interleaved stereo scratch using the two
 * wave-table rows (pWaveL/pWaveR), advancing the source by the fixed-point
 * step (g_nMixCoreRateDiv int + g_nMixCoreFrac frac) per output frame as the
 * original does. */
static void sndMixSamples(int nSteps, unsigned int nPosFrac, int nRateDiv,
                          unsigned int nCoreFrac)
{
    SndMixRec   *rec = g_pMixCoreWave;
    unsigned char *pSrc = (unsigned char *)(g_nMixCoreSrcBase + rec->nPos);
    short       *pScr = g_pMixCoreVoice;
    short       *pL = rec->pWaveL;
    short       *pR = rec->pWaveR;
    unsigned int f = nPosFrac;
    int    nDiv = nRateDiv;
    unsigned int nFr = nCoreFrac;
    int i;

    for (i = 0; i < nSteps; i++) {
        unsigned int b    = *pSrc;
        unsigned int fOld = f;
        pScr[0] = (short)((int)pScr[0] + (int)pL[b]);
        pScr[1] = (short)((int)pScr[1] + (int)pR[b]);
        pScr += 2;
        f = f + nFr;
        pSrc = pSrc + nDiv + (unsigned int)(f < fOld);   /* ADC carry */
    }
    /* write the advanced position + fraction back into the record so the
     * caller's next sndMixStep starts where this one left off. */
    rec->nPos     = (int)(pSrc - (unsigned char *)g_nMixCoreSrcBase);
    rec->nPosFrac = (int)f;
}

/* sndMixStep @0x4396a6 — mix one block of `g_nMixCoreSteps` frames through
 * sndMixSamples from the voice source offset g_nMixCoreSrcBase + rec->nPos,
 * using the fixed-point step (g_nMixCoreRateDiv int + g_nMixCoreFrac frac)
 * set up by sndMixVoiceCore. nSrc is the source data pointer
 * (rec->pSample->pData), nFrac is the leftover division result passed
 * through the original call (kept for the original call shape). */
static long long sndMixStep(int nSrc, unsigned int nFrac)
{
    SndMixRec *rec = g_pMixCoreWave;

    (void)nFrac;
    g_nMixCoreSrcBase = nSrc;
    sndMixSamples(g_nMixCoreSteps, (unsigned int)rec->nPosFrac,
                  g_nMixCoreRateDiv, g_nMixCoreFrac);
    return ((long long)(unsigned int)rec->nPosFrac << 32) |
           (unsigned int)rec->nPos;
}

/* sndMixVoiceCore @0x4395b0 — render one voice into the shared scratch for
 * `nRemain` output frames. The fixed-point step width is derived from the
 * record's pitch divisor and the param block's source sample rate (params[1],
 * original 0x46232c); blocks are mixed via sndMixStep, looping sources wrap
 * at nLoopEnd and non-looping voices stop (nRate = 0) at sample end. Fills
 * the mixer globals (g_nMixCore*) exactly as the register-passed original
 * does: pScratch -> g_pMixCoreVoice, rec -> g_pMixCoreWave. */
static void sndMixVoiceCore(int nFrames, SndMixRec *rec, short *pScratch,
                            int *pParams)
{
    unsigned int rate = 0;
    unsigned int pitch;

    if (pParams != NULL) rate = (unsigned int)pParams[1];
    if (rate == 0 || rec == NULL) {
        if (rec != NULL) rec->nRate = 0;
        return;
    }
    pitch = (unsigned int)rec->nRate;
    g_nMixCoreRateDiv  = (int)(pitch / rate);
    g_nMixCoreFrac     = (unsigned int)
        (((unsigned long long)(pitch % rate) << 0x20) /
         (unsigned long long)rate) & 0xfffffff0;
    g_nMixCoreRemain   = nFrames;
    g_pMixCoreVoice    = pScratch;
    g_pMixCoreWave     = rec;
    g_pMixCoreScratch  = pParams;

    while (g_nMixCoreRemain > 0) {
        SndSample *pS = rec->pSample;
        unsigned long long u64;
        uint32_t uVar5, uVar4, uRes;
        int iVar3;

        if (rec->nLoop == 0)
            iVar3 = (pS != NULL) ? (pS->nSamples - rec->nPos) : 0;
        else
            iVar3 = (pS != NULL)
                        ? (pS->nLoopStart + pS->nLoopEnd - rec->nPos) : 0;
        uVar5 = (uint32_t)0 - (uint32_t)rec->nPosFrac;
        uVar4 = (uint32_t)iVar3 - (uint32_t)(rec->nPosFrac != 0);

        if (g_nMixCoreRateDiv == 0) {
            u64 = ((unsigned long long)uVar4 << 0x20) | uVar5;
            g_nMixCoreSteps = (int)(u64 / (unsigned long long)g_nMixCoreFrac);
            uRes = (unsigned int)(u64 % (unsigned long long)g_nMixCoreFrac);
        }
        else {
            unsigned long long div =
                (unsigned long long)((unsigned int)(g_nMixCoreFrac >> 4) |
                                     ((unsigned int)g_nMixCoreRateDiv << 0x1c));
            u64 = ((unsigned long long)(uVar4 >> 4) << 0x20)
                  | (unsigned)((uVar5 >> 4) | (uVar4 << 0x1c));
            g_nMixCoreSteps = (int)(u64 / div);
            uRes = (unsigned int)(u64 % div);
        }
        if (uRes != 0) g_nMixCoreSteps++;

        if (g_nMixCoreRemain < g_nMixCoreSteps) {
            g_nMixCoreSteps = g_nMixCoreRemain;
            sndMixStep(pS ? (int)(uintptr_t)pS->pData : 0, uRes);
            break;
        }
        {
            int stepCount = g_nMixCoreSteps;
            sndMixStep(pS ? (int)(uintptr_t)pS->pData : 0, uRes);
            if (rec->nLoop == 0) {
                rec->nRate = 0;
                break;
            }
            g_pMixCoreVoice =
                (short *)((char *)g_pMixCoreVoice + stepCount * 2);
            g_nMixCoreRemain -= stepCount;
        }
        rec->nLoop -= 1;
        rec->nPos -= (pS != NULL) ? pS->nLoopEnd : 0;
    }
}

/* sndVoiceRender @0x438b00 — render one voice into the shared scratch buffer
 * through the software mixer core. Builds the mixer record (SndMixRec) from
 * the voice slot, selects the L/R wave-table rows from the two 8-bit voice
 * volumes (abs(vol) * 0x200 + g_pMixWaveTable, +0x8000 if negative — the
 * descending half of the triangle), runs sndMixVoiceCore against the global
 * wave table and the mixer parameter block, then folds the adjusted
 * position / fraction / pitch back into the voice slot. */
static void sndVoiceRender(SndVoiceSlot *pVoice, short *pScratch,
                           int nFrames, int *pParams)
{
    SndMixRec rec;
    int       volL = (int)pVoice->nVolL;
    int       volR = (int)pVoice->nVolR;
    int       absL = abs(volL);
    int       absR = abs(volR);

    rec.pSample  = pVoice->pSample;
    rec.nPos     = pVoice->nPos;
    rec.nPosFrac = pVoice->nPosFrac;
    rec.nRate    = pVoice->nPitch;
    rec.pWaveL   = (short *)((int)g_pMixWaveTable + absL * 0x200);
    rec.pWaveR   = (short *)((int)g_pMixWaveTable + absR * 0x200);
    if (volL < 0) rec.pWaveL = (short *)((int)rec.pWaveL + 0x8000);
    if (volR < 0) rec.pWaveR = (short *)((int)rec.pWaveR + 0x8000);
    rec.nLoop    = pVoice->nLoop;

    sndMixVoiceCore(nFrames, &rec, pScratch, pParams);

    pVoice->nPos     = rec.nPos;
    pVoice->nPosFrac = rec.nPosFrac;
    pVoice->nPitch   = rec.nRate;
}

/* sndVoiceUpdateFinished @0x4386c0 — walk the active chain, render each still
 * playing voice into the scratch (marking it dirty), then advance the
 * positions of the remaining chain entries beyond the voice cap. */
static void sndVoiceUpdateFinished(SndVoiceSlot *pChain)
{
    int n = 0;

    if (g_nMixVoiceCap > 0) {
        while (n < g_nMixVoiceCap && pChain != NULL) {
            if (pChain->nPitch != 0 && pChain->nPos < pChain->pSample->nSamples) {
                sndVoiceRender(pChain, (short *)g_pMixScratch,
                               g_nMixScratchSamples, g_nMixParams);
                g_nMixScratchReady = 0;
            }
            pChain = (SndVoiceSlot *)pChain->pLink;
            n++;
        }
    }
    sndVoiceAdvancePosition(pChain);
}

/* sndVoiceReclaimFinished @0x438630 — walk both voice chains and push any
 * voice whose finished flag (nPitch == 0) is set back onto the free queue,
 * unlinking it from the chain. */
static void sndVoiceReclaimFinished(void)
{
    SndVoiceSlot *aChains[2];
    int c;

    aChains[0] = s_voiceSet.pChainA;
    aChains[1] = s_voiceSet.pChainB;
    for (c = 0; c < 2; c++) {
        SndVoiceSlot *v = aChains[c];
        SndVoiceSlot *pPrev = NULL;
        while (v != NULL) {
            SndVoiceSlot *pNext = (SndVoiceSlot *)v->pLink;
            if (v->nPitch == 0) {
                if (pPrev == NULL) {
                    if (c == 0) s_voiceSet.pChainA = pNext;
                    else        s_voiceSet.pChainB = pNext;
                } else {
                    pPrev->pLink = pNext;
                }
                v->pSample = NULL;
                v->nPos    = 0;
                v->nPosFrac= 0;
                v->nLoop   = 0;
                v->pLink   = NULL;
                if (g_nSndQueueCount < 0x100) {
                    g_pSndQueue[g_nSndQueueCount] = v;
                    g_nSndQueueCount++;
                }
            } else {
                pPrev = v;
            }
            v = pNext;
        }
    }
}

/* sndMixScratchToBuffer @0x4382f0 — convert scratch[frames] into the locked
 * DirectSound region. g_nMixBufferBytes is the original format selector:
 * zero means unsigned PCM (bias the sample), one means signed PCM. */
static void sndMixScratchToBuffer(void *dst, int nFrames, int nOffsetFrames)
{
    short *src = (short *)g_pMixScratch + nOffsetFrames * 2;
    int    stereo = g_nMix16Bit;
    int    k;
    if (dst == NULL || nFrames <= 0) return;
    if (g_nMixScratchReady == 1) {
        if (g_nMixFormat == 0x10) {
            unsigned short *o = (unsigned short *)dst;
            unsigned short silence = (g_nMixBufferBytes == 0) ? 0x8000 : 0;
            for (k = 0; k < (stereo ? nFrames * 2 : nFrames); k++)
                o[k] = silence;
        } else {
            unsigned char *o = (unsigned char *)dst;
            unsigned char silence = (g_nMixBufferBytes == 0) ? 0x80 : 0;
            for (k = 0; k < (stereo ? nFrames * 2 : nFrames); k++)
                o[k] = silence;
        }
        return;
    }
    if (g_nMixFormat == 0x10) {
        unsigned short *o = (unsigned short *)dst;
        if (stereo) {
            for (k = 0; k < nFrames; k++) {
                o[k * 2]     = (unsigned short)src[k * 2];
                o[k * 2 + 1] = (unsigned short)src[k * 2 + 1];
                if (g_nMixBufferBytes == 0) {
                    o[k * 2]     ^= 0x8000;
                    o[k * 2 + 1] ^= 0x8000;
                }
            }
        } else {
            for (k = 0; k < nFrames; k++) {
                o[k] = (unsigned short)((src[k * 2] >> 1) + (src[k * 2 + 1] >> 1));
                if (g_nMixBufferBytes == 0) o[k] ^= 0x8000;
            }
        }
    } else {
        unsigned char *o = (unsigned char *)dst;
        if (stereo) {
            for (k = 0; k < nFrames; k++) {
                o[k * 2]     = (unsigned char)(src[k * 2] >> 8);
                o[k * 2 + 1] = (unsigned char)(src[k * 2 + 1] >> 8);
                if (g_nMixBufferBytes == 0) {
                    o[k * 2]     ^= 0x80;
                    o[k * 2 + 1] ^= 0x80;
                }
            }
        } else {
            for (k = 0; k < nFrames; k++) {
                o[k] = (unsigned char)(((src[k * 2] >> 1) + (src[k * 2 + 1] >> 1)) >> 8);
                if (g_nMixBufferBytes == 0) o[k] ^= 0x80;
            }
        }
    }
}

/* sndMixRenderRegion @0x438090 — render a locked DSound write region from the
 * active voice set. pRegion points at s_mixRegion {start, size, div1, div2}.
 * Mirrors the original call order: latch mixer volume, priority/volume
 * update, scratch reset, finish-check + advance chain A, advance chain B,
 * reclaim finished voices, then convert the scratch twice into the locked
 * region(s). */
static int sndMixRenderRegion(void *pRegion)
{
    int *pi = (int *)pRegion;

    g_nMixMasterVol = s_voiceSet.nMasterVol;
    sndVoicePriorityUpdate();
    sndMixScratchReset();
    sndVoiceUpdateFinished(s_voiceSet.pChainA);
    sndVoiceAdvancePosition(s_voiceSet.pChainB);
    sndVoiceReclaimFinished();
    sndMixScratchToBuffer((void *)pi[0], pi[2], 0);
    if (pi[1] != 0 && pi[3] != 0) {
        sndMixScratchToBuffer((void *)pi[1], pi[3], pi[2]);
    }
    return 1;
}

/* =====================================================================
 * Public interface
 * ===================================================================== */

/* sndPlaySfx @0x437cf0 — queue a one-shot effect. nMixerVoice is 0 for plain
 * cues or the owning MusicEmitter record pointer for 3D emitter voices (the
 * chain build borrows the owner's cached position when the voice has none). */
int sndPlaySfx(int nMixerVoice, unsigned nBank, unsigned nSfxIndex,
               unsigned nVolume, int nPitch, unsigned nFlags)
{
    SndSample *p;
    SndVoiceSlot *v;
    if (g_nSoundInit == -1) return 0;
    if (g_bSoundMute) return 0;
    p = (SndSample *)sndGetSample((int)(nBank & 0xff), (int)(nSfxIndex & 0xff));
    if (p == NULL) return 0;
    if (g_nSndQueueCount == 0) return 0;   /* no free slots */

    g_nSndQueueCount--;
    v = (SndVoiceSlot *)g_pSndQueue[g_nSndQueueCount];
    v->pSample = p;
    v->nVolume = (int)(nVolume & 0xffff);
    if (nPitch == 0) v->nPitch = p->nSampleRate;
    else if ((nFlags & 0x800) != 0)
        v->nPitch = (p->nSampleRate * (nPitch >> 4)) >> 12;
    else v->nPitch = nPitch;
    if (v->nPitch > 160000) v->nPitch = 160000;
    v->nLoop   = ((nFlags & 0x200) != 0) ? 0x7fffffff : 0;
    v->nFlags  = (int)(char)nFlags;
    v->nOwner  = nMixerVoice;
    v->pPos    = ((nFlags & 0x400) != 0 && nMixerVoice == 0)
                     ? &g_nMixRateDivisor : NULL;
    v->nHandle += 0x10000;
    v->nPos    = 0;
    v->nPosFrac= 0;
    appLog("[sound] sndPlaySfx bank %d slot %d vol %d pitch %d flags 0x%x",
           (int)(nBank & 0xff), (int)(nSfxIndex & 0xff),
           v->nVolume, v->nPitch, nFlags);
    return v->nHandle;
}

/* sndMixTick @0x437c50 — per-frame software mixer tick. If unmuted,
 * repeatedly locks the next free DirectSound write region (sndGetWriteRegion),
 * builds the voice chains once, mixes each region (sndMixRenderRegion), and
 * unlocks it (sndMixAdvanceUnlock) until no region is available. Queued
 * sndPlaySfx requests are drained by this loop. */
int sndMixTick(unsigned nFrameCounter)
{
    void *pRegion;

    if (g_nSoundInit == -1) return 0;
    if (g_bSoundMute == 0) {
        pRegion = sndGetWriteRegion();
        if (pRegion != NULL) {
            sndMixBuildVoiceChains((int)nFrameCounter);
            do {
                sndMixRenderRegion(pRegion);
                sndMixAdvanceUnlock();
                pRegion = sndGetWriteRegion();
            } while (pRegion != NULL);
        }
    }
    return 1;
}

/* sndInitSystem @0x437a30 — init the sample banks + voice queue + DirectSound
 * streaming output; failure sets g_bSoundMute like the original. */
int sndInitSystem(unsigned nMixStereoConfig, unsigned short nVoiceCap,
                  int nFrameRegions)
{
    if (g_nSoundInit != -1) return 0;
    g_bSoundMute = 0;
    if (!sndResetBanks()) return 0;
    g_nSoundInit = 1;

    if (!dsoundInitMixer(nFrameRegions)) {
        appLog("[sound] sndInitSystem: DirectSound unavailable — muted");
        g_bSoundMute = 1;
        g_nSoundInit = 1;
        return 1;   /* the original also keeps running muted */
    }
    sndClearMixBuffer();

    g_nMixSrcBits         = g_nDsBits;
    g_nMixSrcSampleRate   = g_nDsSampleRate;
    g_bMixSrcChannels     = (char)g_nDsChannels1;
    g_nMixSrcBlockAlign   = g_nDsBlockAlign1;
    g_nMixSrcVoiceCap     = (int)nVoiceCap;
    g_nMixStereoConfig    = (g_nDsChannels1 != 0) ? (int)nMixStereoConfig : 0;
    g_nMixSrcFrameCount   = g_nDsFrameCount;

    if (!sndCreateMixBuffer()) {
        sndFreeAllSamples();
        dsoundRelease();
        g_nSoundInit = -1;
        return 0;
    }
    sndInitVoices(0x45f0e0);
    appLog("[sound] sndInitSystem: DirectSound %dHz/%d-bit x%d step %d",
           g_nDsSampleRate, g_nDsBits, g_nDsChannels1 + 1, g_nMixRateStep);
    return 1;
}

/* sndShutdown @0x437cb0. */
int sndShutdown(void)
{
    if (g_nSoundInit == -1) return 0;
    if (!g_bSoundMute) {
        dsoundRelease();
        sndFreeMixBuffers();
    }
    sndFreeAllSamples();
    g_nSoundInit = -1;
    return 1;
}

/* =====================================================================
 * 3D positional SFX emitters (sndPlaySfx3D @0x42bcd0 cluster)
 * ===================================================================== */

SndEmitter *g_pSndEmitterHead;  /* @0x45e5f0 (newest) */
SndEmitter *g_pSndEmitterTail;  /* @0x45e5f4 (oldest) */

/* sndVoiceIsActive @0x437e10 — the voice slot for nHandle (index =
 * nHandle & 0xffff) is occupied: its stored id matches the handle and the
 * sample is still queued. Maps onto the rebuild voice table (nHandle at
 * +0x1c, sample at +0x00; the original reads slot+0 == handle over its own
 * 0x2e-byte slot layout). */
int sndVoiceIsActive(unsigned int nHandle) /* @0x437e10 */
{
    SndVoiceSlot *v;

    if ((nHandle & 0xffff) >= 0x100) {
        return 0;   /* safe deviation: the original indexes its 0x100-slot table blindly */
    }
    v = (SndVoiceSlot *)s_voiceSet.aSlots + (nHandle & 0xffff);
    return (unsigned)v->nHandle == nHandle && v->pSample != NULL;
}

/* sndFreeVoiceByHandle @0x437e40 — stop/release the mixer voice with the
 * given handle: when the slot matches and the voice is still live
 * (nPitch != 0), kill it and push the slot back onto the free queue. The
 * original clears the slot's pitch field (+0x10) — the chain-build
 * liveness flag — so the voice can neither be rebuilt into a mix chain nor
 * reach a stale position/sample afterwards; the rebuild additionally
 * clears pSample for its own liveness test (see sndVoiceIsActive).
 * Returns 1. */
int sndFreeVoiceByHandle(unsigned int nHandle) /* @0x437e40 */
{
    SndVoiceSlot *v;
    int nIdx = (int)(nHandle & 0xffff);

    if (nIdx >= 0x100) {
        return 1;
    }
    v = (SndVoiceSlot *)s_voiceSet.aSlots + nIdx;
    if ((unsigned)v->nHandle == nHandle && v->nPitch != 0) {
        v->nPitch = 0;
        v->pSample = NULL;
        g_pSndQueue[g_nSndQueueCount] = v;
        g_nSndQueueCount++;
    }
    return 1;
}

/* sndEmitterFree @0x42bec0 — release an emitter's voice and unlink it from
 * the list. The caller owns the 0x1c block (memFreeDirect after this).
 * The +0x10 music-emitter record is released inside the same !(nFlags & 2)
 * branch; the original calls sceneNodeFree(pMusicEmitter, 1), the rebuild
 * frees its minimal MusicEmitter record directly. */
void sndEmitterFree(SndEmitter *pEmitter) /* @0x42bec0 */
{
    if ((pEmitter->nFlags & 2) == 0) {
        if (pEmitter->nSfxHandle != 0 && sndVoiceIsActive((unsigned)pEmitter->nSfxHandle)) {
            sndFreeVoiceByHandle((unsigned)pEmitter->nSfxHandle);
        }
        if (pEmitter->pMusicEmitter != NULL) {
            memFreeDirect(pEmitter->pMusicEmitter);   /* sceneNodeFree(pMusicEmitter,1) */
            pEmitter->pMusicEmitter = NULL;
        }
    }
    if (g_pSndEmitterHead == pEmitter) {
        g_pSndEmitterHead = pEmitter->pPrev;
    }
    if (g_pSndEmitterTail == pEmitter) {
        g_pSndEmitterTail = pEmitter->pNext;
    }
    if (pEmitter->pPrev != NULL) {
        pEmitter->pPrev->pNext = pEmitter->pNext;
    }
    if (pEmitter->pNext != NULL) {
        pEmitter->pNext->pPrev = pEmitter->pPrev;
    }
}

/* sndEmitterUpdateFree @0x42bf60 — free an emitter whose queued voice has
 * finished, unless the persistent flag (bit 0) is set. */
static void sndEmitterUpdateFree(SndEmitter *pEmitter) /* @0x42bf60 */
{
    if (!sndVoiceIsActive((unsigned)pEmitter->nSfxHandle) &&
        (pEmitter->nFlags & 1) == 0) {
        sndEmitterFree(pEmitter);
        memFreeDirect(pEmitter);
    }
}

/* sndEmitterUpdateAll @0x42bf40 — per-frame pass over the emitter list
 * (head = newest, pPrev walks toward the older end). Also refreshes each
 * emitter's owner-record position cache from its pos node — the role the
 * original's streaming module fills by calling musicModulePosCheck
 * @0x437b60 per emitter per tick (that module is not reproduced; this is
 * the same per-frame emitter set, so the refresh lives here). The original
 * re-caches only when the node moved outside its cached radius; the rebuild
 * always re-caches, which yields the same positions. Frees finished
 * non-persistent emitters. */
void sndEmitterUpdateAll(void) /* @0x42bf40 */
{
    SndEmitter *pEmitter = g_pSndEmitterHead;

    while (pEmitter != NULL) {
        SndEmitter *pPrev = pEmitter->pPrev;
        if (pEmitter->pMusicEmitter != NULL) {
            MusicEmitter *pMusic = (MusicEmitter *)pEmitter->pMusicEmitter;
            if (pEmitter->pPosNode != NULL) {
                sceneNodeGetPos((SceneNode *)pEmitter->pPosNode, 0,
                                pMusic->anPos, 4);
            }
        }
        sndEmitterUpdateFree(pEmitter);
        pEmitter = pPrev;
    }
}

/* sndPlaySfx3D @0x42bcd0 — fill + link the caller-allocated 0x1c emitter
 * block and queue its sample.
 * nFlags: bit 4 = dedupe scan (skip when a same-id emitter sits within
 * ~100 units), bit 3 = free the duplicate instead of self, bit 4 = loop
 * flag 0x200, bit 5 = flag 0x800 + randomized pitch (rand&0x3fff + 0xd000).
 * musicEmitterAlloc (the 3D music-module registration) is reproduced as the
 * minimal MusicEmitter owner record (see above) so the queued voice gets a
 * live 3D position in the mix chain build. */
SndEmitter *sndPlaySfx3D(SndEmitter *pEmitter, unsigned int nBank,
                         unsigned int nIdx, unsigned int nVol, int nSndId,
                         void *pPosNode, int nEmitParam6, int nX, int nY,
                         int nZ, unsigned int nFlags) /* @0x42bcd0 */
{
    static const float g_flDedupeDist2 = 10000.0f; /* g_fl_10000: squared near-duplicate radius */
    unsigned int nSndFlags = 0;
    int nPitch = 0;
    SndEmitter *pOther;
    SndEmitter *pNext;
    int anPos[3];

    (void)nEmitParam6; (void)nX; (void)nY; (void)nZ;
    if ((nFlags & 0x10) != 0) {
        nSndFlags = 0x200;
    }
    if ((nFlags & 0x20) != 0) {
        nSndFlags |= 0x800;
        nPitch = (rand() & 0x3fff) + 0xd000;   /* rand() & 0x80003fff: bit 31 never set */
    }
    pEmitter->pPosNode = pPosNode;
    pEmitter->pMusicEmitter = NULL;
    pEmitter->nSfxHandle = 0;
    pEmitter->nFlags = nFlags;
    pEmitter->nSndId = nSndId;
    /* link as the new head (pPrev = previous head, pNext = NULL) */
    if (g_pSndEmitterHead != NULL) {
        g_pSndEmitterHead->pNext = pEmitter;
    } else {
        g_pSndEmitterTail = pEmitter;
    }
    pEmitter->pPrev = g_pSndEmitterHead;
    g_pSndEmitterHead = pEmitter;
    pEmitter->pNext = NULL;
    if ((pEmitter->nFlags & 4) != 0) {
        for (pOther = g_pSndEmitterHead; pOther != NULL; pOther = pOther->pPrev) {
            if (pOther->nSndId == pEmitter->nSndId) {
                break;
            }
        }
        if (pOther != NULL) {
            for (pNext = pOther->pNext; pNext != NULL; pNext = pNext->pNext) {
                if (pNext->nSndId == pEmitter->nSndId) {
                    break;
                }
            }
            /* NOTE: the original compares the stored position (sceneNodeGetPos
             * mode 4 on the other emitter's pos node) against (nX,nY,nZ);
             * the pos-node read slot in the decompile (+0x10 vs +0x0c) was
             * ambiguous, so the rebuild tests the caller-supplied coords. */
            anPos[0] = anPos[1] = anPos[2] = 0;
            if (pOther->pPosNode != NULL) {
                sceneNodeGetPos((SceneNode *)pOther->pPosNode, 0, anPos, 4);
            }
            if (pOther != pEmitter &&
                ((float)(nZ - anPos[2]) * (float)(nZ - anPos[2]) +
                 (float)(nY - anPos[1]) * (float)(nY - anPos[1]) +
                 (float)(nX - anPos[0]) * (float)(nX - anPos[0])) < g_flDedupeDist2) {
                /* near-duplicate handling simplified: with bit 3 set the old
                 * emitter is freed, otherwise this one frees itself. */
                if ((nFlags & 8) != 0) {
                    sndEmitterFree(pOther);
                    memFreeDirect(pOther);
                } else {
                    sndEmitterFree(pEmitter);
                    memFreeDirect(pEmitter);
                    return pEmitter;
                }
            }
        }
    }
    /* musicEmitterAlloc(pPosNode, nEmitParam6, nX, nY, 0,
     * g_nMusicModuleHandle) — the original allocates a 0xa8 scene-node
     * emitter parented to the pos node and hands it to sndPlaySfx as the
     * mix owner. The rebuild allocates the minimal MusicEmitter record the
     * mixer chain build reads and primes its position cache with the first
     * musicModulePosCheck @0x437b60 pass (nActive = 1 + node position). */
    pEmitter->pMusicEmitter = malloc(sizeof(MusicEmitter));
    if (pEmitter->pMusicEmitter != NULL) {
        MusicEmitter *pMusic = (MusicEmitter *)pEmitter->pMusicEmitter;
        memset(pMusic, 0, sizeof(*pMusic));
        pMusic->nActive = 1;
        if (pPosNode != NULL) {
            sceneNodeGetPos((SceneNode *)pPosNode, 0, pMusic->anPos, 4);
        }
    }
    pEmitter->nSfxHandle = sndPlaySfx((int)pEmitter->pMusicEmitter, nBank,
                                      nIdx, nVol, nPitch, nSndFlags);
    return pEmitter;
}
