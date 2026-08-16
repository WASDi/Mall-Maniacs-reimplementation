#ifndef STUBS_H
#define STUBS_H

#include <windows.h>

/* =====================================================================
 * Declared interfaces for unfinished behavior (Rebuild.md §19).
 * Each is a TODO stub: logs and returns a handled failure so the vertical
 * slice proceeds; callers keep their contract unchanged when the stub body
 * is later replaced. Original target/address noted where one exists.
 * ===================================================================== */

/* gameInit @0x409d90 — full game initialization (level config, scene system,
 * sound, networking). Contract: called once after window + driver init; on
 * success the game enters its state machine. Returns 1 on success, 0 on
 * failure. TODO: not implemented for the vertical slice. */
int  gameInit(void);

/* gameFrameUpdate @0x41abc0 — advance one game frame (update + render).
 * Contract: called when g_nFrameDue != 0; may request quit. TODO. */
int  gameFrameUpdate(void);

/* inputPollKeyboard @0x416800 — poll DirectInput keyboard state into the
 * shared key buffer. Contract: fills global 256-byte key state. TODO: the
 * slice uses window messages (WM_KEYDOWN) instead; DirectInput deferred. */
int  inputPollKeyboard(void);

/* shutdownRenderer — original gxUnloadDriver @0x432880 path (see gx.c).
 * Contract: release driver, restore display. Implemented for the slice;
 * kept here as the named shutdown entry point used by the full WinMain. */
void shutdownRenderer(void);

#endif /* STUBS_H */