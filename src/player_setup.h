#ifndef PLAYER_SETUP_H
#define PLAYER_SETUP_H

/* player_setup.h — per-round character/scene/camera placement
 * (maniac.exe 0x41b6e0, 0x410e90, 0x411550, 0x411b70). */

extern int g_nCurrentItemId; /* @0x458128 — mode 3 target item id */

void playerSetupCharacters(void);      /* @0x41b6e0 */
void playerSetupRound(void);           /* @0x410e90 */
void playerSetupSceneObjects(void);    /* @0x411550 */
void levelObjectsCartsCameraInit(void);/* @0x411b70 */

#endif /* PLAYER_SETUP_H */
