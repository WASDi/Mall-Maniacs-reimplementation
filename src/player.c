#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "player.h"
#include "gx.h"
#include "charselect.h"
#include "config.h"
#include "menu.h"
#include "obj.h"
#include "options.h"
#include "levelselect.h"
#include "record.h"
#include "pool.h"
#include "stubs.h"
#include "time.h"
#include "util.h"
#include "custom_helpers.h"
#include "zone.h"

extern void *g_pSceneDetailGrid;   /* @0x45838c (defined in gameplay.c) */

/* g_nNavPtSearchCount @0x45d4dc — reset before every AI update pass. */
int g_nNavPtSearchCount;
/* g_nControllerIdx @0x4550d0 — chooses the pair of AI players to update. */
int g_nControllerIdx;

/* g_playerRecords @0x456210 — the 0x374-byte gameplay player records.
 * The controller view (original base 0x456524) is the embedded
 * PlayerRecord.ai member. */
PlayerRecord g_playerRecords[8];

/* Arrow/scene globals created by playerSetupSceneObjects @0x411550. */
SceneNode *g_pGoodsArrowObj;   /* @0x458110 VARUPIL arrow object (goods target) */
SceneNode *g_pCartArrowObj;    /* @0x458114 VAGNPIL arrow object (zone exit) */
SceneNode *g_pGoodsArrowMesh;  /* @0x458118 class mesh under g_pSceneRoot */
SceneNode *g_pCartArrowMesh;   /* @0x45811c class mesh under g_pSceneRoot */
/* g_camFollowBlock @0x4588f8 — camera-follow block. Its first four fields
 * ARE the globals g_pSceneRoot (0x4588f8), the follow node (0x4588fc),
 * g_pCamPosNode (0x458900) and g_pCamAimNode (0x458904); see scene.h. */
CameraFollowBlock g_camFollowBlock;
SceneNode *g_pMenuSceneRoot;   /* @0x458910 menu-scene root for the return path */
SceneNode *g_pMenuSceneChildA; /* @0x458918 */
SceneNode *g_pMenuSceneChildB; /* @0x45891c */

/* g_nCurrentItemId @0x458128 — mode 3 target item id, reset by
 * playerSetupRound (checked by the AI state machine). */
int g_nCurrentItemId;

/* g_nResultsScreen @0x458130 — results-screen gate in gameWorldUpdate. */
int g_nResultsScreen;
/* g_nCameraUpdateTick @0x458948 — camera-follow scheduler (mod 4 gate). */
int g_nCameraUpdateTick;

/* g_aflAiCtrlSpeed @0x44b230 — difficulty-scaled AI speed picked by
 * aiControllerCtor (indexed by g_nModeSel 0..2). */
const float g_aflAiCtrlSpeed[3] = { 0.72f, 0.84f, 0.96f };

/* syncAiAnimToSceneObj @0x4015a0 — retain an inactive AI player's last
 * animation state in its player record. */
int syncAiAnimToSceneObj(AiController *pCtrl)
{
    int nFrame = pCtrl->nSavedAnimationFrame;      /* raw dword copy into the float channel */
    int nTimer = pCtrl->nSavedAnimationTimer;
    memcpy(&pCtrl->pPlayerObj->flInputTurn, &nFrame, 4);   /* +0x2e0 */
    memcpy(&pCtrl->pPlayerObj->flInputAccel, &nTimer, 4);  /* +0x2e4 */
    return 1;
}

/* playerUpdateDispatch @0x4010e0 — update the selected pair of AI players
 * and synchronize every other AI player's saved animation state. */
int playerUpdateDispatch(void)
{
    int nPlayer;
    int nFirstUpdatedPlayer;

    if (g_bAiEnabled == 0) return 0;

    g_nNavPtSearchCount = 0;
    nFirstUpdatedPlayer = (g_nControllerIdx % 4) * 2;
    for (nPlayer = 0; nPlayer < g_nPlayerCount; nPlayer++) {
        PlayerRecord *pRecord = &g_playerRecords[nPlayer];

        if (pRecord->nControlType != 2) continue;
        if (nPlayer < nFirstUpdatedPlayer || nPlayer >= nFirstUpdatedPlayer + 2) {
            syncAiAnimToSceneObj(&pRecord->ai);
        } else {
            playerAiUpdate(&pRecord->ai);
        }
    }
    g_nControllerIdx++;
    return 1;
}

/* aiControllersInit @0x401040 — ctor every player's AiController (record
 * +0x314) with the record itself as the back-pointer argument, then reset
 * the rotating update index g_nControllerIdx @0x4550d0. */
int aiControllersInit(void) /* @0x401040 */
{
    int i;

    for (i = 0; i < g_nPlayerCount; i++) {
        aiControllerCtor(&g_playerRecords[i].ai, &g_playerRecords[i]);
    }
    g_nControllerIdx = 0;
    return 1;
}

/* aiControllerCtor @0x401090 — zero the controller state, store the owning
 * record at +0x00 and the difficulty speed at +0x44. Returns 1. */
int aiControllerCtor(AiController *pCtrl, PlayerRecord *pRecord) /* @0x401090 */
{
    pCtrl->nAiState = 0;
    pCtrl->pNavPoint = 0;
    pCtrl->field_0c = 0;
    pCtrl->field_10 = 0;
    pCtrl->pPlayerObj = pRecord;
    pCtrl->nSavedAnimationFrame = 0;
    pCtrl->nSavedAnimationTimer = 0;
    pCtrl->bFlag50 = 0;
    pCtrl->bFlag51 = 0;
    pCtrl->bFlag52 = 0;
    pCtrl->field_54 = 0;
    pCtrl->field_58 = 0;
    pCtrl->field_5c = 0;
    pCtrl->nCtrlSpeed = (int)g_aflAiCtrlSpeed[g_nModeSel];
    return 1;
}

/* g_kModePlayerCounts @0x44b610 — players per round, indexed
 * g_nLevelIdx*3 + g_nModeSel (rows by level, columns by difficulty). */
static const int g_kModePlayerCounts[12] = { /* @0x44b610 */
    2, 3, 4,   /* level 0 */
    2, 3, 4,   /* level 1 */
    3, 3, 4,   /* level 2 */
    3, 4, 4    /* level 3 */
};

/* playerSetupCharacters @0x41b6e0 — assign characters to all players before a
 * round starts (called by the stateLevelInit0..4 handlers). The player count
 * comes from g_kModePlayerCounts when not already decided. getGameTime()%150
 * rand() calls are burned first so the rolls below do not correlate with
 * menu-time rolls. The local player keeps its charselect pick and gets
 * nControlType 0 with cart 1; every other slot rolls a unique character
 * rand()%(g_nLevelCount+6) (re-rolled above 9 and on collision with an
 * earlier player), gets nControlType 2 (AI) and cart 0. Every record's
 * start-position index is its slot. */
void playerSetupCharacters(void) /* @0x41b6e0 */
{
    int i;

    if (g_nPlayerCount == 0) {                                             /* @0x41b6e0 */
        g_nPlayerCount = g_kModePlayerCounts[g_nLevelIdx * 3 + g_nModeSel]; /* @0x41b6f9 */
    }

    {
        int nBurn = getGameTime() % 150;                                   /* @0x40dfe0 @0x41b707 */

        while (nBurn-- > 0) {
            rand();                                                        /* @0x43ea7c @0x41b71a */
        }
    }

    for (i = 0; i < g_nPlayerCount; i++) {                                 /* @0x41b731 */
        PlayerRecord *pRec = &g_playerRecords[i];

        if (i == g_nLocalPlayerIdx) {                                      /* @0x41b73b */
            pRec->nControlType = 0;                                        /* @0x41b786 */
            pRec->nCartIdx = 1;                                            /* @0x41b78c */
        } else {
            int nCharIdx;
            int j;

            do {
                do {
                    nCharIdx = rand() % (g_nLevelCount + 6);               /* @0x45a6f4 @0x41b74e */
                } while (nCharIdx > 9);                                    /* @0x41b753 */
                for (j = 0; j < i; j++) {                                  /* @0x41b765 */
                    if (nCharIdx == g_playerRecords[j].nCharIdx) {
                        break;                                             /* @0x41b76c */
                    }
                }
            } while (j != i);                                              /* @0x41b77b */
            pRec->nCharIdx = nCharIdx;                                     /* @0x41b75d */
            pRec->nControlType = 2;                                        /* @0x41b798 */
            pRec->nCartIdx = 0;                                            /* @0x41b79e */
        }
        pRec->nStartPosIdx = i;                                            /* @0x41b7ae */
    }
}

/* playerSetupRound @0x410e90 — per-player round setup. Reads the [master]
 * physics constants (acc, friction, rotate_acc, acc_WC, rotate_acc_WC) and
 * per-player objects/characters[%d] + objects/carts[%d] object_name
 * entries. Per player it resets animation state, frees the three WorldNode
 * sub-objects, rolls a random offline shopping list (ids 1..24 with one
 * re-roll on collision), fills mode-3/mode-4 lists, allocates the three
 * WorldNode sub-objects, copies the character stats and precomputes the
 * walk/cart acceleration constants, then runs aiControllersInit. */
void playerSetupRound(void) /* @0x410e90 */
{
    float flAcc;
    float flFriction;
    float flRotAcc;
    float flAccWC;
    float flRotAccWC;
    int i;
    char szBuf[0x30];
    MString mstrValue;

    appLog("Loading player config...");
    {
        ConfigNode *pMaster = configEnvGetValue(&g_configEnvMaster, NULL, "master"); /* @0x436550 @0x410ec2 */
        if (pMaster == NULL) {
            fatalError("'master' block not found in cfg.");            /* @0x44f690 @0x410ed2 */
        }
        flAcc = (float)configEnvGetDouble2(&g_configEnvMaster, pMaster, "acc");         /* @0x44fa3c @0x410edf */
        flFriction = (float)configEnvGetDouble2(&g_configEnvMaster, pMaster, "friction"); /* @0x44fa30 @0x410ef3 */
        flRotAcc = (float)configEnvGetDouble2(&g_configEnvMaster, pMaster, "rotate_acc"); /* @0x44fa24 @0x410f07 */
        flAccWC = (float)configEnvGetDouble2(&g_configEnvMaster, pMaster, "acc_WC");    /* @0x44fa1c @0x410f1b */
        flRotAccWC = (float)configEnvGetDouble2(&g_configEnvMaster, pMaster, "rotate_acc_WC"); /* @0x44fa0c @0x410f2f */
    }

    for (i = 0; i < g_nPlayerCount; i++) {
        PlayerRecord *pRec = &g_playerRecords[i];
        ConfigNode *pObjects;
        ConfigNode *pNode;

        pObjects = configEnvGetValue(&g_configEnvMaster, NULL, "objects"); /* @0x44f8e4 @0x410f6b */
        if (pObjects == NULL) {
            fatalError("'objects' block not found in cfg.");               /* @0x44f8c0 @0x410f83 */
        }
        pRec->flInputAccel = 0;                                         /* @0x410f90 */
        pRec->flInputTurn = 0;                                         /* @0x410f93 */
        pRec->field_2e8 = 0;                                               /* @0x410f95 */
        pRec->field_2f0 = 0;                                               /* @0x410f98 */
        pRec->field_2fc = 0;                                               /* @0x410f9b */
        if (pRec->pSubObjA != NULL) {                                      /* @0x410f9e */
            objDtor((WorldNode *)pRec->pSubObjA);                          /* @0x402ab0 @0x410faa */
            memFreeDirect(pRec->pSubObjA);                                 /* @0x43dd37 @0x410fb0 */
        }
        if (pRec->pSubObjB != NULL) {                                      /* @0x410fb8 */
            objDtor((WorldNode *)pRec->pSubObjB);                          /* @0x410fc1 */
            memFreeDirect(pRec->pSubObjB);                                 /* @0x410fc7 */
        }
        if (pRec->pSubObjC != NULL) {                                      /* @0x410fcf */
            objDtor((WorldNode *)pRec->pSubObjC);                          /* @0x410fd8 */
            memFreeDirect(pRec->pSubObjC);                                 /* @0x410fde */
        }
        if (netIsActive() == 0) {                                          /* @0x45e59c @0x410fe6 */
            int nBurn = getGameTime() % 150;                               /* @0x40dfe0 @0x410fee */
            int j;

            while (nBurn-- > 0) {                                          /* @0x410fff */
                rand();                                                    /* @0x43ea7c @0x411001 */
            }
            pRec->nListProgress = 0;                                       /* @0x41100d */
            for (j = 0; j < 10; j++) {                                     /* @0x411013 */
                int k;

                pRec->anListIds[j] = rand() % 24 + 1;                      /* @0x411018 */
                for (k = 0; k < j; k++) {                                  /* @0x41102e */
                    if (pRec->anListIds[j] == pRec->anListIds[k]) {        /* @0x411036 */
                        pRec->anListIds[j] = rand() % 24 + 1;              /* single re-roll @0x41103f */
                        k = -1;                                            /* restart compare @0x41104d */
                    }
                }
                pRec->abListTaken[j] = 0;                                  /* @0x411057 */
            }
        }
        if (g_nGameMode == 3) {                                            /* @0x458120 @0x411063 */
            g_nCurrentItemId = 0;                                          /* @0x458128 @0x41106d */
            pRec->nListProgress = 0;                                       /* @0x411073 */
            {
                int j;

                for (j = 0; j < 10; j++) {                                 /* @0x41107f */
                    pRec->anListIds[j] = 0;                                /* @0x411084 */
                    pRec->abListTaken[j] = 1;                              /* @0x411087 */
                }
            }
        } else if (g_nGameMode == 4) {                                     /* @0x411095 */
            int j;

            for (j = 0; j < 3; j++) {                                      /* @0x41109c */
                pRec->anListIds[j] = 0xc9 + j;                             /* 201+j @0x4110a2 */
                pRec->abListTaken[j] = 0;                                  /* @0x4110ac */
            }
            for (; j < 10; j++) {                                          /* @0x4110b6 */
                pRec->anListIds[j] = 0xc8;                                 /* 200 @0x4110cf */
                pRec->abListTaken[j] = 1;                                  /* @0x4110d6 */
            }
        }
        pRec->nHeldItemId = 0;                                             /* @0x4110f7 */
        pRec->field_248 = 0.0f;                                            /* +0x248 @0x4110f9 */
        pRec->flPosSpeed = 0.0f;                                           /* +0x234 @0x4110ff */
        pRec->field_228_pad = 0.0f;                                        /* +0x228 @0x411105 */
        pRec->flAccFactor = 0.0f;                                          /* +0x22c @0x411107 */
        pRec->flPosTurnAccum = 0.0f;                                       /* +0x244 @0x41110d */
        pRec->field_238_pad = 0.0f;                                        /* +0x238 @0x41110d */
        pRec->flRotFactor = 0.0f;                                          /* +0x23c @0x41110d */
        pRec->field_254 = 0.0f;                                            /* +0x254 @0x411128 */
        pRec->flPosHeading = 0.0f;                                         /* +0x258 @0x411130 */
        pRec->flPosVelLen = 0.0f;                                          /* +0x25c @0x41111f */
        pRec->flPosVelAng = 0.0f;                                          /* +0x260 @0x411125 */
        memcpy(&pRec->flAccSpeed, &pRec->field_228_pad, 0x40);             /* +0x1e8 <- +0x228 @0x411136 */
        memcpy(&pRec->flCartAccSpeed, &pRec->field_228_pad, 0x40);         /* +0x268 <- +0x228 @0x411144 */
        {
            void *pNew = malloc(0x48);                                     /* operator_new @0x43dd42 @0x411146 */
            pRec->pSubObjA = (pNew != NULL)                                /* +0x224 @0x41117f */
                ? worldNodeCtor(pNew, 0, 0, 0, 0, &pRec->mstrCharacterName) /* @0x402a20 @0x41116a */
                : NULL;                                                    /* @0x411171 */
        }
        {
            void *pNew = malloc(0x48);                                     /* @0x411185 */
            pRec->pSubObjB = (pNew != NULL)                                /* +0x264 @0x4111ba */
                ? worldNodeCtor(pNew, 0, 0, 0, 0, NULL)                    /* @0x4111a8 */
                : NULL;                                                    /* @0x4111af */
        }
        {
            void *pNew = malloc(0x48);                                     /* @0x4111bd */
            pRec->pSubObjC = (pNew != NULL)                                /* +0x2a4 @0x4111ee */
                ? worldNodeCtor(pNew, 0, 0, 0, 0, &pRec->mstrCharacterName) /* @0x4111e5 */
                : NULL;                                                    /* @0x4111ec */
        }
        pRec->field_174 = 1;                                               /* @0x4111f1 */
        wsprintfA(szBuf, "characters[%d]", pRec->nCharIdx);           /* @0x44f9fc @0x411201 */
        pNode = configEnvGetValue(&g_configEnvMaster, pObjects, szBuf);    /* @0x436560 @0x411227 */
        if (pNode == NULL) {
            fatalError("'objects/characters[%d]' not found in cfg.",       /* @0x44f9d0 @0x41123e */
                       pRec->nCharIdx);
        }
        configEnvGetString(&mstrValue, pNode, "object_name");              /* @0x44f9c4 @0x436990 @0x411256 */
        mStringAssignCopy(&pRec->mstrCharacterName, &mstrValue);           /* @0x435440 @0x41126f */
        mStringFree(&mstrValue);                                           /* @0x435430 @0x41127f */
        if (netIsActive() == 0) {                                          /* @0x426ed0 @0x411284 */
            lstrcpyA(pRec->szCharName, g_apCharNames[pRec->nCharIdx]);/* @0x45013c @0x41129b */
        }
        pRec->nStatSpeed = g_kCharStatSpeed[pRec->nCharIdx];          /* @0x4501b4 @0x4112c5 */
        pRec->nStatStrength = g_kCharStatStrength[pRec->nCharIdx];    /* @0x4501b8 @0x4112db */
        pRec->nStatAgility = g_kCharStatAgility[pRec->nCharIdx];      /* @0x4501bc @0x4112f1 */
        pRec->nCheckoutProgress = 0;                                               /* @0x411302 */
        wsprintfA(szBuf, "carts[%d]", pRec->nCartIdx);                     /* @0x44f9b8 @0x411313 */
        pNode = configEnvGetValue(&g_configEnvMaster, pObjects, szBuf);    /* @0x411331 */
        if (pNode == NULL) {
            fatalError("'objects/carts[%d]' not found in cfg.",            /* @0x44f990 @0x411348 */
                       pRec->nCartIdx);
        }
        configEnvGetString(&mstrValue, pNode, "object_name");              /* @0x411360 */
        mStringAssignCopy(&pRec->mstrCartName, &mstrValue);                /* @0x411379 */
        mStringFree(&mstrValue);                                           /* @0x41138d */
        pRec->flPosSpeed = (float)configEnvGetDouble2(&g_configEnvMaster, pNode, "friction");       /* +0x234 @0x4113a2 */
        pRec->flPosTurnAccum = (float)configEnvGetDouble2(&g_configEnvMaster, pNode, "friction");   /* +0x244 @0x4113b8 */
        /* walk block: acc = (speed*0.7 + 100) * master acc (0x44b59c = 0.7f,
         * 0x44b598 = 100.0f — the decompiler's "0.06/0.7" naming is wrong). */
        pRec->flAccSpeed = ((float)pRec->nStatSpeed * 0.7f + 100.0f) * flAcc;          /* @0x4113be */
        pRec->flRotAccSpeed = ((float)pRec->nStatAgility * 0.1f + 0.5f) * flRotAcc;    /* @0x4113e0 */
        pRec->flFriction = flFriction;                                     /* +0x1ec @0x411402 */
        pRec->flCurSpeed = flFriction;                                     /* +0x1f4 @0x411408 */
        pRec->flAccFric = pRec->flAccSpeed / (1.0f - pRec->flFriction);    /* @0x411414 */
        pRec->flRotAccFric = pRec->flRotAccSpeed / (1.0f - pRec->flFriction); /* @0x41142c */
        pRec->flCurAccFric = pRec->flAccFric;                              /* +0x230 @0x41143e */
        pRec->flCurRotAccFric = pRec->flRotAccFric;                        /* +0x240 @0x41144a */
        /* cart block: same shape with the WC constants and averaged friction. */
        pRec->flCartAccSpeed = ((float)pRec->nStatSpeed * 0.7f + 100.0f) * flAccWC;    /* @0x411456 */
        pRec->flCartRotAccSpeed = ((float)pRec->nStatAgility * 0.1f + 0.5f) * flRotAccWC; /* @0x41146f */
        pRec->flCartFrictionB = (flFriction + pRec->flPosTurnAccum) * 0.5f;            /* +0x27c @0x41148c */
        pRec->flCartFriction = (flFriction + pRec->flPosSpeed) * 0.5f;                 /* +0x270 @0x41149f */
        pRec->flCartCurSpeed = pRec->flCartAccSpeed / (1.0f - pRec->flCartFriction);   /* +0x274 @0x4114b0 */
        pRec->flCartRotAccFric = pRec->flCartRotAccSpeed / (1.0f - pRec->flCartFrictionB); /* +0x280 @0x4114c6 */
        appLog(" Player(%d) ch<%s> ca<%s>", i,                             /* @0x44f974 @0x4114e3 */
               mStringCStr(&pRec->mstrCharacterName),
               mStringCStr(&pRec->mstrCartName));
    }
    aiControllersInit();                                                   /* @0x401040 @0x411527 */
}

/* playerSetupSceneObjects @0x411550 — create the two shared arrow objects
 * (VAGNPIL cart-exit arrow, VARUPIL goods arrow), then per player: the
 * character scene node with hidden SPLASH shadow, the character mesh plus
 * up to two "_%d<name>" sub-meshes registered as a detail-grid row, the 11
 * animation sets bound to the three mesh slots, and the cart object with
 * shadow and two child nodes. Finally allocates the game scene root with
 * the camera nodes and arrow class meshes and the menu-scene root. */
void playerSetupSceneObjects(void) /* @0x411550 */
{
    int i;
    char szBuf[0x80];
    int nMesh;

    appLog("Creating objects...");                                         /* @0x44fbf4 @0x41155f */
    nMesh = scenNameToId("VAGNPIL");                                       /* @0x44fbec @0x431ed0 @0x41156a */
    if (nMesh == 0) {
        fatalError("Mesh 'VAGNPIL' not found.");                           /* @0x44fbd0 @0x41157d */
    }
    g_pCartArrowObj = (SceneNode *)sceneryObjAlloc(NULL, 0, 0, 0, 0, 0, 0, 0, (void *)(size_t)nMesh); /* @0x430200 @0x41158e */
    nMesh = scenNameToId("VARUPIL");                                       /* @0x44fbc8 @0x41159d */
    if (nMesh == 0) {
        fatalError("Object 'VARUPIL' not found.");                         /* @0x44fbac @0x4115b0 */
    }
    g_pGoodsArrowObj = (SceneNode *)sceneryObjAlloc(NULL, 0, 0, 0, 0, 0, 0, 0, (void *)(size_t)nMesh); /* @0x4115c1 */

    for (i = 0; i < g_nPlayerCount; i++) {
        PlayerRecord *pRec = &g_playerRecords[i];
        SceneNode *pShadow;
        int nCount;
        int slot;

        pRec->pCharSceneNode = (SceneNode *)sceneNodeAllocChild(NULL, NULL, NULL, NULL, NULL); /* @0x4319e0 @0x4115f7 */
        nMesh = scenNameToId("SPLASH");                                    /* @0x44fba4 @0x411604 */
        if (nMesh == 0) {
            fatalError("SPLASH mesh not found.");                          /* @0x44fb8c @0x411617 */
        }
        pShadow = (SceneNode *)sceneryObjAlloc(pRec->pCharSceneNode, 0, 0, -0x4b0, 0, 0, 0, 0, (void *)(size_t)nMesh); /* @0x411635 */
        pRec->pCharShadowNode = pShadow;                                   /* @0x41164d */
        sceneryObjAlloc(pShadow, 0, 0, 0, 0, 0, 16000, 0, (void *)(size_t)nMesh); /* result unused @0x411650 */
        sceneNodeSetHiddenFlag(pRec->pCharShadowNode, 2);             /* @0x4305c0 @0x41165e */
        nMesh = scenNameToId(mStringCStr(&pRec->mstrCharacterName));       /* @0x4351d0 @0x41166e */
        if (nMesh == 0) {
            fatalError("Character mesh '%s' for player %d not found.",     /* @0x44fb5c @0x41168e */
                       mStringCStr(&pRec->mstrCharacterName), i);
        }
        pRec->pCharSceneObj = (SceneNode *)sceneryObjAlloc(pRec->pCharSceneNode, 0, 0, 0, 0, 0, 0, 0, (void *)(size_t)nMesh); /* @0x4116a9 */
        pRec->apMeshSlots[0] = pRec->pCharSceneObj;                        /* @0x4116b1 */
        nCount = 1;                                                        /* @0x4116b6 */
        for (slot = 1; slot < 3; slot++) {                                 /* @0x4116be */
            wsprintfA(szBuf, "_%d%s", slot, mStringCStr(&pRec->mstrCharacterName)); /* @0x44fb54 @0x4116d1 */
            nMesh = scenNameToId(szBuf);                                   /* @0x4116dc */
            if (nMesh == 0) {                                              /* @0x4116e6 */
                if (slot < 3) {
                    int k;

                    for (k = slot; k < 3; k++) {                           /* @0x411718 */
                        pRec->apMeshSlots[k] = NULL;                       /* @0x411733 */
                    }
                }
                break;
            }
            pRec->apMeshSlots[slot] = (SceneNode *)sceneryObjAlloc(pRec->pCharSceneNode, 0, 0, 0, 0, 0, 0, 0, (void *)(size_t)nMesh); /* @0x4116fb */
            nCount = slot + 1;                                             /* @0x411709 */
        }
        sceneDetailGridAddRow((SceneDetailGrid *)g_pSceneDetailGrid, /* @0x42b000 @0x41173d */
                              (int *)pRec->apMeshSlots, nCount);
        pRec->apAnmSets[0] = anmSetAlloc(anmLoadFile("anim\\s_pick1.anm", NULL, pRec->pCharSceneObj));   /* @0x44fb40 @0x411757 */
        pRec->apAnmSets[1] = anmSetAlloc(anmLoadFile("anim\\s_pick2.anm", NULL, pRec->pCharSceneObj));   /* @0x44fb2c @0x411772 */
        pRec->apAnmSets[2] = anmSetAlloc(anmLoadFile("anim\\s_flpick1.anm", NULL, pRec->pCharSceneObj)); /* @0x44fb18 @0x41178d */
        pRec->apAnmSets[3] = anmSetAlloc(anmLoadFile("anim\\s_flpick2.anm", NULL, pRec->pCharSceneObj)); /* @0x44fb04 @0x4117ab */
        pRec->apAnmSets[4] = anmSetAlloc(anmLoadFile("anim\\s_throw1.anm", NULL, pRec->pCharSceneObj));  /* @0x44faf0 @0x4117c6 */
        pRec->apAnmSets[5] = anmSetAlloc(anmLoadFile("anim\\s_throw2.anm", NULL, pRec->pCharSceneObj));  /* @0x44fadc @0x4117e1 */
        pRec->apAnmSets[6] = anmSetAlloc(anmLoadFile("anim\\s_run.anm", NULL, pRec->pCharSceneObj));     /* @0x44facc @0x4117fc */
        pRec->apAnmSets[7] = anmSetAlloc(anmLoadFile("anim\\s_stand.anm", NULL, pRec->pCharSceneObj));   /* @0x44fab8 @0x41181a */
        pRec->apAnmSets[8] = anmSetAlloc(anmLoadFile("anim\\s_grab.anm", NULL, pRec->pCharSceneObj));    /* @0x44faa8 @0x411835 */
        pRec->apAnmSets[9] = anmSetAlloc(anmLoadFile("anim\\s_oops.anm", NULL, pRec->pCharSceneObj));    /* @0x44fa98 @0x411850 */
        pRec->apAnmSets[10] = anmSetAlloc(anmLoadFile("anim\\s_winner.anm", NULL, pRec->pCharSceneObj)); /* @0x44fa84 @0x41186e */
        for (slot = 0; slot < 3; slot++) {                                 /* @0x411874 */
            int k;

            for (k = 0; k < 11; k++) {                                     /* @0x411878 */
                anmSetMeshSlot(pRec->apAnmSets[k], pRec->apMeshSlots[slot], slot); /* @0x434530 @0x411883 */
            }
        }
        nMesh = scenNameToId(mStringCStr(&pRec->mstrCartName));            /* +0x08 @0x4351d0 @0x411946 */
        if (nMesh == 0) {
            fatalError("Cart mesh '%s' for player %d not found.",          /* @0x44fa5c @0x411966 */
                       mStringCStr(&pRec->mstrCartName), i);
        }
        pRec->pCartSceneObj = (SceneNode *)sceneryObjAlloc(NULL, 0, 0, 0, 0, 0, 0, 0, (void *)(size_t)nMesh); /* @0x41197f */
        nMesh = scenNameToId("SPLASH");                                    /* @0x41198c */
        if (nMesh == 0) {
            fatalError("SPLASH mesh not found.");                          /* @0x41199f */
        }
        pShadow = (SceneNode *)sceneryObjAlloc(pRec->pCartSceneObj, 0, 0, -0x4b0, 0, 0, 0, 0, (void *)(size_t)nMesh); /* @0x4119bd */
        pRec->pCartShadowNode = pShadow;                                   /* +0x24 @0x4119d5 */
        sceneryObjAlloc(pShadow, 0, 0, 0, 0, 0, 16000, 0, (void *)(size_t)nMesh); /* result unused @0x4119d8 */
        sceneNodeSetHiddenFlag(pRec->pCartShadowNode, 2);             /* @0x4119e6 */
        pRec->pCartChildA = (SceneNode *)sceneNodeAllocChild(pRec->pCartSceneObj, NULL, NULL, NULL, NULL); /* +0x44 @0x4319e0 @0x4119f7 */
        pRec->pCartChildB = (SceneNode *)sceneNodeAllocChild(pRec->pCartSceneObj, NULL, NULL, NULL, NULL); /* +0x48 @0x411a0b */
    }

    g_pSceneRoot = (SceneNode *)sceneNodeAlloc((void *)0x3f800000, (void *)0x41200000, /* @0x4318e0 @0x411a60 */
                                               (void *)0x7530, 0, 0, 0x1000, 0x1000);
    sceneDetailGridSetRoot((SceneDetailGrid *)g_pSceneDetailGrid, g_pSceneRoot); /* @0x42b350 @0x411a74 */
    g_pCamPosNode = (SceneNode *)sceneNodeAllocChild(NULL, NULL, NULL, NULL, NULL); /* @0x411a83 */
    g_pCamAimNode = (SceneNode *)sceneNodeAllocChild(NULL, NULL, NULL, NULL, NULL); /* @0x411a97 */
    g_pGoodsArrowMesh = (SceneNode *)sceneNodeAllocChild(g_pSceneRoot, NULL,  /* @0x411ab9 */
                                                         (void *)0xfffffce0, (void *)0x24e, (void *)0x4b0);
    sceneObjSetClassMesh((int)g_pGoodsArrowObj, g_pGoodsArrowMesh, 0, 3);  /* @0x430db0 @0x411ace */
    g_pCartArrowMesh = (SceneNode *)sceneNodeAllocChild(g_pSceneRoot, NULL,   /* @0x411aee */
                                                        (void *)0x320, (void *)0x24e, (void *)0x4b0);
    sceneObjSetClassMesh((int)g_pCartArrowObj, g_pCartArrowMesh, 0, 3);    /* @0x411b04 */
    g_pMenuSceneRoot = (SceneNode *)sceneNodeAlloc((void *)0x3f800000, (void *)0x41200000, /* @0x411b26 */
                                                   (void *)0x61a8, 0, 0, 0x400, 0x800);
    g_pMenuSceneChildA = (SceneNode *)sceneNodeAllocChild(NULL, NULL, NULL, NULL, NULL); /* @0x411b3d */
    g_pMenuSceneChildB = (SceneNode *)sceneNodeAllocChild(NULL, NULL, NULL, NULL, NULL); /* @0x411b51 */
}

/* levelObjectsCartsCameraInit @0x411b70 — per-player cart/character world
 * placement followed by the unconditional camera setup. The per-player pass
 * (config objects/carts[%d] + objects/characters[%d] object_pos with the
 * objTurretAdd/nodeAddChildMesh shadow-mesh cluster and the
 * levels[%d]/start_positions[%d] nodeSetTransformFromChannels placement)
 * is TODO — it only runs when g_nPlayerCount > 0, which requires
 * playerSetupCharacters @0x41b760 first. The camera pass below is complete:
 * config objects/camera pos/aim places g_pCamPosNode/g_pCamAimNode, the
 * position/orientation are copied onto the camera root, it faces the aim
 * point and rot_speed/rot_max land in the follow block. */
void levelObjectsCartsCameraInit(void) /* @0x411b70 */
{
    ConfigNode *pCam;
    ConfigNode *pNode;
    int anPosCfg[3];
    int anAimCfg[3];
    short nAng[3];
    int anPos[3];
    int i;

    /* Per-player pass @0x411b9c..0x41241d (runs only when players exist):
     * cart + character collision turret/mesh clusters from the config
     * object_pos entries, handle_pos onto the cart child nodes, the +0x1c
     * walkability flags and the levels[%d]/start_positions[%d] placement
     * of all three WorldNode sub-objects. */
    for (i = 0; i < g_nPlayerCount; i++) {                                /* @0x411b9c */
        PlayerRecord *pRec = &g_playerRecords[i];
        ConfigNode *pCfg;
        ConfigNode *pVal;
        char szKey[0x40];
        int anObjPos[3];
        short anObjRot[3];
        float flExtentA;
        float flExtentB;

        wsprintfA(szKey, "objects/carts[%d]", pRec->nCartIdx);   /* @0x44fd84 @0x411bba */
        pCfg = configEnvGetValue(&g_configEnvMaster, NULL, szKey);        /* @0x411b6a */
        if (pCfg == NULL) {
            fatalError("'objects/carts[%d]' not found in cfg.",           /* @0x44f990 */
                       pRec->nCartIdx);
        }
        pVal = configEnvGetValue(&g_configEnvMaster, pCfg, "object_pos"); /* @0x44fd78 @0x411be8 */
        if (pVal == NULL) {
            fatalError("'objects/carts[%d]/object_pos' not found in cfg.", /* @0x44fd44 */
                       pRec->nCartIdx);
        }
        anObjPos[0] = (int)configEnvGetDouble(pVal);                      /* @0x4369f0 @0x411c16 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x436870 @0x411c2a */
        anObjPos[1] = (int)configEnvGetDouble(pVal);                      /* @0x411c37 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x411c4b */
        anObjPos[2] = (int)configEnvGetDouble(pVal);                      /* @0x411c58 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x411c6c */
        anObjRot[0] = (short)(int)configEnvGetDouble(pVal);               /* @0x411c79 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x411c8e */
        anObjRot[1] = (short)(int)configEnvGetDouble(pVal);               /* @0x411c9a */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x411cb0 */
        anObjRot[2] = (short)(int)configEnvGetDouble(pVal);               /* @0x411cbb */

        /* collision half-extents from strength @0x411cc5 (0.333333/75/90 +
         * 1.0 constants @0x44b5a8/a4/a0/260) */
        flExtentA = (float)pRec->nStatStrength * 0.333333f * 75.0f + 90.0f;
        flExtentB = ((float)pRec->nStatStrength * 0.333333f + 1.0f) * 75.0f;

        /* cart body cluster on pSubObjB (parent = cart scene object) @0x411cd3 */
        objTurretAdd((WorldNode *)pRec->pSubObjB, 0, 0, 0, 0,             /* @0x405280 @0x411d09 */
                     (int)pRec->pCartSceneObj);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjB, 0, 0, 150, 270.0f, 75.0f,      /* @0x4053a0 @0x411d31 */
                         (int)pRec->pCartSceneObj, 240.0f, 1);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjB, 0, 0, -150, 300.0f, 75.0f,     /* @0x411d59 */
                         (int)pRec->pCartSceneObj, 240.0f, 1);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjB, 190, 0, 300, 20.0f, 75.0f,     /* @0x411d84 */
                         (int)pRec->pCartSceneObj, 240.0f, 1);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjB, -190, 0, 300, 20.0f, 75.0f,    /* @0x411daf */
                         (int)pRec->pCartSceneObj, 240.0f, 1);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjB, 240, 0, -300, 20.0f, 75.0f,    /* @0x411dda */
                         (int)pRec->pCartSceneObj, 240.0f, 1);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjB, -240, 0, -300, 20.0f, 75.0f,   /* @0x411e05 */
                         (int)pRec->pCartSceneObj, 240.0f, 1);
        objTurretSetValue((WorldNode *)pRec->pSubObjB, (int)pRec->pCartSceneObj,     /* @0x405370 @0x411e17 */
                          1, 2);

        /* cart world cluster on pSubObjC at carts[%d]/object_pos @0x411e39 */
        objTurretAdd((WorldNode *)pRec->pSubObjC, anObjPos[0], anObjPos[1],          /* @0x411e39 */
                     anObjPos[2], anObjRot[1], (int)pRec->pCartSceneObj);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjC, anObjPos[0], anObjPos[1],      /* @0x411e6d */
                         anObjPos[2] + 150, 270.0f, flExtentB,
                         (int)pRec->pCartSceneObj, 240.0f, 1);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjC, anObjPos[0], anObjPos[1],      /* @0x411e9d */
                         anObjPos[2] - 150, 300.0f, flExtentB,
                         (int)pRec->pCartSceneObj, 300.0f, 1);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjC, anObjPos[2] + 190, anObjPos[1],/* @0x411ed2 */
                         anObjPos[2] + 300, 20.0f, flExtentB,
                         (int)pRec->pCartSceneObj, 240.0f, 1);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjC, anObjPos[2] - 190, anObjPos[1],/* @0x411f07 */
                         anObjPos[2] + 300, 20.0f, flExtentB,
                         (int)pRec->pCartSceneObj, 240.0f, 1);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjC, anObjPos[2] + 240, anObjPos[1],/* @0x411f3d */
                         anObjPos[2] - 300, 20.0f, flExtentB,
                         (int)pRec->pCartSceneObj, 240.0f, 1);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjC, anObjPos[2] - 240, anObjPos[1],/* @0x411f72 */
                         anObjPos[2] - 300, 20.0f, flExtentB,
                         (int)pRec->pCartSceneObj, 240.0f, 1);
        objTurretSetValue((WorldNode *)pRec->pSubObjC, (int)pRec->pCartSceneObj,     /* @0x411f84 */
                          1, 2);

        /* carts[%d]/handle_pos — two {x,y,z} triples onto the cart children @0x411f89 */
        pCfg = configEnvGetValue(&g_configEnvMaster, pCfg, "handle_pos"); /* @0x44fd38 @0x411f89 */
        if (pCfg == NULL) {
            fatalError("'objects/carts[%d]/handle_pos' not found in cfg.", /* @0x44fd04 */
                       pRec->nCartIdx);
        }
        anObjPos[0] = (int)configEnvGetDouble(pCfg);                      /* @0x411fb7 */
        pCfg = configNextNode(&g_configEnvMaster, pCfg);                  /* @0x411fcb */
        anObjPos[1] = (int)configEnvGetDouble(pCfg);                      /* @0x411fd8 */
        pCfg = configNextNode(&g_configEnvMaster, pCfg);                  /* @0x411fec */
        anObjPos[2] = (int)configEnvGetDouble(pCfg);                      /* @0x411ff9 */
        pCfg = configNextNode(&g_configEnvMaster, pCfg);                  /* @0x41200d */
        sceneNodeSetPos(pRec->pCartChildB, anObjPos, 2);                  /* @0x431590 @0x41201f */
        anObjPos[0] = (int)configEnvGetDouble(pCfg);                      /* @0x41202d */
        pCfg = configNextNode(&g_configEnvMaster, pCfg);                  /* @0x412041 */
        anObjPos[1] = (int)configEnvGetDouble(pCfg);                      /* @0x41204e */
        pCfg = configNextNode(&g_configEnvMaster, pCfg);                  /* @0x412062 */
        anObjPos[2] = (int)configEnvGetDouble(pCfg);                      /* @0x41206f */
        pCfg = configNextNode(&g_configEnvMaster, pCfg);                  /* @0x412083 */
        sceneNodeSetPos(pRec->pCartChildA, anObjPos, 2);                  /* @0x412093 */

        /* objects/characters[nCharIdx]/object_pos @0x412098 */
        wsprintfA(szKey, "objects/characters[%d]", pRec->nCharIdx);       /* @0x44fcec @0x412098 */
        pCfg = configEnvGetValue(&g_configEnvMaster, NULL, szKey);        /* @0x4120ba */
        if (pCfg == NULL) {
            fatalError("'objects/characters[%d]' not found in cfg.",      /* @0x44f9d0 */
                       pRec->nCharIdx);
        }
        pVal = configEnvGetValue(&g_configEnvMaster, pCfg, "object_pos"); /* @0x44fd78 @0x4120d9 */
        anObjPos[0] = (int)configEnvGetDouble(pVal);                      /* @0x4120f1 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x412105 */
        anObjPos[1] = (int)configEnvGetDouble(pVal);                      /* @0x412112 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x412126 */
        anObjPos[2] = (int)configEnvGetDouble(pVal);                      /* @0x412133 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x412147 */
        anObjRot[0] = (short)(int)configEnvGetDouble(pVal);               /* @0x412154 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x412169 */
        anObjRot[1] = (short)(int)configEnvGetDouble(pVal);               /* @0x412176 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x41218b */
        anObjRot[2] = (short)(int)configEnvGetDouble(pVal);               /* @0x412196 */
        configNextNode(&g_configEnvMaster, pVal);                         /* @0x41218b' */

        /* character body cluster on pSubObjA + world cluster on pSubObjC @0x4121a0 */
        objTurretAdd((WorldNode *)pRec->pSubObjA, 0, 0, 0, 0,             /* @0x4121b7 */
                     (int)pRec->pCharSceneNode);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjA, 0, 0, 0, flExtentA, 230.0f,    /* @0x4121dd */
                         (int)pRec->pCharSceneNode, 300.0f, 1);
        objTurretAdd((WorldNode *)pRec->pSubObjC, anObjPos[0], anObjPos[1],          /* @0x412200 */
                     anObjPos[2], anObjRot[1], (int)pRec->pCharSceneNode);
        nodeAddChildMesh((WorldNode *)pRec->pSubObjC, anObjPos[0], anObjPos[1],      /* @0x41222b */
                         anObjPos[2], 230.0f, flExtentA,
                         (int)pRec->pCharSceneNode, 300.0f, 1);

        if (pRec->field_174 == 0) {                                           /* @0x412230 */
            ((WorldNode *)pRec->pSubObjA)->field_1c = 1;                      /* @0x412245 */
            ((WorldNode *)pRec->pSubObjB)->field_1c = 1;                      /* @0x41224e */
        } else {
            ((WorldNode *)pRec->pSubObjC)->field_1c = 1;                      /* @0x412259 */
        }

        /* levels[%d]/start_positions[nStartPosIdx] placement @0x412260 */
        wsprintfA(szKey, "levels[%d]", g_nLevelIdx);                      /* @0x44f7b4 @0x412260 */
        pCfg = configEnvGetValue(&g_configEnvMaster, NULL, szKey);        /* @0x412281 */
        if (pCfg == NULL) {
            fatalError("'levels[%d]' not found in cfg.", g_nLevelIdx);    /* @0x44f794 */
        }
        wsprintfA(szKey, "start_positions[%d]", pRec->nStartPosIdx);      /* @0x44fcd8 @0x41229a */
        pVal = configEnvGetValue(&g_configEnvMaster, pCfg, szKey);        /* @0x4122bb */
        if (pVal == NULL) {
            fatalError("'levels[%d]/start_positions[%d]' not found in cfg.", /* @0x44fca4 */
                       g_nLevelIdx, pRec->nStartPosIdx);
        }
        anObjPos[0] = (int)configEnvGetDouble(pVal);                      /* @0x4122e1 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x4122f5 */
        anObjPos[1] = (int)configEnvGetDouble(pVal);                      /* @0x412302 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x412316 */
        anObjPos[2] = (int)configEnvGetDouble(pVal);                      /* @0x412323 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x412337 */
        anObjRot[0] = (short)(int)configEnvGetDouble(pVal);               /* @0x412344 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x412359 */
        anObjRot[1] = (short)(int)configEnvGetDouble(pVal);               /* @0x412366 */
        pVal = configNextNode(&g_configEnvMaster, pVal);                  /* @0x41237b */
        anObjRot[2] = (short)(int)configEnvGetDouble(pVal);               /* @0x412388 */
        configNextNode(&g_configEnvMaster, pVal);                         /* @0x41239d */
        nodeSetTransformFromChannels((WorldNode *)pRec->pSubObjB,          /* @0x404e00 @0x4123be */
                                     anObjPos[0], anObjPos[1], anObjPos[2],
                                     0, anObjRot[1]);
        nodeSetTransformFromChannels((WorldNode *)pRec->pSubObjA,          /* @0x4123df */
                                     anObjPos[0], anObjPos[1], anObjPos[2],
                                     0, anObjRot[1]);
        nodeSetTransformFromChannels((WorldNode *)pRec->pSubObjC,          /* @0x412400 */
                                     anObjPos[0], anObjPos[1], anObjPos[2],
                                     0, anObjRot[1]);
    }

    /* @0x412556 cameraSetClassMeshes(&block, record->pCharSceneNode) — the
     * record field is NULL until players exist; sceneObjSetClassMesh treats
     * a NULL mesh as g_rootNode. */
    cameraSetClassMeshes(&g_camFollowBlock, g_playerRecords[g_nLocalPlayerIdx].pCharSceneNode);

    pCam = configEnvGetValue(&g_configEnvMaster, NULL, "objects/camera"); /* @0x41256e */
    if (pCam == NULL) {
        fatalError("'objects/camera' not found in cfg.");                 /* @0x44fc70 */
    }
    pNode = configEnvGetValue(&g_configEnvMaster, pCam, "pos");           /* @0x41257e */
    if (pNode == NULL) {
        fatalError("'objects/camera/pos' not found in cfg.");             /* @0x44fc48 */
    }
    anPosCfg[0] = (int)configEnvGetDouble(pNode);                         /* @0x412590 */
    pNode = configNextNode(&g_configEnvMaster, pNode);
    anPosCfg[1] = (int)configEnvGetDouble(pNode);                         /* @0x4125b4 */
    pNode = configNextNode(&g_configEnvMaster, pNode);
    anPosCfg[2] = (int)configEnvGetDouble(pNode);                         /* @0x4125d0 */
    pNode = configNextNode(&g_configEnvMaster, pNode);
    nAng[0] = (short)(int)configEnvGetDouble(pNode);                      /* @0x4125e5 */
    pNode = configNextNode(&g_configEnvMaster, pNode);
    nAng[1] = (short)(int)configEnvGetDouble(pNode);                      /* @0x4125f5 */
    pNode = configNextNode(&g_configEnvMaster, pNode);
    nAng[2] = (short)(int)configEnvGetDouble(pNode);
    sceneNodeSetPos(g_pCamPosNode, anPosCfg, 2);                             /* 0x431590 @0x412600 */
    sceneNodeSetPosShorts(g_pCamPosNode, nAng, 2);                        /* 0x431850 @0x412629 */

    pNode = configEnvGetValue(&g_configEnvMaster, pCam, "aim");           /* 0x436870 @0x4125ed */
    if (pNode == NULL) {
        fatalError("'objects/camera/aim' not found in cfg.");             /* @0x44fc1c */
    }
    anAimCfg[0] = (int)configEnvGetDouble(pNode);                         /* @0x412590 */
    pNode = configNextNode(&g_configEnvMaster, pNode);
    anAimCfg[1] = (int)configEnvGetDouble(pNode);
    pNode = configNextNode(&g_configEnvMaster, pNode);
    anAimCfg[2] = (int)configEnvGetDouble(pNode);
    configNextNode(&g_configEnvMaster, pNode);                            /* @0x4125f0 */
    sceneNodeSetPos(g_pCamAimNode, anAimCfg, 2);                             /* 0x431590 @0x412600 */

    /* Copy the placed camera position/orientation onto the camera root. The
     * ints from sceneNodeGetPos pass through sceneNodeSetPos as raw bits
     * (int FLD/FILD round trip in the original), hence the casts. */
    sceneNodeGetPos(g_pCamPosNode, 0, anPos, 4);                          /* 0x431270 @0x412614 */
    sceneNodeGetChannelPos(g_pCamPosNode, 0, nAng, 4, nAng);              /* 0x4315e0 @0x412629b */
    sceneNodeSetPos(g_pSceneRoot, anPos, 2);                     /* 0x431590 @0x41263c */
    sceneNodeSetPosShorts(g_pSceneRoot, nAng, 2);                         /* 0x431850 @0x41264e */

    sceneNodeGetPos(g_pCamAimNode, 0, anPos, 4);                          /* 0x431270 @0x412666 */
    sceneNodeFacePos(g_pSceneRoot, 0, (float)anPos[0], (float)anPos[1],   /* 0x431030 @0x412690 */
                     (float)anPos[2], 2);

    pNode = configEnvGetValueByIndex(&g_configEnvMaster, "objects/camera"); /* 0x436550 @0x4126a2 */
    if (pNode == NULL) {
        fatalError("'objects/camera' not found in cfg.");                 /* @0x44fc70 */
    }
    g_camFollowBlock.nSnapDist = (int)configEnvGetDouble2(&g_configEnvMaster, pNode, "rot_speed"); /* 0x436a20 @0x4126c5 -> 0x45890c */
    g_camFollowBlock.nDiv = (int)configEnvGetDouble2(&g_configEnvMaster, pNode, "rot_max");        /* 0x436a20 @0x4126df -> 0x458908 */
}

/* cameraSetClassMeshes @0x4023b0 — attach pMesh as the class mesh of the
 * block's pos node (+8) and camera root (+0) and store it as the follow
 * node (+4, 0x4588fc; the missing writer of that field). */
void cameraSetClassMeshes(CameraFollowBlock *pBlk, SceneNode *pMesh) /* @0x4023b0 */
{
    sceneObjSetClassMesh((int)(size_t)pBlk->pPosNode, pMesh, 0, 3);   /* @0x4023c0 */
    sceneObjSetClassMesh((int)(size_t)pBlk->pNode, pMesh, 0, 3);      /* @0x4023d5 */
    pBlk->pFollowNode = pMesh;                                        /* @0x4023e6 */
}

/* cameraFollowUpdate @0x4020d0 — camera follow pass. pBlk = &g_camFollowBlock
 * (@0x4588f8; roundStartInit @0x40a9ce and gameWorldUpdate @0x40b508 both
 * pass the block). Reads the target from the camera pos node (or snaps both
 * target and current position to a c_ac zone whose y-range and rect contain
 * the follow node), pushes the target away from zone walls with
 * zoneAvoidWalls (TODO stub: returns 0), follows x/z toward the target with
 * a snap-inside-nSnapDist / delta-nDiv smoothing, lowers the target height
 * when the camera-to-player segment crosses a c_di zone (TODO stub), does
 * the same smoothing for y, pushes the current position away from walls a
 * second time and finally writes the position to the camera root and faces
 * it toward the aim node's world position. */
void cameraFollowUpdate(CameraFollowBlock *pBlk) /* @0x4020d0 */
{
    int anTarget[3];   /* camera-pos-node position / c_ac snap (out1) */
    int anCur[3];      /* camera root position (out2) */
    int anFollow[4];   /* follow-node world position (out3) */
    int anAim[4];      /* aim-node world position (out4) */
    EventObject *pObj;
    int nSnapFlag = 0; /* c_ac snap taken (EBP) */
    GxVec2 vPoint;
    GxVec2 vRef;

    memset(anFollow, 0, sizeof(anFollow));
    sceneNodeGetPos(pBlk->pPosNode, 0, anTarget, 4);                  /* 0x431270 @0x4020e9 */
    sceneNodeGetPos(pBlk->pNode, 0, anCur, 4);                        /* 0x431270 @0x4020f9 */
    sceneNodeGetPosWorld(pBlk->pFollowNode, (float *)anFollow, 4);    /* 0x430e80 @0x402109 */

    pObj = objFindById(OBJ_ID_C_AC, 0);                               /* 0x414a90 @0x402115 */
    while (pObj != NULL) {                                            /* 0x402123..0x40215f */
        if (anTarget[1] <= pObj->field_10 &&
            (pObj->field_18 == 0 || pObj->field_18 <= anTarget[1]) &&
            objContainsPoint(pObj, (float)anFollow[0], (float)anFollow[2])) { /* 0x414bb0 @0x40214b */
            anTarget[0] = (int)pObj->flOriginZ;                       /* +0x3c @0x402163 */
            anTarget[1] = pObj->field_14;                             /* +0x14 @0x402171 */
            anTarget[2] = (int)pObj->flOriginX;                       /* +0x38 @0x402178 */
            anCur[0] = anTarget[0];                                   /* @0x402184 */
            anCur[1] = anTarget[1];
            anCur[2] = anTarget[2];
            nSnapFlag = pObj->field_1c;                               /* +0x1c @0x402190 */
            break;
        }
        pObj = objHashNextSame(pObj);                                 /* 0x414a40 @0x402156 */
    }

    /* First wall-avoid pass: push the target away from zone walls around the
     * follow node within the camera height. (Original @0x402193..0x402203.) */
    if (nSnapFlag == 0) {
        gxVec2Set(&vRef, (float)anFollow[0], (float)anFollow[2]);     /* 0x434fa0 @0x4021a7 */
        gxVec2Set(&vPoint, (float)anTarget[0], (float)anTarget[2]);   /* @0x4021c0 */
        if (zoneAvoidWalls(&vPoint, &vRef, (float)anTarget[1]) != 0) { /* 0x4023e0 @0x4021db */
            anTarget[0] = (int)vPoint.x;                              /* ftol @0x4021e7 */
            anTarget[2] = (int)vPoint.y;
        }
    }

    /* x follow: snap when close, else ease by delta/nDiv. (@0x40220f..0x402232) */
    {
        int nDelta = anTarget[0] - anCur[0];
        if (nDelta >= pBlk->nSnapDist || nDelta <= -pBlk->nSnapDist) {
            anCur[0] += nDelta / pBlk->nDiv;
        } else {
            anCur[0] = anTarget[0];
        }
    }
    /* z follow (same rule). (@0x402234..0x402254) */
    {
        int nDelta = anTarget[2] - anCur[2];
        if (nDelta >= pBlk->nSnapDist || nDelta <= -pBlk->nSnapDist) {
            anCur[2] += nDelta / pBlk->nDiv;
        } else {
            anCur[2] = anTarget[2];
        }
    }

    /* c_di: lower the target height when the camera-to-follow segment
     * crosses a c_di zone segment. (@0x402254..0x4022a7) */
    pObj = objFindById(OBJ_ID_C_DI, 0);                               /* 0x414a90 @0x40225c */
    if (pObj != NULL &&
        objSegListIntersectTest(pObj, (float)anCur[2], (float)anCur[0], /* 0x414ce0 @0x40228c */
                                (float)anFollow[2], (float)anFollow[0]) != 0) {
        anTarget[1] = (anTarget[1] / pObj->field_10) * pObj->field_14; /* @0x402295 */
    }

    /* y follow (same rule as x/z). (@0x4022ad..0x4022d0) */
    {
        int nDelta = anTarget[1] - anCur[1];
        if (nDelta >= pBlk->nSnapDist || nDelta <= -pBlk->nSnapDist) {
            anCur[1] += nDelta / pBlk->nDiv;
        } else {
            anCur[1] = anTarget[1];
        }
    }

    /* Second wall-avoid pass: push the current position away from the walls
     * around the follow node. (Original @0x4022d0..0x402356.) */
    if (nSnapFlag == 0) {
        gxVec2Set(&vRef, (float)anFollow[0], (float)anFollow[2]);     /* @0x40230a */
        gxVec2Set(&vPoint, (float)anCur[0], (float)anCur[2]);         /* @0x4022e4 */
        if (zoneAvoidWalls(&vPoint, &vRef, (float)anTarget[1]) != 0) { /* @0x402332 */
            anCur[0] = (int)vPoint.x;                                 /* ftol @0x40233e */
            anCur[2] = (int)vPoint.y;
        }
    }

    sceneNodeSetPos(pBlk->pNode, anCur, 2);                  /* 0x431590 @0x402362 */
    sceneNodeGetPosWorld(pBlk->pAimNode, (float *)anAim, 4);          /* 0x430e80 @0x402372 */
    sceneNodeFacePos(pBlk->pNode, 0, (float)anAim[0], (float)anAim[1], /* 0x431030 @0x402399 */
                     (float)anAim[2], 2);
}

/* =====================================================================
 * gameUpdate player-physics cluster (gameUpdate @0x426ee0 and its
 * per-frame passes). All originals are __cdecl with the PlayerRecord*
 * as the only stack argument.
 * ===================================================================== */

/* 4-char EventObject ids of the per-level special zones (initialized
 * image data read directly from the original .exe). */
int g_nObjIdMvnc = 0x636E766D;  /* @0x450df4 "mvnc" (move-car / hazard zone) */
int g_nObjIdHurl = 0x6C727568;  /* @0x450dec "hurl" (fan / hurl pad) */
int g_nObjIdTele = 0x656C6574;  /* @0x450de4 "tele" (teleport pad) */

static const double g_dblPi31415 = 3.1415;          /* @0x44b2b0 */
static const double g_dblMinusPi31415 = -3.1415;    /* @0x44b2a0 */
static const float g_flTwoPiF = 6.283f;             /* @0x44b2a8 */
static const float g_flHalfPiPhy = 1.5707964f;      /* @0x44b270 */
static const float g_flEighteen = 18.0f;            /* @0x44b58c */
static const float g_flNegSixSeventh = -0.8571429f; /* @0x44b6f8 */
static const float g_fl0_999 = 0.999f;              /* @0x44b57c */
static const float g_fl300 = 300.0f;                /* @0x44b6f4 */
static const float g_fl250 = 250.0f;                /* @0x44b460 */
static const float g_fl1100 = 1100.0f;              /* @0x44b27c */
static const double g_dbl0_75 = 0.75;               /* @0x44b6e8 */
static const float g_fl0_75f = 0.75f;               /* @0x44b6ec (float 0.75) */
static const float g_flBinAngle = 10430.219f;       /* @0x44b6f0 (32768/pi) */
static const float g_flPiPhy = 3.1415925f;          /* @0x44b2d0 */
static const float g_fl9_588e_05 = 9.587673e-05f;   /* @0x44b6dc */

/* moveStateSetSnapFlag @0x4020c0 — raise the controller's zone-snap flag
 * (+0x51) after a teleport pad moved the node. */
void moveStateSetSnapFlag(AiController *pCtrl) /* @0x4020c0 */
{
    pCtrl->bFlag51 = 1;                             /* +0x51 @0x4020c4 */
}

/* Wrap a node heading into [-3.1415, 3.1415] (shared inline loop of the
 * physics passes; kept as a macro because the original inlines it). */
#define WRAP_NODE_ANGLE(pNode)                                    \
    do {                                                          \
        while ((double)(pNode)->flScaleA > g_dblPi31415) {        \
            (pNode)->flScaleA -= g_flTwoPiF;                      \
        }                                                         \
        while ((double)(pNode)->flScaleA < g_dblMinusPi31415) {   \
            (pNode)->flScaleA += g_flTwoPiF;                      \
        }                                                         \
    } while (0)

/* Floor height of pNode's plane under a world point (the walk-physics
 * variant without the mesh world offset; the child-mesh variant in the
 * support loops adds mesh->vWorldA to the point components first). */
#define FLOOR_PLANE(pSurf, nZ, nX)                                                                \
    ((float)((int)(nZ) - (pSurf)->nRefZ) * (pSurf)->flSlopeZ +                                    \
     (float)((int)(nX) - (pSurf)->nRefX) * (pSurf)->flSlopeX + (float)(pSurf)->nRefY)

/* playerUpdateWalkPhysics @0x426fd0 — per-frame walking physics for the
 * cart-less player (gameUpdate). Damps the speed channel, integrates the
 * turn accumulator, polar-sums the accel vector (target heading) with the
 * current velocity, applies the level 3/4/2 special zones ("mvnc" squash
 * home, "hurl" fan launch, "tele" teleport) and clamps the walk child
 * mesh height against its raycast floor plane. */
void playerUpdateWalkPhysics(PlayerRecord *pRec) /* @0x426fd0 */
{
    WorldNode *pWalk = pRec->pSubObjA;                     /* +0x224 @0x426ffc */
    int nCharKey = (int)(size_t)pRec->pCharSceneNode;      /* +0x10 channel key */
    GxVec2 vRot;
    EventObject *pEo;
    ObjChildMesh *pMesh;
    AiNavNode *pSurf;
    float flAvg;
    float flFloor;

    pRec->flCurSpeed = pRec->flFriction * pRec->flCurSpeed;              /* +0x1f4 @0x426ff0 */
    pRec->flTurnAccum = pRec->flRotAccFric * pRec->flTurnAccum +
                        pRec->flRotAccSpeed * pRec->flInputTurn;         /* +0x204 @0x427008 */
    WRAP_NODE_ANGLE(pWalk);                                              /* @0x427028 */
    pRec->vAccPolar.y = pWalk->flScaleA + pRec->flTurnAccum;             /* +0x218 @0x42707a */
    pRec->vAccPolar.x = pRec->flAccSpeed * pRec->flInputAccel;           /* +0x214 @0x4270a1 */
    pRec->vVelPolar.x = pRec->flCurSpeed;                                /* +0x21c @0x427083 */
    gxVec2RotateAdd(&vRot, &pRec->vAccPolar, &pRec->vVelPolar);          /* @0x4270b3 */
    pRec->vVelPolar = vRot;                                              /* @0x4270b8 */
    pRec->flCurSpeed = pRec->vVelPolar.x;                                /* +0x1f4 @0x4270c6 */

    if (g_nLevelIdx == 3) {                                              /* @0x4270d4 */
        unsigned char bFlags = pRec->bStateFlags;
        int bSquashed = (bFlags & 4) != 0;                               /* @0x4270e8 */
        pRec->bStateFlags = bFlags & ~4u;                                /* AND AL,0xfb @0x4270f4 */
        sceneNodeSetHiddenFlag(pRec->pCharShadowNode, 2);                /* +0x20 @0x4270ff */
        for (pEo = objFindById(g_nObjIdMvnc, 0); pEo != NULL;            /* @0x450df4 @0x427104 */
             pEo = objHashNextSame(pEo)) {                               /* @0x42719c */
            GxVec2 vPolar;
            GxVec2 vIn;
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) <= pEo->flHeightA)) {   /* +0x40 @0x427135 */
                continue;
            }
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) >= pEo->flHeightB)) {   /* +0x44 @0x427156 */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pWalk->vPos.x,
                                 (float)(int)pWalk->vPos.y) == 0) {            /* @0x427191 */
                continue;
            }
            if (bSquashed || rand() % 100 < 5) {                               /* @0x4271b8 */
                void *pEmitter = malloc(0x1c);                                 /* operator_new @0x43dd42 */
                if (pEmitter != NULL) {
                    sndPlaySfx3D(pEmitter, 1, 0x1a, 0xfde8, 0xff, nCharKey, 0, 0, 0, 0);
                }
            }
            gxVec2Set(&vIn, pEo->flOriginZ - (float)(int)pWalk->vPos.y,        /* +0x3c @0x42720f */
                      pEo->flOriginX - (float)(int)pWalk->vPos.x);             /* +0x38 @0x42724c */
            mathVec2Polar(&vPolar, &vIn);                                      /* @0x42725d */
            pRec->vVelPolar.y = vPolar.y;                                      /* +0x220 @0x427276 */
            pRec->vVelPolar.x = 300.0f;                                        /* 0x43960000 @0x427279 */
            pRec->flTurnAccum = 0.23f;                                         /* 0x3e6b851f @0x427280 */
            pRec->bStateFlags |= 4;                                            /* @0x42728a */
            if (g_nGfxMode != 2) {                                             /* @0x427290 */
                sceneObjResetFlags(pRec->pCharShadowNode, 2);                  /* @0x430620 @0x4272a0 */
            }
            sceneObjSetPosOrient(pRec->pCharShadowNode, 0, 0x1a0a, 0, 5);      /* @0x4272b7 */
            goto walkTail;                                                     /* JMP 0x42762c @0x4272bf */
        }
    } else if (g_nLevelIdx == 4 || g_nLevelIdx == 2) {                         /* @0x4272c4 */
        unsigned char bFlags = pRec->bStateFlags;
        int bHurtSfx = (bFlags >> 3) & 1;                                      /* @0x4272ea */
        pRec->bStateFlags = bFlags & ~8u;                                      /* AND AL,0xf7 @0x4272dc */
        for (pEo = objFindById(g_nObjIdMvnc, 0); pEo != NULL;                  /* @0x4272e4 */
             pEo = objHashNextSame(pEo)) {
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) <= pEo->flHeightA)) {   /* @0x427319 */
                continue;
            }
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) >= pEo->flHeightB)) {   /* @0x42733a */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pWalk->vPos.x,
                                 (float)(int)pWalk->vPos.y) == 0) {            /* @0x42737b */
                continue;
            }
            if (bHurtSfx == 0) {                                               /* TEST BL,BL @0x427397 */
                void *pEmitter = malloc(0x1c);                                 /* @0x42739d */
                if (pEmitter != NULL) {
                    sndPlaySfx3D(pEmitter, 1, 7, 0xfde8, 0xff, nCharKey, 0, 0, 0, 0);
                }
            }
            pRec->vVelPolar.x = 0.0f;                                          /* +0x21c @0x4273e6 */
            pRec->flTurnAccum = 0.23f;                                         /* @0x4273ef */
            pRec->flVertVel = -200.0f;                                         /* 0xc3480000 @0x4273f9 */
            pRec->bStateFlags |= 8;                                            /* @0x427403 */
            break;
        }
        for (pEo = objFindById(g_nObjIdHurl, 0); pEo != NULL;                  /* @0x450dec @0x427409 */
             pEo = objHashNextSame(pEo)) {
            GxVec2 vPolar;
            GxVec2 vIn;
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) <= pEo->flHeightA)) {   /* @0x42743a */
                continue;
            }
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) >= pEo->flHeightB)) {   /* @0x42745b */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pWalk->vPos.x,
                                 (float)(int)pWalk->vPos.y) == 0) {            /* @0x427496 */
                continue;
            }
            gxVec2Set(&vIn, pEo->flOriginZ - (float)(int)pWalk->vPos.y,        /* @0x4274b2 */
                      pEo->flOriginX - (float)(int)pWalk->vPos.x);
            mathVec2Polar(&vPolar, &vIn);                                      /* @0x427500 */
            pRec->vVelPolar.y = vPolar.y;                                      /* +0x220 @0x427509 */
            pRec->vVelPolar.x = 400.0f;                                        /* 0x43c80000 @0x427513 */
            pRec->flTurnAccum = 0.23f;                                         /* @0x42751a */
            break;
        }
        for (pEo = objFindById(g_nObjIdTele, 0); pEo != NULL;                  /* @0x450de4 @0x427524 */
             pEo = objHashNextSame(pEo)) {
            short nScale;
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) <= pEo->flHeightA)) {   /* @0x427555 */
                continue;
            }
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) >= pEo->flHeightB)) {   /* @0x427576 */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pWalk->vPos.x,
                                 (float)(int)pWalk->vPos.y) == 0) {            /* @0x4275b1 */
                continue;
            }
            {
                int nTmp = (int)pRec->vVelPolar.y;                             /* ftol of heading */
                nScale = (short)(pEo->field_14 - (short)nTmp);                 /* @0x4275de */
            }
            {
                int nSpeedBits;
                memcpy(&nSpeedBits, &pRec->flCurSpeed, 4);         /* +0x1f4 raw bits @0x4275ed */
                nodeSetTransformFromChannels(pWalk,                            /* @0x427607 */
                                             (int)pEo->flOriginZ,              /* arg2 = ftol(+0x3c) */
                                             pEo->field_10,                    /* arg3 = +0x10 raw */
                                             (int)pEo->flOriginX,              /* arg4 = ftol(+0x38) */
                                             nSpeedBits,                       /* arg5 = +0x1f4 raw bits */
                                             nScale);
            }
            pRec->vAccPolar.y += (float)pEo->field_14 * g_fl9_588e_05;         /* +0x218 @0x427615 */
            moveStateSetSnapFlag(&pRec->ai);                                   /* @0x4020c0 @0x427627 */
            break;
        }
    }

walkTail:
    objMovePolar(pWalk, pRec->vVelPolar.x, pRec->vVelPolar.y);                 /* @0x42763a */
    objSetAngle(pWalk, pRec->vAccPolar.y);                                    /* +0x218 @0x42764c */
    flAvg = nodeChannelAvgFloat(pWalk, nCharKey);                             /* @0x42765b */
    pMesh = objFindTurret(pWalk, nCharKey);                                   /* @0x42766e */
    flFloor = 0.0f;                                                           /* [0x44b244] @0x427673 */
    pSurf = (AiNavNode *)pMesh->pSurface;                                     /* +0x3c @0x42767d */
    if (pSurf != NULL) {
        flFloor = FLOOR_PLANE(pSurf, pWalk->vPos.x, pWalk->vPos.y);           /* @0x42768c */
    }
    if (flFloor > flAvg) {                                                    /* @0x4276ca */
        pRec->flVertVel = pRec->flVertVel + g_flEighteen;                     /* +0x208 @0x4276d2 */
    } else if (flFloor < flAvg) {                                             /* @0x4276e0 */
        flAvg = flFloor;                                                      /* @0x4276eb */
        pRec->flVertVel = 0.0f;                                               /* @0x4276ef */
    }
    pMesh->flHeight = flAvg + pRec->flVertVel;                                /* +0x40 @0x4276fd */
    pRec->flInputAccel = 0.0f;                                                /* +0x2e4 @0x427707 */
    pRec->flInputTurn = 0.0f;                                                 /* +0x2e0 @0x42770d */
}

/* playerUpdateOnFoot @0x427730 — per-frame pass for the cart-less player
 * over the pos-node (+0x264) collision cluster keyed by the cart scene
 * object (+0x14): support-point height integration against the raycast
 * floor planes, pitch/roll tilt channels from the 4 support heights,
 * speed/turn channel damping, down-slope push, the level 3/4/2 hazard
 * zones, then objMovePolar/objSetAngle on the pos node. */
void playerUpdateOnFoot(PlayerRecord *pRec) /* @0x427730 */
{
    WorldNode *pPos = pRec->pSubObjB;                      /* +0x264 */
    WorldNode *pWalk = pRec->pSubObjA;                     /* +0x224 */
    int nCartKey = (int)(size_t)pRec->pCartSceneObj;       /* +0x14 channel key */
    float afH[4];
    float afXw[4];                                         /* vPos.y (world x) slots */
    float afZw[4];                                         /* vPos.x (world z) slots */
    float flMinH = 3.4028235e38f;                          /* 0x7f7fffff @0x4277bf */
    int bGrounded = 0;                                     /* [ESP+0x18] @0x4278a1 */
    GxVec2 vPolar1;
    GxVec2 vPolar2;
    GxVec2 vIn;
    GxVec2 vPush;
    GxVec2 vPushPolar;
    GxVec2 vOut;
    EventObject *pEo;
    ObjChildMesh *pMesh;
    AiNavNode *pSurf;
    float flSupport;
    float flVel;
    float flPosTurn;
    int i;

    for (i = 0; i < 4; i++) {                              /* channel zeroing @0x42775f */
        afH[i] = 0.0f;
        afXw[i] = 0.0f;
        afZw[i] = 0.0f;
    }
    /* pass 1: support-mesh height integration (meshes keyed nCartKey) */
    for (pMesh = objFindTurret(pPos, nCartKey); pMesh != NULL;       /* @0x4277cb */
         pMesh = pMesh->pNext) {                                     /* @0x4278c2 */
        if (pMesh->nChannelKey != nCartKey) {                        /* +0x0c @0x4277da */
            continue;
        }
        pSurf = (AiNavNode *)pMesh->pSurface;                        /* +0x3c @0x4277e8 */
        if (pSurf != NULL) {
            flSupport =                                              /* @0x4277f5..0x427837 */
                (float)((int)pPos->vPos.y + (int)pMesh->vWorldA.y - pSurf->nRefX) * pSurf->flSlopeX +
                (float)((int)pPos->vPos.x + (int)pMesh->vWorldA.x - pSurf->nRefZ) * pSurf->flSlopeZ +
                (float)pSurf->nRefY;
        } else {
            flSupport = pMesh->flHeight;                             /* +0x40 @0x42783c */
        }
        if (flSupport <= pMesh->flHeight) {                          /* FCOM @0x42783f */
            if (pMesh->flVertVel < g_flEighteen) {                   /* +0x44 < 18.0 @0x427878 */
                pMesh->flHeight = flSupport;                         /* @0x42789b */
                pMesh->flVertVel = 0.0f;                             /* @0x42789e */
                bGrounded = 1;                                       /* @0x4278a1 */
            } else {
                flVel = pMesh->flVertVel * g_flNegSixSeventh;        /* @0x42788a */
                pMesh->flVertVel = flVel;                            /* @0x427890 */
                pMesh->flHeight = flVel + pMesh->flHeight;           /* @0x427893 */
            }
        } else {                                                     /* @0x42784c */
            flVel = pMesh->flVertVel + g_flEighteen;                 /* @0x42784c */
            pMesh->flVertVel = flVel;                                /* @0x427852 */
            flVel = flVel + pMesh->flHeight;                         /* @0x427855 */
            pMesh->flHeight = flVel;                                 /* @0x427858 */
            if (flSupport <= flVel) {                                /* @0x42785b */
                pMesh->flVertVel = pMesh->flVertVel - (pMesh->flHeight - flSupport);
                pMesh->flHeight = flSupport;                         /* @0x427873 */
            }
        }
        if (pMesh->flHeight < flMinH) {                              /* @0x4278ad */
            flMinH = pMesh->flHeight;                                /* @0x4278bb */
        }
    }
    /* pass 2: cap every mesh at min + 300 */
    pMesh = objFindTurret(pPos, nCartKey);                           /* @0x4278d7 */
    if (pMesh != NULL) {                                             /* @0x4278de */
        float flCap = flMinH + g_fl300;                              /* [0x44b6f4] @0x4278e6 */
        for (; pMesh != NULL; pMesh = pMesh->pNext) {                /* @0x4278ec */
            if (flCap < pMesh->flHeight) {                           /* @0x4278f1 */
                pMesh->flHeight = flCap;                             /* @0x4278f6 */
                pMesh->flVertVel = 0.0f;                             /* @0x4278f9 */
            }
        }
    }
    /* collect the 4 support points */
    i = 0;                                                           /* @0x427914 */
    for (pMesh = objFindTurret(pPos, nCartKey); pMesh != NULL && i < 16;
         pMesh = pMesh->pNext, i += 4) {                             /* @0x427916 */
        afXw[i / 4] = pMesh->vPos.y;                                 /* +0x28 @0x427921 */
        afZw[i / 4] = pMesh->vPos.x;                                 /* +0x24 @0x42791d */
        afH[i / 4] = pMesh->flHeight;                                /* +0x40 @0x427928 */
    }
    /* tilt channels from the support heights (binary-angle units) */
    gxVec2Set(&vIn, ((afXw[3] - afXw[2]) + afXw[1]) - afXw[0],       /* @0x42793a */
              ((afH[3] - afH[2]) + afH[1]) - afH[0]);
    mathVec2Polar(&vPolar1, &vIn);                                   /* @0x427970 */
    gxVec2Set(&vIn, ((afH[3] - afH[1]) + afH[2]) - afH[0],           /* @0x427975 */
              ((afZw[3] - afZw[1]) + afZw[2]) - afZw[0]);
    mathVec2Polar(&vPolar2, &vIn);                                   /* @0x4279ab */
    pRec->wPosPitch = (short)(int)((vPolar2.y - g_flHalfPiPhy) * g_flBinAngle);  /* +0x24c @0x4279b0 */
    pRec->wPosRoll = (short)(int)(vPolar1.y * g_flBinAngle);                     /* +0x250 @0x4279c5 */
    /* speed/turn channels: damped when airborne, input-scaled when grounded */
    if (bGrounded) {                                                 /* @0x4279e2 */
        pRec->flPosSpeed = pRec->flAccFactor * pRec->flPosSpeed;     /* +0x22c * +0x234 @0x4279ea */
        flPosTurn = pRec->flRotFactor * pRec->flPosTurnAccum;        /* +0x23c * +0x244 @0x4279fc */
    } else {
        pRec->flPosSpeed = pRec->flPosSpeed * g_fl0_999;             /* [0x44b57c] @0x427a0a */
        flPosTurn = pRec->flPosTurnAccum * g_fl0_999;                /* @0x427a1c */
    }
    pRec->flPosTurnAccum = flPosTurn;                                /* +0x244 @0x427a2e */
    WRAP_NODE_ANGLE(pPos);                                           /* @0x427a34 */
    pRec->flPosHeading = pPos->flScaleA + pRec->flPosTurnAccum;      /* +0x258 @0x427a86 */
    /* down-slope push from the surface normals of every surfaced mesh */
    gxVec2Set(&vPush, 0.0f, 0.0f);                                   /* @0x427a91 */
    for (pMesh = objFindTurret(pPos, nCartKey); pMesh != NULL;       /* @0x427aaa */
         pMesh = pMesh->pNext) {                                     /* @0x427ad7 */
        if (pMesh->pSurface != NULL) {                               /* @0x427ab3 */
            pSurf = (AiNavNode *)pMesh->pSurface;
            vPush.x -= pSurf->flNormalZ + pSurf->flNormalZ;          /* +0x20 @0x427aba */
            vPush.y -= pSurf->flNormalX + pSurf->flNormalX;          /* +0x18 @0x427aca */
        }
    }
    if (objDistTo(pPos, pWalk) < g_fl1100) {                         /* [0x44b27c] @0x427af0 */
        pRec->flPosSpeed = pRec->flPosSpeed * (float)g_dbl0_75;      /* [0x44b6e8] @0x427b03 */
    }
    pRec->flPosVelLen = pRec->flPosSpeed;                            /* +0x25c @0x427b24 */
    mathVec2Polar(&vPushPolar, &vPush);                              /* @0x427b26 */
    gxVec2RotateAdd(&vOut, (GxVec2 *)&pRec->flPosVelLen, &vPushPolar); /* @0x427b32 */
    pRec->flPosVelLen = vOut.x;                                      /* +0x25c @0x427b3c */
    pRec->flPosVelAng = vOut.y;                                      /* +0x260 @0x427b41 */

    if (g_nLevelIdx == 3) {                                          /* @0x427b44 */
        sceneNodeSetHiddenFlag(pRec->pCartShadowNode, 2);            /* +0x24 @0x427b58 */
        for (pEo = objFindById(g_nObjIdMvnc, 0); pEo != NULL;        /* @0x427b5d */
             pEo = objHashNextSame(pEo)) {
            GxVec2 vPolar;
            if (!(nodeChannelAvgFloat(pPos, nCartKey) <= pEo->flHeightA)) {  /* @0x427b90 */
                continue;
            }
            if (!(nodeChannelAvgFloat(pPos, nCartKey) >= pEo->flHeightB)) {  /* @0x427bb4 */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pPos->vPos.x,
                                 (float)(int)pPos->vPos.y) == 0) {               /* @0x427bef */
                continue;
            }
            {
                GxVec2 vIn2;
                gxVec2Set(&vIn2, pEo->flOriginZ - (float)(int)pPos->vPos.y,      /* @0x427c0e */
                          pEo->flOriginX - (float)(int)pPos->vPos.x);
                mathVec2Polar(&vPolar, &vIn2);                                   /* @0x427c68 */
                pRec->flPosVelAng = vPolar.y;                                    /* +0x260 @0x427c7b */
                pRec->flPosVelLen = 350.0f;                                      /* 0x43af0000 @0x427c81 */
                pRec->flPosTurnAccum = 0.2f;                                     /* 0x3e4ccccd @0x427c8b */
            }
            if (g_nGfxMode != 2) {                                               /* @0x427c95 */
                sceneObjResetFlags(pRec->pCartShadowNode, 2);                    /* @0x427ca4 */
            }
            sceneObjSetPosOrient(pRec->pCartShadowNode, 0, 0x1a0a, 0, 5);        /* @0x427cb9 */
            break;
        }
    } else if (g_nLevelIdx == 4 || g_nLevelIdx == 2) {                   /* @0x427cc6 */
        unsigned char bFlags = pRec->bStateFlags;
        int bHurtSfx = (bFlags >> 4) & 1;                                /* SHR EBX,4 @0x427cec */
        pRec->bStateFlags = bFlags & ~0x10u;                             /* AND AL,0xef @0x427cde */
        for (pEo = objFindById(g_nObjIdMvnc, 0); pEo != NULL;            /* @0x427ce6 */
             pEo = objHashNextSame(pEo)) {
            if (!(nodeChannelAvgFloat(pPos, nCartKey) <= pEo->flHeightA)) {  /* @0x427d1e */
                continue;
            }
            if (!(nodeChannelAvgFloat(pPos, nCartKey) >= pEo->flHeightB)) {  /* @0x427d42 */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pPos->vPos.x,
                                 (float)(int)pPos->vPos.y) == 0) {               /* @0x427d7d */
                continue;
            }
            if (bHurtSfx == 0) {                                             /* @0x427d9c */
                void *pEmitter = malloc(0x1c);                               /* @0x427da2 */
                if (pEmitter != NULL) {
                    sndPlaySfx3D(pEmitter, 1, 7, 0xfde8, 0xff, nCartKey, 0, 0, 0, 0);
                }
            }
            pRec->bStateFlags |= 0x10;                                       /* @0x427dfd */
            pRec->flPosVelLen = 0.0f;                                        /* +0x25c @0x427e07 */
            pRec->flPosTurnAccum = 0.23f;                                    /* 0x3e6b851f @0x427e11 */
            for (pMesh = objFindTurret(pPos, nCartKey); pMesh != NULL;       /* @0x427e1b */
                 pMesh = pMesh->pNext) {                                     /* @0x427e38 */
                pMesh->flHeight = pMesh->flHeight - g_fl250;                 /* [0x44b460] @0x427e2c */
                pMesh->flVertVel = -250.0f;                                  /* 0xc37a0000 @0x427e32 */
            }
            break;
        }
        for (pEo = objFindById(g_nObjIdHurl, 0); pEo != NULL;            /* @0x450dec @0x427e3f */
             pEo = objHashNextSame(pEo)) {
            GxVec2 vPolar;
            GxVec2 vIn2;
            if (!(nodeChannelAvgFloat(pPos, nCartKey) <= pEo->flHeightA)) {  /* @0x427e72 */
                continue;
            }
            if (!(nodeChannelAvgFloat(pPos, nCartKey) >= pEo->flHeightB)) {  /* @0x427e96 */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pPos->vPos.x,
                                 (float)(int)pPos->vPos.y) == 0) {               /* @0x427ed1 */
                continue;
            }
            gxVec2Set(&vIn2, pEo->flOriginZ - (float)(int)pPos->vPos.y,          /* @0x427ef0 */
                      pEo->flOriginX - (float)(int)pPos->vPos.x);
            mathVec2Polar(&vPolar, &vIn2);                                       /* @0x427f4a */
            pRec->flPosVelAng = vPolar.y;                                        /* +0x260 @0x427f5d */
            pRec->flPosVelLen = 450.0f;                                          /* 0x43e10000 @0x427f63 */
            pRec->flPosTurnAccum = 0.2f;                                         /* 0x3e4ccccd @0x427f6d */
            break;
        }
        for (pEo = objFindById(g_nObjIdTele, 0); pEo != NULL;            /* @0x450de4 @0x427f77 */
             pEo = objHashNextSame(pEo)) {
            short nScale;
            if (!(nodeChannelAvgFloat(pPos, nCartKey) <= pEo->flHeightA)) {  /* @0x427fab */
                continue;
            }
            if (!(nodeChannelAvgFloat(pPos, nCartKey) >= pEo->flHeightB)) {  /* @0x427fcf */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pPos->vPos.x,
                                 (float)(int)pPos->vPos.y) == 0) {               /* @0x42800a */
                continue;
            }
            {
                int nTmp = (int)pRec->flPosVelAng;                           /* ftol of +0x260 @0x428032 */
                nScale = (short)(pEo->field_14 - (short)nTmp);               /* @0x428043 */
            }
            {
                int nSpeedBits;
                memcpy(&nSpeedBits, &pRec->flPosSpeed, 4);         /* +0x234 raw bits @0x428046 */
                nodeSetTransformFromChannels(pPos,                             /* @0x428060 */
                                             (int)pEo->flOriginZ,              /* arg2 = ftol(+0x3c) */
                                             pEo->field_10,                    /* arg3 = +0x10 raw */
                                             (int)pEo->flOriginX,              /* arg4 = ftol(+0x38) */
                                             nSpeedBits,                       /* arg5 = +0x234 raw bits */
                                             nScale);
            }
            break;
        }
    }
    pRec->flPosSpeed = pRec->flPosVelLen;                                /* +0x234 @0x428073 */
    objMovePolar(pPos, pRec->flPosVelLen, pRec->flPosVelAng);            /* @0x428081 */
    objSetAngle(pPos, pRec->flPosHeading);                               /* +0x258 @0x428093 */
}

/* syncWalkNodeChannelsToMesh @0x428990 — per-frame pass (gameUpdate) that
 * clamps the walk speed channel against the walk node's moved distance
 * (+0x3c), re-picks the walk heading channel, and repositions the char
 * mesh (+0x10) onto the walk node (+0x224) via the turret-entry polar
 * lookup and the channel-averaged height. */
void syncWalkNodeChannelsToMesh(PlayerRecord *pRec) /* @0x428990 */
{
    WorldNode *pWalk = pRec->pSubObjA;                     /* +0x224 @0x428996 */
    int nCharKey = (int)(size_t)pRec->pCharSceneNode;      /* +0x10 */
    float flSpeed = pRec->flCurSpeed;                      /* +0x1f4 @0x42899c */
    float flBound = pRec->flAccFric;                       /* +0x1f0 @0x4289a2 */
    float flHeading;
    int nZ;
    int nY;
    int nX;

    if (flBound < pWalk->field_3c) {                       /* @0x4289ab */
        if (flBound > flSpeed) {
            flBound = flSpeed;
        }
    } else {
        flBound = -flBound;
        if (flBound <= pWalk->field_3c) {                  /* @0x4289b8 */
            flBound = pWalk->field_3c;
        } else if (flBound < flSpeed) {                    /* @0x4289c1 */
            flBound = flSpeed;
        }
    }
    pRec->flCurSpeed = flBound;                            /* +0x1f4 @0x4289d1 */
    if (pWalk->field_18 == 0) {                            /* @0x4289d7 */
        flHeading = pRec->vVelPolar.y;                     /* +0x220 @0x4289dd */
    } else {
        flHeading = pWalk->flScaleC;                       /* +0x38 @0x4289e3 */
    }
    pRec->vVelPolar.y = flHeading;                         /* @0x4289e9 */
    sceneObjSetPosOrient(pRec->pCharSceneNode, 0,          /* @0x4289a5..0x4289f1 */
                         (short)objListFindFloat(pWalk, nCharKey), 0, 2);
    nZ = objPolarPosLookup2(pWalk, nCharKey);              /* @0x4289fd */
    nY = (int)nodeChannelAvgFloat(pWalk, nCharKey);        /* ftol @0x428a12 */
    nX = objPolarPosLookup(pWalk, nCharKey);               /* @0x428a27 */
    sceneObjSetPos(pRec->pCharSceneNode, nX, nY, nZ, 2);   /* @0x428a38 */
}

/* syncPosNodeChannelsToMesh @0x428840 — per-frame pass (gameUpdate) for
 * the pos node (+0x264): clamps the rest-height channel (+0x244) against
 * node +0x40 and the turn accumulator (+0x234) against node +0x3c, picks
 * the heading channel and repositions the cart mesh (+0x14) with pitch/
 * roll from +0x24c/+0x250. */
void syncPosNodeChannelsToMesh(PlayerRecord *pRec) /* @0x428840 */
{
    WorldNode *pPos = pRec->pSubObjB;                      /* +0x264 @0x42884c */
    int nCartKey = (int)(size_t)pRec->pCartSceneObj;       /* +0x14 */
    float flBound = pRec->flCurRotAccFric;                 /* +0x240 @0x428846 */
    float flRest = pRec->flPosTurnAccum;                   /* +0x244 @0x428852 */
    int nZ;
    int nY;
    int nX;

    /* clamp 1: +0x244 against the +0x240 bound and node +0x40 */
    if (flRest <= flBound) {                               /* @0x42885c */
        float fNeg = -flBound;
        if (fNeg <= flRest) {                              /* @0x428877 */
            flRest = flRest + pPos->_pad40;                /* node +0x40 @0x42887c */
            if (flBound <= flRest && flRest < fNeg) {      /* @0x42888a */
                flRest = fNeg;
            }
        }
    }
    pRec->flPosTurnAccum = flRest;                         /* +0x244 @0x42889b */
    /* clamp 2: +0x234 against the +0x230 bound and node +0x3c */
    {
        float fA = pRec->flCurAccFric;                     /* +0x230 @0x4288a1 */
        float fS = pRec->flPosSpeed;                       /* +0x234 @0x4288aa */
        if (fA < pPos->field_3c) {                         /* @0x4288b6 */
            if (fA > fS) {
                fA = fS;
            }
        } else {
            fA = -fA;
            if (fA <= pPos->field_3c) {                    /* @0x4288d7 */
                fA = pPos->field_3c;
            } else if (fA < fS) {                          /* @0x4288e7 */
                fA = fS;
            }
        }
        pRec->flPosSpeed = fA;                             /* +0x234 @0x4288f6 */
    }
    if (pPos->field_18 != 0) {                             /* @0x4288fc */
        pRec->flPosVelAng = pPos->flScaleC;                /* +0x260 = node +0x38 @0x428903 */
    }
    sceneObjSetPosOrient(pRec->pCartSceneObj,              /* @0x428934 */
                         pRec->wPosPitch,                  /* +0x24c yaw slot @0x42892b */
                         (short)objListFindFloat(pPos, nCartKey),
                         pRec->wPosRoll, 2);               /* +0x250 roll @0x42890e */
    nZ = objPolarPosLookup2(pPos, nCartKey);               /* @0x428948 */
    nY = (int)nodeChannelAvgFloat(pPos, nCartKey);         /* ftol @0x42895d */
    nX = objPolarPosLookup(pPos, nCartKey);                /* @0x42896d */
    sceneObjSetPos(pRec->pCartSceneObj, nX, nY, nZ, 2);    /* @0x428977 */
}

/* syncCartNodeChannelsToWalkPos @0x40e040 — copy the cart node (+0x2a4)
 * channel transforms onto the walk (+0x224) and pos (+0x264) nodes (the
 * character mounts the cart): clears the round gate (+0x174), derives
 * both node placements from the cart's turret entries, and syncs the
 * walk/pos speed+turn channels from the cart block. */
void syncCartNodeChannelsToWalkPos(PlayerRecord *pRec) /* @0x40e040 */
{
    WorldNode *pCart = pRec->pSubObjC;                     /* +0x2a4 @0x40e047 */
    WorldNode *pWalk = pRec->pSubObjA;                     /* +0x224 */
    WorldNode *pPos = pRec->pSubObjB;                      /* +0x264 */
    int nCharKey = (int)(size_t)pRec->pCharSceneNode;      /* +0x10 @0x40e05e */
    int nCartKey = (int)(size_t)pRec->pCartSceneObj;       /* +0x14 */
    int nZ;
    int nY;
    int nX;
    short nScale;

    pRec->field_174 = 0;                                   /* @0x40e04d */
    pCart->field_1c = 0;                                   /* @0x40e057 */
    nZ = objPolarPosLookup2(pCart, nCharKey);              /* @0x40e0b9 */
    nY = (int)nodeChannelAvgFloat(pCart, nCharKey);        /* ftol @0x40e0b0 */
    nX = objPolarPosLookup(pCart, nCharKey);               /* @0x40e0d5 */
    nScale = (short)objListFindFloat(pCart, nCharKey);     /* @0x40e0a4 */
    nodeSetTransformFromChannels(pWalk, nX, nY, nZ, 0, nScale); /* @0x40e0e7 */
    pWalk->field_1c = 1;                                   /* @0x40e0f2 */
    pRec->flTurnAccum = pRec->flCartTurnAccum;             /* +0x204 = +0x284 @0x40e0f8 */
    pRec->flCurSpeed = pRec->flCartCurSpeed;               /* +0x1f4 = +0x274 @0x40e0fe */
    pRec->vVelPolar.y = pRec->vCartVelPolar.y;             /* +0x220 = +0x2a0 @0x40e104 */
    nZ = objPolarPosLookup2(pCart, nCartKey);              /* @0x40e146 */
    nY = (int)nodeChannelAvgFloat(pCart, nCartKey);        /* ftol @0x40e13d */
    nX = objPolarPosLookup(pCart, nCartKey);               /* @0x40e162 */
    nScale = (short)objListFindFloat(pCart, nCartKey);     /* @0x40e131 */
    nodeSetTransformFromChannels(pPos, nX, nY, nZ, 0, nScale); /* @0x40e174 */
    pPos->field_1c = 1;                                    /* @0x40e17f */
    pRec->flPosTurnAccum = pRec->flCartTurnAccum;          /* +0x244 = +0x284 @0x40e185 */
    pRec->flPosSpeed = pRec->flCartCurSpeed;               /* +0x234 = +0x274 @0x40e18b */
    pRec->flPosVelAng = pRec->vCartVelPolar.y;             /* +0x260 = +0x2a0 @0x40e191 */
}

/* syncCartNodeChannelsToMeshes @0x428a70 — per-frame pass (gameUpdate)
 * for a riding player: clamp the cart turn (+0x284) and speed (+0x274)
 * channels, reposition BOTH meshes (+0x14 cart with pitch/roll, +0x10
 * char plain) onto the cart node (+0x2a4), then when the cart-mesh and
 * char-mesh heights diverge beyond ±1000 re-derive the walk/pos nodes
 * via syncCartNodeChannelsToWalkPos. */
void syncCartNodeChannelsToMeshes(PlayerRecord *pRec) /* @0x428a70 */
{
    WorldNode *pCart = pRec->pSubObjC;                     /* +0x2a4 @0x428a7c */
    int nCharKey = (int)(size_t)pRec->pCharSceneNode;      /* +0x10 */
    int nCartKey = (int)(size_t)pRec->pCartSceneObj;       /* +0x14 */
    float flTurn = pRec->flCartTurnAccum;                  /* +0x284 @0x428a82 */
    float flTurnB = pRec->flCartRotAccFric;                /* +0x280 @0x428a76 */
    int nZ;
    int nY;
    int nX;
    float flAvgCart;
    float flAvgChar;

    if (flTurn <= flTurnB) {                               /* @0x428a8c */
        float fNeg = -flTurnB;
        if (fNeg <= flTurn) {                              /* @0x428aa1 */
            flTurn = flTurn + pCart->_pad40;               /* node +0x40 @0x428aac */
            if (flTurnB <= flTurn && flTurn < fNeg) {      /* @0x428abf */
                flTurn = fNeg;
            }
        }
    }
    pRec->flCartTurnAccum = flTurn;                        /* +0x284 @0x428acb */
    {
        float fA = pRec->flCartFriction;                   /* +0x270 @0x428ad1 */
        float fS = pRec->flCartCurSpeed;                   /* +0x274 @0x428ada */
        if (fA < pCart->field_3c) {                        /* @0x428ae6 */
            if (fA > fS) {
                fA = fS;
            }
        } else {
            fA = -fA;
            if (fA <= pCart->field_3c) {                   /* @0x428b02 */
                fA = pCart->field_3c;
            } else if (fA < fS) {                          /* @0x428b14 */
                fA = fS;
            }
        }
        pRec->flCartCurSpeed = fA;                         /* +0x274 @0x428b26 */
    }
    if (pCart->field_18 != 0) {                            /* @0x428b2c */
        pRec->vCartVelPolar.y = pCart->flScaleC;           /* +0x2a0 = node +0x38 @0x428b33 */
    }
    sceneObjSetPosOrient(pRec->pCartSceneObj,              /* @0x428b64 */
                         pRec->wCartPitch,                 /* +0x28c yaw slot @0x428b5b */
                         (short)objListFindFloat(pCart, nCartKey),
                         pRec->wCartRoll, 2);              /* +0x290 roll @0x428b3e */
    nZ = objPolarPosLookup2(pCart, nCartKey);              /* @0x428b78 */
    nY = (int)nodeChannelAvgFloat(pCart, nCartKey);        /* ftol @0x428b8d */
    nX = objPolarPosLookup(pCart, nCartKey);               /* @0x428b9d */
    sceneObjSetPos(pRec->pCartSceneObj, nX, nY, nZ, 2);    /* @0x428ba7 */
    sceneObjSetPosOrient(pRec->pCharSceneNode, 0,          /* @0x428bc9 */
                         (short)objListFindFloat(pCart, nCharKey), 0, 2);
    nZ = objPolarPosLookup2(pCart, nCharKey);              /* @0x428bdd */
    nY = (int)nodeChannelAvgFloat(pCart, nCharKey);        /* ftol @0x428bf2 */
    nX = objPolarPosLookup(pCart, nCharKey);               /* @0x428c02 */
    sceneObjSetPos(pRec->pCharSceneNode, nX, nY, nZ, 2);   /* @0x428c0c */
    flAvgCart = nodeChannelAvgFloat(pCart, nCartKey);      /* +0x14 key @0x428c1e */
    flAvgChar = nodeChannelAvgFloat(pCart, nCharKey);      /* +0x10 key @0x428c31 */
    if (flAvgCart - flAvgChar > 1000.0f) {                 /* [0x44b464] @0x428c3a */
        syncCartNodeChannelsToWalkPos(pRec);               /* @0x40e040 @0x428c55 */
    } else if (flAvgCart - flAvgChar < -1000.0f) {         /* [0x44b704] @0x428c47 */
        syncCartNodeChannelsToWalkPos(pRec);               /* @0x428c63 */
    }
}

/* playerUpdateCartPhysics @0x4280b0 — per-frame cart-driving physics
 * (gameUpdate, players with the round gate +0x174 set): every 5th tick
 * snaps the rider off unreachable "mvnc" zones, friction-damps the cart
 * speed, integrates the turn accumulator, polar-sums accel with velocity
 * (plus a drift term while turning), applies the "tele" pads (levels
 * 4/2), moves the cart node, then integrates the 4-wheel support heights
 * with ride bounce and derives the cart pitch/roll tilt channels. */
void playerUpdateCartPhysics(PlayerRecord *pRec) /* @0x4280b0 */
{
    WorldNode *pCart = pRec->pSubObjC;                     /* +0x2a4 */
    int nCharKey = (int)(size_t)pRec->pCharSceneNode;      /* +0x10 */
    int nCartKey = (int)(size_t)pRec->pCartSceneObj;       /* +0x14 */
    GxVec2 vDrift;
    GxVec2 vOut;
    EventObject *pEo;
    ObjChildMesh *pMesh;
    AiNavNode *pSurf;
    float flAvg;
    float flFloor;
    float flSupport;
    float flVel;
    float afXw[4];                                         /* vPos.y (world x) */
    float afZw[4];                                         /* vPos.x (world z) */
    float afH[4];
    float flMinH = 3.4028235e38f;                          /* @0x428b?? local_50 */
    GxVec2 vPolar1;
    GxVec2 vPolar2;
    GxVec2 vIn;
    int i;

    if (g_nGameUpdateTick % 5 == 0) {                      /* @0x4280bd */
        for (pEo = objFindById(g_nObjIdMvnc, 0); pEo != NULL;   /* @0x4280c8 */
             pEo = objHashNextSame(pEo)) {
            float flAvgC = nodeChannelAvgFloat(pCart, nCharKey);
            int bTest;
            if (pEo->flHeightA < flAvgC || flAvgC < pEo->flHeightB) {   /* @0x4280e1 */
                float flAvgK = nodeChannelAvgFloat(pCart, nCartKey);
                if (!(pEo->flHeightA >= flAvgK && pEo->flHeightB <= flAvgK)) {  /* @0x428121 */
                    continue;
                }
            }
            bTest = objContainsPoint(pEo, (float)(int)pCart->vPos.x,    /* @0x428193 */
                                     (float)(int)pCart->vPos.y);
            if (bTest == 0) {
                continue;
            }
            syncCartNodeChannelsToWalkPos(pRec);                        /* @0x4281a2 */
            if (pRec->nChannelsDirty != 0) {                            /* +0x2d8 @0x4281ab */
                syncWalkNodeChannelsToMesh(pRec);                       /* @0x4281b5 */
            }
            syncPosNodeChannelsToMesh(pRec);                            /* @0x4281c4 */
            return;
        }
    }
    pRec->flCartCurSpeed = pRec->field_26c_pad * pRec->flCartCurSpeed;  /* +0x274 @0x4281d4 */
    pRec->flCartTurnAccum = pRec->flCartRotAccSpeed * pRec->flInputTurn +
                            pRec->flCartFrictionB * pRec->flCartTurnAccum;   /* +0x284 @0x4281da */
    WRAP_NODE_ANGLE(pCart);                                             /* @0x428204 */
    pRec->vCartAccPolar.y = pCart->flScaleA + pRec->flCartTurnAccum;    /* +0x298 @0x428232 */
    pRec->vCartAccPolar.x = pRec->flCartAccSpeed * pRec->flInputAccel;  /* +0x294 @0x42823e */
    pRec->vCartVelPolar.x = pRec->flCartCurSpeed;                       /* +0x29c @0x428226 */
    gxVec2RotateAdd(&vOut, &pRec->vCartAccPolar, &pRec->vCartVelPolar); /* @0x428252 */
    pRec->vCartVelPolar = vOut;                                         /* @0x428257 */
    if (pRec->flInputTurn != 0.0f) {                                    /* +0x2e0 @0x428262 */
        vDrift.x = pRec->vCartAccPolar.x * 0.5f;                        /* g_flHalf @0x42826e */
        vDrift.y = pRec->vCartAccPolar.y -
                   pRec->flInputAccel * pRec->flInputTurn * g_flPiPhy;  /* @0x428287 */
        gxVec2RotateAdd(&vOut, &vDrift, &pRec->vCartVelPolar);          /* @0x42829c */
        pRec->vCartVelPolar = vOut;                                     /* @0x4282a1 */
    }
    pRec->flCartCurSpeed = pRec->vCartVelPolar.x;                       /* +0x274 @0x4282a9 */
    if (g_nLevelIdx == 4 || g_nLevelIdx == 2) {                         /* @0x4282b1 */
        for (pEo = objFindById(g_nObjIdTele, 0); pEo != NULL;           /* @0x4282bc */
             pEo = objHashNextSame(pEo)) {
            short nScale;
            if (!(nodeChannelAvgFloat(pCart, nCharKey) <= pEo->flHeightA)) {  /* @0x4282e9 */
                continue;
            }
            if (!(nodeChannelAvgFloat(pCart, nCharKey) >= pEo->flHeightB)) {  /* @0x42830a */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pCart->vPos.x,
                                 (float)(int)pCart->vPos.y) == 0) {               /* @0x428345 */
                continue;
            }
            {
                int nTmp = (int)pRec->vCartVelPolar.y;                      /* ftol +0x2a0 @0x428399 */
                nScale = (short)(pEo->field_14 - (short)nTmp);              /* @0x4283a6 */
            }
            {
                int nSpeedBits;
                memcpy(&nSpeedBits, &pRec->flCartCurSpeed, 4);     /* +0x274 raw bits @0x4283b5 */
                nodeSetTransformFromChannels(pCart,                            /* @0x4283c6 */
                                             (int)pEo->flOriginZ,              /* ftol(+0x3c) */
                                             pEo->field_10,                    /* +0x10 raw */
                                             (int)pEo->flOriginX,              /* ftol(+0x38) */
                                             nSpeedBits,                       /* +0x274 raw bits */
                                             nScale);
            }
            pRec->vCartAccPolar.y += (float)pEo->field_14 * g_fl9_588e_05;    /* +0x298 @0x4283d6 */
            moveStateSetSnapFlag(&pRec->ai);                                  /* @0x4283e2 */
            break;
        }
    }
    objMovePolar(pCart, pRec->vCartVelPolar.x, pRec->vCartVelPolar.y);    /* @0x4283f6 */
    objSetAngle(pCart, pRec->vCartAccPolar.y);                            /* +0x298 @0x428408 */
    pRec->flInputAccel = 0.0f;                                            /* +0x2e4 @0x428412 */
    pRec->flInputTurn = 0.0f;                                             /* +0x2e0 @0x428418 */
    flAvg = nodeChannelAvgFloat(pCart, nCharKey);                         /* @0x428425 */
    pMesh = objFindTurret(pCart, nCharKey);                               /* @0x428438 */
    flFloor = 0.0f;
    pSurf = (AiNavNode *)pMesh->pSurface;                                 /* +0x3c @0x428440 */
    if (pSurf != NULL) {
        flFloor = FLOOR_PLANE(pSurf, pCart->vPos.x, pCart->vPos.y);       /* @0x428447 */
    }
    flVel = pMesh->flVertVel + pRec->flCartBounceIn;                      /* +0x44 + +0x288 @0x428483 */
    pMesh->flVertVel = flVel;                                             /* @0x42848a */
    if (flFloor <= flAvg) {                                               /* @0x428492 */
        if (flFloor < flAvg) {                                            /* @0x42849b */
            pMesh->flVertVel = 0.0f;                                      /* @0x4284a2 */
            flAvg = flFloor;                                              /* @0x4284a7 */
        }
    } else {
        pMesh->flVertVel = flVel + g_flEighteen;                          /* @0x4284ad */
    }
    pMesh->flHeight = flAvg + pMesh->flVertVel;                           /* +0x40 @0x4284cd */
    /* support-wheel height pass (meshes keyed nCartKey) */
    for (i = 0; i < 4; i++) {
        afH[i] = 0.0f; afXw[i] = 0.0f; afZw[i] = 0.0f;
    }
    for (pMesh = objFindTurret(pCart, nCartKey); pMesh != NULL;           /* @0x428510 */
         pMesh = pMesh->pNext) {
        if (pMesh->nChannelKey != nCartKey) {                             /* +0x0c @0x428516 */
            continue;
        }
        pSurf = (AiNavNode *)pMesh->pSurface;
        if (pSurf != NULL) {
            flSupport =                                                   /* same plane formula */
                (float)((int)pCart->vPos.y + (int)pMesh->vWorldA.y - pSurf->nRefX) * pSurf->flSlopeX +
                (float)((int)pCart->vPos.x + (int)pMesh->vWorldA.x - pSurf->nRefZ) * pSurf->flSlopeZ +
                (float)pSurf->nRefY;
        } else {
            flSupport = pMesh->flHeight;
        }
        if (flSupport <= pMesh->flHeight) {                               /* @0x428530 */
            if (pMesh->flVertVel < g_flEighteen) {
                pMesh->flVertVel = 0.0f;                                  /* @0x428552 */
                pMesh->flHeight = flSupport;                              /* @0x428556 */
            } else {
                flVel = pMesh->flVertVel * g_fl0_75f;                     /* [0x44b6ec] @0x42855e */
                pMesh->flVertVel = flVel;
                pMesh->flHeight = flVel + pMesh->flHeight;
            }
        } else {
            flVel = pMesh->flVertVel + g_flEighteen;                      /* @0x428571 */
            pMesh->flVertVel = flVel;
            flVel = flVel + pMesh->flHeight;
            pMesh->flHeight = flVel;
            if (flSupport <= flVel) {                                     /* @0x42785b analog */
                float flOld = pMesh->flHeight;
                pMesh->flHeight = flSupport;
                pMesh->flVertVel = pMesh->flVertVel - (flOld - flSupport);
            }
        }
        if (pRec->flCartBounceIn != 0.0f) {                               /* +0x288 @0x428586 */
            int nR = rand() % 0x19 + 0x4b;                                /* @0x42858c */
            float flB = (float)nR * pRec->flCartBounceIn * 0.01f;         /* [0x44b6fc] @0x4285a6 */
            pMesh->flVertVel = flB + pMesh->flVertVel;                    /* @0x4285b4 */
            flVel = flB + pMesh->flHeight;                                /* @0x4285ba */
            pMesh->flHeight = flVel;
            if (flSupport <= flVel) {
                float flOld = pMesh->flHeight;
                pMesh->flHeight = flSupport;
                pMesh->flVertVel = pMesh->flVertVel - (flOld - flSupport);
            }
        }
        if (pMesh->flHeight < flMinH) {                                   /* @0x4285cd */
            flMinH = pMesh->flHeight;
        }
    }
    pMesh = objFindTurret(pCart, nCartKey);                               /* @0x4285e2 */
    if (pMesh != NULL) {
        float flCap = flMinH + g_fl300;                                   /* @0x4285e8 */
        for (; pMesh != NULL; pMesh = pMesh->pNext) {                     /* @0x4285ec */
            if (flCap < pMesh->flHeight) {
                pMesh->flHeight = flCap;
                pMesh->flVertVel = 0.0f;
            }
        }
    }
    i = 0;                                                                /* @0x428608 */
    for (pMesh = objFindTurret(pCart, nCartKey); pMesh != NULL && i < 16;
         pMesh = pMesh->pNext, i += 4) {
        afXw[i / 4] = pMesh->vPos.y;                                      /* @0x428610 */
        afZw[i / 4] = pMesh->vPos.x;                                      /* @0x428614 */
        afH[i / 4] = pMesh->flHeight;                                     /* @0x428620 */
    }
    gxVec2Set(&vIn, ((afXw[3] - afXw[2]) + afXw[1]) - afXw[0],            /* @0x42862c */
              ((afH[3] - afH[2]) + afH[1]) - afH[0]);
    mathVec2Polar(&vPolar1, &vIn);                                        /* @0x428662 */
    gxVec2Set(&vIn, ((afH[3] - afH[1]) + afH[2]) - afH[0],                /* @0x428668 */
              ((afZw[3] - afZw[1]) + afZw[2]) - afZw[0]);
    mathVec2Polar(&vPolar2, &vIn);                                        /* @0x42869d */
    pRec->wCartPitch = (short)(int)((vPolar2.y - g_flHalfPiPhy) * g_flBinAngle);  /* +0x28c */
    pRec->wCartRoll = (short)(int)(vPolar1.y * g_flBinAngle);                     /* +0x290 */
    pRec->flCartBounceIn = 0.0f;                                          /* +0x288 @0x4286a9 */
}

/* gameUpdate @0x426ee0 — per-frame gameplay dispatcher called from
 * gameWorldUpdate: step the thrown-item list, run the per-player physics
 * pass, optionally the world-object update, the thrown-item mesh pass
 * and the per-player mesh-channel syncs. */
void gameUpdate(void) /* @0x426ee0 */
{
    ThrownItemStub *pItem;
    int i;

    g_nGameUpdateTick++;                                                  /* @0x45e5dc @0x426ee0 */
    for (pItem = g_pThrownItemHead; pItem != NULL; pItem = pItem->pNext) { /* @0x426ef9 */
        itemThrowUpdate(pItem);                                           /* @0x40f950 @0x426efc */
    }
    for (i = 0; i < g_nPlayerCount; i++) {                                /* @0x426f17 */
        PlayerRecord *pRec = &g_playerRecords[i];
        if (pRec->field_174 == 0) {                                       /* +0x174 @0x426f17 */
            playerUpdateWalkPhysics(pRec);                                 /* @0x426fd0 @0x426f22 */
            playerUpdateOnFoot(pRec);                                     /* @0x427730 @0x426f28 */
        } else {
            playerUpdateCartPhysics(pRec);                                /* @0x4280b0 @0x426f32 */
        }
    }
    if (g_bCollisionEnabled == 0) {                                       /* [0x458354] @0x426f4f */
        objUpdateAll();                                                   /* @0x4055f0 @0x426f53 */
    }
    for (pItem = g_pThrownItemHead; pItem != NULL; pItem = pItem->pNext) { /* @0x426f62 */
        itemMeshFollowUpdate(pItem);                                      /* @0x40fde0 @0x426f64 */
    }
    for (i = 0; i < g_nPlayerCount; i++) {                                /* @0x426f80 */
        PlayerRecord *pRec = &g_playerRecords[i];
        if (pRec->field_174 == 0) {                                       /* @0x426f80 */
            if (pRec->nChannelsDirty != 0) {                              /* +0x2d8 @0x426f88 */
                syncWalkNodeChannelsToMesh(pRec);                         /* @0x428990 @0x426f95 */
            }
            syncPosNodeChannelsToMesh(pRec);                              /* @0x428840 @0x426f9e */
        } else if (pRec->nChannelsDirty != 0) {                           /* @0x426fa5 */
            syncCartNodeChannelsToMeshes(pRec);                           /* @0x428a70 @0x426faa */
        }
    }
}
