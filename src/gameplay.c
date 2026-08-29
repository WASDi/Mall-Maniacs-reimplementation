#include <stdlib.h>

#include "gameplay.h"
#include "config.h"
#include "gx.h"
#include "hud.h"
#include "input.h"
#include "level.h"
#include "levelselect.h"
#include "menu.h"
#include "obj.h"
#include "player.h"
#include "pool.h"
#include "quest.h"
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
int g_nGamePhase;        /* @0x458124 countdown phase (HUD; -1 = "Gå!!") */
int g_nWinnerIdx;        /* @0x458134 results-screen winner slot */
unsigned char g_bGameRunning; /* @0x44fdc4 round live flag (HUD/results gate) */
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
 * The zone-connection objects, HUD fonts/graphics, the per-object item slot
 * pass and the questLoad startup block are live below; the per-player
 * animation kick, walk-anim table and snd/music/camera-director startup
 * remain documented TODO boundaries. */
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
    /* Original order after the zone-connection pass (@0x40a727..0x40a8bd):
     * fontPoolCreate, the three HUD font loads (per-level descriptor +
     * texture pairs), hudLoadGraphics and the per-object item slot loop
     * (implemented above). The player round setup
     * (playerSetupRound @0x410e90 + playerSetupSceneObjects @0x411550) is
     * live below. */
    fontPoolCreate();                                      /* @0x408f90 @0x40a727 */
    {
        char szPath[124];

        fmtSprintf(szPath, "%s\\hud\\font00.tpg",           /* @0x44f2c8 @0x40a744 */
                   g_aszLevelDirs[g_nLevelIdx]);
        g_hHudFont = fontLoad("scene_ica\\hud\\font.txt",   /* @0x44f2b0 @0x40a765 */
                              (void *)gxLoadTpgFile(szPath), 0, 0, 0);
        fmtSprintf(szPath, "%s\\hud\\hfont00.tpg",          /* @0x44f29c @0x40a786 */
                   g_aszLevelDirs[g_nLevelIdx]);
        g_hHudFontDigits = fontLoad("scene_ica\\hud\\hudfont.txt", /* @0x44f280 @0x40a7a7 */
                                    (void *)gxLoadTpgFile(szPath), 0, 0, 0);
        fmtSprintf(szPath, "%s\\hud\\tfont00.tpg",          /* @0x44f26c @0x40a7c9 */
                   g_aszLevelDirs[g_nLevelIdx]);
        g_hHudFontTiny = fontLoad("menu\\tinyfont.txt",     /* @0x44f258 @0x40a7ea */
                                  (void *)gxLoadTpgFile(szPath), 0, 0, 0);
    }
    hudLoadGraphics();                                     /* @0x412700 @0x40a7f7 */
    /* Per-object item slot pass (@0x40a7fc..0x40a8bd): for slot ids 1..30
     * find the mall EventObject with that id (mode 4 uses ids 200+id for
     * the CHECKFLAG items), store it in slot[id-1].pEventObj and move the
     * slot's scenery node to the event origin. x = originZ, y = heightA,
     * z = originX per the disassembly; the non-mode-4 branch drops y by
     * 1000 (event origins sit 1000 above the floor). */
    {
        int i;
        for (i = 1; i <= LEVEL_ITEM_SLOT_COUNT; i++) {
            LevelItemSlot *pSlot = &g_apLevelItemSlots[i - 1];
            EventObject *pEvent;

            if (g_nGameMode == 4) {
                pEvent = objFindById(i + 200, 0);          /* @0x40a867 */
                pSlot->pEventObj = pEvent;                 /* @0x40a8a7 */
                if (pEvent != NULL) {
                    sceneObjSetPos((SceneNode *)pSlot->pSubObj,  /* @0x430660 @0x40a89f */
                                   (int)pEvent->flOriginZ,
                                   (int)pEvent->flHeightA,
                                   (int)pEvent->flOriginX, 2);
                }
            } else {
                pEvent = objFindById(i, 0);                /* @0x40a810 */
                pSlot->pEventObj = pEvent;                 /* @0x40a84f */
                if (pEvent != NULL) {
                    sceneObjSetPos((SceneNode *)pSlot->pSubObj,  /* @0x40a847 */
                                   (int)pEvent->flOriginZ - 1000, /* @0x40a837 */
                                   (int)pEvent->flHeightA,
                                   (int)pEvent->flOriginX, 2);
                }
            }
        }
    }
    playerSetupRound();                                    /* @0x410e90 @0x40a74c */
    playerSetupSceneObjects();                             /* @0x411550 @0x40a751 */
    levelObjectsCartsCameraInit();                         /* @0x411b70 @0x40a756 */
    /* "__HIDE_ME__" hide pass (original @0x40a764): every scene node whose
     * object name contains "__HIDE_ME__" ("__HIDE_ME__" @0x44f23c) gets
     * bType=2 via sceneNodeSetHiddenFlag 3 so sceneNodeRender skips it. */
    {
        SceneNode *apHideNodes[0x400];
        int n = sceneFindByName(apHideNodes, 0x400, "__HIDE_ME__");  /* @0x431fd0 @0x40a770 */
        int i;
        for (i = 0; i < n; i++) {
            sceneNodeSetHiddenFlag(apHideNodes[i], 3);               /* @0x4305c0 @0x40a77f */
        }
    }
    /* TODO: the per-player sceneObjectAnimStep(+0x2c4) kick,
     * walkAnimTableEntryCalc, snd/music startup, levelDirectorInits,
     * mciPlayCdaudio and winmmInitTimerRes; the cameraFollowUpdate call
     * lands at the end (original @0x40a9ce). questLoad runs here in the
     * original (after walkAnimTableEntryCalc, before sndInitSystem). */
    questLoad("quest.txt");                                /* @0x40ffa0 @0x40a9a6 */
    cameraFollowUpdate(&g_camFollowBlock);                 /* @0x4020d0 @0x40a9ce */
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
    /* TODO: playerCheckBlocked item pickup, playerUpdateAI,
     * playerAnimSfxUpdate, and gameUpdate. */
    if (g_nResultsScreen == 0 &&
        ((g_nCameraUpdateTick & 3) == 0 ||
         g_playerRecords[g_nLocalPlayerIdx].nAnimationFrame != 0)) {
        cameraFollowUpdate(&g_camFollowBlock); /* @0x4020d0 @0x40b503 */
    }
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