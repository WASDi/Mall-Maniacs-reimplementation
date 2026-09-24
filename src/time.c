#include "compat_types.h"
#include "time.h"
#include "platform_sdl2.h"

/* =====================================================================
 * Time / MCI / debug stubs — early initialization helpers.
 * Mirrors original addresses for TrackRebuildDetailed coverage.
 * Clock source is SDL2 (platformTicks); gating logic is untouched.
 * ===================================================================== */

/* g_nClock, g_nClockCache, g_nFrameDue — rebuild side storage for
 * getGameTime @0x40dfe0 logic. Original values live in .data; we keep
 * our own copies that behave identically. */
int g_nClock;       /* maniac g_nClock */
int g_nClockCache;  /* maniac g_nClockCache */
int g_nFrameDue = 1;    /* maniac g_nFrameDue referenced by WindowProc/WinMain/getGameTime — start active */

/* getGameTime @0x40dfe0 — SDL_GetTicks() with clock cache and
 * g_nFrameDue gating, exactly as original. Used by gameFrameUpdate,
 * menuInit, introUpdate, focus handling. */
int getGameTime(void) /* @0x40dfe0 */
{
    DWORD cur = (DWORD)platformTicks();
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

/* winmmInitTimerRes @0x40dfd0 — original timeBeginPeriod(1); SDL2 needs no
 * timer-resolution hint, so this is a no-op (address comment kept). */
void winmmInitTimerRes(void) /* @0x40dfd0 */
{
}

/* winmmRestoreTimerRes @0x40e030 — original timeEndPeriod(1); no-op. */
void winmmRestoreTimerRes(void) /* @0x40e030 */
{
}

/* nopDebugStub @0x401590 — no-op used throughout original as debug hook.
 * Called by menuInit, gameInit, etc. */
void nopDebugStub(void) /* @0x401590 */
{
    return;
}

/* mciPlayCdaudio @0x416cc0 — CD-audio is out of scope; documented silent
 * stub returning 0 (MMSYSERR_NOERROR). HWND parameter residue dropped. */
int mciPlayCdaudio(int nTrack) /* @0x416cc0 */
{
    (void)nTrack;
    return 0;
}

/* mciStopCdaudio @0x416c80 — stop CD audio. Paired to play. */
int mciStopCdaudio(void) /* @0x416c80 */
{
    return 0;
}
