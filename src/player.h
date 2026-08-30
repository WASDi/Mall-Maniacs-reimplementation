#ifndef PLAYER_H
#define PLAYER_H

#include "mstring.h"
#include "scene.h"
#include "anim.h"
#include "obj.h"

struct PlayerRecord;

/* AiController is the 0x60-byte per-player controller view embedded at
 * PlayerRecord+0x314 (array base 0x456524). aiControllersInit @0x401040
 * ctor's one per player via aiControllerCtor @0x401090, which stores the
 * owning record pointer at +0x00. */
typedef struct AiController {
    struct PlayerRecord *pPlayerObj; /* +0x00 back-pointer to the record (ctor arg) */
    int nAiState;                    /* +0x04 state-machine state */
    void *pNavPoint;                 /* +0x08 nav-point search result (0x40f1b0) */
    int field_0c;                    /* +0x0c */
    int field_10;                    /* +0x10 */
    float field_14;                  /* +0x14 float consumed by playerAiUpdate */
    float field_18;                  /* +0x18 float */
    int field_1c;                    /* +0x1c */
    int field_20;                    /* +0x20 vec2 out of 0x409a10 */
    int field_24;                    /* +0x24 */
    int field_28;                    /* +0x28 result of 0x409ad0 */
    int field_2c;                    /* +0x2c vec2 (target pos for 0x401800/0x401ae0) */
    int field_30;                    /* +0x30 */
    int field_34;                    /* +0x34 vec2 out of 0x40f1b0 */
    int field_38;                    /* +0x38 */
    int field_3c;                    /* +0x3c */
    int field_40;                    /* +0x40 list id stored by the 0x40f1b0 search */
    int nCtrlSpeed;                  /* +0x44 g_aflAiCtrlSpeed[g_nModeSel] (difficulty) */
    int nSavedAnimationFrame;        /* +0x48 saved animation frame (sync) */
    int nSavedAnimationTimer;        /* +0x4c saved animation timer (sync) */
    unsigned char bFlag50;           /* +0x50 */
    unsigned char bFlag51;           /* +0x51 */
    unsigned char bFlag52;           /* +0x52 */
    unsigned char _pad53;            /* +0x53 */
    int field_54;                    /* +0x54 */
    int field_58;                    /* +0x58 */
    int field_5c;                    /* +0x5c */
} AiController;                      /* 0x60 */

typedef char AiControllerSizeMustBe0x60[(sizeof(AiController) == 0x60) ? 1 : -1];

/* PlayerRecord is the 0x374-byte gameplay player record at
 * g_playerRecords @0x456210. The controller view lives at +0x314
 * (g_playerRecords[i].ai == original base 0x456524 + i*0x374). Layout
 * verified against playerSetupRound @0x410e90 and playerSetupSceneObjects
 * @0x411550 disassembly. */
typedef struct PlayerRecord {
    MString mstrCharacterName;   /* +0x00 objects/characters[%d]/object_name */
    MString mstrCartName;        /* +0x08 objects/carts[%d]/object_name */
    SceneNode *pCharSceneNode;   /* +0x10 char scene node (SPLASH shadow parent) */
    SceneNode *pCartSceneObj;    /* +0x14 cart scene object */
    int field_18;                /* +0x18 */
    int field_1c;                /* +0x1c */
    SceneNode *pCharShadowNode;  /* +0x20 char SPLASH shadow node (hidden, mode 2) */
    SceneNode *pCartShadowNode;  /* +0x24 cart SPLASH shadow node (hidden, mode 2) */
    int field_28[2];             /* +0x28 */
    SceneNode *pCharSceneObj;    /* +0x30 char scene object */
    SceneNode *apMeshSlots[3];   /* +0x34 char mesh + up to 2 "_%d<name>" sub-meshes */
    int field_40;                /* +0x40 */
    SceneNode *pCartChildA;      /* +0x44 cart child node (sceneNodeAllocChild) */
    SceneNode *pCartChildB;      /* +0x48 cart child node */
    char szCharName[0x100];      /* +0x4c unbounded strcpy of g_apCharNames[idx]
                                  * (netIsActive()==0 path); spans 0x4c..0x14b */
    int nStartPosIdx;            /* +0x14c start_positions[%d] index (player slot) */
    int nCharIdx;           /* +0x150 selected character 0..9 */
    int nCartIdx;                /* +0x154 selected cart 0..23 */
    unsigned char bStateFlags;   /* +0x158 &0x20 = heading to checkout (HUD) */
    unsigned char _pad159[3];
    int nScoreTicks;             /* +0x15c score/time ticks (HUD timer, results) */
    int nStatSpeed;              /* +0x160 g_kCharStatSpeed[nCharIdx] */
    int nStatStrength;           /* +0x164 g_kCharStatStrength */
    int nStatAgility;            /* +0x168 g_kCharStatAgility */
    int nCheckoutProgress;       /* +0x16c checkout bar fill 0..100 (HUD) */
    int field_170_pad[1];        /* +0x170 */
    int field_174;               /* +0x174 round gate: nonzero arms the win
                                  * checks in roundLogicUpdate; mode 4 also
                                  * uses it as the checkpoint-stage flag */
    int field_178_pad[1];        /* +0x178 */
    int nHeldItemId;             /* +0x17c */
    int nListProgress;           /* +0x180 reset each round (checked ==5 in mode 3 AI) */
    int anListIds[10];           /* +0x184 shopping list item ids */
    int abListTaken[10];         /* +0x1ac taken flags */
    int field_1d4_pad[2];        /* +0x1d4 */
    void *pQuestMessage;         /* +0x1dc frog message block (+0x10 = text, HUD) */
    int field_1e0_pad[2];        /* +0x1e0 */
    /* +0x1e8 walk physics block (16 dwords; refreshed from the shared block) */
    float flAccSpeed;            /* +0x1e8 (speed*0.7+100)*[master]/acc */
    float flFriction;            /* +0x1ec [master]/friction */
    float flAccFric;             /* +0x1f0 max per-frame speed step acc/(1-fric) */
    float flCurSpeed;            /* +0x1f4 runtime walk speed channel (setup: friction copy) */
    float flRotAccSpeed;         /* +0x1f8 (agility*0.1+0.5)*[master]/rotate_acc */
    float flRotAccFric;          /* +0x1fc /(1-flFriction) */
    float field_200_pad;         /* +0x200 */
    float flTurnAccum;           /* +0x204 walk-node turn accumulator */
    float flVertVel;             /* +0x208 walk-node vertical velocity */
    float field_20c_pad;         /* +0x20c */
    float field_210_pad;         /* +0x210 */
    GxVec2 vAccPolar;            /* +0x214 scratch accel polar {len, heading=+0x218 target} */
    GxVec2 vVelPolar;            /* +0x21c walk-node velocity {len, heading} */
    void *pSubObjA;              /* +0x224 block dword 15 (WorldNode*, parent = name) */
    /* +0x228 shared physics block (16 dwords; source of both block copies) */
    float field_228_pad;         /* +0x228 (zeroed) */
    float flAccFactor;           /* +0x22c on-foot pos-node accel factor (zeroed) */
    float flCurAccFric;          /* +0x230 copy of flAccFric */
    float flPosSpeed;            /* +0x234 pos-node speed channel (setup: carts friction 1) */
    float field_238_pad;         /* +0x238 (zeroed) */
    float flRotFactor;           /* +0x23c on-foot pos-node turn factor (zeroed) */
    float flCurRotAccFric;       /* +0x240 copy of flRotAccFric */
    float flPosTurnAccum;        /* +0x244 pos-node turn accumulator (setup: carts friction 2) */
    float field_248;             /* +0x248 (zeroed) */
    short wPosPitch;             /* +0x24c pos-mesh orient yaw channel (support tilt) */
    short _pad24e;               /* +0x24e */
    short wPosRoll;              /* +0x250 pos-mesh orient roll channel */
    short _pad252;               /* +0x252 */
    float field_254;             /* +0x254 (zeroed) */
    float flPosHeading;          /* +0x258 pos-node target heading */
    float flPosVelLen;           /* +0x25c pos-node velocity length (RotateAdd result) */
    float flPosVelAng;           /* +0x260 pos-node velocity heading */
    void *pSubObjB;              /* +0x264 block dword 15 (WorldNode*, no parent) */
    /* +0x268 cart physics block (16 dwords) */
    float flCartAccSpeed;        /* +0x268 (speed*0.7+100)*[master]/acc_WC */
    float field_26c_pad;         /* +0x26c cart friction (setup) */
    float flCartFriction;        /* +0x270 avg(flFriction, flCartFrictionA) */
    float flCartCurSpeed;        /* +0x274 runtime cart speed channel (setup: acc/(1-fric)) */
    float flCartRotAccSpeed;     /* +0x278 (agility*0.1+0.5)*[master]/rotate_acc_WC */
    float flCartFrictionB;       /* +0x27c setup: avg friction; runtime: turn-accum coefficient */
    float flCartRotAccFric;      /* +0x280 setup: rot acc/(1-fric); runtime: turn-step bound */
    float flCartTurnAccum;       /* +0x284 cart-node turn accumulator */
    float flCartBounceIn;        /* +0x288 bounce impulse input (zeroed by cart physics) */
    short wCartPitch;            /* +0x28c cart-mesh orient yaw channel */
    short _pad28e;               /* +0x28e */
    short wCartRoll;             /* +0x290 cart-mesh orient roll channel */
    GxVec2 vCartAccPolar;        /* +0x294 scratch accel polar {len, heading=+0x298 target} */
    GxVec2 vCartVelPolar;        /* +0x29c cart-node velocity {len, heading} */
    void *pSubObjC;              /* +0x2a4 block dword 15 (WorldNode*, parent = name) */
    AnmSet *apAnmSets[11];       /* +0x2a8 pick1,pick2,flpick1,flpick2,throw1,throw2,
                                  *       run,stand,grab,oops,winner; +0x2c4 (= stand)
                                  *       is the set stepped by roundStartInit */
    int field_2d4;               /* +0x2d4 (AI: current anim set pointer) */
    int nChannelsDirty;          /* +0x2d8 gameUpdate sync gate (nonzero = re-sync meshes) */
    int nControlType;            /* +0x2dc 2 = AI-driven (playerUpdateDispatch gate) */
    float flInputTurn;           /* +0x2e0 turn impulse channel (float; net anim-sync
                                  * reuse copies raw dwords into it, dead offline) */
    float flInputAccel;          /* +0x2e4 accel impulse channel (float; same reuse) */
    int field_2e8;               /* +0x2e8 zeroed */
    int _pad2ec;                 /* +0x2ec untouched */
    int field_2f0;               /* +0x2f0 zeroed */
    int field_2f4_pad[2];        /* +0x2f4 */
    int field_2fc;               /* +0x2fc zeroed */
    int field_300_pad[3];        /* +0x300 */
    int nNetReady;               /* +0x30c ==1 while waiting for peers (HUD) */
    int nNetFlags;               /* +0x310 &0x100 = peer announced ready
                                  * (roundLogicUpdate server handshake) */
    AiController ai;             /* +0x314 controller view (original base 0x456524) */
} PlayerRecord;                   /* 0x374 */

typedef char PlayerRecordSizeMustBe0x374[(sizeof(PlayerRecord) == 0x374) ? 1 : -1];

extern PlayerRecord g_playerRecords[8];  /* @0x456210 */

/* camera-follow / results globals */
extern int g_nLocalPlayerIdx;    /* @0x458104 local (human) player slot */
extern int g_nResultsScreen;     /* @0x458130 nonzero while results screen runs */
extern int g_nCameraUpdateTick;  /* @0x458948 camera-follow scheduler tick — defined in player_camera.c */
extern int g_nCurrentItemId;     /* @0x458128 mode 3 target item id (HUD) — defined in player_setup.c */

/* 4-byte object ids used by cameraFollowUpdate: "c_ac" camera-active zone
 * (snap target, @0x44e20c) and "c_di" camera-distance limiter (@0x44e204). */
#define OBJ_ID_C_AC 0x63615f63   /* 'c_ac' */
#define OBJ_ID_C_DI 0x69645f63   /* 'c_di' */

/* ThrownItemStub — minimal view of a g_pThrownItemHead list entry for
 * gameUpdate's walks (next link at +0x04). The full 0x30-byte layout
 * lands with the thrown-item cluster (playerThrowItemCtor @0x40f720). */
typedef struct ThrownItemStub {
    int field_00;                    /* +0x00 */
    struct ThrownItemStub *pNext;    /* +0x04 next list entry */
} ThrownItemStub;

/* Per-subsystem headers — umbrella re-export so consumers that include
 * player.h get all player APIs without changing include lists. Guarded
 * includes avoid circular dependency (sub-headers use forward decls). */
#include "player_ai.h"
#include "player_setup.h"
#include "player_camera.h"
#include "player_physics.h"

/* playerAiUpdate is a deferred stub (stubs.h) but historically declared
 * here for the AI dispatch. Keep forward decl for callers that only
 * include player.h. */
struct AiController;
void playerAiUpdate(struct AiController *pCtrl);       /* @0x401160 — stub in stubs.h */

#endif /* PLAYER_H */
