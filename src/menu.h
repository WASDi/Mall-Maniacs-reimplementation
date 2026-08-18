#ifndef MENU_H
#define MENU_H

#include <windows.h>

#include "font.h"

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
 *   introUpdate   @0x41ae50 / stateQuitConfirm @0x4200b0 — the two states
 *     that present their own frames. */
void menuInit(int nRestartMode);
int  introUpdate(int nType, int nKey, int nKeyType);   /* @0x41ae50 */
int  stateQuitConfirm(int nType, int nKey, int nKeyType); /* @0x4200b0 */

/* menuUpdate @0x41b0b0 — main-menu state function (state-func convention
 * int (nType, nKey, nKeyType)). Non-static so the row-target stubs in
 * stubs.c can return to the main menu. */
int  menuUpdate(int nType, int nKey, int nKeyType);

/* tgaLoad16 @0x415df0. */
unsigned short *tgaLoad16(LPCSTR path);

/* Menu-state globals (maniac addresses). */
extern float  g_introFade_2;    /* @0x45d444 intro timeline ms */
extern gxFont *g_hMenuFontTiny;   /* @0x45a654 tinyfont.txt + TINY00.TPG */
extern gxFont *g_hMenuFontSmall;  /* @0x45a644 menysmallfont.txt + MSFONT00.TPG */
extern gxFont *g_hMenuFont;       /* @0x45a648 menyfont.txt + MFONT00.TPG */
extern gxFont *g_hMenuMsfnt;      /* @0x45a64c menysmallfont.txt + MSFNT200.TPG */
extern gxFont *g_hMenuMfnt;       /* @0x45a650 menyfont.txt + MFNT200.TPG */
extern void   *g_hMenuQuitTex;    /* @0x45a640 menu\quit.tga */
extern int     g_nMenuRow;        /* @0x45d448 selected row (0..4) */
extern int     g_nMenuFadeTarget; /* @0x45a6f0 (fade anim out of scope) */

#endif /* MENU_H */
