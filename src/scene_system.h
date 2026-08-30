#ifndef SCENE_SYSTEM_H
#define SCENE_SYSTEM_H

#include "scene.h"

/* scene_system.h — pools, root, sin tables and lifecycle
 * (maniac.exe 0x42ed40 etc). */

float mathSinDeg(short d);                          /* @0x42d030 */
float mathCosDeg(short d);                          /* @0x42d050 */
int mathAtan2Deg(float y, float x);                 /* @0x42d010 */
void mathSinTreeBuild(int a, int b, float *tree);   /* @0x42efb0 */

int sceneSystemInit(int nNodePoolSize, int nSceneBufSize, int nSortBufCount,
                    int nMeshPoolSize, unsigned int nFlags); /* @0x42ed40 */
int sceneFreeAllNodes(void);                        /* @0x42f150 */
int sceneSystemClose(void);                         /* @0x42f180 */

extern const double g_dblBdgToRad;                  /* @0x44b788 */
extern const double g_dblRadToBdg;                  /* @0x44b780 */
extern const float g_flHalf;                        /* @0x44b274 */

#endif /* SCENE_SYSTEM_H */
