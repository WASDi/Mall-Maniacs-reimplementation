#ifndef HUD_H
#define HUD_H

/* In-game HUD overlay subsystem (renderGameHud @0x412810, hudLoadGraphics
 * @0x412700). All overlay drawing goes through gxDrawQuadColor/gxDrawPolygon
 * with the four shared HUD textures loaded by hudLoadGraphics, plus the three
 * HUD fonts defined in font.c. */

/* HUD textures @0x458980..0x45898c (loaded by hudLoadGraphics @0x412700). */
extern void *g_hHudListTpg;    /* @0x458980 hud/list00.tpg — shopping list panel */
extern void *g_hHudCharTpg;    /* @0x458984 hud/char00.tpg — character face panel */
extern void *g_hHudGfx2Tpg;    /* @0x458988 hud/gfx2200.tpg — panels, arrows, bars */
extern void *g_hHudFrogeTpg;   /* @0x45898c hud/froge00.tpg — frog message panel */

/* HUD draw state @0x458990..0x4589a4. */
extern int g_nScoreDisplay;    /* @0x458990 mode 3 final score (ticks*obj_update) */
extern int g_nInvBarCur;       /* @0x458994 current shopping-list panel height */
extern int g_nInvBarTarget;    /* @0x458998 target panel height (per taken item) */
extern unsigned char g_bItemSfx;    /* @0x45899c mode 3 target-item pickup ping */
extern unsigned char g_bCheckoutSfx; /* @0x45899d checkout arrival ping */
extern int g_nPhaseStartTime;  /* @0x4589a0 getGameTime() when phase 5 hit */
extern int g_nLastSfx;         /* @0x4589a4 last countdown sfx id (dedup) */

/* Scroll/console text rows (in-game console feeds these; console itself is
 * out of scope, the HUD only draws them). 29 rows of 100 bytes each. */
extern char g_acScrollLines[29][100];   /* @0x4589a8 */
extern int  g_nScrollLineCount;         /* @0x4594fc */
extern char g_acConsoleLines[29][100];  /* @0x4551e0 */
extern int  g_nConsoleLineCount;        /* @0x455d34 */

/* Scratch string buffer @0x4550d8 (shared .bss scratch; rendered by the
 * phase-5/wait overlays — empty unless something wrote it). */
extern char g_acScratchText[256];

/* Item name table @0x45839c: 0x2c-byte name slots indexed by the shopping
 * list item id (record.anListIds). Filled by questLoad (TODO boundary). */
extern char g_acItemNames[64][0x2c];

void hudLoadGraphics(void);   /* @0x412700 */
void renderGameHud(void);     /* @0x412810 */

#endif /* HUD_H */
