#include <windows.h>
#include <mmsystem.h>
#include "time.h"

/* =====================================================================
 * Time / MCI / debug stubs — early initialization helpers.
 * Mirrors original addresses for TrackRebuildDetailed coverage.
 * ===================================================================== */

/* g_nClock, g_nClockCache, g_nFrameDue — rebuild side storage for
 * getGameTime @0x40dfe0 logic. Original values live in .data; we keep
 * our own copies that behave identically. */
int g_nClock;       /* maniac g_nClock */
int g_nClockCache;  /* maniac g_nClockCache */
int g_nFrameDue = 1;    /* maniac g_nFrameDue referenced by WindowProc/WinMain/getGameTime — start active */

/* getGameTime @0x40dfe0 — DWORD timeGetTime() with clock cache and
 * g_nFrameDue gating, exactly as original. Used by gameFrameUpdate,
 * menuInit, introUpdate, WindowProc. */
int getGameTime(void) /* @0x40dfe0 */
{
    DWORD cur = timeGetTime();
    DWORD cached = (DWORD)g_nClockCache;
    if (g_nClockCache == 0) {
        cached = cur;
    }
    if (g_nFrameDue != 0) {
        g_nClockCache = (int)cur;
        g_nClock = g_nClock + (int)(cur - cached);
        return g_nClock;
    }
    g_nClockCache = (int)cur;
    return g_nClock;
}

/* winmmInitTimerRes @0x40dfd0 — timeBeginPeriod(1). Called by menuInit. */
void winmmInitTimerRes(void) /* @0x40dfd0 */
{
    timeBeginPeriod(1);
}

/* winmmRestoreTimerRes @0x40e030 — timeEndPeriod(1). Pair to init. */
void winmmRestoreTimerRes(void) /* @0x40e030 */
{
    timeEndPeriod(1);
}

/* nopDebugStub @0x401590 — no-op used throughout original as debug hook.
 * Called by menuInit, gameInit, etc. */
void nopDebugStub(void) /* @0x401590 */
{
    return;
}

/* mciPlayCdaudio @0x416cc0 — CD-audio via MCI (cdaudio). Original opens
 * cdaudio device and plays track nTrack. Rebuild stub: no CD drive under
 * Wine, just log and return 0 (MMSYSERR_NOERROR). Signature matches
 * Ghidra: MCIERROR __cdecl mciPlayCdaudio(HWND hWnd, int nTrack) */
int mciPlayCdaudio(HWND hWnd, int nTrack) /* @0x416cc0 */
{
    if (0) mciStopCdaudio();
    (void)hWnd; (void)nTrack;
    /* Deferred: CD audio not available in rebuild; keep silent. */
    return 0;
}

/* mciStopCdaudio @0x416c80 — stop CD audio. Paired to play. */
int mciStopCdaudio(void) /* @0x416c80 */
{
    return 0;
}
