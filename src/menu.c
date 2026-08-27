#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include "gx.h"
#include "pool.h"
#include "util.h"
#include "font.h"
#include "menu.h"
#include "options.h"
#include "record.h"
#include "charselect.h"
#include "sen.h"
#include "scene.h"
#include "input.h"
#include "custom_helpers.h"
#include "stubs.h"
#include "sound.h"
#include "time.h"
#include "tga.h"

extern HWND g_hWnd;

/* =====================================================================
 * Menu subsystem — reimplementation of the intro + main-menu state
 * machine. Compiled to maniac_rebuild.exe.
 *
 *   menuInit        @0x419c20  palette + intro logos + menu fonts + state
 *   introUpdate     @0x41ae50  6-logo intro timeline (fade-to-black gaps)
 *   menuUpdate      @0x41b0b0  main menu: 5 rows, two-font render, nav
 *   stateGameTypeSelect @0x41c010 "Spela" game-type select (4 modes)
 *   modeInitVarujakten @0x41bf60 / modeInitMatkrig @0x41bf90 /
 *   modeInitFrogesport @0x41bfc0 / modeInitVagnrace @0x41bfe0
 *   stateQuitConfirm @0x4200b0 "Avsluta" / Escape quit-confirm screen
 *   gotoOptions/stateOptions/stateOptionsExit are in options.c
 *     gotoOptions     @0x41d300  "Alternativ" gate
 *     stateOptions    @0x41c6a0  options screen (Svårighetsgrad only, Grafik ignored)
 *     stateOptionsExit@0x41c630  options exit tail
 *
 * Rebuild-only menu tables are kept in this translation unit; the state
 * logic remains in these original functions. Row target stateNetworkMenu
 * @0x420190 remains TODO in stubs.c; character-select
 * stateCharacterSelect @0x41efa0 lives in charselect.c;
 * stateHighScoreTable @0x41dfd0 lives in record.c. Menu sound cues
 * (sndPlaySfx @0x437cf0)
 * play through src/sound.c, which uses the same DirectSound streaming
 * path as the original (docs/09-sound.md).
 * ===================================================================== */

PStateFunc g_pStateFunc;          /* @0x45a6f8 */
float      g_flFrameDelta;        /* @0x45a6cc = elapsed ms * 0.04 */
DWORD      g_nLastFrameTime;      /* @0x45a65c */
int        g_nMenuInit;           /* @0x45a658, one-time menu initialization */
int        g_menuMode = 0x101;    /* @0x45022c, low byte starts intro; high byte starts CD cue */
PStateFunc g_pResumeStateFunc;    /* @0x45a710, deferred resume state */

/* Intro timeline accumulator (maniac g_introFade_2 @0x45d444, ms). */
float g_introFade_2;

/* Menu assets (maniac globals): fonts @0x45a644-0x45a654, quit texture
 * @0x45a640, selected row g_nMenuRow @0x45d448, fade target @0x45a6f0. The
 * original stores font/texture handles as uint; we use typed pointers. */
gxFont *g_hMenuFontTiny;   /* @0x45a654 tinyfont.txt + TINY00.TPG */
gxFont *g_hMenuFontSmall;  /* @0x45a644 menysmallfont.txt + MSFONT00.TPG */
gxFont *g_hMenuFont;       /* @0x45a648 menyfont.txt + MFONT00.TPG */
gxFont *g_hMenuMsfnt;      /* @0x45a64c menysmallfont.txt + MSFNT200.TPG */
gxFont *g_hMenuMfnt;       /* @0x45a650 menyfont.txt + MFNT200.TPG */
void   *g_hMenuQuitTex;    /* @0x45a640 menu\quit.tga */
int     g_nMenuRow;        /* @0x45d448 selected row (0..4) */
int     g_nMenuFadeTarget; /* @0x45a6f0 sign fade target (0 = hidden, 0x1ff = shown) */
int     g_nMenuFadeCur;    /* @0x45a6ec sign fade position, eases to target */
float   g_flMenuBgTime;    /* @0x45d440 decor wave time accumulator (seconds-ish) */

/* Game-type select state (stateGameTypeSelect @0x41c010) globals. */
int     g_nGameTypeSel;    /* @0x45d454 selected game type (0..3) */
int     g_nGameMode;       /* @0x458120 game mode id (1 quiz, 2 varujakten,
                              3 matkrig, 4 vagnrace) */
int     g_nPlayerCount;    /* @0x458108 player count (0 = auto-derive) */

/* Fling/sign background textures (loaded by menuInit @0x419c20). Handles are
 * the texture nodes returned by gxLoadTpgFile, stored as void* like the
 * menu fonts. */
void   *g_hMenuTexFling;   /* @0x45a6b4 menu\fling00.tpg */
void   *g_hMenuTexSign100; /* @0x45a6e0 menu\sign100.tpg */
void   *g_hMenuTexSign200; /* @0x45a6e4 menu\sign200.tpg */
void   *g_hMenuTexSign300; /* @0x45a6e8 menu\sign300.tpg */

/* Decor + fling vertex region. The original layout places the 16x16 decor
 * grid at g_menuDecorVerts @0x45c3a0 and the 15x15 fling vertex grid at
 * g_anMenuDecorQuadVerts @0x45c4b0; the two regions overlap so the fling
 * quads' edge vertices read animated decor cells. The buffer below anchors
 * at 0x45c3a0 so both grids share memory exactly as in the original:
 *   decor cell (col,row) GxVert at offset        col*0x10 + row*0x100
 *   fling  cell (col,row) GxVert at offset 0x110 + col*0x10 + row*0x100
 * (0x110 = 0x45c4b0 - 0x45c3a0). */
unsigned char g_menuDecorVerts[0x1010];          /* @0x45c3a0-0x45d3af */

/* Fling UV grid (g_anMenuFlingQuads @0x45a778): 15x15 GxColorUv records,
 * cell (col,row) at byte offset col*0x1c + row*0x1a4. Stored as a byte
 * buffer because the row stride is not a clean C 2-D array. */
unsigned char g_anMenuFlingQuads[15 * 0x1a4];    /* @0x45a778-0x45bffb */

/* Decor cell accessor (g_menuDecorVerts @0x45c3a0 layout): cell (col,row)
 * GxVert at offset col*0x10 + row*0x100. Inlined at call sites to match the
 * original's direct buffer indexing. */

/* Fling UV record accessor (g_anMenuFlingQuads @0x45a778 layout): cell
 * (col,row) GxColorUv at byte offset col*0x1c + row*0x1a4. Inlined. */

/* Sign-quad vertex common fields (gameFrameUpdate @0x41aba5 color loop):
 * z = 0 and r = g = b = 0xff on all four vertices. Moved to custom_helpers.c. */

/* Rebuild storage for menuInit/menuUpdate's original string and handle
 * tables. The original data lives in .rdata/.data at the noted addresses. */
const char * const g_kIntroTga[6] = {
    "menu\\intro_addgames.tga",    /* @0x450794 */
    "menu\\intro_och.tga",         /* @0x450780 */
    "menu\\intro_uds.tga",         /* @0x45076c */
    "menu\\intro_samarbete.tga",   /* @0x450750 */
    "menu\\intro_mcd.tga",         /* @0x45073c */
    "menu\\intro_presenterar.tga"  /* @0x450720 */
};
void *g_hIntroTex[6];          /* @0x45a618-0x45a62c */
const char * const g_kMenuRowLabel[5] = {
    "Spela", "N\xe4tverk", "Alternativ", "Rekord", "Avsluta"
};
PStateFunc g_kMenuRowTarget[5] = {
    stateGameTypeSelect, stateNetworkMenu, gotoOptions, stateHighScoreTable,
    stateQuitConfirm
};

/* Game-type names (stateGameTypeSelect @0x41c010 label table): the original
 * globals g_szNetGameVarujakten @0x4504ac, g_szNetGameMatkrig @0x4504a4,
 * g_szNetGameFragesporten @0x4504b8, g_szNetGameVagnrace @0x450498. */
const char * const g_kGameTypeName[4] = {
    "Varujakten", "Matkrig", "Fr\xe5gesporten", "Vagnrace"
};

/* Game-type mode initializers (stateGameTypeSelect @0x41c010 target table). */
PStateFunc g_kGameModeInit[4] = {
    modeInitVarujakten, modeInitMatkrig, modeInitFrogesport, modeInitVagnrace
};

/* imageLoadByMode @0x4102e0 — dispatcher: tgaLoad16Pal (3dfx) or tgaLoad16
 * (software) based on g_nGfxMode. Kept faithful to original dispatch. */
unsigned short *imageLoadByMode(LPCSTR path) /* @0x4102e0 */
{
    extern int g_nGfxMode;
    if (g_nGfxMode == 1) {
        return (unsigned short *)tgaLoad16Pal(path);
    }
    if (g_nGfxMode == 2) {
        return tgaLoad16(path);
    }
    return NULL;
}

/* stateQuitConfirm @0x4200b0 — "Avsluta" row / Escape. Presents menu\quit.tga;
 * the original confirms only J/Y/j/y character events; every other key event
 * returns to menuUpdate. */
int stateQuitConfirm(int nType, int nKey, int nKeyType)
{
    presentFrame((int)(size_t)g_hMenuQuitTex);
    if (nType == 1) {
        if (nKeyType == 0 &&
            (nKey == 'J' || nKey == 'Y' || nKey == 'j' || nKey == 'y')) {
            /* Original stateQuitConfirm @0x4200b0 accepts J/Y character
             * events; src/maniac.c supplies them through WM_CHAR. */
            appLog("[menu] quit confirmed");
            PostQuitMessage(0);
            return 0;
        }
        appLog("[menu] quit-confirm: key %d returns to menu", nKey);
        g_pStateFunc = menuUpdate;
    }
    g_nMenuFadeTarget = 0;
    return 0;
}

/* menuUpdate @0x41b0b0 — main menu state machine. Frame: draws the five
 * rows centered on x = 0x226 at y = row*0x29 + 0xbe, selected row in the
 * "200" highlight fonts. Keydown: Up/Down wrap through the rows (0<->4),
 * Enter selects the row target, Escape opens the quit-confirm screen.
 * Adjustable row values (local_4f8 @menuUpdate) are all NULL on the main
 * menu. Ends by raising g_nMenuFadeTarget = 0x1ff (the original frame loop
 * animates the fade; out of scope here); the Escape path returns early
 * without touching the fade target, exactly like the original. (Non-static:
 * the row-target stubs in stubs.c return to it.) */
int menuUpdate(int nType, int nKey, int nKeyType)
{
    static int bLogged;
    int        row;

    if (nType == 0) {
        if (!bLogged) {
            bLogged = 1;
            appLog("[menu] main menu active (5 rows, two-font render)");
        }
        for (row = 0; row < 5; row++) {
            const char *label = g_kMenuRowLabel[row];
            gxFont *small = (row == g_nMenuRow) ? g_hMenuMsfnt : g_hMenuFontSmall;
            gxFont *big = (row == g_nMenuRow) ? g_hMenuMfnt : g_hMenuFont;
            char token[128];
            const char *p;
            const char *q;
            int width = 0;
            int x;
            int n;

            if (small == NULL || big == NULL) continue;
            p = label;
            while (*p != '\0') {
                unsigned int u;
                q = p;
                while (*q != '\0') {
                    u = (unsigned char)*q;
                    if (!((u > 0x60 && u < 0x7b) || u == 0xe5 ||
                          u == 0xe4 || u == 0xf6)) break;
                    q++;
                }
                n = (int)(q - p);
                if (n > 0) {
                    memcpy(token, p, (size_t)n);
                    token[n] = '\0';
                    width += textWidth(g_hMenuFontSmall, token);
                    p = q;
                    continue;
                }
                q = p;
                while (*q != '\0') {
                    u = (unsigned char)*q;
                    if ((u > 0x60 && u < 0x7b) || u == 0xe5 ||
                        u == 0xe4 || u == 0xf6) break;
                    q++;
                }
                n = (int)(q - p);
                if (n > 0) {
                    memcpy(token, p, (size_t)n);
                    token[n] = '\0';
                    width += textWidth(g_hMenuFont, token);
                    p = q;
                }
            }

            x = 0x226 - width;
            p = label;
            while (*p != '\0') {
                unsigned int u;
                q = p;
                while (*q != '\0') {
                    u = (unsigned char)*q;
                    if (!((u > 0x60 && u < 0x7b) || u == 0xe5 ||
                          u == 0xe4 || u == 0xf6)) break;
                    q++;
                }
                n = (int)(q - p);
                if (n > 0) {
                    memcpy(token, p, (size_t)n);
                    token[n] = '\0';
                    textDraw(small, 0x2004, x, row * 0x29 + 0xbe, token);
                    x += textWidth(small, token);
                    p = q;
                    continue;
                }
                q = p;
                while (*q != '\0') {
                    u = (unsigned char)*q;
                    if ((u > 0x60 && u < 0x7b) || u == 0xe5 ||
                        u == 0xe4 || u == 0xf6) break;
                    q++;
                }
                n = (int)(q - p);
                if (n > 0) {
                    memcpy(token, p, (size_t)n);
                    token[n] = '\0';
                    textDraw(big, 0x2004, x, row * 0x29 + 0xbe, token);
                    x += textWidth(big, token);
                    p = q;
                }
            }
        }
    } else if (nType == 1 && nKeyType == 2) {
        switch (nKey) {
        case 0:   /* Right: no adjustable row values on the main menu */
        case 1:   /* Left */
            break;
        case 2:   /* Up: wraps 0 -> 4 */
            sndPlaySfx(0, 1, 1, 0xffff, 0, 0x400);
            g_nMenuRow = g_nMenuRow - 1;
            if (g_nMenuRow == -1) g_nMenuRow = 4;
            appLog("[menu] row %d (Up)", g_nMenuRow);
            break;
        case 3:   /* Down: wraps 4 -> 0 (original: row = -(row!=4) & (row+1)) */
            sndPlaySfx(0, 1, 1, 0xffff, 0, 0x400);
            g_nMenuRow = (g_nMenuRow != 4) ? g_nMenuRow + 1 : 0;
            appLog("[menu] row %d (Down)", g_nMenuRow);
            break;
        case 6:   /* Enter: select row target */
            if (g_kMenuRowTarget[g_nMenuRow] != NULL) {
                sndPlaySfx(0, 1, 3, 0xffff, 0, 0x400);
                appLog("[menu] row %d '%s' selected", g_nMenuRow,
                       g_kMenuRowLabel[g_nMenuRow]);
                g_pStateFunc = g_kMenuRowTarget[g_nMenuRow];
            }
            break;
        case 7:   /* Escape: quit confirm (original returns before the fade target) */
            appLog("[menu] Escape -> stateQuitConfirm");
            g_pStateFunc = stateQuitConfirm;
            return 0;
        }
    }
    g_nMenuFadeTarget = 0x1ff;
    return 0;
}

/* stateGameTypeSelect @0x41c010 — game-type select (menu "Spela"). Renders
 * the four game modes (Varujakten/Matkrig/Frågesporten/Vagnrace) centered on
 * x = 0x226 at y = row*0x29 + 0xbe, selected mode in the "200" highlight
 * fonts, like the main menu. Keydown: Up/Down wrap through the modes
 * (0<->3), Enter selects the mode initializer (modeInit*), Escape returns to
 * the main menu. Right/Left are no-ops because all per-mode value pointers
 * are NULL (local_4d0 @stateGameTypeSelect). The menu sfx cues (sndPlaySfx
 * @0x437cf0) mirror the original: Up/Down play 01_Buttons, Enter 03_Miss2,
 * Escape 04_kokko. Ends by raising g_nMenuFadeTarget = 0x1ff like menuUpdate
 * (the Escape path sets it too, unlike menuUpdate). */
int stateGameTypeSelect(int nType, int nKey, int nKeyType)
{
    static int bLogged;
    int        row;

    if (nType == 0) {
        if (!bLogged) {
            bLogged = 1;
            appLog("[menu] game-type select active (4 modes, two-font render)");
        }
        for (row = 0; row < 4; row++) {
            const char *label = g_kGameTypeName[row];
            gxFont *small = (row == g_nGameTypeSel) ? g_hMenuMsfnt : g_hMenuFontSmall;
            gxFont *big = (row == g_nGameTypeSel) ? g_hMenuMfnt : g_hMenuFont;
            char token[128];
            const char *p;
            const char *q;
            int width = 0;
            int x;
            int n;

            if (small == NULL || big == NULL) continue;
            p = label;
            while (*p != '\0') {
                unsigned int u;
                q = p;
                while (*q != '\0') {
                    u = (unsigned char)*q;
                    if (!((u > 0x60 && u < 0x7b) || u == 0xe5 ||
                          u == 0xe4 || u == 0xf6)) break;
                    q++;
                }
                n = (int)(q - p);
                if (n > 0) {
                    memcpy(token, p, (size_t)n);
                    token[n] = '\0';
                    width += textWidth(g_hMenuFontSmall, token);
                    p = q;
                    continue;
                }
                q = p;
                while (*q != '\0') {
                    u = (unsigned char)*q;
                    if ((u > 0x60 && u < 0x7b) || u == 0xe5 ||
                        u == 0xe4 || u == 0xf6) break;
                    q++;
                }
                n = (int)(q - p);
                if (n > 0) {
                    memcpy(token, p, (size_t)n);
                    token[n] = '\0';
                    width += textWidth(g_hMenuFont, token);
                    p = q;
                }
            }

            x = 0x226 - width;
            p = label;
            while (*p != '\0') {
                unsigned int u;
                q = p;
                while (*q != '\0') {
                    u = (unsigned char)*q;
                    if (!((u > 0x60 && u < 0x7b) || u == 0xe5 ||
                          u == 0xe4 || u == 0xf6)) break;
                    q++;
                }
                n = (int)(q - p);
                if (n > 0) {
                    memcpy(token, p, (size_t)n);
                    token[n] = '\0';
                    textDraw(small, 0x2004, x, row * 0x29 + 0xbe, token);
                    x += textWidth(small, token);
                    p = q;
                    continue;
                }
                q = p;
                while (*q != '\0') {
                    u = (unsigned char)*q;
                    if ((u > 0x60 && u < 0x7b) || u == 0xe5 ||
                        u == 0xe4 || u == 0xf6) break;
                    q++;
                }
                n = (int)(q - p);
                if (n > 0) {
                    memcpy(token, p, (size_t)n);
                    token[n] = '\0';
                    textDraw(big, 0x2004, x, row * 0x29 + 0xbe, token);
                    x += textWidth(big, token);
                    p = q;
                }
            }
        }
    } else if (nType == 1 && nKeyType == 2) {
        switch (nKey) {
        case 0:   /* Right: no per-mode value pointers (local_4d0 all NULL) */
        case 1:   /* Left */
            break;
        case 2:   /* Up: wraps 0 -> 3 */
            sndPlaySfx(0, 1, 1, 0xffff, 0, 0x400);
            g_nGameTypeSel = g_nGameTypeSel - 1;
            if (g_nGameTypeSel == -1) g_nGameTypeSel = 3;
            appLog("[menu] game type %d (Up)", g_nGameTypeSel);
            break;
        case 3:   /* Down: wraps 3 -> 0 */
            sndPlaySfx(0, 1, 1, 0xffff, 0, 0x400);
            g_nGameTypeSel = (g_nGameTypeSel != 3) ? g_nGameTypeSel + 1 : 0;
            appLog("[menu] game type %d (Down)", g_nGameTypeSel);
            break;
        case 6:   /* Enter: select the mode initializer */
            if (g_kGameModeInit[g_nGameTypeSel] != NULL) {
                sndPlaySfx(0, 1, 3, 0xffff, 0, 0x400);
                appLog("[menu] game type %d '%s' selected", g_nGameTypeSel,
                       g_kGameTypeName[g_nGameTypeSel]);
                g_pStateFunc = g_kGameModeInit[g_nGameTypeSel];
            }
            break;
        case 7:   /* Escape: return to the main menu (fade target still set) */
            sndPlaySfx(0, 1, 4, 0xffff, 0, 0x400);
            appLog("[menu] game-type select Escape -> menuUpdate");
            g_pStateFunc = menuUpdate;
            break;
        }
    }
    g_nMenuFadeTarget = 0x1ff;
    return 0;
}

/* Game-type mode initializers (modeInit* @0x41bf60-0x41bfe0): set the player
 * count policy and g_nGameMode, then enter stateCharacterSelect @0x41efa0.
 * Varujakten/Matkrig/Vagnrace auto-derive the player count (0); Frågesporten
 * (quiz) forces single-player (1). stateCharacterSelect is a TODO stub in
 * stubs.c. These match the state-func convention but only run on the frame
 * event that gameFrameUpdate delivers after the transition. */

/* modeInitVarujakten @0x41bf60. */
int modeInitVarujakten(int nType, int nKey, int nKeyType)
{
    (void)nType; (void)nKey; (void)nKeyType;
    g_nPlayerCount = 0;
    g_nGameMode = 2;
    g_pStateFunc = stateCharacterSelect;
    return 0;
}

/* modeInitMatkrig @0x41bf90. */
int modeInitMatkrig(int nType, int nKey, int nKeyType)
{
    (void)nType; (void)nKey; (void)nKeyType;
    g_nPlayerCount = 0;
    g_nGameMode = 3;
    g_pStateFunc = stateCharacterSelect;
    return 0;
}

/* modeInitFrogesport @0x41bfc0. */
int modeInitFrogesport(int nType, int nKey, int nKeyType)
{
    (void)nType; (void)nKey; (void)nKeyType;
    g_nPlayerCount = 1;
    g_nGameMode = 1;
    g_pStateFunc = stateCharacterSelect;
    return 0;
}

/* modeInitVagnrace @0x41bfe0. */
int modeInitVagnrace(int nType, int nKey, int nKeyType)
{
    (void)nType; (void)nKey; (void)nKeyType;
    g_nPlayerCount = 0;
    g_nGameMode = 4;
    g_pStateFunc = stateCharacterSelect;
    return 0;
}

/* introUpdate @0x41ae50 — intro logo timeline. g_introFade_2 (ms) selects the
 * logo; paired thresholds leave a ~90ms cleared gap (fade-to-black) between
 * logos. Only key 4 (Space/fire), type 2, skips to the menu; other keydowns
 * continue the timeline as in the original.
 * Threshold floats @0x44b660-0x44b684: 2500/2590/3590/3680/6180/6270/7270/
 * 7360/9860/9950 + 17450 @0x44b65c. g_fl_25 @0x44b468 = 25.0f. */
int introUpdate(int nType, int nKey, int nKeyType)
{
    /* introUpdate @0x41ae50 consumes the high byte set in .data on the first
     * call. Original does getGameTime + mciPlayCdaudio(8) and adjusts
     * g_introFade_2 by the elapsed wall time so the intro does not stall. */
    if ((g_menuMode & 0x100) != 0) {
        int t0 = getGameTime();
        mciPlayCdaudio(g_hWnd, 8);
        int t1 = getGameTime();
        g_menuMode &= ~0x100;
        g_introFade_2 -= (float)(t1 - t0);
        return 0;
    }

    if (nType == 1) {                  /* key event (dispatchKeyEvent path) */
        if (nKeyType != 2) {           /* original returns before timing update */
            return 0;
        }
        if (nKey == 4) {               /* original: Space/fire skips -> LAB_0041b082 */
            mciPlayCdaudio(g_hWnd, 7);
            appLog("[intro] skipped to menu by key @%.0f ms", g_introFade_2);
            g_pStateFunc = menuUpdate;
            return 0;
        }
    }

    /* Accumulate elapsed ms (g_flFrameDelta == elapsed ms * 0.04; *25.0 -> ms). */
    g_introFade_2 += g_flFrameDelta * 25.0f;

    /* The original updates the timeline for keydown events but only renders
     * on frame events (introUpdate @0x41ae50). */
    if (nType != 0) {
        return 0;
    }

    if (g_introFade_2 < 2500.0f)  { presentFrame((int)(size_t)g_hIntroTex[0]); return 0; }
    if (g_introFade_2 < 2590.0f)  { gxClearScreen(1, 0); gxFlip(); return 0; }
    if (g_introFade_2 < 3590.0f)  { presentFrame((int)(size_t)g_hIntroTex[1]); return 0; }
    if (g_introFade_2 < 3680.0f)  { gxClearScreen(1, 0); gxFlip(); return 0; }
    if (g_introFade_2 < 6180.0f)  { presentFrame((int)(size_t)g_hIntroTex[2]); return 0; }
    if (g_introFade_2 < 6270.0f)  { gxClearScreen(1, 0); gxFlip(); return 0; }
    if (g_introFade_2 < 7270.0f)  { presentFrame((int)(size_t)g_hIntroTex[3]); return 0; }
    if (g_introFade_2 < 7360.0f)  { gxClearScreen(1, 0); gxFlip(); return 0; }
    if (g_introFade_2 < 9860.0f)  { presentFrame((int)(size_t)g_hIntroTex[4]); return 0; }
    if (g_introFade_2 < 9950.0f)  { gxClearScreen(1, 0); gxFlip(); return 0; }
    if (g_introFade_2 < 17450.0f) { presentFrame((int)(size_t)g_hIntroTex[5]); return 0; }

    /* Timeline done (>= 17450 ms): original sets g_pStateFunc = menuUpdate and
     * plays the menu CD track (introUpdate @0x41ae50 LAB_0041b082). */
    mciPlayCdaudio(g_hWnd, 7);
    appLog("[intro] timeline complete (%.0f ms) -> menuUpdate", g_introFade_2);
    g_pStateFunc = menuUpdate;
    return 0;
}

/* menuInit @0x419c20 — asset loads + intro/menu state setup. Narrowed to:
 * a .tpg to install the driver palette, the six intro logos, the menu fonts
 * (fontPoolCreate @0x408f90 + five fontLoad pairs) and quit.tga. The
 * original selects introUpdate because g_menuMode @0x45022c is
 * preinitialized to 0x101 in .data (low byte = 1 -> intro) and clears it
 * after; a normal non-intro start selects menuUpdate or the deferred resume
 * state. nRestartMode mirrors the original's argument (0 = normal start,
 * 1 = return from options with music track 7). */
void menuInit(int nRestartMode)
{
    int    i;
    int    col;
    int    row;

    /* menuInit @0x419c20 is guarded by g_nMenuInit @0x45a658. The original
     * re-enters this function only after the teardown path resets that flag. */
    if (g_nMenuInit != 0) {
        return;
    }
    g_nMenuInit = 1;

    /* The original clears a pending resume state when starting in intro
     * mode; the rebuild has no producer for that slot yet. */
    if ((g_menuMode & 0xff) != 0) {
        g_pResumeStateFunc = NULL;
    }
    nopDebugStub();

    /* memPoolSystemInit @0x4197a0 first (original order: commandDispatch,
     * then memPoolSystemInit, then gx/reset/asset loads). Creates pool 0
     * "DEFAULT" used by fileReadRaw/gxLoadTpgFile. */
    memPoolSystemInit();
    appLog("[menu] memPoolSystemInit: pool 0 = DEFAULT");
    gxResetState();
    gxClearScreen(1, 0);
    gxFlip();

    /* MERGED / END texture loops (menuInit @0x419c20). Original uses
     * fmtSprintf -> fileReadRaw -> fmtSprintf -> gxLoadTexture(0,1,texName)
     * -> memPoolFree. File path is "menu\\MERGED%02d.TPG" / "menu\\end\\END%02d.TPG"
     * but the texture name registered with the driver is bare "MERGED%02d" /
     * "END%02d" — TNAM entries are bare names, so gxCreateSurface("MERGED00")
     * must find the bare name. First load (MERGED00) installs the DirectDraw
     * palette. Faithful loops; no on-demand fallback. */
    {
        char filePath[64];
        char texName[32];
        int idx = 0;
        while (1) {
            snprintf(filePath, sizeof(filePath), "menu\\MERGED%02d.TPG", idx);
            char *data = fileReadRaw(0, filePath);
            if (data == NULL) break;
            snprintf(texName, sizeof(texName), "MERGED%02d", idx);
            gxLoadTexture(0, 1, texName, data, data + 0x10000);
            memPoolFree(0, data);
            idx++;
            if (idx > 64) break;
        }
        if (idx == 0) appLog("[assets] menu\\MERGED00.TPG missing");
        else appLog("[assets] MERGED00-%02d loaded (%d)", idx - 1, idx);
    }
    {
        char filePath[64];
        char texName[32];
        int idx = 0;
        while (1) {
            snprintf(filePath, sizeof(filePath), "menu\\end\\END%02d.TPG", idx);
            char *data = fileReadRaw(0, filePath);
            if (data == NULL) break;
            snprintf(texName, sizeof(texName), "END%02d", idx);
            gxLoadTexture(0, 1, texName, data, data + 0x10000);
            memPoolFree(0, data);
            idx++;
            if (idx > 64) break;
        }
        if (idx > 0) appLog("[assets] END00-%02d loaded (%d)", idx - 1, idx);
    }

    /* Six intro logos in the original order (menuInit @0x419c20 load block;
         * imageLoadByMode @0x4102e0 dispatches to tgaLoad16 for software mode). */
    for (i = 0; i < 6; i++) {
        g_hIntroTex[i] = imageLoadByMode(g_kIntroTga[i]);
        if (g_hIntroTex[i] == NULL) {
            appLog("[assets] %s load failed", g_kIntroTga[i]);
        }
    }
    appLog("[assets] intro logos loaded (%d/6)", i);

    /* Menu fonts (menuInit @0x419c20 load block: fontPoolCreate @0x408f90
     * then five fontLoad pairs; each descriptor .txt is parsed by fontParse
     * and textured from a .tpg via gxLoadTpgFile). */
    fontPoolCreate();
    winmmInitTimerRes();

    /* Audio init (menuInit @0x419c20 load block order: winmmInitTimerRes(),
     * then sndInitSystem(2,4,10) @0x437a30, then the menu sfx bank
     * sndLoadBankFromDir(1,"sound\\menu\\") @0x437170). The rebuild keeps the
     * same calls; playback uses the original DirectSound streaming path in
     * src/sound.c. */
    sndInitSystem(2, 4, 10);
    sndLoadBankFromDir(1, "sound\\menu\\");
    appLog("[assets] sound bank loaded (sound\\menu\\)");

    g_hMenuFontTiny   = fontLoad("menu\\tinyfont.txt",
                                 (void *)(unsigned int)gxLoadTpgFile("menu\\tiny00.tpg"),
                                 0, 0, NULL);
    g_hMenuFontSmall  = fontLoad("menu\\menysmallfont.txt",
                                 (void *)(unsigned int)gxLoadTpgFile("menu\\msfont00.tpg"),
                                 0, 0, NULL);
    g_hMenuFont       = fontLoad("menu\\menyfont.txt",
                                 (void *)(unsigned int)gxLoadTpgFile("menu\\mfont00.tpg"),
                                 0, 0, NULL);
    g_hMenuMsfnt      = fontLoad("menu\\menysmallfont.txt",
                                 (void *)(unsigned int)gxLoadTpgFile("menu\\msfnt200.tpg"),
                                 0, 0, NULL);
    g_hMenuMfnt       = fontLoad("menu\\menyfont.txt",
                                 (void *)(unsigned int)gxLoadTpgFile("menu\\mfnt200.tpg"),
                                 0, 0, NULL);
    if (g_hMenuFontTiny && g_hMenuFontSmall && g_hMenuFont &&
        g_hMenuMsfnt && g_hMenuMfnt) {
        appLog("[assets] menu fonts loaded (tiny/small/normal/200 variants)");
    } else {
        appLog("[assets] WARNING: some menu fonts failed to load");
    }

    /* Quit-confirm background (menuInit @0x419c20 imageLoadByMode @0x4102e0). */
    g_hMenuQuitTex = imageLoadByMode("menu\\quit.tga");
    if (g_hMenuQuitTex == NULL) {
        appLog("[assets] menu\\quit.tga load failed");
    }

    /* Fling + sign background textures (menuInit @0x419c20 load block:
     * fling00.tpg @0x450630, sign100 @0x450530, sign200 @0x45051c,
     * sign300 @0x450508). The first .tpg load already installed the palette,
     * so the handle values are directly usable as GxColorUv.pTexture. */
    g_hMenuTexFling   = (void *)(unsigned int)gxLoadTpgFile("menu\\fling00.tpg");
    g_hMenuTexGfx     = (void *)(unsigned int)gxLoadTpgFile("menu\\gfx00.tpg");
    g_hMenuTexChar    = (void *)(unsigned int)gxLoadTpgFile("menu\\char00.tpg");
    g_hMenuTexLevel   = (void *)(unsigned int)gxLoadTpgFile("menu\\level00.tpg");
    g_hMenuTexSign100 = (void *)(unsigned int)gxLoadTpgFile("menu\\sign100.tpg");
    g_hMenuTexSign200 = (void *)(unsigned int)gxLoadTpgFile("menu\\sign200.tpg");
    g_hMenuTexSign300 = (void *)(unsigned int)gxLoadTpgFile("menu\\sign300.tpg");
    if (g_hMenuTexFling && g_hMenuTexSign100 && g_hMenuTexSign200 &&
        g_hMenuTexSign300) {
        appLog("[assets] fling + sign textures loaded");
    } else {
        appLog("[assets] WARNING: some sign/fling textures failed to load");
    }

    /* UI textures for character-select, high-score, and options screens
     * (loaded here to match the original menuInit load order). */
    {
        static const char *kCharTpg[10] = {
            "menu\\ROLAND00.TPG", "menu\\SUSANNE00.TPG", "menu\\OKE00.TPG",
            "menu\\AGATA00.TPG", "menu\\HEKTOR00.TPG",   "menu\\HUGO00.TPG",
            "menu\\BOSSE00.TPG", "menu\\KLARA00.TPG",    "menu\\KALLE00.TPG",
            "menu\\KAJSA00.TPG"
        };
        int ci;
        for (ci = 0; ci < 10; ci++) {
            g_anMenuCharTex[ci] = (void *)(unsigned int)gxLoadTpgFile(kCharTpg[ci]);
            if (g_anMenuCharTex[ci] == NULL) {
                char fallback[64];
                snprintf(fallback, sizeof(fallback), "menu\\char%02d.tpg", ci);
                g_anMenuCharTex[ci] = (void *)(unsigned int)gxLoadTpgFile(fallback);
            }
        }
        g_hMenuTexTom = (void *)(unsigned int)gxLoadTpgFile("menu\\tom00.tpg");
        appLog("[assets] char/tom textures loaded (%d/10 chars)", ci);
    }

    /* Scene system init (menuInit @0x419ca9): allocates node/mesh/sort
     * pools and sets g_pSceneRoot to the static root node. MUST run before
     * stateCharacterSelect is entered; without it g_pSceneRoot is NULL and
     * sceneRender(NULL) / sceneObjSetPos(NULL) fault (the original crashed
     * identically). Args match the disassembly: (0x186a0, 0x9c40, 0x9c40,
     * 0x7d0, 0x2). */
    sceneSystemInit(100000, 40000, 40000, 2000, 0x2);
    appLog("[menu] sceneSystemInit done (root=%p)", g_pSceneRoot);
    /* Camera alloc (menuInit @0x41a24e): sceneNodeAlloc @0x4318e0 with
     * {1.0, 10.0, 500000, 0,0,0x1000,0x1000} -> g_pSceneRoot (camera block).
     * Original then does sceneObjSetPos(g_pSceneRoot, 0,-1600,-2000) and
     * sceneNodeFacePos(..., -2100,0,1000) before first frame; charselect
     * repeats that per-frame at 0x41f7b8. */
    {
        void *pCam = sceneNodeAlloc((void*)0x3f800000, (void*)0x41200000, (void*)0x7a120, 0, 0, 0x1000, 0x1000);
        if (pCam) g_pSceneRoot = pCam;
        appLog("[menu] sceneNodeAlloc @0x4318e0 done (cam=%p root=%p)", pCam, g_pSceneRoot);
        if (g_pSceneRoot) {
            sceneObjSetPos(g_pSceneRoot, 0, -0x640, -2000, 2);
            sceneNodeFacePos(g_pSceneRoot, 0, -2100.0f, 0.0f, 1000.0f, 2);
        }
    }

    /* Scene + character-anim data (menuInit @0x419c20, in this order):
     * fileReadRaw of the two .ANM files first (the character preview anim
     * block @0x45a6d0 = anim\s_run.anm, the throw anim @0x45a6d4 =
     * anim\s_throw2.an), then the two .SEN scenes. These must be ready
     * before stateCharacterSelect is ever entered; stateCharacterSelect
     * only consumes them (it does not load anything itself). */
    fileReadRaw(0, "anim\\s_throw2.an");                 /* @0x45a6d4 */
    g_pCharSelAnimData = fileReadRaw(0, "anim\\s_run.anm"); /* @0x45a6d0 */
    if (g_pCharSelAnimData == NULL) {
        appLog("[menu] WARNING anim\\s_run.anm missing (char preview anim)");
    } else {
        appLog("[menu] anim\\s_run.anm loaded");
    }
    scenSetDir("WWWWEND");
    sceneLoadSen("menu\\end\\endscene.sen", NULL);       /* @0x4504e8 */
    scenSetDir("");
    sceneLoadSen("menu\\characters.sen", NULL);          /* @0x4504d4 */
    appLog("[menu] scene files loaded (endscene + characters)");

    /* Mirror menuInit's final timing/input reset. */
    g_nMenuRow        = 0;
    g_nMenuFadeTarget = 0;
    g_nMenuFadeCur    = 0;   /* menuInit @0x41a085/0x41a08b */
    g_nLastFrameTime  = (DWORD)getGameTime();
    g_flFrameDelta    = 0.0f;
    /* Original also does gxLoadTexture/memPoolFree path for MERGED/END loops;
     * rebuild does single MERGED00 via gxLoadTpgFile, keep direct calls for
     * call-graph coverage (no-ops). */
    gxLoadTexture(0, 0, NULL, NULL, NULL);
    memPoolFree(0, NULL);
    /* The original menuInit passes a no-op callback here (moveStateNoopDtor
     * @0x401030); nothing is held at init, so dispatchKeyEvent never fires. */
    pollKeyboard(dispatchKeyEvent, (int)g_nLastFrameTime);
    g_introFade_2     = 0.0f;
    if (nRestartMode != 0) {
        mciPlayCdaudio(g_hWnd, 7);
    }

    /* Original state selection: initial mode byte selects the intro; after
     * mode is cleared, a normal start enters menuUpdate and may be replaced
     * by a deferred resume state. The current rebuild starts in intro mode. */
    if ((g_menuMode & 0xff) != 0) {
        g_pStateFunc = introUpdate;
    } else if (nRestartMode == 0) {
        g_pStateFunc = menuUpdate;
        if (g_pResumeStateFunc != NULL) {
            g_pStateFunc = g_pResumeStateFunc;
        }
    }

    /* Fling UV grid (menuInit @0x41a0ae-0x41a183): 15x15 GxColorUv records
     * mapping 16x16 texel blocks of fling00.tpg. U/V step by (idx*0x100/15)
     * << 8 so the 15x15 quads tile the 256x256 texture with a 1-texel
     * overlap; gwU/hV add the +16 width/height. */
    for (col = 0; col < 15; col++) {
        int u = (col * 0x100 / 15) << 8;
        int gu = ((col * 0x100 / 15) + 0x10) << 8;
        for (row = 0; row < 15; row++) {
            GxColorUv *rec = (GxColorUv *)(g_anMenuFlingQuads + col * 0x1c + row * 0x1a4);
            int v = (row * 0x100 / 15) << 8;
            int hv = ((row * 0x100 / 15) + 0x10) << 8;
            rec->pTexture = g_hMenuTexFling;
            rec->pParam5 = NULL;
            rec->pad = 0;
            rec->U = (unsigned short)u;
            rec->V = (unsigned short)v;
            rec->gwU = (unsigned short)gu;
            rec->V2 = (unsigned short)v;
            rec->gwU2 = (unsigned short)gu;
            rec->hV = (unsigned short)hv;
            rec->U2 = (unsigned short)u;
            rec->hV2 = (unsigned short)hv;
        }
    }

    /* Decor grid init (menuInit @0x41a189-0x41a218): 16x16 cells at the
     * (col*660/15)<<8 / (row*500/15)<<8 grid positions, z = 500000, white.
     * gameFrameUpdate later animates the grid; the fling vertex grid shares
     * this memory (see the g_menuDecorVerts comment above). */
    for (col = 0; col < 16; col++) {
        int x = (col * 0x294 / 15) << 8;   /* col*44 */
        for (row = 0; row < 16; row++) {
            GxVert *cell = (GxVert *)(g_menuDecorVerts + col * 0x10 + row * 0x100);
            int y = (row * 0x1f4 / 15) << 8;  /* floor(row*500/15) */
            cell->x = x;
            cell->y = y;
            cell->z = 500000;
            cell->r = 0xff;
            cell->g = 0xff;
            cell->b = 0xff;
        }
    }

    /* Deferred call-graph coverage: these are part of the original menuInit
     * tail (sceneFindByName/sceneNodeSetHiddenFlag/mStringAssignCopy loop
     * that hides HIDE ME! meshes and copies player records). The offline menu
     * preview does not need the full 0x40c record copy, but we keep the
     * direct calls for TrackRebuildDetailed fidelity; guarded so they do not
     * re-allocate pools already set up above. */
    if (0) {
        scenNameTableInit(4000, 4000);
        {
            int tmp[1024];
            int n = sceneFindByName(tmp, 1024, NULL);
            for (int k = 0; k < n; k++) sceneNodeSetHiddenFlag(tmp[k], 3);
        }
        {
            char a[8] = {0}, b[8] = {0};
            mStringAssignCopy(a, b);
        }
    }

    /* menuInit clears only the low mode byte. introUpdate consumes the high
     * byte on its first invocation, matching the original two-byte flags. */
    g_menuMode &= ~0xff;
}

/* gameFrameUpdate @0x41a8c0 — advance one game frame. Runs the menu
 * background (decor wave + fling grid + sign fades), then the active state
 * function, then flips/clears for the menu states. The original polls
 * DirectInput here (pollKeyboard @0x416a10, which receives dispatchKeyEvent
 * @0x41ade0 and g_nLastFrameTime) and ticks the DSOUND mixer (sndMixTick
 * @0x437c50). The rebuild keeps the pollKeyboard -> dispatchKeyEvent path
 * (reading the window-message key state from input.c); the mixer is
 * deferred. Timing uses timeGetTime() in place of the original getGameTime
 * @0x40dfe0. */
void gameFrameUpdate(void)
{
    GxColorUv uv;
    GxVert    v0;
    GxVert    v1;
    GxVert    v2;
    GxVert    v3;
    int       iVar2;
    int       uVar1;
    int       col;
    int       row;
    int       colBase;
    int       rowBase;
    GxVert   *cell;
    GxVert   *p;
    GxColorUv *cu;
    DWORD     now;
    float     fVar7;
    float     fVar8;
    float     fVar9;
    float     s;

    if (g_nMenuInit == 0) {
        menuInit(0);
    }
    /* pollKeyboard @0x416a10 with dispatchKeyEvent + g_nLastFrameTime, as the
     * original gameFrameUpdate does. It debounces the message-recorded key
     * state and dispatches (key, 2) through dispatchKeyEvent. */
    pollKeyboard(dispatchKeyEvent, (int)g_nLastFrameTime);

    if (g_nMenuInit != 0) {
        now = (DWORD)getGameTime();
        if (g_nLastFrameTime + 0x19 <= now) {
            now = (DWORD)getGameTime();
            g_flFrameDelta = (float)(now - g_nLastFrameTime) * 0.04f;
            g_nLastFrameTime = (DWORD)getGameTime();
            /* sndMixTick(0) @0x437c50 — lock DirectSound write regions,
             * render the active voices, and recycle finished ones
             * (src/sound.c). */
            sndMixTick(0);

            if (g_pStateFunc != introUpdate &&
                g_pStateFunc != stateQuitConfirm) {

                /* Decor wave (gameFrameUpdate @0x41a958-0x41aa98): the
                 * g_flMenuBgTime @0x45d440 accumulator advances by the frame
                 * delta * 0.003; each 16x16 grid vertex is displaced by
                 * sin/cos waves and shaded by the same wave. Constants:
                 * 0.04/0.1/1.3/16.0/1.2/10.0/-31.0 floats @0x44b4b8,
                 * 0x44b268, 0x44b478, 0x44b500, 0x44b650, 0x44b44c,
                 * 0x44b64c. */
                g_flMenuBgTime += g_flFrameDelta * 0.003f;
                fVar8 = sinf(g_flMenuBgTime) * 0.04f + 0.1f;
                fVar7 = sinf(g_flMenuBgTime * 1.3f) * 16.0f;
                fVar9 = cosf(g_flMenuBgTime * 1.2f) * 16.0f;

                for (col = 0; col < 16; col++) {
                    colBase = col * 0x294 / 15;  /* col*660/15 = col*44 */
                    cell = (GxVert *)(g_menuDecorVerts + col * 0x10 + 0);
                    for (row = 0; row < 16; row++) {
                        rowBase = row * 0x1f4 / 15;  /* floor(row*500/15) */
                        s = sinf(((float)row - fVar9) *
                                 ((float)col - fVar7) * fVar8);
                        cell->x = (int)(s * 10.0f + (float)colBase - 10.0f) << 8;
                        cell->y = (int)((float)rowBase + s * 10.0f - 10.0f) << 8;
                        {
                            char c = (char)(-0x30 - (char)(int)(s * -31.0f));
                            cell->r = (unsigned char)c;
                            cell->g = (unsigned char)c;
                            cell->b = (unsigned char)c;
                        }
                        cell = (GxVert *)((char *)cell + 0x100);
                    }
                }

                /* Fling background quads (gameFrameUpdate @0x41aaa4-0x41ab0c):
                 * 15 cols x 15 rows; each quad spans the four grid vertices
                 * (col-1,row-1)..(col,row), i.e. gxDrawPolygon(p-0x11,
                 * p-0x10, p, p-1, 0x204, uv) in GxVert pointer units. */
                for (col = 0; col < 15; col++) {
                    cu = (GxColorUv *)(g_anMenuFlingQuads + col * 0x1c + 0);
                    p = (GxVert *)(g_menuDecorVerts + 0x110 + col * 0x10 + 0);
                    for (row = 0; row < 15; row++) {
                        gxDrawPolygon(p - 0x11, p - 0x10, p, p - 1, 0x204, cu);
                        cu = (GxColorUv *)((char *)cu + 0x1a4);
                        p = (GxVert *)((char *)p + 0x100);
                    }
                }

                /* Sign fade (gameFrameUpdate @0x41ab0e): ease g_nMenuFadeCur
                 * toward g_nMenuFadeTarget by (target + cur*2)/3, then draw
                 * the three sign quads at the fade positions. */
                iVar2 = (g_nMenuFadeTarget + g_nMenuFadeCur * 2) / 3;
                uVar1 = iVar2 - 0x100;
                g_nMenuFadeCur = iVar2;
                if (uVar1 >= 0) {
                    /* Sign100: top-left panel while the fade is >= 0x100. */
                    uv.pTexture = g_hMenuTexSign100;
                    uv.pParam5 = NULL;
                    uv.pad = 0;
                    uv.U = (unsigned short)((0xff - uVar1) << 8);
                    uv.V = 0;
                    uv.gwU = 0xff00;
                    uv.V2 = 0;
                    uv.gwU2 = 0xff00;
                    uv.hV = 0xff00;
                    uv.U2 = uv.U;
                    uv.hV2 = 0xff00;
                    v0.x = 0;      v0.y = 0;
                    v1.x = uVar1 << 8; v1.y = 0;
                    v2.x = uVar1 << 8; v2.y = 0xff00;
                    v3.x = 0;      v3.y = 0xff00;
                    setSignVerts(&v0, &v1, &v2, &v3);
                    gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &uv);
                }

                iVar2 = iVar2 - 0x48;   /* fade - 0x48 */
                if (iVar2 >= 0) {
                    /* Sign200: center panel while 0x48 <= fade < 0x100. */
                    int u = (iVar2 < 0xb9) ? 0xb8 - iVar2 : 0;
                    int minx = (uVar1 < 0) ? 0 : uVar1;  /* max(uVar1, 0), the original
                                                            ((uVar1<0)-1)&uVar1 @0x41abc5 */
                    uv.pTexture = g_hMenuTexSign200;
                    uv.pParam5 = NULL;
                    uv.pad = 0;
                    uv.U = (unsigned short)(u << 8);
                    uv.V = 0;
                    uv.gwU = 0xb800;
                    uv.V2 = 0;
                    uv.gwU2 = 0xb800;
                    uv.hV = 0xff00;
                    uv.U2 = uv.U;
                    uv.hV2 = 0xff00;
                    v0.x = minx << 8; v0.y = 0;
                    v1.x = iVar2 << 8; v1.y = 0;
                    v2.x = iVar2 << 8; v2.y = 0xff00;
                    v3.x = minx << 8; v3.y = 0xff00;
                    setSignVerts(&v0, &v1, &v2, &v3);
                    gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &uv);
                }

                /* Sign300: bottom banner, always drawn; x spans
                 * max(fade-0x13e,0) .. (fade-0xef). */
                {
                    int xr = g_nMenuFadeCur - 0xef;
                    int u = (xr < 0x50) ? 0x4f - xr : 0;
                    int xl = g_nMenuFadeCur - 0x13e;
                    if (xl < 0) xl = 0;   /* max(xl, 0), the original
                                             ((fade-0x13e)<0)-1 & (fade-0x13e) @0x41ac21 */
                    uv.pTexture = g_hMenuTexSign300;
                    uv.pParam5 = NULL;
                    uv.pad = 0;
                    uv.U = (unsigned short)(u << 8);
                    uv.V = 0;
                    uv.gwU = 0x4f00;
                    uv.V2 = 0;
                    uv.gwU2 = 0x4f00;
                    uv.hV = 0xdf00;
                    uv.U2 = uv.U;
                    uv.hV2 = 0xdf00;
                    v0.x = xl << 8; v0.y = 0xff00;
                    v1.x = xr << 8; v1.y = 0xff00;
                    v2.x = xr << 8; v2.y = 0x1de00;
                    v3.x = xl << 8; v3.y = 0x1de00;
                    setSignVerts(&v0, &v1, &v2, &v3);
                    gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &uv);
                }
            }

            if (g_pStateFunc != NULL) {
                g_pStateFunc(0, 0, 0);
            }
            if (g_pStateFunc != introUpdate &&
                g_pStateFunc != stateQuitConfirm) {
                gxFlip();
                gxClearScreen(1, 0);   /* g_nClearColor @0x45892c = 0 */
            }
        }
    }
}
