#include "../src/maniac.c"
#include "../src/gameplay.h"
#include "../src/levelselect.h"
#include "../src/menu.h"
#include "../src/player.h"
#include "../src/scene.h"
#include "../src/scene_text.h"
#include "../src/gx.h"

static int g_nFlipCalls;
static int g_nClearCalls;
static int g_nLastClearColor;

static int testFlip(void)
{
    g_nFlipCalls++;
    return 1;
}

static int testClearScreen(int nMode, int nColor)
{
    if (nMode != 1) return 0;
    g_nClearCalls++;
    g_nLastClearColor = nColor;
    return 1;
}

int main(void)
{
    SceneObjectAnimation aSceneObjects[4] = {0};
    unsigned char abGlyphs[16] = {0};
    unsigned char abTextAnim[] = {1, 1, 2, 3, 1, 2, 0, 0, 1, 0, 0, 0};
    int nPlayer;
    g_bGameActive = 0;
    g_pSceneRoot = NULL;
    g_nLevelIdx = 0;
    g_nReturnToMenu = 0;
    g_nScrollText = 1;
    g_bQuitPrompt = 0;
    g_nWorldFrameTick = 0;
    g_bAiEnabled = 1;
    g_nControllerIdx = 0;
    g_nPlayerCount = 4;
    g_nNavPtSearchCount = 9;
    for (nPlayer = 0; nPlayer < 4; nPlayer++) {
        g_aPlayers[nPlayer].pSceneObject = &aSceneObjects[nPlayer];
        g_aPlayers[nPlayer].nControlType = 2;
        g_aPlayers[nPlayer].nSavedAnimationFrame = 10 + nPlayer;
        g_aPlayers[nPlayer].nSavedAnimationTimer = 20 + nPlayer;
    }
    sceneTextAnimReset();
    if (sceneTextAnimAdd(NULL, abGlyphs, abTextAnim, sizeof(abTextAnim)) != 1) return 10;

    runCmd(0, "3");
    if (g_nLevelIdx != 3 || g_bGameActive != 1) return 1;
    if (g_nReturnToMenu != 1 || g_nScrollText != 0) return 2;

    g_driver.api.pFlip = testFlip;
    g_driver.api.pClearScreen = testClearScreen;
    g_nGameFrameActive = 0;
    g_nClearColor = 0x1234;
    gameFrameRender();
    if (g_nFlipCalls != 1 || g_nClearCalls != 1 || g_nLastClearColor != 0x1234) return 13;
    g_nGameFrameActive = 1;
    gameFrameRender();
    if (g_nFlipCalls != 1 || g_nClearCalls != 1) return 14;
    g_nGameFrameActive = 0;

    gameWorldUpdate();
    if (g_nWorldFrameTick != 1) return 3;
    if (g_nNavPtSearchCount != 0 || g_nControllerIdx != 1) return 4;
    if (aSceneObjects[2].nAnimationFrame != 12 ||
        aSceneObjects[3].nAnimationTimer != 23) return 5;
    if (abGlyphs[8] != 2 || abGlyphs[9] != 3) return 11;

    gameWorldUpdate();
    if (g_nControllerIdx != 2 || aSceneObjects[0].nAnimationFrame != 10 ||
        aSceneObjects[1].nAnimationTimer != 21) return 6;
    if (abGlyphs[8] != 5 || abGlyphs[9] != 7) return 12;

    g_bQuitPrompt = 1;
    gameWorldUpdate();
    if (g_nWorldFrameTick != 2) return 7;
    g_bQuitPrompt = 0;

    runCmd(0, "4");
    if (g_nLevelIdx != 3) return 8;

    g_bGameActive = 0;
    runCmd(0, "bad");
    if (g_bGameActive != 0 || g_nLevelIdx != 3) return 9;
    return 0;
}