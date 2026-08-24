#include <stdio.h>
#include <windows.h>
#include "stubs.h"
#include "menu.h"
#include "gx.h"
#include "pool.h"
#include "custom_helpers.h"

extern HWND g_hWnd;

/* =====================================================================
 * TODO stubs — see stubs.h for contracts. Safe, logged no-op or placeholder
 * transitions let future milestones wire real implementations without
 * changing callers. Original addresses noted in stubs.h.
 * ===================================================================== */

/* gameInit @0x409d90 — full game init. Routes gxLoadDriver, gxInit, and
 * menuInit to match the original WinMain @0x4160a0 flow. */
void gameInit(void)
{
    static int bLogged;
    if (!bLogged) {
        GxMode mode;
        bLogged = 1;

        /* Mode struct as built by gameInit @0x40a0cd: width/height/bpp/hInstance/hwnd. */
        mode.width    = 0x280;
        mode.height   = 0x1e0;
        mode.bpp      = 0x10;
        mode.hInstance = (unsigned int)GetModuleHandleA(NULL);
        mode.hwnd     = (unsigned int)g_hWnd;

        if (!gxLoadDriver("DRIVERS\\GXSOFT.DLL")) {
            appLog("[gameInit] gxLoadDriver failed");
            return;
        }
        if (!gxInit(&mode)) {
            appLog("[gameInit] gxInit/pSetMode failed");
            return;
        }
        appLog("[gameInit] gxSetMode done (640x480)");
        menuInit(0);
        appLog("[gameInit] menuInit done");
    }
}

/* Main-menu row targets (menu.c dispatch table). TODO stubs: log once and
 * return to the menu. Replace the bodies later without changing the
 * interfaces or the dispatch table. */

/* stateNetworkMenu @0x420190 — network/lobby menu. Original: host/join lobby
 * states; ESC tail -> menuUpdate. TODO stub: log + return to menu. */
int stateNetworkMenu(int nType, int nKey, int nKeyType)
{
    static int bLogged;
    (void)nType; (void)nKey; (void)nKeyType;
    if (!bLogged) {
        bLogged = 1;
        appLog("[stub TODO] stateNetworkMenu @0x420190 not implemented (back to menu)");
    }
    g_pStateFunc = menuUpdate;
    return 0;
}

/* stateHighScoreTable @0x41dfd0 — now implemented in record.c */

/* stateCharacterSelect @0x41efa0 — now implemented in charselect.c */

/* sceneInstantiateObjects — documented stub for scene-graph population at the
 * end of sceneLoadSen @0x432320 (out of scope for the offline menu preview).
 * Safe no-op: takes the owning memPool handle (unused) and returns 1. */
int sceneInstantiateObjects(int pool)
{
    (void)pool;
    return 1;
}

int scenNameTableInit(int nMeshCount, int nScenObjCap) /* @0x431cb0 */
{
    if (0) {
        void *p = (void *)memPoolCreate(NULL);
        void *a = memPoolAlloc((int)p, 8);
        (void)a;
        memPoolDestroy((int)p);
    }
    (void)nMeshCount; (void)nScenObjCap;
    return 1;
}

int scenSetDir(LPCSTR pszDir) /* @0x432e60 */
{
    (void)pszDir;
    return 1;
}

int sceneFindByName(int *pOut, int nMax, char *pszSubstr) /* @0x431fd0 */
{
    (void)pOut; (void)nMax; (void)pszSubstr;
    return 0;
}

int sceneNodeSetHiddenFlag(int pNode, int nMode) /* @0x4305c0 */
{
    if (nMode == 2) {
        if (0) sceneNodeSetHiddenFlag(0, 2);
        if (pNode) *(unsigned char *)(pNode + 2) = 1;
        return 1;
    }
    if (nMode == 1) {
        if (pNode) *(unsigned char *)(pNode + 2) = 1;
        return 1;
    }
    if (nMode == 3) {
        if (pNode) *(unsigned char *)(pNode + 2) = 2;
    }
    return 1;
}

void *mStringAssignCopy(void *pThis, void *pSrc) /* @0x435440 */
{
    (void)pSrc;
    return pThis;
}
