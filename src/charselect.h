#ifndef CHARSELECT_H
#define CHARSELECT_H

#include "compat_types.h"
#include "gx.h"
#include "font.h"
#include "scene.h"   /* SceneNode (g_pCharModelNode etc are scene nodes) + AnmFile via anim.h */

/* Character-select state (stateCharacterSelect @0x41efa0, reached from
 * modeInitVarujakten @0x41bf60, modeInitMatkrig @0x41bf90,
 * modeInitFrogesport @0x41bfc0, modeInitVagnrace @0x41bfe0 via Spela
 * 4-mode screen stateGameTypeSelect @0x41c010). Enter validates the
 * selected character and advances to stateLevelSelect @0x41b900;
 * ESC returns to stateGameTypeSelect.
 *
 * Implementation lives in src/charselect.c (new file per milestone).
 * Rendering reuses the menu two-font pipeline (g_hMenuMsfnt/msfnt @
 * 0x45a64c/0x45a650, g_hMenuFontSmall/Font @0x45a644/0x45a648) plus the
 * stat bars and portrait quads (g_hMenuTexGfx @0x45a6bc, g_anMenuCharTex
 * @0x45a660, g_hMenuTexTom @0x45a688). 3D preview is stubbed (scene
 * system deferred) but the call hierarchy and 2D UI match the original.
 */

/* Globals (maniac addresses). Defined in charselect.c. */
extern int   g_nCharSelIdx;      /* @0x45d480 selected char 0..9 */
extern int   g_nCharSelSaved;    /* @0x45d450 saved char idx */
extern int   g_nCharSelIdxPrev;  /* @0x45d484 previous idx for model swap */
extern int   g_nCharSelRow;      /* @0x45d490 row cursor (single row 0) */
extern float g_flCharModelRot;   /* @0x45d488 preview rotation */
extern float g_flCharModelZoom;  /* @0x45d48c preview zoom */
extern float g_flCharAnimTime;   /* @0x45d410 anim time (frameDelta*0.3) */
extern int   g_nCharAnimFrame;   /* @0x45d498 anim frame counter */
extern float g_flCharAnimAccum;  /* @0x45d49c anim accumulator */
extern SceneNode *g_pCharModelNode;   /* @0x45a6c4 current preview node */
extern SceneNode *g_pCharModelNodePrev; /* @0x45a6c8 previous node */
extern AnmFile *g_pCharAnim;        /* @0x45a6d8 current anim */
extern AnmFile *g_pCharAnimPrev;    /* @0x45a6dc previous anim */
extern int   g_nCharModelSwapFlag; /* @0x45d494 swap flag */
/* g_pSceneRoot is a macro over g_camFollowBlock (see scene.h). */
extern void *g_pCharSelAnimData;  /* @0x45a6d0 anim-data block (fileReadRaw of anim\s_run.anm) */
extern void *g_pThrowAnimData;    /* @0x45a6d4 anim-data block (fileReadRaw of anim\s_throw2.an) */
extern int g_anMenuCharTex[10];/* @0x45a660 per-char portrait textures */
extern int g_hMenuTexTom;      /* @0x45a688 QUESTION fallback tex */

/* Character tables (rebuilt from .rdata). Addresses noted where original
 * tables live: g_apCharNames @0x45013c, g_apCharSceneNames @0x450164,
 * g_roundInitb4 @0x4501b4, g_roundInitb8 @0x4501b8, g_roundInitbc @0x4501bc. */
extern const char *g_apCharNames[10];      /* @0x45013c */
extern const char *g_apCharSceneNames[10]; /* @0x450164 */
extern const int   g_kCharStatSpeed[10];   /* @0x4501b4 Snabbhet */
extern const int   g_kCharStatStrength[10];/* @0x4501b8 Styrka */
extern const int   g_kCharStatAgility[10]; /* @0x4501bc Smidighet */

/* State functions (maniac addresses). */
int stateCharacterSelect(int nType, int nKey, int nKeyType); /* @0x41efa0 */
int stateCharSelectOk(int nType, int nKey, int nKeyType);    /* @0x41ef40 */

/* Mixed-case text helper (textDrawMixedCase @0x41ffc0): lower-case a-z
 * plus å/ä/ö drawn with g_hMenuMsfnt, other chars with g_hMenuMfnt,
 * advancing x by textWidth per token run. */
void textDrawMixedCase(int x, int y, const char *text); /* @0x41ffc0 */

#endif /* CHARSELECT_H */
