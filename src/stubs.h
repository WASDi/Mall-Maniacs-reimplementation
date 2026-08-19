#ifndef STUBS_H
#define STUBS_H

#include <windows.h>

/* =====================================================================
 * Declared interfaces for unfinished behavior (Rebuild.md §19).
 * Each is a TODO stub: logs and performs a documented no-op or safe
 * placeholder transition so the vertical slice proceeds; callers keep their
 * contract unchanged when the stub body is later replaced. Original
 * target/address noted where one exists.
 * ===================================================================== */

/* gameInit @0x409d90 — full game initialization (level config, scene system,
 * sound, networking). Ghidra signature: void(void). Contract:
 * replacement is a logged no-op; it does not initialize state or claim
 * success. TODO: not implemented for the vertical slice. */
void gameInit(void);

/* pollKeyboard @0x416a10 — poll DirectInput keyboard state, debounce key
 * presses, and dispatch (key, 2) events through the active state callback.
 * Ghidra signature: void(void). Contract: replacement is a logged
 * no-op; window messages provide the vertical-slice input path instead, and
 * DirectInput plus its quit-request side effects remain deferred. TODO. */
void pollKeyboard(void);

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