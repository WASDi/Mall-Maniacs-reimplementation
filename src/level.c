#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "level.h"
#include "config.h"
#include "gameplay.h"
#include "gx.h"
#include "levelselect.h"
#include "menu.h"
#include "mstring.h"
#include "nav.h"
#include "pool.h"
#include "scene.h"
#include "sen.h"
#include "stubs.h"
#include "time.h"
#include "util.h"
#include "zone.h"
#include "custom_helpers.h"

/* =====================================================================
 * level.c — levelSetup @0x4108a0. Called from roundStartInit after
 * configMasterLoad. Loads the three level .sen scenes from the
 * "levels[%d]" config block, allocates the per-level detail grid and
 * fills the 30 item slots from the "items[%d]" blocks. The startup
 * command script runs through commandDispatch.
 * ===================================================================== */

int g_nLevelScene;        /* @0x4580b0 */
int g_nCharScene;         /* @0x4580b4 */
int g_nObjScene;          /* @0x4580b8 */
int g_bMusicTrack;        /* @0x4580c0 */
int g_nTexHudFlingbjorn;  /* @0x458348 */

const char *g_aszLevelDirs[5] = {      /* @0x44f0d8 pointer table */
    "scene_ica",                       /* [0] @0x44f128 */
    "scene_wood",                      /* [1] @0x44f11c */
    "scene_orient",                    /* [2] @0x44f10c */
    "scene_aqua",                      /* [3] @0x44f100 */
    "scene_future",                    /* [4] @0x44f0f0 */
};

LevelItemSlot g_apLevelItemSlots[LEVEL_ITEM_SLOT_COUNT]; /* @0x4583c8 */

char g_szLevelScenePath[256];  /* @0x457db0 */
char g_szObjScenePath[256];    /* @0x457fb0 */
char g_szCharScenePath[256];   /* @0x457eb0 */

extern void *g_pSceneDetailGrid;   /* @0x45838c (defined in gameplay.c) */

/* levelSceneTexturesLoad @0x4104b0 — level world pre-pass, run inside
 * roundStartInit's FIRST scene-system cycle (scenNameTableInit 10000/10000),
 * before the teardown + load screen + second cycle that levelSetup uses.
 * Streams the "levels[%d]/texture_files" pattern (e.g.
 * "Scene_ica\merged%02d.tpg") into GX with names "MERGED%02d" until
 * fileReadRaw fails, plus the per-level extra sets (aqua/future/orient).
 * Then reloads the "levels[%d]" block, builds "%s\ph%s" from scene_path +
 * scene_file, loads that scene into g_nLevelScene @0x4580b0, turns every
 * "FLOOR" scene-object into a 0x48-byte AI nav node (aiNavNodeCtorScene
 * @0x428cf0) and runs zoneWallListBuild @0x42a650. Fatals with
 * "'levels[%d]' not found in cfg." @0x44f794 when the block is missing. */
void levelSceneTexturesLoad(void) /* @0x4104b0 */
{
    char szLevelKey[32];
    char szPattern[400];
    char szPath[400];
    char *pData;
    MString mstr;
    MString mstrScenePath;
    MString mstrSceneFile;
    ConfigNode *pLevel;
    int i;
    int n;
    int anHandles[1024];

    fmtSprintf(szLevelKey, "levels[%d]", g_nLevelIdx);            /* wsprintfA @0x4104e9 */
    pLevel = configEnvGetValue(&g_configEnvMaster, NULL, szLevelKey);  /* @0x4104fa */
    if (pLevel == NULL) {
        fatalError("'levels[%d]' not found in cfg.", g_nLevelIdx);  /* @0x410510 */
    }
    configEnvGetString(&mstr, pLevel, "texture_files");           /* @0x410528 */
    strcpy(szPattern, mStringCStr(&mstr));                        /* inline rep move @0x410546 */
    mStringFree(&mstr);                                           /* @0x410574 */
    nopDebugStub();                                               /* "Loading textures..." @0x44f770, ch 0 @0x41057f */
    for (i = 0; ; i++) {
        fmtSprintf(szPath, szPattern, i);                         /* @0x41059d (pattern carries %02d) */
        pData = fileReadRaw(0, szPath);                           /* @0x4105a9 */
        if (pData == NULL) break;
        fmtSprintf(szPath, "MERGED%02d", i);                      /* @0x4105c2 */
        gxLoadTexture(0, 1, szPath, pData, pData + 0x10000);      /* @0x4105d7 */
        memPoolFree(0, pData);                                    /* @0x4105df */
    }
    if (g_nLevelIdx == 3) {                                       /* aqua extras @0x4105f4 */
        for (i = 0; ; i++) {
            fmtSprintf(szPath, "scene_aqua\\aqua%02d.tpg", i);    /* @0x44f74c */
            pData = fileReadRaw(0, szPath);
            if (pData == NULL) break;
            fmtSprintf(szPath, "AQUA%02d", i);                    /* @0x44f740 */
            gxLoadTexture(0, 1, szPath, pData, pData + 0x10000);
            memPoolFree(0, pData);
        }
    } else if (g_nLevelIdx == 4) {                                /* future extras @0x410657 */
        for (i = 0; ; i++) {
            fmtSprintf(szPath, "scene_future\\fut%02d.tpg", i);   /* @0x44f724 */
            pData = fileReadRaw(0, szPath);
            if (pData == NULL) break;
            fmtSprintf(szPath, "FUT%02d", i);                     /* @0x44f71c */
            gxLoadTexture(0, 1, szPath, pData, pData + 0x10000);
            memPoolFree(0, pData);
        }
    } else if (g_nLevelIdx == 2) {                                /* orient extras @0x4106ba */
        for (i = 0; ; i++) {
            fmtSprintf(szPath, "scene_orient\\orient%02d.tpg", i); /* @0x44f700 */
            pData = fileReadRaw(0, szPath);
            if (pData == NULL) break;
            fmtSprintf(szPath, "ORIENT%02d", i);                  /* @0x44f6f4 */
            gxLoadTexture(0, 1, szPath, pData, pData + 0x10000);
            memPoolFree(0, pData);
        }
    }
    /* Second "levels[%d]" lookup for the ph-scene pass @0x410728. */
    fmtSprintf(szLevelKey, "levels[%d]", g_nLevelIdx);
    pLevel = configEnvGetValue(&g_configEnvMaster, NULL, szLevelKey);
    if (pLevel == NULL) {
        fatalError("'levels[%d]' not found in cfg.", g_nLevelIdx);  /* @0x41075d */
    }
    configEnvGetString(&mstrScenePath, pLevel, "scene_path");     /* @0x44f6e8 @0x410775 */
    configEnvGetString(&mstrSceneFile, pLevel, "scene_file");     /* @0x44f6dc @0x4107ac */
    fmtSprintf(g_szLevelScenePath, "%s\\ph%s",                    /* @0x44f6d4 @0x4107d5 */
               mStringCStr(&mstrScenePath), mStringCStr(&mstrSceneFile));
    mStringFree(&mstrSceneFile);                                  /* @0x4107e8 */
    nopDebugStub();                                               /* "Loading MallScene <%s>" @0x44f6bc, ch 0 @0x4107f9 */
    g_nLevelScene = sceneLoadSen(g_szLevelScenePath, NULL);       /* @0x432320 @0x410807 */
    n = sceneCollectMeshHandles(anHandles, 0x400, "FLOOR");       /* @0x42b360 @0x410823 */
    if (n > 0) {
        for (i = 0; i < n; i++) {
            AiNavNode *pNav = malloc(sizeof(AiNavNode));          /* operator_new @0x43dd42 @0x41083a */
            if (pNav != NULL) {
                aiNavNodeCtorScene(pNav, (SceneNode *)(size_t)anHandles[i]); /* @0x428cf0 @0x410857 */
            }
        }
    }
    zoneWallListBuild();                                          /* @0x42a650 @0x410869 */
    mStringFree(&mstrScenePath);                                  /* @0x41087d */
}

/* levelSetup @0x4108a0 — per-level scene + item setup from config
 * (levels/%d). */
void levelSetup(void) /* @0x4108a0 */
{
    char szPath[256];
    char szTexFiles[252];
    MString mstrScenePath;
    MString mstrTmp;
    ConfigNode *pLevel;
    ConfigNode *pObjects;
    ConfigNode *pStartup;
    ConfigNode *pCur;
    int i;
    LevelItemSlot *pSlot;

    wsprintf(szPath, "levels[%d]", g_nLevelIdx);              /* "levels[%d]" @0x44f7b4 */
    pLevel = configEnvGetValue(&g_configEnvMaster, NULL, szPath);
    if (pLevel == NULL) {
        fatalError("'levels[%d]' not found in cfg.", g_nLevelIdx);   /* @0x44f794 */
    }
    configEnvGetString(&mstrTmp, pLevel, "texture_files");   /* @0x44f784 */
    lstrcpyn(szTexFiles, mStringCStr(&mstrTmp), (int)sizeof(szTexFiles));
    mStringFree(&mstrTmp);
    nopDebugStub();                                          /* original keeps the copy unused */

    fmtSprintf(szPath, "%s\\hud\\flingbjorn.tga",             /* @0x44f95c */
               g_aszLevelDirs[g_nLevelIdx]);
    g_nTexHudFlingbjorn = (int)imageLoadByMode(szPath);
    if (g_nTexHudFlingbjorn == 0) {
        fatalError("Flingbj\x94rn picture not found!!");     /* @0x44f93c */
    }

    mStringCtorEmpty(&mstrScenePath);
    wsprintf(szPath, "levels[%d]", g_nLevelIdx);
    pLevel = configEnvGetValue(&g_configEnvMaster, NULL, szPath);
    if (pLevel == NULL) {
        fatalError("'levels[%d]' not found in cfg.", g_nLevelIdx);
    }
    configEnvGetString(&mstrTmp, pLevel, "scene_path");      /* @0x44f6e8 */
    mStringAssignCopy(&mstrScenePath, &mstrTmp);
    mStringFree(&mstrTmp);
    configEnvGetString(&mstrTmp, pLevel, "scene_file");      /* @0x44f6dc */
    fmtSprintf(g_szLevelScenePath, "%s\\%s",                 /* @0x44f934 */
               mStringCStr(&mstrScenePath), mStringCStr(&mstrTmp));
    mStringFree(&mstrTmp);
    nopDebugStub();
    /* original also passes two NULL trailing args to sceneLoadSen */
    g_nLevelScene = sceneLoadSen(g_szLevelScenePath, NULL);

    {
        void *pGrid = malloc(0x20);                          /* operator_new @0x43dd42 */
        g_pSceneDetailGrid = (pGrid != NULL)
            ? sceneDetailGridCtor(pGrid, 0, 4, 0x400, 10000)
            : NULL;
    }
    if (*(int *)g_pSceneDetailGrid != 0) {
        fatalError("Failed to set up detail levels!!");      /* @0x44f8f4 */
    }

    g_bMusicTrack = (int)configEnvGetDouble2(&g_configEnvMaster, pLevel, "music"); /* @0x44f8ec */

    scenSetDir("__HIDE_ME__");                               /* @0x44f23c @0x410b1e
                                                              * (same "__HIDE_ME__" string the roundStartInit hide pass filters for) */
    pObjects = configEnvGetValue(&g_configEnvMaster, NULL, "objects");   /* @0x44f8e4 */
    if (pObjects == NULL) {
        fatalError("'objects' block not found in cfg.");     /* @0x44f8c0 */
    }
    configEnvGetString(&mstrTmp, pObjects, "obj_scene_file");/* @0x44f8b0 */
    fmtSprintf(g_szObjScenePath, "%s\\%s",
               mStringCStr(&mstrScenePath), mStringCStr(&mstrTmp));
    mStringFree(&mstrTmp);
    nopDebugStub();
    g_nObjScene = sceneLoadSen(g_szObjScenePath, NULL);

    configEnvGetString(&mstrTmp, pObjects, "char_scene_file"); /* @0x44f884 */
    fmtSprintf(g_szCharScenePath, "%s\\%s",
               mStringCStr(&mstrScenePath), mStringCStr(&mstrTmp));
    mStringFree(&mstrTmp);
    nopDebugStub();
    g_nCharScene = sceneLoadSen(g_szCharScenePath, NULL);
    scenSetDir("");                                          /* "" @0x4550d8 */

    wsprintf(szPath, "levels[%d]/startup", g_nLevelIdx);     /* @0x44f854 */
    pStartup = configEnvGetValue(&g_configEnvMaster, NULL, szPath);
    if (pStartup == NULL) {
        nopDebugStub();                                      /* "WARNING! No startup script..." @0x44f824 */
    }
    pCur = pStartup;
    do {
        MString mstrCmd;
        configNodeGetValue(&mstrCmd, pCur);
        commandDispatch(0, mStringCStr(&mstrCmd));
        mStringFree(&mstrCmd);
        pCur = configNextNode(&g_configEnvMaster, pCur);
    } while (pCur != NULL);

    /* Item slots: mesh field base 0x4583e4, stride 0x2c, bound 0x45890c. */
    for (i = 0; i < LEVEL_ITEM_SLOT_COUNT; i++) {
        pSlot = &g_apLevelItemSlots[i];
        wsprintf(szPath, "items[%d]", i);                    /* @0x44f818 */
        pCur = configEnvGetValue(&g_configEnvMaster, NULL, szPath);
        if (pCur == NULL) {
            fatalError("'items' block not found in cfg.");   /* @0x44f7f8 */
        }
        configEnvGetString(&mstrTmp, pCur, "name");          /* @0x44f7f0 */
        lstrcpyn(pSlot->szName, mStringCStr(&mstrTmp), (int)sizeof(pSlot->szName));
        mStringFree(&mstrTmp);
        if (g_nGameMode == 4) {
            pSlot->nMeshId = scenNameToId("CHECKFLAG");      /* @0x44f7c0 */
        } else {
            configEnvGetString(&mstrTmp, pCur, "mesh");      /* @0x44f7e8 */
            pSlot->nMeshId = scenNameToId(mStringCStr(&mstrTmp));
            mStringFree(&mstrTmp);
            if (pSlot->nMeshId == 0) {
                fatalError("Item %d's mesh is missing.", i); /* @0x44f7cc */
            }
        }
        pSlot->pSubObj = sceneNodeAllocChild(NULL, NULL, NULL, NULL, NULL);
        pSlot->pSceneObj = sceneryObjAlloc((SceneNode *)pSlot->pSubObj, 0, 0, 0, 0,
                                           0, 0, 0, (void *)(size_t)pSlot->nMeshId);
    }

    mStringFree(&mstrScenePath);
}
