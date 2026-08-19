#include <windows.h>
#include <stdio.h>

#include "input.h"
#include "menu.h"
#include "custom_helpers.h"

/* =====================================================================
 * Input subsystem — pollKeyboard @0x416a10 and dispatchKeyEvent @0x41ade0.
 * The DirectInput 256-byte state read in the original pollKeyboard is
 * replaced by the message-recorded g_abInputKeyHeld state (written by
 * WindowProc in maniac.c); the per-key debounce logic below is the original.
 * ===================================================================== */

/* Per-key debounce ticks (pollKeyboard @0x416a10). A tick stores the frame
 * time of the last dispatched (key, 2) event and is cleared while the key is
 * released, exactly like the original globals. */
int g_nBtnDebounceTick0;   /* @0x459d54 Right */
int g_nBtnDebounceTick1;   /* @0x459d58 Left  */
int g_nBtnDebounceTick2;   /* @0x459d4c Up    */
int g_nBtnDebounceTick3;   /* @0x459d50 Down  */
int g_nBtnDebounceTick4;   /* @0x459d64 Space */
int g_nBtnDebounceTick6;   /* @0x459d5c Enter */
int g_nBtnDebounceTick7;   /* @0x459d60 Escape */

/* Rebuild-only held-key state (see input.h). */
char g_abInputKeyHeld[8];

/* pollKeyboard @0x416a10 — poll the keyboard state and dispatch debounced
 * keydown events. Mirror of the original: each mapped key is checked every
 * frame; while released its debounce tick is cleared, while held a (key, 2)
 * event is dispatched through pfnDispatchKeyEvent when 200 ms have passed
 * since the last one (Enter dispatches once per press, no repeat). The
 * GetDeviceState read is replaced by g_abInputKeyHeld. */
void pollKeyboard(DispatchKeyEventFn pfnDispatchKeyEvent, int nFrameTime)
{
    if (nFrameTime == 0) {
        nFrameTime = 1;
    }

    if ((g_abInputKeyHeld[0] & 0x80) == 0) {
        g_nBtnDebounceTick0 = 0;
    }
    else if ((g_nBtnDebounceTick0 == 0) || (g_nBtnDebounceTick0 + 200 < nFrameTime)) {
        pfnDispatchKeyEvent(0, 2);
        g_nBtnDebounceTick0 = nFrameTime;
    }

    if ((g_abInputKeyHeld[1] & 0x80) == 0) {
        g_nBtnDebounceTick1 = 0;
    }
    else if ((g_nBtnDebounceTick1 == 0) || (g_nBtnDebounceTick1 + 200 < nFrameTime)) {
        pfnDispatchKeyEvent(1, 2);
        g_nBtnDebounceTick1 = nFrameTime;
    }

    if ((g_abInputKeyHeld[2] & 0x80) == 0) {
        g_nBtnDebounceTick2 = 0;
    }
    else if ((g_nBtnDebounceTick2 == 0) || (g_nBtnDebounceTick2 + 200 < nFrameTime)) {
        pfnDispatchKeyEvent(2, 2);
        g_nBtnDebounceTick2 = nFrameTime;
    }

    if ((g_abInputKeyHeld[3] & 0x80) == 0) {
        g_nBtnDebounceTick3 = 0;
    }
    else if ((g_nBtnDebounceTick3 == 0) || (g_nBtnDebounceTick3 + 200 < nFrameTime)) {
        pfnDispatchKeyEvent(3, 2);
        g_nBtnDebounceTick3 = nFrameTime;
    }

    if ((g_abInputKeyHeld[6] & 0x80) == 0) {
        g_nBtnDebounceTick6 = 0;
    }
    else if (g_nBtnDebounceTick6 == 0) {      /* Enter: once per press, no repeat */
        pfnDispatchKeyEvent(6, 2);
        g_nBtnDebounceTick6 = nFrameTime;
    }

    if ((g_abInputKeyHeld[7] & 0x80) == 0) {
        g_nBtnDebounceTick7 = 0;
    }
    else if ((g_nBtnDebounceTick7 == 0) || (g_nBtnDebounceTick7 + 200 < nFrameTime)) {
        pfnDispatchKeyEvent(7, 2);
        g_nBtnDebounceTick7 = nFrameTime;
    }

    if ((g_abInputKeyHeld[4] & 0x80) == 0) {
        g_nBtnDebounceTick4 = 0;
    }
    else if ((g_nBtnDebounceTick4 == 0) || (g_nBtnDebounceTick4 + 200 < nFrameTime)) {
        pfnDispatchKeyEvent(4, 2);
        g_nBtnDebounceTick4 = nFrameTime;
    }
}

/* dispatchKeyEvent @0x41ade0 — forward a key event to the active state
 * function as (1, nKey, nKeyType). The original's g_pStateFunc == NULL branch
 * ran the intro timeline and the Escape exit; in this rebuild a state is
 * always active (menuInit sets it) and the intro lives in introUpdate, so
 * only the forwarding branch is needed. */
int dispatchKeyEvent(int nKey, int nKeyType)
{
    if (g_pStateFunc != NULL) {
        return g_pStateFunc(1, nKey, nKeyType);
    }
    return 0;
}
