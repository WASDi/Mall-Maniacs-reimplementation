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
#include "pool.h"
#include "stubs.h"
#include "time.h"
#include "util.h"
#include "custom_helpers.h"

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
    pCtrl->pPlayerObj->nAnimationFrame = pCtrl->nSavedAnimationFrame;
    pCtrl->pPlayerObj->nAnimationTimer = pCtrl->nSavedAnimationTimer;
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
        pRec->nAnimationTimer = 0;                                         /* @0x410f90 */
        pRec->nAnimationFrame = 0;                                         /* @0x410f93 */
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
        pRec->flCartFrictionA = 0.0f;                                      /* +0x234 @0x4110ff */
        pRec->field_228_pad[0] = 0.0f;                                     /* +0x228 @0x411105 */
        pRec->field_228_pad[1] = 0.0f;                                     /* +0x22c @0x411107 */
        pRec->flCartFrictionB = 0.0f;                                      /* +0x244 @0x41110d */
        pRec->field_238_pad[0] = 0.0f;                                     /* +0x238 @0x41110d */
        pRec->field_238_pad[1] = 0.0f;                                     /* +0x23c @0x41110d */
        pRec->field_254 = 0.0f;                                            /* +0x254 @0x411128 */
        pRec->field_258 = 0.0f;                                            /* +0x258 @0x411130 */
        pRec->field_25c = 0.0f;                                            /* +0x25c @0x41111f */
        pRec->field_260 = 0.0f;                                            /* +0x260 @0x411125 */
        memcpy(&pRec->flAccSpeed, &pRec->field_228_pad[0], 0x40);          /* +0x1e8 <- +0x228 @0x411136 */
        memcpy(&pRec->flCartAccSpeed, &pRec->field_228_pad[0], 0x40);      /* +0x268 <- +0x228 @0x411144 */
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
        pRec->field_16c = 0;                                               /* @0x411302 */
        wsprintfA(szBuf, "carts[%d]", pRec->nCartIdx);                     /* @0x44f9b8 @0x411313 */
        pNode = configEnvGetValue(&g_configEnvMaster, pObjects, szBuf);    /* @0x411331 */
        if (pNode == NULL) {
            fatalError("'objects/carts[%d]' not found in cfg.",            /* @0x44f990 @0x411348 */
                       pRec->nCartIdx);
        }
        configEnvGetString(&mstrValue, pNode, "object_name");              /* @0x411360 */
        mStringAssignCopy(&pRec->mstrCartName, &mstrValue);                /* @0x411379 */
        mStringFree(&mstrValue);                                           /* @0x41138d */
        pRec->flCartFrictionA = (float)configEnvGetDouble2(&g_configEnvMaster, pNode, "friction"); /* @0x4113a2 */
        pRec->flCartFrictionB = (float)configEnvGetDouble2(&g_configEnvMaster, pNode, "friction"); /* @0x4113b8 */
        /* walk block: acc = (speed*0.7 + 100) * master acc (0x44b59c = 0.7f,
         * 0x44b598 = 100.0f — the decompiler's "0.06/0.7" naming is wrong). */
        pRec->flAccSpeed = ((float)pRec->nStatSpeed * 0.7f + 100.0f) * flAcc;          /* @0x4113be */
        pRec->flRotAccSpeed = ((float)pRec->nStatAgility * 0.1f + 0.5f) * flRotAcc;    /* @0x4113e0 */
        pRec->flFriction = flFriction;                                     /* +0x1ec @0x411402 */
        pRec->flFrictionB = flFriction;                                    /* +0x1f4 @0x411408 */
        pRec->flAccFric = pRec->flAccSpeed / (1.0f - pRec->flFriction);    /* @0x411414 */
        pRec->flRotAccFric = pRec->flRotAccSpeed / (1.0f - pRec->flFriction); /* @0x41142c */
        pRec->flCurAccFric = pRec->flAccFric;                              /* +0x230 @0x41143e */
        pRec->flCurRotAccFric = pRec->flRotAccFric;                        /* +0x240 @0x41144a */
        /* cart block: same shape with the WC constants and averaged friction. */
        pRec->flCartAccSpeed = ((float)pRec->nStatSpeed * 0.7f + 100.0f) * flAccWC;    /* @0x411456 */
        pRec->flCartRotAccSpeed = ((float)pRec->nStatAgility * 0.1f + 0.5f) * flRotAccWC; /* @0x41146f */
        pRec->flCartFrictionB2 = (flFriction + pRec->flCartFrictionB) * 0.5f;          /* +0x27c @0x41148c */
        pRec->flCartFriction = (flFriction + pRec->flCartFrictionA) * 0.5f;            /* +0x270 @0x41149f */
        pRec->flCartAccFric = pRec->flCartAccSpeed / (1.0f - pRec->flCartFriction);    /* @0x4114b0 */
        pRec->flCartRotAccFric = pRec->flCartRotAccSpeed / (1.0f - pRec->flCartFrictionB2); /* @0x4114c6 */
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

    {   /* TEMP DEBUG: dump the placed camera state */
        extern int g_nDbgRendArm; /* scene.c */
        int anChkI[3];
        g_nDbgRendArm = 1;    /* arm [rendbg] for the first gameplay frames */
        sceneNodeGetPos(g_pCamAimNode, 0, anChkI, 4);
        appLog("[camdbg] aimNode=%08x %08x %08x", anChkI[0], anChkI[1], anChkI[2]);
        sceneNodeGetPos(g_pCamPosNode, 0, anChkI, 4);
        appLog("[camdbg] posNode=%08x %08x %08x", anChkI[0], anChkI[1], anChkI[2]);
        sceneNodeGetPos(g_pSceneRoot, 0, anChkI, 4);
        appLog("[camdbg] root=%08x %08x %08x", anChkI[0], anChkI[1], anChkI[2]);
        appLog("[camdbg] root rot=%d %d %d", (int)((SceneNode *)g_pSceneRoot)->pChannels[0].rot[0],
               (int)((SceneNode *)g_pSceneRoot)->pChannels[0].rot[1],
               (int)((SceneNode *)g_pSceneRoot)->pChannels[0].rot[2]);
    }
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
