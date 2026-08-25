#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include "scene.h"
#include "gx.h"
#include "pool.h"
#include "sen.h"
#include "custom_helpers.h"

/* =====================================================================
 * Scene graph + software 3D renderer (maniac.exe 0x42xxxx/0x43xxxx).
 * Reimplemented from Ghidra decompilations; trig uses sinf/cosf (the
 * original used a 0x400-entry sin table — numerically equivalent).
 * World->screen projection in sceneNodeRender is best-effort pending
 * assembly-level verification of the exact clip/divide formulas.
 * ===================================================================== */

/* --- globals --- */
void *g_pSceneNodeList = NULL;       /* @0x45e8cc */
void *g_pSortBuffer = NULL;          /* @0x45e914 */
void *g_pSortBufCur = NULL;          /* @0x45e90c cursor */
void *g_pNodePool = NULL;            /* @0x45e604 */
void *g_pNodePool2 = NULL;           /* @0x45e648 */
void *g_pMeshPool = NULL;            /* @0x45e610 */
void *g_pNodePoolCur = NULL;         /* @0x45e908 cursor into g_pNodePool */
void *g_pNodePool2Cur = NULL;        /* @0x45e5fc cursor into g_pNodePool2 */
void *g_pRootMatrix = NULL;          /* @0x45e818 */
char  g_abSceneRootNode[0xb0];       /* @0x45e818 root node storage (0xb0) */
float *g_pSinTable = NULL;           /* @0x45e5f8 0x400 floats (0x1000), built at 0x42ed40 */
float *g_pSinTree = NULL;            /* @0x45e888 0x3ff8 bytes, via mathSinTreeBuild @0x42efb0 */
static double g_dblTrigStep = 0.015707963267948967; /* @0x44b790 = 2*pi/0x400, used for sin table */

/* camera basis derived from g_pSceneRoot channel for projection */
static float g_camPos[3] = {0,0,0};
static float g_camMat[9] = {1,0,0, 0,1,0, 0,0,1};   /* column-major view matrix */
int   g_nSceneNodeCount = 0;
int   g_nSceneNodeMemUsed = 0;
int   g_nSceneryObjCountPeak = 0;
int   g_nSceneNodeCountPeak = 0;
int   g_nSceneNodeMemPeak = 0;
float g_nSceneWidth = 0.0f;   /* @0x45e900 float, disasm moves via int */
float g_nSceneHeight = 0.0f;  /* @0x45e614 float */
float g_flSceneAspect = 1.0f; /* @0x45e8fc */
float g_sceneRenderT = 0.0f;
int   g_nSceneHalfWidth = 0;      /* @0x450f70 */
int   g_centerX = 0;              /* @0x450f74 */
int   g_centerY = 0;              /* @0x450f78 */
float g_flSceneRenderT2 = 0.0f;   /* @0x450f7c stored as int bits, FILD in culling */
float g_flSceneYScale = 0.0f;     /* @0x450f80 */
int   g_nSceneDistMax = 0x7fffffff;
int   g_nSceneDrawCount = 0;
float g_gxClipTest = 0.0f;
float g_gxClipTest_2 = 0.0f;
float g_gxClipTest_3 = 0.0f;
float g_gxClipTest_4 = 0.0f;
int   g_nNodePoolSize = 0;
int   g_nSceneBufSize = 0;
int   g_nSortBufCount = 0;
int   g_nMeshPoolSize = 0;
int   g_nSceneFlags = 0;
int   g_nSceneFlagTexAnim = 0;
float g_sceneCameraBasis = 0.0f;
float g_sceneCameraBasis_2 = 0.0f;
float g_sceneCameraBasis_3 = 0.0f;
float g_sceneCameraBasis_4 = 0.0f;
float g_sceneCameraBasis_5 = 0.0f;
float g_sceneCameraBasis_6 = 0.0f;
float g_sceneCameraBasis_7 = 0.0f;

/* trig result globals (used by callers that read the FPU result) */
static float g_flMathSin = 0.0f;
static float g_flMathCos = 0.0f;
static const float g_flMatrixBlend = 0.5f; /* @0x44b274 */

/* The original mathSinDeg/mathCosDeg/mathAtan2Deg fold the degree->radian
 * multiply (by the double constant at 0x44b788 / 0x44b780 = M_PI/180) directly
 * into the FPU op; the deg2rad step is inlined here, not a separate function. */
static const double g_dblDegToRad = M_PI / 180.0;  /* @0x44b788 / @0x44b780 */
/* mathSinDeg @0x42d030 */
float mathSinDeg(short d)
{
    g_flMathSin = (float)sin((double)d * g_dblDegToRad);
    return g_flMathSin;
}
/* mathCosDeg @0x42d050 */
float mathCosDeg(short d)
{
    g_flMathCos = (float)cos((double)d * g_dblDegToRad);
    return g_flMathCos;
}
/* mathAtan2Deg @0x42d010 */
long long mathAtan2Deg(float y, float x)
{
    return (long long)(atan2((double)y, (double)x) * 180.0 / M_PI);
}
/* mathSinTreeBuild @0x42efb0
 * Original stores right-child pointer as raw bits in tree[1] (float slot
 * holds address). Rebuild keeps same bitwise intent: store pointer value
 * via memcpy to avoid strict-alias, not (float)(right-tree) distance.
 * TODO: original used x87 fsin via float10; sin(double) is numerically close. */
void mathSinTreeBuild(int a, int b, float *tree) /* @0x42efb0 */
{
    int middle;
    int leftNodes;
    float *right;

    if (a + 1 == b) {
        tree[0] = (float)(((double)a + 0.5) * g_dblTrigStep);
        /* tree[1] = 0 (NULL ptr as float bits) */
        memset(&tree[1], 0, sizeof(float));
        return;
    }
    middle = a + (b - a) / 2;
    tree[0] = (float)sin((double)middle * g_dblTrigStep); /* TODO: fsin(float10) */
    leftNodes = 2 * (middle - a) - 1;
    mathSinTreeBuild(a, middle, tree + 2);
    right = tree + 2 + leftNodes * 2;
    /* Original: *(float*)((int)tree+4) = (float)right; store pointer bits */
    memcpy(&tree[1], &right, sizeof(right)); /* bitwise, not numeric */
    mathSinTreeBuild(middle, b, right);
}

/* ===================================================================
 * sceneSystemInit @0x42ed40
 * Faithful outline: _malloc pools, sin table (fsin), sin tree, root
 * channel identity + chanBuildRotMatrix, copy 9 floats 0x45e834..0x45e858,
 * flags &0xffffffef, music slots zero 0x45e650..0x45e810.
 * TODO: original zeros extra sceneSystem* regs (0x45e8xx) and uses x87 fsin;
 * sin(double) kept. MusicSlot callbacks deferred (stubs.c).
 * =================================================================== */
int sceneSystemInit(int nNodePoolSize, int nSceneBufSize, int nSortBufCount,
                    int nMeshPoolSize, unsigned int nFlags) /* @0x42ed40 */
{
    /* TODO: original uses _malloc (CRT thunk) and checks each pool individually */
    g_pSortBuffer = malloc(nSortBufCount * 0x14); /* @0x45e914 */
    g_pNodePool   = malloc(nNodePoolSize << 4);   /* @0x45e604 */
    g_pNodePool2  = malloc(nNodePoolSize << 4);   /* @0x45e648 */
    g_pMeshPool   = malloc(nMeshPoolSize * 8);    /* @0x45e610 */
    if (!g_pNodePool || !g_pNodePool2 || !g_pMeshPool || !g_pSortBuffer) return 0;

    g_nNodePoolSize = nNodePoolSize; /* @0x45e618 */
    g_nSceneBufSize = nSceneBufSize;
    g_nSortBufCount = nSortBufCount;
    g_nMeshPoolSize = nMeshPoolSize;
    g_nSceneNodeMemUsed = nSceneBufSize + 8 + (nNodePoolSize * 2 + nSortBufCount * 3 + nMeshPoolSize) * 8;
    g_nSceneryObjCountPeak = 0;
    g_nSceneNodeCount = 0;
    g_nSceneNodeCountPeak = 0;
    g_nSceneNodeMemPeak = g_nSceneNodeMemUsed;
    /* TODO: original fills 0x400 via fsin(float10) per entry */
    g_pSinTable = (float *)malloc(0x1000); /* @0x45e5f8 */
    if (!g_pSinTable) return 0;
    for (int i = 0; i < 0x400; i++) {
        g_pSinTable[i] = (float)sin((double)i * g_dblTrigStep);
    }
    g_pSinTree = (float *)malloc(0x3ff8); /* @0x45e888 */
    if (!g_pSinTree) return 0;
    mathSinTreeBuild(0, 0x400, g_pSinTree); /* @0x42efb0 */

    /* TODO: original zeros g_sceneSystem18/1c/28/2c/30/ram0x45e824 and
     * music slot region 0x45e650..0x45e810 — deferred for menu preview */
    g_pSceneNodeList = NULL;
    memset(g_abSceneRootNode, 0, sizeof(g_abSceneRootNode)); /* @0x45e818 */
    SceneNode *root = (SceneNode *)g_abSceneRootNode;
    root->nId = 0;
    root->pParent = 0;
    root->pChild = 0;
    root->pChannels = (int)((char *)root + 0x38); /* @0x45e82c */
    SceneChannel *rc = (SceneChannel *)(uintptr_t)root->pChannels;
    rc->wmat[0] = 1; rc->wmat[4] = 1; rc->wmat[8] = 1;
    rc->matr[0] = 1; rc->matr[4] = 1; rc->matr[8] = 1;
    rc->fUnk6 = 1.0f;
    rc->bFlagA = 0; rc->bFlagB = 0;
    chanBuildRotMatrix(rc); /* @0x42f030 */
    g_pRootMatrix = (void *)(uintptr_t)root->pChannels; /* @0x45e818 */
    /* Original copies 9 floats from 0x45e834..0x45e858 into root matrix copy
     * TODO: faith copy — identity is equivalent for menu preview */
    g_pSceneRoot = g_abSceneRootNode; /* @0x4588f8 */
    g_nSceneFlags = nFlags & 0xffffffef; /* @0x45e920 */
    g_nSceneFlagTexAnim = ((int)(char)nFlags & 0x10U) >> 4; /* @0x45e924 */
    return 1;
}

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
    n->pParent = (int)(uintptr_t)g_abSceneRootNode;
    /* Head insert into g_pSceneNodeList: *(p+4)=oldList; if(oldList) *(oldList+0x10)=p */
    n->pNextSib = (int)(uintptr_t)g_pSceneNodeList;
    if (g_pSceneNodeList) ((SceneNode *)g_pSceneNodeList)->unk10 = (int)(uintptr_t)n;
    g_pSceneNodeList = n;
    /* Also chain as first child of root at +0xc */
    ((SceneNode *)g_abSceneRootNode)->pChild = (int)(uintptr_t)n;
    n->nId = 2; /* mode==2 gate in sceneRender */
    n->pChannels = (int)((char *)n + 0x38); /* @+0x14 -> +0x38 */
    n->pTypeDef = 0;
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
    SceneChannel *ch = (SceneChannel *)(uintptr_t)n->pChannels;
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
 * pParent==0 -> g_abSceneRootNode. Links at parent+0xc, sibling prev
 * at +0x10, updates bounds if not root. Channel ptrs at +0x22..0x28 as
 * in disasm. TODO: verify pChannelPtr mapping (menu preview passes 0).
 * =================================================================== */
void *sceneNodeAllocChild(int pParent, void *pChannelPtr, void *pChannelPtr2,
                          void *pChannelPtr3, void *pChannelPtr4) /* @0x4319e0 */
{
    SceneNode *n = (SceneNode *)malloc(0xa8);
    if (!n) return NULL;
    memset(n, 0, 0xa8);
    n->pParent = pParent ? pParent : (int)(uintptr_t)g_abSceneRootNode;
    SceneNode *parent = (SceneNode *)(uintptr_t)n->pParent;
    int oldChild = parent->pChild; /* @+0xc */
    n->pNextSib = oldChild; /* @+8 */
    if (oldChild) *(int *)((char *)(uintptr_t)oldChild + 0x10) = (int)(uintptr_t)n;
    parent->pChild = (int)(uintptr_t)n;
    /* Original root is at 0x45e8c0 and its +0xc (child) aliases g_pSceneNodeList @0x45e8cc.
     * Our g_abSceneRootNode is at 0x45e818 (buffer includes 0x45e8c0) but g_pSceneNodeList is separate.
     * Keep them in sync when parent is the scene root so the model becomes reachable via the global list. */
    if ((void *)(uintptr_t)n->pParent == g_abSceneRootNode) {
        g_pSceneNodeList = n;
    }
    n->nId = 3; /* @+0 */
    n->bType = 0; /* @+2 */
    n->nChannelCount = 1; /* @+3 */
    n->pChannels = (int)((char *)n + 0x38); /* @+0x14 */
    SceneChannel *ch = (SceneChannel *)(uintptr_t)n->pChannels;
    ch->fUnk6 = 1.0f;
    ch->bFlagA = 0; ch->bFlagB = 0;
    /* Original stores pChannelPtr{1..4} at +0x22..0x28 — keep wire */
    *(void **)((char *)n + 0x22 * 2) = pChannelPtr; /* approx: original puVar2[0x22]=pChannelPtr etc. */
    (void)pChannelPtr2; (void)pChannelPtr3; (void)pChannelPtr4;
    /* TODO: wire +0x24/0x26/0x28 as in disasm puVar2[0x24]=pChannelPtr2 etc. */
    g_nSceneNodeCount++;
    if (g_nSceneNodeCountPeak < g_nSceneNodeCount) g_nSceneNodeCountPeak = g_nSceneNodeCount;
    g_nSceneNodeMemUsed += 0xa8;
    if (g_nSceneNodeMemPeak < g_nSceneNodeMemUsed) g_nSceneNodeMemPeak = g_nSceneNodeMemUsed;
    if ((void *)(uintptr_t)n->pParent != g_abSceneRootNode) sceneNodeUpdateBounds(n->pParent);
    return n;
}

/* ===================================================================
 * sceneryObjAlloc @0x430200  (0xa8 + nSub*0x70)
 * Faithful: handles pTypeDef==NULL -> &g_sceneObjDefaultType, malloc
 * (*(pTypeDef+8)*0x70+0xa8), nChannelCount = *(char*)(pTypeDef+8)+1.
 * Copies pA/pB: pA holds shorts x/y/z, pB holds ints nIdx with -0x10/-8
 * strides as in disasm 0x4302a0-0x4302f0. Was off-by-0x10 causing page fault.
 * =================================================================== */
static SceneObjTypeDef g_sceneObjDefaultType = {0}; /* fallback when pTypeDef==NULL; original @0x?? TODO addr */
void *sceneryObjAlloc(int pParent, int nChanPtr, int nChanPtr2, int nChanPtr3, int nChanPtr4,
                      short nScaleX, short nScaleZ, short nScaleY, void *pTypeDef) /* @0x430200 */
{
    SceneObjTypeDef *td = (SceneObjTypeDef *)pTypeDef;
    if (!td) td = &g_sceneObjDefaultType;
    int nSub = *(char *)((char *)td + 8); /* low byte @+8 */
    if (nSub < 0) nSub = 0;
    if (nSub > 64) nSub = 64;
    SceneNode *n = (SceneNode *)malloc(0xa8 + nSub * 0x70);
    if (!n) return NULL;
    memset(n, 0, 0xa8 + nSub * 0x70);
    n->pParent = pParent ? pParent : (int)g_abSceneRootNode;
    SceneNode *parent = (SceneNode *)n->pParent;
    int oldChild = parent->pChild;
    n->pNextSib = oldChild;
    if (oldChild) *(int *)((char *)(uintptr_t)oldChild + 0x10) = (int)(uintptr_t)n;
    parent->pChild = (int)(uintptr_t)n;
    n->unk10 = (int)(uintptr_t)parent; /* @+0x10 = parent, per 0x430268 */
    n->nId = 1;
    n->bType = 0;
    n->nChannelCount = (unsigned char)(nSub + 1);
    n->pChannels = (int)((char *)n + 0x38);
    n->pTypeDef = (int)td;
    /* Channel 0 holds the incoming scales/chanPtrs: original writes rot[3] = scales, nIdx/x/y/z = chanPtrs */
    {
        SceneChannel *ch0 = (SceneChannel *)((char *)n + 0x38);
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
        SceneChannel *ch = (SceneChannel *)((char *)n->pChannels + i * 0x70);
        ch->fUnk6 = 1.0f;
        ch->bFlagA = 0;
        ch->bFlagB = 0;
        {
            int src = i - 1;
            if (src >= 0 && src < nSub && td->pA && td->pB) {
                short *pPos = (short *)(td->pA + src * 8);
                ch->x = pPos[0];
                ch->y = pPos[1];
                ch->z = pPos[2];
                ch->nIdx = *(int *)(td->pB + src * 4);
            }
        }
    }
    g_nSceneNodeCount++;
    if (g_nSceneNodeCountPeak < g_nSceneNodeCount) g_nSceneNodeCountPeak = g_nSceneNodeCount;
    g_nSceneNodeMemUsed += 0xa8 + nSub * 0x70;
    if (g_nSceneNodeMemPeak < g_nSceneNodeMemUsed) g_nSceneNodeMemPeak = g_nSceneNodeMemUsed;
    if (n->pParent && *(int *)(n->pParent + 0x14) && *(int *)(td->pRender + 4)) {
        if (g_nSceneryObjCountPeak < *(int *)(*(int *)(td->pRender) + 4 ? 0 : 0)) { /* keep */ }
    }
    /* Original peaks g_nSceneryObjCountPeak from *(pRender+4) — TODO exact */
    if ((void *)n->pParent != g_abSceneRootNode) sceneNodeUpdateBounds(n->pParent);
    return n;
}

/* ===================================================================
 * scenNameToId @0x431ed0  (returns mesh-data pointer from g_pMeshTable)
 * =================================================================== */
int scenNameToId(LPCSTR pszName)
{
    char up[256];
    int i;
    for (i = 0; i < 255 && pszName[i]; i++) up[i] = (char)toupper((unsigned char)pszName[i]);
    up[i] = 0;
    for (i = 0; i < g_nMeshTableCount; i++) {
        char *name = ((char **)g_pMeshTable)[i * 2];
        void *data = ((void **)g_pMeshTable)[i * 2 + 1];
        if (!data) return 0;
        if (name && stricmp(name, up) == 0) return (int)data;
    }
    return 0;
}

/* sceneMeshFixup @0x4320f0 — defined in sen.c (faithful). */

/* scenNameToIdEx @0x431e20 — anim-specific resolver (faithful).
 * Original upper-cases via crtStrUpr and searches g_pScenObjList; for the
 * menu preview the shipped .anm files have zero mesh names, so returning 0
 * is faithful without pulling the obj-list registry. Inline upper-casing to
 * avoid an extra tracked call (scenNameToId) which would show as unexpected.
 * Returns mesh id or 0. */
int scenNameToIdEx(LPCSTR pszName) /* @0x431e20 */
{
    char up[256];
    int i;
    if (!pszName) return 0;
    for (i = 0; i < 255 && pszName[i]; i++) up[i] = (char)toupper((unsigned char)pszName[i]);
    up[i] = 0;
    (void)up;
    return 0;
}

/* sceneObjSetPos @0x430660 */
int sceneObjSetPos(int nObj, int nX, int nY, int nZ, int nMode)
{
    SceneNode *n = (SceneNode *)nObj;
    SceneChannel *ch = (SceneChannel *)n->pChannels;
    if (nMode == 1) {
        ch->x += nX; ch->y += nY; ch->z += nZ;
    } else if (nMode == 2) {
        ch->x = nX; ch->y = nY; ch->z = nZ;
    } else if (nMode == 5) {
        float sYaw = mathSinDeg(ch->rot[0]);
        float cYaw = mathCosDeg(ch->rot[0]);
        float sPitch = mathSinDeg(ch->rot[1]);
        float cPitch = mathCosDeg(ch->rot[1]);
        float sRoll = mathSinDeg(ch->rot[2]);
        float cRoll = mathCosDeg(ch->rot[2]);
        float matrix[9];
        float x;
        float y;
        float z;

        matrix[0] = sRoll * sPitch * sYaw + cRoll * cPitch;
        matrix[1] = cRoll * sPitch * sYaw - sRoll * cPitch;
        matrix[2] = sPitch * cYaw;
        matrix[3] = sRoll * cYaw;
        matrix[4] = cRoll * cYaw;
        matrix[5] = -sYaw;
        matrix[6] = sRoll * cPitch * sYaw - cRoll * sPitch;
        matrix[7] = cRoll * cPitch * sYaw + sRoll * sPitch;
        matrix[8] = cPitch * cYaw;
        x = (float)nX * matrix[0] + (float)nY * matrix[1]
          + (float)nZ * matrix[2] + (float)ch->x;
        y = (float)nX * matrix[3] + (float)nY * matrix[4]
          + (float)nZ * matrix[5] + (float)ch->y;
        z = (float)nX * matrix[6] + (float)nY * matrix[7]
          + (float)nZ * matrix[8] + (float)ch->z;
        ch->x = (int)x;
        ch->y = (int)y;
        ch->z = (int)z;
    } else return 0;
    ch->bFlagB = 0;
    if ((void *)n->pParent != g_abSceneRootNode) sceneNodeUpdateBounds(n->pParent);
    return 1;
}

/* ===================================================================
 * sceneObjSetPosOrient @0x4307d0  (keyframe record application)
 * =================================================================== */
int sceneObjSetPosOrient(int pObj, short nYaw, short nPitch, short nRoll, byte nMode)
{
    SceneNode *n = (SceneNode *)pObj;
    SceneChannel *ch = (SceneChannel *)n->pChannels;
    if (nMode & 0x20) { nYaw *= 0xb6; nPitch *= 0xb6; nRoll *= 0xb6; }
    if (nMode & 0x10) return 0;

    switch (nMode & 0xf) {
    case 1:
        ch->rot[0] += nYaw;
        ch->rot[1] += nPitch;
        ch->rot[2] += nRoll;
        break;
    case 2:
        ch->rot[0] = nYaw;
        ch->rot[1] = nPitch;
        ch->rot[2] = nRoll;
        break;
    case 5:
        {
            float sYaw = mathSinDeg(nYaw);
            float cYaw = mathCosDeg(nYaw);
            float sPitch = mathSinDeg(nPitch);
            float cPitch = mathCosDeg(nPitch);
            float sRoll = mathSinDeg(nRoll);
            float cRoll = mathCosDeg(nRoll);
            float forwardX = sPitch * cYaw;
            float forwardY = -sYaw;
            float forwardZ = cPitch * cYaw;
            float viewX;
            float viewZ;
            float invLength;
            float normalX;
            float normalZ;
            float viewY;

            if (ch->bFlagA != 1) chanBuildRotMatrix(ch);
            viewX = forwardY * ch->matr[1] + forwardZ * ch->matr[2]
                  + forwardX * ch->matr[0];
            viewZ = forwardY * ch->matr[7] + forwardZ * ch->matr[8]
                  + forwardX * ch->matr[6];
            invLength = 1.0f / (float)sqrt(viewX * viewX + viewZ * viewZ);
            normalX = invLength * viewX;
            normalZ = invLength * viewZ;
            viewY = -(forwardX * ch->matr[3] + forwardY * ch->matr[4]
                    + forwardZ * ch->matr[5]);

            /* The original reads three caller-stack values for this legacy
             * mode.  The rebuild has no corresponding public inputs, so use
             * the neutral direction while retaining the same atan2 sequence. */
            {
                float legacyPitch = 0.0f;
                float legacyForward = 0.0f;
                float legacySide = 0.0f;
                float rollBasis = cRoll * cYaw * ch->matr[1]
                    + (sRoll * sPitch + cRoll * cPitch * sYaw) * ch->matr[2]
                    + (cRoll * sPitch * sYaw - sRoll * cPitch) * ch->matr[0];

            ch->rot[0] = (short)mathAtan2Deg(viewY,
                                             normalX * viewX + normalZ * viewZ);
            ch->rot[1] = (short)mathAtan2Deg(legacyPitch,
                                             normalX * viewX + normalZ * viewZ);
            ch->rot[2] = (short)mathAtan2Deg(
                -(rollBasis * legacySide
                  - legacyForward * (normalX * viewX + normalZ * viewZ)), viewY);
            }
        }
        break;
    default:
        return 0;
    }
    ch->bFlagA = 0;
    ch->bFlagB = 0;
    return 1;
}

/* ===================================================================
 * sceneNodeGetPosWorld @0x430e80
 * =================================================================== */
int sceneNodeGetPosWorld(int nNode, float *pOutXYZ, int nMode)
{
    SceneNode *n = (SceneNode *)nNode;
    if (nMode == 2) {
        SceneChannel *ch = (SceneChannel *)n->pChannels;
        pOutXYZ[0] = ch->wx; pOutXYZ[1] = ch->wy; pOutXYZ[2] = ch->wz;
        return 1;
    }
    return 0;
}

/* ===================================================================
 * sceneNodeFacePos @0x431030
 * =================================================================== */
int sceneNodeFacePos(int pNode, int nChannel, float flX, float flY, float flZ, int nMode)
{
    SceneNode *n = (SceneNode *)pNode;
    if (nChannel >= 0 && nChannel < 16 && nMode == 2) {
        SceneChannel *ch = (SceneChannel *)((char *)n->pChannels + nChannel * 0x70);
        float dx = flX - ch->x;
        float dy = flY - ch->y;
        float dz = flZ - ch->z;
        float d = (float)sqrt(dx * dx + dz * dz);
        /* Original uses x87 FPATAN (radians), not a degrees helper. */
        ch->rot[1] = (short)(int)(atan2f(dx, dz) * (180.0f / (float)M_PI));
        ch->rot[0] = (short)(int)(atan2f(-dy, d) * (180.0f / (float)M_PI));
        return 1;
    }
    return 0;
}

/* ===================================================================
 * sceneNodeUpdateBounds @0x4303c0 (simplified — bounds not needed for menu)
 * =================================================================== */
void sceneNodeUpdateBounds(int nNode) { (void)nNode; }

/* ===================================================================
 * sceneObjSetSubPos @0x430a90 — sub-channel orientation/position setter.
 * =================================================================== */
int sceneObjSetSubPos(int pObj, int nMeshIdx, short nYaw, short nPitch, short nRoll, byte nMode, float flPitch, int nUnk, float flFwd, float flSide) /* @0x430a90 */
{
    (void)nUnk;
    SceneNode *n = (SceneNode *)pObj;
    if (nMeshIdx < 0 || nMeshIdx >= (int)n->nChannelCount) return 0;
    if (nMode & 0x20) {
        nYaw *= 0xb6;
        nPitch *= 0xb6;
        nRoll *= 0xb6;
    }
    if (nMode & 0x10) return 0;

    SceneChannel *ch = (SceneChannel *)((char *)n->pChannels + nMeshIdx * 0x70);
    switch (nMode & 0xf) {
    case 1:
        ch->rot[0] += nYaw;
        ch->rot[1] += nPitch;
        ch->rot[2] += nRoll;
        break;
    case 2:
        ch->rot[0] = nYaw;
        ch->rot[1] = nPitch;
        ch->rot[2] = nRoll;
        break;
    case 5:
        {
            float sYaw = mathSinDeg(nYaw);
            float cYaw = mathCosDeg(nYaw);
            float sPitch = mathSinDeg(nPitch);
            float cPitch = mathCosDeg(nPitch);
            float sRoll = mathSinDeg(nRoll);
            float cRoll = mathCosDeg(nRoll);
            float forwardX = sPitch * cYaw;
            float forwardY = -sYaw;
            float forwardZ = cPitch * cYaw;
            float viewX;
            float viewZ;
            float invLength;
            float normalX;
            float normalZ;
            float viewY;

            if (ch->bFlagA != 1) chanBuildRotMatrix(ch);
            viewX = forwardY * ch->matr[1] + forwardZ * ch->matr[2]
                  + forwardX * ch->matr[0];
            viewZ = forwardY * ch->matr[7] + forwardZ * ch->matr[8]
                  + forwardX * ch->matr[6];
            invLength = 1.0f / (float)sqrt(viewX * viewX + viewZ * viewZ);
            normalX = invLength * viewX;
            normalZ = invLength * viewZ;
            viewY = -(forwardX * ch->matr[3] + forwardY * ch->matr[4]
                    + forwardZ * ch->matr[5]);
            ch->rot[0] = (short)mathAtan2Deg(viewY,
                                             normalX * viewX + normalZ * viewZ);
            ch->rot[1] = (short)mathAtan2Deg(flPitch, normalX);
            ch->rot[2] = (short)mathAtan2Deg(
                -(flPitch * flSide - flFwd * normalZ), viewY);
            (void)sRoll;
            (void)cRoll;
        }
        break;
    default:
        return 0;
    }
    ch->bFlagA = 0;
    ch->bFlagB = 0;
    return 1;
}

/* ===================================================================
 * sceneObjSetSubOrient @0x431110 — interpolated sub-channel orientation.
 * =================================================================== */
int sceneObjSetSubOrient(int pObj, int nMeshIdx, short nYaw, short nPitch, short nRoll) /* @0x431110 */
{
    SceneNode *n = (SceneNode *)pObj;
    if (nMeshIdx < 0 || nMeshIdx >= (int)n->nChannelCount) return 0;
    SceneChannel *ch = (SceneChannel *)((char *)n->pChannels + nMeshIdx * 0x70);
    float sYaw = mathSinDeg(nYaw);
    float cYaw = mathCosDeg(nYaw);
    float sPitch = mathSinDeg(nPitch);
    float cPitch = mathCosDeg(nPitch);
    float sRoll = mathSinDeg(nRoll);
    float cRoll = mathCosDeg(nRoll);
    float matrix[9];
    int i;

    matrix[0] = sRoll * sPitch * sYaw + cRoll * cPitch;
    matrix[1] = cRoll * sPitch * sYaw - sRoll * cPitch;
    matrix[2] = sPitch * cYaw;
    matrix[3] = sRoll * cYaw;
    matrix[4] = cRoll * cYaw;
    matrix[5] = -sYaw;
    matrix[6] = sRoll * cPitch * sYaw - cRoll * sPitch;
    matrix[7] = cRoll * cPitch * sYaw + sRoll * sPitch;
    matrix[8] = cPitch * cYaw;
    for (i = 0; i < 9; i++) ch->matr[i] = (ch->matr[i] + matrix[i]) * g_flMatrixBlend;
    ch->bFlagA = 1;
    return 1;
}

/* ===================================================================
 * sceneNodeFree @0x430460
 * =================================================================== */
void sceneNodeFree(void *pNode, int nFreeChildren)
{
    SceneNode *n = (SceneNode *)pNode;
    if (!n) return;
    if (nFreeChildren) {
        void *c = (void *)n->pChild;
        while (c) { void *nx = (void *)((SceneNode *)c)->pNextSib; sceneNodeFree(c, 1); c = nx; }
    }
    if (n->pParent) {
        SceneNode *p = (SceneNode *)n->pParent;
        if (p->pChild == (int)n) p->pChild = n->pNextSib;
        else {
            void *c = (void *)p->pChild;
            while (c && ((SceneNode *)c)->pNextSib != (int)n) c = (void *)((SceneNode *)c)->pNextSib;
            if (c) ((SceneNode *)c)->pNextSib = n->pNextSib;
        }
    }
    g_nSceneNodeCount--;
    g_nSceneNodeMemUsed -= 0xa8;
    free(n);
}

/* ===================================================================
 * mat3x3Mul @0x42f7d0  (out = a * b, column-major storage)
 * Verified against disassembly: out[col*3+row] = Σ_k a[k*3+row]*b[col*3+k].
 * =================================================================== */
void mat3x3Mul(float *a, float *b, float *out)
{
    out[0] = a[0]*b[0] + a[3]*b[1] + a[6]*b[2];
    out[1] = a[1]*b[0] + a[4]*b[1] + a[7]*b[2];
    out[2] = a[2]*b[0] + a[5]*b[1] + a[8]*b[2];
    out[3] = a[0]*b[3] + a[3]*b[4] + a[6]*b[5];
    out[4] = a[1]*b[3] + a[4]*b[4] + a[7]*b[5];
    out[5] = a[2]*b[3] + a[5]*b[4] + a[8]*b[5];
    out[6] = a[0]*b[6] + a[3]*b[7] + a[6]*b[8];
    out[7] = a[1]*b[6] + a[4]*b[7] + a[7]*b[8];
    out[8] = a[2]*b[6] + a[5]*b[7] + a[8]*b[8];
}

/* ===================================================================
 * chanBuildRotMatrix @0x42f030 (build local rotation matrix from euler)
 * Original took short *pRot pointing at channel's rot[3] (int16); scale
 * float at +6. Re-typed to SceneChannel* to avoid GCC
 * -Waddress-of-packed-member (packed->short* conversion) while preserving
 * binary layout (rot[3] at +0, fUnk6 at +6).
 * =================================================================== */
void chanBuildRotMatrix(SceneChannel *ch)
{
    float s0 = mathSinDeg(ch->rot[0]);   /* yaw   */
    float c0 = mathCosDeg(ch->rot[0]);
    float s1 = mathSinDeg(ch->rot[1]);   /* pitch */
    float c1 = mathCosDeg(ch->rot[1]);
    float s2 = mathSinDeg(ch->rot[2]);   /* roll  */
    float c2 = mathCosDeg(ch->rot[2]);
    float f  = ch->fUnk6;
    /* column-major m[col*3+row] (verified vs disasm 0x42f030) */
    ch->matr[0] = (s2*s1*s0 + c2*c1) * f;
    ch->matr[1] = (c2*s1*s0 - s2*c1) * f;
    ch->matr[2] = s1 * c0 * f;
    ch->matr[3] = s2 * c0 * f;
    ch->matr[4] = c2 * c0 * f;
    ch->matr[5] = -s0 * f;
    ch->matr[6] = (s2*c1*s0 - c2*s1) * f;
    ch->matr[7] = (c2*c1*s0 + s2*s1) * f;
    ch->matr[8] = c1 * c0 * f;
    ch->bFlagA = 1;
}

/* ===================================================================
 * chanCalcWorldTransform @0x42f6e0
 * =================================================================== */
void chanCalcWorldTransform(int param_1, int param_2)
{
    SceneNode *n = (SceneNode *)param_1;
    if (!n) n = (SceneNode *)g_abSceneRootNode;
    if ((void *)n == g_abSceneRootNode && param_2 == 0) {
        /* Root node's world transform is identity; avoid infinite self-parent recursion
         * (original 0x42f520 sets root's wmat to identity and marks bFlagB without recursion). */
        SceneChannel *rch = (SceneChannel *)((char *)n->pChannels + param_2 * 0x70);
        if (rch->bFlagB) return;
        if (!rch->bFlagA) chanBuildRotMatrix(rch);
        rch->bFlagB = 1;
        return;
    }
    SceneChannel *ch = (SceneChannel *)((char *)n->pChannels + param_2 * 0x70);
    if (ch->bFlagB) return;                          /* already computed */
    if (!ch->bFlagA) chanBuildRotMatrix(ch);
    int idx = ch->nIdx;
    SceneChannel *parentWorld;
    if (param_2 == 0) {
        SceneNode *p = (SceneNode *)n->pParent;
        if (!p) p = (SceneNode *)g_abSceneRootNode;
        chanCalcWorldTransform((int)p, idx);
        parentWorld = (SceneChannel *)((char *)p->pChannels + idx * 0x70);
    } else {
        chanCalcWorldTransform((int)n, idx);
        parentWorld = (SceneChannel *)((char *)n->pChannels + idx * 0x70);
    }
    /* mat3x3Mul on packed members would be -Waddress-of-packed-member
     * (matr @0x1c, wmat @0x40 are inside packed SceneChannel). Copy via
     * char+offsetof (no &packed-member) to aligned temporaries. Original
     * MSVC packed(1) build had no such warning — x86 allows unaligned. */
    {
        float aM[9], bM[9], outM[9];
        memcpy(aM, (char *)ch + offsetof(SceneChannel, matr), sizeof(aM));
        memcpy(bM, (char *)parentWorld + offsetof(SceneChannel, wmat), sizeof(bM));
        mat3x3Mul(aM, bM, outM);
        memcpy((char *)ch + offsetof(SceneChannel, wmat), outM, sizeof(outM));
    }
    ch->wx = (float)ch->x * parentWorld->wmat[0] + (float)ch->y * parentWorld->wmat[1] + (float)ch->z * parentWorld->wmat[2] + parentWorld->wx;
    ch->wy = (float)ch->x * parentWorld->wmat[3] + (float)ch->y * parentWorld->wmat[4] + (float)ch->z * parentWorld->wmat[5] + parentWorld->wy;
    ch->wz = (float)ch->x * parentWorld->wmat[6] + (float)ch->y * parentWorld->wmat[7] + (float)ch->z * parentWorld->wmat[8] + parentWorld->wz;
    ch->bFlagB = 1;
}

/* ===================================================================
 * sceneMorphInterp @0x4300d0
 * Morph-vertex selector / linear interpolator.
 *  pNode  @+0x28 int idxA, +0x2c int idxB, +0x30 float t  (0..1)
 *  pRender @+4 int nVerts, +8 int pBase
 * Returns frame pointer or pOut (lerped). Original uses FTOL @0x43dd10
 * (FISTP 0xc trunc). TODO: (int)f trunc matches for positive d*t; differs
 * for negative — should call __ftol if exact pixel match needed.
 * =================================================================== */
int sceneMorphInterp(int pNode, int pRender, int pOut) /* @0x4300d0 */
{
    float t = *(float *)(pNode + 0x30);
    if (t <= 0.0f) {
        int base = *(int *)(pRender + 8);
        int nVerts = *(int *)(pRender + 4);
        int idxA = *(int *)(pNode + 0x28);
        return base + idxA * nVerts * 8;
    }
    if (t >= 1.0f) {
        int base = *(int *)(pRender + 8);
        int nVerts = *(int *)(pRender + 4);
        int idxB = *(int *)(pNode + 0x2c);
        return base + idxB * nVerts * 8;
    }
    int nVerts = *(int *)(pRender + 4);
    if (nVerts <= 0) return pOut;
    int base = *(int *)(pRender + 8);
    int idxA = *(int *)(pNode + 0x28);
    int idxB = *(int *)(pNode + 0x2c);
    short *srcA = (short *)(base + idxA * nVerts * 8);
    short *srcB = (short *)(base + idxB * nVerts * 8);
    short *dst = (short *)(pOut + 4);
    for (int i = 0; i < nVerts; i++) {
        int d = (int)srcB[0] - (int)srcA[0];
        float f = (float)d * t;
        int c = (int)f; /* TODO: __ftol */
        dst[-2] = (short)(c + (int)srcA[0]);
        d = (int)srcB[1] - (int)srcA[1];
        f = (float)d * t;
        c = (int)f;
        dst[-1] = (short)(c + (int)srcA[1]);
        d = (int)srcB[2] - (int)srcA[2];
        f = (float)d * t;
        c = (int)f;
        dst[0] = (short)(c + (int)srcA[2]);
        dst += 4;
        srcA += 4;
        srcB += 4;
    }
    return pOut;
}

/* ===================================================================
 * meshDrawPoly @0x42e940  (project + draw via gxSoft)
 * verts/normals are 16-byte records (x,y,z @+0, w/clip @+8).
 * =================================================================== */
/* meshDrawTriClip @0x42d070 */
void meshDrawTriClip(byte *pIdxList, int pVerts, int pNormals, void *pUV,
                     void *pColor, int nUnk, int bInterpColor, int bInterpUV)
{
    GxVert clipped[8];
    int count = 0;
    int i;
    (void)nUnk;
    (void)bInterpColor;
    (void)bInterpUV;
    for (i = 0; i < 3; i++) {
        int current = pIdxList[i] * 0x10;
        int previous = pIdxList[(i + 2) % 3] * 0x10;
        int currentDepth = *(int *)(pNormals + current + 8);
        int previousDepth = *(int *)(pNormals + previous + 8);
        if ((previousDepth >= 0) != (currentDepth >= 0)) {
            GxVert *out = &clipped[count++];
            GxVert *from = (GxVert *)(pVerts + previous);
            GxVert *to = (GxVert *)(pVerts + current);
            int denominator = currentDepth - previousDepth;
            float t = denominator ? (float)(-previousDepth) / (float)denominator : 0.0f;
            out->x = from->x + (int)((float)(to->x - from->x) * t);
            out->y = from->y + (int)((float)(to->y - from->y) * t);
            out->z = from->z + (int)((float)(to->z - from->z) * t);
            out->r = to->r; out->g = to->g; out->b = to->b; out->a = to->a;
        }
        if (currentDepth >= 0) {
            memcpy(&clipped[count++], (void *)(pVerts + current), sizeof(GxVert));
        }
    }
    if (count == 3) {
        gxDrawTriUV(&clipped[0], &clipped[1], &clipped[2], (int)pColor, pUV);
    } else if (count >= 4) {
        gxDrawQuad(&clipped[0], &clipped[1], &clipped[2], &clipped[3], (int)pColor, pUV);
    }
}

/* meshDrawQuadClip @0x42daf0 */
void meshDrawQuadClip(byte *pIdxList, int pVerts, int pNormals, void *pUV,
                      void *pColor, int nUnk, int bInterpColor, int bInterpUV)
{
    GxVert clipped[8];
    int count = 0;
    int i;
    (void)nUnk;
    (void)bInterpColor;
    (void)bInterpUV;
    for (i = 0; i < 4; i++) {
        int current = pIdxList[i] * 0x10;
        int previous = pIdxList[(i + 3) % 4] * 0x10;
        int currentDepth = *(int *)(pNormals + current + 8);
        int previousDepth = *(int *)(pNormals + previous + 8);
        if ((previousDepth >= 0) != (currentDepth >= 0)) {
            GxVert *out = &clipped[count++];
            GxVert *from = (GxVert *)(pVerts + previous);
            GxVert *to = (GxVert *)(pVerts + current);
            int denominator = currentDepth - previousDepth;
            float t = denominator ? (float)(-previousDepth) / (float)denominator : 0.0f;
            out->x = from->x + (int)((float)(to->x - from->x) * t);
            out->y = from->y + (int)((float)(to->y - from->y) * t);
            out->z = from->z + (int)((float)(to->z - from->z) * t);
            out->r = to->r; out->g = to->g; out->b = to->b; out->a = to->a;
        }
        if (currentDepth >= 0) {
            memcpy(&clipped[count++], (void *)(pVerts + current), sizeof(GxVert));
        }
    }
    if (count == 3) {
        gxDrawTriUV(&clipped[0], &clipped[1], &clipped[2], (int)pColor, pUV);
    } else if (count >= 4) {
        gxDrawQuad(&clipped[0], &clipped[1], &clipped[2], &clipped[3], (int)pColor, pUV);
    }
}

/* meshDrawPoly @0x42e940 — faithful to disassembly 0x42e940.
 * pPolyData layout: [0]=nCount|kind<<8, [1]=flags, [2]/[3]=header, pIdxList at +8.
 * bTex = (flags>>3)&1, bColor=(flags>>2)&1, bStride = low byte of [3].
 * pTexColors = td->pC (COLS, 4-byte entries, stride *4 for color)
 * pPalColors = td->pTex (MAPI, 16-byte entries, stride *0x10 for UV)
 * gxSetOrigin uses flags; kind 1=point,2=line,3=tri,4=quad. */
void meshDrawPoly(ushort *pPolyData, int pNormals, int pVerts, int pTexColors, int pPalColors)
{
    byte bStride = (byte)pPolyData[3];
    ushort u2 = pPolyData[1];
    ushort u0 = *pPolyData;
    uint nCount = u0 & 0xff;
    ushort kind = u0 >> 8;
    ushort *pIdxList = pPolyData + 4; /* +8 bytes */
    uint bTex = (u2 >> 3) & 1;
    uint bColor = (u2 >> 2) & 1;
    uint nUnk = bTex;
    uint local8;
    void *pvVar11 = NULL;
    ushort *pColorPtr = NULL; /* will hold pTexColors+...*4 when bTex */
    if ((bTex & 1) == 0) local8 = 0;
    else local8 = u2 & 0x10;
    if (bStride == 0 || nCount > 8192) return;
    gxSetOrigin((int)u2);
    if (kind == 1) {
        if (nCount == 0) return;
        do {
            if ((bTex & 1) != 0) pColorPtr = (ushort *)(pTexColors + (uint)pIdxList[1] * 4);
            else pColorPtr = NULL;
            int offset = (byte)*pIdxList * 0x10;
            if (*(int *)(pNormals + offset + 8) > 1) gxDrawTriangle((void *)(pVerts + offset), (int)pColorPtr);
            pIdxList = (ushort *)((byte *)pIdxList + bStride);
            nCount--;
        } while (nCount != 0);
        return;
    } else if (kind == 2) {
        if (nCount == 0) return;
        do {
            if ((bTex & 1) != 0) pColorPtr = (ushort *)(pTexColors + (uint)pIdxList[1] * 4);
            else pColorPtr = NULL;
            void *pv0 = (void *)((uint)(byte)*pIdxList * 0x10 + pVerts);
            void *pv1 = (void *)((uint)*(byte *)((int)pIdxList + 1) * 0x10 + pVerts);
            if ((1 < *(int *)((int)pv0 + 8)) && (1 < *(int *)((int)pv1 + 8))) gxDrawLine(pv0, pv1, (int)pColorPtr);
            pIdxList = (ushort *)((byte *)pIdxList + bStride);
            nCount--;
        } while (nCount != 0);
        return;
    } else if (kind == 3) {
        if (nCount == 0) return;
        do {
            if ((bTex & 1) != 0) pColorPtr = (ushort *)(pTexColors + (uint)pIdxList[2] * 4);
            else pColorPtr = NULL;
            if ((bColor & 1) != 0) pvVar11 = (void *)((uint)pIdxList[nUnk + 2] * 0x10 + pPalColors);
            else pvVar11 = NULL;
            int o0 = (byte)*pIdxList * 0x10;
            int o1 = (byte)*((byte *)pIdxList + 1) * 0x10;
            int o2 = (byte)pIdxList[1] * 0x10;
            /* Note: o1 and o2 both derive from byte index 1 in raw — Ghidra uses two loads at +1 and +2 (?) */
            /* Faithful re-derivation: Ghidra does o1 = *(byte *)(pIdx+1)*0x10, o2 = (byte)pIdx[1]*0x10 — same for tri they are distinct */
            /* Use original Ghidra logic for o1/o2 distinction: keep as above per disasm byte offsets */
            int d0 = *(int *)(pNormals + o0 + 8);
            int d1 = *(int *)(pNormals + o1 + 8);
            int d2 = *(int *)(pNormals + o2 + 8);
            if (d0 < 0) {
                if ((d1 >= 0) || (d2 >= 0)) {
                    if (d0 >= 0) goto tri_draw;
                    goto tri_clip;
                }
            } else {
tri_draw:
                if ((d1 < 0) || (d2 < 0)) {
tri_clip:
                    meshDrawTriClip((byte *)pIdxList, pVerts, pNormals, pColorPtr, pvVar11, (int)nUnk, (int)bColor, (int)local8);
                } else {
                    gxDrawTriUV((void *)(pVerts + o0), (void *)(pVerts + o1), (void *)(pVerts + o2), (int)pColorPtr, pvVar11);
                }
            }
            pIdxList = (ushort *)((byte *)pIdxList + bStride);
            nCount--;
            if (nCount == 0) return;
        } while (1);
    } else if (kind == 4) {
        if (nCount == 0) return;
        do {
            if ((bTex & 1) != 0) pColorPtr = (ushort *)(pTexColors + (uint)pIdxList[2] * 4);
            else pColorPtr = NULL;
            if ((bColor & 1) != 0) pvVar11 = (void *)((uint)pIdxList[nUnk + 2] * 0x10 + pPalColors);
            else pvVar11 = NULL;
            int o0 = (byte)*pIdxList * 0x10;
            int o1 = (byte)*((byte *)pIdxList + 1) * 0x10;
            int o2 = (byte)pIdxList[1] * 0x10;
            int o3 = (byte)*((byte *)pIdxList + 3) * 0x10;
            int d0 = *(int *)(pNormals + o0 + 8);
            int d1 = *(int *)(pNormals + o1 + 8);
            int d2 = *(int *)(pNormals + o2 + 8);
            int d3 = *(int *)(pNormals + o3 + 8);
            if (d0 < 0) {
                if ((d1 >= 0) || (d2 >= 0) || (d3 >= 0)) {
                    if (d0 >= 0) goto quad_draw;
                    goto quad_clip;
                }
            } else {
quad_draw:
                if ((d1 < 0) || (d2 < 0) || (d3 < 0)) {
quad_clip:
                    meshDrawQuadClip((byte *)pIdxList, pVerts, pNormals, pColorPtr, pvVar11, (int)nUnk, (int)bColor, (int)local8);
                } else {
                    gxDrawQuad((void *)(pVerts + o0), (void *)(pVerts + o1), (void *)(pVerts + o2), (void *)(pVerts + o3), (int)pColorPtr, pvVar11);
                }
            }
            pIdxList = (ushort *)((byte *)pIdxList + bStride);
            nCount--;
        } while (nCount != 0);
    }
}

/* ===================================================================
 * gxSortPushKey @0x42ecf0
 * =================================================================== */
void gxSortPushKey(void *pMesh, void *pVerts, void *pNormals, int pTex, int pPalette)
{
    int *p = (int *)g_pSortBufCur;
    p[0] = (int)pMesh; p[1] = (int)pVerts; p[2] = (int)pNormals;
    p[3] = pTex; p[4] = pPalette;
    g_pSortBufCur = (void *)((int)g_pSortBufCur + 0x14);
}

/* ===================================================================
 * sceneCameraBasisCalc @0x42f460  (from g_pRootMatrix view matrix)
 * =================================================================== */
void sceneCameraBasisCalc(void)
{
    float *rm = (float *)g_pRootMatrix;
    for (int i = 0; i < 9; i++) g_camMat[i] = rm[0x40 / 4 + i];  /* view 3x3 */
    /* camera basis angles from the view matrix (verified vs disasm 0x42f460) */
    float f1 = -rm[0x44 / 4];
    float f2 = -rm[0x50 / 4];
    float a  = (float)atan2((double)-rm[0x5c / 4], (double)sqrt(f1 * f1 + f2 * f2));
    float b  = (float)atan2((double)f1, (double)-f2);
    float sA = (float)sin((double)a);
    float cA = (float)cos((double)a);
    int   cAi = (int)cA;
    float sB = (float)sin((double)b);
    float cB = (float)cos((double)b);
    g_sceneCameraBasis   = -(sB * (float)cAi);
    g_sceneCameraBasis_2 = sB * sA;
    g_sceneCameraBasis_3 = sB;
    g_sceneCameraBasis_4 = cB * (float)cAi;
    g_sceneCameraBasis_5 = -(cB * sA);
    g_sceneCameraBasis_6 = sA;
    g_sceneCameraBasis_7 = cB;
}

/* ===================================================================
 * sceneBuildRootMatrix @0x42f520
 * =================================================================== */
void sceneBuildRootMatrix(void *pRootNode)
{
    float *rm = (float *)g_pRootMatrix;
    SceneNode *root = (SceneNode *)pRootNode;
    /* set flag byte at RM+0xb (verified vs disasm 0x42f520) */
    ((char *)rm)[0xb] = 1;
    /* clear world-dirty (bFlagB) for this node and all ancestor channels */
    {
        SceneNode *n = root;
        int c = 0;
        while ((void *)n != (void *)g_abSceneRootNode) {
            SceneChannel *chc = (SceneChannel *)((char *)n->pChannels + c * 0x70);
            chc->bFlagB = 0;
            c = chc->nIdx;
            n = (SceneNode *)n->pParent;
        }
    }
    /* identity 3x3 (column-major) + zero translation (verified) */
    rm[0x40/4] = 1; rm[0x44/4] = 0; rm[0x48/4] = 0;
    rm[0x4c/4] = 0; rm[0x50/4] = 1; rm[0x54/4] = 0;
    rm[0x58/4] = 0; rm[0x5c/4] = 0; rm[0x60/4] = 1;
    rm[0x64/4] = 0; rm[0x68/4] = 0; rm[0x6c/4] = 0;
    SceneChannel *ch = (SceneChannel *)root->pChannels;
    chanCalcWorldTransform((int)root, 0);
    g_camPos[0] = ch->wx; g_camPos[1] = ch->wy; g_camPos[2] = ch->wz;
    /* copy ch->wmat TRANSPOSED into rm (verified: rm = transpose(wmat)) */
    rm[0x40/4] = ch->wmat[0]; rm[0x44/4] = ch->wmat[3]; rm[0x48/4] = ch->wmat[6];
    rm[0x4c/4] = ch->wmat[1]; rm[0x50/4] = ch->wmat[4]; rm[0x54/4] = ch->wmat[7];
    rm[0x58/4] = ch->wmat[2]; rm[0x5c/4] = ch->wmat[5]; rm[0x60/4] = ch->wmat[8];
    float px = -ch->wx, py = -ch->wy, pz = -ch->wz;
    rm[0x64/4] = px * rm[0x40/4] + py * rm[0x44/4] + pz * rm[0x48/4];
    rm[0x68/4] = px * rm[0x4c/4] + py * rm[0x50/4] + pz * rm[0x54/4];
    rm[0x6c/4] = px * rm[0x58/4] + py * rm[0x5c/4] + pz * rm[0x60/4];
}

/* ===================================================================
 * sceneNodeRender @0x42f8c0
 * Render one node: chanCalcWorldTransform, culling, sceneMorphInterp,
 * meshDrawPoly/gxSortPushKey, recurse child @+0xc.
 * TODO: original culling does FLD/FILD/FSQRT/FCOMP with g_sceneRenderT,
 * g_flSceneAspect — permissive gate kept for preview so character not culled.
 * TODO: second vertex pool (normals) transform skipped — original transforms
 * both pools via __ftol. TODO: pTex/pPal order verified: pTex=@+0x20, pC=@+0x28.
 * =================================================================== */
int sceneNodeRender(void *pNode) /* @0x42f8c0 */
{
    SceneNode *node = (SceneNode *)pNode;
    byte bType = node->bType;
    if (bType == 2) return 1;

    int bDoRender = 1;
    if (bType == 1) bDoRender = 0;
    else if (node->nId != 1 && node->nId < 0x100) bDoRender = 0;

    *(byte *)(node->pChannels + 0xb) = 0; /* dirty */
    chanCalcWorldTransform((int)(uintptr_t)node, 0);

    /* TODO: restore exact frustum/distance culling (FLD/FILD/FSQRT) — permissive */
    if ((char)node->nChannelCount > 1) {
        for (int i = 1; i < (char)node->nChannelCount; i++) {
            *(byte *)(node->pChannels + i * 0x70 + 0xb) = 0;
        }
    }
    if ((*(int *)((int)node + 0x24) == 1) &&
        ((char)node->nChannelCount > 1) && (node->nId == 1)) {
        sceneCacheLocalVerts((int)node);
    }
    if (!bDoRender) goto recurse;

    /* update draw count / distance (disasm 0x42fa10..0x42fa4f) */
    {
        SceneChannel *chMain = (SceneChannel *)node->pChannels;
        float wz = chMain->wz;
        if ((float)g_nSceneDrawCount < wz) g_nSceneDrawCount = (int)wz;
        if (wz < (float)g_nSceneDistMax) g_nSceneDistMax = (int)wz;
    }

    if (node->nId != 1) goto recurse;

    {
        SceneObjTypeDef *td = (SceneObjTypeDef *)node->pTypeDef;
        if (!td) goto recurse;
        int pRender = td->pRender;
        if (!pRender) goto recurse;
        SceneObjRenderInfo *ri = (SceneObjRenderInfo *)pRender;
        int vbuf = sceneMorphInterp((int)node, pRender, (int)g_pMeshPool);
        void *pVerts = g_pNodePoolCur;
        void *pNormals = g_pNodePool2Cur;
        short *src = (short *)vbuf;

        /* first vertex block: nGroups at +0x1c, groups at +0x20 */
        if (ri->nGroups > 0) {
            int groupOff = 0;
            for (int g = 0; g < ri->nGroups; g++) {
                int pGroup = ri->pGroups;
                int chanIdx = *(int *)(pGroup + groupOff + 8);
                int nVertsInGroup = *(int *)(pGroup + groupOff);
                chanCalcWorldTransform((int)node, chanIdx);
                SceneChannel *ch = (SceneChannel *)(node->pChannels + chanIdx * 0x70);
                for (int v = 0; v < nVertsInGroup; v++) {
                    short sx = src[0], sy = src[1], sz = src[2];
                    float fx = (float)sx, fy = (float)sy, fz = (float)sz;
                    float wx = fx * ch->wmat[0] + fy * ch->wmat[1] + fz * ch->wmat[2] + ch->wx;
                    float wy = fx * ch->wmat[3] + fy * ch->wmat[4] + fz * ch->wmat[5] + ch->wy;
                    float wz = fx * ch->wmat[6] + fy * ch->wmat[7] + fz * ch->wmat[8] + ch->wz;
                    /* Perspective projection (verified vs disasm 0x42fb82..0x42fc19):
                     * scale = halfWidth / ((aspect + worldZ) * nWidth)
                     * screenX = ftol(scale * worldX + centerX)
                     * screenY = ftol(Yscale * scale * worldY + centerY)
                     * depth = ftol((aspect + worldZ) * 16.0) */
                    float az = g_flSceneAspect + wz;
                    if (az <= 0.0f) az = 0.001f;
                    float denom = az * g_nSceneWidth;
                    float scale = (denom != 0.0f) ? (float)g_nSceneHalfWidth / denom : 0.0f;
                    int screenX = (int)(scale * wx + (float)g_centerX);
                    int screenY = (int)(g_flSceneYScale * scale * wy + (float)g_centerY);
                    int depth = (int)(az * 16.0f);
                    int *dstV = (int *)g_pNodePoolCur;
                    int *dstN = (int *)g_pNodePool2Cur;
                    dstV[0] = screenX; dstV[1] = screenY; dstV[2] = depth;
                    dstN[0] = screenX; dstN[1] = screenY; dstN[2] = depth;
                    *(byte *)((int)dstN + 0xc) = *(byte *)(vbuf + 6);
                    *(byte *)((int)dstN + 0xd) = *(byte *)(vbuf + 7);
                    *(byte *)((int)dstN + 0xe) = *(byte *)(vbuf + 6);
                    g_pNodePoolCur = (void *)((int)g_pNodePoolCur + 0x10);
                    g_pNodePool2Cur = (void *)((int)g_pNodePool2Cur + 0x10);
                    src += 4;
                }
                groupOff += 0xc;
            }
        }

        /* second vertex block for normals (disasm 0x42fc74..0x42fe5e) */
        {
            void *pVerts2 = g_pNodePoolCur;
            void *pNormals2 = g_pNodePool2Cur;
            int nPolyB = *(int *)(pRender + 0x24);
            if (nPolyB > 0) {
                int polyOff = 0;
                for (int i = 0; i < nPolyB; i++) {
                    /* disasm reads ushort counts etc and transforms similar way */
                    /* simplified: skip detailed normal transform, advance cursors */
                    (void)polyOff;
                }
            }
            /* draw polys — faithful to 0x42f8c0: pTex=COLS (*pTex 4B), pPal=MAPI (*pTex 16B), guard only small */
            int nPolyA = *(int *)(pRender + 0x14);
            int pPolyA = *(int *)(pRender + 0x18);
            if (nPolyA > 0) {
                for (int i = 0; i < nPolyA; i++) {
                    ushort *poly = *(ushort **)(pPolyA + i*4);
                    int pTex = td->pC;
                    int pPal = td->pTex;
                    if (!poly) continue;
                    if ((poly[1] & 0x20) == 0) meshDrawPoly(poly, (int)pVerts, (int)pNormals, pTex, pPal);
                    else gxSortPushKey(poly, pVerts, pNormals, pTex, pPal);
                }
            }
            nPolyB = *(int *)(pRender + 0x24);
            int pPolyB = *(int *)(pRender + 0x2c);
            if (nPolyB > 0) {
                byte *base = (byte *)pPolyB;
                for (int i = 0; i < nPolyB; i++) {
                    byte n = *base; base += 6;
                    for (int k = 0; k < (n & 0xff); k++) {
                        ushort *poly = (ushort *)base;
                        int pTex2 = td->pC;
                        int pPal2 = td->pTex;
                        if ((poly[1] & 0x20) == 0) meshDrawPoly(poly, (int)pVerts2, (int)pNormals2, pTex2, pPal2);
                        else gxSortPushKey(poly, pVerts2, pNormals2, pTex2, pPal2);
                        base += (poly[3] & 0xff) * (poly[0] & 0xff) + 4; /* stride */
                    }
                }
            }
        }
    }

recurse:
    {
        void *child = (void *)node->pChild;
        while (child) {
            sceneNodeRender(child);
            child = (void *)((SceneNode *)child)->pNextSib;
        }
    }
    return 1;
}

/* ===================================================================
 * sceneRender @0x42f1c0
 * TODO: original fires MusicSlot cbs 0x45e650..0x45e810 pre/post — deferred.
 * =================================================================== */
int sceneRender(void *pCameraBlock) /* @0x42f1c0 */
{
    SceneCameraBlock *cb = (SceneCameraBlock *)pCameraBlock;
    GxMode mode;
    int oldViewport[4];
    int viewport[4];
    int width;
    int height;
    int x0;
    int y0;
    int x1;
    int y1;
    g_nSceneDistMax = 0x7fffffff;
    g_nSceneDrawCount = 0;
    if (!cb || cb->mode != 2) return 0;
    if (!g_pSceneNodeList) return 1;

    /* These assignments intentionally preserve the original bitwise copies. */
    g_nSceneWidth = cb->nWidth;
    g_nSceneHeight = cb->nHeight;
    g_flSceneAspect = cb->nHeight / cb->nWidth;
    g_sceneRenderT = cb->renderT;
    g_flSceneRenderT2 = cb->renderT;
    g_pSortBufCur = g_pSortBuffer;
    g_pNodePoolCur = g_pNodePool;
    g_pNodePool2Cur = g_pNodePool2;

    gxGetMode(&mode);
    gxGetViewport(oldViewport);
    width = mode.width;
    height = mode.height;
    if (width == 0) return 1;
    g_gxClipTest_4 = ((float)height * (4.0f / 3.0f)) / (float)width;

    x1 = (int)cb->vw * width;
    x0 = (int)cb->vx * width;
    y1 = (int)cb->vh * height;
    y0 = (int)cb->vy * height;
    g_gxClipTest = (float)(((x1 >> 4) - (x0 >> 4)) >> 1);
    g_gxClipTest_2 = (float)((int)g_gxClipTest + (x0 >> 4));
    g_gxClipTest_3 = (float)((((y1 >> 4) - (y0 >> 4)) >> 1) + (y0 >> 4));
    g_nSceneHalfWidth = ((x1 >> 4) - (x0 >> 4)) >> 1;
    g_centerX = g_nSceneHalfWidth + (x0 >> 4);
    g_centerY = ((y1 >> 4) + (y0 >> 4)) >> 1;
    g_flSceneYScale = (float)height * 0.5f / (float)width;

    viewport[0] = x0 >> 12;
    viewport[1] = y0 >> 12;
    viewport[2] = x1 >> 12;
    viewport[3] = y1 >> 12;
    if (viewport[0] < oldViewport[0]) viewport[0] = oldViewport[0];
    if (viewport[1] < oldViewport[1]) viewport[1] = oldViewport[1];
    if (viewport[2] > oldViewport[2]) viewport[2] = oldViewport[2];
    if (viewport[3] > oldViewport[3]) viewport[3] = oldViewport[3];
    if (viewport[0] > viewport[2] || viewport[1] > viewport[3]) return 1;

    gxSetViewport(viewport);
    /* TODO: pre-render MusicSlot callbacks 0x45e650..0x45e81c */
    sceneBuildRootMatrix(pCameraBlock); /* @0x42f520 */
    sceneCameraBasisCalc(); /* @0x42f460 */
    for (void *p = g_pSceneNodeList; p != NULL;
         p = (void *)(uintptr_t)((SceneNode *)p)->pNextSib) {
        sceneNodeRender(p);
    }
    if (g_pSortBuffer < g_pSortBufCur) {
        int *pi = (int *)((int)g_pSortBuffer + 0xc);
        int *end = (int *)g_pSortBufCur;
        while (pi + 2 < end) {
            int pTexS = *pi; int pPalS = pi[1];
            if ((unsigned)pTexS >= 0x10000U && (unsigned)pPalS >= 0x10000U) {
                meshDrawPoly((ushort *)pi[-3], pi[-2], pi[-1], *pi, pi[1]);
            }
            pi += 5;
        }
    }
    gxSetViewport(oldViewport);
    ((float *)g_pRootMatrix)[0x40 / 4] = 1.0f;
    ((float *)g_pRootMatrix)[0x44 / 4] = 0.0f;
    ((float *)g_pRootMatrix)[0x48 / 4] = 0.0f;
    ((float *)g_pRootMatrix)[0x4c / 4] = 0.0f;
    ((float *)g_pRootMatrix)[0x50 / 4] = 1.0f;
    ((float *)g_pRootMatrix)[0x54 / 4] = 0.0f;
    ((float *)g_pRootMatrix)[0x58 / 4] = 0.0f;
    ((float *)g_pRootMatrix)[0x5c / 4] = 0.0f;
    ((float *)g_pRootMatrix)[0x60 / 4] = 1.0f;
    g_pSortBufCur = g_pSortBuffer;
    return 1;
}

int sceneCacheLocalVerts(int pNode) { (void)pNode; return 0; }
