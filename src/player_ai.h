#ifndef PLAYER_AI_H
#define PLAYER_AI_H

/* player_ai.h — AI controller view + dispatch (maniac.exe 0x401xxx). */

struct AiController;
struct PlayerRecord;
struct EventObject;

/* Globals */
extern int g_nNavPtSearchCount;        /* @0x45d4dc */
extern int g_nControllerIdx;           /* @0x4550d0 */
extern const float g_aflAiCtrlSpeed[3];/* @0x44b230 */

/* playerUpdateAI @0x40b510 — the AI action phase machine: advances every
 * player's nAiPhase (+0x2f0) based on nActionSubstate (+0x2e8), including
 * the 0xf timer wait, cart approach/steer, item pickup (modes 1..3), the
 * MCDMAN burger delivery (phase 0xa/0xb) and the input-impulse damping. */
void playerUpdateAI(void);                            /* @0x40b510 */

/* AI movement / pickup cluster (0x40e180..0x40f760) */
int playerFindCart(PlayerRecord *pRec);                             /* @0x40e180 */
int aiStateCartApproach(PlayerRecord *pRec, int *pAnimState);        /* @0x40e1d0 */
void playerUpdateOrientToTurret(PlayerRecord *pRec);                 /* @0x40e5b0 */
int playerCheckTurn(PlayerRecord *pRec);                             /* @0x40e670 */
int playerAiGrabItem(PlayerRecord *pRec);                            /* @0x40ea20 */
EventObject *playerCheckBlocked(PlayerRecord *pRec);                 /* @0x40ec40 */
EventObject *playerFindNearestTarget(PlayerRecord *pRec);            /* @0x40ed10 */
int aiStateTurnToBlocked(PlayerRecord *pRec);                        /* @0x40ef60 */
int playerCollectItem(PlayerRecord *pRec, int nItemId, void *pThrownMesh); /* @0x40f1f0 */
int aiCollectItem(PlayerRecord *pRec, int nSlot);                    /* @0x40f420 */
int playerCheckTargetRange(PlayerRecord *pRec, int nSlot);           /* @0x40f4d0 */
int aiCheckItemRange(PlayerRecord *pRec, int nSlot);                /* @0x40f5e0 */

/* --- thrown-item cluster (0x40f720..0x40fde0) --- */
extern ThrownItem *g_pThrownItemHead;   /* @0x45896c newest item */
extern ThrownItem *g_pThrownItemTail;   /* @0x458970 oldest item */
extern int g_nThrownItemCount;          /* @0x458974 */

/* playerThrowItemCtor @0x40f720 — finish a thrown shopping item: allocate
 * the 0x48 physics WorldNode (worldNodeCtor), add its child-mesh + turret
 * entry keyed by pMesh (extents 150/2/500, channel values 20/20), place it
 * at {flNodeX, flNodeY, flNodeZ} (original arg order is Z, X, Y-1000),
 * then fill the record: +0x14 = flSpeed, +0x18 = flHeading, +0x20 = itemId,
 * +0x24 = pMesh, +0x2c = nOwnerIdx, +0x0c = 0, entry->flVertVel (+0x44) =
 * flVertVel and g_playerRecords[nOwnerIdx].nLastThrownItemId (+0x1e4) =
 * nItemId. Caps the list at 10 items (frees the oldest). */
ThrownItem *playerThrowItemCtor(ThrownItem *pItem, int nItemId, float flNodeZ,
                                float flNodeX, float flNodeY, float flSpeed,
                                float flHeading, float flVertVel,
                                SceneNode *pMesh, int nOwnerIdx);   /* @0x40f720 */

/* thrownItemFree @0x40f8d0 — unlink pItem from the g_pThrownItemHead/Tail
 * list, sceneNodeFree the mesh (+0x24), objDtor+free the world node
 * (+0x10) and decrement g_nThrownItemCount. */
void thrownItemFree(ThrownItem *pItem);                             /* @0x40f8d0 */

/* itemThrowUpdate @0x40f950 — gameUpdate pass 1: integrate one thrown item
 * against its child-mesh raycast surface (gravity +18/frame, ×-0.4 ground
 * bounce with sfx 0x14/0x6e, 0.5-speed landing), and once at rest register
 * the pickup EventObject (sceneObjCtor3 + objHashRegister) and free the
 * world node. Then the per-player proximity pickup scan. */
void itemThrowUpdate(ThrownItem *pItem);                            /* @0x40f950 */

/* itemMeshFollowUpdate @0x40fde0 — gameUpdate pass 2: clamp the item's
 * +0x14 height channel against the node's last polar length (+0x3c),
 * adopt the node's +0x38 heading when +0x18 is set, and reposition the
 * mesh onto the node (mode-5 orient + mode-2 pos, height -200). */
void itemMeshFollowUpdate(ThrownItem *pItem);                       /* @0x40fde0 */

/* Console "action <cmd>" handler (commandDispatch table @0x44b308). */
int actionCmd(int nContext, LPCSTR pszArgs);          /* @0x4067c0 */

/* AI movement / pickup cluster (0x40e180..0x40f760) */
int syncAiAnimToSceneObj(struct AiController *pCtrl);               /* @0x4015a0 */
int playerUpdateDispatch(void);                                      /* @0x4010e0 */
int aiControllersInit(void);                                         /* @0x401040 */
int aiControllerCtor(struct AiController *pCtrl, struct PlayerRecord *pRecord); /* @0x401090 */
void moveStateSetSnapFlag(struct AiController *pCtrl);               /* @0x4020c0 */

#endif /* PLAYER_AI_H */
