#ifndef OPTIONS_H
#define OPTIONS_H

#include <windows.h>

#include "menu.h"

/* Options screen globals (stateOptions @0x41c6a0, gotoOptions @0x41d300).
 * Moved from menu.c to options.c per file-size split. Addresses are
 * absolute based at 0x400000. */
extern int   g_nOptionsRow;     /* @0x45d45c selected options row (0 only, Grafik ignored) */
extern float g_optionsVol;      /* @0x45d458 anim accumulator */
extern int   g_nModeSel;        /* @0x4580fc difficulty 0=Lätt,1=Medium,2=Svårt */
extern int   g_nGfxMode;        /* @0x4580c4 driver selection (1=Glide,2=Software) */
extern int   g_nRendererMode;   /* @0x45a390 active renderer mode */
extern void *g_hMenuTexGfx;     /* @0x45a6bc menu\gfx00.tpg */

/* gotoOptions @0x41d300 — "Alternativ" gate: copies g_nGfxMode into
 * g_nRendererMode then enters stateOptions. */
int gotoOptions(int nType, int nKey, int nKeyType);
/* stateOptions @0x41c6a0 — options screen (Svårighetsgrad only, Grafik
 * ignored): single difficulty row driven by g_nModeSel. */
int stateOptions(int nType, int nKey, int nKeyType);
/* stateOptionsExit @0x41c630 — options exit tail: returns to menuUpdate
 * (original would re-init renderer if g_nGfxMode != g_nRendererMode). */
int stateOptionsExit(int nType, int nKey, int nKeyType);

#endif /* OPTIONS_H */
