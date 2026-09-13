#include <stdlib.h>
#include <math.h>

#include "gameplay.h"
#include "anim.h"
#include "config.h"
#include "font.h"
#include "gx.h"
#include "hud.h"
#include "input.h"
#include "level.h"
#include "level0.h"
#include "level1.h"
#include "level2.h"
#include "level3.h"
#include "level4.h"
#include "levelselect.h"
#include "menu.h"
#include "nav.h"
#include "obj.h"
#include "player.h"
#include "pool.h"
#include "quest.h"
#include "scene.h"
#include "scene_render.h"
#include "scene_text.h"
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
int g_nGameFrameActive2; /* @0x458350 */
int g_nGameTime;         /* @0x4580d4 */
int g_nObjUpdateTime = 25; /* @0x4580d0 */
int g_nRoundStartTime;   /* @0x4580cc */
int g_nRoundTimeLimit;   /* @0x4588f0 */
int g_nRoundElapsedTicks;/* @0x455e94 */
int g_anRoundPhaseIds[8];/* @0x458390 */
int g_nRoundSpareFlag;   /* @0x4583b4 */
int g_nSpawnTimer;       /* @0x458950 */
int g_nMusicModuleHandle;/* @0x4580bc */
int g_bQuitPrompt;       /* @0x45812c */
int g_nGamePhase;        /* @0x458124 */
int g_nWinnerIdx;        /* @0x458134 results-screen winner slot */
unsigned char g_bGameRunning; /* @0x44fdc4 round live flag (HUD/results gate) */
/* "action get item" @0x44e1f4 — commandDispatch payload for the local
 * player's blocked-target pickup (gameWorldUpdate tail). */
#define SZ_ACTION_GET_ITEM "action get item"            /* @0x44e1f4 */

int g_nWorldFrameTick;   /* @0x458944 */
int g_nClearColor;       /* @0x45892c */
void *g_pSceneDetailGrid;/* @0x45838c */
void *g_pGameVoiceList;  /* @0x45f0e0 */
int g_bCollisionEnabled; /* @0x458354 */
int g_nGameUpdateTick;   /* @0x45e5dc */
int g_nMovieRecord;      /* @0x455e8c */
int g_nMoviePlay;        /* @0x455e90 */

/* g_flGameObjSpeed @0x45895c — cart arrow anim phase (gameObjectUpdate). */
float g_flGameObjSpeed;  /* @0x45895c */
float g_flGameObjSpeed2; /* @0x458960 goods arrow phase */
float g_flGameObjSpeed3; /* @0x458964 item-slot bob phase */

/* roundStartInit @0x40a4d0 — round-start / level setup, called from runCmd
 * @0x4084c0. Two scene-system cycles: the first (scenNameTableInit
 * 10000/10000) hosts the levelSceneTexturesLoad @0x4104b0 world pre-pass
 * (level textures + ph scene + FLOOR nav + zone walls) and is torn down
 * behind a "%s\hud\load.tga" presentFrame flash; the second
 * (scenNameTableInit 4000/4000) owns configMasterLoad + levelSetup and the
 * live game world. This increment implements both cycles through levelSetup.
 * The zone-connection objects, HUD fonts/graphics, the per-object item slot
 * pass, the player animation kick, walk-anim table, questLoad and the
 * snd/music/director/camera startup blocks are all live below. */
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
     * z = originX per the disassembly; the non-mode-4 branch raises the
     * vertical position by 1000. */
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
                                   (int)pEvent->flOriginZ,
                                   (int)pEvent->flHeightA - 1000, /* @0x40a837 */
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
    /* Per-player animation kick (original @0x40a8bd..0x40a93f): step each
     * player's "stand" anim set (+0x2c4 = apAnmSets[7]) once, bLoop=1. */
    {
        int i;
        for (i = 0; i < g_nPlayerCount; i++) {
            sceneObjectAnimStep((SceneObjAnimList *)g_playerRecords[i].apAnmSets[7],
                                1);                                 /* @0x434540 @0x40a929 */
        }
    }
    /* Walk-anim lookup table fill (original @0x40a941..0x40a981): 128
     * entries, entry i for normalized speed i * 0.0078740157f (const
     * @0x44b458, bytes 15 62 01 3C = 1/127) on the circle center 0xd2 /
     * radius 0x118. */
    {
        static const float g_flNormSpeedStep = 0.0078740157f; /* @0x44b458 */
        int i;
        for (i = 0; i < 128; i++) {
            walkAnimTableEntryCalc(g_awWalkAnimTable[i], (float)i * g_flNormSpeedStep,
                                   0xd2, 0x118);                    /* @0x433980 @0x40a967 */
        }
    }
    questLoad("quest.txt");                                /* @0x40ffa0 @0x40a988 */
    sndInitSystem(2, 4, 10);                               /* @0x437a30 @0x40a993 */
    g_nMusicModuleHandle =
        (int)(size_t)musicModuleInit(g_pMusicSlotAlloc);   /* @0x437b10 @0x40a9a1 */
    sndLoadBankFromDir(1, "sound\\");                      /* @0x437170 @0x40a9b2 */
    levelDirectorInits();                                  /* @0x40bdf0 @0x40a9b7 */
    mciPlayCdaudio(g_hWnd, g_bMusicTrack);                 /* @0x416cc0 @0x40a9c9 */
    cameraFollowUpdate(&g_camFollowBlock);                 /* @0x4020d0 @0x40a9d3 */
    winmmInitTimerRes();                                   /* @0x40dfd0 @0x40a9db */
    g_nRoundStartTime = getGameTime();                     /* @0x40dfe0 @0x40a9e5 */
    g_nGameTime = getGameTime();                           /* @0x40a9ea.. */
}

/* levelDirectorInits @0x40bdf0 — round-init dispatcher called from
 * roundStartInit before the CD track. Resets the round clock
 * (g_nRoundElapsedTicks @0x455e94 = 0, g_nGamePhase @0x458124 = 0,
 * g_nResultsScreen @0x458130 = 0, g_nRoundSpareFlag @0x4583b4 = 0,
 * g_nRoundTimeLimit @0x4588f0 = 7000 ms), resolves the eight spin objects
 * GRIND2..GRIND16 (wsprintf "GRIND%d" @0x44f4bc, scenNameToIdEx
 * @0x431e20, stride 2) into g_anRoundPhaseIds @0x458390 — these are the
 * name-table node ids the round-end blink pass in roundLogicUpdate
 * rotates with sceneObjSetPosOrient mode 5 — warms netIsActive
 * @0x426ed0, then dispatches to the per-level director reset
 * levelEventDirector_L<n>_Init on g_nLevelIdx @0x458100. */
void levelDirectorInits(void) /* @0x40bdf0 */
{
    char szName[12];
    int i;
    int j;

    g_nRoundElapsedTicks = 0;                              /* @0x40bdff */
    for (j = 0, i = 2; i < 0x12; i += 2, j++) {            /* @0x40be0a..0x40be34 */
        wsprintfA(szName, "GRIND%d", i);                   /* "GRIND%d" @0x44f4bc */
        g_anRoundPhaseIds[j] = scenNameToIdEx(szName);     /* @0x431e20 @0x40be21 */
    }
    netIsActive();                                         /* @0x40be36 */
    g_nGamePhase = 0;                                      /* @0x40be41 */
    g_nResultsScreen = 0;                                  /* @0x40be47 */
    g_nRoundSpareFlag = 0;                                 /* @0x40be4d */
    g_nRoundTimeLimit = 7000;                              /* @0x40be58 */
    switch (g_nLevelIdx) {                                 /* @0x40be55..0x40be63 */
    case 0: levelEventDirector_L0_Init(); break;           /* @0x416db0 @0x40be6c */
    case 1: levelEventDirector_L1_Init(); break;           /* @0x4175e0 @0x40be75 */
    case 2: levelEventDirector_L2_Init(); break;           /* @0x417dc0 @0x40be7e */
    case 3: levelEventDirector_L3_Init(); break;           /* @0x4186c0 @0x40be87 */
    case 4: levelEventDirector_L4_Init(); break;           /* @0x418f30 @0x40be90 */
    }
}

/* kGoalObjNameId — the checkout-zone object id: the original loads the
  * first dword of the "goal" string @0x44f4e4 (0x6C616F67) and passes it
 * to objFindById @0x414a90 from all four win checks. */
static const int kGoalObjNameId = 0x6C616F67; /* *(int *)"goal" @0x44f4e4 */

/* ROUND_WIN_TAIL — the shared win tail inlined by the original at all
 * four mode win sites (0x40c450 for modes 1/2/3, 0x40c75b for mode 4):
 * debug print, attach a "KUNDKORT" (@0x44f4c4) scenery card to the
 * winner's char scene object (+0x30) via sceneryObjAlloc @0x430200, flag
 * the results screen (g_nResultsScreen @0x458130 = 1, g_nWinnerIdx
 * @0x458134 = winner), play win sfx 8 through sndPlaySfx @0x437cf0, then
 * clear the game-frame active pair @0x45834c/@0x458350 (if either is
 * already clear the original returns without clearing) and stop. The
 * per-mode net announcements (server sub 10, client sub 0x3f) happen at
 * the call sites, before this tail. nWinner is the ESI/EBP loop index
 * at the call site. */
#define ROUND_WIN_TAIL(nWinner)                                          \
    do {                                                                 \
        int nNameId = scenNameToId("KUNDKORT");      /* @0x431ed0 */     \
        SceneNode *pCardHost =                                             \
            g_playerRecords[(nWinner)].pCharSceneObj; /* +0x30 */          \
        nopDebugStub();                                /* @0x40c1dc */     \
        sceneryObjAlloc(pCardHost, 8, 0, 150, 0, 32000, 0, 0,              \
                        (void *)(size_t)nNameId);      /* @0x430200 */     \
        sndPlaySfx(0, 1, 8, 0xffff, 0, 0x400);         /* @0x437cf0 */     \
        g_nResultsScreen = 1;                          /* @0x40c465 */     \
        g_nWinnerIdx = (nWinner);                      /* @0x40c46f */     \
        if (g_nGameFrameActive2 == 0) break;           /* @0x40c47a */     \
        if (g_nGameFrameActive == 0) break;            /* @0x40c48a */     \
        g_nGameFrameActive = 0;                        /* @0x40c496 */     \
        g_nGameFrameActive2 = 0;                       /* @0x40c49c */     \
    } while (0)

/* ROUND_CHECK_WIN — per-player win condition inlined by the original at
 * the mode 1 (@0x40c123..0x40c1a1), mode 2 (@0x40c229..0x40c2a9) and
 * mode 3 (@0x40c323..0x40c39a) loops: with the results screen down, a
 * player whose anHeldSlot[1] (+0x180) reached the target has bStateFlags
 * |= 0x20, and when its +0x174 gate is set and the player's char node
 * pSubObjC (+0x2a4, position floats +0x20 x / +0x24 y) stands inside the
 * "goal" checkout zone, the ROUND_WIN_TAIL fires. nAnnounce bit0 makes
 * the server announce sub 10 (mode 2/3) and bit1 the client sub 0x3f
 * (mode 3); mode 1 announces nothing. Expands to "break out of the
 * round when won". */
#define ROUND_CHECK_WIN(nIdx, nTarget, nAnnounce)                        \
    do {                                                                 \
        PlayerRecord *pRec = &g_playerRecords[(nIdx)];                   \
        EventObject *pGoal;                                              \
        if (g_nResultsScreen != 0 ||                                     \
            pRec->anHeldSlot[1] != (nTarget)) {                          \
            break;        /* @0x40c137 @0x40c140 */                      \
        }                                                                \
        pRec->bStateFlags |= 0x20;     /* @0x40c146 */                   \
        if (pRec->nCartMode == 0) {    /* +0x174 @0x40c14d */            \
            break;                                                       \
        }                                                                \
        pGoal = objFindById(kGoalObjNameId, 0); /* @0x414a90 @0x40c15c */\
        if (pGoal == NULL) {                                             \
            break;                                                       \
        }                                                                \
        {   float *pfPos = (float *)pRec->pSubObjC; /* +0x2a4 @0x40c16a */\
            /* original truncates the node coords (roundFloat @0x43dd10 +  \
             * FILD @0x40c170..0x40c197) before the zone test. */          \
            if (objContainsPoint(pGoal, (float)(int)pfPos[8],              \
                                 (float)(int)pfPos[9])) {                 \
                if (((nAnnounce) & 1) && g_nNetIsServer != 0) {          \
                    netServerSendSubCmd(10, (nIdx), 0, 0, 0, 0, 0);      \
                }  /* @0x40c2b0 @0x40c3e2 */                             \
                if (((nAnnounce) & 2) && g_nNetIsClient != 0) {          \
                    netClientSendSubCmd(0x3f, (nIdx), 0, 0, 0, 0, 0);    \
                }  /* @0x40c3fd */                                       \
                ROUND_WIN_TAIL((nIdx));                                  \
                return;                                                  \
            }                                                            \
        }                                                                \
    } while (0)

/* roundLogicUpdate @0x40beb0 — per-frame round logic, called from
 * gameWorldUpdate @0x40b404 between movieFrameUpdate and netGameUpdate.
 * Structure (verified against the full disassembly):
 *   1. Net handshake (dead offline): the server counts peers whose
 *      nNetFlags (+0x310) announced 0x100 and, when all are ready,
 *      broadcasts sub 0xFF then sub 1; the client announces 0x100 once
 *      while nNetReady (+0x30c) is 0.
 *   2. Round clock: while g_nRoundElapsedTicks @0x455e94 stays below
 *      g_nRoundTimeLimit @0x4588f0 / g_nObjUpdateTime @0x4580d0, compute
 *      g_nGamePhase @0x458124 = (limit - objUpdate*ticks + 500)/1000
 *      (floor 1) and advance the tick counter, zero every player's
 *      animation state (+0x2e0/+0x2e4/+0x2e8), and once the last 250 ms
 *      begin (ticks > (limit-250)/objUpdate) spin the GRIND rails stored
 *      in g_anRoundPhaseIds @0x458390 with sceneObjSetPosOrient mode 5
 *      (pitch = objUpdate * +-56.603775f, alternating per index — consts
 *      @0x44b488/@0x44b48c). Past the limit g_nGamePhase = -20 once
 *      (0x40c0b0), which the HUD countdown counts back up through
 *      "Gå!!"/"Klara!".
 *   3. Score/overtime: with the results screen down and the clock
 *      expired, every player's nScoreTicks (+0x15c) increments once per
 *      frame (HUD timer keeps running); on the results screen the
 *      animation state is zeroed instead.
 *   4. Mode rules (switch g_nGameMode @0x458120):
 *      mode 1 Frögesporten / mode 2 Varujakten: list complete
 *      (anHeldSlot[1] +0x180 == 10) + checkout zone -> win; mode 2 skips
 *      the check on clients. mode 3 Matkrig: anHeldSlot[1] == 5 + zone;
 *      afterwards the server/offline item spawner keeps g_nSpawnTimer
 *      @0x458950 (forced to 8999 during the first 3 s of player-0
 *      ticks): at 10000 ms it rolls g_nCurrentItemId @0x458128 =
 *      rand()%24+1 (net sub 0x20) and deals it to every player's
 *      anListIds[0]/abListTaken[0] (+0x184/+0x1ac); an active item
 *      resets the timer. mode 4 Vagnrace: per player, when the +0x174
 *      stage gate is 0 the bStateFlags bit 2 gates the first rail, else
 *      the current rail id anListIds[0] (+0x184) resolves; touching a
  *      rail origin (+0x38/+0x3c) within 1500 of the char node position
  *      (pSubObjA +0x224 on the first rail, pSubObjC +0x2a4 afterwards;
  *      double 1500.0 @0x44b480, verified from the image bytes 2026-09-13)
  *      increments anListIds[0..2] and clears
 *      abListTaken[0..2] with sfx 0x16 for the local player; when the
 *      rail id no longer resolves, the checkout zone ends the round.
 *   5. Level directors: dispatch levelEventDirector_L0..L4 on
 *      g_nLevelIdx @0x458100. */
void roundLogicUpdate(void) /* @0x40beb0 */
{
    int i;
    int j;

    if (netIsActive() != 0) {                              /* @0x426ed0 @0x40beb5 */
        if (g_nNetIsServer != 0) {                         /* @0x45e598 @0x40bec4 */
            int nReady = 0;
            for (i = 0; i < g_nPlayerCount; i++) {         /* @0x40bedd */
                if ((g_playerRecords[i].nNetFlags & 0x100) == 0) {
                    break;                                 /* +0x310 @0x40bedd */
                }
                nReady++;
            }
            if (nReady == g_nPlayerCount) {                /* @0x40beef */
                netServerSendSubCmd(0xff, 0, 0, 0, 0, 0, 0);   /* @0x415d20 @0x40befe */
                netServerSendSubCmd(1, 0, 0, 0, 0, 0, 0);      /* @0x40bf0b */
                nopDebugStub();                            /* @0x40bf16 */
            }
        }
        if (g_playerRecords[g_nLocalPlayerIdx].nNetReady == 0) {   /* @0x40bf35 */
            nopDebugStub();                                /* @0x40bf44 */
            netClientSendSubCmd(0x100, g_nLocalPlayerIdx, 0, 0, 0, 0, 0);  /* @0x415cb0 @0x40bf5a */
            g_playerRecords[g_nLocalPlayerIdx].nNetReady = 1;      /* @0x40bf73 */
        }
    }

    if (g_nRoundElapsedTicks < g_nRoundTimeLimit / g_nObjUpdateTime) {     /* @0x40bf9b */
        if (netIsActive() == 0 ||                          /* @0x40bfa3 */
            g_playerRecords[g_nLocalPlayerIdx].nNetReady == 2) {   /* @0x40bfc9 */
            g_nGamePhase = (g_nRoundTimeLimit
                            - g_nObjUpdateTime * g_nRoundElapsedTicks
                            + 500) / 1000;                 /* @0x40bfd3..0x40bff7 */
            if (g_nGamePhase == 0) {                       /* @0x40bffd */
                g_nGamePhase = 1;                          /* @0x40bfff */
            }
            g_nRoundElapsedTicks++;                        /* @0x455e94 @0x40c009 */
        }
        for (i = 0; i < g_nPlayerCount; i++) {             /* anim-state zero @0x40c019 */
            g_playerRecords[i].flInputTurn = 0;        /* +0x2e0 */
            g_playerRecords[i].flInputAccel = 0;        /* +0x2e4 */
            g_playerRecords[i].nActionSubstate = 0;        /* +0x2e8 */
        }
        if (g_nRoundElapsedTicks >
            (g_nRoundTimeLimit - 250) / g_nObjUpdateTime) {/* @0x40c030..0x40c041 */
            for (j = 0; j < 8; j++) {                      /* @0x458390..0x4583b0 @0x40c04a */
                if (g_anRoundPhaseIds[j] != 0) {
                    /* pitch = ftol(objUpdate * (even: +56.603775f,
                     * odd: -56.603775f)) — consts @0x44b488/@0x44b48c. */
                    sceneObjSetPosOrient((SceneNode *)(size_t)g_anRoundPhaseIds[j],
                                         0,
                                         (short)(int)((float)g_nObjUpdateTime *
                                                      ((j & 1) == 0 ? 56.603775f
                                                                    : -56.603775f)),
                                         0, 5);            /* @0x4307d0 @0x40c080 */
                }
            }
        }
    } else if (g_nGamePhase > 0) {                         /* @0x40c0a8 */
        g_nGamePhase = -20;                                /* -0x14 @0x40c0b0 */
        /* Gates open = countdown expired: the per-frame input zeroing
         * above stops and controls go live (automation marker). */
        appLog("[gameplay] gates open (phase -20, controls live)");
    }

    if (g_nResultsScreen == 0) {                           /* @0x40c0ba */
        if (g_nRoundElapsedTicks >=
            g_nRoundTimeLimit / g_nObjUpdateTime &&        /* @0x40c0cd: CMP ECX,EAX @0x40c0cd — ECX is the raw tick count (IDIV only rewrites EAX/EDX), so the original compares unticked elapsed against limit/objUpdate, not elapsed/objUpdate */
            g_nPlayerCount > 0) {
            for (i = 0; i < g_nPlayerCount; i++) {         /* @0x40c0d5 */
                g_playerRecords[i].nScoreTicks++;          /* +0x15c @0x40c0dc */
            }
        }
    } else if (g_nPlayerCount > 0) {                       /* @0x40c0ef */
        for (i = 0; i < g_nPlayerCount; i++) {             /* @0x40c0fa */
            g_playerRecords[i].flInputTurn = 0;
            g_playerRecords[i].flInputAccel = 0;
            g_playerRecords[i].nActionSubstate = 0;
        }
    }

    switch (g_nGameMode) {                                 /* @0x458120 @0x40c10a */
    case 1:                                                /* Frögesporten @0x40c123 */
        for (i = 0; i < g_nPlayerCount; i++) {
            ROUND_CHECK_WIN(i, 10, 0);                     /* @0x40c123..0x40c1a1 */
        }
        break;
    case 2:                                                /* Varujakten @0x40c215 */
        if (g_nNetIsClient == 0) {                         /* @0x45e59c @0x40c215 */
            for (i = 0; i < g_nPlayerCount; i++) {
                ROUND_CHECK_WIN(i, 10, 1);                 /* @0x40c229..0x40c2a9 */
            }
        }
        break;
    case 3:                                                /* Matkrig @0x40c30b */
        for (i = 0; i < g_nPlayerCount; i++) {
            ROUND_CHECK_WIN(i, 5, 3);                      /* @0x40c323..0x40c39a */
        }
        if (g_nNetIsClient == 0) {                         /* spawner @0x40c39c */
            if (g_playerRecords[0].nScoreTicks * g_nObjUpdateTime < 3000) {
                g_nSpawnTimer = 8999;                      /* 0x2327 @0x40c3bc */
            }
            if (g_nCurrentItemId != 0) {                   /* @0x40c3c6 */
                g_nSpawnTimer = 0;                         /* @0x40c3d3 */
            } else if (g_nSpawnTimer >= 10000) {           /* 0x2710 @0x40c4a8 */
                g_nCurrentItemId = rand() % 24 + 1;        /* rand @0x43ea7c @0x40c4b4 */
                if (netIsActive() != 0) {
                    netServerSendSubCmd(0x20, 0, g_nCurrentItemId,
                                        0, 0, 0, 0);       /* @0x40c4e4 */
                }
                for (i = 0; i < g_nPlayerCount; i++) {     /* @0x40c4f6 */
                    g_playerRecords[i].anListIds[0] = g_nCurrentItemId;    /* +0x184 @0x40c501 */
                    g_playerRecords[i].abListTaken[0] = 0; /* +0x1ac @0x40c504 */
                }
            }
            g_nSpawnTimer += g_nObjUpdateTime;             /* @0x40c512 */
        }
        break;
    case 4:                                                /* Vagnrace @0x40c52a */
        for (i = 0; i < g_nPlayerCount; i++) {             /* @0x40c53e */
            PlayerRecord *pRec = &g_playerRecords[i];
            EventObject *pRail;
            float *pfPos;
            int k;

            if (g_nResultsScreen != 0) {
                break;                                     /* @0x40c543 */
            }
            if (pRec->nCartMode == 0) {                    /* first stage @0x40c54b */
                if ((pRec->bStateFlags & 4) == 0) {        /* +0x158 &4 @0x40c556 */
                    continue;
                }
                pRail = objFindById(pRec->anListIds[0], 0);    /* +0x184 @0x40c565 */
                if (pRail == NULL) {
                    continue;
                }
                pfPos = (float *)pRec->pSubObjA;           /* +0x224 @0x40c577 */
            } else {                                       /* @0x40c60a */
                pRail = objFindById(pRec->anListIds[0], 0);    /* @0x40c60f */
                if (pRail != NULL) {
                    pfPos = (float *)pRec->pSubObjC;       /* +0x2a4 @0x40c621 */
                } else {
                    /* rail list exhausted: the checkout zone ends the
                     * round (@0x40c6b1..0x40c6fe). */
                    EventObject *pGoal = objFindById(kGoalObjNameId, 0);
                    if (pGoal == NULL) {
                        continue;
                    }
                    pfPos = (float *)pRec->pSubObjC;
                    /* original truncates the node coords (roundFloat      */
                    /* @0x43dd10 + FILD @0x40c6cd..0x40c6f4). */
                    if (objContainsPoint(pGoal, (float)(int)pfPos[8],
                                         (float)(int)pfPos[9])) {
                        if (g_nNetIsServer != 0) {         /* @0x40c727 */
                            netServerSendSubCmd(10, i, 0, 0, 0, 0, 0);
                        }
                        if (g_nNetIsClient != 0) {         /* @0x40c743 */
                            netClientSendSubCmd(0x3f, i, 0, 0, 0, 0, 0);
                        }
                        nopDebugStub();                    /* @0x40c722 */
                        ROUND_WIN_TAIL(i);                 /* @0x40c75b..0x40c79a */
                        return;
                    }
                    continue;
                }
            }
            /* rail proximity: |origin - char node pos| <= 1500 on both
             * axes (double 1500.0 @0x44b480, verified from the image bytes
             * 2026-09-13; origin +0x38/+0x3c vs node +0x20/+0x24). */
            if (!((float)pRail->flOriginX - (int)pfPos[8] <= 1500.0 &&
                  (float)pRail->flOriginX - (int)pfPos[8] >= -1500.0 &&
                  (float)pRail->flOriginZ - (int)pfPos[9] <= 1500.0 &&
                  (float)pRail->flOriginZ - (int)pfPos[9] >= -1500.0)) {
                continue;                                  /* @0x40c59d @0x40c5c3 */
            }
            for (k = 0; k < 3; k++) {                      /* @0x40c5d0 */
                pRec->anListIds[k]++;                      /* +0x184.. @0x40c5da */
                pRec->abListTaken[k] = 0;                  /* +0x1ac.. @0x40c5dc */
                if (i == g_nLocalPlayerIdx) {
                    sndPlaySfx(0, 1, 0x16, 0xffff, 0, 0x400);  /* @0x437cf0 @0x40c5f7 */
                }
            }
        }
        break;
    default:
        break;
    }

    switch (g_nLevelIdx) {                                 /* @0x458100 @0x40c1b3 */
    case 0: levelEventDirector_L0(); break;                /* @0x416fd0 @0x40c1c8 */
    case 1: levelEventDirector_L1(); break;                /* @0x4178c0 @0x40c7a6 */
    case 2: levelEventDirector_L2(); break;                /* @0x418000 @0x40c7b1 */
    case 3: levelEventDirector_L3(); break;                /* @0x418a30 @0x40c7bc */
    case 4: levelEventDirector_L4(); break;                /* @0x4192a0 @0x40c7c7 */
    }
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

/* roundTeardown @0x40aa10 — round-end teardown, called from killCmd
 * @0x407870 and WinMain @0x4160a0. Verified 2026-08-31 against the full
 * disassembly 0x40aa10..0x40ad5e. Sequence: g_bGameActive gate (0 -> log +
 * return, @0x44f440), netExit @0x414f60, mciStopCdaudio @0x416c80, free
 * every SndEmitter (g_pSndEmitterHead @0x45e5f0, next +0x04) +
 * sndShutdown @0x437cb0, free every ZoneConn (g_pZoneConnHead @0x45e5e8,
 * toward-tail link +0x04) via zoneConnUnlink @0x42b890, release the HUD
 * flingbjorn texture (memPoolFree of g_nTexHudFlingbjorn @0x458348) +
 * fontPoolDestroy @0x408fc0, per-level director Cleanup dispatch (jump
 * table @0x40ad60 on g_nLevelIdx; L1..L4 remain stubs), free each player
 * record's three WorldNodes pSubObjA/B/C (+0x224/+0x264/+0x2a4) via
 * objDtor @0x402ab0, free the thrown-item list (g_pThrownItemHead
 * @0x45896c via thrownItemFree @0x40f8d0), the quest list (g_pQuestHead
 * @0x458978 via questRecordDtor @0x40ff50) and the scene detail grid
 * (g_pSceneDetailGrid @0x45838c via sceneDetailGridFree @0x42b190), free
 * the 11 per-record AnmSets (apAnmSets +0x2a8..+0x2d0) via anmSetFree
 * @0x434500, then sceneTextAnimClose @0x434bf0, scenNameTableFree
 * @0x431e00, sceneSystemClose @0x42f180, zoneWallListFree @0x42a150, the
 * NavPoint list (navPointListGetHead @0x425ad0 + navPointListFreeAll
 * @0x4257a0), objHashFreeAll @0x414950, winmmRestoreTimerRes @0x40e030,
 * memPoolSystemShutdown @0x419bb0, and finally clears the editor/nav
 * selection slots 0x45e480/0x45e498/0x45e49c. The inter-block nopDebugStub
 * logs (@0x44f42c..0x44f3c0) are no-ops. */
void roundTeardown(void) /* @0x40aa10 */
{
    SndEmitter *pEmitter;
    SndEmitter *pNextEmitter;
    ZoneConn *pConn;
    ZoneConn *pNextConn;
    ThrownItem *pItem;
    ThrownItem *pNextItem;
    QuestRecord *pRec;
    QuestRecord *pNextRec;
    NavPoint *pNavHead;
    int i;
    int j;

    if (g_bGameActive == 0) {                        /* @0x40aa18 */
        nopDebugStub();                              /* @0x44f440 @0x40aa1c */
        return;                                      /* @0x40aa2b */
    }
    g_bGameActive = 0;                               /* @0x4580f8 @0x40aa2e */
    netExit();                                       /* @0x414f60 @0x40aa34 */
    mciStopCdaudio();                                /* @0x416c80 @0x40aa39 */
    for (pEmitter = g_pSndEmitterHead; pEmitter != NULL; /* @0x40aa3e */
         pEmitter = pNextEmitter) {
        pNextEmitter = pEmitter->pPrev;              /* +0x04 @0x40aa48 */
        sndEmitterFree(pEmitter);                    /* @0x42bec0 @0x40aa51 */
        memFreeDirect(pEmitter);                     /* @0x43dd37 @0x40aa57 */
    }
    g_pSndEmitterHead = NULL;                        /* @0x45e5f0 @0x40aa65 */
    g_pSndEmitterTail = NULL;                        /* @0x45e5f4 @0x40aa6b */
    sndShutdown();                                   /* @0x437cb0 @0x40aa71 */
    for (pConn = (ZoneConn *)g_pZoneConnHead; pConn != NULL; /* @0x40aa76 */
         pConn = pNextConn) {
        pNextConn = pConn->pPrev;                    /* +0x04 @0x40aa80 */
        zoneConnUnlink(pConn);                       /* @0x42b890 @0x40aa89 */
        memFreeDirect(pConn);                        /* @0x43dd37 @0x40aa8f */
    }
    g_pZoneConnHead = NULL;                          /* @0x45e5e8 @0x40aaa2 */
    g_pZoneConnTail = NULL;                          /* @0x45e5ec @0x40aaaa */
    memPoolFree(0, (void *)(size_t)g_nTexHudFlingbjorn); /* @0x419a60 @0x40aab0 */
    fontPoolDestroy();                               /* @0x408fc0 @0x40aab8 */
    switch (g_nLevelIdx) {                           /* @0x458100 @0x40aabd, jump table @0x40ad60 */
    case 0: levelEventDirector_L0_Cleanup(); break;  /* @0x416f70 @0x40aace */
    case 1: levelEventDirector_L1_Cleanup(); break;  /* @0x417860 @0x40aad5 */
    case 2: levelEventDirector_L2_Cleanup(); break;  /* @0x417fa0 @0x40aadc */
    case 3: levelEventDirector_L3_Cleanup(); break;  /* @0x4189d0 @0x40aae3 */
    case 4: levelEventDirector_L4_Cleanup(); break;  /* @0x419240 @0x40aaea */
    }
    nopDebugStub();                                  /* @0x44f42c @0x40aaf6 */
    for (i = 0; i < 8; i++) {                        /* ESI 0x456474..0x458014 @0x40aafe */
        PlayerRecord *pRecPlayer = &g_playerRecords[i];
        if (pRecPlayer->pSubObjA != NULL) {          /* +0x224 @0x40ab03 */
            objDtor((WorldNode *)pRecPlayer->pSubObjA);       /* @0x402ab0 @0x40ab0c */
            memFreeDirect(pRecPlayer->pSubObjA);               /* @0x43dd37 @0x40ab12 */
        }
        pRecPlayer->pSubObjA = NULL;                 /* @0x40ab1a */
        if (pRecPlayer->pSubObjB != NULL) {          /* +0x264 @0x40ab1d */
            objDtor((WorldNode *)pRecPlayer->pSubObjB);       /* @0x40ab25 */
            memFreeDirect(pRecPlayer->pSubObjB);              /* @0x40ab2b */
        }
        pRecPlayer->pSubObjB = NULL;                 /* @0x40ab33 */
        if (pRecPlayer->pSubObjC != NULL) {          /* +0x2a4 @0x40ab35 */
            objDtor((WorldNode *)pRecPlayer->pSubObjC);       /* @0x40ab3e */
            memFreeDirect(pRecPlayer->pSubObjC);              /* @0x40ab44 */
        }
        pRecPlayer->pSubObjC = NULL;                 /* @0x40ab4c */
    }
    for (pItem = g_pThrownItemHead; pItem != NULL;   /* @0x40ab5d */
         pItem = pNextItem) {
        pNextItem = pItem->pNext;                    /* +0x04 @0x40ab67 */
        thrownItemFree(pItem);                       /* @0x40f8d0 @0x40ab70 */
        memFreeDirect(pItem);                        /* @0x43dd37 @0x40ab76 */
    }
    g_pThrownItemHead = NULL;                        /* @0x45896c @0x40ab8a */
    g_pThrownItemTail = NULL;                        /* @0x458970 @0x40ab92 */
    for (pRec = g_pQuestHead; pRec != NULL;          /* @0x40ab84 */
         pRec = pNextRec) {
        pNextRec = pRec->pNext;                      /* @0x40ab9a */
        questRecordDtor(pRec);                       /* @0x40ff50 @0x40aba3 */
        memFreeDirect(pRec);                         /* @0x43dd37 @0x40aba9 */
    }
    g_pQuestHead = NULL;                             /* @0x458978 @0x40abbd */
    g_pQuestTail = NULL;                             /* @0x45897c @0x40abc5 */
    if (g_pSceneDetailGrid != NULL) {                /* @0x45838c @0x40abcb */
        sceneDetailGridFree((SceneDetailGrid *)g_pSceneDetailGrid); /* @0x42b190 @0x40abcf */
        memFreeDirect(g_pSceneDetailGrid);           /* @0x43dd37 @0x40abd5 */
    }
    nopDebugStub();                                  /* @0x44f41c @0x40abdd */
    for (i = 0; i < 8; i++) {                        /* ESI 0x4564d4..0x458074 @0x40abec */
        for (j = 0; j < 11; j++) {                   /* apAnmSets +0x2a8..+0x2d0 @0x40abf1 */
            if (g_playerRecords[i].apAnmSets[j] != NULL) {
                anmSetFree(g_playerRecords[i].apAnmSets[j]);   /* @0x434500 @0x40abf9 */
            }
        }
    }
    sceneTextAnimClose();                            /* @0x434bf0 @0x40acb2 */
    nopDebugStub();                                  /* @0x44f40c @0x40acb7 */
    scenNameTableFree();                             /* @0x431e00 @0x40acc3 */
    nopDebugStub();                                  /* @0x44f3fc @0x40acc8 */
    sceneSystemClose();                              /* @0x42f180 @0x40acd4 */
    nopDebugStub();                                  /* @0x44f3ec @0x40acde */
    zoneWallListFree();                              /* @0x42a150 @0x40ace5 */
    nopDebugStub();                                  /* @0x44f3e4 @0x40acea */
    pNavHead = navPointListGetHead();                /* @0x425ad0 @0x40acf9 */
    if (pNavHead != NULL) {                          /* @0x40ad00 */
        navPointListFreeAll(pNavHead);               /* @0x4257a0 @0x40ad06 */
        memFreeDirect(pNavHead);                     /* @0x43dd37 @0x40ad0c */
    }
    nopDebugStub();                                  /* @0x44f3d8 @0x40ad14 */
    objHashFreeAll();                                /* @0x414950 @0x40ad1f */
    nopDebugStub();                                  /* @0x44f3cc @0x40ad24 */
    winmmRestoreTimerRes();                          /* @0x40e030 @0x40ad30 */
    nopDebugStub();                                  /* @0x44f3c0 @0x40ad35 */
    memPoolSystemShutdown();                         /* @0x419bb0 @0x40ad44 */
    g_pNavPointSel = NULL;                           /* @0x45e480 @0x40ad4a */
    g_pEditorHover = NULL;                           /* @0x45e498 @0x40ad50 */
    g_pEditorDrag = NULL;                            /* @0x45e49c @0x40ad56 */
}

/* killCmd @0x407870 — console "kill" (command table @0x44b384, name
 * "kill" @0x44e548, desc "Kill current game" @0x44e534). Ends the live
 * round: roundTeardown (gameplay.c) then g_bGameActive = 0 +
 * g_nReturnToMenu = 1. Dispatched by commandDispatch (stubs.c) for
 * gameKeyHandler's quit-confirm and results-screen tails. */
int killCmd(int nContext, LPCSTR pszArgs) /* @0x407870 */
{
    (void)nContext;
    (void)pszArgs;
    if (g_bGameActive == 0) {
        nopDebugStub();                            /* @0x40787b */
        return 0;
    }
    roundTeardown();                               /* @0x407884 */
    g_bGameActive = 0;                             /* @0x4580f8 @0x40788d */
    g_nReturnToMenu = 1;                           /* @0x407894 */
    return 0;
}

/* gameKeyHandler @0x40db80 — in-game key handler, dispatched every frame
 * while a round runs: gameWorldUpdate @0x40b3d0 passes this function
 * (pushed @0x40b3d0) to pollKeyboard @0x416820, which forwards the
 * debounced (key, 2) events (key ids 0 Right, 1 Left, 2 Up, 3 Down,
 * 4 Space, 6 Enter, 7 Escape). Key 0..3 clear bit 0 of bStateFlags (+0x158)
 * and set flInputTurn (+0x2e0) / flInputAccel (+0x2e4) to ±1.0 — the player
 * physics integrates these channels. Key 4 dispatches "action smart".
 * Enter/Escape run the results-screen continuation (request play_level /
 * request endscene via commandDispatch then "kill"; the requestCmd
 * @0x41a730 deferred-state machinery they feed is not rebuilt yet, so the
 * rebuild's commandDispatch leaves "request ..." unrecognized like any
 * unknown command). Escape outside the results screen toggles g_bQuitPrompt
 * (cleared again by WM_CHAR J/Y via killCmd, resumed by N/n).
 * nKeyType 0 (WM_CHAR): J/Y/j/y and N/n route through the jump table
 * @0x40df9c/@0x40df90 into the quit-prompt / quest-answer branches
 * (pQuestMessage +0x1dc set -> nQuestStage +0x1e0 = 2 or 1). The console
 * branch (g_nScrollText != 0 -> consoleHandleKey @0x4086e0) is unreachable
 * offline: the rebuild never opens the console overlay.
 * Field +0x2fc is the action-wait timer, decremented once per event before
 * any dispatch; type-2 events are gated on it reaching 0 and on
 * g_bGameActive != 0, and every dispatch sets g_nReturnToMenu = 1. */
void gameKeyHandler(int nKey, int nKeyType) /* @0x40db80 */
{
    PlayerRecord *pRec;
    char szCmd[64];
    int i = g_nLocalPlayerIdx;                     /* @0x458104 @0x40db80 */
    pRec = &g_playerRecords[i];                    /* i * 0x374 @0x40db8b */

    if (pRec->field_2fc > 0) {                     /* +0x2fc @0x45650c @0x40dba0 */
        pRec->field_2fc--;                         /* @0x40dba4 */
    }

    if (nKeyType == 0) {                           /* @0x40dbb6 */
        if (g_nScrollText != 0) {                  /* @0x4580ec @0x40de90 */
            consoleHandleKey(nKey);                /* @0x4086e0 @0x40dea2 */
            return;
        }
        switch (nKey) {                            /* table @0x40df9c -> @0x40df90 */
        case 'J':                                  /* @0x40ded4 */
        case 'Y':                                  /* idx15 @0x40dfab */
        case 'j':                                  /* idx32 @0x40dfbc */
        case 'y':                                  /* idx47 @0x40dfcb */
            if (g_bQuitPrompt != 0) {              /* @0x45812c @0x40dedc */
                commandDispatch(0, "kill");        /* "kill" @0x44e548 @0x40dede */
            }
            if (pRec->pQuestMessage != NULL) {     /* +0x1dc @0x4563ec @0x40df01 */
                pRec->nQuestStage = 2;             /* +0x1e0 @0x4563f0 @0x40df0b */
            }
            return;                                /* @0x40df67 */
        case 'N':                                  /* @0x40df1d */
        case 'n':                                  /* idx36 @0x40dfc0 */
            if (g_bQuitPrompt != 0) {              /* @0x40df25 */
                g_bQuitPrompt = 0;                 /* @0x40df2d */
                getGameTime();                     /* @0x40dfe0 @0x40df34 */
            }
            if (pRec->pQuestMessage != NULL) {     /* @0x40df53 */
                pRec->nQuestStage = 1;             /* @0x40df5d */
            }
            return;                                /* @0x40df67 */
        default:
            return;                                /* @0x40df67 */
        }
    }
    if (nKeyType != 2) {                           /* @0x40dbbf */
        nopDebugStub();                            /* "mm_key: unknown flag"
                                                    * @0x44f570 @0x40ddaf */
        return;
    }
    if (g_nScrollText != 0) {                      /* @0x40dbc5 */
        return;                                    /* @0x40df67 */
    }
    if (g_bGameActive == 0) {                      /* @0x4580f8 @0x40dbd2 */
        return;                                    /* @0x40df67 */
    }
    if (pRec->field_2fc >= 1) {                    /* @0x40dbdf */
        return;                                    /* @0x40df67 */
    }

    g_nReturnToMenu = 1;                           /* @0x45810c @0x40dbf4 */
    switch (nKey) {                                /* table @0x40df70 @0x40dc07 */
    case 0:                                        /* Right @0x40dc0e */
        pRec->bStateFlags &= 0xfeu;                /* +0x158 @0x456368 @0x40dc15 */
        pRec->flInputTurn = 1.0f;                  /* +0x2e0 @0x4564f0 @0x40dc1d */
        return;                                    /* @0x40dc27 */
    case 1:                                        /* Left @0x40dc2e */
        pRec->bStateFlags &= 0xfeu;                /* @0x40dc35 */
        pRec->flInputTurn = -1.0f;                 /* @0x40dc3d */
        return;                                    /* @0x40dc47 */
    case 2:                                        /* Up @0x40dc4e */
        pRec->bStateFlags &= 0xfeu;                /* @0x40dc55 */
        pRec->flInputAccel = 1.0f;                 /* +0x2e4 @0x4564f4 @0x40dc5d */
        return;                                    /* @0x40dc67 */
    case 3:                                        /* Down @0x40dc6e */
        pRec->bStateFlags &= 0xfeu;                /* @0x40dc75 */
        pRec->flInputAccel = -1.0f;                /* @0x40dc7d */
        return;                                    /* @0x40dc87 */
    case 4:                                        /* Space @0x40dc8e */
        commandDispatch((int)(size_t)pRec, "action smart");  /* @0x44f5b8 @0x40dc9a */
        return;                                    /* @0x40dca2 */
    case 6:                                        /* Enter @0x40dcaa */
        if (g_nResultsScreen == 0) {               /* @0x458130 @0x40dcaf */
            return;                                /* @0x40df67 */
        }
        if (g_nGameMode != 1 && g_nGameMode != 4) {/* @0x458120 @0x40dcb7 */
            if (netIsActive() == 0 &&              /* @0x426ed0 @0x40dcce */
                g_nWinnerIdx == g_nLocalPlayerIdx) {/* @0x458134 @0x40dcd7 */
                if (g_nLevelIdx == 4) {            /* @0x458100 @0x40dcec */
                    fmtSprintf(szCmd, "request endscene %d",    /* @0x44f5a4 */
                               g_playerRecords[g_nWinnerIdx].nCharIdx);  /* +0x150 @0x456360 @0x40dd01 */
                    commandDispatch(0, szCmd);     /* @0x40dd33 */
                }
                else {                             /* @0x40dd1b */
                    fmtSprintf(szCmd, "request play_level %d 0 0 0",  /* @0x44f588 */
                               g_nLevelIdx + 1);    /* @0x40dd1c */
                    commandDispatch(0, szCmd);     /* @0x40dd77 */
                }
                commandDispatch(0, "kill");        /* @0x40dd3b */
                return;                            /* @0x40dd51 */
            }
            if (netIsActive() == 0) {              /* @0x40dd52 */
                fmtSprintf(szCmd, "request play_level %d 0 0 0",
                           g_nLevelIdx + 1);       /* @0x40dd64 */
                commandDispatch(0, szCmd);         /* @0x40dd77 */
                commandDispatch(0, "kill");        /* @0x40dd7c */
            }
            netIsActive();                         /* @0x40dd93 */
            return;                                /* @0x40dd9f */
        }
        commandDispatch(0, "kill");                /* @0x40dda0 */
        nopDebugStub();                            /* @0x40ddaf */
        return;                                    /* @0x40ddc5 */
    case 7:                                        /* Escape @0x40ddc6 */
        if (g_nResultsScreen != 0) {               /* @0x40ddc6 */
            if (netIsActive() == 0 &&              /* @0x40de00 */
                g_nWinnerIdx == g_nLocalPlayerIdx) {
                if (g_nLevelIdx == 4) {            /* @0x40de18 */
                    fmtSprintf(szCmd, "request endscene %d",
                               g_playerRecords[g_nWinnerIdx].nCharIdx);
                    commandDispatch(0, szCmd);     /* @0x40de4b */
                }
                commandDispatch(0, "kill");        /* @0x40de53 */
            }
            commandDispatch(0, "kill");            /* @0x40de62 */
            return;                                /* @0x40de78 */
        }
        g_bQuitPrompt = (g_bQuitPrompt == 0);      /* @0x40ddcf */
        if (g_bQuitPrompt == 0) {                  /* @0x40dde1 */
            getGameTime();                         /* @0x40df34 */
        }
        return;                                    /* @0x40df67 */
    default:                                       /* key 5 / >7 @0x40de79 */
        nopDebugStub();                            /* "mm_key:DI: unknown key"
                                                    * @0x44f558 / flag @0x44f570 */
        return;
    }
}

/* gameWorldUpdate @0x40b3d0 — game-frame scheduler. The original polls input,
 * runs player/movie/round work, rechecks the active round, then advances this
 * tick before its player/item stage. Text-animation records are stepped before
 * the remaining player, item, animation, game, and camera TODO boundaries. */
void gameWorldUpdate(void)
{
    PlayerRecord *pLocalRec;
    /* The original pushes gameKeyHandler @0x40db80 into the IN-GAME poll
     * pollKeyboardGame @0x416820 (@0x40b3d0: PUSH 0x40db80 / CALL 0x416820,
     * no time argument): movement keys and Enter dispatch (key, 2) every
     * frame while held — no debounce — so holding a key keeps feeding the
     * flInputTurn/flInputAccel channels each world update. The debounced
     * menu poll pollKeyboard @0x416a10 stays on the gameFrameUpdate path
     * (dispatchKeyEvent -> g_pStateFunc). Ghidra types gameKeyHandler
     * void __cdecl(int,int) while DispatchKeyEventFn returns int;
     * pollKeyboardGame never reads the callback's return, so the pragma
     * just bridges the two signatures of the same original call site. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
    pollKeyboardGame((DispatchKeyEventFn)gameKeyHandler);
#pragma GCC diagnostic pop
    if (g_bGameActive == 0 || g_bQuitPrompt != 0) return;

    playerUpdateDispatch();
    movieFrameUpdate();
    roundLogicUpdate();
    if (g_bGameActive == 0) return;

    netGameUpdate();
    g_nWorldFrameTick++;
    /* Local-player item pickup on even world ticks while the AI phase is
     * idle (@0x40b434..0x40b4a1): a blocked target raises the "action get
     * item" command unless an AI-controlled local player is suppressed by
     * g_bAiEnabled (0x458358). */
    pLocalRec = &g_playerRecords[g_nLocalPlayerIdx];
    /* even-tick gate @0x40b440 */
    if (pLocalRec->nAiPhase == 0 && (g_nWorldFrameTick & 1) == 0) {
        if (playerCheckBlocked(pLocalRec) != 0 &&
            !(pLocalRec->nControlType == 2 && g_bAiEnabled != 0)) {
            commandDispatch((int)pLocalRec, SZ_ACTION_GET_ITEM);   /* @0x408b60 @0x40b49f */
        }
    }
    playerUpdateAI();                                   /* @0x40b510 @0x40b4a7 */
    playerAnimSfxUpdate();                              /* @0x40c800 @0x40b4ac */
    sceneTextAnimUpdate(1);
    gameUpdate();                                       /* @0x426ee0 @0x40b4bb */
    if (g_nResultsScreen == 0 &&
        ((g_nCameraUpdateTick & 3) == 0 ||
         g_playerRecords[g_nLocalPlayerIdx].flInputTurn != 0)) {
        cameraFollowUpdate(&g_camFollowBlock); /* @0x4020d0 @0x40b503 */
    }
}

/* gameObjectUpdate @0x40cf40 — per-frame targeting + pointer update for the
 * LOCAL player (g_nLocalPlayerIdx @0x458104, record = g_playerRecords +
 * idx*0x374), called from gameRunFrame (alternating with gameWorldUpdate).
 * Five stages verified against the full disassembly 0x40cf40..0x40db52:
 *  1) resolve player's current zone (objContainsPoint over ARE1/ARE2/ARE4 on
 *     level 4, else PUNK loop).
 *  2) pick target obj (jump table @0x40db54 on g_nGameMode-1: mode 3 vs 1/2/4).
 *  2b) retarget across zones (exit objects EXI1..4 or zone rect logic).
 *  3) cart arrow (VAGNPIL) position + bob anim via sceneNodeFacePos / sin.
 *     Goods arrow (VARUPIL) over the target item.
 *  4) 30 item-slot pass (hide/reset per mode, then bob visible slots).
 * The original inlines the zone-resolution block three times; the rebuild keeps
 * it inline to preserve the call hierarchy (see gameobjectupdate-notes.md). */
void gameObjectUpdate(void)
{
    /* 4-byte zone / goal ids (.rdata verified 2026-08-31) */
    static const int kIdGoal = 0x6C616F67; /* "goal" @0x44f4e4 */
    static const int kIdAre1 = 0x31455241; /* "ARE1" @0x44f550 */
    static const int kIdAre2 = 0x32455241; /* "ARE2" @0x44f548 */
    static const int kIdAre4 = 0x34455241; /* "ARE4" @0x44f540 */
    static const int kIdPunk = 0x4B4E5550; /* "PUNK" @0x44f538 */
    static const int kIdExi1 = 0x31495845; /* "EXI1" @0x44f530 */
    static const int kIdExi2 = 0x32495845; /* "EXI2" @0x44f520 */
    static const int kIdExi3 = 0x33495845; /* "EXI3" @0x44f518 */
    static const int kIdExi4 = 0x34495845; /* "EXI4" @0x44f528 */
    static const float kFlSpeedInc = 0.3f;
    static const double kDblCartPh1 = 0.123;
    static const double kDblCartPh2 = 1.37;
    static const double kDblWalkPh1 = 0.97;
    static const double kDblWalkPh2 = 0.21;
    static const double kDblGoodsCartPh1 = 0.14;
    static const double kDblGoodsCartPh2 = 0.3;
    static const double kDblGoodsWalkPh1 = 0.237;
    static const double kDblGoodsWalkPh2 = 0.42;
    static const double kDblSlotBob100 = 100.0;
    static const float kFlCartMul1 = 17.0f;
    static const float kFlCartMul2 = 16.0f;
    static const float kFlWalkMul1 = 11.0f;
    static const float kFlWalkMul2 = 14.0f;
    static const float kFlGoodsCartMul1 = 12.0f;
    static const float kFlGoodsCartMul2 = 17.0f;
    static const float kFlGoodsWalkMul1 = 14.0f;
    static const float kFlGoodsWalkMul2 = 10.0f;

    PlayerRecord *pRec = &g_playerRecords[g_nLocalPlayerIdx];
    float vPos[2];
    float flSelfZ;
    float flSelfX;
    EventObject *pPlayerZone = NULL;
    EventObject *pTarget = NULL;

    sceneObjGetPosXZ(pRec, vPos);
    flSelfZ = vPos[0];
    flSelfX = vPos[1];

    /* Stage 1 — resolve player's current zone (0x40cf6a..0x40d03f) */
    if (g_nLevelIdx == 4) {
        pPlayerZone = objFindById(kIdAre1, 0);
        if (pPlayerZone == NULL || objContainsPoint(pPlayerZone, flSelfZ, flSelfX) == 0) {
            pPlayerZone = objFindById(kIdAre2, 0);
            if (pPlayerZone == NULL || objContainsPoint(pPlayerZone, flSelfZ, flSelfX) == 0) {
                pPlayerZone = objFindById(kIdAre4, 0);
            }
        }
    } else {
        int occ = 0;
        pPlayerZone = objFindById(kIdPunk, occ);
        occ = 1;
        while (pPlayerZone != NULL && objContainsPoint(pPlayerZone, flSelfZ, flSelfX) == 0) {
            pPlayerZone = objFindById(kIdPunk, occ);
            occ++;
        }
    }

    /* Stage 2 — pick target obj (jump table @0x40db54, index = g_nGameMode-1) */
    switch (g_nGameMode) {
    case 3: {
        if (pRec->anHeldSlot[1] == 5) {
            pTarget = objFindById(kIdGoal, 0);
        } else if (g_nCurrentItemId != 0) {
            pTarget = objFindById(g_nCurrentItemId, 0);
        } else {
            pTarget = NULL;
        }
        break;
    }
    case 1:
    case 2:
    case 4: {
        if (pRec->anHeldSlot[1] == 10) {
            pTarget = objFindById(kIdGoal, 0);
        } else {
            float flBest = 3.4028235e38f; /* FLT_MAX 0x7f7fffff @0x40d170 */
            GxVec2 vZeroA;
            GxVec2 vZeroB;
            gxVec2SetAngleZero(&vZeroA);
            gxVec2SetAngleZero(&vZeroB);
            if (pRec->anHeldSlot[0] != 0) {
                pTarget = NULL;
            } else if (pRec->nQuestFlags != -1 &&
                       pRec->abListTaken[pRec->nQuestFlags] == 0) {
                pTarget = objFindById(pRec->anListIds[pRec->nQuestFlags], 0);
            } else {
                int i;
                for (i = 0; i < 10; i++) {
                    if (pRec->abListTaken[i] != 0) continue;
                    if (g_nGameMode == 1 && pRec->nQuestTargetId == pRec->anListIds[i]) continue;
                    if (g_nGameMode == 3 && g_nCurrentItemId != 0) continue; /* dead branch for 1/2/4 @0x40d1f1 */
                    if (pRec->anListIds[i] <= 0) continue;
                    {
                        int occ = 0;
                        EventObject *pObj = objFindById(pRec->anListIds[i], occ);
                        occ = 1;
                        while (pObj != NULL) {
                            GxVec2 vDelta;
                            GxVec2 vPolar;
                            vDelta.x = pObj->flOriginX - flSelfZ;
                            vDelta.y = pObj->flOriginZ - flSelfX;
                            mathVec2Polar(&vPolar, &vDelta);
                            if (vPolar.x < flBest) { /* length @0x40d24b..0x40d264 */
                                flBest = vPolar.x;
                                pRec->nQuestFlags = i;
                                pTarget = pObj;
                            }
                            pObj = objFindById(pRec->anListIds[i], occ);
                            occ++;
                        }
                    }
                }
            }
        }
        break;
    }
    default:
        pTarget = NULL;
        break;
    }

    /* Stage 2b — retarget across zones when target != NULL (@0x40d096) */
    if (pTarget != NULL) {
        if (g_nLevelIdx == 4) {
            EventObject *pTargetZone = objFindById(kIdAre1, 0);
            if (pTargetZone == NULL || objContainsPoint(pTargetZone, pTarget->flOriginX, pTarget->flOriginZ) == 0) { /* +0x38/+0x3c @0x40d0d3 */
                pTargetZone = objFindById(kIdAre2, 0);
                if (pTargetZone == NULL || objContainsPoint(pTargetZone, pTarget->flOriginX, pTarget->flOriginZ) == 0) {
                    pTargetZone = objFindById(kIdAre4, 0);
                }
            }
            if (pPlayerZone != pTargetZone) {
                int nPlayerId = (pPlayerZone != NULL) ? pPlayerZone->nId : 0;
                if (nPlayerId == kIdAre1) {
                    EventObject *pExit = objFindById(kIdExi1, 0);
                    if (pExit != NULL) pTarget = pExit;
                } else if (nPlayerId == kIdAre4) {
                    EventObject *pExit = objFindById(kIdExi4, 0);
                    if (pExit != NULL) pTarget = pExit;
                } else if (nPlayerId == kIdAre2) {
                    int nTargetId = (pTargetZone != NULL) ? pTargetZone->nId : 0;
                    if (nTargetId == kIdAre1) {
                        EventObject *pExit = objFindById(kIdExi2, 0);
                        if (pExit != NULL) pTarget = pExit;
                    } else {
                        EventObject *pExit = objFindById(kIdExi3, 0);
                        if (pExit != NULL) pTarget = pExit;
                    }
                }
            }
        } else { /* levels 0-3: PUNK loop over target pos @0x40d313..@0x40d3d0 */
            EventObject *pTargetZone = objFindById(kIdPunk, 0);
            int occ = 1;
            while (pTargetZone != NULL && objContainsPoint(pTargetZone, pTarget->flOriginX, pTarget->flOriginZ) == 0) {
                pTargetZone = objFindById(kIdPunk, occ);
                occ++;
            }
            if (pPlayerZone != pTargetZone) {
                if (pPlayerZone == NULL) {
                    if (pTargetZone != NULL) {
                        float fdx = fabsf(pTargetZone->flOriginX - flSelfZ);
                        float fdy = fabsf(pTargetZone->flOriginZ - flSelfX);
                        int bOutside = ((float)pTargetZone->field_10 <= fdx || (float)pTargetZone->field_14 <= fdy); /* +0x10/+0x14 @0x40d3af/@0x40d3c4 */
                        if (bOutside) pTarget = pTargetZone;
                    }
                } else {
                    float fdxP = fabsf(pPlayerZone->flOriginX - flSelfZ);
                    float fdyP = fabsf(pPlayerZone->flOriginZ - flSelfX);
                    int bInsideP = ((float)pPlayerZone->field_10 > fdxP && (float)pPlayerZone->field_14 > fdyP); /* < both @0x40d37b/@0x40d390 */
                    if (bInsideP) {
                        if (pTargetZone != NULL) {
                            float fdxT = fabsf(pTargetZone->flOriginX - flSelfZ);
                            float fdyT = fabsf(pTargetZone->flOriginZ - flSelfX);
                            int bOutsideT = ((float)pTargetZone->field_10 <= fdxT || (float)pTargetZone->field_14 <= fdyT);
                            if (bOutsideT) pTarget = pTargetZone;
                        }
                    } else {
                        pTarget = pPlayerZone;
                    }
                }
            }
        }
    }

    /* Stage 3 — cart arrow (VAGNPIL, @0x40d3d4..0x40d780) */
    if (pRec->nCartMode != 0 || g_nResultsScreen != 0) {
        sceneNodeSetHiddenFlag(g_pCartArrowObj, 2);
    } else {
        int nSlotX; /* base+0x50: X-ish = +0x3c / objPolarPosLookup(+0x24) */
        int nSlotZ; /* base+0x58: Z-ish = +0x38 / objPolarPosLookup2(+0x20) */
        EventObject *pCartZone = NULL;

        nSlotX = objPolarPosLookup((WorldNode *)pRec->pSubObjB, (int)(size_t)pRec->pCartSceneObj);
        (void)nodeChannelAvgFloat((WorldNode *)pRec->pSubObjB, (int)(size_t)pRec->pCartSceneObj);
        nSlotZ = objPolarPosLookup2((WorldNode *)pRec->pSubObjB, (int)(size_t)pRec->pCartSceneObj);

        /* zone re-resolution on the CART pos */
        if (g_nLevelIdx == 4) {
            pCartZone = objFindById(kIdAre1, 0);
            if (pCartZone == NULL || objContainsPoint(pCartZone, (float)nSlotZ, (float)nSlotX) == 0) { /* FILD @0x40d44e etc */
                pCartZone = objFindById(kIdAre2, 0);
                if (pCartZone == NULL || objContainsPoint(pCartZone, (float)nSlotZ, (float)nSlotX) == 0) {
                    pCartZone = objFindById(kIdAre4, 0);
                }
            }
            if (pPlayerZone != pCartZone) {
                int nPlayerId = (pPlayerZone != NULL) ? pPlayerZone->nId : 0;
                EventObject *pExit = NULL;
                if (nPlayerId == kIdAre1) {
                    pExit = objFindById(kIdExi1, 0);
                } else if (nPlayerId == kIdAre4) {
                    pExit = objFindById(kIdExi4, 0);
                } else if (nPlayerId == kIdAre2) {
                    int nCartId = (pCartZone != NULL) ? pCartZone->nId : 0;
                    if (nCartId == kIdAre1) pExit = objFindById(kIdExi2, 0);
                    else pExit = objFindById(kIdExi3, 0);
                }
                if (pExit != NULL) {
                    nSlotZ = (int)pExit->flOriginX; /* +0x38 ftol @0x40d5ec */
                    nSlotX = (int)pExit->flOriginZ; /* +0x3c ftol @0x40d5f4 */
                }
            }
        } else { /* PUNK loop @0x40d51a..@0x40d5fd */
            pCartZone = objFindById(kIdPunk, 0);
            {
                int occ = 1;
                while (pCartZone != NULL && objContainsPoint(pCartZone, (float)nSlotZ, (float)nSlotX) == 0) {
                    pCartZone = objFindById(kIdPunk, occ);
                    occ++;
                }
            }
            if (pPlayerZone != pCartZone) {
                if (pPlayerZone != NULL) {
                    float fdxP = fabsf(pPlayerZone->flOriginX - flSelfZ);
                    float fdyP = fabsf(pPlayerZone->flOriginZ - flSelfX);
                    int bOutsideP = ((float)pPlayerZone->field_10 <= fdxP || (float)pPlayerZone->field_14 <= fdyP);
                    if (bOutsideP) {
                        nSlotZ = (int)pPlayerZone->flOriginX;
                        nSlotX = (int)pPlayerZone->flOriginZ;
                    } else if (pCartZone != NULL) {
                        float fdxT = fabsf(pCartZone->flOriginX - flSelfZ);
                        float fdyT = fabsf(pCartZone->flOriginZ - flSelfX);
                        int bOutsideT = ((float)pCartZone->field_10 <= fdxT || (float)pCartZone->field_14 <= fdyT);
                        if (bOutsideT) {
                            nSlotZ = (int)pCartZone->flOriginX;
                            nSlotX = (int)pCartZone->flOriginZ;
                        }
                    }
                } else if (pCartZone != NULL) {
                    float fdxT = fabsf(pCartZone->flOriginX - flSelfZ);
                    float fdyT = fabsf(pCartZone->flOriginZ - flSelfX);
                    int bOutsideT = ((float)pCartZone->field_10 <= fdxT || (float)pCartZone->field_14 <= fdyT);
                    if (bOutsideT) {
                        nSlotZ = (int)pCartZone->flOriginX;
                        nSlotX = (int)pCartZone->flOriginZ;
                    }
                }
            }
        }

        /* Arrow anim — per side cart then goods share the same pattern */
        {
            float flSpeed = g_flGameObjSpeed + kFlSpeedInc;
            int anWorld[3];
            short anRotBefore[3];
            short anRotAfter[3];
            int nBob1;
            int nBob2;

            g_flGameObjSpeed = flSpeed;
            sceneNodeGetPosWorld(g_pSceneRoot, (float *)anWorld, 2);
            sceneObjGetPos(g_pSceneRoot, anRotBefore, 2);
            sceneNodeFacePos(g_pSceneRoot, 0, (float)nSlotX, (float)anWorld[1], (float)nSlotZ, 2);
            sceneObjGetPos(g_pSceneRoot, anRotAfter, 2);
            sceneObjSetPosOrient(g_pCartArrowMesh, (short)(anRotAfter[0] - anRotBefore[0]), 0, (short)(anRotAfter[2] - anRotBefore[2]), 2);

            if (pRec->nCartMode != 0) {
                nBob1 = (int)(sin(g_flGameObjSpeed + kDblCartPh1) * (pRec->flCartTurnAccum + pRec->flCartCurSpeed) * kFlCartMul1);
                nBob2 = (int)(sin(g_flGameObjSpeed + kDblCartPh2) * (pRec->flCartTurnAccum + pRec->flCartCurSpeed) * kFlCartMul2);
            } else {
                nBob1 = (int)(sin(g_flGameObjSpeed + kDblWalkPh1) * (pRec->flTurnAccum + pRec->flCurSpeed) * kFlWalkMul1);
                nBob2 = (int)(sin(g_flGameObjSpeed + kDblWalkPh2) * (pRec->flTurnAccum + pRec->flCurSpeed) * kFlWalkMul2);
            }
            sceneObjSetPosOrient(g_pCartArrowObj, (short)nBob2, (short)(anRotAfter[1] - anRotBefore[1]), (short)nBob1, 2);
            sceneObjSetPosOrient(g_pSceneRoot, anRotBefore[0], anRotBefore[1], anRotBefore[2], 2); /* restore @0x40d76a */
            sceneObjResetFlags(g_pCartArrowObj, 2);
        }
    }

    /* Goods arrow target position */
    if (pTarget == NULL || g_nResultsScreen != 0) {
        sceneNodeSetHiddenFlag(g_pGoodsArrowObj, 2);
    } else {
        int nSlotX;
        int nSlotZ;
        float flSpeed;
        int anWorld[3];
        short anRotBefore[3];
        short anRotAfter[3];
        int nBob1;
        int nBob2;

        g_flGameObjSpeed2 += kFlSpeedInc;
        nSlotX = (int)pTarget->flOriginZ; /* +0x3c ftol @0x40d7c1 */
        nSlotZ = (int)pTarget->flOriginX; /* +0x38 ftol @0x40d7c6 */

        sceneNodeGetPosWorld(g_pSceneRoot, (float *)anWorld, 2);
        sceneObjGetPos(g_pSceneRoot, anRotBefore, 2);
        sceneNodeFacePos(g_pSceneRoot, 0, (float)nSlotX, (float)anWorld[1], (float)nSlotZ, 2);
        sceneObjGetPos(g_pSceneRoot, anRotAfter, 2);
        sceneObjSetPosOrient(g_pGoodsArrowMesh, (short)(anRotAfter[0] - anRotBefore[0]), 0, (short)(anRotAfter[2] - anRotBefore[2]), 2);
        flSpeed = g_flGameObjSpeed2;
        if (pRec->nCartMode != 0) {
            nBob1 = (int)(sin(flSpeed + kDblGoodsCartPh1) * (pRec->flCartTurnAccum + pRec->flCartCurSpeed) * kFlGoodsCartMul1);
            nBob2 = (int)(sin(flSpeed + kDblGoodsCartPh2) * (pRec->flCartTurnAccum + pRec->flCartCurSpeed) * kFlGoodsCartMul2);
        } else {
            nBob1 = (int)(sin(flSpeed + kDblGoodsWalkPh1) * (pRec->flTurnAccum + pRec->flCurSpeed) * kFlGoodsWalkMul1);
            nBob2 = (int)(sin(flSpeed + kDblGoodsWalkPh2) * (pRec->flTurnAccum + pRec->flCurSpeed) * kFlGoodsWalkMul2);
        }
        sceneObjSetPosOrient(g_pGoodsArrowObj, (short)nBob2, (short)(anRotAfter[1] - anRotBefore[1]), (short)nBob1, 2);
        sceneObjSetPosOrient(g_pSceneRoot, anRotBefore[0], anRotBefore[1], anRotBefore[2], 2);
        sceneObjResetFlags(g_pGoodsArrowObj, 2);
    }

    /* Stage 4 — 30 item-slot pass (@0x40d955..0x40db4b) */
    g_flGameObjSpeed3 += kFlSpeedInc;
    {
        int i;
        for (i = 0; i < 30; i++) {
            LevelItemSlot *pSlot = &g_apLevelItemSlots[i]; /* @0x4583c8 + i*0x2c @0x40d97c */
            sceneNodeSetHiddenFlag((SceneNode *)pSlot->pSubObj, 2);
            switch (g_nGameMode) {
            case 1: {
                int j;
                for (j = 0; j < 10; j++) {
                    if (pRec->abListTaken[j] != 0) continue;
                    if (pRec->anListIds[j] != i + 1) continue;
                    if (pRec->nQuestTargetId == i + 1) continue;
                    if (pRec->anHeldSlot[0] == i + 1) continue;
                    if (pRec->nLastThrownItemId == i + 1) continue;
                    if (pSlot->pEventObj != NULL) {
                        sceneObjResetFlags((SceneNode *)pSlot->pSubObj, 2);
                    }
                    break;
                }
                break;
            }
            case 2: {
                int j;
                for (j = 0; j < 10; j++) {
                    if (pRec->abListTaken[j] != 0) continue;
                    if (pRec->anListIds[j] != i + 1) continue;
                    if (pRec->anHeldSlot[0] == i + 1) continue;
                    if (pRec->nLastThrownItemId == i + 1) continue;
                    if (pSlot->pEventObj != NULL) {
                        sceneObjResetFlags((SceneNode *)pSlot->pSubObj, 2);
                    }
                    break;
                }
                break;
            }
            case 3:
                if (g_nCurrentItemId == i + 1 && pSlot->pEventObj != NULL) {
                    sceneObjResetFlags((SceneNode *)pSlot->pSubObj, 2);
                }
                break;
            case 4: {
                int k;
                for (k = 0; k < 3; k++) {
                    if (pRec->anListIds[k] == i + 0xc9 && pSlot->pEventObj != NULL) { /* 201 @0x40da99 */
                        sceneObjResetFlags((SceneNode *)pSlot->pSubObj, 2);
                    }
                }
                break;
            }
            default:
                break;
            }
            if (sceneNodeGetHiddenFlag((SceneNode *)pSlot->pSubObj) == 0 && pSlot->pEventObj != NULL) {
                int anOut[3];
                sceneObjSetPosOrient((SceneNode *)pSlot->pSceneObj, 100, 0x60e, 200, 5);
                sceneNodeGetPos((SceneNode *)pSlot->pSceneObj, 0, anOut, 2);
                {
                    int nBob = (int)(sin((double)g_flGameObjSpeed3) * kDblSlotBob100);
                    sceneObjSetPos((SceneNode *)pSlot->pSceneObj, anOut[0], nBob, anOut[2], 2);
                }
            }
        }
    }
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
/* --- playerAnimSfxUpdate @0x40c800 — per-player animation stepping +
 * gameplay sfx emitters (verified 2026-08-30 against the disassembly). --- */
int g_nAnimSfxTick;     /* @0x458954 alternating step gate (toggled each call) */
float g_playerAnimT;    /* @0x458958 results-screen winner orbit phase */

/* Constants used by playerAnimSfxUpdate (addresses from the disassembly). */
static const float g_flLimbTurnScale = 5575.0f;   /* @0x44b4a8 (bytes 00 00 AF 45) */
static const float g_flEngineSpeedHi = 45.0f;     /* @0x44b4a4 (bytes 00 00 34 42) */
static const float g_flEngineSpeedLo = -45.0f;    /* @0x44b4a0 (bytes 00 00 34 C2) */
static const double g_dblEngineTurnHi = 0.012;    /* @0x44b498 (bytes FA 7E 6A BC 74 93 88 3F) */
static const double g_dblEngineTurnLo = -0.012;   /* @0x44b490 (bytes FA 7E 6A BC 74 93 88 BF) */
static const double g_dblOrbitRadius = -2000.0;   /* @0x44b4b0 (bytes 00 40 9F C0 00 00 00 00) */
static const float g_flOrbitStep = 0.04f;         /* @0x44b4b8 (bytes 0A D7 23 3D) */
static const float g_flOrbitAimY = 1000.0f;       /* @0x44b464 (bytes 00 00 7A 44) */

/* playerAnimSfxUpdate @0x40c800 — per-player animation + gameplay sfx pass,
 * called from gameWorldUpdate (@0x40b4ac). Two halves:
 *  - g_nResultsScreen == 0: toggle g_nAnimSfxTick, then per player: set
 *    nChannelsDirty, run the nAiPhase animation-state machine (1/4/7 reset +
 *    advance, 2/5/8 step + advance, 3/6/9/0xf skip), on-foot/cart idle and
 *    run anim stepping, the flInputTurn limb sub-pos pass (sub-meshes 1/2
 *    pitch = turn accum * 5575 / rot-step, doubled on mesh 1), the cart
 *    limb-aim pass (sub-meshes 6/7 and 3/4 aimed at the two cart children
 *    via playerAnimOrientFromDir +-16000 roll), and the move/engine
 *    emitters (bank 1 idx 10 while accelerating, idx 11 while |speed| > 45
 *    or |turn| > 0.012, freed in the quiet band).
 *  - results screen: step the winner set (interp on even ticks) and the
 *    losers' stand set (odd ticks), free every emitter, then orbit the
 *    scene root around the winner node (t += 0.04, x/z -= +-2000-radius
 *    sin/cos, y - 2000; facePos aims 1000 above). */
void playerAnimSfxUpdate(void) /* @0x40c800 */
{
    static const float g_flZero = 0.0f;   /* @0x44b244 */
    PlayerRecord *pRec;
    AnmSet *pSet;
    int iPlayerIdx; /* Ghidra local_58 */

    g_nAnimSfxTick = (g_nAnimSfxTick == 0);                     /* @0x40c82d */
    if (g_nResultsScreen != 0) {                                /* @0x40c838 */
        for (iPlayerIdx = 0; iPlayerIdx < g_nPlayerCount; iPlayerIdx++) { /* @0x40c84d */
            pRec = &g_playerRecords[iPlayerIdx];
            if (iPlayerIdx == g_nWinnerIdx) {                   /* @0x40c85f */
                pSet = (AnmSet *)pRec->apAnmSets[10];           /* +0x2d0 winner set */
                if (g_nAnimSfxTick == 0) {                      /* @0x40c863 */
                    sceneObjectAnimStepInterp((SceneObjAnimList *)pSet, 1); /* @0x4347c0 @0x40c892 */
                } else {
                    sceneObjectAnimStep((SceneObjAnimList *)pSet, 1); /* @0x434540 @0x40c8a5 */
                }
            } else if (g_nAnimSfxTick != 0) {                   /* @0x40c899 */
                sceneObjectAnimStep((SceneObjAnimList *)pRec->apAnmSets[7], 1); /* stand */
            }
            if (pRec->pSndEmitterStep != NULL) {                /* @0x40c8b3 */
                sndEmitterFree((SndEmitter *)pRec->pSndEmitterStep); /* @0x42bec0 */
                memFreeDirect(pRec->pSndEmitterStep);
                pRec->pSndEmitterStep = NULL;
            }
            if (pRec->pSndEmitterEngine != NULL) {              /* @0x40c8d0 */
                sndEmitterFree((SndEmitter *)pRec->pSndEmitterEngine);
                memFreeDirect(pRec->pSndEmitterEngine);
                pRec->pSndEmitterEngine = NULL;
            }
        }
        {
            int anPos[3];
            int nOrbitX;
            int nOrbitY;
            int nOrbitZ;
            pRec = &g_playerRecords[g_nWinnerIdx];
            sceneNodeGetPosWorld(pRec->pCharSceneNode, (float *)anPos, 2); /* @0x430e80 @0x40c926 */
            g_playerAnimT += g_flOrbitStep;                     /* @0x40c92b */
            /* The orbit offsets go only to the scene-root position; the
             * aim below uses the pristine winner position (@0x40c989..0x40c9b1
             * reads the unmodified locals). */
            nOrbitX = anPos[0] - (int)(sin((double)g_playerAnimT) * g_dblOrbitRadius); /* @0x40c952 */
            nOrbitY = anPos[1] - 2000;                          /* @0x40c964 */
            nOrbitZ = anPos[2] - (int)(cos((double)g_playerAnimT) * g_dblOrbitRadius); /* @0x40c945 */
            sceneObjSetPos(g_pSceneRoot, nOrbitX, nOrbitY, nOrbitZ, 2); /* @0x430660 @0x40c984 */
            sceneNodeFacePos(g_pSceneRoot, 0, (float)anPos[0],  /* @0x431030 @0x40c9b3 */
                             (float)anPos[1] - g_flOrbitAimY, (float)anPos[2], 2);
        }
        return;                                                 /* @0x40cf39 */
    }

    for (iPlayerIdx = 0; iPlayerIdx < g_nPlayerCount; iPlayerIdx++) { /* @0x40c9ce */
        pRec = &g_playerRecords[iPlayerIdx];
        pRec->nChannelsDirty = 1;                               /* +0x2d8 @0x40c9e9 */
        if ((pRec->bStateFlags & 4) != 0) {                     /* +0x158 @0x40c9ec */
            sceneObjectAnimStep((SceneObjAnimList *)pRec->apAnmSets[9], 1); /* oops @0x40c9fb */
            continue;                                           /* @0x40cf0d */
        }
        pSet = (AnmSet *)pRec->pAnimSet;                        /* +0x2d4 */
        if (pRec->nAiPhase == 1 || pRec->nAiPhase == 4 ||       /* @0x40ca08 */
            pRec->nAiPhase == 7) {
            eventAnimReset(pSet->pAnm);                         /* @0x434270 @0x40cedc */
            pRec->nAiPhase++;                                   /* @0x40cee7 */
        } else if (pRec->nAiPhase == 2 || pRec->nAiPhase == 5 || /* @0x40ce97 */
                   pRec->nAiPhase == 8) {
            if (sceneObjectAnimStep((SceneObjAnimList *)pSet, 1) != 0) { /* @0x40ce9c */
                pRec->nAiPhase++;                               /* @0x40cecc */
            }
        } else if (pRec->nAiPhase != 3 && pRec->nAiPhase != 6 && /* @0x40ca40 */
                   pRec->nAiPhase != 9 && pRec->nAiPhase != 0xf) {
            /* idle/move anim + limb + emitter pass */
            if (pRec->flInputAccel == g_flZero) {               /* +0x2e4 @0x40ca64 */
                pSet = (AnmSet *)pRec->apAnmSets[6];            /* +0x2c0 run set */
                if (((AnmFile *)pSet->pAnm)->nFrame != 0) {     /* @0x40ca7c */
                    eventAnimReset(pSet->pAnm);                 /* @0x434270 @0x40ca84 */
                }
                if (g_nAnimSfxTick != 0) {                      /* @0x40ca8c */
                    sceneObjectAnimStep((SceneObjAnimList *)pRec->apAnmSets[7], 1); /* stand @0x40ca99 */
                }
            } else {
                sceneObjectAnimStep((SceneObjAnimList *)pRec->apAnmSets[6], 1); /* run @0x40ca78 */
            }
            if (pRec->flInputTurn != g_flZero) {                /* +0x2e0 @0x40caa1 */
                short anAngles[3];
                int nLimbPitch;

                if (pRec->nCartMode != 0) {                     /* @0x40cab5 */
                    nLimbPitch = (int)(pRec->flCartTurnAccum * g_flLimbTurnScale /
                                       pRec->flCartRotAccFric); /* @0x40cabd */
                } else {
                    nLimbPitch = (int)(pRec->flTurnAccum * g_flLimbTurnScale /
                                       pRec->flRotAccStep);     /* @0x40cacb */
                }
                sceneNodeGetChannelPos(pRec->pCharSceneObj, 1, anAngles, 2, NULL); /* @0x4315e0 @0x40caf3 */
                sceneObjSetSubPos(pRec->pCharSceneObj, 1,       /* @0x430a90 @0x40cb10 */
                                  anAngles[0], (short)(nLimbPitch * 2), anAngles[2], 2);
                sceneNodeGetChannelPos(pRec->pCharSceneObj, 2, anAngles, 2, NULL); /* @0x40cb24 */
                sceneObjSetSubPos(pRec->pCharSceneObj, 2,       /* @0x40cb40 */
                                  anAngles[0], (short)nLimbPitch, anAngles[2], 2);
            }
            if (pRec->nCartMode != 0) {                         /* cart limb aim @0x40cb48 */
                short anAngles[3];
                short anWalk[3];
                int anCart[6];

                sceneSetCurrentObj(pRec->pCharSceneObj, 1);     /* @0x430d98 @0x40cb5c */

                sceneNodeGetPos(pRec->pCartHandleR, 0, anCart, 6);   /* @0x431270 @0x40cb70 */
                sceneNodeGetPos(pRec->pCharSceneObj, 6, anCart + 3, 2); /* @0x40cb85 */
                playerAnimOrientFromDir(anCart[0] - anCart[3],  /* @0x4336b0 @0x40cbdb */
                                        anCart[1] - anCart[4],
                                        anCart[2] - anCart[5],
                                        anAngles, anWalk, NULL,
                                        &g_awWalkAnimTable[0][0], 0x1ea, (short)-16000);
                sceneObjSetSubPos(pRec->pCharSceneObj, 6,       /* @0x40cbfd */
                                  anAngles[0], anAngles[1], anAngles[2], 2);
                sceneObjSetSubPos(pRec->pCharSceneObj, 7,       /* @0x40cc1c */
                                  anWalk[0], anWalk[1], anWalk[2], 2);

                sceneNodeGetPos(pRec->pCartHandleL, 0, anCart, 6);   /* @0x40cc30 */
                sceneNodeGetPos(pRec->pCharSceneObj, 3, anCart + 3, 2); /* @0x40cc48 */
                playerAnimOrientFromDir(anCart[0] - anCart[3],  /* @0x40cc98 */
                                        anCart[1] - anCart[4],
                                        anCart[2] - anCart[5],
                                        anAngles, anWalk, NULL,
                                        &g_awWalkAnimTable[0][0], 0x1ea, (short)16000);
                sceneObjSetSubPos(pRec->pCharSceneObj, 3,       /* @0x40ccb7 */
                                  anAngles[0], anAngles[1], anAngles[2], 2);
                sceneObjSetSubPos(pRec->pCharSceneObj, 4,       /* @0x40ccd9 */
                                  anWalk[0], anWalk[1], anWalk[2], 2);
            }
            /* footstep/move emitter: live while accelerating */
            if (pRec->flInputAccel == g_flZero) {               /* @0x40cce1 */
                if (pRec->pSndEmitterStep != NULL) {            /* @0x40cd52 */
                    sndEmitterFree((SndEmitter *)pRec->pSndEmitterStep);
                    memFreeDirect(pRec->pSndEmitterStep);
                    pRec->pSndEmitterStep = NULL;
                }
            } else if (pRec->pSndEmitterStep == NULL) {         /* @0x40ccf1 */
                void *pEmitter = malloc(0x1c);                  /* operator_new @0x43dd42 @0x40ccfb */
                if (pEmitter != NULL) {
                    sndPlaySfx3D((SndEmitter *)pEmitter, 1, 10, 65000, 0xff, /* @0x42bcd0 @0x40cd2b */
                                 pRec->pCharSceneNode, 0, 0, 0, 0, 0x11);
                }
                pRec->pSndEmitterStep = pEmitter;
            }
            /* engine emitter: live while |speed| > 45 or |turn| > 0.012 */
            {
                float flSpeed = (pRec->nCartMode != 0) ? pRec->flCartCurSpeed /* @0x40cdc6 */
                                                       : pRec->flPosSpeed;   /* @0x40cd7c */
                float flTurn = (pRec->nCartMode != 0) ? pRec->flCartTurnAccum /* @0x40cdc6 */
                                                      : pRec->flPosTurnAccum; /* @0x40cda2 */
                if (flSpeed > g_flEngineSpeedHi || flSpeed < g_flEngineSpeedLo ||
                    flTurn > g_dblEngineTurnHi || flTurn < g_dblEngineTurnLo) {
                    if (pRec->pSndEmitterEngine == NULL) {       /* @0x40ce2f */
                        void *pEmitter = malloc(0x1c);           /* operator_new @0x40ce3d */
                        if (pEmitter != NULL) {
                            sndPlaySfx3D((SndEmitter *)pEmitter, 1, 11, 65000, 0xff, /* @0x40ce6d */
                                         pRec->pCartSceneObj, 0, 0, 0, 0, 0x11);
                        }
                        pRec->pSndEmitterEngine = pEmitter;
                    }
                } else if (pRec->pSndEmitterEngine != NULL) {    /* @0x40ce06 */
                    sndEmitterFree((SndEmitter *)pRec->pSndEmitterEngine);
                    memFreeDirect(pRec->pSndEmitterEngine);
                    pRec->pSndEmitterEngine = NULL;
                }
            }
        }
    }
}
