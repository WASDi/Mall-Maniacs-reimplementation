#include <stdlib.h>

#include "gameplay.h"
#include "config.h"
#include "gx.h"
#include "input.h"
#include "level.h"
#include "levelselect.h"
#include "menu.h"
#include "obj.h"
#include "player.h"
#include "pool.h"
#include "scene.h"
#include "scenetext.h"
#include "sen.h"
#include "sound.h"
#include "stubs.h"
#include "time.h"
#include "util.h"
#include "zone.h"
#include "custom_helpers.h"

extern HWND g_hWnd;                  /* maniac g_hMainWindow @0x459ce0 (maniac.c) */
extern HINSTANCE g_hAppInstance;     /* maniac g_hAppInstance @0x459cdc (maniac.c) */

/* g_bGameActive @0x4580f8 — selects gameRunFrame in WinMain. */
int g_bGameActive;
int g_nReturnToMenu;     /* @0x45810c */
int g_nScrollText;       /* @0x4580ec */
int g_nGameFrameActive;  /* @0x45834c */
int g_nGameTime;         /* @0x4580d4 */
int g_nObjUpdateTime = 25; /* @0x4580d0 */
int g_nRoundStartTime;   /* @0x4580cc */
int g_bQuitPrompt;       /* @0x45812c */
int g_nWorldFrameTick;   /* @0x458944 */
int g_nClearColor;       /* @0x45892c */
void *g_pSceneDetailGrid;/* @0x45838c */
void *g_pGameVoiceList;  /* @0x45f0e0 */
int g_bCollisionEnabled; /* @0x458354 */
int g_nMovieRecord;      /* @0x455e8c */
int g_nMoviePlay;        /* @0x455e90 */

/* roundStartInit @0x40a4d0 — round-start / level setup, called from runCmd
 * @0x4084c0. Two scene-system cycles: the first (scenNameTableInit
 * 10000/10000) hosts the levelSceneTexturesLoad @0x4104b0 world pre-pass
 * (level textures + ph scene + FLOOR nav + zone walls) and is torn down
 * behind a "%s\hud\load.tga" presentFrame flash; the second
 * (scenNameTableInit 4000/4000) owns configMasterLoad + levelSetup and the
 * live game world. This increment implements both cycles through levelSetup.
 * The zone-connection objects, HUD fonts/graphics, per-object item slots,
 * player/quest/sound/music/camera startup blocks are still documented
 * TODO boundaries after levelSetup. */
void roundStartInit(void) /* @0x40a4d0 */
{
    GxMode mode;
    unsigned short *pImg;
    char szPath[256];

    gxSnooze();                                   /* @0x40a4f1 */
    mode.width = 0x280;                           /* @0x40a505: 640x480x16 */
    mode.height = 0x1e0;
    mode.bpp = 0x10;
    mode.hInstance = (unsigned int)(size_t)g_hAppInstance;  /* [0x459cdc] @0x40a4f6 */
    mode.hwnd = (unsigned int)(size_t)g_hWnd;     /* [0x459cd0] win-handle twin of g_hWnd @0x459ce0 @0x40a4fb */
    gxInit(&mode);                                /* @0x40a521 */
    nopDebugStub();                               /* "Memory init" @0x44f3b4, ch 1 @0x40a52d */
    memPoolSystemInit();                          /* @0x40a532 */
    gxResetState();                               /* @0x40a537 */
    sceneSystemInit(100000, 40000, 40000, 2000, 2);   /* @0x40a552 */
    scenNameTableInit(10000, 10000);              /* @0x40a561 */
    sceneTextAnimReset();                         /* @0x40a566 */
    levelSceneTexturesLoad();                     /* @0x40a56b */
    sceneTextAnimClose();                         /* @0x40a570 */
    scenNameTableFree();                          /* @0x40a575 */
    sceneSystemClose();                           /* @0x40a57a */
    /* Load screen: "<level dir>\hud\load.tga" (dir table @0x44f0d8). */
    fmtSprintf(szPath, "%s\\hud\\load.tga", g_aszLevelDirs[g_nLevelIdx]);  /* @0x44f3a4 @0x40a599 */
    pImg = imageLoadByMode(szPath);               /* @0x40a5a6 */
    if (pImg != NULL) {
        presentFrame((int)pImg);                  /* @0x40a5b7 */
        memPoolFree(0, pImg);                     /* @0x40a5be */
    }
    nopDebugStub();                               /* "Spitfire init" @0x44f394, ch 1 @0x40a5cf */
    sceneSystemInit(100000, 40000, 40000, 2000, 2);   /* @0x40a5ea */
    nopDebugStub();                               /* "Scenery init" @0x44f384, ch 1 @0x40a5f6 */
    scenNameTableInit(4000, 4000);                /* @0x40a605 */
    sceneTextAnimReset();                         /* @0x40a60a */
    g_nGameFrameActive = 0;                       /* @0x45834c @0x40a616 */
    g_bCollisionEnabled = 0;                      /* @0x458354 @0x40a61c */
    g_nMovieRecord = 0;                           /* @0x455e8c @0x40a622 */
    g_nMoviePlay = 0;                             /* @0x455e90 @0x40a628 */
    g_bQuitPrompt = 0;                            /* @0x45812c @0x40a62e */
    nopDebugStub();                               /* 56 '-' separator @0x44f348, ch 1 @0x40a635 */
    configMasterLoad();                           /* @0x40a63a */
    levelSetup();                                 /* @0x40a63f */
    nopDebugStub();                               /* "Visual areas init:" @0x44f334, ch 0 @0x40a64a */
    /* AR/IN zone-connection objects: for i in 0..98, look up the
     * "AR%02.2d" EventObject by its 4-byte name tag (the formatted text's
     * first dword is the integer id) and pair it with "IN%02.2d". Both
     * lookup results feed zoneConnCtor @0x42b410 (the IN zone may be
     * NULL — the original still constructs the node). */
    {
        char szName[8];
        int i;
        for (i = 0; i < 99; i++) {                /* @0x40a65f */
            EventObject *pAr;
            EventObject *pIn;
            ZoneConn *pConn;
            fmtSprintf(szName, "AR%02.2d", i);    /* @0x44f328 @0x40a668 */
            pAr = objFindById(*(int *)szName, 0); /* @0x414a90 @0x40a67f */
            if (pAr == NULL) continue;
            fmtSprintf(szName, "IN%02.2d", i);    /* @0x44f31c @0x40a691 */
            pIn = objFindById(*(int *)szName, 0); /* @0x40a6a2 */
            nopDebugStub();                       /* @0x40a6ab / @0x40a6b6 */
            pConn = (ZoneConn *)malloc(0x1c);     /* operator_new @0x43dd42 @0x40a6b2 */
            if (pConn != NULL) {
                zoneConnCtor(pConn, pIn, pAr, g_pSceneDetailGrid); /* @0x42b410 @0x40a6c8 */
            }
        }
    }
    g_nRoundStartTime = getGameTime();
    g_nGameTime = getGameTime();
}

/* runCmd @0x4084c0 — parse "run <level>", initialize its round, then select
 * the game frame driver. */
int runCmd(int nContext, LPCSTR pszArgs)
{
    char *end;
    long level;

    (void)nContext;
    if (g_bGameActive != 0 || pszArgs == NULL) return 0;

    level = strtol(pszArgs, &end, 10);
    if (end == pszArgs || *end != '\0') return 0;

    g_nLevelIdx = (int)level;
    roundStartInit();
    appLog("[gameplay] round %d initialized", g_nLevelIdx);
    g_nReturnToMenu = 1;
    g_bGameActive = 1;
    g_nScrollText = 0;
    return 0;
}

/* gameWorldUpdate @0x40b3d0 — game-frame scheduler. The original polls input,
 * runs player/movie/round work, rechecks the active round, then advances this
 * tick before its player/item stage. Text-animation records are stepped before
 * the remaining player, item, animation, game, and camera TODO boundaries. */
void gameWorldUpdate(void)
{
    pollKeyboard(dispatchKeyEvent, (int)g_nLastFrameTime);
    if (g_bGameActive == 0 || g_bQuitPrompt != 0) return;

    playerUpdateDispatch();
    movieFrameUpdate();
    roundLogicUpdate();
    if (g_bGameActive == 0) return;

    netGameUpdate();
    g_nWorldFrameTick++;
    sceneTextAnimUpdate(1);
    /* TODO: playerCheckBlocked, playerUpdateAI, playerAnimSfxUpdate,
     * gameUpdate, and camera. */
}

/* gameObjectUpdate @0x40cf40 — alternating object update dependency. */
void gameObjectUpdate(void)
{
    /* TODO: update non-player scene objects. */
}

/* gameFrameRender @0x40ae30 — cull the level, render its camera root, then
 * render HUD and present. The world loader has not populated its grid or HUD
 * yet, but the existing scene camera can now render through this hierarchy. */
void gameFrameRender(void)
{
    if (g_nGameFrameActive == 0) {
        zoneConnUpdateCulling();
        sceneDetailGridUpdate(g_pSceneDetailGrid);
        sndStopAllVoices(g_pGameVoiceList);
        if (g_pSceneRoot != NULL) sceneRender(g_pSceneRoot);
        renderGameHud();
        gxFlip();
        gxClearScreen(1, g_nClearColor);
    }
}

/* gameRunFrame @0x40ad80 — fixed-step game loop entered by WinMain while a
 * round is active. */
void gameRunFrame(int forceRender)
{
    unsigned int updateCount = 0;
    int now = getGameTime();

    if (now >= g_nObjUpdateTime + g_nGameTime) {
        do {
            if (g_bGameActive != 0) {
                gameWorldUpdate();
                if (g_bGameActive != 0 && (updateCount & 1) == 0)
                    gameObjectUpdate();
            }
            g_nGameTime += g_nObjUpdateTime;
            sndMixTick(0);
            updateCount++;
            if (updateCount > 0x19) {
                g_nGameTime = getGameTime();
                break;
            }
            now = getGameTime();
        } while (g_nObjUpdateTime + g_nGameTime <= now);
    } else if (forceRender == 0) {
        return;
    }

    if (g_bGameActive != 0) gameFrameRender();
    sndEmitterUpdateAll();
}