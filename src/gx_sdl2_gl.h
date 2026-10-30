#ifndef GX_SDL2_GL_H
#define GX_SDL2_GL_H

/* OpenGL 3.3 backend for the GxDriverApi table (Phase 3).
 * Installed by gxLoadDriver(NULL); destroyed by gxUnloadDriver.
 * Texture handles stay int via an int->GLuint registry (no pointer
 * truncation). Batching: CPU-side triangle batch flushed at state /
 * ordering boundaries and at pFlip; overflow flushes safely. */

int gxGLBackendInstall(void);
void gxGLBackendUninstall(void);

/* PORT ADDITION: depth-of-field control for the HD post pass (cinematic
 * shots). strength = max circle of confusion in pixels (0 disables);
 * focus/range in az world units. Defaults come from MANIAC_DOF*. */
void gxGLSetDepthOfField(float strength, float focus, float range);

/* PORT ADDITION: logical pixels the widescreen canvas exposes on each side
 * of the 640x480 GX canvas (0 at 4:3 or with MANIAC_WIDESCREEN=0). */
int gxGLGetWideMargin(void);

/* PORT ADDITION: shift subsequent pDrawPolygon draws (2D UI) and their
 * clip rect horizontally by shiftX logical pixels; 0 restores. */
void gxGLSetUiShift(int shiftX);

#endif /* GX_SDL2_GL_H */
