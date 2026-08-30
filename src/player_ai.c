#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "player.h"
#include "player_ai.h"
#include "config.h"
#include "stubs.h"
#include "options.h"
#include "level.h"
#include "levelselect.h"
#include "quest.h"
#include "sound.h"
#include "time.h"
#include "util.h"
#include "quest.h"
#include "scene_alloc.h"
#include "gx.h"

/* g_nPlayerAiTick @0x45894c — global AI tick, incremented once per
 * playerUpdateAI pass; drives the %5 checkout-tick decay. */
int g_nPlayerAiTick;                                /* @0x45894c */

/* g_nObjIdJumpPad @0x44f4a0 — the "sjmp" landing-zone object id used by the
 * jump-landing clamp in playerUpdateAI substate 2 (next to the "hurl"/
 * "tele"/"mvnc" ids in player_physics.c). */
int g_nObjIdJumpPad = 0x706d6a73;                   /* @0x44f4a0 "sjmp" */

/* "%s\hud\allah%d.tga" @0x44f4a8 — fling-björn full-screen frame. */
#define SZ_ALLAH_TGA "%s\\hud\\allah%d.tga"

int g_nNavPtSearchCount;
/* g_nControllerIdx @0x4550d0 — chooses the pair of AI players to update. */
int g_nControllerIdx;

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

/* moveStateSetSnapFlag @0x4020c0 — raise the controller's zone-snap flag
 * (+0x51) after a teleport pad moved the node. */
void moveStateSetSnapFlag(AiController *pCtrl) /* @0x4020c0 */
{
    pCtrl->bFlag51 = 1;                             /* +0x51 @0x4020c4 */
}


/* actionCmd @0x4067c0 — console "action <cmd>" handler (commandDispatch
 * name table @0x44b308): sets PlayerRecord.nActionSubstate (+0x2e8) from
 * the verb — smart (4/6 via playerFindCart on the LOCAL record, or 4 when
 * riding), grab cart 3, release cart 4, ride cart 7, get item 5,
 * drop item 6, jump (g_cCmdJump @0x44e718) 2, break 1. Leading blanks are
 * skipped; the verb gate rejects anything while a substate other than 0/3
 * is active. Returns 0. */
int actionCmd(int nContext, LPCSTR pszArgs) /* @0x4067c0 */
{
    PlayerRecord *pRec = (PlayerRecord *)nContext;
    int nFound;

    if (nContext == 0 ||
        (pRec->nActionSubstate != 0 && pRec->nActionSubstate != 3)) {
        return 0;                                   /* @0x4067e2..0x4067e8 */
    }
    if (*pszArgs == ' ') {                          /* @0x4067ec */
        while (*pszArgs == ' ') {
            pszArgs++;
        }
        if (*pszArgs == '\0') return 0;
    }
    if (strcmp(pszArgs, "smart") == 0) {            /* s_smart @0x44e760 */
        if (g_playerRecords[g_nLocalPlayerIdx].field_174 == 0) { /* @0x406842 */
            nFound = playerFindCart(&g_playerRecords[g_nLocalPlayerIdx]); /* @0x40e180 @0x40684b */
            pRec->nActionSubstate = (nFound != 0) ? 3 : 6;    /* @0x406857 */
        } else {
            pRec->nActionSubstate = 4;              /* riding: release @0x406851 */
        }
    }
    switch (*pszArgs) {                             /* @0x40683b */
    case 'g':
        if (strcmp(pszArgs, "grab cart") == 0) {    /* @0x44e754 */
            pRec->nActionSubstate = 3;              /* @0x4068c2 */
            return 0;
        }
        if (strcmp(pszArgs, "get item") == 0) {     /* @0x44e72c */
            pRec->nActionSubstate = 5;              /* @0x4069a9 */
            return 0;
        }
        break;
    case 'r':                                       /* @0x4068de */
        if (strcmp(pszArgs, "release cart") == 0) { /* @0x44e744 */
            pRec->nActionSubstate = 4;              /* @0x40690f */
            return 0;
        }
        if (strcmp(pszArgs, "ride cart") == 0) {    /* @0x44e738 */
            pRec->nActionSubstate = 7;              /* @0x406954 */
            return 0;
        }
        break;
    case 'd':                                       /* @0x4069ab */
        if (strcmp(pszArgs, "drop item") == 0) {    /* @0x44e720 */
            pRec->nActionSubstate = 6;              /* @0x4069f7 */
            return 0;
        }
        break;
    case 'j':                                       /* @0x406a02 */
        if (strcmp(pszArgs, "jump") == 0) {         /* g_cCmdJump @0x44e718 */
            pRec->nActionSubstate = 2;              /* @0x406a3f */
            return 0;
        }
        break;
    case 'b':                                       /* @0x406a45 */
        if (strcmp(pszArgs, "break") == 0) {        /* @0x44e710 */
            pRec->nActionSubstate = 1;              /* @0x406a8e */
        }
        break;
    default:
        break;
    }
    return 0;
}

/* --- AI movement / pickup cluster (0x40e180..0x40f760) --- */

static const double g_dblPi31415 = 3.1415;          /* @0x44b2b0 */
static const double g_dblMinusPi31415 = -3.1415;    /* @0x44b2a0 */
static const float g_flTwoPiF = 6.283f;             /* @0x44b2a8 */
static const float g_flPiLoop = 3.1415927f;         /* @0x44b29c (bytes 0F DB 49 40) */
static const float g_flNegPiLoop = -3.1415927f;     /* @0x44b294 */
static const float g_flTwoPiLoop = 6.2831855f;      /* @0x44b298 */
static const double g_dbl0_1 = 0.1;                 /* @0x44b550 */
static const double g_dblNeg0_1 = -0.1;             /* @0x44b548 */
static const float g_flQuarterPi = 0.7853982f;      /* @0x44b55c */
static const float g_flNegQuarterPi = -0.7853982f;  /* @0x44b558 */
static const float g_flHalfPiAi = 1.5707964f;       /* @0x44b270 */
static const float g_flNegHalfPi = -1.5707964f;     /* @0x44b56c */
static const double g_dbl3000 = 3000.0;             /* @0x44b538 */
static const double g_dbl1000 = 1000.0;             /* @0x44b540 */
static const float g_fl4000000 = 4000000.0f;        /* @0x44b560 */
static const float g_fl1000000 = 1000000.0f;        /* @0x44b564 */
static const float g_fl1690000 = 1690000.0f;        /* @0x44b570 */
static const float g_fl81000000 = 8.1e7f;           /* @0x44b574 */
static const float g_fl1500 = 1500.0f;              /* @0x44b568 */
static const float g_fl500 = 500.0f;                /* @0x44b2e4 */
static const float g_fl175 = 175.0f;                /* @0x44b45c */
static const float g_fl250Phy = 250.0f;             /* @0x44b460 */
static const float g_fl25 = 25.0f;                  /* @0x44b468 */
static const float g_fl50 = 50.0f;                  /* @0x44b46c */
static const float g_fl200 = 200.0f;                /* @0x44b470 */
static const float g_fl0_0333333 = 0.03333334f;     /* @0x44b474 */
static const float g_fl1_3 = 1.3f;                  /* @0x44b478 */
static const float g_flZero = 0.0f;                 /* @0x44b244 */
static const float g_flNegOne = -1.0f;              /* @0x44b25c */
static const float g_flOne = 1.0f;                  /* @0x44b260 */

#define AI_WRAP_SCALEA(pNode)                                            \
    do {                                                                  \
        while ((double)(pNode)->flScaleA > g_dblPi31415) {                \
            (pNode)->flScaleA -= g_flTwoPiF;                              \
        }                                                                 \
        while ((double)(pNode)->flScaleA < g_dblMinusPi31415) {           \
            (pNode)->flScaleA += g_flTwoPiF;                              \
        }                                                                 \
    } while (0)

/* aiTurnDiff — shared body of the three "aim at" helpers: wrap pNode's
 * flScaleA into ±3.1415 (AI_WRAP_SCALEA), relax the difference into ±pi
 * (g_flPiLoop @0x44b29c / g_flNegPiLoop @0x44b294, step g_flTwoPiLoop
 * @0x44b298), then re-wrap and recompute (matches the original's two
 * relaxation loops; e.g. @0x40e221/0x40e293/0x40e3b8/0x40ef99). */
static float aiTurnDiff(WorldNode *pNode, float flAngle) /* @0x40ef9f..0x40f0d5 */
{
    float flDiff;

    AI_WRAP_SCALEA(pNode);
    flDiff = flAngle - pNode->flScaleA;                 /* @0x40eff1 */
    while (flDiff > g_flPiLoop) {                       /* @0x40eff6..0x40f009 */
        flDiff -= g_flTwoPiLoop;
    }
    while (flDiff < g_flNegPiLoop) {                    /* @0x40f063..0x40f07b */
        flDiff += g_flTwoPiLoop;
    }
    AI_WRAP_SCALEA(pNode);                              /* @0x40f07d..0x40f0d3 */
    flDiff = flAngle - pNode->flScaleA;                 /* @0x40f0d5 */
    return flDiff;
}

/* playerFindCart @0x40e180 — 1 when the cart sub-object (+0x264) is within
 * 3000 (g_dbl3000 @0x44b538) of the walk node and the held slots (+0x17c,
 * +0x180) are empty. */
int playerFindCart(PlayerRecord *pRec) /* @0x40e180 */
{
    int i;

    if (objDistTo(pRec->pSubObjA, pRec->pSubObjC) > (float)g_dbl3000) {
        return 0;                                       /* @0x40e197..0x40e1a7 */
    }
    for (i = 0; i < 1; i++) {                           /* @0x40e1b0..0x40e1bc */
        if (pRec->anHeldSlot[0] != 0 ||                   /* +0x17c @0x40e1b0 */
            pRec->anHeldSlot[1] != 0) {                 /* +0x180 @0x40e1b5 */
            return 0;
        }
    }
    return 1;
}

/* aiStateCartApproach @0x40e1d0 — drive/turn toward the cart: returns 1 when
 * already riding (field_174) or when close enough to steer (cart node within
 * 1000 and facing within the ±0.1 band); *pAnimState gets 0x50 (turn),
 * 0x5a (drive), 0x6e (grab) or 100 (steer step). */
int aiStateCartApproach(PlayerRecord *pRec, int *pAnimState) /* @0x40e1d0 */
{
    float flDiff;
    float flDiffCart;
    if (pRec->field_174 != 0) return 1;                 /* @0x40e1d5 */
    if (playerFindCart(pRec) == 0) return 0;            /* @0x40e1e6 */

    pRec->flInputTurn = 0.0f;                           /* +0x2e0 @0x40e204 */
    pRec->flInputAccel = 0.0f;                          /* +0x2e4 @0x40e20e */
    flDiff = aiTurnDiff(pRec->pSubObjA,
                        objAngleTo(pRec->pSubObjA, pRec->pSubObjC)); /* @0x40e218..0x40e293 */
    if (flDiff > (float)g_dbl0_1 || flDiff < (float)g_dblNeg0_1) {
        if (flDiff >= g_flZero) {                       /* @0x40e581 */
            pRec->flInputTurn = g_flOne;                /* +0x2e0 @0x40e592 */
        } else {
            pRec->flInputTurn = g_flNegOne;             /* +0x2e0 @0x40e586 */
        }
        if (pAnimState != NULL) *pAnimState = 0x50;     /* 80 @0x40e5a4 */
        return 0;
    }
    if (objDistTo(pRec->pSubObjA, pRec->pSubObjC) > (float)g_dbl1000) {
        pRec->flInputAccel = g_flOne;                   /* +0x2e4 @0x40e3c6 */
        if (pAnimState != NULL) *pAnimState = 0x5a;     /* 90 @0x40e3a7 */
        return 0;
    }
    flDiffCart = aiTurnDiff(pRec->pSubObjC, flDiff);    /* @0x40e3b8..0x40e50b */
    if (fabs(flDiffCart) <= fabs(pRec->flPosTurnAccum)) { /* FABS/FCOMPP @0x40e50e */
        if (pAnimState != NULL) *pAnimState = 0x6e;     /* 110 @0x40e56c */
        return 1;
    }
    if (flDiff >= g_flZero) {                           /* @0x40e529 */
        pRec->flPosTurnAccum = pRec->flPosTurnAccum + pRec->flRotAccSpeed; /* @0x40e53e */
    } else {
        pRec->flPosTurnAccum = pRec->flPosTurnAccum - pRec->flRotAccSpeed; /* @0x40e530 */
    }
    if (pAnimState != NULL) *pAnimState = 100;          /* @0x40e558 */
    return 0;
}

/* playerUpdateOrientToTurret @0x40e5b0 — attach the walk node to its cart:
 * copy the walk channels into the cart block, move the cart sub-object to
 * the walk node's world position and drop both moveState flags. */
void playerUpdateOrientToTurret(PlayerRecord *pRec) /* @0x40e5b0 */
{
    int nX;
    int nZ;
    short nScale;
    float flAvgFront;
    int nSpeedBits;

    pRec->field_174 = 1;                                        /* @0x40e5b4 */
    ((WorldNode *)pRec->pSubObjC)->field_1c = 1;                       /* +0x1c @0x40e5ba */
    pRec->flCartTurnAccum = pRec->flTurnAccum;          /* +0x284 = +0x204 @0x40e5c0 */
    pRec->vCartVelPolar.y = pRec->vVelPolar.y;          /* +0x2a0 = +0x220 @0x40e5cd */
    pRec->flCartCurSpeed = pRec->flCurSpeed;            /* +0x274 = +0x1f4 @0x40e5d8 */
    nX = objPolarPosLookup(pRec->pSubObjA, (int)pRec->pCharSceneNode);   /* @0x40e5e8 */
    nZ = objPolarPosLookup2(pRec->pSubObjA, (int)pRec->pCharSceneNode);  /* @0x40e5fb */
    nScale = (short)objListFindFloat(pRec->pSubObjA, (int)pRec->pCharSceneNode); /* @0x40e5f6 */
    flAvgFront = nodeChannelAvgFloat(pRec->pSubObjA, (int)pRec->pCharSceneNode); /* @0x40e60c */
    memcpy(&nSpeedBits, &pRec->flCurSpeed, 4);          /* +0x1f4 raw bits @0x40e5d8 */
    nodeSetTransformFromChannels(pRec->pSubObjC, nX, (int)flAvgFront, nZ, nSpeedBits, nScale); /* @0x40e606 */
    ((WorldNode *)pRec->pSubObjA)->field_1c = 0;                       /* @0x40e661 */
    ((WorldNode *)pRec->pSubObjC)->field_1c = 0;                       /* @0x40e669 */
}

/* playerCheckTurn @0x40e670 — 1 when already riding, facing within the
 * ±0.1 band, or far away (>4000000) and pointing away; otherwise sets
 * +0x2e0 to the turn direction and returns 0. */
int playerCheckTurn(PlayerRecord *pRec) /* @0x40e670 */
{
    int nWalkX;
    int nWalkZ;
    int nCartX;
    int nCartZ;
    float flDiff;
    float flDx;
    float flDz;

    if (pRec->field_174 != 0) return 1;
    flDiff = aiTurnDiff(pRec->pSubObjA, objAngleTo(pRec->pSubObjA, pRec->pSubObjC)); /* @0x40e6c6 */
    nWalkX = objPolarPosLookup(pRec->pSubObjA, (int)pRec->pCharSceneNode);   /* @0x40e6f2 */
    nWalkZ = objPolarPosLookup2(pRec->pSubObjA, (int)pRec->pCharSceneNode);  /* @0x40e709 */
    nCartX = objPolarPosLookup(pRec->pSubObjC, (int)pRec->pCartSceneObj);    /* @0x40e72d */
    nCartZ = objPolarPosLookup2(pRec->pSubObjC, (int)pRec->pCartSceneObj);   /* @0x40e72d */
    if (flDiff <= (float)g_dbl0_1 && flDiff >= (float)g_dblNeg0_1) {
        return 1;                                       /* @0x40e7c1 */
    }
    flDx = (float)nWalkX - (float)nCartX;
    flDz = (float)nWalkZ - (float)nCartZ;
    if (flDx * flDx + flDz * flDz > g_fl4000000 &&
        (flDiff > g_flQuarterPi || flDiff < g_flNegQuarterPi)) {
        return 1;                                       /* @0x40e846..0x40e86a */
    }
    if (flDiff < g_flZero) {                            /* @0x40e8d9 */
        pRec->flInputTurn = g_flNegOne;                 /* +0x2e0 @0x40e8db */
    } else {
        pRec->flInputTurn = g_flOne;                    /* @0x40e8e5 */
    }
    return 0;
}

/* playerCheckBlocked @0x40ec40 — return the blocked-target EventObject for
 * pRec (via playerFindNearestTarget) unless a scenery zone object with an
 * id in [1,0x1e] sits within 1000 (1e6 squared @0x44b564) of the walk node,
 * in which case the approach is considered blocked and NULL is returned. */
EventObject *playerCheckBlocked(PlayerRecord *pRec) /* @0x40ec40 */
{
    EventObject *pObj;
    EventObject *pTarget;
    int nWalkX;
    int nWalkZ;
    int nIdx;
    float flDz;
    float flDy;
    GxVec2 vScratch;

    gxVec2SetAngleZero(&vScratch);                      /* @0x434f90 @0x40ec48 */                      /* @0x434f90 @0x40ec48 */
    if (pRec->anHeldSlot[0] != 0 || pRec->field_174 != 0) {
        return NULL;                                    /* @0x40ec51..0x40ec67 */
    }
    pTarget = playerFindNearestTarget(pRec);            /* @0x40ec6e */
    if (pTarget != NULL) {
        return pTarget;                                 /* @0x40ec78 */
    }
    nWalkX = objPolarPosLookup(pRec->pSubObjA, (int)pRec->pCharSceneNode);   /* @0x40ec88 */
    nWalkZ = objPolarPosLookup2(pRec->pSubObjA, (int)pRec->pCharSceneNode);  /* @0x40ec95 */
    for (nIdx = 0; (pObj = objFindByIdInRange(1, 0x1e, nIdx)) != NULL; nIdx++) { /* @0x40ecae */
        flDz = (float)nWalkZ - pObj->flOriginX;         /* +0x38 @0x40ecca */
        flDy = (float)nWalkX - pObj->flOriginZ;         /* +0x3c @0x40ecd1 */
        if (flDz * flDz + flDy * flDy <= g_fl1000000) {
            return NULL;                                /* zone blocks @0x40ecf1 */
        }
    }
    return NULL;
}

/* playerFindNearestTarget @0x40ed10 — nearest pickable world item for pRec
 * over the [1,0x1f] id range. Two branches per object:
 *  - burger (id 0x1f, human players only, ±1500 height band @0x44b568);
 *  - regular items 1..0x1e with a ±500 height band @0x44b2e4 and, per game
 *    mode, the list/quest filters: mode 1 skips nQuestTargetId (+0x1d8) and
 *    nLastThrownItemId (+0x1e4), mode 2 skips nLastThrownItemId, mode 3
 *    needs nListProgress < 5 and (g_nCurrentItemId match or a bonus value)
 *    and gives up beyond 5; modes outside 1..3 abort the scan. */
EventObject *playerFindNearestTarget(PlayerRecord *pRec) /* @0x40ed10 */
{
    EventObject *pObj;
    EventObject *pBest = NULL;
    int nWalkX;
    int nWalkZ;
    int nSlot;
    float flAvgFront;
    float flBest = 1000000.0f;                  /* 0x49742400 @0x40ed2c */
    float flD2;
    float flDz;
    float flDy;
    int nItemId;
    GxVec2 vScratch;

    gxVec2SetAngleZero(&vScratch);                  /* @0x434f90 @0x40ed1b */
    if (pRec->anHeldSlot[0] != 0 || pRec->field_174 != 0) {
        return NULL;                                /* @0x40ed34..0x40ed4a */
    }
    flAvgFront = nodeChannelAvgFloat(pRec->pSubObjA, (int)pRec->pCharSceneNode); /* @0x40ed5a */
    nWalkX = objPolarPosLookup(pRec->pSubObjA, (int)pRec->pCharSceneNode);   /* @0x40ed6d */
    nWalkZ = objPolarPosLookup2(pRec->pSubObjA, (int)pRec->pCharSceneNode);  /* @0x40ed80 */
    for (nSlot = 0; ; nSlot++) {                    /* @0x40ed93 */
        pObj = objFindByIdInRange(1, 0x1f, nSlot);
        if (pObj == NULL) {
            return pBest;                           /* @0x40ed9f */
        }
        nItemId = pObj->nId;                        /* +0x08 @0x40edae */
        if (pRec->nControlType != 2 && nItemId == 0x1f) {    /* burger @0x40edae */
            if (pObj->flHeightA > flAvgFront - g_fl1500 &&   /* +0x40 @0x40edb4 */
                flAvgFront < pObj->flHeightA + g_fl1500) {   /* @0x40edcb */
                flDz = (float)nWalkZ - pObj->flOriginX;   /* +0x38 @0x40ede0 */
                flDy = (float)nWalkX - pObj->flOriginZ;   /* +0x3c @0x40ede7 */
                flD2 = flDz * flDz + flDy * flDy;
                if (flD2 <= flBest) {
                    flBest = flD2;                  /* @0x40edf8 */
                    pBest = pObj;                   /* @0x40ee09 */
                }
                continue;
            }
        }
        if (pObj->nId == 0x1f) {                    /* @0x40ee14..0x40ee1a */
            continue;
        }
        if (pObj->flHeightA - g_fl500 >= flAvgFront ||   /* @0x40ee20 */
            flAvgFront >= pObj->flHeightA + g_fl500) {   /* @0x40ee3b */
            continue;                               /* @0x40ef2d */
        }
        flDz = (float)nWalkZ - pObj->flOriginX;     /* +0x38 @0x40ee50 */
        flDy = (float)nWalkX - pObj->flOriginZ;     /* +0x3c @0x40ee57 */
        flD2 = flDz * flDz + flDy * flDy;
        if (flD2 > flBest) {                        /* @0x40ee74 */
            continue;
        }
        if (g_nGameMode == 1) {                     /* @0x40ee83 */
            for (nSlot = 0; nSlot < 10; nSlot++) {  /* @0x40eef3 */
                if (pRec->abListTaken[nSlot] == 0 &&     /* +0x1ac @0x40eefe */
                    pRec->anListIds[nSlot] == nItemId &&
                    pRec->nQuestTargetId != pRec->anListIds[nSlot] &&
                    pRec->nLastThrownItemId != pRec->anListIds[nSlot]) {
                    flBest = flD2;
                    pBest = pObj;
                }
            }
        } else if (g_nGameMode == 2) {              /* @0x40eebf */
            for (nSlot = 0; nSlot < 10; nSlot++) {  /* @0x40eeca */
                if (pRec->abListTaken[nSlot] == 0 &&     /* +0x1ac */
                    pRec->anListIds[nSlot] == nItemId &&
                    pRec->nLastThrownItemId != pRec->anListIds[nSlot]) {
                    flBest = flD2;
                    pBest = pObj;
                }
            }
        } else if (g_nGameMode == 3) {              /* @0x40ee8e */
            if (pRec->anHeldSlot[1] < 5 &&          /* +0x180 @0x40ee95 */
                (g_nCurrentItemId == nItemId || pObj->field_14 != 0)) {  /* @0x40eea2 */
                pBest = pObj;
                flBest = flD2;
            }
        } else {
            return NULL;                            /* @0x40ef53 */
        }
    }
}

/* playerAiGrabItem @0x40ea20 — take the blocked target EventObject into the
 * held slots. Burger (id 0x1f): local player only — held id 0x1f, +0x40 =
 * the EventObject's own scene object (+0x14) re-parented onto the character
 * (class mesh 8, y=300, orient zeroed), EventObject removed from the hash.
 * Backref path (item +0x14 set, +0x24 thrown record): held slot = id, +0x40
 * = the thrown mesh, which is detached, hashed out, freed. Default path:
 * held = id, mesh allocated from the per-id table @0x4583b8 (ids <= 0x1e);
 * mode 3 resets g_nCurrentItemId (+0x10 log call) and pings peers
 * (server 0x20 / client 0x3e). */
int playerAiGrabItem(PlayerRecord *pRec) /* @0x40ea20 */
{
    EventObject *pObj;
    int nItemId;
    int nBurgerObj;

    pObj = playerCheckBlocked(pRec);                    /* @0x40ea28 */
    if (pObj == NULL) {
        return 0;                                       /* @0x40ea36 */
    }
    if (pRec == &g_playerRecords[g_nLocalPlayerIdx] && netIsActive() != 0) {
        netClientSendSubCmd(0x3c, g_nLocalPlayerIdx, pObj->nId, 0, 0, 0, 0); /* @0x40ea76 */
    }
    nItemId = pObj->nId;
    if (nItemId == 0x1f) {                              /* @0x40ea86 */
        if (pRec != &g_playerRecords[g_nLocalPlayerIdx]) {
            return 0;                                   /* @0x40eaa0 */
        }
        pRec->anHeldSlot[0] = 0x1f;                       /* +0x17c @0x40eaa5 */
        nBurgerObj = pObj->field_14;                    /* +0x14 @0x40eaaf */
        pRec->pGrabSceneObj = (SceneNode *)nBurgerObj;  /* +0x40 @0x40eab8 */
        sceneObjSetClassMesh(nBurgerObj, pRec->pCharSceneObj, 8, 3);   /* @0x40eabb */
        sceneObjSetPos(pRec->pGrabSceneObj, 0, 300, 0, 2);   /* @0x430660 @0x40eacf */
        sceneObjSetPosOrient(pRec->pGrabSceneObj, 0, 0, 0, 2); /* @0x4307d0 @0x40eae0 */
        objHashRemoveFree(pObj);                        /* @0x414990 @0x40eae6 */
        return pRec->anHeldSlot[0];                       /* +0x17c @0x40eaeb */
    }
    if (pObj->field_14 != 0 && pObj->field_24 != 0) {   /* @0x40eaf8 */
        pRec->anHeldSlot[0] = nItemId;                  /* +0x17c @0x40eb09 */
        pRec->pGrabSceneObj = *(SceneNode **)pObj->field_24; /* @0x40eb0f */
        sceneObjSetClassMesh((int)*(void **)pObj->field_24, pRec->pCharSceneObj, 8, 3); /* @0x40eb1e */
        sceneObjSetPos(*(SceneNode **)pObj->field_24, 0, 300, 0, 2);   /* @0x40eb32 */
        sceneObjSetPosOrient(*(SceneNode **)pObj->field_24, 0, 0, 0, 2); /* @0x40eb43 */
        objHashRemoveFree(pObj);                        /* @0x40eb49 */
        *(void **)pObj->field_24 = NULL;                /* +0x24 @0x40eb53 */
        thrownItemFree((ThrownItemStub *)pObj->field_24);    /* @0x40f8d0 @0x40eb5a */
        memFreeDirect((void *)pObj->field_24);          /* @0x43dd37 @0x40eb60 */
        return pRec->anHeldSlot[0];
    }
    pRec->anHeldSlot[0] = nItemId;                      /* +0x17c @0x40eb72 */
    nItemId = pObj->nId;                                /* +0x08 @0x40eb78 */
    if (nItemId <= 0x1e) {                              /* @0x40eb7b */
        pRec->pGrabSceneObj = (SceneNode *)sceneryObjAlloc(  /* @0x430200 @0x40eba3 */
            pRec->pCharSceneObj, nItemId * 5, 8, 0, 0, 300, 0, 0,
            &g_apGrabbMesh[nItemId]);                  /* 0x2c-stride table @0x4583b8 */
    }
    if (g_nGameMode == 3) {                             /* @0x40ebae */
        g_nCurrentItemId = 0;                           /* @0x458128 @0x40ebbe */
        nopDebugStub();                                 /* @0x440159 @0x40ebc8 */
        if (netIsActive() != 0) {
            if (g_nNetIsServer != 0) {                  /* @0x45e598 */
                netServerSendSubCmd(0x20, 0, g_nCurrentItemId, 0, 0, 0, 0); /* @0x40ebf5 */
                return pRec->anHeldSlot[0];
            }
            if (g_nNetIsClient != 0) {                  /* @0x45e59c */
                netClientSendSubCmd(0x3e, g_nLocalPlayerIdx, g_nCurrentItemId, 0, 0, 0, 0); /* @0x40ec27 */
            }
        }
    }
    return pRec->anHeldSlot[0];
}

/* playerUpdateAI @0x40b510 — the AI action phase machine. Per player:
 *  - phase 0xf: countdown nAiPhaseTimer; at 0 restore nAiPhaseNext; clear
 *    the input impulses while substate == 5 (get item) pending.
 *  - substate switch (+0x2e8): 2 = jump-landing clamp (support floor vs the
 *    child-mesh average, "sjmp" destination zone deepens the drop), 3 =
 *    cart approach/steer, 4 = release-cart sync, 5 = item action (the big
 *    nAiPhase switch), 6 = drop item; other substates only clear +0x2e8.
 *  - every player with +0x16c > 0 decays its checkout ticks every 5th AI
 *    tick and damps the input impulses by 1.3.
 * Phase-4 substates: 0 turn-to-target/pick decision (with the mode-1 quest
 * staging), 2/5 clear impulses, 3 grab the item, 6 burger delivery / fling
 * björn frame, 0xa arm the 0xf wait, 0xb cleanup. */
void playerUpdateAI(void) /* @0x40b510 */
{
    PlayerRecord *pRec;
    ObjChildMesh *pWalkMesh;
    AiNavNode *pSupport;
    EventObject *pObj;
    EventObject *pLanding;
    unsigned short *pTex;
    WorldNode *pWalkNode;
    int nPlayer;
    int nAnimState;
    int nWorldZ;
    int nWalkX;
    int nTaken;
    int nSfxIdx;
    int nThrowX;
    int nThrowZ;
    int i;
    char szPath[256];
    float flAvgFront;
    float flFloor;
    float flThrowDist;
    ThrownItemStub *pThrownItem;
    GxVec2 vThrowPolar;

    g_nPlayerAiTick++;                                  /* @0x45894c @0x40b531 */
    if (g_nPlayerCount <= 0) return;
    for (nPlayer = 0; nPlayer < g_nPlayerCount; nPlayer++) {
        pRec = &g_playerRecords[nPlayer];
        if (pRec->nAiPhase == 0xf) {                    /* @0x40b570 */
            if (pRec->nAiPhaseTimer < 1) {              /* @0x40b575 */
                pRec->nAiPhase = pRec->nAiPhaseNext;    /* +0x2f0 = +0x2f4 @0x40b57f */
            } else {
                pRec->nAiPhaseTimer--;                  /* +0x2f8 @0x40b58d */
            }
            if (pRec->nActionSubstate == 5) {           /* +0x2e8 @0x40b594 */
                pRec->nAiPhaseNext = 0;                 /* +0x2f4 @0x40b5a1 */
                pRec->flInputTurn = 0.0f;               /* +0x2e0 @0x40b5a7 */
            }
            continue;
        }
        switch (pRec->nActionSubstate) {                /* +0x2e8 @0x40b5be */
        case 1:                                         /* jump landing @0x40bc95 */
            flAvgFront = nodeChannelAvgFloat(pRec->pSubObjA, (int)pRec->pCharSceneNode); /* @0x40bc9f */
            pWalkMesh = objFindTurret(pRec->pSubObjA, (int)pRec->pCharSceneNode); /* @0x40bcb2 */
            flFloor = g_flZero;
            pSupport = (pWalkMesh != NULL) ? pWalkMesh->pSurface : NULL;  /* +0x3c @0x40bcb7 */
            if (pSupport != NULL) {
                nWorldZ = (int)((WorldNode *)pRec->pSubObjA)->vPos.x;   /* +0x20 @0x40bccc */
                nWalkX = (int)((WorldNode *)pRec->pSubObjA)->vPos.y;    /* +0x24 @0x40bce2 */
                flFloor = (float)
                    ((nWorldZ - pSupport->nRefZ) * pSupport->flSlopeZ + /* @0x40bcdc..0x40bcfa */
                     (nWalkX - pSupport->nRefX) * pSupport->flSlopeX +
                     pSupport->nRefY);
            }
            if (flFloor <= flAvgFront) {                /* @0x40bd03 */
                pLanding = objFindById(g_nObjIdJumpPad, 0); /* "sjmp" @0x44f4a0 @0x40bd0d */
                if (pLanding != NULL &&                 /* @0x40bd1a */
                    objContainsPoint(pLanding,
                                     (float)(int)((WorldNode *)pRec->pSubObjA)->vPos.y,   /* x-slot @0x40bd3b */
                                     (float)(int)((WorldNode *)pRec->pSubObjA)->vPos.x) != 0) {
                    pRec->flVertVel -= g_fl250Phy;      /* -250 @0x40bd5d */
                } else {
                    pRec->flVertVel -= g_fl175;         /* -175 @0x40bd6b */
                }
            }
            pRec->nActionSubstate = 0;                  /* @0x40bd7b */
            break;
        case 3:                                         /* grab cart @0x40b5c5 */
            if (playerFindCart(pRec) == 0) {            /* @0x40e180 @0x40b5c6 */
                pRec->nActionSubstate = 0;              /* @0x40b5d2 */
                pRec->nAiPhase = 0;
                break;
            }
            if (aiStateCartApproach(pRec, &nAnimState) != 0) { /* @0x40e1d0 @0x40b5e6 */
                playerUpdateOrientToTurret(pRec);       /* @0x40e5b0 @0x40b5f3 */
                pRec->nActionSubstate = 0;              /* @0x40b5fb */
                pRec->nAiPhase = 0;
                break;
            }
            if (nAnimState == 100 && pRec->nAiPhase != 2) {  /* @0x40b609 */
                pRec->nAiPhase = 1;                     /* @0x40b61e */
                pRec->pAnimSet = pRec->apAnmSets[8];    /* +0x2d4 = +0x2c8 grab @0x40b618 */
            }
            break;
        case 4:                                         /* release cart @0x40b630 */
            syncCartNodeChannelsToWalkPos(pRec);        /* @0x40e040 @0x40b631 */
            pRec->nActionSubstate = 0;                  /* @0x40b639 */
            break;
        case 5:                                         /* get item @0x40b6b0 */
            if (pRec->field_174 != 0) {                 /* @0x40b6b0 */
                pRec->nActionSubstate = 0;              /* @0x40b6b8 */
                pRec->nAiPhase = 0;
                break;
            }
            switch (pRec->nAiPhase) {                   /* @0x40b6d2 */
            case 0:                                     /* @0x40b6d9 */
                if (aiStateTurnToBlocked(pRec) != 0) {  /* @0x40ef60 @0x40b6da */
                    pObj = playerCheckBlocked(pRec);    /* @0x40ec40 @0x40b6eb */
                    if (pObj == NULL) {                 /* @0x40b6f5 */
                        pRec->nActionSubstate = 0;
                        pRec->nAiPhase = 0;
                        break;
                    }
                    if (g_nGameMode == 1) {             /* @0x40b708 */
                        if (pRec->pQuestMessage == NULL) {   /* +0x1dc @0x40b71b */
                            pRec->flInputTurn = 0.0f;
                            pRec->flInputAccel = 0.0f;
                            pRec->nQuestStage = 0;
                            pRec->pQuestMessage = questPickRandom(0); /* @0x410290 @0x40b738 */
                            break;                      /* @0x40b746 */
                        }
                        if (pRec->nQuestStage == 0) {   /* +0x1e0 @0x40b74b */
                            pRec->flInputTurn = 0.0f;   /* @0x40b755 */
                            pRec->flInputAccel = 0.0f;
                            break;
                        }
                        if (pRec->nQuestStage == *(int *)((char *)pRec->pQuestMessage + 0x14)) { /* @0x40b766 */
                            pRec->nQuestTargetId = 0;
                            pRec->nQuestStage = 0;
                            pRec->pQuestMessage = NULL;
                        } else {                        /* @0x40b76b */
                            pRec->nQuestFlags = -1;     /* +0x1d4 @0x40b76b */
                            pRec->nQuestStage = 0;
                            pRec->pQuestMessage = NULL;
                            nTaken = 0;
                            for (i = 0; i < 10; i++) {  /* @0x40b78e */
                                if (pRec->abListTaken[i] == 0) {
                                    nTaken++;
                                }
                            }
                            if (nTaken > 1) {           /* @0x40b79d */
                                pRec->nQuestTargetId = pObj->nId;    /* +0x08 @0x40b7a2 */
                            }
                            pRec->nActionSubstate = 0;  /* @0x40b7af */
                            pRec->nAiPhase = 0;
                            break;
                        }
                    }
                    /* mode != 1: pick the matching animation set and start
                     * the pickup approach (nAiPhase 1). */
                    if (pObj->field_14 == 1) {          /* @0x40b7d2 */
                        pRec->nAiPhase = 1;             /* @0x40b7dd */
                        pRec->pAnimSet = pRec->apAnmSets[2]; /* flpick1 +0x2b0 @0x40b7d7 */
                    } else {
                        pRec->nAiPhase = 1;
                        pRec->pAnimSet = pRec->apAnmSets[0]; /* pick1 @0x40b7ee */
                    }
                    break;
                }
                /* turnToBlocked == 0: keep walking; the blocked check still
                 * runs once so a lost target drops the state. */
                if (playerCheckBlocked(pRec) != 0) {    /* @0x40ec40 @0x40b805 */
                    break;                              /* keep approaching */
                }
                pRec->nActionSubstate = 0;              /* @0x40b815 */
                pRec->nAiPhase = 0;
                pRec->nQuestTargetId = 0;
                pRec->nQuestStage = 0;
                pRec->pQuestMessage = NULL;
                break;
            case 3:                                     /* @0x40b838 */
                pRec->flInputAccel = 0.0f;
                pRec->flInputTurn = 0.0f;
                if (playerCheckBlocked(pRec) == 0) {    /* @0x40ec40 @0x40b845 */
                    pRec->nActionSubstate = 0;
                    pRec->nAiPhase = 0;
                    break;
                }
                playerAiGrabItem(pRec);                 /* @0x40ea20 @0x40b863 */
                if (pRec->pAnimSet == pRec->apAnmSets[2]) {  /* +0x2d4 == +0x2b0 @0x40b877 */
                    pRec->nAiPhase = 4;                 /* @0x40b881 */
                    pRec->pAnimSet = pRec->apAnmSets[3];     /* flpick2 @0x40b87b */
                } else {
                    pRec->nAiPhase = 4;
                    pRec->pAnimSet = pRec->apAnmSets[1];     /* pick2 @0x40b89c */
                }
                break;
            case 6:                                     /* burger delivery @0x40b8b1 */
                pRec->flInputAccel = 0.0f;
                pRec->flInputTurn = 0.0f;
                if (pRec->anHeldSlot[0] == 0x1f) {      /* @0x40b8c0 */
                    nSfxIdx = (g_nLevelIdx & 0xff) + 0xf;    /* @0x40b8cd */
                    pRec->nCheckoutProgress = 100;      /* +0x16c @0x40b8de */
                    sndPlaySfx(0, 1, (unsigned int)nSfxIdx, 0xffff, 0, 0x400); /* @0x437cf0 @0x40b8e4 */
                    pRec->nAiPhase = 0xa;               /* @0x40b8ec */
                    break;
                }
                if (pRec == &g_playerRecords[g_nLocalPlayerIdx]) {   /* @0x40b914 */
                    g_nGameFrameActive = 1;             /* @0x45834c @0x40b924 */
                    g_nGameFrameActive2 = 1;            /* @0x458350 @0x40b931 */
                    fmtSprintf(szPath, SZ_ALLAH_TGA,
                               g_aszLevelDirs[g_nLevelIdx],
                               pRec->anHeldSlot[0]);    /* @0x44f4a8 @0x40b942 */
                    pTex = imageLoadByMode(szPath);     /* @0x4102e0 @0x40b94c */
                    if (pTex == NULL) {
                        presentFrame(g_nTexHudFlingbjorn);   /* @0x458348 @0x40b95f */
                    } else {
                        presentFrame((int)pTex);        /* @0x410310 @0x40b978 */
                        memPoolFree(0, pTex);           /* @0x419a60 @0x40f97f */
                    }
                }
                pRec->nAiPhase = 0xa;                   /* @0x40b987 */
                break;
            case 0xa:                                   /* @0x40b996 */
                pRec->nAiPhaseTimer = 0x18;             /* @0x40b99c */
                if (pRec->nControlType == 2) {          /* @0x40b9a6 */
                    pRec->nAiPhaseTimer = (4 - g_nModeSel) * 0x18;   /* @0x40b9ab */
                }
                pRec->nAiPhase = 0xf;                   /* @0x40b9c4 */
                pRec->nAiPhaseNext = 0xb;               /* +0x2f4 @0x40b9ce */
                break;
            case 0xb:                                   /* @0x40b9dd */
                pRec->flInputAccel = 0.0f;              /* @0x40b9e3 */
                pRec->flInputTurn = 0.0f;
                if (pRec == &g_playerRecords[g_nLocalPlayerIdx]) {   /* @0x40ba02 */
                    g_nGameFrameActive = 0;             /* @0x40ba06 */
                    g_nGameFrameActive2 = 0;
                }
                if (pRec->anHeldSlot[0] == 0x1f) {      /* @0x40ba12 */
                    sceneNodeSetHiddenFlag(pRec->pGrabSceneObj, 2);  /* @0x4305c0 @0x40ba21 */
                    pRec->anHeldSlot[0] = 0;
                }
                pRec->nAiPhase = 0;                     /* @0x40ba2f */
                break;
            case 2:                                     /* @0x40ba98 */
            case 5:
                pRec->flInputAccel = 0.0f;              /* @0x40ba98 */
                pRec->flInputTurn = 0.0f;
                break;
            default:                                    /* 1,4,7,8,9 @0x40ba2f */
                pRec->nAiPhase = 0;
                break;
            }
            break;
        case 6:                                         /* drop item @0x40ba3a */
            if (pRec->field_174 != 0 || pRec->anHeldSlot[0] == 0) {
                pRec->nActionSubstate = 0;              /* @0x40b6f7 */
                pRec->nAiPhase = 0;
                break;
            }
            switch (pRec->nAiPhase) {                   /* @0x40ba57 */
            case 0:                                     /* @0x40ba5e */
                if (aiCheckItemRange(pRec, 0) != 0 &&   /* @0x40f5e0 @0x40ba60 */
                    playerCheckTurn(pRec) == 0) {
                    break;                              /* keep steering @0x40ba75 */
                }
                pRec->pAnimSet = pRec->apAnmSets[4];    /* throw1 @0x40ba7d */
                pRec->nAiPhase = 1;
                break;
            case 3:                                     /* @0x40baa9 */
                pRec->flInputAccel = 0.0f;              /* @0x40baab */
                pRec->flInputTurn = 0.0f;
                if (aiCollectItem(pRec, 0) != 0) {      /* @0x40f420 @0x40bab7 */
                    break;                              /* @0x40bc7a */
                }
                if (pRec->anHeldSlot[0] == 0) {         /* @0x40bacd */
                    break;
                }
                if (pRec == &g_playerRecords[g_nLocalPlayerIdx]) {   /* @0x40baeb */
                    sndPlaySfx(0, 1, 0x15, 0xffff, 0, 0x400);    /* @0x437cf0 @0x40baff */
                }
                pWalkNode = (WorldNode *)pRec->pSubObjA;         /* +0x224 @0x40bb25 */
                nThrowX = (int)pWalkNode->vPos.y;       /* ftol @0x40bb2e */
                nThrowZ = (int)pWalkNode->vPos.x;       /* ftol @0x40bb44 */
                flThrowDist = objDistToPoint(pRec->pSubObjC,        /* @0x405050 @0x40bb5b */
                                             (float)nThrowZ,
                                             (float)nThrowX);
                flThrowDist *= g_fl0_0333333;           /* @0x40bb60 */
                if (flThrowDist > g_fl200) {
                    flThrowDist = g_fl200;              /* @0x40bb77 */
                }
                /* distance-based throw: the polar length is
                 * -(distance) - 50, +25 for the throw vector below. */
                flThrowDist = -(flThrowDist * g_flNegOne) - g_fl50;
                pThrownItem = (ThrownItemStub *)malloc(0x30);  /* operator_new @0x43dd42 @0x40bb95 */
                if (pThrownItem != NULL) {
                    AI_WRAP_SCALEA(pWalkNode);          /* @0x40bbb2..0x40bc0a */
                    gxVec2Set(&vThrowPolar, flThrowDist + g_fl25,
                              pWalkNode->flScaleA);     /* @0x434fa0 @0x40bc2e */
                    playerThrowItemCtor(pThrownItem, pRec->anHeldSlot[0]);  /* @0x40f720 @0x40bc61 */
                }
                pRec->anHeldSlot[0] = 0;                /* @0x40bc71 */
                pRec->pGrabSceneObj = NULL;             /* @0x40bc77 */
                break;
            default:                                    /* 1,4 @0x40ba2f */
                pRec->nAiPhase = 0;
                break;
            }
            break;
        }
        /* Checkout tick decay + input damping (+0x16c, x1.3 @0x44b478). */
        if (pRec->nCheckoutProgress > 0) {              /* +0x16c @0x40b63f */
            if (g_nPlayerAiTick % 5 == 0) {             /* @0x40b654 */
                pRec->nCheckoutProgress--;
            }
            pRec->flInputTurn *= g_fl1_3;               /* @0x40b661 */
            pRec->flInputAccel *= g_fl1_3;              /* @0x40b679 */
        }
    }
}

/* aiStateTurnToBlocked @0x40ef60 — aim the walk node at the blocked target:
 * 1 when facing within the ±0.1 band AND within 1000 of the target;
 * otherwise steers (+0x2e0 turn dir, +0x2e4 speed 1 / -1 backup). */
int aiStateTurnToBlocked(PlayerRecord *pRec) /* @0x40ef60 */
{
    EventObject *pObj;
    float flDiff;
    float flDist;

    pObj = playerCheckBlocked(pRec);                    /* @0x40ef68 */
    if (pObj == NULL) {
        return 0;
    }
    flDiff = aiTurnDiff(pRec->pSubObjA,                         /* @0x40ef94 */
                        objAngleToPoint(pRec->pSubObjA, pObj->flOriginX, pObj->flOriginZ));
    if (flDiff <= (float)g_dbl0_1 && flDiff >= (float)g_dblNeg0_1) {
        flDist = objDistToPoint(pRec->pSubObjA, pObj->flOriginX, pObj->flOriginZ); /* @0x40f11c */
        if (flDist > (float)g_dbl1000) {
            pRec->flInputAccel = g_flOne;               /* +0x2e4 = 1.0 @0x40f12e */
            return 0;
        }
        return 1;                                       /* @0x40f13f */
    }
    if (flDiff <= g_flHalfPiAi && flDiff >= g_flNegHalfPi) { /* @0x40f147..0x40f167 */
        if (flDiff < g_flZero) {                        /* @0x40f175 */
            pRec->flInputTurn = g_flNegOne;             /* +0x2e0 @0x40f17a */
        } else {
            pRec->flInputTurn = g_flOne;                /* +0x2e0 @0x40f18a */
        }
        return 0;
    }
    pRec->flInputAccel = g_flNegOne;                    /* +0x2e4 @0x40f19a */
    return 0;
}

/* playerCheckTargetRange @0x40f4d0 — 1 when the held item in slot nSlot is
 * within 1690000 (g_fl1690000 @0x44b570) of the cart sub-object and the game
 * mode allows collecting (1/2 also need the free list slot scan). */
int playerCheckTargetRange(PlayerRecord *pRec, int nSlot) /* @0x40f4d0 */
{
    int nWalkX;
    int nWalkZ;
    int nCartX;
    int nCartZ;
    float flDx;
    float flDz;
    int i;

    if (nSlot > 1 || pRec == NULL) return 0;
    if (pRec->anHeldSlot[nSlot] <= 0) return 0;        /* +0x17c+4*nSlot @0x40f4ee */
    nWalkX = objPolarPosLookup(pRec->pSubObjA, (int)pRec->pCharSceneNode);   /* @0x40f507 */
    nWalkZ = objPolarPosLookup2(pRec->pSubObjA, (int)pRec->pCharSceneNode);  /* @0x40f522 */
    nCartX = objPolarPosLookup(pRec->pSubObjC, (int)pRec->pCartSceneObj);    /* @0x40f53d */
    nCartZ = objPolarPosLookup2(pRec->pSubObjC, (int)pRec->pCartSceneObj);   /* @0x40f558 */
    flDx = (float)nWalkX - (float)nCartX;
    flDz = (float)nWalkZ - (float)nCartZ;
    if (flDx * flDx + flDz * flDz > g_fl1690000) return 0;   /* @0x40f57b */
    if (g_nGameMode <= 0) return 0;                     /* @0x40f591 */
    if (g_nGameMode <= 2) {                             /* @0x40f595 */
        for (i = 0; i < 10; i++) {                      /* @0x40f5b6 */
            if (pRec->anListIds[i] == pRec->anHeldSlot[nSlot] &&
                pRec->abListTaken[i] == 0) {
                break;                                  /* @0x40f5be */
            }
        }
    } else if (g_nGameMode != 3) {                      /* @0x40f59a */
        return 0;
    }
    return 1;                                           /* @0x40f5c9 */
}

/* playerCollectItem @0x40f1f0 — record the pickup of nItemId. Modes 1/2 fill
 * the first free list slot matching nItemId; with < 3 items collected the
 * thrown mesh becomes held loot (random bobbing offsets), otherwise it is
 * freed; +0x180 is the next free slot. Mode 3 only counts. NOTE: the
 * original passes the thrown mesh through a 16-bit parameter slot; the
 * rebuild keeps the full pointer (documented deviation). */
int playerCollectItem(PlayerRecord *pRec, int nItemId, void *pThrownMesh) /* @0x40f1f0 */
{
    int nSlot;
    int nTaken;
    int nTakenSlot;

    if (g_nGameMode <= 0) return 0;
    if (g_nGameMode < 3) {                              /* @0x40f20e */
        for (nSlot = 0; nSlot < 10; nSlot++) {          /* @0x40f1f5 */
            nTaken = 0;
            for (nTakenSlot = 0; nTakenSlot < 10; nTakenSlot++) { /* @0x40f1fc */
                if (pRec->abListTaken[nTakenSlot] != 0) {
                    nTaken++;
                }
            }
            if (pRec->anListIds[nSlot] != nItemId || pRec->abListTaken[nSlot] != 0) {
                continue;                               /* @0x40f222 */
            }
            pRec->abListTaken[nSlot] = nItemId;         /* +0x1ac+i*4 @0x40f23c */
            if (pThrownMesh != NULL) {                  /* @0x40f246 */
                if (nTaken < 3) {
                    sceneObjSetClassMesh((int)pThrownMesh, pRec->pCartSceneObj, 0, 3); /* @0x40f265 */
                    sceneObjSetPos(pThrownMesh, rand() % 0xfa - 0x7d, -500,
                                   rand() % 500 - 250, 2);   /* @0x40f2ab */
                    sceneObjSetPosOrient(pThrownMesh, rand() % 32000, (short)(rand() % 3200),
                                         (short)(rand() % 32000), 2); /* @0x40f2ab */
                } else {
                    sceneNodeFree(pThrownMesh, 3);          /* @0x40f3dd */
                }
            }
            pRec->anHeldSlot[1] = 0;                    /* +0x180 @0x40f3ea */
            /* +0x180 walk @0x40f26f */
            while (pRec->anHeldSlot[1] < 10 &&
                   pRec->abListTaken[pRec->anHeldSlot[1]] != 0) {
                pRec->anHeldSlot[1]++;
            }
            return 1;                                   /* @0x40f2c8 */
        }
        return 0;
    }
    if (g_nGameMode != 3) return 0;                     /* @0x40f1ed */
    if (pThrownMesh != NULL) {
        sceneObjSetClassMesh((int)pThrownMesh, pRec->pCartSceneObj, 0, 3); /* @0x40f265 */
        sceneObjSetPos(pThrownMesh, rand() % 0xfa - 0x7d, -500,
                       rand() % 500 - 250, 2);
        sceneObjSetPosOrient(pThrownMesh, (short)(rand() % 32000),
                             (short)(rand() % 3200), (short)(rand() % 32000), 2);
    }
    pRec->anHeldSlot[1]++;                              /* +0x180 */
    return 1;
}

/* aiCollectItem @0x40f420 — collect the held item (slot nSlot): range check
 * via playerCheckTargetRange, then playerCollectItem; on success the local
 * player pings peers (0x3d) and plays sfx 0x16, and the slot is cleared. */
int aiCollectItem(PlayerRecord *pRec, int nSlot) /* @0x40f420 */
{
    if (playerCheckTargetRange(pRec, nSlot) == 0) return 0;
    if (playerCollectItem(pRec, pRec->anHeldSlot[nSlot],
                          pRec->pGrabSceneObj) == 0) {
        return 0;
    }
    if (pRec == &g_playerRecords[g_nLocalPlayerIdx]) {
        if (netIsActive() != 0) {
            netClientSendSubCmd(0x3d, g_nLocalPlayerIdx,
                                pRec->anHeldSlot[nSlot], 0, 0, 0, 0); /* @0x40f493 */
        }
        sndPlaySfx(0, 1, 0x16, 0xffff, 0, 0x400);       /* @0x40f4ad */
    }
    pRec->anHeldSlot[nSlot] = 0;                       /* @0x40f4b5 */
    return 1;
}

/* aiCheckItemRange @0x40f5e0 — AI pickup range (8.1e7 = 9000 squared) with
 * modes 1..3; the mode 1/2 list-slot scan mirrors playerCheckTargetRange
 * (its per-slot result does not gate the return: any in-range held item
 * with a free matching list slot passes). */
int aiCheckItemRange(PlayerRecord *pRec, int nSlot) /* @0x40f5e0 */
{
    int i;
    int nWalkX;
    int nWalkZ;
    int nCartX;
    int nCartZ;
    float flDx;
    float flDy;

    if (nSlot > 1 || pRec == NULL) return 0;
    if (pRec->anHeldSlot[nSlot] <= 0) return 0;
    nWalkX = objPolarPosLookup(pRec->pSubObjA, (int)pRec->pCharSceneNode);   /* @0x40f617 */
    nWalkZ = objPolarPosLookup2(pRec->pSubObjA, (int)pRec->pCharSceneNode);  /* @0x40f632 */
    nCartX = objPolarPosLookup(pRec->pSubObjC, (int)pRec->pCartSceneObj);    /* @0x40f64d */
    nCartZ = objPolarPosLookup2(pRec->pSubObjC, (int)pRec->pCartSceneObj);   /* @0x40f668 */
    flDx = (float)nWalkZ - (float)nCartZ;
    flDy = (float)nWalkX - (float)nCartX;
    if (flDx * flDx + flDy * flDy > g_fl81000000) return 0;
    if (g_nGameMode <= 0 || g_nGameMode > 3) return 0;
    for (i = 0; i < 10; i++) {
        if (pRec->anListIds[i] == pRec->anHeldSlot[nSlot] &&
            pRec->abListTaken[i] == 0) {
            return 1;                                   /* @0x40f6d4 */
        }
    }
    return 0;
}
