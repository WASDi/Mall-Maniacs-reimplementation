#include <windows.h>
#include <stdio.h>

#include "input.h"
#include "menu.h"
#include "custom_helpers.h"

/* =====================================================================
 * Input subsystem — pollKeyboard @0x416a10 (menu), pollKeyboardGame
 * @0x416820 (in-game) and dispatchKeyEvent @0x41ade0.
 * The DirectInput 256-byte state read in the original polls is replaced
 * by the message-recorded g_abInputKeyHeld state (written by WindowProc
 * in maniac.c); the dispatch logic below is the original.
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

/* In-game poll state (pollKeyboardGame @0x416820). */
char g_bPollGameEscLatch;    /* @0x459d48 Escape edge latch (1 = dispatched, key still down) */
int  g_nSpaceHeldFrames;     /* @0x459d3c Space held-frame counter */
int  g_nSpaceReleaseFrames;  /* @0x459d40 Space release-frame counter */
int  g_bSpaceStateFlag;      /* @0x459d44 Space-down flag (arms the release dispatch) */

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

/* pollKeyboardGame @0x416820 — the in-game poll gameWorldUpdate @0x40b3d0
 * feeds gameKeyHandler through (PUSH 0x40db80 / CALL 0x416820; takes only
 * the callback, RET 0x4 — no time argument, no debounce). Called once per
 * world update; the DirectInput GetDeviceState read is replaced by
 * g_abInputKeyHeld. Semantics, verified against the disassembly:
 *  - Right (0) else Left (1): (key, 2) dispatched EVERY frame while held,
 *    at most one of the pair (@0x4168b0..0x4168da).
 *  - Up (2) else Down (3): same every-frame dispatch (@0x4168dd..0x4168fb).
 *  - Enter (6): (6, 2) every frame held (@0x4168fe..0x41690a).
 *  - Escape (7): edge-latched via the byte @0x459d48 — dispatched once per
 *    press, latch cleared on release (@0x41690d..0x41692c).
 *  - Space (4): tap/hold state machine over nSpaceHeldFrames @0x459d3c,
 *    nSpaceReleaseFrames @0x459d40 and bSpaceStateFlag @0x459d44
 *    (@0x416935..0x4169e4): a (4, 2) event fires on the 6th frame held
 *    (held counter == 5), or ~6 frames after release of a shorter tap
 *    (flag armed while held <= 5); a quick re-press while the release
 *    counter is still 1..5 emits (5, 2) instead (double-tap channel).
 * The acquire-failed PostMessage(WM_QUIT) tail of the original belongs to
 * DirectInput and has no rebuild counterpart. */
void pollKeyboardGame(DispatchKeyEventFn pfnDispatchKeyEvent)
{
    if ((g_abInputKeyHeld[0] & 0x80) != 0) {                  /* [ESP+0xd1] @0x4168b0 */
        pfnDispatchKeyEvent(0, 2);                            /* @0x4168c6 */
    }
    else if ((g_abInputKeyHeld[1] & 0x80) != 0) {             /* [ESP+0xd7] @0x4168cb */
        pfnDispatchKeyEvent(1, 2);                            /* @0x4168d4 */
    }

    if ((g_abInputKeyHeld[2] & 0x80) != 0) {                  /* [ESP+0xd4] @0x4168dd */
        pfnDispatchKeyEvent(2, 2);                            /* @0x4168e6 */
    }
    else if ((g_abInputKeyHeld[3] & 0x80) != 0) {             /* [ESP+0xdc] @0x4168ec */
        pfnDispatchKeyEvent(3, 2);                            /* @0x4168f5 */
    }

    if ((g_abInputKeyHeld[6] & 0x80) != 0) {                  /* [ESP+0x28] @0x4168fe */
        pfnDispatchKeyEvent(6, 2);                            /* @0x416904 */
    }

    if ((g_abInputKeyHeld[7] & 0x80) != 0) {                  /* [ESP+0xd] @0x41690d */
        if (g_bPollGameEscLatch == 0) {                       /* [0x459d48] @0x416913 */
            pfnDispatchKeyEvent(7, 2);                        /* @0x41691c */
            g_bPollGameEscLatch = 1;                          /* @0x416925 */
        }
    }
    else {
        g_bPollGameEscLatch = 0;                              /* @0x41692e */
    }

    if ((g_abInputKeyHeld[4] & 0x80) != 0) {                  /* [ESP+0x45] @0x416935 */
        int nHeld = g_nSpaceHeldFrames;                       /* [0x459d3c] @0x41693b */
        if (nHeld <= 5) {                                     /* CMP EAX,5 @0x416940 */
            g_bSpaceStateFlag = 1;                            /* [0x459d44] @0x416945 */
            if (nHeld == 5) {                                 /* JNZ 0x416982 @0x41694f */
                int nRelease = g_nSpaceReleaseFrames;         /* [0x459d40] @0x416951 */
                if (nRelease == 0 || nRelease > 5) {          /* @0x416959/@0x41695e */
                    pfnDispatchKeyEvent(4, 2);                /* @0x416960 */
                }
                /* release 1..5 falls to L_98d -> L_9a9 with the flag kept */
                g_bSpaceStateFlag = 0;                        /* @0x41696f */
                g_nSpaceReleaseFrames = 0;                    /* @0x416975 */
                g_nSpaceHeldFrames = nHeld + 1;               /* @0x41697b */
                return;                                       /* JMP 0x4169e4 */
            }
        }
        /* L_982: dispatch (5, 2) only when release 1..5 and held == 0. */
        {
            int nRelease = g_nSpaceReleaseFrames;             /* @0x416982 */
            if (nRelease <= 5 && nRelease != 0 && nHeld == 0) { /* @0x41698b..0x416993 */
                pfnDispatchKeyEvent(5, 2);                    /* @0x416995 */
                nHeld = 5;                                    /* @0x41699e */
                g_bSpaceStateFlag = 0;                        /* @0x4169a3 */
            }
        }
        g_nSpaceHeldFrames = nHeld + 1;                       /* L_9a9 @0x4169a9 */
        g_nSpaceReleaseFrames = 0;                            /* @0x4169aa */
    }
    else {
        if (g_bSpaceStateFlag != 0) {                         /* [0x459d44] @0x4169bf */
            g_nSpaceReleaseFrames = g_nSpaceReleaseFrames + 1; /* @0x4169c5 */
            if (g_nSpaceReleaseFrames > 5) {                  /* CMP EAX,5 @0x4169c8 */
                pfnDispatchKeyEvent(4, 2);                    /* @0x4169cf */
                g_bSpaceStateFlag = 0;                        /* @0x4169d8 */
            }
        }
        g_nSpaceHeldFrames = 0;                               /* @0x4169de */
    }
}
