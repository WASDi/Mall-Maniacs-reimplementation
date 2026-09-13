/* level2.c — level-2 (Harbour Mall) "MCDMAN bonus task" event director.
 *
 * Original functions (verified 2026-09-13 against full disassembly):
 *   levelEventDirector_L2_Init    @0x417dc0
 *   levelEventDirector_L2_Cleanup @0x417fa0
 *   levelEventDirector_L2         @0x418000
 *
 * Per frame the director:
 *  1. bobs the four SIGN_FELIXPO sign sub-meshes on independent sin()
 *     phases (sub 0/2 in x/z via g_dL2SignAngle*4.0 and
 *     sin(g_dL2SubAngleA)*3.0, sub 1 in pitch via sin(g_dL2SubAngleC)*384.0,
 *     sub 3 in pitch via sin(g_dL2SubAngleB)*180.0 — all sceneObjSetSubPos
 *     mode 1 adds);
 *  2. while the bonus item is not active, runs a 9-step anim state machine
 *     after a 10 s idle gate (g_nObjUpdateTime * g_nL2EventTick >= 10000):
 *     flpick1 blink (s1-2), flpick2 (s3-4), throw1 + attach the hidden
 *     BURGER prop to the MCDMAN class mesh slot 8 (s5-6), throw2 + detach
 *     and drop the BURGER at (0x81b0,-1510,800) sliding in four mode-5
 *     x-steps (s7-8), then spawn a class-0x1f pickup EventObject
 *     (sceneObjCtor3 + objHashRegister) at the BURGER world position —
 *     the pickup consumed by playerAiGrabItem @0x40ea20 via
 *     EventObject.field_14 (s9);
 *  3. blinks the KASSOERSKA cashier anim (cash_sit during play,
 *     s_winner on the results screen) every other frame via the
 *     eventAnim step/apply alternation;
 *  4. beeps sfx bank 0 idx 0x23 (vol 0xffff, flags 0x400) once while the
 *     local player stands within 2500 of the "gong" cash-zone object
 *     (@0x4500cc, +0x38/+0x3c origin), latched by g_bL2CashSfxLatched.
 *
 * Cleanup frees only the anims: the sign/cashier/mascot/BURGER scene
 * objects belong to the round scene teardown (roundTeardown @0x40aa10).
 * The original UB on operator_new failure (NULL deref at [EAX+0x10]) is
 * guarded with the same `if (pNew != NULL)` pattern used by the
 * player_ai.c landed-item path.
 */

#include <stdlib.h>
#include <math.h>

#include "level2.h"
#include "anim.h"
#include "gameplay.h"
#include "obj_event.h"
#include "player.h"
#include "player_ai.h"
#include "scene.h"
#include "scene_alloc.h"
#include "scene_render.h"
#include "scene_transform.h"
#include "sound.h"

/* --- file constants (same byte patterns as L0 @0x44b5c8..0x44b600) --- */
static const double g_dblL2SignStep = 0.009;      /* @0x44b600 */
static const double g_dblL2OffsetAScale = 4.0;    /* @0x44b5f8 */
static const double g_dblL2SubAngleAStep = 0.011; /* @0x44b5f0 */
static const double g_dblL2OffsetBScale = 3.0;    /* @0x44b5e8 */
static const double g_dblL2SubAngleCStep = 0.01;  /* @0x44b5e0 */
static const double g_dblL2Sub1Scale = 384.0;     /* @0x44b5d8 */
static const double g_dblL2SubAngleBStep = 0.008; /* @0x44b5d0 */
static const double g_dblL2Sub3Scale = 180.0;     /* @0x44b5c8 */
static const double g_dblL2CashRadius = 2500.0;   /* @0x44b608 */
static const int kL2CashZoneId = 0x676e6f67;      /* *(int *)"gong" @0x4500cc */

/* --- director state (xref-verified private to the three functions) --- */
static double g_dL2SubAngleA;           /* @0x459e38 sign sub-mesh 0/2 x phase */
static SceneNode *g_pL2SignObj;         /* @0x459e40 scenNameToIdEx("SIGN_FELIXPO") node handle */
static int g_nL2EventActive;            /* @0x459e44 1 while the bonus pickup is on the field */
static int g_nL2EventTick;              /* @0x459e48 idle-frame counter / presenter blink clock */
static int g_nL2EventStep;              /* @0x459e4c presenter state machine step (0..9) */
static int g_nL2MoveStep;               /* @0x459e50 throw-landing mode-5 move-step */
static SceneNode *g_pL2McdmanObj;       /* @0x459e54 MCDMAN mascot scenery object */
static SceneNode *g_pL2BurgerObj;       /* @0x459e58 BURGER prop scenery object */
static SceneNode *g_pL2KassoerskaObj;   /* @0x459e5c KASSOERSKA cashier scenery object */
static int g_anL2SpawnPos[3];           /* @0x459e60 BURGER world pos readback (mode-2 raw ints) */
static AnmFile *g_pL2StandAnim;         /* @0x459e6c anim\s_stand.anm (mascot idle) */
static AnmFile *g_pL2Flpick1Anim;       /* @0x459e70 anim\s_flpick1.anm */
static AnmFile *g_pL2Flpick2Anim;       /* @0x459e74 anim\s_flpick2.anm */
static AnmFile *g_pL2Throw1Anim;        /* @0x459e78 anim\s_throw1.anm */
static AnmFile *g_pL2Throw2Anim;        /* @0x459e7c anim\s_throw2.anm */
static AnmFile *g_pL2CashSitAnim;       /* @0x459e80 anim\cash_sit.anm (cashier idle) */
static AnmFile *g_pL2WinnerAnim;        /* @0x459e84 anim\s_winner.anm (results screen) */
static double g_dL2OffsetA;             /* @0x459e88 sin(g_dL2SignAngle)*4.0 */
static double g_dL2SubAngleC;           /* @0x459e98 sign sub-mesh 1 phase */
static double g_dL2OffsetB;             /* @0x459ea0 sin(g_dL2SubAngleA)*3.0 */
static double g_dL2SubAngleB;           /* @0x459ea8 sign sub-mesh 3 phase */
static double g_dL2SignAngle;           /* @0x459eb0 sign sub-mesh 0/2 phase */
static int g_nL2TrailTick;              /* @0x459eb8 cashier blink clock */
static char g_bL2CashSfxLatched;        /* @0x459ebc cash-zone beep latch */

/* levelEventDirector_L2_Init @0x417dc0 — round-start asset setup. Loads the
 * SIGN_FELIXPO sign handle, allocates the KASSOERSKA cashier (at pitch
 * 16000) and MCDMAN mascot scenery objects plus the hidden BURGER prop, and
 * loads the seven event anims against the mascot/cashier nodes. */
void levelEventDirector_L2_Init(void) /* @0x417dc0 */
{
    void *pName;

    g_pL2SignObj = (SceneNode *)(uintptr_t)scenNameToIdEx("SIGN_FELIXPO");   /* @0x431e20 @0x417dc5 */
    g_pL2KassoerskaObj = (SceneNode *)sceneNodeAllocChild(NULL, NULL,        /* @0x4319e0 @0x417de2 */
                                                          (void *)0xffffd6fc, (void *)0xfffffe0c,
                                                          (void *)0xffffafec);
    sceneObjSetPosOrient(g_pL2KassoerskaObj, 0, 16000, 0, 2);                /* @0x4307d0 @0x417df8 */
    pName = (void *)(uintptr_t)scenNameToId("KASSOERSKA");                   /* @0x431ed0 @0x417e02 */
    g_pL2KassoerskaObj = (SceneNode *)sceneryObjAlloc(g_pL2KassoerskaObj,    /* @0x430200 @0x417e1c */
                                                      0, 0, 0, 0, 0, 0, 0, pName);
    g_pL2CashSitAnim = anmLoadFile("anim\\cash_sit.anm", NULL,               /* @0x433a50 @0x417e31 */
                                   g_pL2KassoerskaObj);
    g_pL2WinnerAnim = anmLoadFile("anim\\s_winner.anm", NULL,                /* @0x433a50 @0x417e49 */
                                  g_pL2KassoerskaObj);
    g_pL2McdmanObj = (SceneNode *)sceneNodeAllocChild(NULL, NULL,            /* @0x4319e0 @0x417e66 */
                                                      (void *)0x81b0, (void *)0xfffffe0c,
                                                      (void *)0x3e8);
    sceneObjSetPosOrient(g_pL2McdmanObj, 0, -16000, 0, 2);                   /* @0x4307d0 @0x417e7c */
    pName = (void *)(uintptr_t)scenNameToId("MCDMAN");                       /* @0x431ed0 @0x417e89 */
    g_pL2McdmanObj = (SceneNode *)sceneryObjAlloc(g_pL2McdmanObj,            /* @0x430200 @0x417ea4 */
                                                  0, 0, 0, 0, 0, 0, 0, pName);
    pName = (void *)(uintptr_t)scenNameToId("BURGER");                       /* @0x431ed0 @0x417eae */
    g_pL2BurgerObj = (SceneNode *)sceneryObjAlloc(NULL, 0, 0, 0, 0, 0, 0, 0, /* @0x430200 @0x417ec9 */
                                                  pName);
    sceneNodeSetHiddenFlag(g_pL2BurgerObj, 2);                               /* @0x4305c0 @0x417ed9 */
    g_pL2StandAnim = anmLoadFile("anim\\s_stand.anm", NULL,                  /* @0x433a50 @0x417eeb */
                                 g_pL2McdmanObj);
    g_pL2Flpick1Anim = anmLoadFile("anim\\s_flpick1.anm", NULL,              /* @0x433a50 @0x417f03 */
                                   g_pL2McdmanObj);
    g_pL2Flpick2Anim = anmLoadFile("anim\\s_flpick2.anm", NULL,              /* @0x433a50 @0x417f1b */
                                   g_pL2McdmanObj);
    g_pL2Throw1Anim = anmLoadFile("anim\\s_throw1.anm", NULL,                /* @0x433a50 @0x417f32 */
                                  g_pL2McdmanObj);
    g_pL2Throw2Anim = anmLoadFile("anim\\s_throw2.anm", NULL,                /* @0x433a50 @0x417f4a */
                                  g_pL2McdmanObj);
    eventAnimStep(g_pL2StandAnim, 1);                                        /* @0x434090 @0x417f60 */
    eventAnimStep(g_pL2CashSitAnim, 1);                                      /* @0x434090 @0x417f6d */
    g_nL2EventActive = 0;                                                    /* @0x417f75 */
    g_nL2EventTick = 0;                                                      /* @0x417f7f */
    g_nL2EventStep = 0;                                                      /* @0x417f89 */
}

/* levelEventDirector_L2_Cleanup @0x417fa0 — frees the seven event anims.
 * Only the anims: the sign/cashier/mascot/BURGER scene objects are owned by
 * the round scene teardown. Dispatched by roundTeardown @0x40aa10 (case 2
 * @0x40aadc) on g_nLevelIdx == 2. */
void levelEventDirector_L2_Cleanup(void) /* @0x417fa0 */
{
    anmFree(g_pL2StandAnim);     /* @0x434050 @0x417fa6 */
    anmFree(g_pL2Flpick1Anim);   /* @0x434050 @0x417fb2 */
    anmFree(g_pL2Flpick2Anim);   /* @0x434050 @0x417fbe */
    anmFree(g_pL2Throw1Anim);    /* @0x434050 @0x417fc9 */
    anmFree(g_pL2Throw2Anim);    /* @0x434050 @0x417fd5 */
    anmFree(g_pL2CashSitAnim);   /* @0x434050 @0x417fe1 */
    anmFree(g_pL2WinnerAnim);    /* @0x434050 @0x417fec */
}

/* levelEventDirector_L2 @0x418000 — per-frame director, dispatched from
 * roundLogicUpdate @0x40c1c8 on g_nLevelIdx == 2. Switch/jump table at
 * 0x418698 covers steps 1..9; step 0 falls to the common tick increment.
 * Every switch path increments g_nL2EventTick exactly once. */
void levelEventDirector_L2(void) /* @0x418000 */
{
    EventObject *pNew;
    EventObject *pCashZone;
    float avPos[2];

    if (g_pL2SignObj != NULL) {                                              /* @0x418022 */
        SceneNode *pObj = g_pL2SignObj;
        g_dL2SignAngle += g_dblL2SignStep;                                   /* @0x41802a..0x418038 */
        g_dL2OffsetA = sin(g_dL2SignAngle) * g_dblL2OffsetAScale;            /* @0x41803e..0x418046 */
        g_dL2SubAngleA += g_dblL2SubAngleAStep;                              /* @0x41804c..0x418058 */
        g_dL2OffsetB = sin(g_dL2SubAngleA) * g_dblL2OffsetBScale;            /* @0x41805e..0x418066 */
        sceneObjSetSubPos(pObj, 0, (short)(int)g_dL2OffsetB, 0,              /* @0x430a90 @0x418087 */
                          (short)(int)g_dL2OffsetA, 1);
        g_dL2SubAngleC += g_dblL2SubAngleCStep;                              /* @0x41808c..0x41809b */
        sceneObjSetSubPos(pObj, 1, 0,                                        /* @0x430a90 @0x4180be */
                          (short)(int)(sin(g_dL2SubAngleC) * g_dblL2Sub1Scale),
                          0, 1);
        sceneObjSetSubPos(pObj, 2, (short)(int)(-g_dL2OffsetB), 0,           /* @0x430a90 @0x4180eb */
                          (short)(int)(-g_dL2OffsetA), 1);
        g_dL2SubAngleB += g_dblL2SubAngleBStep;                              /* @0x4180f0..0x418102 */
        sceneObjSetSubPos(pObj, 3, 0,                                        /* @0x430a90 @0x418126 */
                          (short)(int)(sin(g_dL2SubAngleB) * g_dblL2Sub3Scale),
                          0, 1);
    }

    if (g_nL2EventActive != 0) {                                             /* @0x41812e */
        /* bonus item on the field: re-arm once it is gone (picked up or
         * otherwise hidden again) */
        if (sceneNodeGetHiddenFlag(g_pL2BurgerObj) != 0) {                   /* @0x4305b0 @0x418538 */
            g_nL2EventActive = 0;                                            /* @0x418544 */
        }
    } else {
        if (g_nObjUpdateTime * g_nL2EventTick >= 10000 &&                    /* @0x41813f */
            g_nL2EventStep == 0) {                                           /* @0x41814d */
            g_nL2EventStep = 1;                                              /* @0x418155 */
        }
        switch (g_nL2EventStep) {                                            /* jump table @0x418698 */
        case 1:
            eventAnimReset(g_pL2Flpick1Anim);                                /* @0x434270 @0x41817f */
            eventAnimStep(g_pL2Flpick1Anim, 1);                              /* @0x434090 @0x41818c */
            g_nL2EventStep = 2;                                              /* @0x418194 */
            /* fall through */
        case 2:
            if ((g_nL2EventTick & 1) == 0) {                                 /* @0x4181a4 */
                eventAnimApply(g_pL2Flpick1Anim, 1);                         /* @0x434290 @0x4181ec */
                break;                                                       /* @0x4181ff */
            }
            if (eventAnimStep(g_pL2Flpick1Anim, 1) != 0) {                   /* @0x434090 @0x4181bc */
                g_nL2EventStep = 3;                                          /* @0x4181d1 */
                break;                                                       /* @0x4181e1 */
            }
            break;                                                           /* @0x4181c6 */
        case 3:
            eventAnimReset(g_pL2Flpick2Anim);                                /* @0x434270 @0x41820b */
            eventAnimStep(g_pL2Flpick2Anim, 1);                              /* @0x434090 @0x418219 */
            g_nL2EventStep = 4;                                              /* @0x418221 */
            /* fall through */
        case 4:
            if ((g_nL2EventTick & 1) == 0) {                                 /* @0x41822b */
                eventAnimApply(g_pL2Flpick2Anim, 1);                         /* @0x434290 @0x418278 */
                break;                                                       /* @0x41828b */
            }
            if (eventAnimStep(g_pL2Flpick2Anim, 1) != 0) {                   /* @0x434090 @0x418247 */
                g_nL2EventStep = 5;                                          /* @0x41825c */
                break;                                                       /* @0x41826c */
            }
            break;                                                           /* @0x418251 */
        case 5:
            eventAnimReset(g_pL2Throw1Anim);                                 /* @0x434270 @0x418296 */
            eventAnimStep(g_pL2Throw1Anim, 1);                               /* @0x434090 @0x4182a4 */
            sceneObjSetClassMesh((int)g_pL2BurgerObj, g_pL2McdmanObj, 8, 3); /* @0x430db0 @0x4182ba */
            sceneObjSetPos(g_pL2BurgerObj, 0, 0x78, 0, 2);                   /* @0x430660 @0x4182cc */
            sceneObjSetPosOrient(g_pL2BurgerObj, 0, 0, -16000, 2);           /* @0x4307d0 @0x4182e1 */
            sceneObjResetFlags(g_pL2BurgerObj, 2);                           /* @0x430620 @0x4182f1 */
            g_nL2EventStep = 6;                                              /* @0x4182f9 */
            /* fall through */
        case 6:
            if ((g_nL2EventTick & 1) == 0) {                                 /* @0x418303 */
                eventAnimApply(g_pL2Throw1Anim, 1);                          /* @0x434290 @0x41834b */
                break;                                                       /* @0x418364 */
            }
            if (eventAnimStep(g_pL2Throw1Anim, 1) != 0) {                    /* @0x434090 @0x418321 */
                g_nL2EventStep = 7;                                          /* @0x418336 */
                break;                                                       /* @0x418346 */
            }
            break;                                                           /* @0x41832b */
        case 7:
            eventAnimReset(g_pL2Throw2Anim);                                 /* @0x434270 @0x418370 */
            eventAnimStep(g_pL2Throw2Anim, 1);                               /* @0x434090 @0x41837e */
            sceneObjSetClassMesh((int)g_pL2BurgerObj, NULL, 0, 3);           /* @0x430db0 @0x41838d */
            sceneObjSetPos(g_pL2BurgerObj, 0x81b0, -0x5e6, 800, 2);          /* @0x430660 @0x4183aa */
            sceneObjSetPosOrient(g_pL2BurgerObj, 0, 0, 0, 2);                /* @0x4307d0 @0x4183bb */
            g_nL2MoveStep = 0;                                               /* @0x4183c3 */
            g_nL2EventStep = 8;                                              /* @0x4183c5 */
            /* fall through */
        case 8:
            g_nL2MoveStep++;                                                 /* @0x4183d6 */
            if (g_nL2MoveStep < 6) {                                         /* @0x4183d7 */
                sceneObjSetPos(g_pL2BurgerObj, g_nL2MoveStep * 4 - 0xa0, 0,  /* @0x430660 @0x4183f4 */
                               0, 5);
            }
            if ((g_nL2EventTick & 1) == 0) {                                 /* @0x418402 */
                eventAnimApply(g_pL2Throw2Anim, 1);                          /* @0x434290 @0x41844a */
                break;                                                       /* @0x41845d */
            }
            if (eventAnimStep(g_pL2Throw2Anim, 1) != 0) {                    /* @0x434090 @0x418419 */
                g_nL2EventStep = 9;                                          /* @0x41842e */
                break;                                                       /* @0x41843e */
            }
            break;                                                           /* @0x418423 */
        case 9:
            g_nL2MoveStep++;                                                 /* @0x418462 */
            if (g_nL2MoveStep < 6) {                                         /* @0x418468 */
                sceneObjSetPos(g_pL2BurgerObj, g_nL2MoveStep * 4 - 0xa0, 0,  /* @0x430660 @0x418484 */
                               0, 5);
                break;                                                       /* @0x418497 */
            }
            g_nL2EventTick = 0;                                              /* @0x4184aa */
            g_nL2EventStep = 0;                                              /* @0x4184b0 */
            g_nL2EventActive = 1;                                            /* @0x4184b6 */
            /* mode 2 stores the raw int channel coords into the buffer */
            sceneNodeGetPosWorld(g_pL2BurgerObj, (float *)g_anL2SpawnPos, 2); /* @0x430e80 @0x4184c0 */
            pNew = (EventObject *)malloc(0x50);                              /* operator_new @0x43dd42 @0x4184c7 */
            if (pNew != NULL) {                                              /* @0x4184d3 */
                /* ctor args are the {flX=worldZ, flY=worldX, flHeight=worldY}
                 * ints of the BURGER pos (FILD integer loads @0x4184db..0x4184f6) */
                sceneObjCtor3(pNew, 0x1f, (float)g_anL2SpawnPos[2],          /* @0x4146a0 @0x4184fd */
                              (float)g_anL2SpawnPos[0], (float)g_anL2SpawnPos[1]);
                pNew->field_10 = g_anL2SpawnPos[1];                          /* +0x10 @0x41850d */
                pNew->field_14 = (int)(uintptr_t)g_pL2BurgerObj;             /* +0x14 @0x41851e */
                objHashRegister(pNew);                                       /* @0x4148f0 @0x418521 */
            }
            break;                                                           /* @0x418529 (tick++ -> 1) */
        default:
            break;                                                           /* @0x418529 */
        }
        g_nL2EventTick++;                                                    /* @0x418529 */
    }

    /* trailing cashier ambient: blink every other frame — even ticks step,
     * odd ticks apply (opposite phase of the presenter anims) */
    g_nL2TrailTick++;                                                        /* @0x41854f */
    if (g_nResultsScreen == 0) {                                             /* @0x418555 */
        if ((g_nL2TrailTick & 1) == 0) {                                     /* @0x41855f */
            eventAnimStep(g_pL2CashSitAnim, 1);                              /* @0x434090 @0x418575 */
        } else {
            eventAnimApply(g_pL2CashSitAnim, 1);                             /* @0x434290 @0x4185a9 */
        }
    } else {
        if ((g_nL2TrailTick & 1) == 0) {                                     /* @0x418585 */
            eventAnimStep(g_pL2WinnerAnim, 1);                               /* @0x434090 @0x41859c */
        } else {
            eventAnimApply(g_pL2WinnerAnim, 1);                              /* @0x434290 @0x4185a9 */
        }
    }

    /* trailing cash-zone proximity beep: while the local player is within
     * 2500 of the "gong" zone origin on both axes, beep once (latched);
     * leaving the radius clears the latch (@0x4185ae..0x418696). */
    pCashZone = objFindById(kL2CashZoneId, 0);                               /* @0x414a90 @0x4185b9 */
    if (pCashZone != NULL) {                                                 /* @0x4185c3 */
        sceneObjGetPosXZ(&g_playerRecords[g_nLocalPlayerIdx], avPos);        /* @0x4099d0 @0x4185ef */
        if (fabsf(pCashZone->flOriginX - avPos[0]) < (float)g_dblL2CashRadius && /* @0x4185f4 */
            (sceneObjGetPosXZ(&g_playerRecords[g_nLocalPlayerIdx], avPos),   /* @0x4099d0 @0x41862d */
             fabsf(pCashZone->flOriginZ - avPos[1]) < (float)g_dblL2CashRadius)) { /* @0x418632 */
            if (g_bL2CashSfxLatched == 0) {                                  /* @0x418648 */
                sndPlaySfx(0, 1, 0x23, 0xffff, 0, 0x400);                    /* @0x437cf0 @0x418660 */
            }
            g_bL2CashSfxLatched = 1;                                         /* @0x418669 */
            return;
        }
    }
    g_bL2CashSfxLatched = 0;                                                 /* @0x418684 */
}
