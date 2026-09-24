#ifndef PLATFORM_SDL2_H
#define PLATFORM_SDL2_H

#include <stdio.h>

/* SDL2 platform layer (Phase 2 of cross_platform_plan.md).
 * Owns the window, GL context, event pump, clock, asset/pref paths and
 * error dialog. Game logic never calls SDL directly; maniac.c/time.c/
 * util.c/game.c go through these entry points.
 */

int platformInit(int argc, char **argv);
void platformShutdown(void);
void *platformWindow(void);
void *platformGLContext(void);

/* Event pump: maps SDL scancodes to g_abInputKeyHeld channels
 * (Right 0 / Left 1 / Up 2 / Down 3 / Space 4 / Enter 6 / Escape 7),
 * routes SDL_TEXTINPUT as WindowProc @0x4161b0 WM_CHAR did, handles
 * focus lost/gained as WM_ACTIVATE @0x4161f6, and returns 0 when the
 * app should quit. */
int platformPumpEvents(void);

/* Push a quit event (replaces PostQuitMessage(0) in stateQuitConfirm). */
void platformRequestQuit(void);

/* Clock: SDL_GetTicks-based (see time.c getGameTime @0x40dfe0). */
unsigned int platformTicks(void);
void platformSleep(unsigned int ms);

/* Paths: read-only asset lookup vs writable user config/temp data. */
const char *platformAssetDir(void);
const char *platformPrefDir(void);
int platformAssetPath(const char *rel, char *out, unsigned int outSize);

/* Case-insensitive asset open (Windows parity): the original ran on a
 * case-insensitive filesystem, but config/.sen literals (e.g.
 * 'Scene_ica\mall1_ica.sen') don't match disk casing
 * ('scene_ica/MALL1_ICA.SEN'). Resolves each path component
 * case-insensitively (exact match preferred at every level, otherwise the
 * first readdir match). Read-only fallback — never used for writes. */
FILE *platformFopenCI(const char *path, const char *mode);

/* Dialog: SDL_ShowSimpleMessageBox + stderr (see fatalError @0x414570). */
void platformShowError(const char *title, const char *msg);

#endif /* PLATFORM_SDL2_H */
