#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "player.h"
#include "player_setup.h"
#include "player_ai.h"
#include "player_camera.h"
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

/* g_playerRecords @0x456210 — the 0x374-byte gameplay player records.
 * The controller view (original base 0x456524) is the embedded
 * PlayerRecord.ai member. */
PlayerRecord g_playerRecords[8];

/* g_nResultsScreen @0x458130 — results-screen gate in gameWorldUpdate. */
int g_nResultsScreen;

/* g_nCurrentItemId @0x458128 — mode 3 target item id, reset by
 * playerSetupRound (checked by the AI state machine). */
int g_nCurrentItemId;

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
        pRec->nActionSubstate = 0;                                         /* @0x410f95 */
        pRec->nAiPhase = 0;                                                /* @0x410f98 */
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
            pRec->anHeldSlot[1] = 0;                                       /* @0x41100d */
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
            pRec->anHeldSlot[1] = 0;                                       /* @0x411073 */
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
        pRec->anHeldSlot[0] = 0;                                             /* @0x4110f7 */
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
