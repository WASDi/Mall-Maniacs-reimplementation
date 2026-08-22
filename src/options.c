#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "gx.h"
#include "font.h"
#include "menu.h"
#include "options.h"
#include "input.h"
#include "custom_helpers.h"
#include "sound.h"
#include "util.h"

/* =====================================================================
 * Alternativ / Options subsystem — reimplementation of the options
 * state machine. Split from menu.c per file-size.
 *
 *   gotoOptions      @0x41d300  "Alternativ" gate
 *   stateOptions     @0x41c6a0  options screen (Svårighetsgrad only, Grafik ignored)
 *   stateOptionsExit @0x41c630  options exit tail
 *
 * Original has two rows (Difficulty g_nModeSel @0x4580fc 0..2 and
 * Graphics g_nRendererMode @0x45a390 1..2) with g_optionsVol @0x45d458
 * and g_hMenuTexGfx @0x45a6bc. This rebuild keeps only the difficulty
 * row per task scope ("Ignore the 'Grafik' option").
 * ===================================================================== */

int   g_nOptionsRow = 0;      /* @0x45d45c */
float g_optionsVol = 0.0f;    /* @0x45d458 */
int   g_nModeSel = 1;         /* @0x4580fc difficulty 0 Lätt,1 Medium,2 Svårt (default Medium) */
int   g_nGfxMode = 2;         /* @0x4580c4 1 Glide,2 Software */
int   g_nRendererMode = 2;    /* @0x45a390 */
void *g_hMenuTexGfx;          /* @0x45a6bc menu\gfx00.tpg */

static const char * const kDifficultyName[3] = {
    "L\xe4tt",   /* @0x450260 */
    "Medium",   /* @0x450258 */
    "Sv\xe5rt"   /* @0x450250 */
};

/* gotoOptions @0x41d300 — "Alternativ" gate. Copies g_nGfxMode @0x4580c4
 * into g_nRendererMode @0x45a390, resets g_nOptionsRow @0x45d45c, and
 * enters stateOptions @0x41c6a0. */
int gotoOptions(int nType, int nKey, int nKeyType)
{
    (void)nType; (void)nKey; (void)nKeyType;
    g_nRendererMode = g_nGfxMode;
    g_nOptionsRow = 0;
    g_pStateFunc = stateOptions;
    appLog("[menu] Alternativ -> stateOptions (Sv\xe5righetsgrad=%s)", kDifficultyName[g_nModeSel]);
    return 0;
}

/* stateOptionsExit @0x41c630 — options exit tail. Original checks
 * g_nGfxMode @0x4580c4 != g_nRendererMode @0x45a390 and would re-init
 * (configSetValueDispatch, save, unload, shutdown, gameInit, menuInit);
 * simplified build ignores Grafik and just returns to menuUpdate @0x41b0b0. */
int stateOptionsExit(int nType, int nKey, int nKeyType)
{
    (void)nType; (void)nKey; (void)nKeyType;
    g_pStateFunc = menuUpdate;
    return 0;
}

/* stateOptions @0x41c6a0 — options screen (simplified to Svårighetsgrad
 * only). Original has two rows (Difficulty g_nModeSel @0x4580fc 0..2 and
 * Graphics g_nRendererMode @0x45a390 1..2) with g_optionsVol @0x45d458
 * and g_hMenuTexGfx @0x45a6bc. Left/Right adjust difficulty
 * (Lätt/Medium/Svårt @0x450230), Up/Down no-op, Enter no-op, Escape
 * returns to menuUpdate. Frame replicates g_optionsVol accumulator and
 * label/value presentation with original sfx cues. */
int stateOptions(int nType, int nKey, int nKeyType)
{
    g_nPlayerCount = 8; /* original stateOptions sets this on every entry */

    if (nType == 0) {
        /* Frame: ensure the gfx banner texture is available (original loads
         * it in menuInit @0x419c20: g_hMenuTexGfx @0x45a6bc = gfx00.tpg;
         * the rebuild loads lazily). */
        if (g_hMenuTexGfx == NULL) {
            g_hMenuTexGfx = (void *)(uintptr_t)gxLoadTpgFile("menu\\gfx00.tpg");
        }

        /* Label "Svårighetsgrad" — original at x=0xac,y=0x32 highlighted
         * with msfnt/mfnt; we center it and use the highlight fonts when
         * available so the selected row matches the original two-font look. */
        {
            const char *label = "Sv\xe5righetsgrad"; /* @0x450894 */
            gxFont *hiSmall = g_hMenuMsfnt;
            gxFont *hiBig = g_hMenuMfnt;
            if (hiSmall != NULL && hiBig != NULL) {
                int w = 0;
                /* width via highlight fonts (small for lower-case/åäö, big otherwise) — reuse menuUpdate's split for fidelity */
                const char *p = label;
                while (*p != '\0') {
                    unsigned int u = (unsigned char)*p;
                    const char *q = p;
                    char token[128];
                    int n;
                    if ((u > 0x60 && u < 0x7b) || u == 0xe5 || u == 0xe4 || u == 0xf6) {
                        while (*q != '\0') {
                            u = (unsigned char)*q;
                            if (!((u > 0x60 && u < 0x7b) || u == 0xe5 || u == 0xe4 || u == 0xf6)) break;
                            q++;
                        }
                        n = (int)(q - p);
                        if (n > 0) {
                            memcpy(token, p, (size_t)n); token[n] = '\0';
                            w += textWidth(hiSmall, token);
                        }
                    } else {
                        while (*q != '\0') {
                            u = (unsigned char)*q;
                            if ((u > 0x60 && u < 0x7b) || u == 0xe5 || u == 0xe4 || u == 0xf6) break;
                            q++;
                        }
                        n = (int)(q - p);
                        if (n > 0) {
                            memcpy(token, p, (size_t)n); token[n] = '\0';
                            w += textWidth(hiBig, token);
                        }
                    }
                    p = q; if (*p=='\0') break;
                }
                int x = 0x140 - w/2;
                int y = 0x32;
                p = label;
                while (*p != '\0') {
                    unsigned int u = (unsigned char)*p;
                    const char *q = p;
                    char token[128];
                    int n;
                    if ((u > 0x60 && u < 0x7b) || u == 0xe5 || u == 0xe4 || u == 0xf6) {
                        while (*q != '\0') {
                            u = (unsigned char)*q;
                            if (!((u > 0x60 && u < 0x7b) || u == 0xe5 || u == 0xe4 || u == 0xf6)) break;
                            q++;
                        }
                        n = (int)(q - p);
                        if (n > 0) {
                            memcpy(token, p, (size_t)n); token[n] = '\0';
                            textDraw(hiSmall, 0x2004, x, y, token);
                            x += textWidth(hiSmall, token);
                        }
                    } else {
                        while (*q != '\0') {
                            u = (unsigned char)*q;
                            if ((u > 0x60 && u < 0x7b) || u == 0xe5 || u == 0xe4 || u == 0xf6) break;
                            q++;
                        }
                        n = (int)(q - p);
                        if (n > 0) {
                            memcpy(token, p, (size_t)n); token[n] = '\0';
                            textDraw(hiBig, 0x2004, x, y, token);
                            x += textWidth(hiBig, token);
                        }
                    }
                    p = q;
                }
            } else if (g_hMenuFont != NULL) {
                int w = textWidth(g_hMenuFont, (char*)label);
                textDraw(g_hMenuFont, 0x2004, 0x140 - w/2, 0x32, (char*)label);
            }
        }
        /* Arrows for Svårighetsgrad — original at 0x41c6a0 uses
         * g_hMenuTexGfx @0x45a6bc with fsin(g_optionsVol) wobble and
         * highlight UVs based on g_nOptionsRow @0x45d45c (0 selected).
         * Left at 0x8e/0xb9, right at 0x1c6/0x1f1, y 0x5900-0x8400,
         * UV 0x2b00/0x5800 etc. Logically equivalent to original. */
        {
            /* Original @0x41caf8/0x41cc07: FLD g_optionsVol @0x45d458; FSIN;
             * FILD sel; FMULP; FMUL scale; CALL __ftol @0x43dd10; SUB.
             * Left scale = [0x44b6a0]=-5.0, right scale=[0x44b698]=+5.0,
             * sel=(g_nOptionsRow==row). So left = base+5*sin, right=base-5*sin
             * when selected — arrows breathe out/in together, not slide. */
            float s = sinf(g_optionsVol);
            int sel = (g_nOptionsRow == 0);
            int offL = sel ? (int)(s * -5.0f) : 0;
            int offR = sel ? (int)(s *  5.0f) : 0;
            GxVert v0, v1, v2, v3;
            GxColorUv uv;
            uv.pTexture = g_hMenuTexGfx;
            uv.pParam5 = NULL;
            uv.pad = 0;
            uv.V = 0x3300;
            uv.V2 = 0x3300;
            uv.hV = 0x5e00;
            uv.hV2 = 0x5e00;
            /* left arrow — original @0x41caf8 SUB -5*sin */
            uv.U = sel ? 0 : 0x5800;
            uv.U2 = uv.U;
            uv.gwU = sel ? 0x2b00 : 0x8300;
            uv.gwU2 = uv.gwU;
            v0.y = 0x5900; v1.y = 0x5900; v2.y = 0x8400; v3.y = 0x8400;
            v0.x = (0x8e - offL) * 0x100; v1.x = (0xb9 - offL) * 0x100;
            v2.x = v1.x; v3.x = v0.x;
            setSignVerts(&v0, &v1, &v2, &v3);
            gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &uv);
            /* right arrow — original @0x41cc07 SUB +5*sin (opposite) */
            uv.U = sel ? 0x2c00 : 0x8400;
            uv.U2 = uv.U;
            uv.gwU = sel ? 0x5700 : 0xaf00;
            uv.gwU2 = uv.gwU;
            v0.y = 0x5900; v1.y = 0x5900; v2.y = 0x8400; v3.y = 0x8400;
            v0.x = (0x1c6 - offR) * 0x100; v1.x = (0x1f1 - offR) * 0x100;
            v2.x = v1.x; v3.x = v0.x;
            setSignVerts(&v0, &v1, &v2, &v3);
            gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &uv);
        }
        /* Original advances g_optionsVol after arrows, before banners. */
        g_optionsVol += g_flFrameDelta * 0.3f;
        /* Top banner — gfx00.tpg full-width header at y 0x5500-0x8800,
         * UV 0/0xff00 as in original @0x41c6a0. */
        {
            GxVert v0, v1, v2, v3;
            GxColorUv uv;
            uv.pTexture = g_hMenuTexGfx;
            uv.pParam5 = NULL;
            uv.pad = 0;
            uv.U = 0; uv.V = 0; uv.V2 = 0; uv.hV = 0x3200; uv.hV2 = 0x3200;
            uv.U2 = 0; uv.gwU = 0xff00; uv.gwU2 = 0xff00;
            v0.x = 0xc000; v0.y = 0x5500; v1.x = 0x1bf00; v1.y = 0x5500;
            v2.x = 0x1bf00; v2.y = 0x8800; v3.x = 0xc000; v3.y = 0x8800;
            setSignVerts(&v0, &v1, &v2, &v3);
            gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &uv);
        }
        /* Difficulty value text and underline — original at y 0x67 (tiny
         * font) and quad at y 0x6900-0x8300 with UV based on
         * g_nModeSel @0x4580fc *0x1a. */
        {
            const char *val = kDifficultyName[g_nModeSel < 0 ? 0 : (g_nModeSel > 2 ? 2 : g_nModeSel)];
            gxFont *tiny = g_hMenuFontTiny;
            if (tiny != NULL) {
                int w = textWidth(tiny, (char*)val);
                textDraw(tiny, 0x2004, 0x140 - w/2, 0x67, (char*)val);
            } else if (g_hMenuFontSmall != NULL) {
                int w = textWidth(g_hMenuFontSmall, (char*)val);
                textDraw(g_hMenuFontSmall, 0x2004, 0x140 - w/2, 0x67, (char*)val);
            }
            {
                GxVert v0, v1, v2, v3;
                GxColorUv uv;
                int off2 = g_nModeSel * 0x1a;
                uv.pTexture = g_hMenuTexGfx;
                uv.pParam5 = NULL;
                uv.pad = 0;
                uv.V = 32000; uv.V2 = 32000; uv.hV = 0x9600; uv.hV2 = 0x9600;
                uv.U = (unsigned short)((off2 + 0x65) * 0x100);
                uv.U2 = uv.U;
                uv.gwU = (unsigned short)((off2 + 0x7e) * 0x100);
                uv.gwU2 = uv.gwU;
                v0.y = 0x6900; v1.y = 0x6900; v2.y = 0x8300; v3.y = 0x8300;
                v0.x = 0x19c00; v1.x = 0x1b600; v2.x = 0x1b600; v3.x = 0x19c00;
                setSignVerts(&v0, &v1, &v2, &v3);
                gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &uv);
            }
        }
        g_nMenuFadeTarget = 0;
        return 0;
    } else if (nType == 1) {
        if (nKeyType != 2) {
            return 0;
        }
        switch (nKey) {
        case 0: /* Right: increase difficulty */
            if (g_nModeSel < 2) {
                sndPlaySfx(0, 1, 2, 0xffff, 0, 0x400);
                g_nModeSel++;
                appLog("[options] Sv\xe5righetsgrad -> %s (%d)", kDifficultyName[g_nModeSel], g_nModeSel);
            }
            return 0;
        case 1: /* Left: decrease */
            if (g_nModeSel > 0) {
                sndPlaySfx(0, 1, 2, 0xffff, 0, 0x400);
                g_nModeSel--;
                appLog("[options] Sv\xe5righetsgrad -> %s (%d)", kDifficultyName[g_nModeSel], g_nModeSel);
            }
            return 0;
        case 2: /* Up */
        case 3: /* Down — single row, stay but give feedback like original row switch */
            sndPlaySfx(0, 1, 1, 0xffff, 0, 0x400);
            return 0;
        case 6: /* Enter — no-op (original local_210[g_nOptionsRow]==0) */
            return 0;
        case 7: /* Escape -> exit to menu */
            sndPlaySfx(0, 1, 4, 0xffff, 0, 0x400);
            appLog("[options] Escape -> menuUpdate");
            g_pStateFunc = stateOptionsExit;
            /* stateOptionsExit will immediately set menuUpdate on next frame dispatch;
             * call it directly so the transition is visible without an extra frame. */
            g_pStateFunc = menuUpdate;
            return 0;
        default:
            return 0;
        }
    }
    if (nType != 0) {
        return 0;
    }
    return 0;
}
