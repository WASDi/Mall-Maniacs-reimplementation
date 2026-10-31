#ifndef CINEMATIC_H
#define CINEMATIC_H

/* cinematic.h — offline trailer capture mode (PORT ADDITION, no original
 * counterpart; see Trailer.md). Compiled only with -DMANIAC_CINEMATIC=ON.
 *
 * `maniac_rebuild --cinematic <shot.txt> [--out <clip.mkv>]` starts a
 * round on the shot's level, drives the game from a virtual clock (one
 * 25 fps video frame per rendered frame, independent of wall time),
 * hides the HUD and pipes every captured frame into ffmpeg as a lossless
 * FFV1 clip. Hooks sit at the existing seams: clock (platformTicks),
 * window/events (platform_sdl2.c), frame read-back (gx_sdl2_gl.c glFlip),
 * HUD (gameFrameRender) and the WinMain loop (maniac.c). With shot keys
 * the camera follows a keyframed spline; `--freecam` opens the shot
 * interactively to fly the camera and record keys (see cinematic.c). */

/* Parse --cinematic/--out and the shot file, export its env lines. Must
 * run before platformInit (window size, SDL_AUDIODRIVER, MANIAC_* knobs).
 * Returns 1 when cinematic mode is active, 0 when not requested, -1 on
 * error. */
int cinematicInit(int argc, char **argv);
int cinematicActive(void);
/* Nonzero in --freecam (interactive) mode. */
int cinematicFreecam(void);

/* Window size (drawable pixels): the shot's capture size, or a smaller
 * preview with the same aspect in freecam mode. */
void cinematicWindowSize(int *pnWidth, int *pnHeight);

/* Virtual clock in milliseconds, returned by platformTicks. */
unsigned int cinematicTicks(void);

/* Called at the start of gameFrameRender: applies the shot's staging
 * (actor/cart/item placement and poses) and overrides the camera pose. */
void cinematicSceneApply(void);

/* Called right after zoneConnUpdateCulling: a shot with `zones all`
 * shows every zone (aerial / high shots outside the zone volumes). */
void cinematicZonesOverride(void);

/* Nonzero while renderGameHud must be skipped. */
int cinematicHideHud(void);

/* Bracket sceneRender in gameFrameRender: hide the in-world guidance
 * arrows (goods VARUPIL and cart VAGNPIL objects), which are HUD for the
 * trailer, plus actors/carts a shot hides, then restore their hidden flags
 * so game state is untouched. */
void cinematicHideOverlays(void);
void cinematicRestoreOverlays(void);

/* Nonzero while glFlip should draw the freecam framing guides. */
int cinematicGuides(void);

/* Free-camera controls, gathered by the platform layer from the event
 * stream: held keys plus edge-triggered presses and mouse motion since the
 * previous platformCineInput call. */
typedef struct CineInput {
    int bForward, bBack, bLeft, bRight, bUp, bDown, bFast;
    int bFovIn, bFovOut;
    int nMouseDX, nMouseDY, nWheel;
    int bRecord, bPlay, bFreeze, bGuides, bPrint, bQuit;
} CineInput;
void platformCineInput(CineInput *pOut);   /* defined in platform_sdl2.c */
/* Wall-clock milliseconds (platformTicks returns the virtual clock). */
unsigned int platformWallTicks(void);       /* defined in platform_sdl2.c */

/* Frame read-back contract with the graphics backend: when
 * cinematicWantsFrame() is nonzero, glFlip reads the finished back buffer
 * (bottom-up RGB, 1-byte rows) into cinematicFrameBuffer(w, h) before the
 * swap and then calls cinematicSubmitFrame(). */
int cinematicWantsFrame(void);
unsigned char *cinematicFrameBuffer(int nWidth, int nHeight);
void cinematicSubmitFrame(void);

/* Replaces the WinMain frame loop after gameInit: starts the round, runs
 * warm-up and capture frames, closes the clip. Returns 0 on success. */
int cinematicRun(void);

#endif /* CINEMATIC_H */
