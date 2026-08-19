#ifndef INPUT_H
#define INPUT_H

#include <windows.h>

/* Input subsystem — pollKeyboard @0x416a10 and dispatchKeyEvent @0x41ade0.
 * The original reads DirectInput here; the rebuild records the mapped key
 * down/up state from window messages (WindowProc in maniac.c) and the
 * debounced (key, 2) events reach the active state through this same path. */

/* dispatchKeyEvent @0x41ade0 callback type, as typed in Ghidra
 * (DispatchKeyEventFn). */
typedef int (*DispatchKeyEventFn)(int nKey, int nKeyType);

/* Rebuild-only held-key state, indexed by the original game key id (0 Right,
 * 1 Left, 2 Up, 3 Down, 4 Space, 6 Enter, 7 Escape). The 0x80 high bit is set
 * while the key is held, mirroring the DirectInput state byte the original
 * polled. No original address — message-driven replacement for the DI state
 * buffer read by pollKeyboard. */
extern char g_abInputKeyHeld[8];

/* pollKeyboard @0x416a10 — Ghidra signature void __stdcall
 * pollKeyboard(DispatchKeyEventFn pfnDispatchKeyEvent, int nFrameTime). */
void pollKeyboard(DispatchKeyEventFn pfnDispatchKeyEvent, int nFrameTime);

/* dispatchKeyEvent @0x41ade0 — Ghidra signature int __cdecl
 * dispatchKeyEvent(int nKey, int nKeyType). */
int dispatchKeyEvent(int nKey, int nKeyType);

#endif /* INPUT_H */
