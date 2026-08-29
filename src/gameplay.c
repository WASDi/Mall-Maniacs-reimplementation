#include <stdlib.h>

#include "gameplay.h"
#include "anim.h"
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
     * entries, entry i for normalized speed i*0.01f (const @0x44b458) on
     * the circle center 0xd2 / radius 0x118. */
    {
        int i;
        for (i = 0; i < 128; i++) {
            walkAnimTableEntryCalc(g_awWalkAnimTable[i], (float)i * 0.01f,
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
 * first dword of the "goal" string @0x44f4e4 (0x6C6F6767) and passes it
 * to objFindById @0x414a90 from all four win checks. */
static const int kGoalObjNameId = 0x6C6F6767; /* *(int *)"goal" @0x44f4e4 */

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
 * player whose nListProgress (+0x180) reached the target has bStateFlags
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
            pRec->nListProgress != (nTarget)) {                          \
            break;        /* @0x40c137 @0x40c140 */                      \
        }                                                                \
        pRec->bStateFlags |= 0x20;     /* @0x40c146 */                   \
        if (pRec->field_174 == 0) {    /* +0x174 @0x40c14d */            \
            break;                                                       \
        }                                                                \
        pGoal = objFindById(kGoalObjNameId, 0); /* @0x414a90 @0x40c15c */\
        if (pGoal == NULL) {                                             \
            break;                                                       \
        }                                                                \
        {   float *pfPos = (float *)pRec->pSubObjC; /* +0x2a4 @0x40c16a */\
            if (objContainsPoint(pGoal, pfPos[8], pfPos[9])) {           \
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
 *      (nListProgress +0x180 == 10) + checkout zone -> win; mode 2 skips
 *      the check on clients. mode 3 Matkrig: nListProgress == 5 + zone;
 *      afterwards the server/offline item spawner keeps g_nSpawnTimer
 *      @0x458950 (forced to 8999 during the first 3 s of player-0
 *      ticks): at 10000 ms it rolls g_nCurrentItemId @0x458128 =
 *      rand()%24+1 (net sub 0x20) and deals it to every player's
 *      anListIds[0]/abListTaken[0] (+0x184/+0x1ac); an active item
 *      resets the timer. mode 4 Vagnrace: per player, when the +0x174
 *      stage gate is 0 the bStateFlags bit 2 gates the first rail, else
 *      the current rail id anListIds[0] (+0x184) resolves; touching a
 *      rail origin (+0x38/+0x3c) within 1.5 of the char node position
 *      (pSubObjA +0x224 on the first rail, pSubObjC +0x2a4 afterwards;
 *      double 1.5 @0x44b480) increments anListIds[0..2] and clears
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
            g_playerRecords[i].nAnimationFrame = 0;        /* +0x2e0 */
            g_playerRecords[i].nAnimationTimer = 0;        /* +0x2e4 */
            g_playerRecords[i].field_2e8 = 0;              /* +0x2e8 */
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
    }

    if (g_nResultsScreen == 0) {                           /* @0x40c0ba */
        if (g_nRoundElapsedTicks / g_nObjUpdateTime >=
            g_nRoundTimeLimit / g_nObjUpdateTime &&        /* @0x40c0cd */
            g_nPlayerCount > 0) {
            for (i = 0; i < g_nPlayerCount; i++) {         /* @0x40c0d5 */
                g_playerRecords[i].nScoreTicks++;          /* +0x15c @0x40c0dc */
            }
        }
    } else if (g_nPlayerCount > 0) {                       /* @0x40c0ef */
        for (i = 0; i < g_nPlayerCount; i++) {             /* @0x40c0fa */
            g_playerRecords[i].nAnimationFrame = 0;
            g_playerRecords[i].nAnimationTimer = 0;
            g_playerRecords[i].field_2e8 = 0;
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
            if (pRec->field_174 == 0) {                    /* first stage @0x40c54b */
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
                    if (objContainsPoint(pGoal, pfPos[8], pfPos[9])) {
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
            /* rail proximity: |origin - char node pos| <= 1.5 on both
             * axes (double 1.5 @0x44b480; origin +0x38/+0x3c vs node
             * +0x20/+0x24). */
            if (!((float)pRail->flOriginX - (int)pfPos[8] <= 1.5 &&
                  (float)pRail->flOriginX - (int)pfPos[8] >= -1.5 &&
                  (float)pRail->flOriginZ - (int)pfPos[9] <= 1.5 &&
                  (float)pRail->flOriginZ - (int)pfPos[9] >= -1.5)) {
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