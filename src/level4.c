/* level4.c — level-4 (Downtown Mall) "MCDMAN bonus task" event director.
 *
 * Original functions (verified 2026-09-13 against full disassembly):
 *   levelEventDirector_L4_Init    @0x418f30
 *   levelEventDirector_L4_Cleanup @0x419240
 *   levelEventDirector_L4         @0x4192a0
 *
 * Per frame the director:
 *  1. while the bonus item is not active, runs a 9-step anim state machine
 *     after a 10 s idle gate (g_nObjUpdateTime * g_nL4EventTick >= 10000):
 *     flpick1 blink (s1-2), flpick2 (s3-4), throw1 + attach the hidden
 *     BURGER prop to the MCDMAN class mesh slot 8 (s5-6), throw2 + detach
 *     and drop the BURGER at (54800,-1010,4200) sliding in four mode-5
 *     x-steps (step*4-0xa0, s7-8), then spawn a class-0x1f pickup
 *     EventObject (sceneObjCtor3 + objHashRegister) at the BURGER world
 *     position — the pickup consumed by playerAiGrabItem @0x40ea20 via
 *     EventObject.field_14 (s9);
 *  2. blinks the KASSOERSKA cashier anim (cash_sit during play,
 *     s_winner on the results screen) every other frame via the
 *     eventAnim step/apply alternation.
 *
 * Init additionally registers five positional sfx emitters
 * (sndPlaySfx3D @0x42bcd0, bank 1 idx 6/6/5/5/5, vol 65000, sndId 0xff,
 * flags 0x11) at (500,-2000,0), (42250,-1000,200), (42800,-1000,11000),
 * (42800,-1000,-11000) and (11250,-1000,38500).
 *
 * Cleanup frees only the anims: the cashier/mascot/BURGER scene objects
 * belong to the round scene teardown (roundTeardown @0x40aa10). The
 * original UB on operator_new failure (NULL deref at [EAX+0x10]) is
 * guarded with the same `if (pNew != NULL)` pattern used by the
 * player_ai.c landed-item path.
 */

#include <stdlib.h>

#include "level4.h"
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
static int g_nL4EventActive;            /* @0x459f28 1 while the bonus pickup is on the field */
static int g_nL4EventTick;              /* @0x459f2c idle-frame counter / presenter blink clock */
static int g_nL4EventStep;              /* @0x459f30 presenter state machine step (0..9) */
static int g_nL4MoveStep;               /* @0x459f34 throw-landing mode-5 move-step */
static SceneNode *g_pL4McdmanObj;       /* @0x459f38 MCDMAN mascot scenery object */
static SceneNode *g_pL4BurgerObj;       /* @0x459f3c BURGER prop scenery object */
static SceneNode *g_pL4KassoerskaObj;   /* @0x459f40 KASSOERSKA cashier scenery object */
static int g_anL4SpawnPos[3];           /* @0x459f44 BURGER world pos readback (mode-2 raw ints) */
static AnmFile *g_pL4StandAnim;         /* @0x459f50 anim\s_stand.anm (mascot idle) */
static AnmFile *g_pL4Flpick1Anim;       /* @0x459f54 anim\s_flpick1.anm */
static AnmFile *g_pL4Flpick2Anim;       /* @0x459f58 anim\s_flpick2.anm */
static AnmFile *g_pL4Throw1Anim;        /* @0x459f5c anim\s_throw1.anm */
static AnmFile *g_pL4Throw2Anim;        /* @0x459f60 anim\s_throw2.anm */
static AnmFile *g_pL4CashSitAnim;       /* @0x459f64 anim\cash_sit.anm (cashier idle) */
static AnmFile *g_pL4WinnerAnim;        /* @0x459f68 anim\s_winner.anm (results screen) */
static int g_nL4TrailTick;              /* @0x459f6c cashier blink clock */

/* levelEventDirector_L4_Init @0x418f30 — round-start asset setup. Allocates
 * the KASSOERSKA cashier (at pitch 32000) and MCDMAN mascot scenery
 * objects plus the hidden BURGER prop, loads the seven event anims
 * against the mascot/cashier nodes, and registers the five positional
 * sfx emitters. */
void levelEventDirector_L4_Init(void) /* @0x418f30 */
{
    void *pName;
    SndEmitter *pEmitter;

    g_pL4KassoerskaObj = (SceneNode *)sceneNodeAllocChild(NULL, NULL,      /* @0x4319e0 @0x418f5a */
                                                          (void *)0x5bcc, (void *)0x7d0,
                                                          (void *)0x1194);
    sceneObjSetPosOrient(g_pL4KassoerskaObj, 0, 32000, 0, 2);               /* @0x4307d0 @0x418f6e */
    pName = (void *)(uintptr_t)scenNameToId("KASSOERSKA");                  /* @0x431ed0 @0x418f78 */
    g_pL4KassoerskaObj = (SceneNode *)sceneryObjAlloc(g_pL4KassoerskaObj,   /* @0x430200 @0x418f8b */
                                                      0, 0, 0, 0, 0, 0, 0, pName);
    g_pL4CashSitAnim = anmLoadFile("anim\\cash_sit.anm", NULL,              /* @0x433a50 @0x418f9f */
                                   g_pL4KassoerskaObj);
    g_pL4WinnerAnim = anmLoadFile("anim\\s_winner.anm", NULL,               /* @0x433a50 @0x418fb6 */
                                  g_pL4KassoerskaObj);
    g_pL4McdmanObj = (SceneNode *)sceneNodeAllocChild(NULL, NULL,           /* @0x4319e0 @0x418fcd */
                                                      (void *)0xd7a0, NULL,
                                                      (void *)0xfa0);
    sceneObjSetPosOrient(g_pL4McdmanObj, 0, -16000, 0, 2);                  /* @0x4307d0 @0x418fe1 */
    pName = (void *)(uintptr_t)scenNameToId("MCDMAN");                      /* @0x431ed0 @0x418fee */
    g_pL4McdmanObj = (SceneNode *)sceneryObjAlloc(g_pL4McdmanObj,           /* @0x430200 @0x419002 */
                                                  0, 0, 0, 0, 0, 0, 0, pName);
    pName = (void *)(uintptr_t)scenNameToId("BURGER");                      /* @0x431ed0 @0x41900c */
    g_pL4BurgerObj = (SceneNode *)sceneryObjAlloc(NULL, 0, 0, 0, 0, 0, 0, 0, /* @0x430200 @0x41901f */
                                                  pName);
    sceneNodeSetHiddenFlag(g_pL4BurgerObj, 2);                              /* @0x4305c0 @0x41902f */
    g_pL4StandAnim = anmLoadFile("anim\\s_stand.anm", NULL,                 /* @0x433a50 @0x419040 */
                                 g_pL4McdmanObj);
    g_pL4Flpick1Anim = anmLoadFile("anim\\s_flpick1.anm", NULL,             /* @0x433a50 @0x419057 */
                                   g_pL4McdmanObj);
    g_pL4Flpick2Anim = anmLoadFile("anim\\s_flpick2.anm", NULL,             /* @0x433a50 @0x41906e */
                                   g_pL4McdmanObj);
    g_pL4Throw1Anim = anmLoadFile("anim\\s_throw1.anm", NULL,               /* @0x433a50 @0x419084 */
                                  g_pL4McdmanObj);
    g_pL4Throw2Anim = anmLoadFile("anim\\s_throw2.anm", NULL,               /* @0x433a50 @0x41909b */
                                  g_pL4McdmanObj);
    eventAnimStep(g_pL4StandAnim, 1);                                       /* @0x434090 @0x4190b1 */
    eventAnimStep(g_pL4CashSitAnim, 1);                                     /* @0x434090 @0x4190be */
    g_nL4EventActive = 0;                                                   /* @0x4190c5 */
    g_nL4EventTick = 0;                                                     /* @0x4190cb */
    g_nL4EventStep = 0;                                                     /* @0x4190d1 */
    pEmitter = (SndEmitter *)malloc(0x1c);                                  /* operator_new @0x43dd42 @0x4190d7 */
    if (pEmitter != NULL) {
        sndPlaySfx3D(pEmitter, 1, 6, 65000, 0xff, NULL, 0,                 /* @0x42bcd0 @0x41910a */
                     500, -2000, 0, 0x11);
    }
    pEmitter = (SndEmitter *)malloc(0x1c);                                  /* operator_new @0x43dd42 @0x419119 */
    if (pEmitter != NULL) {
        sndPlaySfx3D(pEmitter, 1, 6, 65000, 0xff, NULL, 0,                 /* @0x42bcd0 @0x419154 */
                     42250, -1000, 200, 0x11);
    }
    pEmitter = (SndEmitter *)malloc(0x1c);                                  /* operator_new @0x43dd42 @0x41915f */
    if (pEmitter != NULL) {
        sndPlaySfx3D(pEmitter, 1, 5, 65000, 0xff, NULL, 0,                 /* @0x42bcd0 @0x41919a */
                     42800, -1000, 11000, 0x11);
    }
    pEmitter = (SndEmitter *)malloc(0x1c);                                  /* operator_new @0x43dd42 @0x4191a5 */
    if (pEmitter != NULL) {
        sndPlaySfx3D(pEmitter, 1, 5, 65000, 0xff, NULL, 0,                 /* @0x42bcd0 @0x4191e0 */
                     42800, -1000, -11000, 0x11);
    }
    pEmitter = (SndEmitter *)malloc(0x1c);                                  /* operator_new @0x43dd42 @0x4191eb */
    if (pEmitter != NULL) {
        sndPlaySfx3D(pEmitter, 1, 5, 65000, 0xff, NULL, 0,                 /* @0x42bcd0 @0x419227 */
                     11250, -1000, 38500, 0x11);
    }
}

/* levelEventDirector_L4_Cleanup @0x419240 — frees the seven event anims.
 * Only the anims: the cashier/mascot/BURGER scene objects are owned by
 * the round scene teardown. Dispatched by roundTeardown @0x40aa10 (case 4)
 * on g_nLevelIdx == 4. */
void levelEventDirector_L4_Cleanup(void) /* @0x419240 */
{
    anmFree(g_pL4StandAnim);     /* @0x434050 */
    anmFree(g_pL4Flpick1Anim);   /* @0x434050 */
    anmFree(g_pL4Flpick2Anim);   /* @0x434050 */
    anmFree(g_pL4Throw1Anim);    /* @0x434050 */
    anmFree(g_pL4Throw2Anim);    /* @0x434050 */
    anmFree(g_pL4CashSitAnim);   /* @0x434050 */
    anmFree(g_pL4WinnerAnim);    /* @0x434050 */
}

/* levelEventDirector_L4 @0x4192a0 — per-frame director, dispatched from
 * roundLogicUpdate @0x40c1c8 on g_nLevelIdx == 4. Switch/jump table at
 * 0x419770 covers steps 1..9; step 0 falls to the common tick increment.
 * Every switch path increments g_nL4EventTick exactly once. */
void levelEventDirector_L4(void) /* @0x4192a0 */
{
    EventObject *pNew;

    if (g_nL4EventActive != 0) {                                           /* @0x4192be */
        /* bonus item on the field: re-arm once it is gone (picked up or
         * otherwise hidden again) */
        if (sceneNodeGetHiddenFlag(g_pL4BurgerObj) != 0) {                 /* @0x4305b0 @0x4196c4 */
            g_nL4EventActive = 0;                                          /* @0x4196d0 */
        }
    } else {
        if (g_nObjUpdateTime * g_nL4EventTick >= 10000 &&                  /* @0x4192cb */
            g_nL4EventStep == 0) {                                         /* @0x4192d9 */
            g_nL4EventStep = 1;                                            /* @0x4192e1 */
        }
        switch (g_nL4EventStep) {                                          /* jump table @0x419770 */
        case 1:
            eventAnimReset(g_pL4Flpick1Anim);                              /* @0x434270 @0x41930b */
            eventAnimStep(g_pL4Flpick1Anim, 1);                            /* @0x434090 @0x419318 */
            g_nL4EventStep = 2;                                            /* @0x419320 */
            /* fall through */
        case 2:
            if ((g_nL4EventTick & 1) == 0) {                               /* @0x41932a */
                eventAnimApply(g_pL4Flpick1Anim, 1);                       /* @0x434290 @0x419378 */
                break;                                                     /* @0x41938b */
            }
            if (eventAnimStep(g_pL4Flpick1Anim, 1) != 0) {                 /* @0x434090 @0x419348 */
                g_nL4EventStep = 3;                                        /* @0x41935d */
                break;                                                     /* @0x41936d */
            }
            break;                                                         /* @0x419352 */
        case 3:
            eventAnimReset(g_pL4Flpick2Anim);                              /* @0x434270 @0x419397 */
            eventAnimStep(g_pL4Flpick2Anim, 1);                            /* @0x434090 @0x4193a5 */
            g_nL4EventStep = 4;                                            /* @0x4193ad */
            /* fall through */
        case 4:
            if ((g_nL4EventTick & 1) == 0) {                               /* @0x4193b7 */
                eventAnimApply(g_pL4Flpick2Anim, 1);                       /* @0x434290 @0x419404 */
                break;                                                     /* @0x419417 */
            }
            if (eventAnimStep(g_pL4Flpick2Anim, 1) != 0) {                 /* @0x434090 @0x4193d3 */
                g_nL4EventStep = 5;                                        /* @0x4193e8 */
                break;                                                     /* @0x4193f8 */
            }
            break;                                                         /* @0x4193dd */
        case 5:
            eventAnimReset(g_pL4Throw1Anim);                               /* @0x434270 @0x419422 */
            eventAnimStep(g_pL4Throw1Anim, 1);                             /* @0x434090 @0x419430 */
            sceneObjSetClassMesh((int)g_pL4BurgerObj, g_pL4McdmanObj, 8, 3); /* @0x430db0 @0x419446 */
            sceneObjSetPos(g_pL4BurgerObj, 0, 0x78, 0, 2);                 /* @0x430660 @0x419458 */
            sceneObjSetPosOrient(g_pL4BurgerObj, 0, 0, -16000, 2);         /* @0x4307d0 @0x41946d */
            sceneObjResetFlags(g_pL4BurgerObj, 2);                         /* @0x430620 @0x41947d */
            g_nL4EventStep = 6;                                            /* @0x419485 */
            /* fall through */
        case 6:
            if ((g_nL4EventTick & 1) == 0) {                               /* @0x41948f */
                eventAnimApply(g_pL4Throw1Anim, 1);                        /* @0x434290 @0x4194dd */
                break;                                                     /* @0x4194f0 */
            }
            if (eventAnimStep(g_pL4Throw1Anim, 1) != 0) {                  /* @0x434090 @0x4194ad */
                g_nL4EventStep = 7;                                        /* @0x4194c2 */
                break;                                                     /* @0x4194d2 */
            }
            break;                                                         /* @0x4194b7 */
        case 7:
            eventAnimReset(g_pL4Throw2Anim);                               /* @0x434270 @0x4194fc */
            eventAnimStep(g_pL4Throw2Anim, 1);                             /* @0x434090 @0x41950a */
            sceneObjSetClassMesh((int)g_pL4BurgerObj, NULL, 0, 3);         /* @0x430db0 @0x419519 */
            sceneObjSetPos(g_pL4BurgerObj, 0xd610, -0x3f2, 0x1068, 2);     /* @0x430660 @0x419536 */
            sceneObjSetPosOrient(g_pL4BurgerObj, 0, 0, 0, 2);              /* @0x4307d0 @0x419547 */
            g_nL4MoveStep = 0;                                             /* @0x41954f */
            g_nL4EventStep = 8;                                            /* @0x419551 */
            /* fall through */
        case 8:
            g_nL4MoveStep++;                                               /* @0x419562 */
            if (g_nL4MoveStep < 6) {                                       /* @0x419566 */
                sceneObjSetPos(g_pL4BurgerObj, g_nL4MoveStep * 4 - 0xa0, 0, /* @0x430660 @0x419580 */
                               0, 5);
            }
            if ((g_nL4EventTick & 1) == 0) {                               /* @0x41958e */
                eventAnimApply(g_pL4Throw2Anim, 1);                        /* @0x434290 @0x4195d6 */
                break;                                                     /* @0x4195e9 */
            }
            if (eventAnimStep(g_pL4Throw2Anim, 1) != 0) {                  /* @0x434090 @0x4195a5 */
                g_nL4EventStep = 9;                                        /* @0x4195ba */
                break;                                                     /* @0x4195ca */
            }
            break;                                                         /* @0x4195af */
        case 9:
            g_nL4MoveStep++;                                               /* @0x4195ee */
            if (g_nL4MoveStep < 6) {                                       /* @0x4195f7 */
                sceneObjSetPos(g_pL4BurgerObj, g_nL4MoveStep * 4 - 0xa0, 0, /* @0x430660 @0x419610 */
                               0, 5);
                break;                                                     /* @0x419623 */
            }
            g_nL4EventTick = 0;                                            /* @0x419636 */
            g_nL4EventStep = 0;                                            /* @0x41963c */
            g_nL4EventActive = 1;                                          /* @0x419642 */
            /* mode 2 stores the raw int channel coords into the buffer */
            sceneNodeGetPosWorld(g_pL4BurgerObj, (float *)g_anL4SpawnPos, 2); /* @0x430e80 @0x41964c */
            pNew = (EventObject *)malloc(0x50);                            /* operator_new @0x43dd42 @0x419653 */
            if (pNew != NULL) {                                            /* @0x41965f */
                /* ctor args are the {flX=worldZ, flY=worldX, flHeight=worldY}
                 * ints of the BURGER pos (FILD integer loads @0x419667..0x419681) */
                sceneObjCtor3(pNew, 0x1f, (float)g_anL4SpawnPos[2],        /* @0x4146a0 @0x419689 */
                              (float)g_anL4SpawnPos[0], (float)g_anL4SpawnPos[1]);
                pNew->field_10 = g_anL4SpawnPos[1];                        /* +0x10 @0x419699 */
                pNew->field_14 = (int)(uintptr_t)g_pL4BurgerObj;           /* +0x14 @0x4196aa */
                objHashRegister(pNew);                                     /* @0x4148f0 @0x4196ad */
            }
            break;                                                         /* @0x4196b5 (tick++ -> 1) */
        default:
            break;                                                         /* @0x4196b5 */
        }
        g_nL4EventTick++;                                                  /* @0x4196b5 */
    }

    /* trailing cashier ambient: blink every other frame — even ticks step,
     * odd ticks apply (opposite phase of the presenter anims) */
    g_nL4TrailTick++;                                                      /* @0x4196db */
    if (g_nResultsScreen == 0) {                                           /* @0x4196e1 */
        if ((g_nL4TrailTick & 1) == 0) {                                   /* @0x4196ec */
            eventAnimStep(g_pL4CashSitAnim, 1);                            /* @0x434090 @0x419702 */
            return;                                                        /* @0x419718 */
        }
        eventAnimApply(g_pL4CashSitAnim, 1);                               /* @0x434290 @0x419756 */
    } else {
        if ((g_nL4TrailTick & 1) == 0) {                                   /* @0x41972e */
            eventAnimStep(g_pL4WinnerAnim, 1);                             /* @0x434090 @0x419739 */
            return;                                                        /* @0x41974f */
        }
        eventAnimApply(g_pL4WinnerAnim, 1);                                /* @0x434290 @0x419756 */
    }
}
