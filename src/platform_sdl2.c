#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <dirent.h>
#include <strings.h>
#endif
#include <SDL2/SDL.h>

#include "platform_sdl2.h"
#include "input.h"
#include "gx.h"
#include "time.h"
#include "menu.h"
#include "gameplay.h"
#include "custom_helpers.h"

/* SDL2 platform layer. Replaces WinMain @0x4160a0 window setup,
 * WindowProc @0x4161b0 message routing and WM_ACTIVATE @0x4161f6 focus
 * handling. Polling/debounce logic in input.c is untouched; only the
 * event source changes (SDL2 instead of WindowProc/DirectInput). */

/* g_hWnd @0x459ce0 / g_hAppInstance @0x459cdc — opaque window twins
 * (SDL2 window pointer on the native target). Owned here; maniac.c and
 * legacy readers declare them extern. */
void *g_hWnd;
void *g_hAppInstance;
static SDL_Window *s_window;
static SDL_GLContext s_gl;
static char s_assetDir[1024];
static char s_prefDir[1024];
static int s_textInputOn;
static Uint32 s_fpsStart;
static unsigned int s_fpsFrameCount;
static int s_fpsStarted;

static int scancodeToChannel(SDL_Scancode sc)
{
    switch (sc) {
    case SDL_SCANCODE_RIGHT:  return 0;
    case SDL_SCANCODE_LEFT:   return 1;
    case SDL_SCANCODE_UP:     return 2;
    case SDL_SCANCODE_DOWN:   return 3;
    case SDL_SCANCODE_SPACE:  return 4;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return 6;
    case SDL_SCANCODE_ESCAPE: return 7;
    default: return -1;
    }
}

static void resolveAssetDir(const char *argv0)
{
    const char *env;
    s_assetDir[0] = '\0';
    env = getenv("MM_DATA_DIR");
    if (env && env[0]) {
        snprintf(s_assetDir, sizeof(s_assetDir), "%s", env);
        return;
    }
#ifdef MANIAC_DATA_DIR
    snprintf(s_assetDir, sizeof(s_assetDir), "%s", MANIAC_DATA_DIR);
    if (s_assetDir[0]) return;
#endif
    {
        char *base = SDL_GetBasePath();
        if (base) {
            snprintf(s_assetDir, sizeof(s_assetDir), "%s", base);
            SDL_free(base);
            return;
        }
    }
    if (argv0) {
        const char *slash = strrchr(argv0, '/');
        if (slash) {
            size_t n = (size_t)(slash - argv0);
            if (n >= sizeof(s_assetDir)) n = sizeof(s_assetDir) - 1;
            memcpy(s_assetDir, argv0, n);
            s_assetDir[n] = '\0';
            return;
        }
    }
    snprintf(s_assetDir, sizeof(s_assetDir), ".");
}

int platformInit(int argc, char **argv)
{
    const char *argv0 = (argc > 0) ? argv[0] : NULL;
    (void)argc;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "[platform] SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    resolveAssetDir(argv0);
    /* Writable user dir for config/temp (Phase 2 bridge; Phase 5 owns the
     * canonical filesystem boundary). Never redirect asset reads here. */
    {
        char *pref = SDL_GetPrefPath("AddGames", "MallManiacs");
        if (pref) {
            snprintf(s_prefDir, sizeof(s_prefDir), "%s", pref);
            SDL_free(pref);
        } else {
            snprintf(s_prefDir, sizeof(s_prefDir), ".");
        }
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    {
        int attempt;
        for (attempt = 0; attempt < 2; attempt++) {
            int requestMultisampling = (attempt == 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, requestMultisampling ? 1 : 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, requestMultisampling ? 4 : 0);
            s_window = SDL_CreateWindow("Mall Maniacs",
                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
                SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
            if (!s_window) {
                char error[256];
                snprintf(error, sizeof(error), "%s", SDL_GetError());
                if (requestMultisampling) {
                    fprintf(stderr, "[platform] SDL_CreateWindow with MSAA failed (%s); retrying without MSAA\n", error);
                    continue;
                }
                fprintf(stderr, "[platform] SDL_CreateWindow failed: %s\n", error);
                SDL_Quit();
                return 0;
            }
            s_gl = SDL_GL_CreateContext(s_window);
            if (s_gl) break;
            {
                char error[256];
                snprintf(error, sizeof(error), "%s", SDL_GetError());
                SDL_DestroyWindow(s_window);
                s_window = NULL;
                if (requestMultisampling) {
                    fprintf(stderr, "[platform] SDL_GL_CreateContext with MSAA failed (%s); retrying without MSAA\n", error);
                    continue;
                }
                fprintf(stderr, "[platform] SDL_GL_CreateContext failed: %s\n", error);
                SDL_Quit();
                return 0;
            }
        }
    }
    {
        int sampleBuffers = 0, samples = 0;
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &sampleBuffers);
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &samples);
        if (sampleBuffers > 0 && samples > 0)
            appLog("[platform] GL multisampling enabled (%d samples)", samples);
        else
            appLog("[platform] GL multisampling unavailable; using non-MSAA rendering");
    }
    if (SDL_GL_SetSwapInterval(1) != 0) {
        appLog("[platform] VSync unavailable: %s", SDL_GetError());
    } else if (SDL_GL_GetSwapInterval() == 1) {
        appLog("[platform] VSync enabled (swap interval=1)");
    } else {
        appLog("[platform] VSync request did not activate swap interval=1");
    }
    g_hWnd = (void *)s_window;
    g_hAppInstance = (void *)s_window;
    s_fpsStart = 0;
    s_fpsFrameCount = 0;
    s_fpsStarted = 0;
    SDL_StartTextInput();
    s_textInputOn = 1;
    appLog("[platform] window 640x480 GL3.3 asset='%s' pref='%s'",
        s_assetDir, s_prefDir);
    return 1;
}

void platformShutdown(void)
{
    if (s_textInputOn) SDL_StopTextInput();
    if (s_gl) { SDL_GL_DeleteContext(s_gl); s_gl = NULL; }
    if (s_window) { SDL_DestroyWindow(s_window); s_window = NULL; }
    g_hWnd = NULL;
    SDL_Quit();
}

void *platformWindow(void) { return (void *)s_window; }
void *platformGLContext(void) { return (void *)s_gl; }
const char *platformAssetDir(void) { return s_assetDir; }
const char *platformPrefDir(void) { return s_prefDir; }

int platformAssetPath(const char *rel, char *out, unsigned int outSize)
{
    if (!rel || !out || outSize == 0) return 0;
    /* Absolute paths pass through (tests + legacy absolute refs). */
    if (rel[0] == '/' || rel[0] == '\\') {
        snprintf(out, outSize, "%s", rel);
        return 1;
    }
    if (s_assetDir[0])
        snprintf(out, outSize, "%s/%s", s_assetDir, rel);
    else
        snprintf(out, outSize, "%s", rel);
    return 1;
}

/* platformFopenCI — see platform_sdl2.h. Component-wise resolution so
 * only the mismatched levels fall back (exact parents keep working). */
FILE *platformFopenCI(const char *path, const char *mode)
{
#ifdef _WIN32
    return fopen(path, mode);
#else
    char cur[2048], comp[256];
    size_t i, n, cn;
    if (!path || !mode) return NULL;
    cur[0] = '\0';
    i = 0;
    n = strlen(path);
    if (n >= sizeof(cur)) n = sizeof(cur) - 1;
    if (n > 0 && (path[0] == '/')) {
        cur[0] = '/'; cur[1] = '\0';
        i = 1;
    }
    while (i <= n) {
        size_t j = i;
        while (j < n && path[j] != '/' && path[j] != '\\') j++;
        cn = j - i;
        if (cn >= sizeof(comp)) return NULL;
        memcpy(comp, path + i, cn);
        comp[cn] = '\0';
        if (comp[0] != '\0') {
            char next[2048], found[256];
            DIR *d;
            int have = 0;
            /* Join cur + comp safely without -Wformat-truncation noise:
             * overlong paths fail closed (NULL) instead of truncating. */
            {
                size_t cl = strlen(cur), pl = strlen(comp);
                if (cur[0] == '\0') {
                    if (pl >= sizeof(next)) return NULL;
                    memcpy(next, comp, pl + 1);
                } else if (cur[1] == '\0' && cur[0] == '/') {
                    if (1 + pl >= sizeof(next)) return NULL;
                    next[0] = '/';
                    memcpy(next + 1, comp, pl + 1);
                } else {
                    if (cl + 1 + pl >= sizeof(next)) return NULL;
                    memcpy(next, cur, cl);
                    next[cl] = '/';
                    memcpy(next + cl + 1, comp, pl + 1);
                }
            }
            /* Exact first (fast path + determinism). */
            if (j >= n) {
                FILE *f = fopen(next, mode);
                if (f) return f;
            } else {
                DIR *dt = opendir(next);
                if (dt) { closedir(dt); snprintf(cur, sizeof(cur), "%s", next); i = j + 1; continue; }
            }
            d = opendir(cur[0] ? cur : ".");
            if (!d) return NULL;
            {
                struct dirent *e;
                while ((e = readdir(d)) != NULL) {
                    if (strcasecmp(e->d_name, comp) == 0) {
                        snprintf(found, sizeof(found), "%s", e->d_name);
                        have = 1;
                        break;
                    }
                }
            }
            closedir(d);
            if (!have) return NULL;
            {
                size_t cl = strlen(cur), pl = strlen(found);
                if (cur[0] == '\0') {
                    if (pl >= sizeof(next)) return NULL;
                    memcpy(next, found, pl + 1);
                } else if (cur[1] == '\0' && cur[0] == '/') {
                    if (1 + pl >= sizeof(next)) return NULL;
                    next[0] = '/';
                    memcpy(next + 1, found, pl + 1);
                } else {
                    if (cl + 1 + pl >= sizeof(next)) return NULL;
                    memcpy(next, cur, cl);
                    next[cl] = '/';
                    memcpy(next + cl + 1, found, pl + 1);
                }
            }
            if (j >= n) return fopen(next, mode);
            snprintf(cur, sizeof(cur), "%s", next);
        }
        i = j + 1;
    }
    return fopen(cur[0] ? cur : ".", mode);
#endif
}

unsigned int platformTicks(void) { return SDL_GetTicks(); }
void platformSleep(unsigned int ms) { SDL_Delay(ms); }

void platformUpdateFPS(void)
{
    Uint32 now = SDL_GetTicks();
    if (!s_window) return;
    if (!s_fpsStarted) {
        s_fpsStart = now;
        s_fpsFrameCount = 1;
        s_fpsStarted = 1;
        return;
    }
    s_fpsFrameCount++;
    if (now - s_fpsStart >= 1000) {
        char title[64];
        Uint32 elapsed = now - s_fpsStart;
        unsigned int fps = (unsigned int)((Uint64)s_fpsFrameCount * 1000 / elapsed);
        snprintf(title, sizeof(title), "Mall Maniacs - %u FPS", fps);
        SDL_SetWindowTitle(s_window, title);
        s_fpsStart = now;
        s_fpsFrameCount = 0;
    }
}

void platformShowError(const char *title, const char *msg)
{
    if (s_window)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, s_window);
    else
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, NULL);
    fprintf(stderr, "[fatal] %s: %s\n", title, msg);
}

/* Focus handling mirrors WindowProc WM_ACTIVATE @0x4161f6:
 * deactivate freezes the clock (g_nFrameDue=0) + gxSnooze once;
 * reactivate advances the clock then re-inits GxMode + gxInit. */
static void platformFocusLost(void)
{
    g_nFrameDue = 0;
    if (g_nGameFrameActive == 0) {
        gxSnooze();                            /* @0x433330 @0x416273 */
        g_nGameFrameActive = 1;                /* @0x45834c @0x416278 */
    }
}

static void platformFocusGained(void)
{
    int nSavedClockCache = g_nClockCache;
    /* Original WM_ACTIVATE sets g_nFrameDue from the activate wParam first
     * (nonzero = active, clock runs); without this a single focus loss
     * freezes getGameTime @0x40dfe0 forever. */
    g_nFrameDue = 1;
    getGameTime();                             /* @0x40dfe0 @0x41620c */
    if (g_nGameFrameActive != 0) {             /* @0x45834c @0x416211 */
        GxMode mode;
        mode.width = 0x280;
        mode.height = 0x1e0;
        mode.bpp = 0x10;
        mode.hInstance = NULL;                 /* ignored field (GL backend) */
        mode.hwnd = NULL;                      /* ignored field (GL backend) */
        gxInit(&mode);                         /* @0x4332f0 @0x41624f */
        g_nGameFrameActive = 0;                /* @0x416257 */
    }
    g_nClockCache = nSavedClockCache;          /* @0x416216 */
}

void platformRequestQuit(void)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_QUIT;
    SDL_PushEvent(&ev);
}

int platformPumpEvents(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            return 0;
        case SDL_KEYDOWN: {
            int ch = scancodeToChannel(ev.key.keysym.scancode);
            if (ch >= 0 && ch < 8) g_abInputKeyHeld[ch] = (char)0x80;
            /* Repeat events must not re-trigger WM_CHAR routing. */
            if (ev.key.repeat) break;
            break;
        }
        case SDL_KEYUP: {
            int ch = scancodeToChannel(ev.key.keysym.scancode);
            if (ch >= 0 && ch < 8) g_abInputKeyHeld[ch] = 0;
            break;
        }
        case SDL_TEXTINPUT: {
            /* WindowProc @0x4161b0 WM_CHAR routing: in-round chars go to
             * gameKeyHandler @0x40db80, otherwise through g_pStateFunc. */
            unsigned char c = (unsigned char)ev.text.text[0];
            if (g_bGameActive != 0) {
                gameKeyHandler((int)c, 0);
            } else if (g_pStateFunc != NULL) {
                g_pStateFunc(1, (int)c, 0);
            }
            break;
        }
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                platformFocusLost();
            else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                platformFocusGained();
            else if (ev.window.event == SDL_WINDOWEVENT_CLOSE)
                return 0;
            break;
        default:
            break;
        }
    }
    return 1;
}
