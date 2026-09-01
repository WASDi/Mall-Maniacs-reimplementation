#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include "scene.h"
#include "scene_alloc.h"
#include "pool.h"
#include "custom_helpers.h"

/* ===================================================================
 * sceneNodeAlloc @0x4318e0 — camera/block alloc (0xa8, mode 2)
 * Verified vs disasm 0x4318e0: PUSH 0xa8; CALL _malloc; links into
 * g_pSceneNodeList @0x45e8cc (head, sibling @+0x10) and root child @+0xc.
 * Sets mode=2 @+0, nWidth/nHeight/renderT as ints at +0x28/0x2c/0x30,
 * vx/vy/vw/vh shorts at +0x20..0x26, pChannels @+0x14 -> +0x38, ch@+0x38
 * fUnk6=1.0. TODO: original writes sibling prev at +0x10 via *(list+0x10)=p.
 * =================================================================== */
void *sceneNodeAlloc(void *pChannelPtr, void *pChannelPtr2, void *pChannelPtr3,
                     short nMeshIdx, short nUnk5, short nUnk6, short nUnk7) /* @0x4318e0 */
{
    SceneNode *n = (SceneNode *)malloc(0xa8);
    if (!n) return NULL;
    memset(n, 0, 0xa8);
    n->pParent = &g_rootNode;
    /* Head insert into g_pSceneNodeList: *(p+4)=oldList; if(oldList) *(oldList+0x10)=p */
    n->pNextSib = (SceneNode *)g_pSceneNodeList;
    if (g_pSceneNodeList) ((SceneNode *)g_pSceneNodeList)->pPrevLink = n;
    g_pSceneNodeList = n;
    /* Also chain as first child of root at +0xc */
    g_rootNode.pChild = n;
    n->nId = 2; /* mode==2 gate in sceneRender */
    n->nChannelCount = 1; /* @0x43196c: MOV byte [EAX+0x3],0x1 (camera has 1 channel) */
    n->pChannels = &n->ch;
    n->pTypeDef = NULL;
    /* Camera block repurposes 0x20..0x30 as viewport: use SceneCameraBlock view.
     * Store float bits without numeric conversion. */
    {
        SceneCameraBlock *cb = (SceneCameraBlock *)n;
        int tmp;
        tmp = (int)(uintptr_t)pChannelPtr; memcpy(&cb->nWidth,  &tmp, sizeof(tmp));
        tmp = (int)(uintptr_t)pChannelPtr2; memcpy(&cb->nHeight, &tmp, sizeof(tmp));
        tmp = (int)(uintptr_t)pChannelPtr3; memcpy(&cb->renderT, &tmp, sizeof(tmp));
        cb->vx = nMeshIdx;
        cb->vy = nUnk5;
        cb->vw = nUnk6;
        cb->vh = nUnk7;
    }
    SceneChannel *ch = n->pChannels;
    ch->fUnk6 = 1.0f;
    ch->bFlagA = 0;
    ch->bFlagB = 0;
    ch->nIdx = 0;
    g_nSceneNodeCount++;
    if (g_nSceneNodeCountPeak < g_nSceneNodeCount) g_nSceneNodeCountPeak = g_nSceneNodeCount;
    g_nSceneNodeMemUsed += 0xa8;
    if (g_nSceneNodeMemPeak < g_nSceneNodeMemUsed) g_nSceneNodeMemPeak = g_nSceneNodeMemUsed;
    return n;
}

/* ===================================================================
 * sceneNodeAllocChild @0x4319e0  (0xa8-byte node)
 * pParent==0 -> &g_rootNode. Links at parent+0xc, sibling prev
 * at +0x10, updates bounds if not root. Channel ptrs at +0x22..0x28 as
 * in disasm. TODO: verify pChannelPtr mapping (menu preview passes 0).
 * =================================================================== */
void *sceneNodeAllocChild(SceneNode *pParent, void *pChannelPtr, void *pChannelPtr2,
                          void *pChannelPtr3, void *pChannelPtr4) /* @0x4319e0 */
{
    SceneNode *n = (SceneNode *)malloc(0xa8);
    if (!n) return NULL;
    memset(n, 0, 0xa8);
    n->pParent = pParent ? pParent : &g_rootNode;
    SceneNode *parent = n->pParent;
    SceneNode *oldChild = parent->pChild;
    n->pNextSib = oldChild;
    if (oldChild) oldChild->pPrevLink = n;
    parent->pChild = n;
    n->pPrevLink = parent; /* @+0x10 = parent, per 0x431a2b */
    /* Original root is at 0x45e8c0 and its +0xc (child) aliases g_pSceneNodeList @0x45e8cc.
     * Our &g_rootNode is at 0x45e818 (buffer includes 0x45e8c0) but g_pSceneNodeList is separate.
     * Keep them in sync when parent is the scene root so the model becomes reachable via the global list. */
    if (n->pParent == &g_rootNode) {
        g_pSceneNodeList = n;
    }
    n->nId = 3; /* @+0 */
    n->bType = 0; /* @+2 */
    n->nChannelCount = 1; /* @+3 */
    n->pChannels = &n->ch;
    SceneChannel *ch = n->pChannels;
    ch->fUnk6 = 1.0f;
    ch->bFlagA = 0; ch->bFlagB = 0;
    /* Original stores pChannelPtr{1..4} at node+0x44 etc which map to ch->nIdx/x/y/z */
    ch->nIdx = (int)(uintptr_t)pChannelPtr;
    ch->x = (int)(uintptr_t)pChannelPtr2;
    ch->y = (int)(uintptr_t)pChannelPtr3;
    ch->z = (int)(uintptr_t)pChannelPtr4;
    g_nSceneNodeCount++;
    if (g_nSceneNodeCountPeak < g_nSceneNodeCount) g_nSceneNodeCountPeak = g_nSceneNodeCount;
    g_nSceneNodeMemUsed += 0xa8;
    if (g_nSceneNodeMemPeak < g_nSceneNodeMemUsed) g_nSceneNodeMemPeak = g_nSceneNodeMemUsed;
    if (n->pParent != &g_rootNode) sceneNodeUpdateBounds(n->pParent);
    return n;
}

/* ===================================================================
 * sceneryObjAlloc @0x430200  (0xa8 + nSub*0x70)
 * Faithful: handles pTypeDef==NULL -> &g_sceneObjDefaultType, malloc
 * (*(pTypeDef+8)*0x70+0xa8), nChannelCount = *(char*)(pTypeDef+8)+1.
 * Copies pA/pB: pA holds shorts x/y/z, pB holds ints nIdx with -0x10/-8
 * strides as in disasm 0x4302a0-0x4302f0. Was off-by-0x10 causing page fault.
 * =================================================================== */
SceneObjTypeDef g_sceneObjDefaultType = {0}; /* fallback when pTypeDef==NULL; original @0x?? TODO addr */
void *sceneryObjAlloc(SceneNode *pParent, int nChanPtr, int nChanPtr2, int nChanPtr3, int nChanPtr4,
                      short nScaleX, short nScaleZ, short nScaleY, void *pTypeDef) /* @0x430200 */
{
    SceneObjTypeDef *td = (SceneObjTypeDef *)pTypeDef;
    if (!td) td = &g_sceneObjDefaultType;
    int nSub = td->field_08 & 0xFF; /* low byte @+8 */
    if (nSub < 0) nSub = 0;
    if (nSub > 64) nSub = 64;
    SceneNode *n = (SceneNode *)malloc(0xa8 + nSub * 0x70);
    if (!n) return NULL;
    memset(n, 0, 0xa8 + nSub * 0x70);
    n->pParent = pParent ? pParent : &g_rootNode;
    SceneNode *parent = n->pParent;
    SceneNode *oldChild = parent->pChild;
    n->pNextSib = oldChild;
    if (oldChild) oldChild->pPrevLink = n;
    parent->pChild = n;
    n->pPrevLink = parent; /* @+0x10 = parent, per 0x430268 */
    n->nId = 1;
    n->bType = 0;
    n->nChannelCount = (unsigned char)(nSub + 1);
    n->pChannels = &n->ch;
    n->pTypeDef = td;
    /* Bounds: original copies *(pTypeDef+0x18) to both node+0x18/+0x1c (disasm 0x430288..0x430291) */
    n->nBoundingRadiusA = td->field_18;
    n->nBoundingRadiusB = td->field_18;
    /* Channel 0 holds the incoming scales/chanPtrs: original writes rot[3] = scales, nIdx/x/y/z = chanPtrs */
    {
        SceneChannel *ch0 = &n->ch;
        ch0->rot[0] = nScaleX;
        ch0->rot[1] = nScaleZ;
        ch0->rot[2] = nScaleY;
        ch0->fUnk6 = 1.0f;
        ch0->bFlagA = 0;
        ch0->bFlagB = 0;
        ch0->nIdx = nChanPtr;
        ch0->x = nChanPtr2;
        ch0->y = nChanPtr3;
        ch0->z = nChanPtr4;
    }
    /* Remaining sub-channels: pA/pB are relocated absolute pointers set by sceneMeshFixup @0x4320f0 */
    for (int i = 1; i <= nSub; i++) {
        SceneChannel *ch = &n->pChannels[i];
        ch->fUnk6 = 1.0f;
        ch->bFlagA = 0;
        ch->bFlagB = 0;
        {
            int src = i - 1;
            if (src >= 0 && src < nSub && td->pA && td->pB) {
                short *pPos = (short *)((char *)td->pA + src * 8);
                ch->x = pPos[0];
                ch->y = pPos[1];
                ch->z = pPos[2];
                ch->nIdx = *(int *)((char *)td->pB + src * 4);
            }
        }
    }
    g_nSceneNodeCount++;
    if (g_nSceneNodeCountPeak < g_nSceneNodeCount) g_nSceneNodeCountPeak = g_nSceneNodeCount;
    g_nSceneNodeMemUsed += 0xa8 + nSub * 0x70;
    if (g_nSceneNodeMemPeak < g_nSceneNodeMemUsed) g_nSceneNodeMemPeak = g_nSceneNodeMemUsed;
    if (n->pParent && n->pParent->pChannels && td->pRender && td->pRender->nVerts) {
        if (g_nSceneryObjCountPeak < td->pRender->nVerts) { /* keep */ }
    }
    /* Original peaks g_nSceneryObjCountPeak from *(pRender+4) — TODO exact */
    if (n->pParent != &g_rootNode) sceneNodeUpdateBounds(n->pParent);
    return n;
}
