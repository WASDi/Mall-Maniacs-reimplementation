#include "../src/menu.h"
#include "../src/gx.h"

extern int g_menuMode;
extern void *g_hIntroTex[6];

static int nPresentCalls;

static int captureBlit(int a, int b, int c, void *texture, int x, int y,
                       int width, int height, int depth)
{
    (void)a; (void)b; (void)c; (void)texture;
    (void)x; (void)y; (void)width; (void)height; (void)depth;
    nPresentCalls++;
    return 1;
}

static int captureFlip(void)
{
    nPresentCalls++;
    return 1;
}

int main(void)
{
    g_menuMode = 0;
    g_introFade_2 = 0.0f;
    g_flFrameDelta = 1.0f;
    g_hIntroTex[0] = (void *)1;
    g_driver.api.pBlitSurface = captureBlit;
    g_driver.api.pFlip = captureFlip;
    g_driver.api.pClearScreen = NULL;

    /* A non-space keydown advances time but must not render a logo. */
    introUpdate(1, 99, 2);
    if (nPresentCalls != 0) return 1;
    if (g_introFade_2 != 25.0f) return 2;
    return 0;
}