#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "player.h"
#include "player_physics.h"
#include "player_ai.h"
#include "gx.h"
#include "config.h"
#include "obj.h"
#include "pool.h"
#include "stubs.h"
#include "time.h"
#include "util.h"
#include "zone.h"
#include "scene.h"
#include "gameplay.h"
#include "levelselect.h"
#include "menu.h"
#include "options.h"
#include "sound.h"

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
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) <= pEo->flHeight)) {   /* +0x40 @0x427135 */
                continue;
            }
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) >= pEo->flHeight2)) {   /* +0x44 @0x427156 */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pWalk->vPos.x,
                                 (float)(int)pWalk->vPos.y) == 0) {            /* @0x427191 */
                continue;
            }
            if (bSquashed || rand() % 100 < 5) {                               /* @0x4271b8 */
                void *pEmitter = malloc(0x1c);                                 /* operator_new @0x43dd42 */
                if (pEmitter != NULL) {
                    sndPlaySfx3D(pEmitter, 1, 0x1a, 0xfde8, 0xff, (void *)(size_t)nCharKey, 0, 0, 0, 0, 0);
                }
            }
            gxVec2Set(&vIn, pEo->flPosZ - (float)(int)pWalk->vPos.y,        /* +0x3c @0x42720f */
                      pEo->flPosX - (float)(int)pWalk->vPos.x);             /* +0x38 @0x42724c */
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
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) <= pEo->flHeight)) {   /* @0x427319 */
                continue;
            }
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) >= pEo->flHeight2)) {   /* @0x42733a */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pWalk->vPos.x,
                                 (float)(int)pWalk->vPos.y) == 0) {            /* @0x42737b */
                continue;
            }
            if (bHurtSfx == 0) {                                               /* TEST BL,BL @0x427397 */
                void *pEmitter = malloc(0x1c);                                 /* @0x42739d */
                if (pEmitter != NULL) {
                    sndPlaySfx3D(pEmitter, 1, 7, 0xfde8, 0xff, (void *)(size_t)nCharKey, 0, 0, 0, 0, 0);
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
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) <= pEo->flHeight)) {   /* @0x42743a */
                continue;
            }
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) >= pEo->flHeight2)) {   /* @0x42745b */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pWalk->vPos.x,
                                 (float)(int)pWalk->vPos.y) == 0) {            /* @0x427496 */
                continue;
            }
            gxVec2Set(&vIn, pEo->flPosZ - (float)(int)pWalk->vPos.y,        /* @0x4274b2 */
                      pEo->flPosX - (float)(int)pWalk->vPos.x);
            mathVec2Polar(&vPolar, &vIn);                                      /* @0x427500 */
            pRec->vVelPolar.y = vPolar.y;                                      /* +0x220 @0x427509 */
            pRec->vVelPolar.x = 400.0f;                                        /* 0x43c80000 @0x427513 */
            pRec->flTurnAccum = 0.23f;                                         /* @0x42751a */
            break;
        }
        for (pEo = objFindById(g_nObjIdTele, 0); pEo != NULL;                  /* @0x450de4 @0x427524 */
             pEo = objHashNextSame(pEo)) {
            short nScale;
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) <= pEo->flHeight)) {   /* @0x427555 */
                continue;
            }
            if (!(nodeChannelAvgFloat(pWalk, nCharKey) >= pEo->flHeight2)) {   /* @0x427576 */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pWalk->vPos.x,
                                 (float)(int)pWalk->vPos.y) == 0) {            /* @0x4275b1 */
                continue;
            }
            {
                int nTmp = (int)pRec->vVelPolar.y;                             /* ftol of heading */
                nScale = (short)(pEo->nValue1 - (short)nTmp);                 /* @0x4275de */
            }
            {
                int nSpeedBits;
                memcpy(&nSpeedBits, &pRec->flCurSpeed, 4);         /* +0x1f4 raw bits @0x4275ed */
                nodeSetTransformFromChannels(pWalk,                            /* @0x427607 */
                                             (int)pEo->flPosZ,              /* arg2 = ftol(+0x3c) */
                                             pEo->nValue0,                    /* arg3 = +0x10 raw */
                                             (int)pEo->flPosX,              /* arg4 = ftol(+0x38) */
                                             nSpeedBits,                       /* arg5 = +0x1f4 raw bits */
                                             nScale);
            }
            pRec->vAccPolar.y += (float)pEo->nValue1 * g_fl9_588e_05;         /* +0x218 @0x427615 */
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
            if (!(nodeChannelAvgFloat(pPos, nCartKey) <= pEo->flHeight)) {  /* @0x427b90 */
                continue;
            }
            if (!(nodeChannelAvgFloat(pPos, nCartKey) >= pEo->flHeight2)) {  /* @0x427bb4 */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pPos->vPos.x,
                                 (float)(int)pPos->vPos.y) == 0) {               /* @0x427bef */
                continue;
            }
            {
                GxVec2 vIn2;
                gxVec2Set(&vIn2, pEo->flPosZ - (float)(int)pPos->vPos.y,      /* @0x427c0e */
                          pEo->flPosX - (float)(int)pPos->vPos.x);
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
            if (!(nodeChannelAvgFloat(pPos, nCartKey) <= pEo->flHeight)) {  /* @0x427d1e */
                continue;
            }
            if (!(nodeChannelAvgFloat(pPos, nCartKey) >= pEo->flHeight2)) {  /* @0x427d42 */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pPos->vPos.x,
                                 (float)(int)pPos->vPos.y) == 0) {               /* @0x427d7d */
                continue;
            }
            if (bHurtSfx == 0) {                                             /* @0x427d9c */
                void *pEmitter = malloc(0x1c);                               /* @0x427da2 */
                if (pEmitter != NULL) {
                    sndPlaySfx3D(pEmitter, 1, 7, 0xfde8, 0xff, (void *)(size_t)nCartKey, 0, 0, 0, 0, 0);
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
            if (!(nodeChannelAvgFloat(pPos, nCartKey) <= pEo->flHeight)) {  /* @0x427e72 */
                continue;
            }
            if (!(nodeChannelAvgFloat(pPos, nCartKey) >= pEo->flHeight2)) {  /* @0x427e96 */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pPos->vPos.x,
                                 (float)(int)pPos->vPos.y) == 0) {               /* @0x427ed1 */
                continue;
            }
            gxVec2Set(&vIn2, pEo->flPosZ - (float)(int)pPos->vPos.y,          /* @0x427ef0 */
                      pEo->flPosX - (float)(int)pPos->vPos.x);
            mathVec2Polar(&vPolar, &vIn2);                                       /* @0x427f4a */
            pRec->flPosVelAng = vPolar.y;                                        /* +0x260 @0x427f5d */
            pRec->flPosVelLen = 450.0f;                                          /* 0x43e10000 @0x427f63 */
            pRec->flPosTurnAccum = 0.2f;                                         /* 0x3e4ccccd @0x427f6d */
            break;
        }
        for (pEo = objFindById(g_nObjIdTele, 0); pEo != NULL;            /* @0x450de4 @0x427f77 */
             pEo = objHashNextSame(pEo)) {
            short nScale;
            if (!(nodeChannelAvgFloat(pPos, nCartKey) <= pEo->flHeight)) {  /* @0x427fab */
                continue;
            }
            if (!(nodeChannelAvgFloat(pPos, nCartKey) >= pEo->flHeight2)) {  /* @0x427fcf */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pPos->vPos.x,
                                 (float)(int)pPos->vPos.y) == 0) {               /* @0x42800a */
                continue;
            }
            {
                int nTmp = (int)pRec->flPosVelAng;                           /* ftol of +0x260 @0x428032 */
                nScale = (short)(pEo->nValue1 - (short)nTmp);               /* @0x428043 */
            }
            {
                int nSpeedBits;
                memcpy(&nSpeedBits, &pRec->flPosSpeed, 4);         /* +0x234 raw bits @0x428046 */
                nodeSetTransformFromChannels(pPos,                             /* @0x428060 */
                                             (int)pEo->flPosZ,              /* arg2 = ftol(+0x3c) */
                                             pEo->nValue0,                    /* arg3 = +0x10 raw */
                                             (int)pEo->flPosX,              /* arg4 = ftol(+0x38) */
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

    if (pWalk->field_3c <= flBound) {                      /* @0x4289a7 C0=0 */
        flBound = -flBound;                                /* @0x4289ca */
        if (flBound <= pWalk->field_3c) {                  /* @0x4289cc JNZ: -bound <= +0x3c */
            flBound = pWalk->field_3c;                     /* @0x4289ed */
        } else if (flSpeed < flBound) {                    /* @0x4289d6 JZ: speed < -bound */
            flBound = flSpeed;
        }
    } else if (flBound < flSpeed) {                        /* @0x4289b5: bound < +0x3c and
                                                            * bound < speed @0x4289b9 */
        flBound = flSpeed;
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
            flRest = flRest + pPos->flImpulse;                /* node +0x40 @0x42887c */
            if (flBound <= flRest && flRest < fNeg) {      /* @0x42888a */
                flRest = fNeg;
            }
        }
    }
    pRec->flPosTurnAccum = flRest;                         /* +0x244 @0x42889b */
    /* clamp 2: +0x234 against the +0x230 bound and node +0x3c — transfer
     * the collision-imparted node speed (objUpdateFire writes +0x3c) into
     * the channel, capped at ±bound; beyond the bound keep
     * max(bound, speed) so a bump still launches the node. */
    {
        float fA = pRec->flCurAccFric;                     /* +0x230 @0x4288a1 */
        float fS = pRec->flPosSpeed;                       /* +0x234 @0x4288aa */
        if (pPos->field_3c <= fA) {                        /* @0x4288a7 C0=0 */
            fA = -fA;                                      /* @0x4288d0 */
            if (fA <= pPos->field_3c) {                    /* @0x4288d2 JNZ */
                fA = pPos->field_3c;                       /* @0x4288f3 */
            } else if (fS < fA) {                          /* @0x4288dc JZ: speed < -bound */
                fA = fS;
            }
        } else if (fA < fS) {                              /* @0x4288bb: bound < +0x3c and
                                                            * bound < speed @0x4288bf */
            fA = fS;
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

    pRec->nCartMode = 0;                                   /* @0x40e04d */
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

/* objWalkAnimSync @0x409b10 — objUpdateFire shot-response tail: when the
 * round gate (+0x174) is armed, take a rand()%100 stumble roll against
 * the threshold (pRec->nStatStrength*5 + 60 - pSelf->nStatStrength*10)
 * * 0.2 (@0x44b454/0x44b450/0x44b44c/0x44b2d4) and re-derive the walk
 * and pos nodes from the cart via syncCartNodeChannelsToWalkPos (which
 * clears the gate, making it a one-shot). The caller passes the shot
 * victim's pParent (+0x44) as pRec and the owning node's pParent as
 * pSelf (the original __thiscall ECX); the original's second stack
 * argument is pRec itself. */
void objWalkAnimSync(struct PlayerRecord *pSelf, struct PlayerRecord *pRec) /* @0x409b10 */
{
    static const float g_fl5 = 5.0f;   /* @0x44b454 */
    static const float g_fl60 = 60.0f; /* @0x44b450 */
    static const float g_fl10 = 10.0f; /* @0x44b44c */
    static const float g_fl0_2 = 0.2f; /* @0x44b2d4 */
    float flThreshold;

    if (pSelf->nCartMode != 0) {                           /* +0x174 @0x409b14 */
        flThreshold = ((float)pRec->nStatStrength * g_fl5 + g_fl60 -
                       (float)pSelf->nStatStrength * g_fl10) * g_fl0_2; /* @0x409b1e */
        if ((float)(rand() % 100) < flThreshold) {         /* rand @0x43ea7c @0x409b4c */
            syncCartNodeChannelsToWalkPos(pSelf);          /* @0x40e040 @0x409b6d */
        }
    }
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
            flTurn = flTurn + pCart->flImpulse;               /* node +0x40 @0x428aac */
            if (flTurnB <= flTurn && flTurn < fNeg) {      /* @0x428abf */
                flTurn = fNeg;
            }
        }
    }
    pRec->flCartTurnAccum = flTurn;                        /* +0x284 @0x428acb */
    {
        float fA = pRec->flCartFriction;                   /* +0x270 @0x428ad1 */
        float fS = pRec->flCartCurSpeed;                   /* +0x274 @0x428ada */
        if (pCart->field_3c <= fA) {                       /* @0x428ad7 C0=0 */
            fA = -fA;                                      /* @0x428b00 */
            if (fA <= pCart->field_3c) {                   /* @0x428b02 JNZ */
                fA = pCart->field_3c;                      /* @0x428b23 */
            } else if (fS < fA) {                          /* @0x428b0c JZ: speed < -bound */
                fA = fS;
            }
        } else if (fA < fS) {                              /* @0x428aeb: bound < +0x3c and
                                                            * bound < speed @0x428aef */
            fA = fS;
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
            if (pEo->flHeight < flAvgC || flAvgC < pEo->flHeight2) {   /* @0x4280e1 */
                float flAvgK = nodeChannelAvgFloat(pCart, nCartKey);
                if (!(pEo->flHeight >= flAvgK && pEo->flHeight2 <= flAvgK)) {  /* @0x428121 */
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
            if (!(nodeChannelAvgFloat(pCart, nCharKey) <= pEo->flHeight)) {  /* @0x4282e9 */
                continue;
            }
            if (!(nodeChannelAvgFloat(pCart, nCharKey) >= pEo->flHeight2)) {  /* @0x42830a */
                continue;
            }
            if (objContainsPoint(pEo, (float)(int)pCart->vPos.x,
                                 (float)(int)pCart->vPos.y) == 0) {               /* @0x428345 */
                continue;
            }
            {
                int nTmp = (int)pRec->vCartVelPolar.y;                      /* ftol +0x2a0 @0x428399 */
                nScale = (short)(pEo->nValue1 - (short)nTmp);              /* @0x4283a6 */
            }
            {
                int nSpeedBits;
                memcpy(&nSpeedBits, &pRec->flCartCurSpeed, 4);     /* +0x274 raw bits @0x4283b5 */
                nodeSetTransformFromChannels(pCart,                            /* @0x4283c6 */
                                             (int)pEo->flPosZ,              /* ftol(+0x3c) */
                                             pEo->nValue0,                    /* +0x10 raw */
                                             (int)pEo->flPosX,              /* ftol(+0x38) */
                                             nSpeedBits,                       /* +0x274 raw bits */
                                             nScale);
            }
            pRec->vCartAccPolar.y += (float)pEo->nValue1 * g_fl9_588e_05;    /* +0x298 @0x4283d6 */
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
    ThrownItem *pItem;
    int i;

    g_nGameUpdateTick++;                                                  /* @0x45e5dc @0x426ee0 */
    for (pItem = g_pThrownItemHead; pItem != NULL; pItem = pItem->pNext) { /* @0x426ef9 */
        itemThrowUpdate(pItem);                                           /* @0x40f950 @0x426efc */
    }
    for (i = 0; i < g_nPlayerCount; i++) {                                /* @0x426f17 */
        PlayerRecord *pRec = &g_playerRecords[i];
        if (pRec->nCartMode == 0) {                                       /* +0x174 @0x426f17 */
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
        if (pRec->nCartMode == 0) {                                       /* @0x426f80 */
            if (pRec->nChannelsDirty != 0) {                              /* +0x2d8 @0x426f88 */
                syncWalkNodeChannelsToMesh(pRec);                         /* @0x428990 @0x426f95 */
            }
            syncPosNodeChannelsToMesh(pRec);                              /* @0x428840 @0x426f9e */
        } else if (pRec->nChannelsDirty != 0) {                           /* @0x426fa5 */
            syncCartNodeChannelsToMeshes(pRec);                           /* @0x428a70 @0x426faa */
        }
    }
}
