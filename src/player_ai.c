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
#include "custom_helpers.h"
#include "gx.h"
#include "nav.h"
#include "gameplay.h"
#include "obj_event.h"

// For debugging
#define DISABLE_AI 0

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

/* g_pThrownItemHead/Tail @0x45896c/0x458970 — thrown-item list (head =
 * newest). g_nThrownItemCount @0x458974 caps it at 10. */
ThrownItem *g_pThrownItemHead;                      /* @0x45896c */
ThrownItem *g_pThrownItemTail;                      /* @0x458970 */
int g_nThrownItemCount;                             /* @0x458974 */

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
    pCtrl->pNavTarget = 0;
    pCtrl->pNavCurrent = 0;
    pCtrl->pPlayerObj = pRecord;
    pCtrl->nSavedAnimationFrame = 0;
    pCtrl->nSavedAnimationTimer = 0;
    pCtrl->bFlag50 = 0;
    pCtrl->bFlag51 = 0;
    pCtrl->bFlag52 = 0;
    pCtrl->nStuckTicks = 0;
    pCtrl->vStuckPos.x = 0;
    pCtrl->vStuckPos.y = 0;
    pCtrl->flCtrlSpeed = g_aflAiCtrlSpeed[g_nModeSel]; /* raw float copy @0x4010c9 */
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
int actionCmd(intptr_t nContext, LPCSTR pszArgs) /* @0x4067c0 */
{
    PlayerRecord *pRec = (PlayerRecord *)(uintptr_t)nContext;
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
        if (g_playerRecords[g_nLocalPlayerIdx].nCartMode == 0) { /* @0x406842 */
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
/* --- AI controller cluster constants (0x401160..0x402060) --- */
static const float g_flFltMax = 3.4028235e38f;      /* @0x44b240 (0x7f7fffff) */
static const float g_fl1210000 = 1210000.0f;        /* @0x44b23c (1100 squared) */
static const float g_fl490000 = 490000.0f;          /* @0x44b248 (700 squared) */
static const float g_fl700 = 700.0f;                /* @0x44b24c */
static const float g_fl0_00125 = 0.00125f;          /* @0x44b250 (1/800) */
static const float g_fl800 = 800.0f;                /* @0x44b254 */
static const float g_fl2250000 = 2250000.0f;        /* @0x44b258 (1500 squared) */
static const float g_fl2000 = 2000.0f;              /* @0x44b264 */
static const float g_fl0_1f = 0.1f;                 /* @0x44b268 */
static const float g_fl0_9f = 0.9f;                 /* @0x44b26c */
static const float g_fl2 = 2.0f;                    /* @0x44b278 */
static const float g_fl1100 = 1100.0f;              /* @0x44b27c */
static const float g_fl1_27324 = 1.2732395f;        /* @0x44b290 (4/pi) */
/* thrown-item cluster constants */
static const float g_fl1200 = 1200.0f;              /* @0x44b578 */
static const float g_fl0_999 = 0.999f;              /* @0x44b57c */
static const float g_flNeg15 = -15.0f;              /* @0x44b580 */
static const float g_fl15 = 15.0f;                  /* @0x44b584 */
static const float g_flNeg0_4 = -0.4f;              /* @0x44b588 */
static const float g_fl18 = 18.0f;                  /* @0x44b58c */
static const float g_flNeg100 = -100.0f;            /* @0x44b590 */
static const float g_fl100 = 100.0f;                /* @0x44b594 */
static const float g_fl1000 = 1000.0f;              /* @0x44b464 */

#define AI_WRAP_SCALEA(pNode)                                            \
    do {                                                                  \
        while ((double)(pNode)->flScaleA > g_dblPi31415) {                \
            (pNode)->flScaleA -= g_flTwoPiF;                              \
        }                                                                 \
        while ((double)(pNode)->flScaleA < g_dblMinusPi31415) {           \
            (pNode)->flScaleA += g_flTwoPiF;                              \
        }                                                                 \
    } while (0)

/* --- AI decision cluster (0x4015c0..0x402060) ---
 * Position vec2s passed through this cluster use the WorldNode.vPos order
 * {x = vPos.x ("z" axis), y = vPos.y ("x" axis)}; NavPoint nPosZ (+0x54,
 * config "Z") pairs with vPos.x and nPosX (+0x4c, config "X") with vPos.y.
 * navPointFindNearestInYRange(nX = vPos.y, nZ = vPos.x, ...) per the
 * original @0x4259d0 body. */

/* commandDispatch payloads for the AI action verbs (actionCmd @0x4067c0) */
#define SZ_ACTION_DROP_ITEM    "action drop item"      /* @0x44e1cc */
#define SZ_ACTION_GRAB_CART    "action grab cart"      /* @0x44e1e0 */
#define SZ_ACTION_GET_ITEM     "action get item"       /* @0x44e1f4 */
#define SZ_ACTION_RELEASE_CART "action release cart"   /* @0x44e11c */

/* sceneObjGetPosXZ @0x4099d0 — write the player's ground position to pOut:
 * node = cart node (+0x2a4) when nCartMode is set, else walk node (+0x224);
 * pOut[0] = node->vPos.x (+0x20), pOut[1] = node->vPos.y (+0x24). */
void sceneObjGetPosXZ(PlayerRecord *pRec, float *pOut) /* @0x4099d0 */
{
    WorldNode *pNode = (pRec->nCartMode != 0)          /* @0x4099d0 */
        ? (WorldNode *)pRec->pSubObjC                  /* +0x2a4 @0x4099da */
        : (WorldNode *)pRec->pSubObjA;                 /* +0x224 @0x4099e3 */
    pOut[0] = pNode->vPos.x;                           /* +0x20 */
    pOut[1] = pNode->vPos.y;                           /* +0x24 */
}

/* playerGetPos @0x409a10 — write the player's walk position to pOut:
 * with a cart (nCartMode), sceneNodeGetPos(pCartSceneObj, 0, buf, 2) gives
 * the channel translation {x, y, z} and pOut[0] = (float)buf[2] (z axis =
 * vPos.x), pOut[1] = (float)buf[0] (x axis = vPos.y); without, pOut comes
 * straight from the pos node (+0x264) fields. */
void playerGetPos(PlayerRecord *pRec, float *pOut) /* @0x409a10 */
{
    if (pRec->nCartMode != 0) {                        /* @0x409a16 */
        int anPos[3];
        sceneNodeGetPos(pRec->pCartSceneObj, 0, anPos, 2); /* @0x431270 @0x409a36 */
        pOut[0] = (float)anPos[2];                     /* channel z @0x409a3b */
        pOut[1] = (float)anPos[0];                     /* channel x @0x409a4a */
        return;
    }
    {
        WorldNode *pNode = (WorldNode *)pRec->pSubObjB; /* +0x264 @0x409a66 */
        pOut[0] = pNode->vPos.x;                       /* +0x20 */
        pOut[1] = pNode->vPos.y;                       /* +0x24 */
    }
}

/* sceneObjGetHeightChar @0x409a90 — ftol of nodeChannelAvgFloat over the
 * cart node (+0x2a4) when riding, else the walk node (+0x224), keyed by
 * the record's char channel (+0x10). Feeds the nav Y range in
 * playerAiUpdate (floor selection). */
int sceneObjGetHeightChar(PlayerRecord *pRec) /* @0x409a90 */
{
    WorldNode *pNode = (pRec->nCartMode != 0)
        ? (WorldNode *)pRec->pSubObjC                  /* @0x409a9d */
        : (WorldNode *)pRec->pSubObjA;                 /* @0x409ab1 */
    return (int)nodeChannelAvgFloat(pNode, (intptr_t)pRec->pCharSceneNode); /* @0x4050c0 */
}

/* sceneObjGetHeightCart @0x409ad0 — ftol of nodeChannelAvgFloat over the
 * cart node (+0x2a4) when riding, else the pos node (+0x264), keyed by the
 * record's cart channel (+0x14). Cached at AiController+0x28. */
int sceneObjGetHeightCart(PlayerRecord *pRec) /* @0x409ad0 */
{
    WorldNode *pNode = (pRec->nCartMode != 0)
        ? (WorldNode *)pRec->pSubObjC                  /* @0x409add */
        : (WorldNode *)pRec->pSubObjB;                 /* @0x409af1 */
    return (int)nodeChannelAvgFloat(pNode, (intptr_t)pRec->pCartSceneObj); /* @0x4050c0 */
}

/* aiSteerToTarget @0x401ae0 — drive pFromPos toward pToPos: writes
 * flInputTurn (+0x2e0, heading diff ×4/pi clamped to ±1) and flInputAccel
 * (+0x2e4, speed by distance/facing band, clamped ±1). Uses the walk node
 * (+0x224, turn step flAccFric +0x1f0) on foot and the cart node (+0x2a4,
 * flCartFriction +0x270) when riding. The stuck detector (+0x54 ticks,
 * +0x58/+0x5c position sample) raises bFlag51 and clears bFlag50 after
 * 875 update ticks within 1500 of the sample. */
void aiSteerToTarget(AiController *pCtrl, float *pFromPos, float *pToPos) /* @0x401ae0 */
{
    GxVec2 vDelta;
    GxVec2 vPolar;
    PlayerRecord *pRec = pCtrl->pPlayerObj;
    WorldNode *pNode;
    float flTurnStep;
    float flDiff;
    float flFactor;

    gxVec2Sub(&vDelta, (const GxVec2 *)pToPos, (const GxVec2 *)pFromPos); /* @0x435020 @0x401b07 */
    mathVec2Polar(&vPolar, &vDelta);                   /* {len, ang} @0x435060 @0x401b14 */

    if (pRec->nCartMode != 0) {                        /* @0x401b2b */
        pNode = (WorldNode *)pRec->pSubObjC;           /* +0x2a4 @0x401b35 */
        flTurnStep = pRec->flCartFriction;             /* +0x270 @0x401b3b */
    } else {
        pNode = (WorldNode *)pRec->pSubObjA;           /* +0x224 @0x401ba4 */
        flTurnStep = pRec->flAccFric;                  /* +0x1f0 @0x401baa */
    }
    AI_WRAP_SCALEA(pNode);                             /* ±3.1415, step 6.283f @0x401b45 */

    flDiff = vPolar.y - pNode->flScaleA;               /* @0x401c0d */
    while (flDiff > g_flPiLoop) {                      /* @0x401c14..0x401c32 */
        flDiff -= g_flTwoPiLoop;
    }
    while (flDiff < g_flNegPiLoop) {                   /* @0x401c34..0x401c52 */
        flDiff += g_flTwoPiLoop;
    }
    pRec->flInputTurn = flDiff * g_fl1_27324;          /* ×4/pi @0x401c5d */
    if (pRec->flInputTurn > 1.0) {                     /* @0x401c6b */
        pRec->flInputTurn = g_flOne;
    } else if (pRec->flInputTurn < -1.0) {             /* @0x401c8a */
        pRec->flInputTurn = g_flNegOne;
    }

    if (vPolar.x < g_fl1100) {                         /* @0x401ca3 */
        if (fabsf(flDiff) > g_fl2) {                   /* @0x401cb4..0x401ccf */
            float flNodeSpeed = pNode->field_3c;       /* +0x3c @0x401b4e */
            if (flTurnStep * pCtrl->flCtrlSpeed * g_flHalf >= flNodeSpeed) {
                pRec->flInputAccel = -pCtrl->flCtrlSpeed; /* back off @0x401cea */
                goto apply;
            }
        }
    }
    /* speed branch @0x401cf6 */
    {
        float flAbs = fabsf(flDiff);
        if (flAbs > g_flHalfPiAi) {
            flFactor = g_flZero;                       /* @0x401d16 */
        } else {
            float c = cosf(flAbs);                     /* @0x401d2d */
            flFactor = c * c * g_fl0_9f + g_fl0_1f;    /* @0x401d33..0x401d39 */
        }
        if (vPolar.x < g_fl2000) {                     /* @0x401d3f */
            pRec->flInputAccel = ((g_flOne - flFactor) * g_fl2000 + vPolar.x)
                * pCtrl->flCtrlSpeed * flFactor
                / ((g_fl2 - flFactor) * g_fl2000);     /* @0x401d50..0x401d77 */
        } else {
            pRec->flInputAccel = flFactor * pCtrl->flCtrlSpeed; /* @0x401d83 */
        }
    }
apply:
    if (pRec->flInputAccel < g_flNegOne) {             /* @0x401d90 */
        pRec->flInputAccel = g_flNegOne;
    } else if (pRec->flInputAccel > g_flOne) {         /* @0x401dab */
        pRec->flInputAccel = g_flOne;
    }

    {                                                  /* stuck detector @0x401dc8 */
        float flDx = pCtrl->vStuckPos.y - pCtrl->vSelfXZ.y;
        float flDy = pCtrl->vStuckPos.x - pCtrl->vSelfXZ.x;
        if (flDx * flDx + flDy * flDy < g_fl2250000) {
            pCtrl->nStuckTicks += g_nObjUpdateTime;    /* @0x4580d0 @0x401e04 */
        } else {
            pCtrl->vStuckPos = pCtrl->vSelfXZ;         /* @0x401def */
            pCtrl->nStuckTicks = 0;
        }
        if (pCtrl->nStuckTicks > 0x36b) {              /* 875 ticks @0x401e11 */
            pCtrl->nStuckTicks = 0;
            pCtrl->bFlag51 = 1;
            pCtrl->bFlag50 = 0;
        }
    }
}

/* aiPathfindToTarget @0x401800 — AI waypoint movement toward pToPos
 * ({z, x, height}): nearest NavPoint in the level Y band (±10000 on level
 * 4, else -400/+1000), steer to it; on arrival (1210000) return 2 (or 1
 * when already at pToPos); otherwise walk the buoy link graph via
 * navPointPickCheapestLink, extrapolating the current waypoint by up to
 * 800 units toward the next link. Returns 0 = steering, 1 = at pToPos,
 * 2 = waypoint reached. */
int aiPathfindToTarget(AiController *pCtrl, float *pFromPos, float *pToPos) /* @0x401800 */
{
    GxVec2 vWpt;
    GxVec2 vNext;
    GxVec2 vDelta;
    GxVec2 vPolar;
    GxVec2 vStepPolar;
    GxVec2 vStep;
    NavPoint *pTarget;
    NavPoint *pNext;
    float flDist;
    int nYMin;
    int nYMax;

    if (g_nLevelIdx == 4) {                            /* @0x401823 */
        nYMax = (int)pToPos[2] + 10000;                /* +0x2710 */
        nYMin = (int)pToPos[2] - 10000;
    } else {
        nYMax = (int)pToPos[2] + 1000;                 /* +0x3e8 */
        nYMin = (int)pToPos[2] - 400;                  /* 0xfffffe70 */
    }
    pTarget = navPointFindNearestInYRange((int)pToPos[1], (int)pToPos[0],
                                          nYMin, nYMax); /* @0x4259d0 @0x401860 */

    if (pCtrl->pNavCurrent == pTarget) {               /* @0x40186d */
        if (pCtrl->bFlag50 == 0) {                     /* @0x401875 */
            vWpt.x = (float)((NavPoint *)pTarget)->nPosZ;   /* +0x54 @0x4018cf */
            vWpt.y = (float)((NavPoint *)pTarget)->nPosX;   /* +0x4c @0x4018da */
            {
                float flDx = pFromPos[1] - vWpt.y;
                float flDz = pFromPos[0] - vWpt.x;
                if (flDz * flDz + flDx * flDx < g_fl1210000) {
                    pCtrl->bFlag50 = 1;                /* @0x401909 */
                    return 2;                          /* waypoint reached */
                }
            }
            aiSteerToTarget(pCtrl, pFromPos, &vWpt.x); /* @0x401923 */
            return 0;
        }
        {                                              /* bFlag50 set @0x40187c */
            float flDx = pFromPos[1] - pToPos[1];
            float flDz = pFromPos[0] - pToPos[0];
            int nArrived = (flDz * flDz + flDx * flDx < g_fl1210000);
            aiSteerToTarget(pCtrl, pFromPos, pToPos);  /* @0x4018a9 */
            return nArrived ? 1 : 2;                   /* @0x4018b0/0x4018bc */
        }
    }
    pCtrl->bFlag50 = 0;                                /* @0x401936 */
    if (pTarget != pCtrl->pNavTarget || pCtrl->bFlag51 != 0 ||
        pCtrl->pNavCurrent == NULL) {                  /* @0x40193a..0x40194f */
        pCtrl->bFlag51 = 0;                            /* @0x401ab3 */
        pCtrl->pNavTarget = pTarget;                   /* @0x401ab7 */
        pCtrl->pNavCurrent = pCtrl->pNavPoint;         /* +0x08 @0x401aba */
        g_nReturnToMenu = 1;                           /* @0x45810c @0x401abf */
        return 0;
    }
    vWpt.x = (float)((NavPoint *)pCtrl->pNavCurrent)->nPosZ;   /* @0x401955 */
    vWpt.y = (float)((NavPoint *)pCtrl->pNavCurrent)->nPosX;
    {
        float flDx = pFromPos[1] - vWpt.y;
        float flDz = pFromPos[0] - vWpt.x;
        flDist = sqrtf(flDz * flDz + flDx * flDx);     /* @0x40197e */
    }
    if (flDist < g_fl800) {                            /* @0x401988 */
        pNext = navPointPickCheapestLink((NavPoint *)pCtrl->pNavCurrent,
                                         pTarget);      /* @0x425bd0 @0x40199f */
        if (pNext != NULL) {                           /* @0x4019a7 */
            vNext.x = (float)pNext->nPosZ;             /* @0x4019af */
            vNext.y = (float)pNext->nPosX;
            gxVec2Sub(&vDelta, &vNext, &vWpt);         /* @0x435020 @0x4019cc */
            mathVec2Polar(&vPolar, &vDelta);           /* @0x435060 @0x4019d9 */
            vStepPolar.x = g_fl800;                    /* @0x4019f7 */
            vStepPolar.y = vPolar.y;
            gxVec2FromPolar(&vStep, &vStepPolar);      /* @0x434fc0 @0x4019ff */
            vWpt.x += (g_fl800 - flDist) * vStep.x * g_fl0_00125;  /* @0x401a04 */
            vWpt.y += (g_fl800 - flDist) * vStep.y * g_fl0_00125;  /* @0x401a2a */
        }
    }
    if (flDist >= g_fl700) {                           /* @0x401a44 */
        float flDx = pFromPos[1] - vWpt.y;
        float flDz = pFromPos[0] - vWpt.x;
        if (flDz * flDz + flDx * flDx >= g_fl490000) {
            aiSteerToTarget(pCtrl, pFromPos, &vWpt.x); /* @0x401a85 */
            return 0;
        }
    }
    pCtrl->pNavCurrent = navPointPickCheapestLink(     /* @0x425bd0 @0x401a9a */
        (NavPoint *)pCtrl->pNavCurrent, pTarget);
    return 0;
}

/* aiStateSetTargetItem @0x4015c0 — AI state 0: pick the goal. Mode 3 with
 * the list full (+0x180 == 5) returns 0; with no spawned item
 * (g_nCurrentItemId == 0) it targets a random NavPoint (navPointPickRandom)
 * and returns 1. Otherwise it scans the shopping list (+0x184..+0x1a8,
 * uncollected and positive ids), skipping objects with +0x14 == 1, and
 * keeps the nearest by |dx|/|dy|/dist over all objGetPos occurrences
 * (nTargetItemId +0x40 / nTargetOccurrence +0x3c). Returns 0 with no
 * candidate, else stores the nearest NavPoint to the goal ({z, x} into
 * +0x2c/+0x30, nPosY into +0x34, bFlag51 = 1) and returns 1. */
int aiStateSetTargetItem(AiController *pCtrl) /* @0x4015c0 */
{
    PlayerRecord *pRec = pCtrl->pPlayerObj;
    float flBest = g_flFltMax;                         /* @0x4015e7 */
    int nBestId = 0;                                   /* EBX @0x4015d9 */
    int nBestOcc = 0;                                  /* [ESP+0x10] @0x4015e3 */
    GxVec2 vGoal;                                      /* {z, x} of the best occurrence */
    int nGoalHeight = 0;
    int nYMin;
    int nYMax;
    int nSlot;
    int nId;
    int nOcc;
    EventObject *pObj;
    NavPoint *pNav;

    if (g_nGameMode == 3) {                            /* @0x4015f4 */
        if (pRec->anHeldSlot[1] == 5) {                /* +0x180 @0x4015ff */
            return 0;
        }
        if (g_nCurrentItemId == 0) {                   /* @0x401610 */
            pNav = navPointPickRandom();               /* @0x425aa0 @0x401618 */
            pCtrl->vTargetXZ.x = (float)pNav->nPosZ;   /* +0x2c @0x401620 */
            pCtrl->vTargetXZ.y = (float)pNav->nPosX;   /* +0x30 @0x401626 */
            pCtrl->nTargetHeight = pNav->nPosY;        /* +0x34 @0x401630 */
            pCtrl->bFlag51 = 1;                        /* @0x40162c */
            return 1;
        }
    }
    for (nSlot = 0; nSlot < 10; nSlot++) {             /* EBP 0x184..0x1ac @0x40163d */
        nId = pRec->anListIds[nSlot];                  /* +0x184 @0x401650 */
        if (pRec->abListTaken[nSlot] != 0 || nId <= 0) { /* +0x1ac @0x401644 */
            continue;
        }
        nOcc = 0;
        for (pObj = objGetPos(nId, 0, &vGoal.x, &nGoalHeight); pObj != NULL;
             pObj = objGetPos(nId, ++nOcc, &vGoal.x, &nGoalHeight)) { /* @0x40f1b0 @0x401669 */
            float flDx;
            float flDz;
            float flDist;
            if (pObj->nValue1 == 1) {                 /* +0x14 @0x401679 */
                continue;                              /* @0x401746 */
            }
            flDx = vGoal.y - pCtrl->vSelfXZ.y;         /* @0x401683 gxVec2Sub */
            flDz = vGoal.x - pCtrl->vSelfXZ.x;
            if (fabsf(flDz) >= flBest || fabsf(flDx) >= flBest) { /* @0x40169f/0x4016c8 */
                continue;
            }
            flDist = sqrtf(flDz * flDz + flDx * flDx); /* @0x4016fc */
            if (flDist >= flBest) {                    /* @0x4016fe */
                continue;
            }
            flBest = flDist;                           /* @0x40170f */
            nBestId = nId;                             /* @0x401713 */
            nBestOcc = nOcc;                           /* [ESP+0x28] @0x40171e */
            objGetPos(nBestId, nBestOcc, &vGoal.x, &nGoalHeight); /* @0x401722 */
        }
    }
    if (flBest == g_flFltMax) {                        /* @0x40177b */
        return 0;
    }
    pCtrl->nTargetItemId = nBestId;                    /* +0x40 @0x401796 */
    pCtrl->nTargetOccurrence = nBestOcc;               /* +0x3c @0x401799 */
    if (g_nLevelIdx == 4) {                            /* @0x40179c */
        nYMax = nGoalHeight + 10000;
        nYMin = nGoalHeight - 10000;
    } else {
        nYMax = nGoalHeight + 1000;
        nYMin = nGoalHeight - 400;
    }
    pNav = navPointFindNearestInYRange((int)vGoal.y, (int)vGoal.x,
                                       nYMin, nYMax);  /* @0x4259d0 @0x4017d9 */
    pCtrl->vTargetXZ.x = (float)pNav->nPosZ;           /* +0x2c @0x4017e4 */
    pCtrl->vTargetXZ.y = (float)pNav->nPosX;           /* +0x30 @0x4017ea */
    pCtrl->nTargetHeight = pNav->nPosY;                /* +0x34 @0x4017f4 */
    pCtrl->bFlag51 = 1;                                /* @0x4017f0 */
    return 1;
}

/* aiStateCartAction @0x401e40 — AI state 2 ("cart action"): drop any held
 * item ("action drop item"); else, when not already grabbing (+0x2e8 != 3)
 * and the cart is close (playerFindCart @0x40e180), dispatch "action grab
 * cart" and raise bFlag51; otherwise pathfind from vSelfXZ to vWalkXZ. */
int aiStateCartAction(AiController *pCtrl) /* @0x401e40 */
{
    PlayerRecord *pRec = pCtrl->pPlayerObj;

    if (pRec->anHeldSlot[0] != 0) {                    /* +0x17c @0x401e4d */
        commandDispatch((intptr_t)pRec, SZ_ACTION_DROP_ITEM); /* @0x408b60 @0x401e8f */
        return 0;                                      /* dispatch result, AL=0 */
    }
    if (pRec->nActionSubstate != 3 &&                  /* +0x2e8 @0x401e5b */
        playerFindCart(pRec) != 0) {                   /* @0x40e180 @0x401e65 */
        commandDispatch((intptr_t)pRec, SZ_ACTION_GRAB_CART); /* @0x401e79 */
        pCtrl->bFlag51 = 1;                            /* @0x401e81 */
        return 0;
    }
    aiPathfindToTarget(pCtrl, &pCtrl->vSelfXZ.x, &pCtrl->vWalkXZ.x); /* @0x401ea5 */
    return 0;
}

/* aiStateGrabObject @0x401eb0 — AI state 3: mode 3 re-targets anListIds[0]
 * while an item is spawned (+0x40). Fetches the goal item's position via
 * objGetPos(nTargetItemId, nTargetOccurrence). When a held slot (+0x17c)
 * already holds the target id it returns 1 (-> state 4); holding something
 * else dispatches "action drop item" (mode 3 returns 1 instead). When
 * blocked (playerCheckBlocked @0x40ec40) it dispatches "action get item"
 * into the free held slot or drops, else pathfinds from vSelfXZ to the
 * item position. Returns 1 only in the already-held / mode-3-held cases. */
int aiStateGrabObject(AiController *pCtrl) /* @0x401eb0 */
{
    PlayerRecord *pRec = pCtrl->pPlayerObj;
    GxVec2 vItem;
    int nItemHeight;
    int nHeld;
    EventObject *pObj;

    if (g_nGameMode == 3 && g_nCurrentItemId != 0) {   /* @0x401ec0 */
        pCtrl->nTargetItemId = pRec->anListIds[0];     /* +0x184 @0x401eda */
    }
    pObj = objGetPos(pCtrl->nTargetItemId, pCtrl->nTargetOccurrence,
                     &vItem.x, &nItemHeight);          /* @0x40f1b0 @0x401eef */
    (void)pObj;

    nHeld = pRec->anHeldSlot[0];                       /* +0x17c @0x401f01 */
    if (nHeld == pCtrl->nTargetItemId) {               /* @0x401f04 */
        return 1;                                      /* already holding it */
    }
    if (nHeld != 0) {                                  /* @0x401f08 */
        if (g_nGameMode == 3) {                        /* @0x401f0c */
            return 1;                                  /* @0x401f6f */
        }
        commandDispatch((intptr_t)pRec, SZ_ACTION_DROP_ITEM); /* @0x401f1f */
    }
    if (playerCheckBlocked(pRec) != 0) {               /* @0x40ec40 @0x401f35 */
        if (pRec->anHeldSlot[0] == 0) {                /* @0x401f4b */
            commandDispatch((intptr_t)pRec, SZ_ACTION_GET_ITEM); /* @0x401f7d */
        } else {
            commandDispatch((intptr_t)pRec, SZ_ACTION_DROP_ITEM); /* @0x401f5f */
        }
        return 0;
    }
    aiPathfindToTarget(pCtrl, &pCtrl->vSelfXZ.x, &vItem.x); /* @0x401f98 */
    return 0;
}

/* aiStatePutObjectInCart @0x401fb0 — AI state 4: with no held item return 1
 * (item delivered, -> state 6); when vSelfXZ is farther than 1100 (1210000)
 * from vWalkXZ pathfind there; else dispatch "action drop item" (drop into
 * the cart) — returns 1 only in the empty-handed case. */
int aiStatePutObjectInCart(AiController *pCtrl) /* @0x401fb0 */
{
    PlayerRecord *pRec = pCtrl->pPlayerObj;
    float flDx;
    float flDz;

    if (pRec->anHeldSlot[0] == 0) {                    /* +0x17c @0x401fbf */
        return 1;                                      /* @0x401fd2 */
    }
    flDx = pCtrl->vSelfXZ.y - pCtrl->vWalkXZ.y;        /* @0x401fd8 */
    flDz = pCtrl->vSelfXZ.x - pCtrl->vWalkXZ.x;
    if (flDz * flDz + flDx * flDx >= g_fl1210000) {    /* @0x401ff3 */
        aiPathfindToTarget(pCtrl, &pCtrl->vSelfXZ.x, &pCtrl->vWalkXZ.x); /* @0x402048 */
        return 0;
    }
    commandDispatch((intptr_t)pRec, SZ_ACTION_DROP_ITEM); /* @0x402038 */
    return 0;
}

/* aiStateReturnHome @0x402060 — AI state 7: pathfind from vWalkXZ to the
 * checkout ("goal" object via objGetCheckoutPos @0x40f6e0); the goal height
 * band base is 8000 on level 3 and 2000 on level 4, 0 elsewhere (passed as
 * the raw int third element of the target {z, x, height}). Returns
 * aiPathfindToTarget's low byte (1 = arrived). */
int aiStateReturnHome(AiController *pCtrl) /* @0x402060 */
{
    struct {                                           /* {z, x, height} layout */
        GxVec2 vXZ;
        int nHeight;
    } target;
    int nRet;

    objGetCheckoutPos(&target.vXZ.x);                  /* @0x40f6e0 @0x402074 */
    target.nHeight = 0;                                /* @0x402084 */
    if (g_nLevelIdx == 3) {                            /* @0x402079 */
        target.nHeight = 8000;                         /* 0x1f40 */
    } else if (g_nLevelIdx == 4) {                     /* @0x402098 */
        target.nHeight = 2000;                         /* 0x7d0 */
    }
    nRet = aiPathfindToTarget(pCtrl, &pCtrl->vWalkXZ.x, &target.vXZ.x); /* @0x4020b0 */
    return nRet;
}

/* playerAiUpdate @0x401160 — the AI decision state machine for one player
 * (called for two players per tick from playerUpdateDispatch @0x4010e0).
 * Refreshes the self position/height, the nearest NavPoint (stored at +0x08
 * and published to g_pNavPointSel @0x45e480) and the walk position, then
 * runs the per-mode state machine:
 *  - mode 4 (Vagnrace): state 0 picks anListIds[0] via objGetPos, state 1
 *    steers (aiSteerToTarget until 1210000, then aiPathfindToTarget with
 *    the bFlag52 latch), state 7 returns home; states route cart handling
 *    through aiStateCartAction.
 *  - modes 1..3: 0 SETTARGETITEM -> 1 GOTOITEM (aiPathfindToTarget until
 *    arrival, then 5) -> 5 RELEASECART ("action release cart" / back to 3)
 *    -> 3 GRABOBJECT (aiStateGrabObject -> 4) -> 4 PUTOBJECTINCART
 *    (aiStatePutObjectInCart -> 6) -> 6 (cart gate -> 0 / aiStateCartAction)
 *    -> 7 RETURNHOME (aiStateReturnHome -> 9). Mode 3 waits for the spawn
 *    via state 2 (random NavPoint) whenever g_nCurrentItemId is 0.
 * Every state change logs through the 0x401590 stub (no-op in release).
 * The tail copies the record's input channels (+0x2e0/+0x2e4) into the
 * controller's anim-sync slots (+0x48/+0x4c). Returns 1. */
int playerAiUpdate(AiController *pCtrl) /* @0x401160 */
{
    if (DISABLE_AI) {
        return 1;
    }
    PlayerRecord *pRec;
    GxVec2 vSelfXZ;
    GxVec2 vWalkXZ;
    int nYMin;
    int nYMax;
    int nSelfHeight;
    int nRet;

    pRec = pCtrl->pPlayerObj;                          /* @0x401171 */
    sceneObjGetPosXZ(pRec, &vSelfXZ.x);                /* @0x4099d0 @0x401178 */
    pCtrl->vSelfXZ = vSelfXZ;                          /* +0x14 @0x40117f */
    nSelfHeight = sceneObjGetHeightChar(pRec);         /* @0x409a90 @0x40118a */
    pCtrl->nSelfHeight = nSelfHeight;                  /* +0x1c @0x40118f */
    if (g_nLevelIdx == 4) {                            /* @0x401192 */
        nYMax = nSelfHeight + 10000;
        nYMin = nSelfHeight - 10000;
    } else {
        nYMax = nSelfHeight + 1000;
        nYMin = nSelfHeight - 400;
    }
    pCtrl->pNavPoint = navPointFindNearestInYRange((int)vSelfXZ.y, (int)vSelfXZ.x,
                                                   nYMin, nYMax); /* @0x4259d0 @0x4011ca */
    playerGetPos(pRec, &vWalkXZ.x);                    /* @0x409a10 @0x4011df */
    pCtrl->vWalkXZ = vWalkXZ;                          /* +0x20 @0x4011e6 */
    pCtrl->nPosNodeHeight = sceneObjGetHeightCart(pRec); /* @0x409ad0 @0x4011f5 */
    g_pNavPointSel = (NavPoint *)pCtrl->pNavPoint;     /* @0x45e480 @0x4011fb */

    if (g_nGameMode == 4) {                            /* @0x401200 */
        switch (pCtrl->nAiState) {                     /* @0x40120e */
        case 0:                                        /* @0x4012f6 */
            pCtrl->nTargetItemId = pRec->anListIds[0]; /* +0x184 @0x401302 */
            if (objGetPos(pCtrl->nTargetItemId, 0, &pCtrl->vTargetXZ.x,
                          &pCtrl->nTargetHeight) != NULL) { /* @0x40f1b0 @0x40130c */
                pCtrl->nAiState = 1;                   /* @0x401318 */
                nopDebugStub();                        /* log "AIMODE_SETTARGETITEM->
                                                          AIMODE_GOTOITEM" @0x44e180 */
            } else {
                pCtrl->nAiState = 7;                   /* @0x40137f */
                nopDebugStub();                        /* "AIMODE_SETTARGETITEM->
                                                          AIMODE_GOAL" @0x44e130 */
            }
            break;
        case 1:                                        /* @0x40124b */
            if (pRec->nCartMode != 1) {                /* @0x40124d */
                aiStateCartAction(pCtrl);              /* @0x401e40 @0x4012dd */
                if (pCtrl->bFlag51 != 0) {             /* @0x4012e2 */
                    pCtrl->bFlag52 = 1;                /* @0x4012ed */
                }
                break;
            }
            if (pCtrl->bFlag52 == 0) {                 /* @0x40125a */
                aiSteerToTarget(pCtrl, &pCtrl->vWalkXZ.x, &pCtrl->vTargetXZ.x); /* @0x4012a0 */
                if (pCtrl->bFlag51 != 0) {             /* @0x4012a5 */
                    pCtrl->bFlag52 = 1;                /* @0x4012ac */
                }
                {
                    float flDx = vWalkXZ.y - pCtrl->vTargetXZ.y;
                    float flDz = vWalkXZ.x - pCtrl->vTargetXZ.x;
                    if (flDz * flDz + flDx * flDx < g_fl1210000) { /* @0x4012c5 */
                        pCtrl->nAiState = 0;           /* @0x401288 */
                        nopDebugStub();                /* "AIMODE_GOTOITEM->
                                                          AIMODE_RELEASECART" @0x44e1a8 */
                    }
                }
                break;
            }
            nRet = aiPathfindToTarget(pCtrl, &pCtrl->vWalkXZ.x,
                                      &pCtrl->vTargetXZ.x); /* @0x401268 */
            if (nRet == 2 || nRet == 1) {              /* @0x40126d */
                pCtrl->bFlag52 = 0;                    /* @0x40127e */
                if (nRet == 1) {                       /* @0x40127b */
                    pCtrl->nAiState = 0;               /* @0x401288 */
                    nopDebugStub();                    /* "AIMODE_GOTOITEM->
                                                          AIMODE_RELEASECART" @0x44e1a8 */
                }
            }
            break;
        case 7:                                        /* @0x401226 */
            if (pRec->nCartMode == 1) {                /* @0x401230 */
                if (aiStateReturnHome(pCtrl) == 1) {   /* @0x402060 @0x401239 */
                    pCtrl->nAiState = 9;               /* @0x40152f */
                    nopDebugStub();                    /* "AIMODE_GOAL->AIMODE_END"
                                                          @0x44e060 */
                }
            } else {
                aiStateCartAction(pCtrl);              /* @0x401502 */
            }
            break;
        default:                                       /* @0x401545 */
            break;
        }
    } else {
        switch (pCtrl->nAiState) {                     /* jump table @0x401564 */
        case 0:                                        /* @0x40133c */
            if (aiStateSetTargetItem(pCtrl) == 1) {    /* @0x4015c0 @0x40133e */
                if (g_nGameMode == 3) {                /* @0x40134c */
                    if (g_nCurrentItemId != 0) {       /* @0x401351 */
                        pCtrl->nAiState = 1;           /* @0x401318 */
                        nopDebugStub();                /* "AIMODE_SETTARGETITEM->
                                                          AIMODE_GOTOITEM" @0x44e180 */
                    } else {
                        pCtrl->nAiState = 2;           /* @0x40135a */
                        nopDebugStub();                /* "AIMODE_SETTARGETITEM->
                                                          AIMODE_GOTORANDITEM"
                                                          @0x44e154 */
                    }
                } else {
                    pCtrl->nAiState = 1;               /* @0x401318 */
                    nopDebugStub();                    /* @0x44e180 */
                }
            } else {
                if (g_nGameMode == 3) {                /* @0x40136b */
                    if (pRec->anHeldSlot[1] != 5) {    /* +0x180 @0x401372 */
                        break;                         /* wait @0x401545 */
                    }
                }
                pCtrl->nAiState = 7;                   /* @0x40137f */
                nopDebugStub();                        /* "AIMODE_SETTARGETITEM->
                                                          AIMODE_GOAL" @0x44e130 */
            }
            break;
        case 1:                                        /* @0x401390 */
            if (g_nGameMode == 3 && pRec->anHeldSlot[1] == 5) { /* @0x40139a */
                pCtrl->nAiState = 0;                   /* @0x4013a4 */
                break;
            }
            if (pRec->nCartMode != 1) {                /* @0x4013b2 */
                aiStateCartAction(pCtrl);              /* @0x401500 */
                break;
            }
            nRet = aiPathfindToTarget(pCtrl, &pCtrl->vWalkXZ.x,
                                      &pCtrl->vTargetXZ.x); /* @0x4013c6 */
            if (nRet == 2 || nRet == 1) {              /* @0x4013cb */
                pCtrl->nAiState = 5;                   /* @0x4013d9 */
                nopDebugStub();                        /* "AIMODE_GOTOITEM->
                                                          AIMODE_RELEASECART"
                                                          @0x44e1a8 */
            }
            break;
        case 2:                                        /* @0x4013e6 */
            if (g_nGameMode == 3 && pRec->anHeldSlot[1] == 5) { /* @0x4013eb */
                pCtrl->nAiState = 0;                   /* @0x4013f6 */
                break;
            }
            if (g_nCurrentItemId != 0) {               /* @0x401402 */
                pCtrl->nAiState = 0;                   /* @0x40140b */
                break;
            }
            if (pRec->nCartMode == 1) {                /* @0x401419 */
                aiPathfindToTarget(pCtrl, &pCtrl->vWalkXZ.x,
                                   &pCtrl->vTargetXZ.x); /* @0x40142d */
                break;
            }
            aiStateCartAction(pCtrl);                  /* @0x401500 */
            break;
        case 3:                                        /* @0x401464 */
            if (g_nGameMode == 3 && pRec->anHeldSlot[1] == 5) { /* @0x401469 */
                pCtrl->nAiState = 0;                   /* @0x401474 */
                break;
            }
            if (aiStateGrabObject(pCtrl) == 1) {       /* @0x401eb0 @0x401482 */
                pCtrl->nAiState = 4;                   /* @0x40148b */
                nopDebugStub();                        /* "AIMODE_GRABOBJECT->
                                                          AIMODE_PUTOBJECTINCART"
                                                          @0x44e0c8 */
            } else if (g_nGameMode == 3 && g_nCurrentItemId == 0) { /* @0x40149c */
                pCtrl->nAiState = 0;                   /* @0x4014b6 */
            }
            break;
        case 4:                                        /* @0x4014c2 */
            if (g_nGameMode == 3 && pRec->anHeldSlot[1] == 5) { /* @0x4014c7 */
                pCtrl->nAiState = 0;                   /* @0x4014d2 */
                break;
            }
            if (aiStatePutObjectInCart(pCtrl) == 1) {  /* @0x401fb0 @0x4014dd */
                pCtrl->nAiState = 6;                   /* @0x4014e6 */
                nopDebugStub();                        /* "AIMODE_PUTOBJECTINCART->
                                                          AIMODE_GRABCART"
                                                          @0x44e0a0 */
            }
            break;
        case 5:                                        /* @0x401437 */
            if (pRec->nCartMode != 0) {                /* @0x401439 */
                commandDispatch((intptr_t)pRec, SZ_ACTION_RELEASE_CART); /* @0x401449 */
                break;
            }
            pCtrl->nAiState = 3;                       /* @0x401453 */
            nopDebugStub();                            /* "AIMODE_RELEASECART->
                                                          AIMODE_GRABOBJECT"
                                                          @0x44e0f4 */
            break;
        case 6:                                        /* @0x4014f4 */
            if (pRec->nCartMode != 0) {                /* @0x4014f6 */
                pCtrl->nAiState = 0;                   /* @0x401509 */
                nopDebugStub();                        /* "AIMODE_GRABCART->
                                                          AIMODE_SETTARGETITEM"
                                                          @0x44e078 */
            } else {
                aiStateCartAction(pCtrl);              /* @0x401502 */
            }
            break;
        case 7:                                        /* @0x401517 */
            if (pRec->nCartMode != 1) {                /* @0x40151f */
                aiStateCartAction(pCtrl);              /* @0x401502 */
                break;
            }
            if (aiStateReturnHome(pCtrl) == 1) {       /* @0x402060 @0x401526 */
                pCtrl->nAiState = 9;                   /* @0x40152f */
                nopDebugStub();                        /* "AIMODE_GOAL->AIMODE_END"
                                                          @0x44e060 */
            }
            break;
        default:                                       /* @0x401545 */
            break;
        }
    }

    memcpy(&pCtrl->nSavedAnimationFrame, &pRec->flInputTurn, 4);  /* +0x2e0 -> +0x48 @0x401548 */
    memcpy(&pCtrl->nSavedAnimationTimer, &pRec->flInputAccel, 4); /* +0x2e4 -> +0x4c @0x401551 */
    return 1;
}
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

    if (objDistTo(pRec->pSubObjA, pRec->pSubObjB) > (float)g_dbl3000) {
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
 * already riding (nCartMode) or when close enough to steer (cart node within
 * 1000 and facing within the ±0.1 band); *pAnimState gets 0x50 (turn),
 * 0x5a (drive), 0x6e (grab) or 100 (steer step). */
int aiStateCartApproach(PlayerRecord *pRec, int *pAnimState) /* @0x40e1d0 */
{
    float flAngle;
    float flDiff;
    float flDiffCart;
    if (pRec->nCartMode != 0) return 1;                 /* @0x40e1d5 */
    if (playerFindCart(pRec) == 0) return 0;            /* @0x40e1e6 */

    pRec->flInputTurn = 0.0f;                           /* +0x2e0 @0x40e204 */
    pRec->flInputAccel = 0.0f;                          /* +0x2e4 @0x40e20e */
    flAngle = objAngleTo(pRec->pSubObjA, pRec->pSubObjB); /* @0x40e218 */
    flDiff = aiTurnDiff(pRec->pSubObjA, flAngle);       /* @0x40e218..0x40e293 */
    if (flDiff > (float)g_dbl0_1 || flDiff < (float)g_dblNeg0_1) {
        if (flDiff >= g_flZero) {                       /* @0x40e581 */
            pRec->flInputTurn = g_flOne;                /* +0x2e0 @0x40e592 */
        } else {
            pRec->flInputTurn = g_flNegOne;             /* +0x2e0 @0x40e586 */
        }
        if (pAnimState != NULL) *pAnimState = 0x50;     /* 80 @0x40e5a4 */
        return 0;
    }
    if (objDistTo(pRec->pSubObjA, pRec->pSubObjB) > (float)g_dbl1000) {
        pRec->flInputAccel = g_flOne;                   /* +0x2e4 @0x40e3c6 */
        if (pAnimState != NULL) *pAnimState = 0x5a;     /* 90 @0x40e3a7 */
        return 0;
    }
    flDiffCart = aiTurnDiff(pRec->pSubObjB, flAngle);   /* @0x40e3b8..0x40e50b */
    if (fabs(flDiffCart) <= fabs(pRec->flPosTurnAccum)) { /* FABS/FCOMPP @0x40e50e */
        if (pAnimState != NULL) *pAnimState = 0x6e;     /* 110 @0x40e56c */
        return 1;
    }
    if (flDiffCart >= g_flZero) {                       /* @0x40e529 */
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

    pRec->nCartMode = 1;                                        /* @0x40e5b4 */
    ((WorldNode *)pRec->pSubObjC)->field_1c = 1;                       /* +0x1c @0x40e5ba */
    pRec->flCartTurnAccum = pRec->flTurnAccum;          /* +0x284 = +0x204 @0x40e5c0 */
    pRec->vCartVelPolar.y = pRec->vVelPolar.y;          /* +0x2a0 = +0x220 @0x40e5cd */
    pRec->flCartCurSpeed = pRec->flCurSpeed;            /* +0x274 = +0x1f4 @0x40e5d8 */
    nX = objPolarPosLookup(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode);   /* @0x40e5e8 */
    nZ = objPolarPosLookup2(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode);  /* @0x40e5fb */
    nScale = (short)objListFindFloat(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode); /* @0x40e5f6 */
    flAvgFront = nodeChannelAvgFloat(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode); /* @0x40e60c */
    memcpy(&nSpeedBits, &pRec->flCurSpeed, 4);          /* +0x1f4 raw bits @0x40e5d8 */
    nodeSetTransformFromChannels(pRec->pSubObjC, nX, (int)flAvgFront, nZ, nSpeedBits, nScale); /* @0x40e606 */
    ((WorldNode *)pRec->pSubObjA)->field_1c = 0;                       /* @0x40e661 */
    ((WorldNode *)pRec->pSubObjB)->field_1c = 0;                       /* @0x40e669 */
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

    if (pRec->nCartMode != 0) return 1;
    flDiff = aiTurnDiff(pRec->pSubObjA, objAngleTo(pRec->pSubObjA, pRec->pSubObjB)); /* @0x40e6c6 */
    nWalkX = objPolarPosLookup(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode);   /* @0x40e6f2 */
    nWalkZ = objPolarPosLookup2(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode);  /* @0x40e709 */
    nCartX = objPolarPosLookup(pRec->pSubObjB, (intptr_t)pRec->pCartSceneObj);    /* @0x40e72d */
    nCartZ = objPolarPosLookup2(pRec->pSubObjB, (intptr_t)pRec->pCartSceneObj);   /* @0x40e72d */
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
    if (pRec->anHeldSlot[0] != 0 || pRec->nCartMode != 0) {
        return NULL;                                    /* @0x40ec51..0x40ec67 */
    }
    pTarget = playerFindNearestTarget(pRec);            /* @0x40ec6e */
    if (pTarget != NULL) {
        return pTarget;                                 /* @0x40ec78 */
    }
    nWalkX = objPolarPosLookup(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode);   /* @0x40ec88 */
    nWalkZ = objPolarPosLookup2(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode);  /* @0x40ec95 */
    for (nIdx = 0; (pObj = objFindByIdInRange(1, 0x1e, nIdx)) != NULL; nIdx++) { /* @0x40ecae */
        flDz = (float)nWalkZ - pObj->flPosX;         /* +0x38 @0x40ecca */
        flDy = (float)nWalkX - pObj->flPosZ;         /* +0x3c @0x40ecd1 */
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
    int nList;
    float flAvgFront;
    float flBest = 1000000.0f;                  /* 0x49742400 @0x40ed2c */
    float flD2;
    float flDz;
    float flDy;
    int nItemId;
    GxVec2 vScratch;

    gxVec2SetAngleZero(&vScratch);                  /* @0x434f90 @0x40ed1b */
    if (pRec->anHeldSlot[0] != 0 || pRec->nCartMode != 0) {
        return NULL;                                /* @0x40ed34..0x40ed4a */
    }
    flAvgFront = nodeChannelAvgFloat(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode); /* @0x40ed5a */
    nWalkX = objPolarPosLookup(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode);   /* @0x40ed6d */
    nWalkZ = objPolarPosLookup2(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode);  /* @0x40ed80 */
    for (nSlot = 0; ; nSlot++) {                    /* @0x40ed93 */
        pObj = objFindByIdInRange(1, 0x1f, nSlot);
        if (pObj == NULL) {
            return pBest;                           /* @0x40ed9f */
        }
        nItemId = pObj->nId;                        /* +0x08 @0x40edae */
        if (pRec->nControlType != 2 && nItemId == 0x1f) {    /* burger @0x40edae */
            if (flAvgFront > pObj->flHeight - g_fl1500 &&   /* @0x40edb4: NOT(flAvgFront <= h-1500) */
                flAvgFront < pObj->flHeight + g_fl1500) {   /* @0x40edcb */
                flDz = (float)nWalkZ - pObj->flPosX;   /* +0x38 @0x40ede0 */
                flDy = (float)nWalkX - pObj->flPosZ;   /* +0x3c @0x40ede7 */
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
        if (pObj->flHeight - g_fl500 >= flAvgFront ||   /* @0x40ee20 */
            flAvgFront >= pObj->flHeight + g_fl500) {   /* @0x40ee3b */
            continue;                               /* @0x40ef2d */
        }
        flDz = (float)nWalkZ - pObj->flPosX;     /* +0x38 @0x40ee50 */
        flDy = (float)nWalkX - pObj->flPosZ;     /* +0x3c @0x40ee57 */
        flD2 = flDz * flDz + flDy * flDy;
        if (flD2 > flBest) {                        /* @0x40ee74 */
            continue;
        }
        if (g_nGameMode == 1) {                     /* @0x40ee83 */
            for (nList = 0; nList < 10; nList++) {  /* @0x40eef3 */
                if (pRec->abListTaken[nList] == 0 &&     /* +0x1ac @0x40eefe */
                    pRec->anListIds[nList] == nItemId &&
                    pRec->nQuestTargetId != pRec->anListIds[nList] &&
                    pRec->nLastThrownItemId != pRec->anListIds[nList]) {
                    flBest = flD2;
                    pBest = pObj;
                }
            }
        } else if (g_nGameMode == 2) {              /* @0x40eebf */
            for (nList = 0; nList < 10; nList++) {  /* @0x40eeca */
                if (pRec->abListTaken[nList] == 0 &&     /* +0x1ac */
                    pRec->anListIds[nList] == nItemId &&
                    pRec->nLastThrownItemId != pRec->anListIds[nList]) {
                    flBest = flD2;
                    pBest = pObj;
                }
            }
        } else if (g_nGameMode == 3) {              /* @0x40ee8e */
            if (pRec->anHeldSlot[1] < 5 &&          /* +0x180 @0x40ee95 */
                (g_nCurrentItemId == nItemId || pObj->nValue1 != 0)) {  /* @0x40eea2 */
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
 * held = id, mesh = g_apLevelItemSlots[id-1].nMeshId — the SceneObjTypeDef*
 * from levelSetup's items[%d]/mesh lookup (original read [id*0x2c+0x4583b8]
 * == slot[id-1].nMeshId @0x4583e4+(id-1)*0x2c, ids <= 0x1e); mode 3 resets
 * g_nCurrentItemId (+0x10 log call) and pings peers (server 0x20 / client
 * 0x3e). */
int playerAiGrabItem(PlayerRecord *pRec) /* @0x40ea20 */
{
    EventObject *pObj;
    int nItemId;
    SceneNode *pBurgerObj;

    pObj = playerCheckBlocked(pRec);                    /* @0x40ea28 */
    if (pObj == NULL) {
        return 0;                                       /* @0x40ea36 */
    }
    nItemId = pObj->nId;
    if (nItemId == 0x1f) {                              /* @0x40ea86 */
        if (pRec != &g_playerRecords[g_nLocalPlayerIdx]) {
            return 0;                                   /* @0x40eaa0 */
        }
        pRec->anHeldSlot[0] = 0x1f;                       /* +0x17c @0x40eaa5 */
        pBurgerObj = (SceneNode *)pObj->nValue1;      /* +0x14 @0x40eaaf */
        pRec->pGrabSceneObj = pBurgerObj;  /* +0x40 @0x40eab8 */
        sceneObjSetClassMesh(pBurgerObj, pRec->pCharSceneObj, 8, 3);   /* @0x40eabb */
        sceneObjSetPos(pRec->pGrabSceneObj, 0, 300, 0, 2);   /* @0x430660 @0x40eacf */
        sceneObjSetPosOrient(pRec->pGrabSceneObj, 0, 0, 0, 2); /* @0x4307d0 @0x40eae0 */
        objHashRemoveFree(pObj);                        /* @0x414990 @0x40eae6 */
        return pRec->anHeldSlot[0];                       /* +0x17c @0x40eaeb */
    }
    if (pObj->nValue1 != 0 && pObj->pThrownRef != NULL) {   /* @0x40eaf8 */
        ThrownItem *pThrown = pObj->pThrownRef; /* +0x24 @0x40eaff */
        pRec->anHeldSlot[0] = nItemId;                  /* +0x17c @0x40eb09 */
        pRec->pGrabSceneObj = pThrown->pMesh;           /* [EDI+0x24] @0x40eb0f */
        sceneObjSetClassMesh(pThrown->pMesh, pRec->pCharSceneObj, 8, 3); /* @0x430db0 @0x40eb1e */
        sceneObjSetPos(pThrown->pMesh, 0, 300, 0, 2);   /* @0x430660 @0x40eb32 */
        sceneObjSetPosOrient(pThrown->pMesh, 0, 0, 0, 2); /* @0x4307d0 @0x40eb43 */
        objHashRemoveFree(pObj);                        /* @0x414990 @0x40eb49 */
        pThrown->pMesh = NULL;                          /* +0x24 @0x40eb53 */
        thrownItemFree(pThrown);                        /* @0x40f8d0 @0x40eb5a */
        memFreeDirect(pThrown);                         /* @0x43dd37 @0x40eb60 */
        return pRec->anHeldSlot[0];
    }
    pRec->anHeldSlot[0] = nItemId;                      /* +0x17c @0x40eb72 */
    nItemId = pObj->nId;                                /* +0x08 @0x40eb78 */
    if (nItemId <= 0x1e) {                              /* @0x40eb7b */
        pRec->pGrabSceneObj = (SceneNode *)sceneryObjAlloc(  /* @0x430200 @0x40eba3 */
            pRec->pCharSceneObj, 8, 0, 300, 0, 0, 0, 0,
            g_apLevelItemSlots[nItemId - 1].nMeshId); /* slot[id-1].nMeshId @0x4583b8+id*0x2c */
    }
    if (g_nGameMode == 3) {                             /* @0x40ebae */
        g_nCurrentItemId = 0;                           /* @0x458128 @0x40ebbe */
        nopDebugStub();                                 /* @0x440159 @0x40ebc8 */
    }
    return pRec->anHeldSlot[0];
}

/* playerUpdateAI @0x40b510 — the AI action phase machine. Per player:
 *  - phase 0xf: countdown nAiPhaseTimer; at 0 restore nAiPhaseNext; clear
 *    the input impulses while substate == 5 (get item) pending.
 *  - substate switch (+0x2e8): 2 = jump-landing clamp (support floor vs the
 *    child-mesh average, "sjmp" destination zone deepens the drop), 3 =
 *    cart approach/steer, 4 = release-cart sync, 5 = item action (the big
 *    nAiPhase switch), 6 = drop item (0 = face/throw1, 2/5 preserve the
 *    in-flight throw1/throw2 anim phases, 3 = release + throw2
 *    follow-through at phase 4); other substates only clear +0x2e8.
 *    The substate jump table @0x40bd88 serves substates 1..6 only
 *    (0x40b639/0x40bc95/0x40b5c5/0x40b630/0x40b6b0/0x40ba3a); the inner
 *    phase-switch defaults clear the phase AND the substate.
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
    float flVertVel;
    ThrownItem *pThrownItem;
    GxVec2 vThrowPolar;
    int anCharPos[3];

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
                pRec->flInputAccel = 0.0f;              /* +0x2e4 @0x40b5a1 */
                pRec->flInputTurn = 0.0f;               /* +0x2e0 @0x40b5a7 */
            }
            continue;
        }
        switch (pRec->nActionSubstate) {                /* +0x2e8 @0x40b5be */
        case 2:                                         /* jump landing @0x40bc95 */
            flAvgFront = nodeChannelAvgFloat(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode); /* @0x40bc9f */
            pWalkMesh = objFindTurret(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode); /* @0x40bcb2 */
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
            if (pRec->nCartMode != 0) {                 /* @0x40b6b0 */
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
                        if (pRec->nQuestStage == pRec->pQuestMessage->bAnswer) { /* +0x14 orig @0x40b766 */
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
                    if (pObj->nValue1 == 1) {          /* @0x40b7d2 */
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
                        presentFrame(pTex);        /* @0x410310 @0x40b978 */
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
                pRec->nAiPhase = 0;                     /* +0x2f0 @0x40ba2f */
                pRec->nActionSubstate = 0;              /* +0x2e8: shared default
                                                         * target also clears the
                                                         * substate (decompile
                                                         * switchD_0040ba57_caseD_1) */
                break;
            }
            break;
        case 6:                                         /* drop item @0x40ba3a */
            if (pRec->nCartMode != 0 || pRec->anHeldSlot[0] == 0) {
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
                if (aiCollectItem(pRec, 0) == 0 &&      /* @0x40f420 @0x40bab7 */
                    pRec->anHeldSlot[0] != 0) {         /* @0x40bacd */
                    if (pRec == &g_playerRecords[g_nLocalPlayerIdx]) {   /* @0x40baeb */
                        sndPlaySfx(0, 1, 0x15, 0xffff, 0, 0x400);    /* @0x437cf0 @0x40baff */
                    }
                    sceneObjSetClassMesh(pRec->pGrabSceneObj, NULL, 0, 3); /* @0x430db0 @0x40bb1d */
                    sceneNodeGetPos(pRec->pCharSceneNode, 0, anCharPos, 2);     /* @0x431270 @0x40bb31 */
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
                    flVertVel = flThrowDist * g_flNegOne - g_fl50;  /* (dist * -1) - 50
                                                                 * @0x40bb83 FMUL
                                                                 * [0x44b25c] / @0x40bb8b
                                                                 * FSUB [0x44b46c]: always
                                                                 * negative (upward toss),
                                                                 * -50..-250 */
                    pThrownItem = (ThrownItem *)malloc(sizeof(ThrownItem));  /* operator_new @0x43dd42 @0x40bb95 */
                    if (pThrownItem != NULL) {
                        AI_WRAP_SCALEA(pWalkNode);          /* @0x40bbb2..0x40bc0a */
                        gxVec2Set(&vThrowPolar, flThrowDist + g_fl25,
                                  pWalkNode->flScaleA);     /* @0x434fa0 @0x40bc2e */
                        playerThrowItemCtor(pThrownItem, pRec->anHeldSlot[0],  /* @0x40f720 @0x40bc61 */
                                            (float)anCharPos[2],
                                            (float)anCharPos[0],
                                            (float)anCharPos[1] - 1000.0f,
                                            vThrowPolar.x, vThrowPolar.y,
                                            flVertVel, pRec->pGrabSceneObj, nPlayer);
                    }
                    pRec->anHeldSlot[0] = 0;                /* @0x40bc71 */
                    pRec->pGrabSceneObj = NULL;             /* @0x40bc77 */
                }
                /* Release follow-through (unconditional in the original):
                 * throw2 plays through phases 4 (reset) -> 5 (step) -> 6
                 * (anim-update), which is why the switch preserves phases
                 * 2/5 below instead of resetting them. */
                pRec->nAiPhase = 4;                         /* +0x2f0 @0x40bc... */
                pRec->pAnimSet = pRec->apAnmSets[5];        /* throw2 (+0x2bc) */
                break;
            case 2:                                     /* throw1 stepping / throw2
                                                         * follow-through: zero the
                                                         * inputs but preserve the
                                                         * phase (decompile
                                                         * switchD_0040ba57_caseD_2) */
            case 5:
                pRec->flInputAccel = 0.0f;
                pRec->flInputTurn = 0.0f;
                break;
            default:                                    /* 1,4,6+: phase table
                                                         * @0x40bdd0 serves 0..5
                                                         * (CMP EAX,5 + JA
                                                         * @0x40ba55); abort the
                                                         * throw */
                pRec->nAiPhase = 0;
                pRec->nActionSubstate = 0;              /* shared default target
                                                         * clears both (decompile
                                                         * switchD_0040ba57_caseD_1) */
                break;
            }
            break;
        default:                                        /* substate 0/1/7: the
                                                         * substate jump table
                                                         * @0x40bd88 has entries
                                                         * only for 1..6, so anything
                                                         * else clears +0x2e8
                                                         * (decompile
                                                         * switchD_0040b5be_caseD_1) */
            pRec->nActionSubstate = 0;
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
                        objAngleToPoint(pRec->pSubObjA, pObj->flPosX, pObj->flPosZ));
    if (flDiff <= (float)g_dbl0_1 && flDiff >= (float)g_dblNeg0_1) {
        flDist = objDistToPoint(pRec->pSubObjA, pObj->flPosX, pObj->flPosZ); /* @0x40f11c */
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
    nWalkX = objPolarPosLookup(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode);   /* @0x40f507 */
    nWalkZ = objPolarPosLookup2(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode);  /* @0x40f522 */
    nCartX = objPolarPosLookup(pRec->pSubObjB, (intptr_t)pRec->pCartSceneObj);    /* @0x40f53d */
    nCartZ = objPolarPosLookup2(pRec->pSubObjB, (intptr_t)pRec->pCartSceneObj);   /* @0x40f558 */
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
                    sceneObjSetClassMesh(pThrownMesh, pRec->pCartSceneObj, 0, 3); /* @0x40f265 */
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
        sceneObjSetClassMesh(pThrownMesh, pRec->pCartSceneObj, 0, 3); /* @0x40f265 */
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
    nWalkX = objPolarPosLookup(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode);   /* @0x40f617 */
    nWalkZ = objPolarPosLookup2(pRec->pSubObjA, (intptr_t)pRec->pCharSceneNode);  /* @0x40f632 */
    nCartX = objPolarPosLookup(pRec->pSubObjB, (intptr_t)pRec->pCartSceneObj);    /* @0x40f64d */
    nCartZ = objPolarPosLookup2(pRec->pSubObjB, (intptr_t)pRec->pCartSceneObj);   /* @0x40f668 */
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

/* --- thrown-item cluster (0x40f720..0x40fde0) --- */

/* playerThrowItemCtor @0x40f720 — finish a thrown shopping item. Allocates
 * the 0x48-byte physics WorldNode (worldNodeCtor), adds its child mesh
 * (extents 150/2/500, mesh tag 1) and a turret entry keyed by pMesh with
 * channel values 20/20, marks WorldNode+0x1c = 1, places the node at
 * {flNodeX, flNodeY, flNodeZ} (the original argument order is Z, X,
 * Y-1000), then fills the record: +0x14 = flSpeed, +0x18 = flHeading,
 * +0x20 = nItemId, +0x24 = pMesh, +0x2c = nOwnerIdx, +0x0c = 0, the
 * child-mesh entry's flVertVel (+0x44) = flVertVel and
 * g_playerRecords[nOwnerIdx].nLastThrownItemId (+0x1e4) = nItemId. The
 * list is capped at 10 items: when the count exceeds 10 the oldest entry
 * (g_pThrownItemTail) has its landed pickup object (if any) removed from
 * the id hash and is freed. The record is head-inserted into
 * g_pThrownItemHead/Tail (+0x04 next toward the tail, +0x08 toward the
 * head). */
ThrownItem *playerThrowItemCtor(ThrownItem *pItem, int nItemId, float flNodeZ,
                                float flNodeX, float flNodeY, float flSpeed,
                                float flHeading, float flVertVel,
                                SceneNode *pMesh, int nOwnerIdx) /* @0x40f720 */
{
    WorldNode *pNode;
    ObjChildMesh *pEntry;
    ThrownItem *pOld;

    /* 64-bit port: +0x14/+0x18 are flSpeed/flHeading in the 32-bit
     * layout; use struct fields (&flSpeed is layout-stable since the two
     * floats are contiguous). Raw +0x14 would hit pPrev/pLandedObj. */
    gxVec2SetAngleZero((GxVec2 *)&pItem->flSpeed);      /* @0x434f90 @0x40f73f */
    pNode = worldNodeCtor(malloc(sizeof(WorldNode)), 0, 0, 0, 0, NULL);     /* operator_new @0x43dd42
                                                                * @0x40f746 + worldNodeCtor
                                                                * @0x402a20 @0x40f76a */
    pItem->pMesh = pMesh;                                      /* +0x24 @0x40f78b */
    pItem->pObj = pNode;                                       /* +0x10 @0x40f79c */
    if (pNode != NULL) {
        nodeAddChildMesh(pNode, 0, 0, 0, 150.0f, 2.0f,         /* @0x4053a0 @0x40f79f */
                         (intptr_t)pMesh, 500.0f, 1);
        objTurretAdd(pNode, 0, 0, 0, 0, (intptr_t)pMesh);           /* @0x405280 @0x40f7b3 */
        pNode->field_1c = 1;                                   /* @0x40f7c1 */
        nodeSetTransformFromChannels(pNode, (int)flNodeX,      /* @0x404e00 @0x40f7e7 */
                                     (int)flNodeY, (int)flNodeZ, 0, 0);
        objTurretSetValue(pNode, (intptr_t)pMesh, 0x14, 0x14);      /* @0x405370 @0x40f804 */
        pEntry = objFindTurret(pNode, (intptr_t)pMesh);             /* @0x405120 @0x40f810 */
        if (pEntry != NULL) {
            pEntry->flVertVel = flVertVel;                     /* +0x44 @0x40f81d */
        }
    }
    pItem->flSpeed = flSpeed;           /* +0x14 @0x40f7ff */
    pItem->flHeading = flHeading;         /* +0x18 @0x40f801 */
    pItem->nOwnerIdx = nOwnerIdx;                              /* +0x2c @0x40f824 */
    pItem->nItemId = nItemId;                                  /* +0x20 @0x40f827 */
    pItem->pLandedObj = NULL;                                  /* +0x0c @0x40f82d */
    g_playerRecords[nOwnerIdx].nLastThrownItemId = nItemId;    /* +0x1e4 @0x40f83d */
    g_nThrownItemCount++;                                      /* @0x458974 @0x40f844 */
    if (g_nThrownItemCount > 10) {                             /* @0x40f852 */
        pOld = g_pThrownItemTail;                              /* @0x458970 @0x40f854 */
        if (pOld != NULL) {
            if (pOld->pLandedObj != NULL) {                    /* +0x0c @0x40f85a */
                objHashRemoveFree(pOld->pLandedObj);           /* @0x414990 @0x40f862 */
            }
            thrownItemFree(pOld);                              /* @0x40f8d0 @0x40f876 */
            memFreeDirect(pOld);                               /* @0x43dd37 @0x40f87c */
        }
    }
    if (g_pThrownItemHead != NULL) {                           /* @0x45896c @0x40f884 */
        g_pThrownItemHead->pPrev = pItem;                      /* +0x08 @0x40f88d */
    } else {
        g_pThrownItemTail = pItem;                             /* @0x40f897 */
    }
    pItem->pNext = g_pThrownItemHead;                          /* +0x04 @0x40f8a1 */
    g_pThrownItemHead = pItem;                                 /* @0x40f8a4 */
    pItem->pPrev = NULL;                                       /* +0x08 @0x40f8aa */
    return pItem;
}

/* thrownItemFree @0x40f8d0 — unlink pItem from the g_pThrownItemHead/Tail
 * list (+0x04 = next toward the tail, +0x08 = next toward the head),
 * sceneNodeFree the item mesh (+0x24, children too) and objDtor+free the
 * physics world node (+0x10), then decrement g_nThrownItemCount. */
void thrownItemFree(ThrownItem *pItem) /* @0x40f8d0 */
{
    if (g_pThrownItemHead == pItem) {                          /* @0x40f8d8 */
        g_pThrownItemHead = pItem->pNext;                      /* +0x04 @0x40f8dc */
    }
    if (g_pThrownItemTail == pItem) {                          /* @0x40f8e4 */
        g_pThrownItemTail = pItem->pPrev;                      /* +0x08 @0x40f8ec */
    }
    if (pItem->pNext != NULL) {                                /* @0x40f8f5 */
        pItem->pNext->pPrev = pItem->pPrev;                    /* @0x40f8ff */
    }
    if (pItem->pPrev != NULL) {                                /* @0x40f902 */
        pItem->pPrev->pNext = pItem->pNext;                    /* @0x40f90c */
    }
    if (pItem->pMesh != NULL) {                                /* +0x24 @0x40f90f */
        sceneNodeFree(pItem->pMesh, 1);                        /* @0x430460 @0x40f919 */
    }
    if (pItem->pObj != NULL) {                                 /* +0x10 @0x40f921 */
        objDtor(pItem->pObj);                                  /* @0x402ab0 @0x40f92a */
        memFreeDirect(pItem->pObj);                            /* @0x43dd37 @0x40f92f */
    }
    g_nThrownItemCount--;                                      /* @0x40f93e */
}

/* itemThrowUpdate @0x40f950 — gameUpdate pass 1 over the thrown-item list.
 * Reads the floor plane from the child-mesh entry's raycast surface
 * (AiNavNode) offset by the entry's world position, then integrates the
 * turret entry's flHeight (+0x40) / flVertVel (+0x44):
 *  - above the floor: gravity (+18.0f per frame), height += velocity, and
 *    when the floor is crossed the height is clamped and the overshoot is
 *    subtracted from the velocity; horizontal speed decays by 0.999.
 *  - at/below the floor with velocity < 18.0f: land — velocity 0, height =
 *    floor, speed *= 0.5; when the speed reaches [-15, 15] the item comes
 *    to rest: the mesh is re-oriented/positioned, a 0x50-byte pickup
 *    EventObject is built (sceneObjCtor3 + objHashRegister) with
 *    +0x10 = (int)channel height, +0x14 = 1 and +0x24 = this record, the
 *    world node is freed and the owner's nLastThrownItemId (+0x1e4) is
 *    cleared when it still names this item.
 *  - at/below the floor with velocity >= 18.0f: bounce — velocity *=
 *    -0.4, height += velocity and a positional bounce sfx (bank 1,
 *    index 0x14, volume 65000, id 0x6e) plays from a 0x1c-byte emitter;
 *    horizontal speed decays by 0.999.
 * The non-rest paths then objMovePolar the node by {speed, heading} and
 * run the pickup scan over g_playerRecords: players on foot (nCartMode ==
 * 0) within the z band (item-1200, item+500] and within 1000.0f ground
 * distance pick the item up through playerCollectItem; the local player
 * additionally notifies the server (sub-cmd 0x3d) and plays sfx 0x16.
 * The picked-up record frees with its mesh left allocated (pMesh = NULL
 * before thrownItemFree, matching the original's deliberate leak). */
void itemThrowUpdate(ThrownItem *pItem) /* @0x40f950 */
{
    WorldNode *pObj;
    ObjChildMesh *pEntry;
    AiNavNode *pSurf;
    EventObject *pNew;
    PlayerRecord *pRec;
    void *pEmitter;
    float flFloor;
    float flPlayerZ;
    float flItemZ;
    float flHeight;
    int nX;
    int nZ;
    int nFloor;
    int i;
    short anRot[3];

    pObj = pItem->pObj;                                        /* +0x10 @0x40f970 */
    if (pObj == NULL) {                                        /* @0x40f975 */
        return;
    }
    pEntry = objFindTurret(pObj, (intptr_t)pItem->pMesh);           /* @0x405120 @0x40f97f */
    flFloor = 0.0f;                                            /* @0x40f9cf */
    pSurf = (pEntry != NULL) ? (AiNavNode *)pEntry->pSurface : NULL;  /* +0x3c @0x40f986 */
    if (pSurf != NULL) {
        nX = (int)pObj->vPos.y;                                /* ftol @0x40f993 */
        nZ = (int)pObj->vPos.x;                                /* ftol @0x40f9ac */
        flFloor = (float)
            ((nX + pEntry->vWorldA.y - pSurf->nRefX) * pSurf->flSlopeX +  /* @0x40f9a0 */
             (nZ + pEntry->vWorldA.x - pSurf->nRefZ) * pSurf->flSlopeZ +
             pSurf->nRefY);                                               /* @0x40f9c6 */
    }
    if (flFloor <= pEntry->flHeight) {                         /* +0x40 @0x40f9e6 */
        if (pEntry->flVertVel < g_fl18) {                      /* +0x44 @0x40fa29 */
            /* rest @0x40fa99 */
            pEntry->flVertVel = 0.0f;                          /* @0x40fa9d */
            pEntry->flHeight = flFloor;                        /* @0x40faa0 */
            pItem->flSpeed *= g_flHalf;                        /* 0.5f @0x44b274 @0x40faa6 */
            if (pItem->flSpeed > g_fl15 || pItem->flSpeed < g_flNeg15) {  /* @0x40fab7 */
                objMovePolar(pObj, pItem->flSpeed, pItem->flHeading);     /* @0x40fc2e..0x40fc53 */
            } else {
                /* come to rest: register the pickup object @0x40fad1 */
                sceneObjGetPos(pItem->pMesh, anRot, 2);        /* @0x4317e0 @0x40fadc */
                sceneObjSetPosOrient(pItem->pMesh, 0, anRot[1], 0x4000, 2);  /* @0x4307d0 @0x40faf2 */
                nZ = objPolarPosLookup2(pObj, (intptr_t)pItem->pMesh);            /* @0x4051c0 @0x40fb03 */
                flHeight = nodeChannelAvgFloat(pObj, (intptr_t)pItem->pMesh);     /* @0x4050c0 @0x40fb10 */
                nX = objPolarPosLookup(pObj, (intptr_t)pItem->pMesh);             /* @0x405140 @0x40fb22 */
                sceneObjSetPos(pItem->pMesh, nX, (int)flHeight, nZ, 2);      /* @0x430660 @0x40fb2c */
                pNew = (EventObject *)malloc(sizeof(EventObject));            /* operator_new @0x43dd42 @0x40fb33 */
                if (pNew != NULL) {                            /* @0x40fb41 */
                    nX = (int)pObj->vPos.y;                    /* ftol @0x40fb53 */
                    nZ = (int)pObj->vPos.x;                    /* ftol @0x40fb5f */
                    flHeight = nodeChannelAvgFloat(pObj, (intptr_t)pItem->pMesh); /* @0x40fb6e */
                    sceneObjCtor3(pNew, pItem->nItemId, (float)nZ,           /* @0x4146a0 @0x40fb8d */
                                  (float)nX, flHeight);
                }
                pItem->pLandedObj = pNew;                      /* +0x0c @0x40fba1 */
                if (pNew != NULL) {
                    pNew->nValue0 = (int)nodeChannelAvgFloat(pObj,          /* +0x10 @0x40fbb5 */
                                                              (intptr_t)pItem->pMesh);
                    pNew->nValue1 = 1;                        /* +0x14 @0x40fbbb */
                    pNew->pThrownRef = pItem;               /* +0x24 @0x40fbbf */
                    objHashRegister(pNew);                     /* @0x4148f0 @0x40fbcc */
                }
                if (pObj != NULL) {                            /* @0x40fbd7 */
                    objDtor(pObj);                             /* @0x402ab0 @0x40fbdd */
                    memFreeDirect(pObj);                       /* @0x43dd37 @0x40fbe3 */
                }
                pItem->pObj = NULL;                            /* +0x10 @0x40fbee */
                pItem->flSpeed = 0.0f;                         /* +0x14 @0x40fbf1 */
                if (g_playerRecords[pItem->nOwnerIdx].nLastThrownItemId ==    /* @0x40fc03 */
                    pItem->nItemId) {
                    g_playerRecords[pItem->nOwnerIdx].nLastThrownItemId = 0;  /* @0x40fc13 */
                }
                return;                                        /* @0x40fc1b */
            }
        } else {
            /* bounce @0x40fa2e */
            pEntry->flVertVel *= g_flNeg0_4;                   /* @0x40fa31 */
            pEntry->flHeight += pEntry->flVertVel;             /* @0x40fa3f */
            pEmitter = malloc(sizeof(SndEmitter));                           /* operator_new @0x43dd42 @0x40fa42 */
            if (pEmitter != NULL) {                            /* @0x40fa50 */
                nZ = (int)pObj->vPos.x;                        /* ftol @0x40fa5f */
                nFloor = (int)flFloor;                         /* ftol @0x40fa69 */
                nX = (int)pObj->vPos.y;                        /* ftol @0x40fa72 */
                sndPlaySfx3D(pEmitter, 1, 0x14, 65000, 0x6e,   /* @0x42bcd0 @0x40fa87 */
                             0, 0, nX, nFloor, nZ, 0);
            }
            pItem->flSpeed *= g_fl0_999;                       /* @0x40fc3f */
            objMovePolar(pObj, pItem->flSpeed, pItem->flHeading);            /* @0x40fc53 */
        }
    } else {
        /* airborne @0x40f9e8 */
        pEntry->flVertVel += g_fl18;                           /* gravity @0x40f9e8 */
        pEntry->flHeight += pEntry->flVertVel;                 /* @0x40f9f6 */
        if (flFloor <= pEntry->flHeight) {                     /* @0x40fa04 */
            pEntry->flVertVel -= pEntry->flHeight - flFloor;   /* @0x40fa15 */
            pEntry->flHeight = flFloor;                        /* @0x40fa0e */
        }
        pItem->flSpeed *= g_fl0_999;                           /* @0x40fc3f */
        objMovePolar(pObj, pItem->flSpeed, pItem->flHeading);  /* @0x40fc53 */
    }
    /* pickup scan @0x40fc58 */
    for (i = 0; i < g_nPlayerCount; i++) {
        pRec = &g_playerRecords[i];
        if (pRec->nCartMode != 0) {                            /* +0x174 @0x40fc7a */
            continue;
        }
        flPlayerZ = nodeChannelAvgFloat(pRec->pSubObjC,        /* +0x264 @0x40fc8b */
                                        (intptr_t)pRec->pCartSceneObj);
        flItemZ = nodeChannelAvgFloat(pObj, (intptr_t)pItem->pMesh);              /* @0x40fc9b */
        if (flPlayerZ <= flItemZ - g_fl1200 ||                 /* @0x40fca2..0x40fc02 */
            flPlayerZ > flItemZ + g_fl500) {
            continue;
        }
        nX = (int)pObj->vPos.y;                                /* ftol @0x40fcca */
        nZ = (int)pObj->vPos.x;                                /* ftol @0x40fcde */
        if (objDistToPoint(pRec->pSubObjC, (float)nZ,          /* @0x405050 @0x40fcf1 */
                           (float)nX) >= g_fl1000) {
            continue;                                          /* @0x40fcf6 */
        }
        if (playerCollectItem(pRec, pItem->nItemId,            /* @0x40f1f0 @0x40fd0c */
                              pItem->pMesh) == 0) {
            continue;
        }
        if (pRec == &g_playerRecords[g_nLocalPlayerIdx]) {     /* @0x40fd64 */
            sndPlaySfx(0, 1, 0x16, 0xffff, 0, 0x400);          /* @0x437cf0 @0x40fd9f */
        }
        pItem->pMesh = NULL;                                   /* +0x24 @0x40fda9 */
        thrownItemFree(pItem);                                 /* @0x40f8d0 @0x40fdb4 */
        memFreeDirect(pItem);                                  /* @0x43dd37 @0x40fdba */
        return;
    }
}

/* itemMeshFollowUpdate @0x40fde0 — gameUpdate pass 2: clamp the record's
 * +0x14 height channel against the node's last polar length (WorldNode
 * +0x3c): above 100 the channel is raised to at least 100, within
 * [-100, 100] it takes the node value, below -100 it is lowered to at
 * most -100. When WorldNode+0x18 is nonzero the +0x18 heading adopts the
 * node's last move angle (+0x38). The mesh is then re-oriented in mode 5
 * with the constant Euler triple (0x173, 0x348, 0xfa) and repositioned
 * onto the node in mode 2 with the height channel minus 200. */
void itemMeshFollowUpdate(ThrownItem *pItem) /* @0x40fde0 */
{
    WorldNode *pObj;
    float flHeight;
    int nX;
    int nZ;

    pObj = pItem->pObj;                                        /* +0x10 @0x40fde3 */
    if (pObj == NULL) {                                        /* @0x40fde8 */
        return;
    }
    if (pObj->field_3c > g_fl100) {                            /* +0x3c @0x40fdf4 */
        if (pItem->flSpeed < g_fl100) {                        /* +0x14 @0x40fe07 */
            pItem->flSpeed = g_fl100;                          /* @0x40fe10 */
        }
    } else if (pObj->field_3c >= g_flNeg100) {                 /* @0x40fe1b */
        pItem->flSpeed = pObj->field_3c;                       /* @0x40fe3f */
    } else if (pItem->flSpeed > g_flNeg100) {                  /* @0x40fe2e */
        pItem->flSpeed = g_flNeg100;                           /* @0x40fe37 */
    }
    if (pObj->field_18 != 0) {                                 /* @0x40fe47 */
        pItem->flHeading = pObj->flScaleC;                     /* +0x38 @0x40fe4e */
    }
    sceneObjSetPosOrient(pItem->pMesh, 0x173, 0x348, 0xfa, 5); /* @0x4307d0 @0x40fe72 */
    nZ = objPolarPosLookup2(pObj, (intptr_t)pItem->pMesh);          /* @0x4051c0 @0x40fe83 */
    flHeight = nodeChannelAvgFloat(pObj, (intptr_t)pItem->pMesh);   /* @0x4050c0 @0x40fe90 */
    nX = objPolarPosLookup(pObj, (intptr_t)pItem->pMesh);           /* @0x405140 @0x40fea7 */
    sceneObjSetPos(pItem->pMesh, nX, (int)flHeight - 200, nZ, 2);            /* @0x430660 @0x40feb1 */
}
