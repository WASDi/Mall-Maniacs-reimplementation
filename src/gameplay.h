#ifndef GAMEPLAY_H
#define GAMEPLAY_H

#include <windows.h>

/* Gameplay entry and frame driver. The original WinMain @0x4160a0 calls
 * gameRunFrame instead of gameFrameUpdate while g_bGameActive is set. */
extern int g_bGameActive;       /* @0x4580f8 */
extern int g_nReturnToMenu;     /* @0x45810c */
extern int g_nScrollText;       /* @0x4580ec */
extern int g_nGameFrameActive;  /* @0x45834c */
extern int g_nGameTime;         /* @0x4580d4 */
extern int g_nObjUpdateTime;    /* @0x4580d0 */
extern int g_nRoundStartTime;   /* @0x4580cc */
extern int g_bQuitPrompt;       /* @0x45812c */
extern int g_nGamePhase;        /* @0x458124 countdown phase (HUD; -1 = "Gå!!") */
extern int g_nWinnerIdx;        /* @0x458134 results-screen winner slot */
extern unsigned char g_bGameRunning; /* @0x44fdc4 round live flag (HUD/results gate) */
extern int g_nWorldFrameTick;   /* @0x458944 */
extern int g_nClearColor;       /* @0x45892c */
extern int g_bCollisionEnabled; /* @0x458354 */
extern int g_nMovieRecord;      /* @0x455e8c */
extern int g_nMoviePlay;        /* @0x455e90 */

int runCmd(int nContext, LPCSTR pszArgs); /* @0x4084c0 */
void roundStartInit(void);                /* @0x40a4d0 */
void gameRunFrame(int forceRender);       /* @0x40ad80 */
void gameWorldUpdate(void);               /* @0x40b3d0 */
void gameObjectUpdate(void);              /* @0x40cf40 */
void gameFrameRender(void);               /* @0x40ae30 */

#endif /* GAMEPLAY_H */