#ifndef PLAYER_AI_H
#define PLAYER_AI_H

/* player_ai.h — AI controller view + dispatch (maniac.exe 0x401xxx). */

struct AiController;
struct PlayerRecord;

/* Globals */
extern int g_nNavPtSearchCount;        /* @0x45d4dc */
extern int g_nControllerIdx;           /* @0x4550d0 */
extern const float g_aflAiCtrlSpeed[3];/* @0x44b230 */

/* AI controller lifecycle + dispatch */
int syncAiAnimToSceneObj(struct AiController *pCtrl);               /* @0x4015a0 */
int playerUpdateDispatch(void);                                      /* @0x4010e0 */
int aiControllersInit(void);                                         /* @0x401040 */
int aiControllerCtor(struct AiController *pCtrl, struct PlayerRecord *pRecord); /* @0x401090 */
void moveStateSetSnapFlag(struct AiController *pCtrl);               /* @0x4020c0 */

#endif /* PLAYER_AI_H */
