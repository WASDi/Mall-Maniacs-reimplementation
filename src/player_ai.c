#include <string.h>

#include "player.h"
#include "player_ai.h"
#include "config.h"
#include "stubs.h"
#include "options.h"

/* g_nNavPtSearchCount @0x45d4dc — reset before every AI update pass. */
int g_nNavPtSearchCount;
/* g_nControllerIdx @0x4550d0 — chooses the pair of AI players to update. */
int g_nControllerIdx;

/* g_aflAiCtrlSpeed @0x44b230 — difficulty-scaled AI speed picked by
 * aiControllerCtor (indexed by g_nModeSel 0..2). */
const float g_aflAiCtrlSpeed[3] = { 0.72f, 0.84f, 0.96f };

/* syncAiAnimToSceneObj @0x4015a0 — retain an inactive AI player's last
 * animation state in its player record. */
int syncAiAnimToSceneObj(AiController *pCtrl)
{
    int nFrame = pCtrl->nSavedAnimationFrame;      /* raw dword copy into the float channel */
    int nTimer = pCtrl->nSavedAnimationTimer;
    memcpy(&pCtrl->pPlayerObj->flInputTurn, &nFrame, 4);   /* +0x2e0 */
    memcpy(&pCtrl->pPlayerObj->flInputAccel, &nTimer, 4);  /* +0x2e4 */
    return 1;
}

/* playerUpdateDispatch @0x4010e0 — update the selected pair of AI players
 * and synchronize every other AI player's saved animation state. */
int playerUpdateDispatch(void)
{
    int nPlayer;
    int nFirstUpdatedPlayer;

    if (g_bAiEnabled == 0) return 0;

    g_nNavPtSearchCount = 0;
    nFirstUpdatedPlayer = (g_nControllerIdx % 4) * 2;
    for (nPlayer = 0; nPlayer < g_nPlayerCount; nPlayer++) {
        PlayerRecord *pRecord = &g_playerRecords[nPlayer];

        if (pRecord->nControlType != 2) continue;
        if (nPlayer < nFirstUpdatedPlayer || nPlayer >= nFirstUpdatedPlayer + 2) {
            syncAiAnimToSceneObj(&pRecord->ai);
        } else {
            playerAiUpdate(&pRecord->ai);
        }
    }
    g_nControllerIdx++;
    return 1;
}

/* aiControllersInit @0x401040 — ctor every player's AiController (record
 * +0x314) with the record itself as the back-pointer argument, then reset
 * the rotating update index g_nControllerIdx @0x4550d0. */
int aiControllersInit(void) /* @0x401040 */
{
    int i;

    for (i = 0; i < g_nPlayerCount; i++) {
        aiControllerCtor(&g_playerRecords[i].ai, &g_playerRecords[i]);
    }
    g_nControllerIdx = 0;
    return 1;
}

/* aiControllerCtor @0x401090 — zero the controller state, store the owning
 * record at +0x00 and the difficulty speed at +0x44. Returns 1. */
int aiControllerCtor(AiController *pCtrl, PlayerRecord *pRecord) /* @0x401090 */
{
    pCtrl->nAiState = 0;
    pCtrl->pNavPoint = 0;
    pCtrl->field_0c = 0;
    pCtrl->field_10 = 0;
    pCtrl->pPlayerObj = pRecord;
    pCtrl->nSavedAnimationFrame = 0;
    pCtrl->nSavedAnimationTimer = 0;
    pCtrl->bFlag50 = 0;
    pCtrl->bFlag51 = 0;
    pCtrl->bFlag52 = 0;
    pCtrl->field_54 = 0;
    pCtrl->field_58 = 0;
    pCtrl->field_5c = 0;
    pCtrl->nCtrlSpeed = (int)g_aflAiCtrlSpeed[g_nModeSel];
    return 1;
}

/* moveStateSetSnapFlag @0x4020c0 — raise the controller's zone-snap flag
 * (+0x51) after a teleport pad moved the node. */
void moveStateSetSnapFlag(AiController *pCtrl) /* @0x4020c0 */
{
    pCtrl->bFlag51 = 1;                             /* +0x51 @0x4020c4 */
}
