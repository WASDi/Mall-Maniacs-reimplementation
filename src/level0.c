/* level0.c — level-0 (ica, "Småköp") "MCDMAN bonus task" event director.
 *
 * Original functions (verified 2026-08-30 against full disassembly):
 *   levelEventDirector_L0_Init    @0x416db0
 *   levelEventDirector_L0_Cleanup @0x416f70
 *   levelEventDirector_L0         @0x416fd0
 *
 * Per frame the director:
 *  1. bobs the four SIGN_FELIXPO sign sub-meshes on independent sin()
 *     phases (sub 0/2 in x/z via g_dL0SignAngle*4.0 and
 *     sin(g_dL0SubAngleA)*3.0, sub 1 in pitch via sin(g_dL0SubAngleC)*384.0,
 *     sub 3 in pitch via sin(g_dL0SubAngleB)*180.0 — all sceneObjSetSubPos
 *     mode 1 adds);
 *  2. while the bonus item is not active, runs a 9-step anim state machine
 *     after a 10 s idle gate (g_nObjUpdateTime * g_nL0EventTick >= 10000):
 *     flpick1 blink (s1-2), flpick2 (s3-4), throw1 + attach the hidden
 *     BURGER prop to the MCDMAN class mesh slot 8 (s5-6), throw2 + detach
 *     and drop the BURGER onto the counter in four mode-5 z-steps (s7-8),
 *     then spawn a class-0x1f pickup EventObject (sceneObjCtor3 +
 *     objHashRegister) at the BURGER world position — the pickup consumed
 *     by playerAiGrabItem @0x40ea20 via EventObject.nValue1 (s9);
 *  3. blinks the KASSOERSKA cashier anim (cash_sit during play,
 *     s_winner on the results screen) every other frame via the
 *     eventAnim step/apply alternation.
 *
 * Cleanup frees only the anims: the sign/cashier/mascot/BURGER scene
 * objects belong to the round scene teardown (roundTeardown @0x40aa10).
 * The original UB on operator_new failure
 * (NULL deref at [EAX+0x10]) is guarded with the same `if (pNew != NULL)`
 * pattern used by the player_ai.c landed-item path.
 */

#include <stdlib.h>
#include <math.h>

#include "level0.h"
#include "anim.h"
#include "gameplay.h"
#include "obj_event.h"
#include "player.h"
#include "scene.h"
#include "scene_alloc.h"
#include "scene_render.h"
#include "scene_transform.h"

/* --- file constants (verified byte patterns) --- */
static const double g_dblL0SignStep = 0.009;      /* @0x44b600 (bytes 3B DF 4F 8D 97 6E 82 3F) */
static const double g_dblL0OffsetAScale = 4.0;    /* @0x44b5f8 (bytes 00 00 00 00 00 00 10 40) */
static const double g_dblL0SubAngleAStep = 0.011; /* @0x44b5f0 (bytes BA 49 0C 02 2B 87 86 3F) */
static const double g_dblL0OffsetBScale = 3.0;    /* @0x44b5e8 (bytes 00 00 00 00 00 00 08 40) */
static const double g_dblL0SubAngleCStep = 0.01;  /* @0x44b5e0 (bytes 7B 14 AE 47 E1 7A 84 3F) */
static const double g_dblL0Sub1Scale = 384.0;     /* @0x44b5d8 (bytes 00 00 00 00 00 80 78 40) */
static const double g_dblL0SubAngleBStep = 0.008; /* @0x44b5d0 (bytes FC A9 F1 D2 4D 62 80 3F) */
static const double g_dblL0Sub3Scale = 180.0;     /* @0x44b5c8 (bytes 00 00 00 00 00 E0 6C 40) */

/* --- director state (xref-verified private to the three functions) --- */
static SceneNode *g_pL0SignObj;       /* @0x459d70 scenNameToIdEx("SIGN_FELIXPO") node handle */
static int g_nL0EventActive;          /* @0x459d74 1 while the bonus pickup is on the field */
static int g_nL0EventTick;            /* @0x459d78 idle-frame counter / presenter blink clock */
static int g_nL0EventStep;            /* @0x459d7c presenter state machine step (0..9) */
static int g_nL0MoveStep;             /* @0x459d80 throw-landing mode-5 move-step */
static SceneNode *g_pL0McdmanObj;     /* @0x459d84 MCDMAN mascot scenery object */
static SceneNode *g_pL0BurgerObj;     /* @0x459d88 BURGER prop scenery object */
static SceneNode *g_pL0KassoerskaObj; /* @0x459d8c KASSOERSKA cashier scenery object */
static int g_anL0SpawnPos[3];         /* @0x459d90 BURGER world pos readback (mode-2 raw ints) */
static AnmFile *g_pL0StandAnim;       /* @0x459d9c anim\s_stand.anm (mascot idle) */
static AnmFile *g_pL0Flpick1Anim;     /* @0x459da0 anim\s_flpick1.anm */
static AnmFile *g_pL0Flpick2Anim;     /* @0x459da4 anim\s_flpick2.anm */
static AnmFile *g_pL0Throw1Anim;      /* @0x459da8 anim\s_throw1.anm */
static AnmFile *g_pL0Throw2Anim;      /* @0x459dac anim\s_throw2.anm */
static AnmFile *g_pL0CashSitAnim;     /* @0x459db0 anim\cash_sit.anm (cashier idle) */
static AnmFile *g_pL0WinnerAnim;      /* @0x459db4 anim\s_winner.anm (results screen) */
static double g_dL0SignAngle;         /* @0x459d68 sign sub-mesh 0/2 phase */
static double g_dL0SubAngleA;         /* @0x459db8 sign sub-mesh 0/2 x phase */
static double g_dL0OffsetA;           /* @0x459dc0 sin(g_dL0SignAngle)*4.0 */
static double g_dL0SubAngleB;         /* @0x459dc8 sign sub-mesh 3 phase */
static double g_dL0SubAngleC;         /* @0x459dd0 sign sub-mesh 1 phase */
static double g_dL0OffsetB;           /* @0x459de0 sin(g_dL0SubAngleA)*3.0 */
static int g_nL0TrailTick;            /* @0x459de8 cashier blink clock */

/* levelEventDirector_L0_Init @0x416db0 — round-start asset setup. Loads the
 * SIGN_FELIXPO sign handle, allocates the KASSOERSKA cashier (at pitch
 * 16000) and MCDMAN mascot scenery objects plus the hidden BURGER prop, and
 * loads the seven event anims against the mascot/cashier nodes. */
void levelEventDirector_L0_Init(void) /* @0x416db0 */
{
    void *pName;

    g_pL0SignObj = (SceneNode *)(uintptr_t)scenNameToIdEx("SIGN_FELIXPO");   /* @0x431e20 @0x416db5 */
    g_pL0KassoerskaObj = (SceneNode *)sceneNodeAllocChild(NULL, NULL,        /* @0x4319e0 @0x416dcf */
                                                          (void *)0x30d4, NULL,
                                                          (void *)0x2134);
    sceneObjSetPosOrient(g_pL0KassoerskaObj, 0, 16000, 0, 2);                /* @0x4307d0 @0x416de5 */
    pName = (void *)(uintptr_t)scenNameToId("KASSOERSKA");                   /* @0x431ed0 @0x416def */
    g_pL0KassoerskaObj = (SceneNode *)sceneryObjAlloc(g_pL0KassoerskaObj,    /* @0x430200 @0x416e09 */
                                                      0, 0, 0, 0, 0, 0, 0, pName);
    g_pL0CashSitAnim = anmLoadFile("anim\\cash_sit.anm", NULL,               /* @0x433a50 @0x416e1e */
                                   g_pL0KassoerskaObj);
    g_pL0WinnerAnim = anmLoadFile("anim\\s_winner.anm", NULL,                /* @0x433a50 @0x416e36 */
                                  g_pL0KassoerskaObj);
    g_pL0McdmanObj = (SceneNode *)sceneNodeAllocChild(NULL, NULL,            /* @0x4319e0 @0x416e50 */
                                                      (void *)0x1f40, NULL,
                                                      (void *)0xffffad30);
    pName = (void *)(uintptr_t)scenNameToId("MCDMAN");                       /* @0x431ed0 @0x416e5f */
    g_pL0McdmanObj = (SceneNode *)sceneryObjAlloc(g_pL0McdmanObj,            /* @0x430200 @0x416e7a */
                                                  0, 0, 0, 0, 0, 0, 0, pName);
    pName = (void *)(uintptr_t)scenNameToId("BURGER");                       /* @0x431ed0 @0x416e8c */
    g_pL0BurgerObj = (SceneNode *)sceneryObjAlloc(NULL, 0, 0, 0, 0, 0, 0, 0, /* @0x430200 @0x416ea2 */
                                                  pName);
    sceneNodeSetHiddenFlag(g_pL0BurgerObj, 2);                               /* @0x4305c0 @0x416eaf */
    g_pL0StandAnim = anmLoadFile("anim\\s_stand.anm", NULL,                  /* @0x433a50 @0x416ec1 */
                                 g_pL0McdmanObj);
    g_pL0Flpick1Anim = anmLoadFile("anim\\s_flpick1.anm", NULL,              /* @0x433a50 @0x416ed9 */
                                   g_pL0McdmanObj);
    g_pL0Flpick2Anim = anmLoadFile("anim\\s_flpick2.anm", NULL,              /* @0x433a50 @0x416ef4 */
                                   g_pL0McdmanObj);
    g_pL0Throw1Anim = anmLoadFile("anim\\s_throw1.anm", NULL,                /* @0x433a50 @0x416f0b */
                                  g_pL0McdmanObj);
    g_pL0Throw2Anim = anmLoadFile("anim\\s_throw2.anm", NULL,                /* @0x433a50 @0x416f23 */
                                  g_pL0McdmanObj);
    eventAnimStep(g_pL0StandAnim, 1);                                        /* @0x434090 @0x416f36 */
    eventAnimStep(g_pL0CashSitAnim, 1);                                      /* @0x434090 @0x416f43 */
    g_nL0EventActive = 0;                                                    /* @0x416f4b */
    g_nL0EventTick = 0;                                                      /* @0x416f55 */
    g_nL0EventStep = 0;                                                      /* @0x416f5f */
}

/* levelEventDirector_L0_Cleanup @0x416f70 — frees the seven event anims.
 * Only the anims: the sign/cashier/mascot/BURGER scene objects are owned by
 * the round scene teardown. Dispatched by roundTeardown @0x40aa10 (case 0
 * @0x40aace) on g_nLevelIdx == 0. */
void levelEventDirector_L0_Cleanup(void) /* @0x416f70 */
{
    anmFree(g_pL0StandAnim);     /* @0x434050 @0x416f76 */
    anmFree(g_pL0Flpick1Anim);   /* @0x434050 @0x416f82 */
    anmFree(g_pL0Flpick2Anim);   /* @0x434050 @0x416f8e */
    anmFree(g_pL0Throw1Anim);    /* @0x434050 @0x416f99 */
    anmFree(g_pL0Throw2Anim);    /* @0x434050 @0x416fa5 */
    anmFree(g_pL0CashSitAnim);   /* @0x434050 @0x416fb1 */
    anmFree(g_pL0WinnerAnim);    /* @0x434050 @0x416fbc */
}

/* levelEventDirector_L0 @0x416fd0 — per-frame director, dispatched from
 * roundLogicUpdate @0x40c1c8 on g_nLevelIdx == 0. Switch/jump table at
 * 0x4175b8 covers steps 1..9; step 0 falls to the common tick increment.
 * Every switch path increments g_nL0EventTick exactly once (the asm has a
 * direct INC on some paths and the shared INC at 0x4174fc on the rest). */
void levelEventDirector_L0(void) /* @0x416fd0 */
{
    EventObject *pNew;

    if (g_pL0SignObj != NULL) {                                              /* @0x416ff0 */
        SceneNode *pObj = g_pL0SignObj;
        g_dL0SignAngle += g_dblL0SignStep;                                   /* @0x416ff8..0x417006 */
        g_dL0OffsetA = sin(g_dL0SignAngle) * g_dblL0OffsetAScale;            /* @0x41700c..0x417014 */
        g_dL0SubAngleA += g_dblL0SubAngleAStep;                              /* @0x41701a..0x417026 */
        g_dL0OffsetB = sin(g_dL0SubAngleA) * g_dblL0OffsetBScale;            /* @0x41702c..0x417034 */
        sceneObjSetSubPos(pObj, 0, (short)(int)g_dL0OffsetB, 0,              /* @0x430a90 @0x417055 */
                          (short)(int)g_dL0OffsetA, 1);
        g_dL0SubAngleC += g_dblL0SubAngleCStep;                              /* @0x41705a..0x417069 */
        sceneObjSetSubPos(pObj, 1, 0,                                        /* @0x430a90 @0x41708c */
                          (short)(int)(sin(g_dL0SubAngleC) * g_dblL0Sub1Scale),
                          0, 1);
        sceneObjSetSubPos(pObj, 2, (short)(int)(-g_dL0OffsetB), 0,           /* @0x430a90 @0x4170b9 */
                          (short)(int)(-g_dL0OffsetA), 1);
        g_dL0SubAngleB += g_dblL0SubAngleBStep;                              /* @0x4170be..0x4170d0 */
        sceneObjSetSubPos(pObj, 3, 0,                                        /* @0x430a90 @0x4170f4 */
                          (short)(int)(sin(g_dL0SubAngleB) * g_dblL0Sub3Scale),
                          0, 1);
    }

    if (g_nL0EventActive != 0) {                                             /* @0x4170fc */
        /* bonus item on the field: re-arm once it is gone (picked up or
         * otherwise hidden again) */
        if (sceneNodeGetHiddenFlag(g_pL0BurgerObj) != 0) {                   /* @0x4305b0 @0x41750a */
            g_nL0EventActive = 0;                                            /* @0x417516 */
        }
    } else {
        if (g_nObjUpdateTime * g_nL0EventTick >= 10000 &&                    /* @0x417108 */
            g_nL0EventStep == 0) {                                           /* @0x41711b */
            g_nL0EventStep = 1;                                              /* @0x417123 */
        }
        switch (g_nL0EventStep) {                                            /* jump table @0x4175b8 */
        case 1:
            eventAnimReset(g_pL0Flpick1Anim);                                /* @0x434270 @0x41714d */
            eventAnimStep(g_pL0Flpick1Anim, 1);                              /* @0x434090 @0x41715a */
            g_nL0EventStep = 2;                                              /* @0x417162 */
            /* fall through */
        case 2:
            if ((g_nL0EventTick & 1) == 0) {                                 /* @0x41716c */
                eventAnimApply(g_pL0Flpick1Anim, 1);                         /* @0x434290 @0x4171ba */
                break;                                                       /* @0x4171cd */
            }
            if (eventAnimStep(g_pL0Flpick1Anim, 1) != 0) {                   /* @0x434090 @0x41718a */
                g_nL0EventStep = 3;                                          /* @0x41719f */
                break;                                                       /* @0x4171af */
            }
            break;                                                           /* @0x417194 */
        case 3:
            eventAnimReset(g_pL0Flpick2Anim);                                /* @0x434270 @0x4171d9 */
            eventAnimStep(g_pL0Flpick2Anim, 1);                              /* @0x434090 @0x4171e7 */
            g_nL0EventStep = 4;                                              /* @0x4171ef */
            /* fall through */
        case 4:
            if ((g_nL0EventTick & 1) == 0) {                                 /* @0x4171f9 */
                eventAnimApply(g_pL0Flpick2Anim, 1);                         /* @0x434290 @0x417246 */
                break;                                                       /* @0x417259 */
            }
            if (eventAnimStep(g_pL0Flpick2Anim, 1) != 0) {                   /* @0x434090 @0x417215 */
                g_nL0EventStep = 5;                                          /* @0x41722a */
                break;                                                       /* @0x41723a */
            }
            break;                                                           /* @0x41721f */
        case 5:
            eventAnimReset(g_pL0Throw1Anim);                                 /* @0x434270 @0x417264 */
            eventAnimStep(g_pL0Throw1Anim, 1);                               /* @0x434090 @0x417272 */
            sceneObjSetClassMesh((int)g_pL0BurgerObj, g_pL0McdmanObj, 8, 3); /* @0x430db0 @0x417288 */
            sceneObjSetPos(g_pL0BurgerObj, 0, 0x78, 0, 2);                   /* @0x430660 @0x41729a */
            sceneObjSetPosOrient(g_pL0BurgerObj, 0, 0, -16000, 2);           /* @0x4307d0 @0x4172af */
            sceneObjResetFlags(g_pL0BurgerObj, 2);                           /* @0x430620 @0x4172bf */
            g_nL0EventStep = 6;                                              /* @0x4172c7 */
            /* fall through */
        case 6:
            if ((g_nL0EventTick & 1) == 0) {                                 /* @0x4172d1 */
                eventAnimApply(g_pL0Throw1Anim, 1);                          /* @0x434290 @0x41731f */
                break;                                                       /* @0x417332 */
            }
            if (eventAnimStep(g_pL0Throw1Anim, 1) != 0) {                    /* @0x434090 @0x4172ef */
                g_nL0EventStep = 7;                                          /* @0x417304 */
                break;                                                       /* @0x417314 */
            }
            break;                                                           /* @0x4172f9 */
        case 7:
            eventAnimReset(g_pL0Throw2Anim);                                 /* @0x434270 @0x41733e */
            eventAnimStep(g_pL0Throw2Anim, 1);                               /* @0x434090 @0x41734c */
            sceneObjSetClassMesh((int)g_pL0BurgerObj, NULL, 0, 3);           /* @0x430db0 @0x41735b */
            sceneObjSetPos(g_pL0BurgerObj, 0x2008, -0x3f2, -21000, 2);       /* @0x430660 @0x417378 */
            sceneObjSetPosOrient(g_pL0BurgerObj, 0, 0, 0, 2);                /* @0x4307d0 @0x417389 */
            g_nL0MoveStep = 0;                                               /* @0x417391 */
            g_nL0EventStep = 8;                                              /* @0x417393 */
            /* fall through */
        case 8:
            g_nL0MoveStep++;                                                 /* @0x4173a4 */
            if (g_nL0MoveStep < 6) {                                         /* @0x4173ad */
                sceneObjSetPos(g_pL0BurgerObj, 0, 0,                         /* @0x430660 @0x4173c5 */
                               (40 - g_nL0MoveStep) * 4, 5);
            }
            if ((g_nL0EventTick & 1) == 0) {                                 /* @0x4173cd */
                eventAnimApply(g_pL0Throw2Anim, 1);                          /* @0x434290 @0x41741a */
                break;                                                       /* @0x41742d */
            }
            if (eventAnimStep(g_pL0Throw2Anim, 1) != 0) {                    /* @0x434090 @0x4173e9 */
                g_nL0EventStep = 9;                                          /* @0x4173fe */
                break;                                                       /* @0x41740e */
            }
            break;                                                           /* @0x4173f3 */
        case 9:
            g_nL0MoveStep++;                                                 /* @0x417432 */
            if (g_nL0MoveStep < 6) {                                         /* @0x417440 */
                sceneObjSetPos(g_pL0BurgerObj, 0, 0,                         /* @0x430660 @0x417458 */
                               (40 - g_nL0MoveStep) * 4, 5);
                break;                                                       /* @0x41746b */
            }
            g_nL0EventTick = 0;                                              /* @0x41747d */
            g_nL0EventStep = 0;                                              /* @0x417483 */
            g_nL0EventActive = 1;                                            /* @0x417489 */
            /* mode 2 stores the raw int channel coords into the buffer */
            sceneNodeGetPosWorld(g_pL0BurgerObj, (float *)g_anL0SpawnPos, 2); /* @0x430e80 @0x417493 */
            pNew = (EventObject *)malloc(0x50);                              /* operator_new @0x43dd42 @0x41749a */
            if (pNew != NULL) {                                              /* @0x4174ac */
                /* ctor args are the {flX=worldZ, flY=worldX, flHeight=worldY}
                 * ints of the BURGER pos (FILD integer loads @0x4174ae..0x4174cb) */
                sceneObjCtor3(pNew, 0x1f, (float)g_anL0SpawnPos[2],          /* @0x4146a0 @0x4174d0 */
                              (float)g_anL0SpawnPos[0], (float)g_anL0SpawnPos[1]);
                pNew->nValue0 = g_anL0SpawnPos[1];                          /* +0x10 @0x4174e0 */
                pNew->nValue1 = (int)(uintptr_t)g_pL0BurgerObj;             /* +0x14 @0x4174f1 */
                objHashRegister(pNew);                                       /* @0x4148f0 @0x4174f4 */
            }
            break;                                                           /* @0x4174fc (tick++ -> 1) */
        default:
            break;                                                           /* @0x4174fc */
        }
        g_nL0EventTick++;                                                    /* @0x4174fc */
    }

    /* trailing cashier ambient: blink every other frame — even ticks step,
     * odd ticks apply (opposite phase of the presenter anims) */
    g_nL0TrailTick++;                                                        /* @0x41751c */
    if (g_nResultsScreen == 0) {                                             /* @0x417521 */
        if ((g_nL0TrailTick & 1) == 0) {                                     /* @0x417533 */
            eventAnimStep(g_pL0CashSitAnim, 1);                              /* @0x434090 @0x41754a */
            return;                                                          /* @0x417560 */
        }
        eventAnimApply(g_pL0CashSitAnim, 1);                                 /* @0x434290 @0x41759e */
    } else {
        if ((g_nL0TrailTick & 1) == 0) {                                     /* @0x41756a */
            eventAnimStep(g_pL0WinnerAnim, 1);                               /* @0x434090 @0x417580 */
            return;                                                          /* @0x417596 */
        }
        eventAnimApply(g_pL0WinnerAnim, 1);                                  /* @0x434290 @0x41759e */
    }
}
