/* level3.c — level-3 (aqua, "Sjöhästen") "MCDMAN bonus task" event director.
 *
 * Original functions (verified 2026-09-13 against full disassembly):
 *   levelEventDirector_L3_Init    @0x4186c0
 *   levelEventDirector_L3_Cleanup @0x4189d0
 *   levelEventDirector_L3         @0x418a30
 *
 * Per frame the director:
 *  1. rotates the seven SKYLT%d sign nodes (scenNameToIdEx @0x431e20) in
 *     pitch via sceneObjSetPosOrient(mode 5) with pitch (i*5+0x28)*10
 *     (400..700 for i=0..6);
 *  2. while the bonus item is not active, runs a 9-step anim state machine
 *     after a 10 s idle gate (g_nObjUpdateTime * g_nL3EventTick >= 10000):
 *     flpick1 blink (s1-2), flpick2 (s3-4), throw1 + attach the hidden
 *     BURGER prop to the MCDMAN class mesh slot 8 (s5-6), throw2 + detach
 *     and drop the BURGER at (-39000,3010,-23200) sliding in four mode-5
 *     x-steps ((0x28-step)*4, s7-8), then spawn a class-0x1f pickup
 *     EventObject (sceneObjCtor3 + objHashRegister) at the BURGER world
 *     position — the pickup consumed by playerAiGrabItem @0x40ea20 via
 *     EventObject.nValue1 (s9);
 *  3. blinks the KASSOERSKA cashier anim (cash_sit during play,
 *     s_winner on the results screen) every other frame via the
 *     eventAnim step/apply alternation.
 *
 * Init additionally registers four positional sfx emitters
 * (sndPlaySfx3D @0x42bcd0, bank 1 idx 0x1b/0x1b/0x19/0x1b, vol 65000,
 * sndId 0xff, flags 0x11) at (8200,8000,-39000), (-25500,4000,-29000),
 * (30500,-1000,-29000) and (3500,-1000,-17000).
 *
 * Cleanup frees only the anims: the cashier/mascot/BURGER scene objects
 * belong to the round scene teardown (roundTeardown @0x40aa10). The
 * original UB on operator_new failure (NULL deref at [EAX+0x10]) is
 * guarded with the same `if (pNew != NULL)` pattern used by the
 * player_ai.c landed-item path.
 */

#include <stdlib.h>

#include "level3.h"
#include "anim.h"
#include "gameplay.h"
#include "obj_event.h"
#include "player.h"
#include "scene.h"
#include "scene_alloc.h"
#include "scene_render.h"
#include "scene_transform.h"
#include "sound.h"
#include "util.h"

#define L3_SKYLT_COUNT 7  /* @0x459f04..0x459f1c (trail tick @0x459f20) */

/* --- director state (xref-verified private to the three functions) --- */
static int g_nL3EventActive;            /* @0x459ec0 1 while the bonus pickup is on the field */
static int g_nL3EventTick;              /* @0x459ec4 idle-frame counter / presenter blink clock */
static int g_nL3EventStep;              /* @0x459ec8 presenter state machine step (0..9) */
static int g_nL3MoveStep;               /* @0x459ecc throw-landing mode-5 move-step */
static SceneNode *g_pL3McdmanObj;       /* @0x459ed0 MCDMAN mascot scenery object */
static SceneNode *g_pL3BurgerObj;       /* @0x459ed4 BURGER prop scenery object */
static int g_anL3SpawnPos[3];           /* @0x459ed8 BURGER world pos readback (mode-2 raw ints) */
static AnmFile *g_pL3StandAnim;         /* @0x459ee4 anim\s_stand.anm (mascot idle) */
static AnmFile *g_pL3Flpick1Anim;       /* @0x459ee8 anim\s_flpick1.anm */
static AnmFile *g_pL3Flpick2Anim;       /* @0x459eec anim\s_flpick2.anm */
static AnmFile *g_pL3Throw1Anim;        /* @0x459ef0 anim\s_throw1.anm */
static AnmFile *g_pL3Throw2Anim;        /* @0x459ef4 anim\s_throw2.anm */
static SceneNode *g_pL3KassoerskaObj;   /* @0x459ef8 KASSOERSKA cashier scenery object */
static AnmFile *g_pL3CashSitAnim;       /* @0x459efc anim\cash_sit.anm (cashier idle) */
static AnmFile *g_pL3WinnerAnim;        /* @0x459f00 anim\s_winner.anm (results screen) */
static int g_anL3SkyltIds[L3_SKYLT_COUNT]; /* @0x459f04 scenNameToIdEx("SKYLT%d") node handles */
static int g_nL3TrailTick;              /* @0x459f20 cashier blink clock */

/* levelEventDirector_L3_Init @0x4186c0 — round-start asset setup. Allocates
 * the KASSOERSKA cashier (at pitch 32000) and MCDMAN mascot scenery
 * objects plus the hidden BURGER prop, loads the seven event anims
 * against the mascot/cashier nodes, resolves the seven SKYLT%d sign
 * handles, and registers the four positional sfx emitters. */
void levelEventDirector_L3_Init(void) /* @0x4186c0 */
{
    void *pName;
    SndEmitter *pEmitter;
    char szBuf[32];
    int i;

    g_pL3KassoerskaObj = (SceneNode *)sceneNodeAllocChild(NULL, NULL,      /* @0x4319e0 @0x4186ee */
                                                          (void *)0x7b0c, (void *)0x1f40,
                                                          (void *)0xffff793c);
    sceneObjSetPosOrient(g_pL3KassoerskaObj, 0, 32000, 0, 2);               /* @0x4307d0 @0x418702 */
    pName = (void *)(uintptr_t)scenNameToId("KASSOERSKA");                  /* @0x431ed0 @0x41870c */
    g_pL3KassoerskaObj = (SceneNode *)sceneryObjAlloc(g_pL3KassoerskaObj,   /* @0x430200 @0x41871f */
                                                      0, 0, 0, 0, 0, 0, 0, pName);
    g_pL3CashSitAnim = anmLoadFile("anim\\cash_sit.anm", NULL,              /* @0x433a50 @0x418733 */
                                   g_pL3KassoerskaObj);
    g_pL3WinnerAnim = anmLoadFile("anim\\s_winner.anm", NULL,               /* @0x433a50 @0x41874a */
                                  g_pL3KassoerskaObj);
    g_pL3McdmanObj = (SceneNode *)sceneNodeAllocChild(NULL, NULL,           /* @0x4319e0 @0x418765 */
                                                      (void *)0xffff66e0, (void *)0xfa0,
                                                      (void *)0xffffa628);
    sceneObjSetPosOrient(g_pL3McdmanObj, 0, 16000, 0, 2);                   /* @0x4307d0 @0x418779 */
    pName = (void *)(uintptr_t)scenNameToId("MCDMAN");                      /* @0x431ed0 @0x418786 */
    g_pL3McdmanObj = (SceneNode *)sceneryObjAlloc(g_pL3McdmanObj,           /* @0x430200 @0x41879a */
                                                  0, 0, 0, 0, 0, 0, 0, pName);
    pName = (void *)(uintptr_t)scenNameToId("BURGER");                      /* @0x431ed0 @0x4187a9 */
    g_pL3BurgerObj = (SceneNode *)sceneryObjAlloc(NULL, 0, 0, 0, 0, 0, 0, 0, /* @0x430200 @0x4187b7 */
                                                  pName);
    sceneNodeSetHiddenFlag(g_pL3BurgerObj, 2);                              /* @0x4305c0 @0x4187c7 */
    g_pL3StandAnim = anmLoadFile("anim\\s_stand.anm", NULL,                 /* @0x433a50 @0x4187d8 */
                                 g_pL3McdmanObj);
    g_pL3Flpick1Anim = anmLoadFile("anim\\s_flpick1.anm", NULL,             /* @0x433a50 @0x4187ef */
                                   g_pL3McdmanObj);
    g_pL3Flpick2Anim = anmLoadFile("anim\\s_flpick2.anm", NULL,             /* @0x433a50 @0x418806 */
                                   g_pL3McdmanObj);
    g_pL3Throw1Anim = anmLoadFile("anim\\s_throw1.anm", NULL,               /* @0x433a50 @0x41881c */
                                  g_pL3McdmanObj);
    g_pL3Throw2Anim = anmLoadFile("anim\\s_throw2.anm", NULL,               /* @0x433a50 @0x418833 */
                                  g_pL3McdmanObj);
    eventAnimStep(g_pL3StandAnim, 1);                                       /* @0x434090 @0x418849 */
    eventAnimStep(g_pL3CashSitAnim, 1);                                     /* @0x434090 @0x418856 */
    g_nL3EventActive = 0;                                                   /* @0x41885e */
    g_nL3EventTick = 0;                                                     /* @0x418864 */
    g_nL3EventStep = 0;                                                     /* @0x41886a */
    for (i = 0; i < L3_SKYLT_COUNT; i++) {                                 /* @0x418870..0x4188a0 */
        fmtSprintf(szBuf, "SKYLT%d", i);                                    /* @0x43e767 @0x418882 */
        g_anL3SkyltIds[i] = scenNameToIdEx(szBuf);                          /* @0x431e20 @0x41888c */
    }
    pEmitter = (SndEmitter *)malloc(0x1c);                                  /* operator_new @0x43dd42 @0x4188a4 */
    if (pEmitter != NULL) {
        sndPlaySfx3D(pEmitter, 1, 0x1b, 65000, 0xff, NULL, 0,               /* @0x42bcd0 @0x4188db */
                     8200, 8000, -39000, 0x11);
    }
    pEmitter = (SndEmitter *)malloc(0x1c);                                  /* operator_new @0x43dd42 @0x4188e9 */
    if (pEmitter != NULL) {
        sndPlaySfx3D(pEmitter, 1, 0x1b, 65000, 0xff, NULL, 0,               /* @0x42bcd0 @0x418924 */
                     -25500, 4000, -29000, 0x11);
    }
    pEmitter = (SndEmitter *)malloc(0x1c);                                  /* operator_new @0x43dd42 @0x41892f */
    if (pEmitter != NULL) {
        sndPlaySfx3D(pEmitter, 1, 0x19, 65000, 0xff, NULL, 0,               /* @0x42bcd0 @0x41896a */
                     30500, -1000, -29000, 0x11);
    }
    pEmitter = (SndEmitter *)malloc(0x1c);                                  /* operator_new @0x43dd42 @0x418975 */
    if (pEmitter != NULL) {
        sndPlaySfx3D(pEmitter, 1, 0x1b, 65000, 0xff, NULL, 0,               /* @0x42bcd0 @0x4189b0 */
                     3500, -1000, -17000, 0x11);
    }
}

/* levelEventDirector_L3_Cleanup @0x4189d0 — frees the seven event anims.
 * Only the anims: the cashier/mascot/BURGER scene objects are owned by
 * the round scene teardown. Dispatched by roundTeardown @0x40aa10 (case 3
 * @0x40aae6) on g_nLevelIdx == 3. */
void levelEventDirector_L3_Cleanup(void) /* @0x4189d0 */
{
    anmFree(g_pL3StandAnim);     /* @0x434050 */
    anmFree(g_pL3Flpick1Anim);   /* @0x434050 */
    anmFree(g_pL3Flpick2Anim);   /* @0x434050 */
    anmFree(g_pL3Throw1Anim);    /* @0x434050 */
    anmFree(g_pL3Throw2Anim);    /* @0x434050 */
    anmFree(g_pL3CashSitAnim);   /* @0x434050 */
    anmFree(g_pL3WinnerAnim);    /* @0x434050 */
}

/* levelEventDirector_L3 @0x418a30 — per-frame director, dispatched from
 * roundLogicUpdate @0x40c1c8 on g_nLevelIdx == 3. Switch/jump table at
 * 0x418f0c covers steps 1..9; step 0 falls to the common tick increment.
 * Every switch path increments g_nL3EventTick exactly once. */
void levelEventDirector_L3(void) /* @0x418a30 */
{
    EventObject *pNew;
    int i;

    for (i = 0; i < L3_SKYLT_COUNT; i++) {                                 /* @0x418a4d..0x418a79 */
        if (g_anL3SkyltIds[i] != 0) {
            sceneObjSetPosOrient((SceneNode *)(uintptr_t)g_anL3SkyltIds[i], /* @0x4307d0 @0x418a67 */
                                 0, (short)((i * 5 + 0x28) * 10), 0, 5);
        }
    }

    if (g_nL3EventActive != 0) {                                           /* @0x418a85 */
        /* bonus item on the field: re-arm once it is gone (picked up or
         * otherwise hidden again) */
        if (sceneNodeGetHiddenFlag(g_pL3BurgerObj) != 0) {                 /* @0x4305b0 @0x418e81 */
            g_nL3EventActive = 0;                                          /* @0x418e8d */
        }
    } else {
        if (g_nObjUpdateTime * g_nL3EventTick >= 10000 &&                  /* @0x418a93 */
            g_nL3EventStep == 0) {                                         /* @0x418aa2 */
            g_nL3EventStep = 1;                                            /* @0x418aaa */
        }
        switch (g_nL3EventStep) {                                          /* jump table @0x418f0c */
        case 1:
            eventAnimReset(g_pL3Flpick1Anim);                              /* @0x434270 @0x418acf */
            eventAnimStep(g_pL3Flpick1Anim, 1);                            /* @0x434090 @0x418adc */
            g_nL3EventStep = 2;                                            /* @0x418ae4 */
            /* fall through */
        case 2:
            if ((g_nL3EventTick & 1) == 0) {                               /* @0x418aee */
                eventAnimApply(g_pL3Flpick1Anim, 1);                       /* @0x434290 @0x418b3b */
                break;                                                     /* @0x418b4e */
            }
            if (eventAnimStep(g_pL3Flpick1Anim, 1) != 0) {                 /* @0x434090 @0x418b0a */
                g_nL3EventStep = 3;                                        /* @0x418b1f */
                break;                                                     /* @0x418b2f */
            }
            break;                                                         /* @0x418b14 */
        case 3:
            eventAnimReset(g_pL3Flpick2Anim);                              /* @0x434270 @0x418b5a */
            eventAnimStep(g_pL3Flpick2Anim, 1);                            /* @0x434090 @0x418b66 */
            g_nL3EventStep = 4;                                            /* @0x418b6e */
            /* fall through */
        case 4:
            if ((g_nL3EventTick & 1) == 0) {                               /* @0x418b78 */
                eventAnimApply(g_pL3Flpick2Anim, 1);                       /* @0x434290 @0x418bc5 */
                break;                                                     /* @0x418bd8 */
            }
            if (eventAnimStep(g_pL3Flpick2Anim, 1) != 0) {                 /* @0x434090 @0x418b95 */
                g_nL3EventStep = 5;                                        /* @0x418baa */
                break;                                                     /* @0x418bba */
            }
            break;                                                         /* @0x418b9f */
        case 5:
            eventAnimReset(g_pL3Throw1Anim);                               /* @0x434270 @0x418be4 */
            eventAnimStep(g_pL3Throw1Anim, 1);                             /* @0x434090 @0x418bf1 */
            sceneObjSetClassMesh((int)g_pL3BurgerObj, g_pL3McdmanObj, 8, 3); /* @0x430db0 @0x418c07 */
            sceneObjSetPos(g_pL3BurgerObj, 0, 0x78, 0, 2);                 /* @0x430660 @0x418c19 */
            sceneObjSetPosOrient(g_pL3BurgerObj, 0, 0, -16000, 2);         /* @0x4307d0 @0x418c2d */
            sceneObjResetFlags(g_pL3BurgerObj, 2);                         /* @0x430620 @0x418c3e */
            g_nL3EventStep = 6;                                            /* @0x418c46 */
            /* fall through */
        case 6:
            if ((g_nL3EventTick & 1) == 0) {                               /* @0x418c50 */
                eventAnimApply(g_pL3Throw1Anim, 1);                        /* @0x434290 @0x418c9d */
                break;                                                     /* @0x418cb0 */
            }
            if (eventAnimStep(g_pL3Throw1Anim, 1) != 0) {                  /* @0x434090 @0x418c6c */
                g_nL3EventStep = 7;                                        /* @0x418c81 */
                break;                                                     /* @0x418c91 */
            }
            break;                                                         /* @0x418c76 */
        case 7:
            eventAnimReset(g_pL3Throw2Anim);                               /* @0x434270 @0x418cbc */
            eventAnimStep(g_pL3Throw2Anim, 1);                             /* @0x434090 @0x418cc8 */
            sceneObjSetClassMesh((int)g_pL3BurgerObj, NULL, 0, 3);         /* @0x430db0 @0x418cd8 */
            sceneObjSetPos(g_pL3BurgerObj, -39000, 3010, -23200, 2);       /* @0x430660 @0x418cf5 */
            sceneObjSetPosOrient(g_pL3BurgerObj, 0, 0, 0, 2);              /* @0x4307d0 @0x418d05 */
            g_nL3MoveStep = 0;                                             /* @0x418d0d */
            g_nL3EventStep = 8;                                            /* @0x418d0f */
            /* fall through */
        case 8:
            g_nL3MoveStep++;                                               /* @0x418d20 */
            if (g_nL3MoveStep < 6) {                                       /* @0x418d24 */
                sceneObjSetPos(g_pL3BurgerObj, (0x28 - g_nL3MoveStep) * 4, /* @0x430660 @0x418d41 */
                               0, 0, 5);
            }
            if ((g_nL3EventTick & 1) == 0) {                               /* @0x418d4e */
                eventAnimApply(g_pL3Throw2Anim, 1);                        /* @0x434290 @0x418d95 */
                break;                                                     /* @0x418da8 */
            }
            if (eventAnimStep(g_pL3Throw2Anim, 1) != 0) {                  /* @0x434090 @0x418d64 */
                g_nL3EventStep = 9;                                        /* @0x418d79 */
                break;                                                     /* @0x418d89 */
            }
            break;                                                         /* @0x418d6e */
        case 9:
            g_nL3MoveStep++;                                               /* @0x418dad */
            if (g_nL3MoveStep < 6) {                                       /* @0x418db6 */
                sceneObjSetPos(g_pL3BurgerObj, (0x28 - g_nL3MoveStep) * 4, /* @0x430660 @0x418dd3 */
                               0, 0, 5);
                break;                                                     /* @0x418de6 */
            }
            g_nL3EventTick = 0;                                            /* @0x418df8 */
            g_nL3EventStep = 0;                                            /* @0x418dfe */
            g_nL3EventActive = 1;                                          /* @0x418e04 */
            /* mode 2 stores the raw int channel coords into the buffer */
            sceneNodeGetPosWorld(g_pL3BurgerObj, (float *)g_anL3SpawnPos, 2); /* @0x430e80 @0x418e0a */
            pNew = (EventObject *)malloc(0x50);                            /* operator_new @0x43dd42 @0x418e11 */
            if (pNew != NULL) {                                            /* @0x418e1d */
                /* ctor args are the {flX=worldZ, flY=worldX, flHeight=worldY}
                 * ints of the BURGER pos (FILD integer loads @0x418e25..0x418e3f) */
                sceneObjCtor3(pNew, 0x1f, (float)g_anL3SpawnPos[2],        /* @0x4146a0 @0x418e47 */
                              (float)g_anL3SpawnPos[0], (float)g_anL3SpawnPos[1]);
                pNew->nValue0 = g_anL3SpawnPos[1];                        /* +0x10 @0x418e57 */
                pNew->nValue1 = (int)(uintptr_t)g_pL3BurgerObj;           /* +0x14 @0x418e68 */
                objHashRegister(pNew);                                     /* @0x4148f0 @0x418e6b */
            }
            break;                                                         /* @0x418e73 (tick++ -> 1) */
        default:
            break;                                                         /* @0x418e73 */
        }
        g_nL3EventTick++;                                                  /* @0x418e73 */
    }

    /* trailing cashier ambient: blink every other frame — even ticks step,
     * odd ticks apply (opposite phase of the presenter anims) */
    g_nL3TrailTick++;                                                      /* @0x418e9e */
    if (g_nResultsScreen == 0) {                                           /* @0x418e9f */
        if ((g_nL3TrailTick & 1) == 0) {                                   /* @0x418ea8 */
            eventAnimStep(g_pL3CashSitAnim, 1);                            /* @0x434090 @0x418ebe */
            return;                                                        /* @0x418efd */
        }
        eventAnimApply(g_pL3CashSitAnim, 1);                               /* @0x434290 @0x418ef1 */
    } else {
        if ((g_nL3TrailTick & 1) == 0) {                                   /* @0x418eda */
            eventAnimStep(g_pL3WinnerAnim, 1);                             /* @0x434090 @0x418ee3 */
            return;                                                        /* @0x418efd */
        }
        eventAnimApply(g_pL3WinnerAnim, 1);                                /* @0x434290 @0x418ef1 */
    }
}
