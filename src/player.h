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
    int field_174;               /* +0x174 set 1 per player (input gate in playerAiUpdate) */
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
    float flAccFric;             /* +0x1f0 flAccSpeed/(1-flFriction) */
    float flFrictionB;           /* +0x1f4 duplicate friction */
    float flRotAccSpeed;         /* +0x1f8 (agility*0.1+0.5)*[master]/rotate_acc */
    float flRotAccFric;          /* +0x1fc /(1-flFriction) */
    float field_200_pad[9];      /* +0x200 */
    void *pSubObjA;              /* +0x224 block dword 15 (WorldNode*, parent = name) */
    /* +0x228 shared physics block (16 dwords; source of both block copies) */
    float field_228_pad[2];      /* +0x228, +0x22c (zeroed) */
    float flCurAccFric;          /* +0x230 copy of flAccFric */
    float flCartFrictionA;       /* +0x234 carts[%d]/friction read 1 (zeroed) */
    float field_238_pad[2];      /* +0x238, +0x23c (zeroed) */
    float flCurRotAccFric;       /* +0x240 copy of flRotAccFric */
    float flCartFrictionB;       /* +0x244 carts[%d]/friction read 2 (zeroed) */
    float field_248;             /* +0x248 (zeroed) */
    float field_24c_pad[2];      /* +0x24c, +0x250 */
    float field_254;             /* +0x254 (zeroed) */
    float field_258;             /* +0x258 (zeroed) */
    float field_25c;             /* +0x25c (zeroed) */
    float field_260;             /* +0x260 (zeroed) */
    void *pSubObjB;              /* +0x264 block dword 15 (WorldNode*, no parent) */
    /* +0x268 cart physics block (16 dwords) */
    float flCartAccSpeed;        /* +0x268 (speed*0.7+100)*[master]/acc_WC */
    float field_26c_pad;         /* +0x26c */
    float flCartFriction;        /* +0x270 avg(flFriction, flCartFrictionA) */
    float flCartAccFric;         /* +0x274 flCartAccSpeed/(1-flCartFriction) */
    float flCartRotAccSpeed;     /* +0x278 (agility*0.1+0.5)*[master]/rotate_acc_WC */
    float flCartFrictionB2;      /* +0x27c avg(flFriction, flCartFrictionB) */
    float flCartRotAccFric;      /* +0x280 /(1-flCartFrictionB2) */
    float field_284_pad[8];      /* +0x284 */
    void *pSubObjC;              /* +0x2a4 block dword 15 (WorldNode*, parent = name) */
    AnmSet *apAnmSets[11];       /* +0x2a8 pick1,pick2,flpick1,flpick2,throw1,throw2,
                                  *       run,stand,grab,oops,winner; +0x2c4 (= stand)
                                  *       is the set stepped by roundStartInit */
    int field_2d4_pad[2];        /* +0x2d4 */
    int nControlType;            /* +0x2dc 2 = AI-driven (playerUpdateDispatch gate) */
    int nAnimationFrame;         /* +0x2e0 animation frame (sync target) */
    int nAnimationTimer;         /* +0x2e4 animation timer (sync target) */
    int field_2e8;               /* +0x2e8 zeroed */
    int _pad2ec;                 /* +0x2ec untouched */
    int field_2f0;               /* +0x2f0 zeroed */
    int field_2f4_pad[2];        /* +0x2f4 */
    int field_2fc;               /* +0x2fc zeroed */
    int field_300_pad[3];        /* +0x300 */
    int nNetReady;               /* +0x30c ==1 while waiting for peers (HUD) */
    int field_310_pad[1];        /* +0x310 (0x310..0x313 untouched) */
    AiController ai;             /* +0x314 controller view (original base 0x456524) */
} PlayerRecord;                   /* 0x374 */

typedef char PlayerRecordSizeMustBe0x374[(sizeof(PlayerRecord) == 0x374) ? 1 : -1];

extern PlayerRecord g_playerRecords[8];  /* @0x456210 */

/* camera-follow / results globals */
extern int g_nLocalPlayerIdx;    /* @0x458104 local (human) player slot */
extern int g_nResultsScreen;     /* @0x458130 nonzero while results screen runs */
extern int g_nCameraUpdateTick;  /* @0x458948 camera-follow scheduler tick */
extern int g_nCurrentItemId;     /* @0x458128 mode 3 target item id (HUD) */

/* 4-byte object ids used by cameraFollowUpdate: "c_ac" camera-active zone
 * (snap target, @0x44e20c) and "c_di" camera-distance limiter (@0x44e204). */
#define OBJ_ID_C_AC 0x63615f63   /* 'c_ac' */
#define OBJ_ID_C_DI 0x69645f63   /* 'c_di' */

int playerUpdateDispatch(void);                 /* @0x4010e0 */
int syncAiAnimToSceneObj(AiController *pCtrl);  /* @0x4015a0 */
void playerAiUpdate(AiController *pCtrl);       /* @0x401160 */

/* aiControllersInit @0x401040 — aiControllerCtor one controller per player
 * and reset the rotating AI update index g_nControllerIdx. */
int aiControllersInit(void);                    /* @0x401040 */

/* aiControllerCtor @0x401090 — zero the controller state, store the owning
 * record pointer and the difficulty-scaled AI speed. Returns 1. */
int aiControllerCtor(AiController *pCtrl, PlayerRecord *pRecord); /* @0x401090 */

/* playerSetupRound @0x410e90 — per-player round setup from [master] and
 * objects/characters[%d]/carts[%d] config: physics constants, shopping
 * list, names, stats and the three WorldNode sub-objects, then
 * aiControllersInit. */
void playerSetupCharacters(void);               /* @0x41b6e0 */
void playerSetupRound(void);                    /* @0x410e90 */

/* playerSetupSceneObjects @0x411550 — create the per-player scene objects
 * (character + cart nodes, meshes, animation sets), the VAGNPIL/VARUPIL
 * arrow objects, the game scene root with camera nodes and the menu-scene
 * root for the return path. */
void playerSetupSceneObjects(void);             /* @0x411550 */

/* levelObjectsCartsCameraInit @0x411b70 — per-player cart/character world
 * placement from config object_pos/start_positions (TODO: the objTurretAdd /
 * nodeAddChildMesh pass) followed by the unconditional camera setup:
 * cameraSetClassMeshes, then place g_pCamPosNode/g_pCamAimNode from config
 * objects/camera pos/aim, copy the position/orientation onto the camera
 * root and load rot_speed/rot_max into the camera-follow block. */
void levelObjectsCartsCameraInit(void);         /* @0x411b70 */

/* cameraSetClassMeshes @0x4023b0 — attach the followed node's class mesh to
 * the camera pos node and the camera root and store it as the block's
 * follow node (block +4, 0x4588fc). */
void cameraSetClassMeshes(CameraFollowBlock *pBlk, SceneNode *pMesh); /* @0x4023b0 */

/* cameraFollowUpdate @0x4020d0 — per-tick camera follow: pick the target
 * position from the camera pos node or a c_ac zone snap, avoid zone walls
 * (TODO), smooth x/z/y toward the target (snap inside nSnapDist, else
 * delta/nDiv), clamp the height against c_di segments (TODO) and finally
 * set the camera root position and face the aim node's world position. */
void cameraFollowUpdate(CameraFollowBlock *pBlk); /* @0x4020d0 */

#endif /* PLAYER_H */
