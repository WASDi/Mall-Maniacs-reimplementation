/* level1.c — level-1 (Woodland Mall) "MCDMAN bonus task" event director.
 *
 * Original functions (verified 2026-09-05 against full disassembly):
 *   levelEventDirector_L1_Init    @0x4175e0
 *   levelEventDirector_L1_Cleanup @0x417860
 *   levelEventDirector_L1         @0x4178c0
 *
 * Same presenter family as L0 (see level0.c) minus the SIGN_FELIXPO sign
 * bobbing:
 *  1. while the bonus item is not active, runs a 9-step anim state machine
 *     after a 10 s idle gate (g_nObjUpdateTime * g_nL1EventTick >= 10000):
 *     flpick1 blink (s1-2), flpick2 (s3-4), throw1 + attach the hidden
 *     BURGER prop to the MCDMAN class mesh slot 8 (s5-6), throw2 + detach
 *     and drop the BURGER in four mode-5 z-steps (s7-8), then spawn a
 *     class-0x1f pickup EventObject (sceneObjCtor3 + objHashRegister) at
 *     the BURGER world position — the pickup consumed by playerAiGrabItem
 *     @0x40ea20 via EventObject.field_14 (s9);
 *  2. blinks the KASSOERSKA cashier anim (cash_sit during play,
 *     s_winner on the results screen) every other frame via the
 *     eventAnim step/apply alternation.
 *
 * Init additionally registers three positional sfx emitters
 * (sndPlaySfx3D @0x42bcd0, bank 1 idx 0x20/0x1f/0x1f, vol 65000,
 * sndId 0xff, flags 0x11) at (-50000,-5000,3000), (-3700,-4000,16500)
 * and (6400,-2000,-16000).
 *
 * Cleanup frees only the anims: the cashier/mascot/BURGER scene objects
 * belong to the round scene teardown (roundTeardown @0x40aa10). The
 * original UB on operator_new failure (NULL deref at [EAX+0x10]) is
 * guarded with the same `if (pNew != NULL)` pattern used by the
 * player_ai.c landed-item path.
 */

#include <stdlib.h>

#include "level1.h"
#include "anim.h"
#include "gameplay.h"
#include "obj_event.h"
#include "player.h"
#include "scene.h"
#include "scene_alloc.h"
#include "scene_render.h"
#include "scene_transform.h"
#include "sound.h"

/* --- director state (xref-verified private to the three functions) --- */
static int g_nL1EventActive;            /* @0x459df0 1 while the bonus pickup is on the field */
static int g_nL1EventTick;              /* @0x459df4 idle-frame counter */
static int g_nL1EventStep;              /* @0x459df8 presenter state machine step (0..9) */
static int g_nL1MoveStep;               /* @0x459dfc throw-landing mode-5 move-step */
static SceneNode *g_pL1McdmanObj;       /* @0x459e00 MCDMAN mascot scenery object (Ghidra g_pL1AttachMesh) */
static SceneNode *g_pL1BurgerObj;       /* @0x459e04 BURGER prop scenery object (Ghidra g_nL1EventObj) */
static SceneNode *g_pL1KassoerskaObj;   /* @0x459e08 KASSOERSKA cashier scenery object */
static int g_anL1SpawnPos[3];           /* @0x459e0c BURGER world pos readback (mode-2 raw ints) */
static AnmFile *g_pL1StandAnim;         /* @0x459e18 anim\s_stand.anm (mascot idle) */
static AnmFile *g_pL1Flpick1Anim;       /* @0x459e1c anim\s_flpick1.anm (Ghidra g_apL1EventAnims) */
static AnmFile *g_pL1Flpick2Anim;       /* @0x459e20 anim\s_flpick2.anm */
static AnmFile *g_pL1Throw1Anim;        /* @0x459e24 anim\s_throw1.anm */
static AnmFile *g_pL1Throw2Anim;        /* @0x459e28 anim\s_throw2.anm */
static AnmFile *g_pL1CashSitAnim;       /* @0x459e2c anim\cash_sit.anm (cashier idle) */
static AnmFile *g_pL1WinnerAnim;        /* @0x459e30 anim\s_winner.anm (results screen) */
static int g_nL1TrailTick;              /* @0x459e34 cashier blink clock (Ghidra g_nL1ResultAnimTick) */

/* levelEventDirector_L1_Init @0x4175e0 — round-start asset setup. Allocates
 * the KASSOERSKA cashier (at pitch 16000) and MCDMAN mascot scenery
 * objects plus the hidden BURGER prop, loads the seven event anims
 * against the mascot/cashier nodes, and registers the three positional
 * sfx emitters. */
void levelEventDirector_L1_Init(void) /* @0x4175e0 */
{
    void *pName;
    SndEmitter *pEmitter;

    g_pL1KassoerskaObj = (SceneNode *)sceneNodeAllocChild(NULL, NULL,      /* @0x4319e0 @0x417606 */
                                                          (void *)0xffffdecc, NULL,
                                                          (void *)0x1194);
    sceneObjSetPosOrient(g_pL1KassoerskaObj, 0, 16000, 0, 2);              /* @0x4307d0 @0x41761a */
    pName = (void *)(uintptr_t)scenNameToId("KASSOERSKA");                 /* @0x431ed0 @0x417624 */
    g_pL1KassoerskaObj = (SceneNode *)sceneryObjAlloc(g_pL1KassoerskaObj,  /* @0x430200 @0x417637 */
                                                      0, 0, 0, 0, 0, 0, 0, pName);
    g_pL1CashSitAnim = anmLoadFile("anim\\cash_sit.anm", NULL,             /* @0x433a50 @0x41764b */
                                   g_pL1KassoerskaObj);
    g_pL1WinnerAnim = anmLoadFile("anim\\s_winner.anm", NULL,              /* @0x433a50 @0x417662 */
                                  g_pL1KassoerskaObj);
    g_pL1McdmanObj = (SceneNode *)sceneNodeAllocChild(NULL, NULL,          /* @0x4319e0 @0x41767d */
                                                      (void *)0xffff7748,
                                                      (void *)0xfffff448,
                                                      (void *)0xfffffed4);
    pName = (void *)(uintptr_t)scenNameToId("MCDMAN");                     /* @0x431ed0 @0x41768c */
    g_pL1McdmanObj = (SceneNode *)sceneryObjAlloc(g_pL1McdmanObj,          /* @0x430200 @0x4176a0 */
                                                  0, 0, 0, 0, 0, 0, 0, pName);
    pName = (void *)(uintptr_t)scenNameToId("BURGER");                     /* @0x431ed0 @0x4176b2 */
    g_pL1BurgerObj = (SceneNode *)sceneryObjAlloc(NULL, 0, 0, 0, 0, 0, 0, 0, /* @0x430200 @0x4176c0 */
                                                  pName);
    sceneNodeSetHiddenFlag(g_pL1BurgerObj, 2);                             /* @0x4305c0 @0x4176cd */
    g_pL1StandAnim = anmLoadFile("anim\\s_stand.anm", NULL,                /* @0x433a50 @0x4176de */
                                 g_pL1McdmanObj);
    g_pL1Flpick1Anim = anmLoadFile("anim\\s_flpick1.anm", NULL,            /* @0x433a50 @0x4176f5 */
                                   g_pL1McdmanObj);
    g_pL1Flpick2Anim = anmLoadFile("anim\\s_flpick2.anm", NULL,            /* @0x433a50 @0x41770f */
                                   g_pL1McdmanObj);
    g_pL1Throw1Anim = anmLoadFile("anim\\s_throw1.anm", NULL,              /* @0x433a50 @0x417725 */
                                  g_pL1McdmanObj);
    g_pL1Throw2Anim = anmLoadFile("anim\\s_throw2.anm", NULL,              /* @0x433a50 @0x41773c */
                                  g_pL1McdmanObj);
    eventAnimStep(g_pL1StandAnim, 1);                                      /* @0x434090 @0x41774f */
    eventAnimStep(g_pL1CashSitAnim, 1);                                    /* @0x434090 @0x41775c */
    g_nL1EventActive = 0;                                                  /* @0x417763 */
    g_nL1EventTick = 0;                                                    /* @0x417769 */
    g_nL1EventStep = 0;                                                    /* @0x41776f */
    pEmitter = (SndEmitter *)malloc(0x1c);                                 /* operator_new @0x43dd42 @0x417775 */
    if (pEmitter != NULL) {
        sndPlaySfx3D(pEmitter, 1, 0x20, 65000, 0xff, NULL, 0,              /* @0x42bcd0 @0x4177ac */
                     -50000, -5000, 3000, 0x11);
    }
    pEmitter = (SndEmitter *)malloc(0x1c);                                 /* operator_new @0x43dd42 @0x4177bb */
    if (pEmitter != NULL) {
        sndPlaySfx3D(pEmitter, 1, 0x1f, 65000, 0xff, NULL, 0,              /* @0x42bcd0 @0x4177f6 */
                     -3700, -4000, 0x4074, 0x11);
    }
    pEmitter = (SndEmitter *)malloc(0x1c);                                 /* operator_new @0x43dd42 @0x417805 */
    if (pEmitter != NULL) {
        sndPlaySfx3D(pEmitter, 1, 0x1f, 65000, 0xff, NULL, 0,              /* @0x42bcd0 @0x417840 */
                     0x1900, -2000, -16000, 0x11);
    }
}

/* levelEventDirector_L1_Cleanup @0x417860 — frees the seven event anims.
 * Only the anims: the cashier/mascot/BURGER scene objects are owned by
 * the round scene teardown. Dispatched by roundTeardown @0x40aa10 on
 * g_nLevelIdx == 1. */
void levelEventDirector_L1_Cleanup(void) /* @0x417860 */
{
    anmFree(g_pL1StandAnim);     /* @0x434050 @0x417866 */
    anmFree(g_pL1Flpick1Anim);   /* @0x434050 @0x417872 */
    anmFree(g_pL1Flpick2Anim);   /* @0x434050 @0x41787e */
    anmFree(g_pL1Throw1Anim);    /* @0x434050 @0x417889 */
    anmFree(g_pL1Throw2Anim);    /* @0x434050 @0x417895 */
    anmFree(g_pL1CashSitAnim);   /* @0x434050 @0x4178a1 */
    anmFree(g_pL1WinnerAnim);    /* @0x434050 @0x4178ac */
}

/* levelEventDirector_L1 @0x4178c0 — per-frame director, dispatched from
 * roundLogicUpdate @0x40c1c8 on g_nLevelIdx == 1. Switch/jump table at
 * 0x417d90 covers steps 1..9; step 0 falls to the common tick increment.
 * Every switch path increments g_nL1EventTick exactly once. */
void levelEventDirector_L1(void) /* @0x4178c0 */
{
    EventObject *pNew;

    if (g_nL1EventActive != 0) {                                           /* @0x4178de */
        /* bonus item on the field: re-arm once it is gone (picked up or
         * otherwise hidden again) */
        if (sceneNodeGetHiddenFlag(g_pL1BurgerObj) != 0) {                 /* @0x4305b0 @0x417ce4 */
            g_nL1EventActive = 0;                                          /* @0x417cf0 */
        }
    } else {
        if (g_nObjUpdateTime * g_nL1EventTick >= 10000 &&                  /* @0x4178eb */
            g_nL1EventStep == 0) {                                         /* @0x4178f9 */
            g_nL1EventStep = 1;                                            /* @0x417901 */
        }
        switch (g_nL1EventStep) {                                          /* jump table @0x417d90 */
        case 1:
            eventAnimReset(g_pL1Flpick1Anim);                              /* @0x434270 @0x41792b */
            eventAnimStep(g_pL1Flpick1Anim, 1);                            /* @0x434090 @0x417938 */
            g_nL1EventStep = 2;                                            /* @0x417940 */
            /* fall through */
        case 2:
            if ((g_nL1EventTick & 1) == 0) {                               /* @0x41794a */
                eventAnimApply(g_pL1Flpick1Anim, 1);                       /* @0x434290 @0x417998 */
                break;                                                     /* @0x4179a5 */
            }
            if (eventAnimStep(g_pL1Flpick1Anim, 1) != 0) {                 /* @0x434090 @0x417968 */
                g_nL1EventStep = 3;                                        /* @0x41797d */
                break;                                                     /* @0x41798d */
            }
            break;                                                         /* @0x417972 */
        case 3:
            eventAnimReset(g_pL1Flpick2Anim);                              /* @0x434270 @0x4179b7 */
            eventAnimStep(g_pL1Flpick2Anim, 1);                            /* @0x434090 @0x4179c5 */
            g_nL1EventStep = 4;                                            /* @0x4179cd */
            /* fall through */
        case 4:
            if ((g_nL1EventTick & 1) == 0) {                               /* @0x4179d7 */
                eventAnimApply(g_pL1Flpick2Anim, 1);                       /* @0x434290 @0x417a24 */
                break;                                                     /* @0x417a31 */
            }
            if (eventAnimStep(g_pL1Flpick2Anim, 1) != 0) {                 /* @0x434090 @0x4179f3 */
                g_nL1EventStep = 5;                                        /* @0x417a08 */
                break;                                                     /* @0x417a18 */
            }
            break;                                                         /* @0x4179fd */
        case 5:
            eventAnimReset(g_pL1Throw1Anim);                               /* @0x434270 @0x417a42 */
            eventAnimStep(g_pL1Throw1Anim, 1);                             /* @0x434090 @0x417a50 */
            sceneObjSetClassMesh((int)g_pL1BurgerObj, g_pL1McdmanObj, 8, 3); /* @0x430db0 @0x417a66 */
            sceneObjSetPos(g_pL1BurgerObj, 0, 0x78, 0, 2);                 /* @0x430660 @0x417a78 */
            sceneObjSetPosOrient(g_pL1BurgerObj, 0, 0, -16000, 2);         /* @0x4307d0 @0x417a8d */
            sceneObjResetFlags(g_pL1BurgerObj, 2);                         /* @0x430620 @0x417a9d */
            g_nL1EventStep = 6;                                            /* @0x417aa5 */
            /* fall through */
        case 6:
            if ((g_nL1EventTick & 1) == 0) {                               /* @0x417aaf */
                eventAnimApply(g_pL1Throw1Anim, 1);                        /* @0x434290 @0x417afd */
                break;                                                     /* @0x417b0a */
            }
            if (eventAnimStep(g_pL1Throw1Anim, 1) != 0) {                  /* @0x434090 @0x417acd */
                g_nL1EventStep = 7;                                        /* @0x417ae2 */
                break;                                                     /* @0x417af2 */
            }
            break;                                                         /* @0x417ad7 */
        case 7:
            eventAnimReset(g_pL1Throw2Anim);                               /* @0x434270 @0x417b1c */
            eventAnimStep(g_pL1Throw2Anim, 1);                             /* @0x434090 @0x417b2a */
            sceneObjSetClassMesh((int)g_pL1BurgerObj, NULL, 0, 3);         /* @0x430db0 @0x417b39 */
            sceneObjSetPos(g_pL1BurgerObj, -0x87f0, -0xfaa, 0, 2);         /* @0x430660 @0x417b52 */
            sceneObjSetPosOrient(g_pL1BurgerObj, 0, 0, 0, 2);              /* @0x4307d0 @0x417b63 */
            g_nL1MoveStep = 0;                                             /* @0x417b6d */
            g_nL1EventStep = 8;                                            /* @0x417b6d */
            /* fall through */
        case 8:
            g_nL1MoveStep++;                                               /* @0x417b7e */
            if (g_nL1MoveStep < 6) {                                       /* @0x417b7f */
                sceneObjSetPos(g_pL1BurgerObj, 0, 0,                       /* @0x430660 @0x417b9f */
                               (40 - g_nL1MoveStep) * 4, 5);
            }
            if ((g_nL1EventTick & 1) == 0) {                               /* @0x417ba7 */
                eventAnimApply(g_pL1Throw2Anim, 1);                        /* @0x434290 @0x417bf4 */
                break;                                                     /* @0x417c01 */
            }
            if (eventAnimStep(g_pL1Throw2Anim, 1) != 0) {                  /* @0x434090 @0x417bc3 */
                g_nL1EventStep = 9;                                        /* @0x417bd8 */
                break;                                                     /* @0x417be8 */
            }
            break;                                                         /* @0x417bcb */
        case 9:
            g_nL1MoveStep++;                                               /* @0x417c11 */
            if (g_nL1MoveStep < 6) {                                       /* @0x417c12 */
                sceneObjSetPos(g_pL1BurgerObj, 0, 0,                       /* @0x430660 @0x417c32 */
                               (40 - g_nL1MoveStep) * 4, 5);
                break;                                                     /* @0x417c45 */
            }
            g_nL1EventTick = 0;                                            /* @0x417c57 */
            g_nL1EventStep = 0;                                            /* @0x417c5d */
            g_nL1EventActive = 1;                                          /* @0x417c63 */
            /* mode 2 stores the raw int channel coords into the buffer */
            sceneNodeGetPosWorld(g_pL1BurgerObj, (float *)g_anL1SpawnPos, 2); /* @0x430e80 @0x417c6d */
            pNew = (EventObject *)malloc(0x50);                            /* operator_new @0x43dd42 @0x417c74 */
            if (pNew != NULL) {                                            /* @0x417c80 */
                /* ctor args are the {flX=worldZ, flY=worldX, flHeight=worldY}
                 * ints of the BURGER pos (FILD integer loads @0x417c88..0x417ca5) */
                sceneObjCtor3(pNew, 0x1f, (float)g_anL1SpawnPos[2],        /* @0x4146a0 @0x417caa */
                              (float)g_anL1SpawnPos[0], (float)g_anL1SpawnPos[1]);
                pNew->field_10 = g_anL1SpawnPos[1];                        /* +0x10 @0x417cba */
                pNew->field_14 = (int)(uintptr_t)g_pL1BurgerObj;           /* +0x14 @0x417ccb */
                objHashRegister(pNew);                                     /* @0x4148f0 @0x417cce */
            }
            break;                                                         /* @0x417cd6 (tick++ -> 1) */
        default:
            break;                                                         /* @0x417cd6 */
        }
        g_nL1EventTick++;                                                  /* @0x417cd6 */
    }

    /* trailing cashier ambient: blink every other frame — even ticks step,
     * odd ticks apply (opposite phase of the presenter anims) */
    g_nL1TrailTick++;                                                      /* @0x417cf6 */
    if (g_nResultsScreen == 0) {                                           /* @0x417d01 */
        if ((g_nL1TrailTick & 1) == 0) {                                   /* @0x417d0c */
            eventAnimStep(g_pL1CashSitAnim, 1);                            /* @0x434090 @0x417d23 */
            return;                                                        /* @0x417d39 */
        }
        eventAnimApply(g_pL1CashSitAnim, 1);                               /* @0x434290 @0x417d77 */
    } else {
        if ((g_nL1TrailTick & 1) == 0) {                                   /* @0x417d43 */
            eventAnimStep(g_pL1WinnerAnim, 1);                             /* @0x434090 @0x417d59 */
            return;                                                        /* @0x417d6f */
        }
        eventAnimApply(g_pL1WinnerAnim, 1);                                /* @0x434290 @0x417d77 */
    }
}
