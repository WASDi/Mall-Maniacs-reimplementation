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

/* gameFrameUpdate @0x41a8c0 — advance one game frame (update + render).
 * Contract: called when g_nFrameDue != 0; may request quit. TODO. */
int  gameFrameUpdate(void);

/* pollKeyboard @0x416a10 — poll DirectInput keyboard state into the shared
 * key buffer (__stdcall(callback, time); key events via callback). Contract:
 * fills global 256-byte key state. TODO: the slice uses window messages
 * (WM_KEYDOWN) instead; DirectInput deferred. */
int  pollKeyboard(void);

/* Main-menu row targets (menu.c dispatch table g_kMenuRowTarget, entered
 * from menuUpdate @0x41b0b0 on Enter). Contract: state-func convention
 * (nType 0 = frame update, nType 1 + nKeyType 2 = keydown); each sets
 * g_pStateFunc to the next state. TODO stubs: log once + return to the
 * menu. Interfaces stay fixed when the real bodies replace them. */
int stateGameTypeSelect(int nType, int nKey, int nKeyType);  /* @0x41c010 */
int stateNetworkMenu(int nType, int nKey, int nKeyType);     /* @0x420190 */
int gotoOptions(int nType, int nKey, int nKeyType);          /* @0x41d300 */
int stateHighScoreTable(int nType, int nKey, int nKeyType);  /* @0x41dfd0 */

#endif /* STUBS_H */