#ifndef GX_SDL2_GL_H
#define GX_SDL2_GL_H

/* OpenGL 3.3 backend for the GxDriverApi table (Phase 3).
 * Installed by gxLoadDriver(NULL); destroyed by gxUnloadDriver.
 * Texture handles stay int via an int->GLuint registry (no pointer
 * truncation). Batching: CPU-side triangle batch flushed at state /
 * ordering boundaries and at pFlip; overflow flushes safely. */

int gxGLBackendInstall(void);
void gxGLBackendUninstall(void);

#endif /* GX_SDL2_GL_H */
