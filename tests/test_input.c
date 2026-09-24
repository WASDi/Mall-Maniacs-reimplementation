/* test_input.c — SDL2 port: exercises the untouched pollKeyboard
 * @0x416a10 debounce layer directly. The SDL scancode -> channel mapping
 * lives in platformPumpEvents (needs a display); here the held-state
 * bytes are driven directly, as the old WindowProc test did through
 * window messages. */

#include <string.h>

#include "../src/input.h"

static int gotKey;
static int gotType;
static int nCalls;

static int capture(int nKey, int nKeyType)
{
    gotKey = nKey;
    gotType = nKeyType;
    nCalls++;
    return 0;
}

int main(void)
{
    memset(g_abInputKeyHeld, 0, sizeof(g_abInputKeyHeld));

    /* Space held dispatches (4, 2) once, then debounces for 200 ms. */
    g_abInputKeyHeld[4] = (char)0x80;
    if (nCalls != 0) return 1;
    pollKeyboard(capture, 1000);
    if (nCalls != 1 || gotKey != 4 || gotType != 2) return 2;
    pollKeyboard(capture, 1100);
    if (nCalls != 1) return 3;
    pollKeyboard(capture, 1300);
    if (nCalls != 2 || gotKey != 4) return 4;

    /* Release clears the debounce tick with no dispatch. */
    g_abInputKeyHeld[4] = 0;
    pollKeyboard(capture, 1400);
    if (nCalls != 2) return 5;

    /* Enter dispatches once per press, no repeat while held. */
    g_abInputKeyHeld[6] = (char)0x80;
    pollKeyboard(capture, 1500);
    if (nCalls != 3 || gotKey != 6) return 6;
    pollKeyboard(capture, 2000);
    if (nCalls != 3) return 7;
    g_abInputKeyHeld[6] = 0;
    pollKeyboard(capture, 2100);
    if (nCalls != 3) return 8;

    return 0;
}
