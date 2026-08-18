#ifndef MENU_H
#define MENU_H

#include <windows.h>

/* Game state-function pointer (maniac g_pStateFunc @0x45a6f8). Called with
 * (type, key, keyType): type 0 = frame update, type 1 = key event (key,
 * keyType 2 = keydown). Same convention as dispatchKeyEvent (see 0x41ade0).
 * Owned by the menu subsystem (src/menu.c) for now; game states reuse it. */
typedef int (*PStateFunc)(int nType, int nKey, int nKeyType);
extern PStateFunc g_pStateFunc;

/* Frame timing — mirrors maniac g_flFrameDelta @0x45a6cc (float, =
 * elapsed ms * 0.04) and g_nLastFrameTime @0x45a65c. Written by the WinMain
 * frame loop (see WinMain in maniac.c); consumed by introUpdate. */
extern float g_flFrameDelta;
extern DWORD g_nLastFrameTime;

/* Menu subsystem entry points (maniac addresses):
 *   menuInit      @0x419c20 — palette + intro logos + menu fonts + state setup.
 *     Signature `void __cdecl menuInit(int nRestartMode)` matches Ghidra; the
 *     original callers pass 0 (gameFrameUpdate/dispatchKeyEvent first frame)
 *     or 1 (stateOptionsExit "return to options", which also starts music
 *     track 7). The rebuild always starts with the intro, so the arg is
 *     accepted and ignored.
 *   menuFramePost @gameFrameUpdate @0x41a8c0 — after each frame update the
 *     menu states flip + clear (introUpdate / stateQuitConfirm present their
 *     own frames, so they are excluded). */
void menuInit(int nRestartMode);
void menuFramePost(void);

/* menuUpdate @0x41b0b0 — main-menu state function (state-func convention
 * int (nType, nKey, nKeyType)). Non-static so the row-target stubs in
 * stubs.c can return to the main menu. */
int  menuUpdate(int nType, int nKey, int nKeyType);

#endif /* MENU_H */
