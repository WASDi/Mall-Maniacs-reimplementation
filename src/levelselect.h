#ifndef LEVELSELECT_H
#define LEVELSELECT_H

#include "menu.h"

/* Map-selection state @0x41b900 and its five level-entry targets. */
int stateLevelSelect(int nType, int nKey, int nKeyType); /* @0x41b900 */
int stateLevelInit0(int nType, int nKey, int nKeyType);   /* @0x41b810 */
int stateLevelInit1(int nType, int nKey, int nKeyType);   /* @0x41b840 */
int stateLevelInit2(int nType, int nKey, int nKeyType);   /* @0x41b870 */
int stateLevelInit3(int nType, int nKey, int nKeyType);   /* @0x41b8a0 */
int stateLevelInit4(int nType, int nKey, int nKeyType);   /* @0x41b8d0 */

extern int g_nLevelSel;       /* @0x45d44c */
extern int g_nLevelIdx;       /* gameplay level index used by init targets */
extern float g_endSceneT;     /* @0x45a768 */

#endif /* LEVELSELECT_H */