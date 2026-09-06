#ifndef SCENE_TRANSFORM_H
#define SCENE_TRANSFORM_H

#include "scene.h"

/* scene_transform.h — channel/position APIs and helpers
 * (maniac.exe 0x430660 etc). */

int sceneObjSetPos(SceneNode *pObj, int nX, int nY, int nZ, int nMode); /* @0x430660 */
int sceneObjSetPosOrient(SceneNode *pObj, short nYaw, short nPitch, short nRoll, byte nMode); /* @0x4307d0 */
/* sceneObjGetPos @0x4317e0 — read the node's rotation channels (three
 * shorts from pChannels->rot[0..2]) when (nMode & 0xf) == 2, scaled by
 * 182 when (nMode & 0xf0) == 0x20. Returns 1, or 0 for other modes. */
int sceneObjGetPos(SceneNode *pObj, short *pOutXYZ, byte nMode); /* @0x4317e0 */
int sceneNodeGetPosWorld(SceneNode *pNode, float *pOutXYZ, int nMode); /* @0x430e80 */
SceneObjTypeDef *sceneNodeGetMesh(SceneNode *pNode); /* @0x431ae0 */
int sceneSetCurrentObj(SceneNode *pCharSceneObj, int nMode); /* @0x430d98 */
int sceneNodeGetPos(SceneNode *pNode, int nMeshIdx, int *anOutPos, int nMode); /* @0x431270 */
int sceneNodeSetPos(SceneNode *pNode, void *pXYZ, int nMode); /* @0x431590 */
int sceneNodeSetPosShorts(SceneNode *pNode, short *pAngles, byte nMode); /* @0x431850 */
int sceneNodeGetChannelPos(SceneNode *pNode, int nChannel, short *pOutAngles,
                           uint nMode, short *pOutAngles2); /* @0x4315e0 */
void mathVec2Polar(GxVec2 *pOut, const GxVec2 *pIn); /* @0x435060 */
int sceneNodeFacePos(SceneNode *pNode, int nChannel, float flX, float flY, float flZ, int nMode); /* @0x431030 */
void sceneNodeUpdateBounds(SceneNode *pNode); /* @0x4303c0 */
int sceneObjSetSubPos(SceneNode *pObj, int nMeshIdx, short nYaw, short nPitch, short nRoll, byte nMode); /* @0x430a90 */
int sceneObjSetSubOrient(SceneNode *pObj, int nMeshIdx, short nYaw, short nPitch, short nRoll); /* @0x431110 */
void sceneNodeFree(SceneNode *pNode, int nFreeChildren); /* @0x430460 */
void mat3x3Mul(float *a, float *b, float *out); /* @0x42f7d0 */
void chanBuildRotMatrix(SceneChannel *ch); /* @0x42f030 */
void chanCalcWorldTransform(SceneNode *pNode, int nChannel); /* @0x42f6e0 */
int sceneNodeSetHiddenFlag(SceneNode *pNode, int nMode); /* @0x4305c0 */
int sceneNodeGetHiddenFlag(SceneNode *pNode); /* @0x4305b0 */
int sceneObjResetFlags(SceneNode *pNode, int nRecursive); /* @0x430620 */
int sceneObjSetClassMesh(int pObj, SceneNode *pClassNode, int nMeshIdx, int nMode); /* @0x430db0 */

#endif /* SCENE_TRANSFORM_H */
