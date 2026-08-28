#ifndef LEVEL_H
#define LEVEL_H

#include <windows.h>

/* level.h — per-level setup (levelSetup @0x4108a0). Reads the selected
 * "levels[%d]" block from g_configEnvMaster, loads the mall scene,
 * objects scene and characters scene, allocates the scene detail grid,
 * reads the music track and runs the level's startup command script. */

extern int g_nLevelScene;        /* @0x4580b0 main level .sen (sceneLoadSen) */
extern int g_nCharScene;         /* @0x4580b4 characters.sen (sceneLoadSen) */
extern int g_nObjScene;          /* @0x4580b8 objects.sen (sceneLoadSen) */
extern int g_bMusicTrack;        /* @0x4580c0 levels[%d]/music */
extern int g_nTexHudFlingbjorn;  /* @0x458348 hud\flingbjorn.tga texture */

/* Per-level scene base dirs (table @0x44f0d8, indexed by g_nLevelIdx). */
extern const char *g_aszLevelDirs[5];

/* levelSceneTexturesLoad @0x4104b0 — level world pre-pass (textures + the
 * "ph"-prefixed map scene + FLOOR nav nodes + zone wall list), called from
 * roundStartInit's first scene-system cycle. See level.c. */
void levelSceneTexturesLoad(void);

/* Item slot: 30 entries, 0x2c stride; the name field lives 0x1c before
 * the mesh id (original base 0x4583c8, mesh ids at 0x4583e4). */
typedef struct LevelItemSlot {
    char szName[0x1c];       /* +0x00 items[%d]/name */
    int  nMeshId;            /* +0x1c scenNameToId of items[%d]/mesh */
    void *pSceneObj;         /* +0x20 sceneryObjAlloc result */
    void *pSubObj;           /* +0x24 sceneNodeAllocChild result */
    int  _pad28;             /* +0x28 */
} LevelItemSlot;             /* 0x2c */

#define LEVEL_ITEM_SLOT_COUNT 30

extern LevelItemSlot g_levelItemSlots[LEVEL_ITEM_SLOT_COUNT]; /* @0x4583c8 */

extern char g_szLevelScenePath[];  /* @0x457db0 "scene_path\scene_file" */
extern char g_szObjScenePath[];    /* @0x457fb0 "scene_path\obj_scene_file" */
extern char g_szCharScenePath[];   /* @0x457eb0 "scene_path\char_scene_file" */

void levelSetup(void);  /* @0x4108a0 */

#endif /* LEVEL_H */
