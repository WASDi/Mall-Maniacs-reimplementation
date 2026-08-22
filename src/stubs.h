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

/* Main-menu row targets (menu.c dispatch table g_kMenuRowTarget, entered
 * from menuUpdate @0x41b0b0 on Enter). Contract: state-func convention
 * (nType 0 = frame update, nType 1 + nKeyType 2 = keydown); each sets
 * g_pStateFunc to the next state. TODO stubs: log once + return to the
 * menu. Interfaces stay fixed when the real bodies replace them.
 * gotoOptions @0x41d300 is now implemented in options.c (Alternativ ->
 * Svårighetsgrad, Grafik ignored). */
int stateNetworkMenu(int nType, int nKey, int nKeyType);     /* @0x420190 */
/* stateHighScoreTable @0x41dfd0 — now implemented in record.c */

/* stateCharacterSelect @0x41efa0 — character-select screen, entered by the
 * modeInit* game-type initializers (menu.c). Contract: state-func
 * convention. TODO stub: log once + return to the game-type select.
 * Interfaces stay fixed when the real body replaces it. */
int stateCharacterSelect(int nType, int nKey, int nKeyType); /* @0x41efa0 */

#endif /* STUBS_H */
