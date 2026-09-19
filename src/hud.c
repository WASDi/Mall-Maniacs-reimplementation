/* =====================================================================
 * hud.c — in-game HUD overlay (renderGameHud @0x412810) and its shared
 * texture loader (hudLoadGraphics @0x412700).
 *
 * renderGameHud runs every rendered frame from gameFrameRender @0x40ae30
 * (after the scene pass, before gxFlip) and draws, in the original order:
 *   scroll-text console overlay, results screen (per game mode), quit
 *   prompt, mode-3 "Varor" target item, checkout banner, the sliding
 *   shopping-list panel (modes 1/2), the race timer (modes 1/4), the frog
 *   message panel, the local player face + checkout progress bar, the
 *   rank counter, the net-wait banner, and the phase countdown
 *   ("Klara!"/"Färdiga!"/"Gå!!") with its sfx.
 * ===================================================================== */
#include <stdlib.h>
#include <string.h>

#include "custom_helpers.h"
#include "font.h"
#include "gameplay.h"
#include "gx.h"
#include "hud.h"
#include "level.h"
#include "levelselect.h"
#include "menu.h"
#include "player.h"
#include "sound.h"
#include "stubs.h"
#include "util.h"

/* --- HUD textures @0x458980..0x45898c --- */
void *g_hHudListTpg;   /* @0x458980 hud/list00.tpg */
void *g_hHudCharTpg;   /* @0x458984 hud/char00.tpg */
void *g_hHudGfx2Tpg;   /* @0x458988 hud/gfx2200.tpg */
void *g_hHudFrogeTpg;  /* @0x45898c hud/froge00.tpg */

/* --- HUD draw state @0x458990..0x4589a4 --- */
int g_nScoreDisplay;         /* @0x458990 */
int g_nInvBarCur;            /* @0x458994 */
int g_nInvBarTarget;         /* @0x458998 */
unsigned char g_bItemSfx;      /* @0x45899c */
unsigned char g_bCheckoutSfx;  /* @0x45899d */
int g_nPhaseStartTime;       /* @0x4589a0 */
int g_nLastSfx;              /* @0x4589a4 */

/* --- scroll/console rows (drawn by the HUD, fed by the console) --- */
char g_acScrollLines[29][100];  /* @0x4589a8 */
int  g_nScrollLineCount;        /* @0x4594fc */
char g_acConsoleLines[29][100]; /* @0x4551e0 */
int  g_nConsoleLineCount;       /* @0x455d34 */

/* --- shared scratch string buffer @0x4550d8 (drawn by wait overlays) --- */
char g_acScratchText[256];

/* Item name lookup (original @0x412fc0 / @0x41347c): the name area at
 * 0x45839c is indexed with 0x2c stride, i.e. g_apLevelItemSlots[id-1].szName
 * (config items[id-1]/name, filled by levelSetup). Ids outside 1..30 (the
 * mode-4 CHECKFLAG ids 200+) read zeroed/foreign .bss in the original; the
 * rebuild draws an empty name there. */
#define HUD_ITEM_NAME(nId) \
    (((nId) >= 1 && (nId) <= LEVEL_ITEM_SLOT_COUNT) \
         ? g_apLevelItemSlots[(nId) - 1].szName : "")

/* format/name strings used by the HUD (verbatim, with addresses) */
#define SZ_TIME_FMT        "%02d:%02d:%02d"                /* @0x44ff94 */
#define SZ_KLARA           "Klara!"                        /* @0x44fe44 */
#define SZ_FARDIGA         "F\xe4rdiga!"                   /* @0x44fe38 "Färdiga!" */
#define SZ_GA              "G\xc5!!"                       /* @0x44fe30 "Gå!!" */
#define SZ_VANTAR          "V\xe4ntar..."                  /* @0x44fe4c "Väntar..." */
#define SZ_VAROR           "Varor"                         /* @0x44fe78 */
#define SZ_MOT_KASSORNA    "Mot kassorna!!"                /* @0x44fe64 */
#define SZ_J_ELLER_N       "J eller N?"                    /* @0x44fe58 */
#define SZ_AVSLUTA         "Avsluta spelet? J eller N"     /* @0x44fe80 */
#define SZ_SLASH           "/"                             /* @0x44fe74 */
#define SZ_NYTT_REKORD     "NYTT REKORD!!"                 /* @0x44ff70 */
#define SZ_WINNER          "Vinnare"                       /* @0x44ff48 */
#define SZ_TRYCK_ENTER_F   "Tryck ENTER f\xf6r att forts\xe4tta!"     /* @0x44ff08 */
#define SZ_TRYCK_ENTER_F_P "Tryck ENTER f\xf6r att forts\xe4tta."     /* @0x44ffc4 */
#define SZ_TRYCK_ENTER_A   "Tryck ENTER f\xf6r att avsluta."          /* @0x44ffa4 */
#define SZ_TRYCK_ESC_A     "Tryck ESC f\xf6r att avsluta."            /* @0x44fed0 */
#define SZ_NYTT_FORSOK     "Nytt f\xf6rs\xf6k tryck ENTER."           /* @0x44feec */
#define SZ_GET_FSHI        "get fshi%dtime%d"              /* @0x44ff80 */
#define SZ_REQ_FSHI        "request fshiscore %d %d %d %d" /* @0x44ff50 */
#define SZ_GET_VAHI        "get vahi%dtime%d"              /* @0x44febc */
#define SZ_REQ_VAHI        "request vahiscore %d %d %d %d" /* @0x44fe9c */
#define SZ_GET_TOPLEVEL    "get toplevel"                  /* @0x44ff38 */
#define SZ_SET_TOPLEVEL    "set toplevel %d"               /* @0x44ff28 */
#define SZ_CMD_SAVE        "save"                          /* @0x44e398 */

/* Countdown arrow pair (inlined three times in the original: @0x413d3e,
 * @0x413f93, @0x4141cc). One macro expansion = the two gxDrawPolygon calls
 * of the left (0xcf00..0xf900) and right (0x18700/0x1b100) arrow quads over
 * the [u0,u1]x[0x9700,0xaa00] UV window of g_hHudGfx2Tpg. nMirror reverses
 * the right arrow's x order (phase -1 "Gå!!" block @0x4142a0). */
#define HUD_COUNTDOWN_ARROWS(u0, u1, nMirror)                                    \
    memset(&cu, 0, sizeof(cu));                                                  \
    cu.pTexture = g_hHudGfx2Tpg;                                                 \
    cu.U = (u0);  cu.U2 = (u0);                                                  \
    cu.gwU = (u1); cu.gwU2 = (u1);                                               \
    cu.V = 0x9700; cu.V2 = 0x9700;                                               \
    cu.hV = 0xaa00; cu.hV2 = 0xaa00;                                             \
    memset(&v0, 0, sizeof(v0)); memset(&v1, 0, sizeof(v1));                      \
    memset(&v2, 0, sizeof(v2)); memset(&v3, 0, sizeof(v3));                      \
    v0.x = 0xcf00;  v0.y = 0x4000;                                               \
    v1.x = 0xf900;  v1.y = 0x4000;                                               \
    v2.x = 0xf900;  v2.y = 0x5400;                                               \
    v3.x = 0xcf00;  v3.y = 0x5400;                                               \
    setSignVerts(&v0, &v1, &v2, &v3);                                            \
    gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &cu);                              \
    v0.x = (nMirror) ? 0x1b100 : 0x18700;                                        \
    v1.x = (nMirror) ? 0x18700 : 0x1b100;                                        \
    v2.x = (nMirror) ? 0x18700 : 0x1b100;                                        \
    v3.x = (nMirror) ? 0x1b100 : 0x18700;                                        \
    v0.y = 0x4000;  v1.y = 0x4000;                                               \
    v2.y = 0x5400;  v3.y = 0x5400;                                               \
    setSignVerts(&v0, &v1, &v2, &v3);                                            \
    gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &cu);

/* hudLoadGraphics @0x412700 — load the 4 shared HUD textures via
 * gxLoadTpgFile; each failure is a fatal error. Called from roundStartInit
 * @0x40a7f7. */
void hudLoadGraphics(void)
{
    char szPath[124];

    fmtSprintf(szPath, "%s\\hud\\list00.tpg", g_aszLevelDirs[g_nLevelIdx]); /* @0x44fe1c? see note */
    g_hHudListTpg = (void *)gxLoadTpgFile(szPath);
    if (g_hHudListTpg == NULL) {
        fatalError("HUD graphics not found!");               /* @0x44fe04 @0x412741 */
    }
    fmtSprintf(szPath, "%s\\hud\\char00.tpg", g_aszLevelDirs[g_nLevelIdx]); /* @0x44fdf0 @0x412766 */
    g_hHudCharTpg = (void *)gxLoadTpgFile(szPath);
    if (g_hHudCharTpg == NULL) {
        fatalError("HUD graphics not found!");               /* @0x41277b */
    }
    fmtSprintf(szPath, "%s\\hud\\gfx2200.tpg", g_aszLevelDirs[g_nLevelIdx]); /* @0x44fddc @0x41278b */
    g_hHudGfx2Tpg = (void *)gxLoadTpgFile(szPath);
    if (g_hHudGfx2Tpg == NULL) {
        fatalError("HUD graphics not found!");               /* @0x4127a0 */
    }
    fmtSprintf(szPath, "%s\\hud\\froge00.tpg", g_aszLevelDirs[g_nLevelIdx]); /* @0x44fdc8 @0x4127b0 */
    g_hHudFrogeTpg = (void *)gxLoadTpgFile(szPath);
    if (g_hHudGfx2Tpg == NULL) {                             /* sic — re-tests gfx2 @0x4127c2 */
        fatalError("HUD graphics not found!");               /* @0x4127c7 */
    }
}

/* renderGameHud @0x412810 — the full HUD overlay (see file header). */
void renderGameHud(void)
{
    GxVert v0, v1, v2, v3;
    GxColorUv cu;
    char szBuf[124];
    int i, j, n, nTmp, nRank, nMyTaken;

    if (g_nScrollText != 0) {                                /* @0x41437a */
        gxDrawQuadColor(g_hHudListTpg, 0, 0, 0x280, 0xff, 0, 0, 10, 10);
        i = 0x19;                                            /* y = 25 */
        if (g_nScrollLineCount + 0x14 < 0x1d) {              /* @0x4143a6 */
            for (j = g_nScrollLineCount + 0x14; j < 0x1d; j++) {
                textDraw(g_hHudFont, 0x2004, 5, i, g_acScrollLines[j]);
                i += 0x15;
            }
        }
        n = g_nScrollLineCount - 9;                          /* @0x4143e7 */
        if (n < 0) n = 0;
        for (; n < g_nScrollLineCount; n++) {
            textDraw(g_hHudFont, 0x2004, 5, i, g_acScrollLines[n]);
            i += 0x15;
        }
        textDraw(g_hHudFont, 0x2004, 5, i,
                 g_acConsoleLines[g_nConsoleLineCount]);     /* @0x41442d */
        return;
    }

    if (g_nResultsScreen != 0) {                             /* @0x412826 */
        int bFlash = (g_nGameTime % 4000) < 2000;            /* @0x412832 */
        PlayerRecord *pWinner = &g_playerRecords[g_nWinnerIdx];
        int nWinTime = pWinner->nScoreTicks * g_nObjUpdateTime;

        switch (g_nGameMode) {
        case 1:                                              /* @0x412865 */
        case 4:                                              /* @0x412c57 */
            gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x190, 0x1bf, 0x1c3, 0, 0, 0xff, 0x32);
            textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x1a2,
                             SZ_TRYCK_ENTER_F_P);
            nTmp = nWinTime / 100;                           /* @0x412909 */
            fmtSprintf(szBuf, SZ_TIME_FMT, nTmp / 60, nTmp % 60, nWinTime % 100);
            gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x1e, 0x1bf, 0x51, 0, 0, 0xff, 0x32);
            textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x30, szBuf);
            if (g_nLocalPlayerIdx == g_nWinnerIdx) { /* @0x412993 */
                for (i = 0; i < 5; i++) {                    /* @0x4129b8 */
                    fmtSprintf(szBuf, g_nGameMode == 1 ? SZ_GET_FSHI : SZ_GET_VAHI,
                               g_nLevelIdx, g_nWinnerIdx);
                    n = fmtAtoi((const char *)commandDispatch(0, szBuf));
                    if (nWinTime / 10 < n || n < 1) {
                        if (i < 5) {
                            if (bFlash) {
                                gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x1e, 0x1bf, 0x51,
                                                0, 0, 0xff, 0x32);
                                textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x30,
                                                 SZ_NYTT_REKORD);
                            }
                            if (g_bGameRunning) {
                                fmtSprintf(szBuf,
                                           g_nGameMode == 1 ? SZ_REQ_FSHI : SZ_REQ_VAHI,
                                           nWinTime, i, pWinner->nCharIdx, g_nLevelIdx);
                                commandDispatch(0, szBuf);   /* @0x412e72 */
                            }
                        }
                        break;
                    }
                }
            }
            break;
        case 2:                                              /* @0x412a89 */
        case 3:
            gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x1e, 0x1bf, 0x51, 0, 0, 0xff, 0x32);
            if (bFlash) {
                textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x30, SZ_WINNER);
            } else {
                textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x30,
                                 pWinner->szCharName);
            }
            if (g_nWinnerIdx == g_nLocalPlayerIdx) {
                n = fmtAtoi((const char *)commandDispatch(0, SZ_GET_TOPLEVEL)); /* @0x412b26 */
                if (n == g_nLevelIdx && g_bGameRunning) {
                    fmtSprintf(szBuf, SZ_SET_TOPLEVEL, g_nLevelIdx + 1); /* @0x412b4c */
                    commandDispatch(0, szBuf);
                    commandDispatch(0, SZ_CMD_SAVE);     /* @0x412b68 */
                }
            }
            if (bFlash) {
                gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x190, 0x1bf, 0x1c3, 0, 0, 0xff, 0x32);
                textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x1a2,
                                 g_nWinnerIdx == g_nLocalPlayerIdx
                                     ? SZ_TRYCK_ENTER_F : SZ_NYTT_FORSOK);
            } else {
                gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x190, 0x1bf, 0x1c3, 0, 0, 0xff, 0x32);
                textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x1a2, SZ_TRYCK_ESC_A);
            }
            break;
        }
        /* winner face @0x412e7c */
        i = pWinner->nCharIdx;
        gxDrawQuadColor(g_hHudCharTpg, 0xac, 0x1e, 0xec, 0x5e,
                        (i & 3) * 0x40, (i / 4) * 0x40,
                        (i & 3) * 0x40 + 0x3f, (i / 4) * 0x40 + 0x3f);
        g_bGameRunning = 0;                                  /* @0x412ed9 */
        return;
    }

    g_bGameRunning = 1;                                      /* @0x412eec */

    if (g_bQuitPrompt != 0) {                                /* @0x412ee7 */
        gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x32, 0x1bf, 0x65, 0, 0, 0xff, 0x32);
        textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x44, SZ_AVSLUTA);
        return;
    }

    if (g_nGameMode == 3) {                                  /* @0x412f3e */
        if (g_nCurrentItemId == 0) {
            g_bItemSfx = 1;                                  /* @0x412f65 */
            g_nScoreDisplay = g_playerRecords[0].nScoreTicks * g_nObjUpdateTime; /* @0x412f6c */
        } else {
            if (g_playerRecords[g_nLocalPlayerIdx].anHeldSlot[1] < 5) { /* @0x412f8f */
                gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x32, 0x1bf, 0x65, 0, 0, 0xff, 0x32);
                textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x44,
                                 HUD_ITEM_NAME(g_nCurrentItemId)); /* @0x412fc0 */
                memset(&cu, 0, sizeof(cu));
                cu.pTexture = g_hHudGfx2Tpg;                 /* @0x413068 */
                cu.V = 0x7d00;  cu.V2 = 0x7d00;              /* @0x412fe1 */
                cu.gwU = 0x6400; cu.gwU2 = 0x6400;
                cu.hV = 0xd600; cu.hV2 = 0xd600;
                memset(&v0, 0, sizeof(v0)); memset(&v1, 0, sizeof(v1));
                memset(&v2, 0, sizeof(v2)); memset(&v3, 0, sizeof(v3));
                v0.x = 0x7000;  v0.y = 0x1400;               /* @0x41300e */
                v1.x = 0xd500;  v1.y = 0x1400;
                v2.x = 0xd500;  v2.y = 0x6e00;
                v3.x = 0x7000;  v3.y = 0x6e00;
                setSignVerts(&v0, &v1, &v2, &v3);
                gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &cu); /* @0x413098 */
                if (g_bItemSfx &&
                    g_playerRecords[g_nLocalPlayerIdx].anHeldSlot[1] < 5) { /* @0x4130ab */
                    sndPlaySfx(0, 1, 0x1c, 0xffff, 0, 0x400); /* @0x4130cd */
                    g_bItemSfx = 0;                          /* @0x4130e5 */
                }
            }
        }
        gxDrawQuadColor(g_hHudGfx2Tpg, 2, 10, 0x6a, 0x3d, 0x99, 0xac, 0xff, 0xde); /* @0x4130eb */
        textDrawCentered(g_hHudFontTiny, 0x2004, -0x10a, 0x1c, SZ_VAROR); /* @0x413113 */
        textDrawInt(g_hHudFontDigits, 0x2004, 10, 0x28,
                    g_playerRecords[g_nLocalPlayerIdx].anHeldSlot[1]); /* @0x413140 */
        i = textIntWidth(g_hHudFontDigits,
                         g_playerRecords[g_nLocalPlayerIdx].anHeldSlot[1]) + 0xa; /* @0x413185 */
        textDraw(g_hHudFontDigits, 0x2004, i, 0x32, SZ_SLASH); /* @0x41319d */
        i += textWidth(g_hHudFontDigits, SZ_SLASH);          /* @0x4131ae */
        textDrawInt(g_hHudFontDigits, 0x2004, i, 0x3c, 5);   /* @0x4131c6 */
    }

    if (g_playerRecords[g_nLocalPlayerIdx].bStateFlags & 0x20) { /* @0x4131e6 */
        if (g_bCheckoutSfx == 0) {
            sndPlaySfx(0, 1, 0x1c, 0xffff, 0, 0x400);        /* @0x4131fc */
        }
        gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x32, 0x1bf, 0x65, 0, 0, 0xff, 0x32);
        textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x44, SZ_MOT_KASSORNA); /* @0x41323d */
        memset(&cu, 0, sizeof(cu));
        cu.pTexture = g_hHudGfx2Tpg;                         /* @0x4132d7 */
        cu.V = 0x7d00;  cu.V2 = 0x7d00;
        cu.gwU = 0x6400; cu.gwU2 = 0x6400;
        cu.hV = 0xd600; cu.hV2 = 0xd600;
        memset(&v0, 0, sizeof(v0)); memset(&v1, 0, sizeof(v1));
        memset(&v2, 0, sizeof(v2)); memset(&v3, 0, sizeof(v3));
        v0.x = 0x7000;  v0.y = 0x1400;
        v1.x = 0xd500;  v1.y = 0x1400;
        v2.x = 0xd500;  v2.y = 0x6e00;
        v3.x = 0x7000;  v3.y = 0x6e00;
        setSignVerts(&v0, &v1, &v2, &v3);
        gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &cu);      /* @0x413306 */
        g_bCheckoutSfx = 1;                                  /* @0x41331a */
    } else {
        g_bCheckoutSfx = 0;                                  /* @0x413323 */
    }

    if (g_nGameMode != 4 && g_nGameMode != 3) {              /* @0x413329 */
        g_nInvBarTarget = 0xdc;                              /* @0x413343 */
        for (i = 0; i < 10; i++) {
            if (g_playerRecords[g_nLocalPlayerIdx].abListTaken[i] != 0) {
                g_nInvBarTarget -= 0x16;
            }
        }
        if (g_nInvBarCur != g_nInvBarTarget) {
            g_nInvBarCur = (g_nInvBarCur + g_nInvBarTarget) / 2; /* @0x413380 */
        }
        if (g_nInvBarCur != 0) {                             /* @0x413390 */
            memset(&cu, 0, sizeof(cu));
            cu.pTexture = g_hHudListTpg;                     /* @0x413412 */
            cu.V = (0xff - g_nInvBarCur) * 0x100;            /* @0x413398 */
            cu.V2 = cu.V;
            cu.gwU = 0xc800; cu.gwU2 = 0xc800;
            cu.hV = 0xff00;  cu.hV2 = 0xff00;
            memset(&v0, 0, sizeof(v0)); memset(&v1, 0, sizeof(v1));
            memset(&v2, 0, sizeof(v2)); memset(&v3, 0, sizeof(v3));
            v0.x = 0;       v0.y = 0;
            v1.x = 0xc800;  v1.y = 0;
            v2.x = 0xc800;  v2.y = (g_nInvBarCur + 0x16) * 0x100; /* @0x41339d */
            v3.x = 0;       v3.y = (g_nInvBarCur + 0x16) * 0x100;
            setSignVerts(&v0, &v1, &v2, &v3);
            gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &cu);  /* @0x413442 */
            j = 0;                                           /* row y accumulator */
            for (i = 0; i < 10; i++) {                       /* @0x413454 */
                if (g_playerRecords[g_nLocalPlayerIdx].abListTaken[i] == 0) {
                    textDraw(g_hHudFont, 0x2004, 5,
                             (j - g_nInvBarTarget) + 5 + g_nInvBarCur,
                             HUD_ITEM_NAME(g_playerRecords[g_nLocalPlayerIdx].anListIds[i]));
                    j += 0x16;                               /* @0x4134b3 */
                }
            }
        }
    }

    if (g_nGameMode == 1 || g_nGameMode == 4) {              /* @0x4134c2 */
        nTmp = g_playerRecords[g_nLocalPlayerIdx].nScoreTicks * g_nObjUpdateTime; /* @0x4134dc */
        i = nTmp / 100;
        fmtSprintf(szBuf, SZ_TIME_FMT, i / 60, i % 60, nTmp % 100); /* @0x413544 */
        gxDrawQuadColor(g_hHudGfx2Tpg, 0x217, 2, 0x27f, 0x35, 0x99, 0xac, 0xff, 0xde); /* @0x413549 */
        textDrawCentered(g_hHudFontTiny, 0x2004, 0x10b, 0x14, szBuf); /* @0x413577 */
    }

    if (g_playerRecords[g_nLocalPlayerIdx].pQuestMessage != NULL) { /* @0x4135ae */
        memset(&cu, 0, sizeof(cu));
        cu.pTexture = g_hHudFrogeTpg;                        /* @0x413634 */
        cu.gwU = 0xff00; cu.gwU2 = 0xff00;                   /* @0x4135bb */
        cu.hV = 0xff00;  cu.hV2 = 0xff00;
        memset(&v0, 0, sizeof(v0)); memset(&v1, 0, sizeof(v1));
        memset(&v2, 0, sizeof(v2)); memset(&v3, 0, sizeof(v3));
        v0.x = 0xc000;  v0.y = 0x7000;
        v1.x = 0x1c000; v1.y = 0x7000;
        v2.x = 0x1c000; v2.y = 0x17000;
        v3.x = 0xc000;  v3.y = 0x17000;
        setSignVerts(&v0, &v1, &v2, &v3);
        gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &cu);      /* @0x413663 */
        textDrawWrappedCentered(g_playerRecords[g_nLocalPlayerIdx].pQuestMessage,
                                0xc0, 0xa7, 0xe2);           /* @0x413692 */
        gxDrawQuadColor(g_hHudGfx2Tpg, 0x10c, 0x154, 0x174, 0x187, 0x99, 0xac, 0xff, 0xde); /* @0x413697 */
        textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x166, SZ_J_ELLER_N); /* @0x4136d1 */
    }

    /* local player face @0x4136f0 */
    i = g_playerRecords[g_nLocalPlayerIdx].nCharIdx;
    memset(&cu, 0, sizeof(cu));
    cu.pTexture = g_hHudCharTpg;                             /* @0x4137ac */
    cu.U = (i & 3) * 0x40 * 0x100;                           /* @0x413731 */
    cu.U2 = cu.U;
    cu.V = (i / 4) * 0x40 * 0x100;                           /* @0x413737 */
    cu.V2 = cu.V;
    cu.gwU = ((i & 3) * 0x40 + 0x3f) * 0x100;
    cu.gwU2 = cu.gwU;
    cu.hV = ((i / 4) * 0x40 + 0x3f) * 0x100;
    cu.hV2 = cu.hV;
    memset(&v0, 0, sizeof(v0)); memset(&v1, 0, sizeof(v1));
    memset(&v2, 0, sizeof(v2)); memset(&v3, 0, sizeof(v3));
    v0.x = 0x1e000; v0.y = 0xa00;
    v1.x = 0x22000; v1.y = 0xa00;
    v2.x = 0x22000; v2.y = 0x4a00;
    v3.x = 0x1e000; v3.y = 0x4a00;
    setSignVerts(&v0, &v1, &v2, &v3);
    gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &cu);          /* @0x4137dc */

    /* checkout progress bar frame @0x4137e1 */
    nTmp = g_playerRecords[g_nLocalPlayerIdx].nCheckoutProgress; /* @0x413832 */
    memset(&cu, 0, sizeof(cu));
    cu.pTexture = g_hHudCharTpg;                             /* @0x41387a */
    cu.V = 0xc000;  cu.V2 = 0xc000;
    cu.gwU = 0xff00; cu.gwU2 = 0xff00;
    cu.hV = 0xd800;  cu.hV2 = 0xd800;
    memset(&v0, 0, sizeof(v0)); memset(&v1, 0, sizeof(v1));
    memset(&v2, 0, sizeof(v2)); memset(&v3, 0, sizeof(v3));
    v0.x = 0xd700;  v0.y = 0xa00;
    v1.x = 0x1d700; v1.y = 0xa00;
    v2.x = 0x1d700; v2.y = 0x2300;
    v3.x = 0xd700;  v3.y = 0x2300;
    setSignVerts(&v0, &v1, &v2, &v3);
    gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &cu);          /* @0x4138aa */

    if (nTmp != 0) {                                         /* @0x4138b2 */
        int nFill = (nTmp * 0x7f) / 100;                     /* @0x4138c1 */
        int nWide = (nTmp * 0xe9) / 100 + 0xe3;              /* @0x413913 */

        memset(&cu, 0, sizeof(cu));
        cu.pTexture = g_hHudCharTpg;                         /* @0x41397d */
        cu.V = 0xd800;  cu.V2 = 0xd800;
        cu.gwU = nFill * 0x100; cu.gwU2 = cu.gwU;
        cu.hV = 0xdd00;  cu.hV2 = 0xdd00;
        memset(&v0, 0, sizeof(v0)); memset(&v1, 0, sizeof(v1));
        memset(&v2, 0, sizeof(v2)); memset(&v3, 0, sizeof(v3));
        v0.x = 0xe300;       v0.y = 0x1300;
        v1.x = nWide * 0x100; v1.y = 0x1300;
        v2.x = nWide * 0x100; v2.y = 0x1a00;
        v3.x = 0xe300;       v3.y = 0x1a00;
        setSignVerts(&v0, &v1, &v2, &v3);
        gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &cu);      /* @0x4139ad */

        memset(&cu, 0, sizeof(cu));
        cu.pTexture = g_hHudCharTpg;                         /* @0x413a31 */
        cu.U = 0x8000;  cu.U2 = 0x8000;
        cu.V = 0x8000;  cu.V2 = 0x8000;
        cu.gwU = 0xbf00; cu.gwU2 = 0xbf00;
        cu.hV = 0xbf00;  cu.hV2 = 0xbf00;
        memset(&v0, 0, sizeof(v0)); memset(&v1, 0, sizeof(v1));
        memset(&v2, 0, sizeof(v2)); memset(&v3, 0, sizeof(v3));
        v0.x = 0x13700; v0.y = 0;
        v1.x = 0x17700; v1.y = 0;
        v2.x = 0x17700; v2.y = 0x4000;
        v3.x = 0x13700; v3.y = 0x4000;
        setSignVerts(&v0, &v1, &v2, &v3);
        gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &cu);      /* @0x413a61 */
    }

    /* rank counter (modes 2/3/4) @0x413a69 */
    nRank = 0;
    if (g_nGameMode == 2 || g_nGameMode == 3 || g_nGameMode == 4) {
        nRank = 1;
        if (g_nGameMode == 2) {                              /* @0x413b20 */
            nMyTaken = 0;
            for (i = 0; i < 10; i++) {
                if (g_playerRecords[g_nLocalPlayerIdx].abListTaken[i] != 0) nMyTaken++;
            }
            for (j = 0; j < g_nPlayerCount; j++) {
                if (j == g_nLocalPlayerIdx) continue;
                n = 0;
                for (i = 0; i < 10; i++) {
                    if (g_playerRecords[j].abListTaken[i] != 0) n++;
                }
                if (n > nMyTaken) nRank++;
            }
        } else if (g_nGameMode == 3) {                       /* @0x413ad2 */
            for (j = 0; j < g_nPlayerCount; j++) {
                if (j == g_nLocalPlayerIdx) continue;
                if (g_playerRecords[j].anHeldSlot[1] >
                    g_playerRecords[g_nLocalPlayerIdx].anHeldSlot[1]) nRank++;
            }
        } else {                                             /* mode 4 @0x413a81 */
            for (j = 0; j < g_nPlayerCount; j++) {
                if (j == g_nLocalPlayerIdx) continue;
                if (g_playerRecords[j].anListIds[0] >
                    g_playerRecords[g_nLocalPlayerIdx].anListIds[0]) nRank++;
            }
        }
        if (nRank != 0) {                                    /* @0x413b9d */
            textDrawInt(g_hHudFontDigits, 0x2004, 0x230, 0x28, nRank); /* @0x413bad */
            i = textIntWidth(g_hHudFontDigits, nRank) + 0x230; /* @0x413bd4 */
            textDraw(g_hHudFontDigits, 0x2004, i, 0x32, SZ_SLASH); /* @0x413be8 */
            i += textWidth(g_hHudFontDigits, SZ_SLASH);      /* @0x413bf9 */
            textDrawInt(g_hHudFontDigits, 0x2004, i, 0x3c, g_nPlayerCount); /* @0x413c15 */
        }
    }

    if (g_nGamePhase < 1) {                                  /* @0x413c80 */
        if (g_nGamePhase >= 0) return;                       /* @0x414185 */
        g_nGamePhase = g_nGamePhase + 1;                     /* @0x414197 */
        gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x32, 0x1bf, 0x65, 0, 0, 0xff, 0x32);
        textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x44, SZ_GA); /* @0x4141b9 */
        HUD_COUNTDOWN_ARROWS(0x8f00, 0xb800, 1);             /* @0x4141cc */
        if (g_nLastSfx != 3) {                               /* @0x414353 */
            sndPlaySfx(0, 1, 0x22, 0xffff, 0, 0x400);        /* @0x414373 */
        }
        g_nLastSfx = 3;                                      /* @0x413eed */
        return;
    }

    if (g_nGamePhase == 5) {                                 /* @0x413c8d */
        gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x32, 0x1bf, 0x65, 0, 0, 0xff, 0x32);
        textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x44, g_acScratchText); /* @0x413cba */
        g_nLastSfx = 0;                                      /* @0x413cd6 */
        g_nPhaseStartTime = g_nGameTime;                     /* @0x413cdc */
        return;
    }

    if (g_nGameTime < g_nPhaseStartTime + 0x2ee) {           /* @0x413cea */
        gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x32, 0x1bf, 0x65, 0, 0, 0xff, 0x32);
        textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x44, SZ_KLARA); /* @0x413d2b */
        HUD_COUNTDOWN_ARROWS(0x6500, 0x8e00, 0);             /* @0x413d3e */
        if (g_nLastSfx != 1) {                               /* @0x413ec5 */
            sndPlaySfx(0, 1, 0x21, 0xffff, 0, 0x400);        /* @0x413ed6 */
            g_nLastSfx = 1;                                  /* @0x413eed */
        }
        return;
    }
    if (g_nGameTime < g_nPhaseStartTime + 0x8ca) {           /* @0x413efb */
        gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x32, 0x1bf, 0x65, 0, 0, 0xff, 0x32);
        textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x44, g_acScratchText); /* @0x413f2d */
        return;
    }
    if (g_nPhaseStartTime + 0xabe <= g_nGameTime) {          /* @0x413f4b */
        gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x32, 0x1bf, 0x65, 0, 0, 0xff, 0x32);
        textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x44, g_acScratchText); /* @0x414167 */
        return;
    }
    gxDrawQuadColor(g_hHudGfx2Tpg, 0xc0, 0x32, 0x1bf, 0x65, 0, 0, 0xff, 0x32);
    textDrawCentered(g_hHudFontTiny, 0x2004, 0, 0x44, SZ_FARDIGA); /* @0x413f80 */
    HUD_COUNTDOWN_ARROWS(0x6500, 0x8e00, 0);                 /* @0x413f93 */
    if (g_nLastSfx != 2) {                                   /* @0x41411a */
        sndPlaySfx(0, 1, 0x21, 0xffff, 0, 0x400);            /* @0x41412f */
        g_nLastSfx = 2;                                      /* @0x413eed */
    }
    return;
}
