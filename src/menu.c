#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "gx.h"
#include "pool.h"
#include "util.h"
#include "font.h"
#include "menu.h"
#include "stubs.h"

/* =====================================================================
 * Menu subsystem — reimplementation of the intro + main-menu state
 * machine. Compiled to maniac_rebuild.exe per Rebuild.md.
 *
 *   menuInit        @0x419c20  palette + intro logos + menu fonts + state
 *   introUpdate     @0x41ae50  6-logo intro timeline (fade-to-black gaps)
 *   menuUpdate      @0x41b0b0  main menu: 5 rows, two-font render, nav
 *   stateQuitConfirm @0x4200b0 "Avsluta" / Escape quit-confirm screen
 *   menuFramePost   gameFrameUpdate @0x41a8c0 flip/clear gate
 *
 * Row targets stateGameTypeSelect @0x41c010, stateNetworkMenu @0x420190,
 * gotoOptions @0x41d300, stateHighScoreTable @0x41dfd0 are TODO stubs in
 * stubs.c per Rebuild.md §19 (log + return to menu); the original
 * sndPlaySfx sound cues are skipped while the DSOUND mixer is out of scope.
 * ===================================================================== */

PStateFunc g_pStateFunc;          /* @0x45a6f8 */
float      g_flFrameDelta;        /* @0x45a6cc = elapsed ms * 0.04 */
DWORD      g_nLastFrameTime;      /* @0x45a65c */

/* Intro timeline accumulator (maniac g_introFade_2 @0x45d444, ms). */
static float g_introFade_2;

/* Intro logos in load/present order — mirrors menuInit @0x419c20 and the
 * maniac globals g_hIntroTexAddgames..g_hIntroTexPresenterar @0x45a618-0x45a62c
 * (original string table s_menu_intro_*_tga @0x450794-0x450720). */
static const char * const g_kIntroTga[6] = {
    "menu\\intro_addgames.tga",    /* g_hIntroTexAddgames   @0x45a618 */
    "menu\\intro_och.tga",         /* g_hIntroTexOch        @0x45a61c */
    "menu\\intro_uds.tga",         /* g_hIntroTexUds        @0x45a620 */
    "menu\\intro_samarbete.tga",   /* g_hIntroTexSamarbete  @0x45a624 */
    "menu\\intro_mcd.tga",         /* g_hIntroTexMcd        @0x45a628 */
    "menu\\intro_presenterar.tga"  /* g_hIntroTexPresenterar @0x45a62c */
};
static void *g_hIntroTex[6];

/* Menu assets (maniac globals): fonts @0x45a644-0x45a654, quit texture
 * @0x45a640, selected row g_nMenuRow @0x45d448, fade target @0x45a6f0. The
 * original stores font/texture handles as uint; we use typed pointers. */
static gxFont *g_hMenuFontTiny;   /* @0x45a654 tinyfont.txt + TINY00.TPG */
static gxFont *g_hMenuFontSmall;  /* @0x45a644 menysmallfont.txt + MSFONT00.TPG */
static gxFont *g_hMenuFont;       /* @0x45a648 menyfont.txt + MFONT00.TPG */
static gxFont *g_hMenuMsfnt;      /* @0x45a64c menysmallfont.txt + MSFNT200.TPG */
static gxFont *g_hMenuMfnt;       /* @0x45a650 menyfont.txt + MFNT200.TPG */
static void   *g_hMenuQuitTex;    /* @0x45a640 menu\quit.tga */
static int     g_nMenuRow;        /* @0x45d448 selected row (0..4) */
static int     g_nMenuFadeTarget; /* @0x45a6f0 (fade anim out of scope) */

static int stateQuitConfirm(int nType, int nKey, int nKeyType);    /* @0x4200b0 */
static void introPresent(int idx);
static void introClear(void);

/* Main-menu rows (menuUpdate @0x41b0b0). Labels mirror the .rdata strings
 * s_Spela @0x450828 / "Nätverk" @0x450820 / s_Alternativ @0x450814 /
 * s_Rekord @0x45080c / s_Avsluta @0x450804. Targets mirror the local_4e0
 * dispatch table; the first four are stubs declared in stubs.h. */
static const char * const g_kMenuRowLabel[5] = {
    "Spela", "N\xe4tverk", "Alternativ", "Rekord", "Avsluta"
};
static PStateFunc g_kMenuRowTarget[5] = {
    stateGameTypeSelect, stateNetworkMenu, gotoOptions, stateHighScoreTable,
    stateQuitConfirm
};

/* menuUpdate @0x41b0b0 stream splitter: a-z plus the Swedish lowercase
 * vowels å/ä/ö (0xe5/0xe4/0xf6) are drawn with the small menu font; every
 * other character (uppercase, digits, space) with the large font. */
static int menuIsSmallChar(int c)
{
    unsigned int u = (unsigned char)c;
    return (u > 0x60 && u < 0x7b) || u == 0xe5 || u == 0xe4 || u == 0xf6;
}

/* Measure one menu row. The small-font and large-font tokens are measured
 * with g_hMenuFontSmall / g_hMenuFont (menuUpdate @0x41b0b0 width passes;
 * the "200" highlight fonts share the same descriptor so their widths match). */
static int menuRowWidth(const char *label)
{
    char         tok[128];
    int          w = 0;
    const char  *p = label;

    while (*p != '\0') {
        const char *q;
        int         n;

        q = p;
        while (*q != '\0' && menuIsSmallChar((unsigned char)*q)) q = q + 1;
        n = (int)(q - p);
        if (n > 0) {
            memcpy(tok, p, (size_t)n); tok[n] = '\0';
            w += textWidth(g_hMenuFontSmall, tok);
            p = q;
            continue;
        }
        q = p;
        while (*q != '\0' && !menuIsSmallChar((unsigned char)*q)) q = q + 1;
        n = (int)(q - p);
        if (n > 0) {
            memcpy(tok, p, (size_t)n); tok[n] = '\0';
            w += textWidth(g_hMenuFont, tok);
            p = q;
        }
    }
    return w;
}

/* Draw one menu row centered on x = 0x226 at the given y. Selected row uses
 * the "200" highlight fonts g_hMenuMsfnt/g_hMenuMfnt, unselected rows
 * g_hMenuFontSmall/g_hMenuFont; both at color 0x2004 (menuUpdate @0x41b0b0). */
static void menuRowDraw(const char *label, int y, int bSelected)
{
    gxFont      *small = bSelected ? g_hMenuMsfnt : g_hMenuFontSmall;
    gxFont      *big   = bSelected ? g_hMenuMfnt  : g_hMenuFont;
    char         tok[128];
    int          x;
    const char  *p;

    if (small == NULL || big == NULL) return;
    x = 0x226 - menuRowWidth(label);
    p = label;

    while (*p != '\0') {
        const char *q;
        int         n;

        q = p;
        while (*q != '\0' && menuIsSmallChar((unsigned char)*q)) q = q + 1;
        n = (int)(q - p);
        if (n > 0) {
            memcpy(tok, p, (size_t)n); tok[n] = '\0';
            textDraw(small, 0x2004, x, y, tok);
            x += textWidth(small, tok);
            p = q;
            continue;
        }
        q = p;
        while (*q != '\0' && !menuIsSmallChar((unsigned char)*q)) q = q + 1;
        n = (int)(q - p);
        if (n > 0) {
            memcpy(tok, p, (size_t)n); tok[n] = '\0';
            textDraw(big, 0x2004, x, y, tok);
            x += textWidth(big, tok);
            p = q;
        }
    }
}

/* stateQuitConfirm @0x4200b0 — "Avsluta" row / Escape. Presents menu\quit.tga;
 * a confirm key quits the app, any other keydown returns to the main menu.
 * The original tests the J/Y/j/y characters; the rebuild's key-id layer
 * accepts the confirm keys 4=Space and 6=Enter instead. */
static int stateQuitConfirm(int nType, int nKey, int nKeyType)
{
    if (g_hMenuQuitTex != NULL) {
        presentFrame(g_hMenuQuitTex);
    }
    if (nType == 1) {
        if (nKeyType == 2) {
            if (nKey == 4 || nKey == 6) {       /* "Ja" (original: J/Y) */
                appLog("[menu] quit confirmed");
                PostQuitMessage(0);
                return 0;
            }
            appLog("[menu] quit-confirm: key %d returns to menu", nKey);
        }
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
            menuRowDraw(g_kMenuRowLabel[row], row * 0x29 + 0xbe,
                        (row == g_nMenuRow) ? 1 : 0);
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
 * logos. Any keydown skips to the menu (the original skips on key 4, type 2;
 * docs/03-gameflow.md note "any key or ENTER skips" — we accept all keys).
 * Threshold floats @0x44b660-0x44b684: 2500/2590/3590/3680/6180/6270/7270/
 * 7360/9860/9950 + 17450 @0x44b65c. g_fl_25 @0x44b468 = 25.0f. */
static int introUpdate(int nType, int nKey, int nKeyType)
{
    (void)nKey;
    if (nType == 1) {                  /* key event (dispatchKeyEvent path) */
        if (nKeyType == 2) {           /* keydown -> skip to menu */
            appLog("[intro] skipped to menu by key @%.0f ms", g_introFade_2);
            g_pStateFunc = menuUpdate;
        }
        return 0;
    }

    /* Accumulate elapsed ms (g_flFrameDelta == elapsed ms * 0.04; *25.0 -> ms). */
    g_introFade_2 += g_flFrameDelta * 25.0f;

    if (g_introFade_2 < 2500.0f)  { introPresent(0); return 0; }
    if (g_introFade_2 < 2590.0f)  { introClear();    return 0; }
    if (g_introFade_2 < 3590.0f)  { introPresent(1); return 0; }
    if (g_introFade_2 < 3680.0f)  { introClear();    return 0; }
    if (g_introFade_2 < 6180.0f)  { introPresent(2); return 0; }
    if (g_introFade_2 < 6270.0f)  { introClear();    return 0; }
    if (g_introFade_2 < 7270.0f)  { introPresent(3); return 0; }
    if (g_introFade_2 < 7360.0f)  { introClear();    return 0; }
    if (g_introFade_2 < 9860.0f)  { introPresent(4); return 0; }
    if (g_introFade_2 < 9950.0f)  { introClear();    return 0; }
    if (g_introFade_2 < 17450.0f) { introPresent(5); return 0; }

    /* Timeline done (>= 17450 ms): original sets g_pStateFunc = menuUpdate and
     * plays the menu CD track (introUpdate @0x41ae50 LAB_0041b082). */
    appLog("[intro] timeline complete (%.0f ms) -> menuUpdate", g_introFade_2);
    g_pStateFunc = menuUpdate;
    return 0;
}

/* Present one intro logo, logging the first time each logo is shown. */
static void introPresent(int idx)
{
    static int nShown = -1;
    if (idx != nShown) {
        nShown = idx;
        appLog("[intro] logo %d/%d %s @%.0f ms", idx + 1, 6,
               g_kIntroTga[idx], g_introFade_2);
    }
    presentFrame(g_hIntroTex[idx]);
}

/* Clear to g_nClearColor (0) + flip — the gap between intro logos
 * (introUpdate @0x41ae50: gxClearScreen(1,g_nClearColor); gxFlip();). */
static void introClear(void)
{
    gxClearScreen(1, 0);   /* g_nClearColor @0x45892c = 0 */
    gxFlip();
}

/* menuInit @0x419c20 — asset loads + intro/menu state setup. Narrowed to:
 * a .tpg to install the driver palette, the six intro logos, the menu fonts
 * (fontPoolCreate @0x408f90 + five fontLoad pairs) and quit.tga. The
 * original selects introUpdate because g_menuMode @0x45022c is
 * preinitialized to 0x101 in .data (low byte = 1 -> intro) and clears it
 * after; the rebuild always starts with the intro. nRestartMode mirrors the
 * original's argument (0 = first init, 1 = return from options with music
 * track 7) but is unused here since the rebuild always runs the intro. */
void menuInit(int nRestartMode)
{
    size_t size = 0;
    void  *tpg;
    int    i;

    (void)nRestartMode;

    /* memPoolSystemInit @0x4197a0 first (original order: commandDispatch,
     * then memPoolSystemInit, then gx/reset/asset loads). Creates pool 0
     * "DEFAULT" used by fileReadRaw/gxLoadTpgFile. */
    memPoolSystemInit();
    appLog("[menu] memPoolSystemInit: pool 0 = DEFAULT");

    /* First .tpg load installs the DirectDraw palette (gxLoadTexture
     * @0x100019b0, first-call branch). MERGED00 shares the intro palette
     * (249/256 entries), matching menuInit's ordering. */
    tpg = readFileAlloc("menu\\MERGED00.TPG", &size);
    if (tpg == NULL) {
        appLog("[assets] menu\\MERGED00.TPG missing");
    } else if (size < 0x10400) {
        appLog("[assets] menu\\MERGED00.TPG too small (%u bytes)", (unsigned)size);
        free(tpg);
    } else {
        gxLoadTexture(0, 1, "MERGED00", tpg, (char *)tpg + 0x10000);
        appLog("[assets] MERGED00.TPG loaded (%u bytes, palette set)", (unsigned)size);
        free(tpg);
    }

    /* Six intro logos in the original order (menuInit @0x419c20 load block;
     * imageLoadByMode here == tgaLoad16 @0x415df0 path via loadTga640x480). */
    for (i = 0; i < 6; i++) {
        g_hIntroTex[i] = loadTga640x480(g_kIntroTga[i]);
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
    g_hMenuQuitTex = loadTga640x480("menu\\quit.tga");
    if (g_hMenuQuitTex == NULL) {
        appLog("[assets] menu\\quit.tga load failed");
    }

    /* Mirror menuInit: g_nLastFrameTime = getGameTime(); g_pStateFunc =
     * introUpdate (intro selected; see header comment on g_menuMode). */
    g_nMenuRow        = 0;
    g_nMenuFadeTarget = 0;
    g_nLastFrameTime  = timeGetTime();
    g_introFade_2     = 0.0f;
    g_pStateFunc      = introUpdate;
}

/* menuFramePost — mirrors gameFrameUpdate @0x41a8c0's tail: after the state
 * update the menu states flip + clear. introUpdate and stateQuitConfirm are
 * excluded because they present their own frames. */
void menuFramePost(void)
{
    if (g_pStateFunc != introUpdate && g_pStateFunc != stateQuitConfirm) {
        gxFlip();
        gxClearScreen(1, 0);   /* g_nClearColor @0x45892c = 0 */
    }
}
