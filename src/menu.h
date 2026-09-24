#ifndef MENU_H
#define MENU_H

#include "compat_types.h"

#include "gx.h"
#include "font.h"

/* Game state-function pointer (maniac g_pStateFunc @0x45a6f8). Called with
 * (type, key, keyType): type 0 = frame update, type 1 = key event (key,
 * keyType 2 = keydown). Same convention as dispatchKeyEvent (see 0x41ade0).
 * Owned by the menu subsystem (src/menu.c) for now; game states reuse it. */
typedef int (*PStateFunc)(int nType, int nKey, int nKeyType);
extern PStateFunc g_pStateFunc;
extern int g_nMenuInit;           /* @0x45a658 one-time menu init gate (defined in menu.c) */
extern PStateFunc g_pResumeStateFunc; /* @0x45a710 deferred resume state (defined in menu.c) */

/* Frame timing — mirrors maniac g_flFrameDelta @0x45a6cc (float, =
 * elapsed ms * 0.04) and g_nLastFrameTime @0x45a65c. Written by the WinMain
 * frame loop (see WinMain in maniac.c); consumed by introUpdate. */
extern float g_flFrameDelta;
extern DWORD g_nLastFrameTime;

/* Menu subsystem entry points (maniac addresses):
 *   menuInit      @0x419c20 — palette + intro logos + menu fonts + state setup.
 *     Signature `void menuInit(int nRestartMode)` uses the default C
 *     convention; the original callers pass 0 (gameFrameUpdate/dispatchKeyEvent first frame)
 *     or 1 (stateOptionsExit "return to options", which also starts music
 *     track 7). The rebuild always starts with the intro, so the arg is
 *     accepted and ignored.
 *   introUpdate   @0x41ae50 / stateQuitConfirm @0x4200b0 — the two states
 *     that present their own frames. */
void menuInit(int nRestartMode);
int  introUpdate(int nType, int nKey, int nKeyType);   /* @0x41ae50 */
int  stateQuitConfirm(int nType, int nKey, int nKeyType); /* @0x4200b0 */
int  stateGameTypeSelect(int nType, int nKey, int nKeyType); /* @0x41c010 */

/* stateLevelSelect @0x41b900 — map selection after character selection. */
int  stateLevelSelect(int nType, int nKey, int nKeyType);

/* Game-type mode initializers (stateGameTypeSelect @0x41c010 targets). Each
 * sets g_nPlayerCount / g_nGameMode, then g_pStateFunc = stateCharacterSelect
 * @0x41efa0 (TODO stub in stubs.c). */
int  modeInitVarujakten(int nType, int nKey, int nKeyType); /* @0x41bf60 */
int  modeInitMatkrig(int nType, int nKey, int nKeyType);    /* @0x41bf90 */
int  modeInitFrogesport(int nType, int nKey, int nKeyType); /* @0x41bfc0 */
int  modeInitVagnrace(int nType, int nKey, int nKeyType);   /* @0x41bfe0 */

/* gameFrameUpdate @0x41a8c0 — advance one game frame: menu background
 * (decor wave + fling grid + sign fades), then the active state function,
 * then flip/clear for the menu states. Called by WinMain's idle loop. */
void gameFrameUpdate(void);

/* menuUpdate @0x41b0b0 — main-menu state function (state-func convention
 * int (nType, nKey, nKeyType)). Non-static so the row-target stubs in
 * stubs.c can return to the main menu. */
int  menuUpdate(int nType, int nKey, int nKeyType);

/* imageLoadByMode @0x4102e0 — dispatcher: tgaLoad16Pal or tgaLoad16. */
unsigned short *imageLoadByMode(LPCSTR path);

/* Menu-state globals (maniac addresses). */
extern int    g_menuMode;       /* @0x45022c low byte starts intro; high byte starts CD cue */
extern void  *g_hIntroTex[6];   /* @0x45a618-0x45a62c intro logo textures */
extern float  g_introFade_2;    /* @0x45d444 intro timeline ms */
extern gxFont *g_hMenuFontTiny;   /* @0x45a654 tinyfont.txt + TINY00.TPG */
extern gxFont *g_hMenuFontSmall;  /* @0x45a644 menysmallfont.txt + MSFONT00.TPG */
extern gxFont *g_hMenuFont;       /* @0x45a648 menyfont.txt + MFONT00.TPG */
extern gxFont *g_hMenuMsfnt;      /* @0x45a64c menysmallfont.txt + MSFNT200.TPG */
extern gxFont *g_hMenuMfnt;       /* @0x45a650 menyfont.txt + MFNT200.TPG */
extern void   *g_hMenuQuitTex;    /* @0x45a640 menu\quit.tga */
extern int     g_nMenuRow;        /* @0x45d448 selected row (0..4) */
extern int     g_nMenuFadeTarget; /* @0x45a6f0 sign fade target (0 hidden, 0x1ff shown) */
extern int     g_nMenuFadeCur;    /* @0x45a6ec sign fade position */
extern float   g_flMenuBgTime;    /* @0x45d440 decor wave time accumulator */

void setSignVerts(GxVert *v0, GxVert *v1, GxVert *v2, GxVert *v3);

/* Game-type select state (stateGameTypeSelect @0x41c010) globals. */
extern int     g_nGameTypeSel;    /* @0x45d454 selected game type (0..3) */
extern int     g_nGameMode;       /* @0x458120 game mode id (1 quiz, 2 varujakten,
                                     3 matkrig, 4 vagnrace) */
extern int     g_nPlayerCount;    /* @0x458108 player count (0 = auto-derive) */

/* Map-selection assets loaded by menuInit @0x419c20. */
extern int g_hMenuTexSmal;       /* @0x45a68c menu\smal00.tpg */
extern int g_hMenuTexWood;       /* @0x45a690 menu\wood00.tpg */
extern int g_hMenuTexOrie;       /* @0x45a694 menu\orie00.tpg */
extern int g_hMenuTexAqua;       /* @0x45a698 menu\aqua00.tpg */
extern int g_hMenuTexRock;       /* @0x45a69c menu\rock00.tpg */
extern int g_hMenuTexSec100;     /* @0x45a6a0 menu\sec100.tpg */
extern int g_hMenuTexSec200;     /* @0x45a6a4 menu\sec200.tpg */
extern int g_hMenuTexSec300;     /* @0x45a6a8 menu\sec300.tpg */
extern int g_hMenuTexSec400;     /* @0x45a6ac menu\sec400.tpg */
extern int g_hMenuTexSec500;     /* @0x45a6b0 menu\sec500.tpg */

/* Fling/sign background textures (menuInit @0x419c20). */
extern int g_hMenuTexFling;   /* @0x45a6b4 menu\fling00.tpg */
extern int g_hMenuTexSign100; /* @0x45a6e0 menu\sign100.tpg */
extern int g_hMenuTexSign200; /* @0x45a6e4 menu\sign200.tpg */
extern int g_hMenuTexSign300; /* @0x45a6e8 menu\sign300.tpg */

#endif /* MENU_H */
