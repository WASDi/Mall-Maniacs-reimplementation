#include "../src/player.h"
#include "../src/gameplay.h"
#include "../src/menu.h"

HINSTANCE g_hAppInstance;
HWND g_hWnd;

int main(void)
{
    PlayerRecord *pRec = &g_playerRecords[0];

    memset(pRec, 0, sizeof(*pRec));
    g_nPlayerCount = 1;
    pRec->nActionSubstate = 5;
    pRec->nAiPhase = 0xf;
    pRec->nAiPhaseNext = 0xb;
    pRec->flInputTurn = 1.0f;
    pRec->flInputAccel = 1.0f;

    playerUpdateAI();
    if (pRec->nAiPhase != 0xb || pRec->nAiPhaseNext != 0xb ||
        pRec->flInputTurn != 0.0f || pRec->flInputAccel != 0.0f) return 1;

    playerUpdateAI();
    if (pRec->nAiPhase != 0 || g_nGameFrameActive != 0 ||
        g_nGameFrameActive2 != 0) return 2;
    return 0;
}