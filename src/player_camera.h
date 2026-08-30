#ifndef PLAYER_CAMERA_H
#define PLAYER_CAMERA_H

/* player_camera.h — arrow objects, menu scene, and camera follow
 * (maniac.exe 0x411550 camera tail + 0x4020d0 follow). Extracted from
 * src/player.c. */

struct SceneNode;
struct CameraFollowBlock;

/* Scene globals created by playerSetupSceneObjects @0x411550 and
 * levelObjectsCartsCameraInit @0x411b70. Definitions live in
 * player_camera.c; g_camFollowBlock itself is declared in scene.h
 * with macro aliases g_pSceneRoot/g_pCamPosNode/g_pCamAimNode. */
extern struct SceneNode *g_pGoodsArrowObj;   /* @0x458110 VARUPIL */
extern struct SceneNode *g_pCartArrowObj;    /* @0x458114 VAGNPIL */
extern struct SceneNode *g_pGoodsArrowMesh;  /* @0x458118 */
extern struct SceneNode *g_pCartArrowMesh;   /* @0x45811c */
extern struct SceneNode *g_pMenuSceneRoot;   /* @0x458910 */
extern struct SceneNode *g_pMenuSceneChildA; /* @0x458918 */
extern struct SceneNode *g_pMenuSceneChildB; /* @0x45891c */

extern int g_nCameraUpdateTick;              /* @0x458948 */

/* Camera helpers */
void cameraSetClassMeshes(struct CameraFollowBlock *pBlk, struct SceneNode *pMesh); /* @0x4023b0 */
void cameraFollowUpdate(struct CameraFollowBlock *pBlk);                            /* @0x4020d0 */

#endif /* PLAYER_CAMERA_H */
