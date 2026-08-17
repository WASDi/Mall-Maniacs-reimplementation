#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "gx.h"
#include "pool.h"
#include "util.h"

/* =====================================================================
 * Mall Maniacs (maniac.exe) replacement — GUI vertical slice + intro
 * timeline. Compiled to maniac_rebuild.exe per Rebuild.md milestone 1.
 *
 * Reimplements a narrow slice of the original:
 *   - WinMain @0x4160a0           (simplified: no DirectInput, no net loop)
 *   - initWindowAndInput @0x4165f0 (window class + creation only)
 *   - WindowProc @0x4161b0         (close/escape + key events to state fn)
 *   - gxInit/gxLoadTexture/presentFrame  (see gx.c)
 *   - menuInit @0x419c20           (asset loads + intro/menu state setup)
 *   - introUpdate @0x41ae50        (intro logo timeline, all 6 logos)
 *   - menuUpdate @0x41b0b0         (TODO stub: main menu not yet built)
 * ===================================================================== */

static HWND      g_hWnd;          /* maniac g_hMainWindow @0x459ce0 */
static HINSTANCE g_hInstance;     /* maniac g_hAppInstance @0x459cdc */
static int       g_bRunning = 1;

/* State function pointer (maniac g_pStateFunc @0x45a6f8). Called with
 * (type, key, keyType): type 0 = frame update, type 1 = key event
 * (key, 2 = keydown). Same convention as dispatchKeyEvent (see 0x41ade0). */
typedef int (*PStateFunc)(int nType, int nKey, int nKeyType);
static PStateFunc g_pStateFunc;   /* @0x45a6f8 */

/* Intro timeline state — mirrors maniac globals:
 * g_introFade_2 @0x45d444 (float, ms accumulator),
 * g_flFrameDelta @0x45a6cc (float, = elapsed ms * 0.04),
 * g_nLastFrameTime @0x45a65c. */
static float g_introFade_2;
static float g_flFrameDelta;
static DWORD g_nLastFrameTime;

/* Intro logos in load/present order — mirrors menuInit @0x419c20 and the
 * maniac globals g_hIntroTexAddgames..g_hIntroTexPresenterar @0x45a618-0x45a62c
 * (original string table s_menu_intro_*_tga @0x450794-0x450720). */
static const char * const g_kIntroTga[6] = {
    "menu\\intro_addgames.tga",    /* g_hIntroTexAddgames   @0x45a618 */
    "menu\\intro_och.tga",         /* g_hIntroTexOch        @0x45a61c */
    "menu\\intro_uds.tga",         /* g_hIntroTexUds        @0x45a620 */
    "menu\\intro_samarbete.tga",   /* g_hIntroTexSamarbete  @0x45a624 */
    "menu\\intro_mcd.tga",         /* g_hIntroTexMcd        @0x45a628 */
    "menu\\intro_presenterar.tga"  /* g_hIntroTexPresenterar @0x45a62c */
};
static void *g_hIntroTex[6];

static void introPresent(int idx);
static void introClear(void);
static int  vkToKeyId(int vk);

/* menuUpdate @0x41b0b0 — main menu state. TODO stub (Rebuild.md §19): the
 * main menu is not implemented yet; logs once and holds the last drawn frame
 * so the intro timeline can transition cleanly. Replace the body later
 * without changing callers or the interface. */
static int menuUpdate(int nType, int nKey, int nKeyType)
{
    static int bLogged;
    (void)nType; (void)nKey; (void)nKeyType;
    if (!bLogged) {
        bLogged = 1;
        appLog("[stub TODO] menuUpdate @0x41b0b0 not implemented (holds last frame)");
    }
    return 0;
}

/* introUpdate @0x41ae50 — intro logo timeline. g_introFade_2 (ms) selects the
 * logo; paired thresholds leave a ~90ms cleared gap (fade-to-black) between
 * logos. Any keydown skips to the menu (the original skips on key 4, type 2;
 * docs/03-gameflow.md note "any key or ENTER skips" — we accept all keys).
 * Threshold floats @0x44b660-0x44b684: 2500/2590/3590/3680/6180/6270/7270/
 * 7360/9860/9950 + 17450 @0x44b65c. g_fl_25 @0x44b468 = 25.0f. */
static int introUpdate(int nType, int nKey, int nKeyType)
{
    (void)nKey;
    if (nType == 1) {                  /* key event (dispatchKeyEvent path) */
        if (nKeyType == 2) {           /* keydown -> skip to menu */
            appLog("[intro] skipped to menu by key @%.0f ms", g_introFade_2);
            g_pStateFunc = menuUpdate;
        }
        return 0;
    }

    /* Accumulate elapsed ms (g_flFrameDelta == elapsed ms * 0.04; *25.0 -> ms). */
    g_introFade_2 += g_flFrameDelta * 25.0f;

    if (g_introFade_2 < 2500.0f)  { introPresent(0); return 0; }
    if (g_introFade_2 < 2590.0f)  { introClear();    return 0; }
    if (g_introFade_2 < 3590.0f)  { introPresent(1); return 0; }
    if (g_introFade_2 < 3680.0f)  { introClear();    return 0; }
    if (g_introFade_2 < 6180.0f)  { introPresent(2); return 0; }
    if (g_introFade_2 < 6270.0f)  { introClear();    return 0; }
    if (g_introFade_2 < 7270.0f)  { introPresent(3); return 0; }
    if (g_introFade_2 < 7360.0f)  { introClear();    return 0; }
    if (g_introFade_2 < 9860.0f)  { introPresent(4); return 0; }
    if (g_introFade_2 < 9950.0f)  { introClear();    return 0; }
    if (g_introFade_2 < 17450.0f) { introPresent(5); return 0; }

    /* Timeline done (>= 17450 ms): original sets g_pStateFunc = menuUpdate and
     * plays the menu CD track (introUpdate @0x41ae50 LAB_0041b082). */
    appLog("[intro] timeline complete (%.0f ms) -> menuUpdate", g_introFade_2);
    g_pStateFunc = menuUpdate;
    return 0;
}

/* Present one intro logo, logging the first time each logo is shown. */
static void introPresent(int idx)
{
    static int nShown = -1;
    if (idx != nShown) {
        nShown = idx;
        appLog("[intro] logo %d/%d %s @%.0f ms", idx + 1, 6,
               g_kIntroTga[idx], g_introFade_2);
    }
    presentFrame(g_hIntroTex[idx]);
}

/* Clear to g_nClearColor (0) + flip — the gap between intro logos
 * (introUpdate @0x41ae50: gxClearScreen(1,g_nClearColor); gxFlip();). */
static void introClear(void)
{
    gxClearScreen(1, 0);   /* g_nClearColor @0x45892c = 0 */
    gxFlip();
}

/* WindowProc @0x4161b0 — narrowed to close/escape for the slice. The original
 * also routes keyboard to gameKeyHandler / DirectInput polling; we forward
 * keydowns to the state function (dispatchKeyEvent analog, see 0x41ade0). */
static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_CLOSE:                        /* 0x10 */
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:                      /* 0x100 */
        if (wParam == VK_ESCAPE) {        /* 0x1b — quit the slice */
            PostQuitMessage(0);
            return 0;
        }
        if (g_pStateFunc != NULL) {
            g_pStateFunc(1, vkToKeyId((int)wParam), 2);
        }
        break;
    case WM_DESTROY:                      /* 0x2 */
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

/* Map Win32 virtual key -> game key id (Key id map, docs/12-input.md:
 * 0=Right, 1=Left, 2=Up, 3=Down, 4=Space, 6=Enter, 7=Esc). Unknown keys
 * report as 4 so any keypress advances the intro. */
static int vkToKeyId(int vk)
{
    switch (vk) {
    case VK_RIGHT:  return 0;
    case VK_LEFT:   return 1;
    case VK_UP:     return 2;
    case VK_DOWN:   return 3;
    case VK_SPACE:  return 4;
    case VK_RETURN: return 6;
    case VK_ESCAPE: return 7;
    default:        return 4;   /* treat any other key as "fire" (advance) */
    }
}

/* initWindowAndInput @0x4165f0 — window class + creation only (DirectInput
 * deferred; input via window messages for this milestone). */
static int initWindowAndInput(int nShowCmd)
{
    WNDCLASSEXA wc;
    static const char *kClassName = "MallManiacsRebuild";

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_HREDRAW | CS_VREDRAW;  /* original style = 3 */
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = g_hInstance;
    wc.hCursor       = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;

    if (!RegisterClassExA(&wc)) {
        appLog("[init] RegisterClassExA failed: %lu", (unsigned long)GetLastError());
        return 0;
    }

    /* Original CreateWindowExA style 0xcf0000 (WS_POPUP|WS_VISIBLE|WS_SYSMENU).
     * Create a fixed 640x480 window for the slice. */
    g_hWnd = CreateWindowExA(0, kClassName, "Mall Maniacs",
                             0xcf0000, 0, 0, 640, 480,
                             NULL, NULL, g_hInstance, NULL);
    if (g_hWnd == NULL) {
        appLog("[init] CreateWindowExA failed: %lu", (unsigned long)GetLastError());
        return 0;
    }
    ShowWindow(g_hWnd, nShowCmd);
    UpdateWindow(g_hWnd);
    appLog("[init] window created (hwnd=%08x)", (unsigned int)g_hWnd);
    return 1;
}

/* menuInit @0x419c20 — asset loads + intro/menu state setup. Narrowed to the
 * intro-logo screen: a .tpg to install the driver palette, then the six
 * intro logos in the original load order. The original selects introUpdate
 * because g_menuMode @0x45022c is preinitialized to 0x101 in .data (low byte
 * = 1 -> intro) and clears it after; the rebuild always starts with the intro. */
static void menuInit(void)
{
    size_t size = 0;
    void  *tpg;
    int    i;

    /* memPoolSystemInit @0x4197a0 first (original order: commandDispatch,
     * then memPoolSystemInit, then gx/reset/asset loads). Creates pool 0
     * "DEFAULT" used by fileReadRaw/gxLoadTpgFile. */
    memPoolSystemInit();
    appLog("[menu] memPoolSystemInit: pool 0 = DEFAULT");

    /* First .tpg load installs the DirectDraw palette (gxLoadTexture
     * @0x100019b0, first-call branch). MERGED00 shares the intro palette
     * (249/256 entries), matching menuInit's ordering. */
    tpg = readFileAlloc("menu\\MERGED00.TPG", &size);
    if (tpg == NULL) {
        appLog("[assets] menu\\MERGED00.TPG missing");
    } else if (size < 0x10400) {
        appLog("[assets] menu\\MERGED00.TPG too small (%u bytes)", (unsigned)size);
        free(tpg);
    } else {
        gxLoadTexture(0, 1, "MERGED00", tpg, (char *)tpg + 0x10000);
        appLog("[assets] MERGED00.TPG loaded (%u bytes, palette set)", (unsigned)size);
        free(tpg);
    }

    /* Six intro logos in the original order (menuInit @0x419c20 load block;
     * imageLoadByMode here == tgaLoad16 @0x415df0 path via loadTga640x480). */
    for (i = 0; i < 6; i++) {
        g_hIntroTex[i] = loadTga640x480(g_kIntroTga[i]);
        if (g_hIntroTex[i] == NULL) {
            appLog("[assets] %s load failed", g_kIntroTga[i]);
        }
    }
    appLog("[assets] intro logos loaded (%d/6)", i);

    /* Mirror menuInit: g_nLastFrameTime = getGameTime(); g_pStateFunc =
     * introUpdate (intro selected; see header comment on g_menuMode). */
    g_nLastFrameTime = timeGetTime();
    g_introFade_2    = 0.0f;
    g_pStateFunc     = introUpdate;
}

/* WinMain @0x4160a0 — simplified: window, driver, intro timeline loop,
 * clean close. */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd)
{
    GxMode mode;
    MSG    msg;
    DWORD  now;
    int    i;

    (void)hPrevInstance;
    (void)lpCmdLine;

    g_hInstance = hInstance;
    appLog("[winmain] === start ===");

    if (!initWindowAndInput(nShowCmd)) {
        appLog("[winmain] initWindowAndInput failed");
        return 1;
    }

    /* Mode struct as built by gameInit @0x40a0cd: width/height/bpp/hInstance/hwnd. */
    mode.width    = 0x280;
    mode.height   = 0x1e0;
    mode.bpp      = 0x10;
    mode.hInstance = (unsigned int)g_hInstance;
    mode.hwnd     = (unsigned int)g_hWnd;

    if (!gxInit(&mode)) {
        appLog("[winmain] gxInit failed");
        goto out;
    }
    appLog("[winmain] gxSetMode done (640x480, bpp forced by driver)");

    menuInit();
    if (g_pStateFunc == NULL) {
        appLog("[winmain] state setup failed");
        goto out;
    }
    appLog("[winmain] running intro timeline (6 logos)");

    /* Message loop + frame update — mirrors the original WinMain idle path
     * (gameFrameUpdate timing, see 0x41a8c0): state advances at most every
     * 25ms with g_flFrameDelta = elapsed ms * 0.04. */
    while (g_bRunning) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_bRunning = 0;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!g_bRunning) break;

        now = timeGetTime();
        if ((int)(now - g_nLastFrameTime) >= 0x19) {
            g_flFrameDelta = (float)(now - g_nLastFrameTime) * 0.04f;
            g_nLastFrameTime = now;
            if (g_pStateFunc != NULL) {
                g_pStateFunc(0, 0, 0);
            }
        }
    }

    appLog("[winmain] exiting cleanly");

out:
    for (i = 0; i < 6; i++) {
        if (g_hIntroTex[i]) free(g_hIntroTex[i]);
    }
    gxShutdown();
    if (g_hWnd) DestroyWindow(g_hWnd);
    appLog("[winmain] === done ===\n");
    return 0;
}