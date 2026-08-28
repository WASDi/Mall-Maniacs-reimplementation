#include "player.h"
#include "menu.h"

/* g_bAiEnabled @0x458358 — enables the rotating AI update scheduler.
 * Defined in config.c (set by configMasterLoad). */
extern int g_bAiEnabled;
/* g_nNavPtSearchCount @0x45d4dc — reset before every AI update pass. */
int g_nNavPtSearchCount;
/* g_nControllerIdx @0x4550d0 — chooses the pair of AI players to update. */
int g_nControllerIdx;
/* g_aPlayers @0x456524 — original fixed-stride gameplay player records. */
Player g_aPlayers[8];

/* syncAiAnimToSceneObj @0x4015a0 — retain an inactive AI player's last
 * animation state in its scene object. */
int syncAiAnimToSceneObj(Player *pPlayer)
{
    pPlayer->pSceneObject->nAnimationFrame = pPlayer->nSavedAnimationFrame;
    pPlayer->pSceneObject->nAnimationTimer = pPlayer->nSavedAnimationTimer;
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
        Player *pPlayer = &g_aPlayers[nPlayer];

        if (pPlayer->nControlType != 2) continue;
        if (nPlayer < nFirstUpdatedPlayer || nPlayer >= nFirstUpdatedPlayer + 2) {
            syncAiAnimToSceneObj(pPlayer);
        } else {
            playerAiUpdate(pPlayer);
        }
    }
    g_nControllerIdx++;
    return 1;
}