#include <windows.h>

#include "../src/maniac.c"

static int testState(int nType, int nKey, int nKeyType)
{
    (void)nType;
    (void)nKey;
    (void)nKeyType;
    return 0;
}

int main(void)
{
    g_pStateFunc = testState;
    g_pendingKey = 0;

    WindowProc(NULL, WM_KEYDOWN, VK_TAB, 0);
    if (g_pendingKey != 0) return 1;

    WindowProc(NULL, WM_KEYDOWN, VK_SPACE, 0);
    if (g_pendingKey != 1 || g_pendingKeyId != 4) return 2;
    return 0;
}