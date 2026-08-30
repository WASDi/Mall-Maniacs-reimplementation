#ifndef SCENE_ALLOC_H
#define SCENE_ALLOC_H

#include "scene.h"

extern SceneObjTypeDef g_sceneObjDefaultType;

/* scene_alloc.h — node/object allocation
 * (maniac.exe 0x4318e0/0x4319e0/0x430200). */

void *sceneNodeAlloc(void *pChannelPtr, void *pChannelPtr2, void *pChannelPtr3,
                     short nMeshIdx, short nUnk5, short nUnk6, short nUnk7); /* @0x4318e0 */
void *sceneNodeAllocChild(SceneNode *pParent, void *pChannelPtr, void *pChannelPtr2,
                          void *pChannelPtr3, void *pChannelPtr4); /* @0x4319e0 */
void *sceneryObjAlloc(SceneNode *pParent, int nChanPtr, int nChanPtr2, int nChanPtr3, int nChanPtr4,
                      short nScaleX, short nScaleZ, short nScaleY, void *pTypeDef); /* @0x430200 */

#endif /* SCENE_ALLOC_H */
