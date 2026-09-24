#include <stdio.h>

#include "../src/obj_event.h"

/* Asset path comes from the CMake data-dir variable (exact case); the
 * legacy Z:\ install path is obsolete. Window globals live in the
 * platform layer (linked via maniac_lib). */
#ifndef MANIAC_DATA_DIR
#define MANIAC_DATA_DIR "/home/wasd/MallManiacsUnmodified"
#endif

int main(void)
{
    EventObject *pItem1;
    EventObject *pItem2;
    char szEo[1024];

    snprintf(szEo, sizeof(szEo), "%s/ica.eo", MANIAC_DATA_DIR);
    if (eloadCmd(0, szEo) != 0) return 1;

    pItem1 = objFindById(1, 0);
    pItem2 = objFindById(2, 0);
    if (pItem1 == NULL || pItem2 == NULL) return 2;
    if (pItem1->flPosX != 3000.0f || pItem1->flPosZ != -2500.0f) return 3;
    if (pItem2->flPosX != -5600.0f || pItem2->flPosZ != 23200.0f) return 4;
    if (pItem1->flPosX == pItem2->flPosX && pItem1->flPosZ == pItem2->flPosZ) return 5;

    objHashFreeAll();
    return 0;
}
