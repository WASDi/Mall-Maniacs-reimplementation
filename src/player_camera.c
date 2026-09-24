#include <string.h>

#include "player.h"
#include "player_camera.h"
#include "config.h"
#include "gx.h"
#include "obj.h"
#include "scene.h"
#include "stubs.h"
#include "zone.h"

extern void *g_pSceneDetailGrid;   /* @0x45838c (defined in gameplay.c) */

/* Arrow/scene globals created by playerSetupSceneObjects @0x411550. */
SceneNode *g_pGoodsArrowObj;   /* @0x458110 VARUPIL arrow object (goods target) */
SceneNode *g_pCartArrowObj;    /* @0x458114 VAGNPIL arrow object (zone exit) */
SceneNode *g_pGoodsArrowMesh;  /* @0x458118 class mesh under g_pSceneRoot */
SceneNode *g_pCartArrowMesh;   /* @0x45811c class mesh under g_pSceneRoot */
/* g_camFollowBlock @0x4588f8 — camera-follow block. Its first four fields
 * ARE the globals g_pSceneRoot (0x4588f8), the follow node (0x4588fc),
 * g_pCamPosNode (0x458900) and g_pCamAimNode (0x458904); see scene.h. */
CameraFollowBlock g_camFollowBlock;
SceneNode *g_pMenuSceneRoot;   /* @0x458910 menu-scene root for the return path */
SceneNode *g_pMenuSceneChildA; /* @0x458918 */
SceneNode *g_pMenuSceneChildB; /* @0x45891c */

/* g_nCameraUpdateTick @0x458948 — camera-follow scheduler (mod 4 gate). */
int g_nCameraUpdateTick;

/* cameraSetClassMeshes @0x4023b0 — re-parent the camera pos node (block +8,
 * 0x458900) and the camera aim node (block +0xc, 0x458904) as children of
 * the local player's char node (verified disassembly @0x4023be..0x4023d1:
 * both calls read [EDI+8] and [EDI+0xc] — the camera root at +0 stays at
 * scene top level and holds world coordinates), and store pMesh as the
 * follow node (+4, 0x4588fc). This is what makes the config camera pos/aim
 * offsets ride along with the player, so cameraFollowUpdate's mode-4 reads
 * of those nodes track the character. */
void cameraSetClassMeshes(CameraFollowBlock *pBlk, SceneNode *pMesh) /* @0x4023b0 */
{
    sceneObjSetClassMesh(pBlk->pPosNode, pMesh, 0, 3);   /* @0x4023c0 */
    sceneObjSetClassMesh(pBlk->pAimNode, pMesh, 0, 3);   /* @0x4023d5 [EDI+0xc] */
    pBlk->pFollowNode = pMesh;                                        /* @0x4023e6 */
}

/* cameraFollowUpdate @0x4020d0 — camera follow pass. pBlk = &g_camFollowBlock
 * (@0x4588f8; roundStartInit @0x40a9ce and gameWorldUpdate @0x40b508 both
 * pass the block). Reads the target from the camera pos node (or snaps both
 * target and current position to a c_ac zone whose y-range and rect contain
 * the follow node), pushes the target away from zone walls with
 * zoneAvoidWalls @0x4023e0, follows x/z toward the target with
 * a snap-inside-nSnapDist / delta-nDiv smoothing, lowers the target height
 * when the camera-to-player segment crosses a c_di zone (TODO stub), does
 * the same smoothing for y, pushes the current position away from walls a
 * second time and finally writes the position to the camera root and faces
 * it toward the aim node's world position. */
void cameraFollowUpdate(CameraFollowBlock *pBlk) /* @0x4020d0 */
{
    int anTarget[3];   /* camera-pos-node position / c_ac snap (out1) */
    int anCur[3];      /* camera root position (out2) */
    int anFollow[4];   /* follow-node world position (out3) */
    int anAim[4];      /* aim-node world position (out4) */
    EventObject *pObj;
    int nSnapFlag = 0; /* c_ac snap taken (EBP) */
    GxVec2 vPoint;
    GxVec2 vRef;

    memset(anFollow, 0, sizeof(anFollow));
    sceneNodeGetPos(pBlk->pPosNode, 0, anTarget, 4);                  /* 0x431270 @0x4020e9 */
    sceneNodeGetPos(pBlk->pNode, 0, anCur, 4);                        /* 0x431270 @0x4020f9 */
    sceneNodeGetPosWorld(pBlk->pFollowNode, (float *)anFollow, 4);    /* 0x430e80 @0x402109 */

    pObj = objFindById(OBJ_ID_C_AC, 0);                               /* 0x414a90 @0x402115 */
    while (pObj != NULL) {                                            /* 0x402123..0x40215f */
        if (anTarget[1] <= pObj->nValue0 &&
            (pObj->nValue2 == 0 || pObj->nValue2 <= anTarget[1]) &&
            objContainsPoint(pObj, (float)anFollow[2], (float)anFollow[0])) { /* 0x414bb0 @0x40214b: arg1=anFollow[2] (first push) pairs with +0x38 */
            anTarget[0] = (int)pObj->flPosZ;                       /* +0x3c @0x402163 */
            anTarget[1] = pObj->nValue1;                             /* +0x14 @0x402171 */
            anTarget[2] = (int)pObj->flPosX;                       /* +0x38 @0x402178 */
            anCur[0] = anTarget[0];                                   /* @0x402184 */
            anCur[1] = anTarget[1];
            anCur[2] = anTarget[2];
            nSnapFlag = pObj->nValue3;                               /* +0x1c @0x402190 */
            break;
        }
        pObj = objHashNextSame(pObj);                                 /* 0x414a40 @0x402156 */
    }

    /* First wall-avoid pass: construct vectors, then push the target away
     * from zone walls around the follow node within the camera height.
     * (Original @0x402193..0x402203.)
     * Slot order is load-bearing: the original pushes anTarget[2] first so
     * the GxVec2 lands as x=anTarget[2], y=anTarget[0] (cdecl stack slots:
     * first-pushed float ends up at [ESP+8] = vec.y), and writes the pushed
     * vec back to the same slots (anTarget[2]=(int)vPoint.x). */
    gxVec2Set(&vRef, (float)anFollow[2], (float)anFollow[0]);         /* 0x434fa0 @0x4021a7 */
    gxVec2Set(&vPoint, (float)anTarget[2], (float)anTarget[0]);       /* @0x4021c0 */
    if (nSnapFlag == 0) {
        if (zoneAvoidWalls(&vPoint, &vRef, (float)anTarget[1]) != 0) { /* 0x4023e0 @0x4021db */
            anTarget[2] = (int)vPoint.x;                              /* ftol @0x4021e7 */
            anTarget[0] = (int)vPoint.y;
        }
    }

    /* x follow: snap when close, else ease by delta/nDiv. (@0x40220f..0x402232) */
    {
        int nDelta = anTarget[0] - anCur[0];
        if (nDelta >= pBlk->nSnapDist || nDelta <= -pBlk->nSnapDist) {
            anCur[0] += nDelta / pBlk->nDiv;
        } else {
            anCur[0] = anTarget[0];
        }
    }
    /* z follow (same rule). (@0x402234..0x402254) */
    {
        int nDelta = anTarget[2] - anCur[2];
        if (nDelta >= pBlk->nSnapDist || nDelta <= -pBlk->nSnapDist) {
            anCur[2] += nDelta / pBlk->nDiv;
        } else {
            anCur[2] = anTarget[2];
        }
    }

    /* c_di: lower the target height when the camera-to-follow segment
     * crosses a c_di zone segment. (@0x402254..0x4022a7) */
    pObj = objFindById(OBJ_ID_C_DI, 0);                               /* 0x414a90 @0x40225c */
    if (pObj != NULL &&
        objSegListIntersectTest(pObj, (float)anCur[2], (float)anCur[0], /* 0x414ce0 @0x40228c */
                                (float)anFollow[2], (float)anFollow[0]) != 0) {
        anTarget[1] = (anTarget[1] / pObj->nValue0) * pObj->nValue1; /* @0x402295 */
    }

    /* y follow (same rule as x/z). (@0x4022ad..0x4022d0) */
    {
        int nDelta = anTarget[1] - anCur[1];
        if (nDelta >= pBlk->nSnapDist || nDelta <= -pBlk->nSnapDist) {
            anCur[1] += nDelta / pBlk->nDiv;
        } else {
            anCur[1] = anTarget[1];
        }
    }

    /* Second wall-avoid pass: construct vectors, then push the current
     * position away from the walls around the follow node. (Original
     * @0x4022d0..0x402356; same
     * x=slot[2], y=slot[0] order as the first pass.) */
    gxVec2Set(&vRef, (float)anFollow[2], (float)anFollow[0]);         /* @0x4022e4 */
    gxVec2Set(&vPoint, (float)anCur[2], (float)anCur[0]);             /* @0x40230a */
    if (nSnapFlag == 0) {
        if (zoneAvoidWalls(&vPoint, &vRef, (float)anTarget[1]) != 0) { /* @0x402332 */
            anCur[2] = (int)vPoint.x;                                 /* ftol @0x40233e */
            anCur[0] = (int)vPoint.y;
        }
    }

    sceneNodeSetPos(pBlk->pNode, anCur, 2);                  /* 0x431590 @0x402362 */
    sceneNodeGetPosWorld(pBlk->pAimNode, (float *)anAim, 4);          /* 0x430e80 @0x402372 */
    sceneNodeFacePos(pBlk->pNode, 0, (float)anAim[0], (float)anAim[1], /* 0x431030 @0x402399 */
                     (float)anAim[2], 2);
}
