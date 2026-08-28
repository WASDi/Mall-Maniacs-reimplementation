#ifndef PLAYER_H
#define PLAYER_H

/* SceneObjectAnimation is the portion of the scene object accessed by the
 * player scheduler. The animation frame and timer are at 0x2e0/0x2e4. */
typedef struct SceneObjectAnimation {
    char abBeforeAnimation[0x2e0];
    int nAnimationFrame;
    int nAnimationTimer;
} SceneObjectAnimation;

/* Player is the 0x374-byte gameplay player record at g_aPlayers @0x456524.
 * Only fields reached by the current player-update increment are named. */
typedef struct Player {
    SceneObjectAnimation *pSceneObject;
    int nAiState;
    void *pNavPoint;
    char abBeforeSavedAnimation[0x3c];
    int nSavedAnimationFrame;
    int nSavedAnimationTimer;
    char abBeforeControlType[0x13c];
    int nControlType;
    char abUnused[0x1e4];
} Player;

typedef char PlayerSizeMustBe0x374[(sizeof(Player) == 0x374) ? 1 : -1];

extern int g_bAiEnabled;          /* @0x458358 */
extern int g_nNavPtSearchCount;   /* @0x45d4dc */
extern int g_nControllerIdx;      /* @0x4550d0 */
extern Player g_aPlayers[8];      /* @0x456524 */

int playerUpdateDispatch(void);                 /* @0x4010e0 */
int syncAiAnimToSceneObj(Player *pPlayer);      /* @0x4015a0 */
void playerAiUpdate(Player *pPlayer);           /* @0x401160 */

#endif /* PLAYER_H */