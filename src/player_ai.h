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
int aiCheckItemRange(PlayerRecord *pRec, int nSlot);                 /* @0x40f5e0 */

/* Console "action <cmd>" handler (commandDispatch table @0x44b308). */
int actionCmd(int nContext, LPCSTR pszArgs);          /* @0x4067c0 */

/* AI movement / pickup cluster (0x40e180..0x40f760) */
int syncAiAnimToSceneObj(struct AiController *pCtrl);               /* @0x4015a0 */
int playerUpdateDispatch(void);                                      /* @0x4010e0 */
int aiControllersInit(void);                                         /* @0x401040 */
int aiControllerCtor(struct AiController *pCtrl, struct PlayerRecord *pRecord); /* @0x401090 */
void moveStateSetSnapFlag(struct AiController *pCtrl);               /* @0x4020c0 */

#endif /* PLAYER_AI_H */
