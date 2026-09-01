#include "../src/obj_event.h"

HINSTANCE g_hAppInstance;
HWND g_hWnd;

int main(void)
{
    EventObject *pItem1;
    EventObject *pItem2;

    if (eloadCmd(0, "Z:\\home\\wasd\\MallManiacsUnmodified\\ica.eo") != 0) return 1;

    pItem1 = objFindById(1, 0);
    pItem2 = objFindById(2, 0);
    if (pItem1 == NULL || pItem2 == NULL) return 2;
    if (pItem1->flOriginX != 3000.0f || pItem1->flOriginZ != -2500.0f) return 3;
    if (pItem2->flOriginX != -5600.0f || pItem2->flOriginZ != 23200.0f) return 4;
    if (pItem1->flOriginX == pItem2->flOriginX && pItem1->flOriginZ == pItem2->flOriginZ) return 5;

    objHashFreeAll();
    return 0;
}