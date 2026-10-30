#ifndef SCENE_ALLOC_H
#define SCENE_ALLOC_H

#include "scene.h"

extern SceneObjTypeDef g_sceneObjDefaultType;

/* scene_alloc.h — node/object allocation
 * (maniac.exe 0x4318e0/0x4319e0/0x431b00/0x430200). */

void *sceneNodeAlloc(void *pChannelPtr, void *pChannelPtr2, void *pChannelPtr3,
                     short nMeshIdx, short nUnk5, short nUnk6, short nUnk7); /* @0x4318e0 */
void *sceneNodeAllocChild(SceneNode *pParent, void *pChannelPtr, void *pChannelPtr2,
                          void *pChannelPtr3, void *pChannelPtr4); /* @0x4319e0 */
void *sceneryObjAlloc(SceneNode *pParent, int nChanPtr, int nChanPtr2, int nChanPtr3, int nChanPtr4,
                      short nScaleX, short nScaleZ, short nScaleY, void *pTypeDef); /* @0x430200 */

/* musicEmitterAlloc @0x431b00 — 0xa8 music/3D-sound node under pParent
 * (NULL = root), channel nChanIdx-relative offset (nX,nY,nZ), typed by the
 * g_aMusicSlots entry nSlot & 0xff (see scene_alloc.c). */
SceneNode *musicEmitterAlloc(SceneNode *pParent, int nChanIdx, int nX, int nY, int nZ,
                             SceneObjTypeDef *pTypeDef, unsigned int nSlot); /* @0x431b00 */

#endif /* SCENE_ALLOC_H */
