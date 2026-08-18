#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "gx.h"
#include "pool.h"
#include "util.h"
#include "font.h"
#include "menu.h"
#include "custom_helpers.h"
#include "stubs.h"

/* =====================================================================
 * Menu subsystem — reimplementation of the intro + main-menu state
 * machine. Compiled to maniac_rebuild.exe per Rebuild.md.
 *
 *   menuInit        @0x419c20  palette + intro logos + menu fonts + state
 *   introUpdate     @0x41ae50  6-logo intro timeline (fade-to-black gaps)
 *   menuUpdate      @0x41b0b0  main menu: 5 rows, two-font render, nav
 *   stateQuitConfirm @0x4200b0 "Avsluta" / Escape quit-confirm screen
 *
 * Rebuild-only menu tables and deferred row-target stubs are kept outside
 * this translation unit; the state logic remains in these original functions.
 * Row targets stateGameTypeSelect @0x41c010, stateNetworkMenu @0x420190,
 * gotoOptions @0x41d300, stateHighScoreTable @0x41dfd0 are TODO stubs in
 * stubs.c per Rebuild.md §19 (log + return to menu); the original
 * sndPlaySfx sound cues are skipped while the DSOUND mixer is out of scope.
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
int     g_nMenuFadeTarget; /* @0x45a6f0 (fade anim out of scope) */

/* Rebuild storage for menuInit/menuUpdate's original string and handle
 * tables. The original data lives in .rdata/.data at the noted addresses. */
const char * const g_kIntroTga[6] = {
    "menu\\intro_addgames.tga",    /* @0x450794 */
    "menu\\intro_och.tga",         /* @0x45078c */
    "menu\\intro_uds.tga",         /* @0x450780 */
    "menu\\intro_samarbete.tga",   /* @0x450774 */
    "menu\\intro_mcd.tga",         /* @0x450768 */
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

/* tgaLoad16 @0x415df0 — load the original 16-bit TGA surface buffer. */
unsigned short *tgaLoad16(LPCSTR path)
{
    int             dataOffset;
    char           *data;
    unsigned short *surface;
    unsigned short *dst;
    int             row;
    int             column;
    int             nextColumn;
    int             remaining;

    data = fileReadRaw(0, path);
    if (data == NULL) return NULL;

    dataOffset = (int)data[0] + 0x12 +
        (((int)data[7] * (unsigned int)*(unsigned short *)(data + 5) +
          ((((int)data[7] * (unsigned int)*(unsigned short *)(data + 5)) >> 31) & 7U)) >> 3);
    surface = (unsigned short *)memPoolAlloc(0, 0x4b000);

    if ((data[0x11] & 0x20U) == 0) {
        row = 0;
        do {
            column = 0;
            dst = (unsigned short *)((char *)surface + row);
            do {
                nextColumn = column + 2;
                *dst = *(unsigned short *)(data + (column - row) + 0x4ad80 + dataOffset);
                column = nextColumn;
                dst = dst + 1;
            } while (nextColumn < 0x280);
            row = row + 0x280;
        } while (row < 0x4b000);
        memPoolFree(0, data);
        return surface;
    }

    remaining = 0x25800;
    dst = surface;
    do {
        *dst = *(unsigned short *)(data + (dataOffset - (int)surface) +
                                   (int)dst);
        dst = dst + 1;
        remaining = remaining - 1;
    } while (remaining != 0);
    memPoolFree(0, data);
    return surface;
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
            g_nMenuRow = g_nMenuRow - 1;
            if (g_nMenuRow == -1) g_nMenuRow = 4;
            appLog("[menu] row %d (Up)", g_nMenuRow);
            break;
        case 3:   /* Down: wraps 4 -> 0 (original: row = -(row!=4) & (row+1)) */
            g_nMenuRow = (g_nMenuRow != 4) ? g_nMenuRow + 1 : 0;
            appLog("[menu] row %d (Down)", g_nMenuRow);
            break;
        case 6:   /* Enter: select row target */
            if (g_kMenuRowTarget[g_nMenuRow] != NULL) {
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

/* introUpdate @0x41ae50 — intro logo timeline. g_introFade_2 (ms) selects the
 * logo; paired thresholds leave a ~90ms cleared gap (fade-to-black) between
 * logos. Only key 4 (Space/fire), type 2, skips to the menu; other keydowns
 * continue the timeline as in the original.
 * Threshold floats @0x44b660-0x44b684: 2500/2590/3590/3680/6180/6270/7270/
 * 7360/9860/9950 + 17450 @0x44b65c. g_fl_25 @0x44b468 = 25.0f. */
int introUpdate(int nType, int nKey, int nKeyType)
{
    /* introUpdate @0x41ae50 consumes the high byte set in .data on the first
     * call. The CD/audio call is outside this rebuild, but the flag and its
     * early-return behavior are part of the original state transition. */
    if ((g_menuMode & 0x100) != 0) {
        g_menuMode &= ~0x100;
        appLog("[intro] initial music transition deferred");
        return 0;
    }

    if (nType == 1) {                  /* key event (dispatchKeyEvent path) */
        if (nKeyType != 2) {           /* original returns before timing update */
            return 0;
        }
        if (nKey == 4) {               /* original: Space/fire skips */
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

    /* memPoolSystemInit @0x4197a0 first (original order: commandDispatch,
     * then memPoolSystemInit, then gx/reset/asset loads). Creates pool 0
     * "DEFAULT" used by fileReadRaw/gxLoadTpgFile. */
    memPoolSystemInit();
    appLog("[menu] memPoolSystemInit: pool 0 = DEFAULT");
    gxResetState();
    gxClearScreen(1, 0);
    gxFlip();

    /* First .tpg load installs the DirectDraw palette (gxLoadTexture
     * @0x100019b0, first-call branch). MERGED00 shares the intro palette
     * (249/256 entries), matching menuInit's ordering. */
    if (gxLoadTpgFile("menu\\MERGED00.TPG") == 0) {
        appLog("[assets] menu\\MERGED00.TPG missing");
    } else {
        appLog("[assets] MERGED00.TPG loaded (palette set)");
    }

    /* Six intro logos in the original order (menuInit @0x419c20 load block;
         * imageLoadByMode here == tgaLoad16 @0x415df0. */
    for (i = 0; i < 6; i++) {
        g_hIntroTex[i] = tgaLoad16(g_kIntroTga[i]);
        if (g_hIntroTex[i] == NULL) {
            appLog("[assets] %s load failed", g_kIntroTga[i]);
        }
    }
    appLog("[assets] intro logos loaded (%d/6)", i);

    /* Menu fonts (menuInit @0x419c20 load block: fontPoolCreate @0x408f90
     * then five fontLoad pairs; each descriptor .txt is parsed by fontParse
     * and textured from a .tpg via gxLoadTpgFile). */
    fontPoolCreate();
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

    /* Quit-confirm background (menuInit @0x419c20 imageLoadByMode). */
    g_hMenuQuitTex = tgaLoad16("menu\\quit.tga");
    if (g_hMenuQuitTex == NULL) {
        appLog("[assets] menu\\quit.tga load failed");
    }

    /* Mirror menuInit's final timing/input reset. pollKeyboard is still the
     * documented DirectInput stub; window messages drive this slice instead. */
    g_nMenuRow        = 0;
    g_nMenuFadeTarget = 0;
    g_nLastFrameTime  = timeGetTime();
    g_flFrameDelta    = 0.0f;
    pollKeyboard();
    g_introFade_2     = 0.0f;

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
    /* menuInit clears only the low mode byte. introUpdate consumes the high
     * byte on its first invocation, matching the original two-byte flags. */
    g_menuMode &= ~0xff;
}
