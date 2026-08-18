#include <windows.h>

#include "../src/maniac.c"

int main(void)
{
    MSG msg;

    g_bRunning = 1;
    g_pendingKey = 0;
    g_pStateFunc = stateQuitConfirm;
    g_hMenuQuitTex = NULL;

    /* A physical J produces WM_KEYDOWN followed by WM_CHAR. */
    WindowProc(NULL, WM_KEYDOWN, 'J', 0);
    if (g_pStateFunc != stateQuitConfirm) return 1;
    WindowProc(NULL, WM_CHAR, 'j', 0);
    if (!PeekMessageA(&msg, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) return 2;

    /* Enter selects Avsluta through the deferred keydown path. Its translated
     * character is processed while menuUpdate is still active, so it must
     * not immediately cancel the newly displayed quit screen. */
    g_pStateFunc = menuUpdate;
    g_nMenuRow = 4;
    WindowProc(NULL, WM_KEYDOWN, VK_RETURN, 0);
    WindowProc(NULL, WM_CHAR, '\r', 0);
    if (g_pStateFunc != menuUpdate) return 3;
    if (g_pendingKey) {
        int key = g_pendingKeyId;
        g_pendingKey = 0;
        if (g_pStateFunc != NULL) g_pStateFunc(1, key, 2);
    }
    if (g_pStateFunc != stateQuitConfirm) return 4;
    if (PeekMessageA(&msg, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) return 5;
    return 0;
}