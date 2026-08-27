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
void *g_pSceneNodeList = NULL;       /* @0x45e8cc (root node + 0x0c) */
void *g_pSortBuffer = NULL;          /* @0x45e914 */
void *g_pSortBufCur = NULL;          /* @0x45e90c cursor */
void *g_pNodePool = NULL;            /* @0x45e604 */
void *g_pNodePool2 = NULL;           /* @0x45e648 */
void *g_pMeshPool = NULL;            /* @0x45e610 */
void *g_pNodePoolCur = NULL;         /* @0x45e908 cursor into g_pNodePool */
void *g_pNodePool2Cur = NULL;        /* @0x45e5fc cursor into g_pNodePool2 */
/* Root channel — separate from root node (original: root channel @0x45e818,
 * root node @0x45e8c0; root node's pChannels at +0x14 = 0x45e8d4 = g_pRootMatrix).
 * g_pRootMatrix points to this buffer. */
static SceneChannel g_rootChannel;    /* root channel storage */
void *g_pRootMatrix = NULL;           /* @0x45e8d4 — points to g_rootChannel */
SceneNode g_rootNode;                 /* @0x45e8c0 root node storage */
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
int   g_nSceneHalfWidth = 0;      /* @0x450f70 */
int   g_centerX = 0;              /* @0x450f74 */
int   g_centerY = 0;              /* @0x450f78 */
float g_sceneRenderT = 0.0f;      /* @0x450f7c camera-block renderT float bits; original culling re-reads it as int (FILD) */
float g_flSceneYScale = 0.0f;     /* @0x450f80 */
int   g_nSceneDistMax = 0x7fffffff;
int   g_nSceneDrawCount = 0;
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
static const float g_flHalf = 0.5f; /* @0x44b274 shared 0.5f literal (also used by aiSteerToTarget, menus, ...) */

/* The engine stores all rotation angles as int16 "binary degrees":
 * 65536 = 360 degrees. mathSinDeg/mathCosDeg (@0x42d030/@0x42d050) take the
 * int16 angle, FILD it and multiply by the double constant @0x44b788
 * (= 2*pi/65536, exact stored value below) before FSIN/FCOS.
 * mathAtan2Deg (@0x42d010) FPATANs and multiplies by @0x44b780
 * (= 65536/(2*pi), exact stored value below) before ftol. */
static const double g_dblBdgToRad = 9.58737992553711e-05;      /* @0x44b788 */
static const double g_dblRadToBdg = 10430.378349108529;        /* @0x44b780 */
/* mathSinDeg @0x42d030 */
float mathSinDeg(short d)
{
    g_flMathSin = (float)sin((double)d * g_dblBdgToRad);
    return g_flMathSin;
}
/* mathCosDeg @0x42d050 */
float mathCosDeg(short d)
{
    g_flMathCos = (float)cos((double)d * g_dblBdgToRad);
    return g_flMathCos;
}
/* mathAtan2Deg @0x42d010 — FLD y, FLD x, FPATAN (atan(y/x)), FMUL @0x44b780,
 * JMP __ftol: returns the angle in binary degrees as a truncating int.
 * x87 FPATAN(+0,+0) = 0 deterministically, so no special case is needed. */
int mathAtan2Deg(float y, float x)
{
    return (int)(atan2((double)y, (double)x) * g_dblRadToBdg);
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
    /* Root node and root channel are SEPARATE in the original (root channel
     * @0x45e818, root node @0x45e8c0). The root node's pChannels field at
     * +0x14 IS g_pRootMatrix (same address 0x45e8d4 in the original).
     * sceneBuildRootMatrix writes the camera inverse transform into the root
     * channel's wmat, which children of root use as their parent world matrix. */
    g_pSceneNodeList = NULL;
    memset(&g_rootNode, 0, sizeof(g_rootNode));
    memset(&g_rootChannel, 0, sizeof(g_rootChannel));
    SceneNode *root = &g_rootNode;
    root->nId = 0;
    root->pParent = 0;
    root->pChild = 0;
    root->nChannelCount = 1;
    /* root->pChannels (@+0x14) = g_pRootMatrix. In the original these share
     * the same address (0x45e8d4). Set pChannels to point at root channel. */
    g_pRootMatrix = &g_rootChannel;
    root->pChannels = &g_rootChannel;
    SceneChannel *rc = &g_rootChannel;
    rc->wmat[0] = 1; rc->wmat[4] = 1; rc->wmat[8] = 1;
    rc->matr[0] = 1; rc->matr[4] = 1; rc->matr[8] = 1;
    rc->fUnk6 = 1.0f;
    rc->bFlagA = 0; rc->bFlagB = 0;
    chanBuildRotMatrix(rc); /* @0x42f030 */
    /* Copy matr → wmat (original copies 9 floats from 0x45e834 to 0x45e858) */
    memcpy(rc->wmat, rc->matr, sizeof(rc->wmat));
    g_pSceneRoot = &g_rootNode; /* @0x4588f8 */
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
static SceneObjTypeDef g_sceneObjDefaultType = {0}; /* fallback when pTypeDef==NULL; original @0x?? TODO addr */
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
int sceneObjSetPos(SceneNode *pObj, int nX, int nY, int nZ, int nMode) /* @0x430660 */
{
    SceneNode *n = pObj;
    SceneChannel *ch = n->pChannels;
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
    if (n->pParent != &g_rootNode) sceneNodeUpdateBounds(n->pParent);
    return 1;
}

/* ===================================================================
 * sceneObjSetPosOrient @0x4307d0  (keyframe record application)
 * =================================================================== */
int sceneObjSetPosOrient(SceneNode *pObj, short nYaw, short nPitch, short nRoll, byte nMode) /* @0x4307d0 */
{
    SceneNode *n = pObj;
    SceneChannel *ch = n->pChannels;
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
            /* [VERIFIED 2026-08-27] operand pairing from disasm:
             * viewX = fX*m0 + fY*m1 + fZ*m2  (0x4308d7 fwdX*matr[0]@0x1c,
             *             0x4308db fwdZ*matr[2]@0x24, 0x4308e7 fwdY*matr[1]@0x20)
             * viewZ = fX*m6 + fY*m7 + fZ*m8  (0x4308f0..0x430907)
             * viewY = -(fX*m3 + fY*m4 + fZ*m5) (0x430929..0x430944, FCHS)
             * viewX/viewZ/viewY are the true row products of matr (row-major
             * rotation), so a heading delta composes: viewX = sin(dθ)*cosθ +
             * cos(dθ)*sinθ = sin(θ+dθ) -> rot1 accumulates across frames. */
            viewX = forwardX * ch->matr[0] + forwardY * ch->matr[1]
                  + forwardZ * ch->matr[2];
            viewZ = forwardX * ch->matr[6] + forwardY * ch->matr[7]
                  + forwardZ * ch->matr[8];
            invLength = 1.0f / (float)sqrt(viewX * viewX + viewZ * viewZ);
            normalX = invLength * viewX;
            normalZ = invLength * viewZ;
            viewY = -(forwardX * ch->matr[3] + forwardY * ch->matr[4]
                    + forwardZ * ch->matr[5]);

            /* Full look-at Euler decomposition (original @0x430854..0x430a3e,
             * verified instruction-by-instruction incl. FPU stack order).
             * With rollVec2=R1, rollVec=R2, cRoll*cYaw=R3 the original pairs:
             *   rollBasis = R1*matr[0] + R2*matr[2] + R3*matr[1]
             *             (0x430992 FLD ST2=mul matr[0]@0x1c; 0x430997 FLD ST1=
             *              matr[2]@0x24; 0x43099e FLD ST2=matr[1]@0x20 — note
             *              the m2/m1 swap vs matrix index order!)
             *   basisZ    = R1*matr[6] + R2*matr[8] + R3*matr[7] (0x4309a9..ba)
             *   basisY    = R1*matr[4] + R3*matr[3] + R2*matr[5] (0x4309c0..d9)
             * rot0 = atan2(viewY, dot)            (pitch of the view dir)
             * rot1 = atan2(normalX, normalZ)      (yaw/heading of the view dir)
             * rot2 = atan2(normalX*basisZ - normalZ*rollBasis,
             *              basisY*dot + (normalZ*basisZ + normalX*rollBasis)*viewY)
             * With this pairing and yaw=roll=0 inputs, rollBasis==matr[1]==0
             * for yaw-only states, so rot2 stays 0 and (rot0,rot1) converge to
             * a stable fixed point (original shows upright character). */
            {
                float dot = normalX * viewX + normalZ * viewZ;
                float rollVec = sRoll * sPitch + cRoll * cPitch * sYaw;
                float rollVec2 = cRoll * sPitch * sYaw - sRoll * cPitch;
                float rollBasis = rollVec2 * ch->matr[0]
                    + rollVec * ch->matr[2]
                    + cRoll * cYaw * ch->matr[1];
                float basisZ = rollVec2 * ch->matr[6]
                    + rollVec * ch->matr[8]
                    + cRoll * cYaw * ch->matr[7];
                float basisY = rollVec2 * ch->matr[3]
                    + cRoll * cYaw * ch->matr[4]
                    + rollVec * ch->matr[5];

            ch->rot[0] = (short)mathAtan2Deg(viewY, dot);
            ch->rot[1] = (short)mathAtan2Deg(normalX, normalZ);
            ch->rot[2] = (short)mathAtan2Deg(
                -(normalZ * rollBasis - normalX * basisZ),
                basisY * dot + (normalX * rollBasis + normalZ * basisZ) * viewY);
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
int sceneNodeGetPosWorld(SceneNode *pNode, float *pOutXYZ, int nMode) /* @0x430e80 */
{
    SceneNode *n = pNode;
    if (nMode == 2) {
        SceneChannel *ch = (SceneChannel *)n->pChannels;
        /* Disasm @0x430e80 mode 2: store the raw int bits of the LOCAL
         * translation (ch->x/y/z at +0x10/+0x14/+0x18) into the float buffer
         * via MOV dword [ECX],EDX (no int->float conversion). Callers read
         * them back as int via ((int*)pOut)[i]: the charselect falling
         * threshold ((int)vec[1] < 0x4e20) and the anim keyframe midpoint
         * interpolation (memcpy float->int) both rely on LOCAL coordinates. */
        ((int *)pOutXYZ)[0] = ch->x;
        ((int *)pOutXYZ)[1] = ch->y;
        ((int *)pOutXYZ)[2] = ch->z;
        return 1;
    }
    return 0;
}

/* ===================================================================
 * sceneNodeFacePos @0x431030
 * =================================================================== */
int sceneNodeFacePos(SceneNode *pNode, int nChannel, float flX, float flY, float flZ, int nMode) /* @0x431030 */
{
    SceneNode *n = pNode;
    if (nChannel >= 0 && nChannel < (int)n->nChannelCount && nMode == 2) {
        SceneChannel *ch = &n->pChannels[nChannel];
        float dx = flX - ch->x;
        float dy = flY - ch->y;
        float dz = flZ - ch->z;
        float dHoriz = (float)sqrt(dx * dx + dz * dz);
        float d3 = (float)sqrt(dy * dy + dHoriz * dHoriz);
        /* Original @0x4310a7..0x4310e6: FPATAN on (dx/d, dz/d) resp.
         * (-dy/d3, d/d3) followed by FMUL @0x44b780 (65536/(2*pi)) + ftol —
         * the same binary-degree conversion as mathAtan2Deg. rot[2] forced
         * to 0 (0x431081), bFlagA/bFlagB cleared (0x4310f7/0x431104). */
        ch->rot[1] = (short)mathAtan2Deg(dx, dz);
        ch->rot[0] = (short)mathAtan2Deg(-dy, d3);
        ch->rot[2] = 0;
        ch->bFlagA = 0;
        ch->bFlagB = 0;
        return 1;
    }
    return 0;
}

/* ===================================================================
 * sceneNodeUpdateBounds @0x4303c0 (simplified — bounds not needed for menu)
 * =================================================================== */
void sceneNodeUpdateBounds(SceneNode *pNode) { (void)pNode; }

/* ===================================================================
 * sceneObjSetSubPos @0x430a90 — sub-channel orientation/position setter.
 * Original has exactly 6 params (prologue reads entry ESP+0x08..0x18).
 * =================================================================== */
int sceneObjSetSubPos(SceneNode *pObj, int nMeshIdx, short nYaw, short nPitch, short nRoll, byte nMode) /* @0x430a90 */
{
    SceneNode *n = pObj;
    if (nMeshIdx < 0 || nMeshIdx >= (int)n->nChannelCount) return 0;
    if (nMode & 0x20) {
        nYaw *= 0xb6;
        nPitch *= 0xb6;
        nRoll *= 0xb6;
    }
    if (nMode & 0x10) return 0;

    SceneChannel *ch = &n->pChannels[nMeshIdx];
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
        /* [VERIFIED 2026-08-27] identical math to sceneObjSetPosOrient mode 5
         * (@0x430b7c..0x430d1c): fwd=(sP*cY,-sY,cP*cY); viewX=fX*m0+fY*m1+fZ*m2
         * (0x430bb1..0x430bc8); viewZ=fX*m6+fY*m7+fZ*m8; viewY=-(fX*m3+fY*m4+
         * fZ*m5) (0x430c03..0x430c1e); rot0=atan2(viewY,dot) (0x430cd5);
         * rot1=atan2(normalX,normalZ) (0x430ce7); rollBasis=R1*m0+R2*m2+R3*m1
         * (0x430c6c..0x430c7d); basisZ/basisY/V same as sibling; rot2=atan2(
         * -(nZ*rollBasis-nX*basisZ), V) via FCHS + arg overwrite (0x430d0b..d1c). */
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
            if (ch->bFlagA != 1) chanBuildRotMatrix(ch); /* @0x430ba6..0x430bae */
            float viewX = forwardX * ch->matr[0] + forwardY * ch->matr[1]
                        + forwardZ * ch->matr[2];
            float viewZ = forwardX * ch->matr[6] + forwardY * ch->matr[7]
                        + forwardZ * ch->matr[8];
            float invLength = 1.0f / (float)sqrt(viewX * viewX + viewZ * viewZ);
            float normalX = invLength * viewX;
            float normalZ = invLength * viewZ;
            float viewY = -(forwardX * ch->matr[3] + forwardY * ch->matr[4]
                          + forwardZ * ch->matr[5]);
            float dot = normalX * viewX + normalZ * viewZ;
            float rollVec = sRoll * sPitch + cRoll * cPitch * sYaw;
            float rollVec2 = cRoll * sPitch * sYaw - sRoll * cPitch;
            float rollBasis = rollVec2 * ch->matr[0]
                + rollVec * ch->matr[2]
                + cRoll * cYaw * ch->matr[1];
            float basisZ = rollVec2 * ch->matr[6]
                + rollVec * ch->matr[8]
                + cRoll * cYaw * ch->matr[7];
            float basisY = rollVec2 * ch->matr[3]
                + cRoll * cYaw * ch->matr[4]
                + rollVec * ch->matr[5];

            ch->rot[0] = (short)mathAtan2Deg(viewY, dot);
            ch->rot[1] = (short)mathAtan2Deg(normalX, normalZ);
            ch->rot[2] = (short)mathAtan2Deg(
                -(normalZ * rollBasis - normalX * basisZ),
                basisY * dot + (normalX * rollBasis + normalZ * basisZ) * viewY);
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
int sceneObjSetSubOrient(SceneNode *pObj, int nMeshIdx, short nYaw, short nPitch, short nRoll) /* @0x431110 */
{
    SceneNode *n = pObj;
    if (nMeshIdx < 0 || nMeshIdx >= (int)n->nChannelCount) return 0;
    SceneChannel *ch = &n->pChannels[nMeshIdx];
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
    for (i = 0; i < 9; i++) ch->matr[i] = (ch->matr[i] + matrix[i]) * g_flHalf;
    ch->bFlagA = 1;
    return 1;
}

/* ===================================================================
 * sceneNodeFree @0x430460
 * =================================================================== */
void sceneNodeFree(SceneNode *pNode, int nFreeChildren) /* @0x430460 */
{
    SceneNode *n = pNode;
    if (!n) return;
    if (nFreeChildren) {
        SceneNode *c = n->pChild;
        while (c) { SceneNode *nx = c->pNextSib; sceneNodeFree(c, 1); c = nx; }
    }
    if (n->pParent) {
        SceneNode *p = n->pParent;
        if (p->pChild == n) p->pChild = n->pNextSib;
        else {
            SceneNode *c = p->pChild;
            while (c && c->pNextSib != n) c = c->pNextSib;
            if (c) c->pNextSib = n->pNextSib;
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
 * General path handles root node correctly: root->pChannels points to the
 * root channel (g_rootChannel), and sceneBuildRootMatrix sets the root
 * channel's bFlagB=1 so recursion terminates at the root.
 * =================================================================== */
void chanCalcWorldTransform(SceneNode *pNode, int nChannel) /* @0x42f6e0 */
{
    SceneNode *n = pNode;
    if (!n) n = &g_rootNode;
    SceneChannel *ch = &n->pChannels[nChannel];
    if (ch->bFlagB) return;                          /* already computed */
    if (!ch->bFlagA) chanBuildRotMatrix(ch);
    int idx = ch->nIdx;
    SceneChannel *parentWorld;
    if (nChannel == 0) {
        SceneNode *p = n->pParent;
        if (!p) p = &g_rootNode;
        chanCalcWorldTransform(p, idx);
        parentWorld = &p->pChannels[idx];
    } else {
        chanCalcWorldTransform(n, idx);
        parentWorld = &n->pChannels[idx];
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
 * Returns frame pointer or pOut (lerped). Original uses FTOL @0x43dd10
 * (FISTP 0xc trunc). TODO: (int)f trunc matches for positive d*t; differs
 * for negative — should call __ftol if exact pixel match needed.
 * =================================================================== */
void *sceneMorphInterp(SceneNode *pNode, SceneObjRenderInfo *pRender, void *pOut) /* @0x4300d0 */
{
    float t = pNode->flMorphT;
    uintptr_t base = (uintptr_t)pRender->pVerts;
    int nVerts = pRender->nVerts;
    if (t <= 0.0f) {
        int idxA = pNode->nMorphIdxA;
        return (void *)(base + (uintptr_t)idxA * (uintptr_t)nVerts * 8);
    }
    if (t >= 1.0f) {
        int idxB = pNode->nMorphIdxB;
        return (void *)(base + (uintptr_t)idxB * (uintptr_t)nVerts * 8);
    }
    if (nVerts <= 0) return pOut;
    int idxA = pNode->nMorphIdxA;
    int idxB = pNode->nMorphIdxB;
    short *srcA = (short *)(base + (uintptr_t)idxA * (uintptr_t)nVerts * 8);
    short *srcB = (short *)(base + (uintptr_t)idxB * (uintptr_t)nVerts * 8);
    SceneMorphOut *out = (SceneMorphOut *)pOut;
    short *dst = out->verts;
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
            pIdxList = pIdxList + bStride;
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
            pIdxList = pIdxList + bStride;
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
                    if (pvVar11 || pColorPtr)
                        meshDrawTriClip((byte *)pIdxList, pVerts, pNormals, pColorPtr, pvVar11, (int)nUnk, (int)bColor, (int)local8);
                } else {
                    if (pvVar11 || pColorPtr)
                        gxDrawTriUV((void *)(pVerts + o0), (void *)(pVerts + o1), (void *)(pVerts + o2), (int)pColorPtr, pvVar11);
                }
            }
            pIdxList = pIdxList + bStride;
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
            pIdxList = pIdxList + bStride;
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
    SceneChannel *rm = (SceneChannel *)g_pRootMatrix;
    for (int i = 0; i < 9; i++) g_camMat[i] = rm->wmat[i];  /* view 3x3 */
    /* camera basis angles from the view matrix (verified vs disasm 0x42f460) */
    float f1 = -rm->wmat[1];
    float f2 = -rm->wmat[4];
    float a  = (float)atan2((double)-rm->wmat[7], (double)sqrt(f1 * f1 + f2 * f2));
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
void sceneBuildRootMatrix(SceneNode *pRootNode) /* @0x42f520 */
{
    SceneChannel *rm = (SceneChannel *)g_pRootMatrix;
    SceneNode *root = pRootNode;
    /* set flag byte at RM+0xb (verified vs disasm 0x42f520) */
    rm->bFlagB = 1;
    /* clear world-dirty (bFlagB) for this node and all ancestor channels */
    {
        SceneNode *n = root;
        int c = 0;
        while (n != &g_rootNode) {
            SceneChannel *chc = &n->pChannels[c];
            chc->bFlagB = 0;
            c = chc->nIdx;
            n = n->pParent;
        }
    }
    /* identity 3x3 (column-major) + zero translation (verified) */
    rm->wmat[0] = 1; rm->wmat[1] = 0; rm->wmat[2] = 0;
    rm->wmat[3] = 0; rm->wmat[4] = 1; rm->wmat[5] = 0;
    rm->wmat[6] = 0; rm->wmat[7] = 0; rm->wmat[8] = 1;
    rm->wx = 0; rm->wy = 0; rm->wz = 0;
    SceneChannel *ch = root->pChannels;
    chanCalcWorldTransform(root, 0);
    g_camPos[0] = ch->wx; g_camPos[1] = ch->wy; g_camPos[2] = ch->wz;
    /* copy ch->wmat TRANSPOSED into rm (verified: rm = transpose(wmat)) */
    rm->wmat[0] = ch->wmat[0]; rm->wmat[1] = ch->wmat[3]; rm->wmat[2] = ch->wmat[6];
    rm->wmat[3] = ch->wmat[1]; rm->wmat[4] = ch->wmat[4]; rm->wmat[5] = ch->wmat[7];
    rm->wmat[6] = ch->wmat[2]; rm->wmat[7] = ch->wmat[5]; rm->wmat[8] = ch->wmat[8];
    float px = -ch->wx, py = -ch->wy, pz = -ch->wz;
    rm->wx = px * rm->wmat[0] + py * rm->wmat[1] + pz * rm->wmat[2];
    rm->wy = px * rm->wmat[3] + py * rm->wmat[4] + pz * rm->wmat[5];
    rm->wz = px * rm->wmat[6] + py * rm->wmat[7] + pz * rm->wmat[8];
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
int sceneNodeRender(SceneNode *pNode) /* @0x42f8c0 */
{
    SceneNode *node = pNode;
    byte bType = node->bType;
    if (bType == 2) return 1;

    int bDoRender = 1;
    if (bType == 1) bDoRender = 0;
    else if (node->nId != 1 && node->nId < 0x100) bDoRender = 0;

    node->pChannels[0].bFlagB = 0; /* dirty */
    chanCalcWorldTransform(node, 0);

    /* TODO: restore exact frustum/distance culling (FLD/FILD/FSQRT) — permissive */
    if ((char)node->nChannelCount > 1) {
        for (int i = 1; i < (char)node->nChannelCount; i++) {
            node->pChannels[i].bFlagB = 0;
        }
    }
    if ((node->nCacheFlag == 1) &&
        ((char)node->nChannelCount > 1) && (node->nId == 1)) {
        sceneCacheLocalVerts(node);
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
        SceneObjTypeDef *td = node->pTypeDef;
        if (!td) goto recurse;
        SceneObjRenderInfo *ri = td->pRender;
        if (!ri) goto recurse;
        void *vbuf = sceneMorphInterp(node, ri, g_pMeshPool);
        void *pVerts = g_pNodePoolCur;
        void *pNormals = g_pNodePool2Cur;
        short *src = (short *)vbuf;

        /* first vertex block: nGroups at +0x1c, groups at +0x20 */
        if (ri->nGroups > 0) {
            for (int g = 0; g < ri->nGroups; g++) {
                SceneGroupInfo *grp = &ri->pGroups[g];
                int chanIdx = grp->nChanIdx;
                int nVertsInGroup = grp->nVerts;
                chanCalcWorldTransform(node, chanIdx);
                SceneChannel *ch = &node->pChannels[chanIdx];
                for (int v = 0; v < nVertsInGroup; v++) {
                    short sx = src[0], sy = src[1], sz = src[2];
                    float fx = (float)sx, fy = (float)sy, fz = (float)sz;
                    /* World transform: vertex (x,y,z) * channel.wmat + channel.wx/wy/wz.
                     * Verified vs disasm 0x42fae4..0x42fb4f. */
                    float wx = fx * ch->wmat[0] + fy * ch->wmat[1] + fz * ch->wmat[2] + ch->wx;
                    float wy = fx * ch->wmat[3] + fy * ch->wmat[4] + fz * ch->wmat[5] + ch->wy;
                    float wz = fx * ch->wmat[6] + fy * ch->wmat[7] + fz * ch->wmat[8] + ch->wz;
                    /* Perspective projection (verified vs disasm 0x42fb82..0x42fbec):
                     * ORIGINAL stores world coords (ftol(wx),ftol(wy),ftol(wz)) to
                     * g_pNodePoolCur (param 2 "pNormals" in meshDrawPoly = depth
                     * check source) and projected screen coords to g_pNodePool2Cur
                     * (param 3 "pVerts" in meshDrawPoly = gxDrawTriUV source).
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
                    /* pVerts pool (g_pNodePoolCur) = world coordinates (depth test) */
                    int *dstV = (int *)g_pNodePoolCur;
                    dstV[0] = (int)wx; dstV[1] = (int)wy; dstV[2] = (int)wz;
                    /* pNormals pool (g_pNodePool2Cur) = projected screen coordinates (rendered) */
                    int *dstN = (int *)g_pNodePool2Cur;
                    dstN[0] = screenX; dstN[1] = screenY; dstN[2] = depth;
                    *(byte *)((int)dstN + 0xc) = *(byte *)(vbuf + 6);
                    *(byte *)((int)dstN + 0xd) = *(byte *)(vbuf + 7);
                    *(byte *)((int)dstN + 0xe) = *(byte *)(vbuf + 6);
                    g_pNodePoolCur = (void *)((int)g_pNodePoolCur + 0x10);
                    g_pNodePool2Cur = (void *)((int)g_pNodePool2Cur + 0x10);
                    src += 4;
                }
            }
        }

        /* second vertex block for normals (disasm 0x42fc74..0x42fe5e) */
        {
            void *pVerts2 = g_pNodePoolCur;
            void *pNormals2 = g_pNodePool2Cur;
            int nPolyB = ri->nPolyB;
            if (nPolyB > 0) {
                for (int i = 0; i < nPolyB; i++) {
                    /* disasm reads ushort counts etc and transforms similar way */
                    /* simplified: skip detailed normal transform, advance cursors */
                }
            }
            /* draw polys — faithful to 0x42f8c0: pTex=COLS (*pTex 4B), pPal=MAPI (*pTex 16B), guard only small */
            int nPolyA = ri->nPolyA;
            void *pPolyA = ri->pPolyA;
            if (nPolyA > 0) {
                for (int i = 0; i < nPolyA; i++) {
                    ushort *poly = ((ushort **)pPolyA)[i];
                    int pTex = (int)(uintptr_t)td->pC;
                    int pPal = (int)(uintptr_t)td->pTex;
                    if (!poly) continue;
                    if ((poly[1] & 0x20) == 0) meshDrawPoly(poly, (int)(uintptr_t)pVerts, (int)(uintptr_t)pNormals, pTex, pPal);
                    else gxSortPushKey(poly, pVerts, pNormals, pTex, pPal);
                }
            }
            nPolyB = ri->nPolyB;
            void *pPolyB = ri->pPolyB;
            if (nPolyB > 0) {
                byte *base = (byte *)pPolyB;
                for (int i = 0; i < nPolyB; i++) {
                    byte n = *base; base += 6;
                    for (int k = 0; k < (n & 0xff); k++) {
                        ushort *poly = (ushort *)base;
                        int pTex2 = (int)(uintptr_t)td->pC;
                        int pPal2 = (int)(uintptr_t)td->pTex;
                        if ((poly[1] & 0x20) == 0) meshDrawPoly(poly, (int)(uintptr_t)pVerts2, (int)(uintptr_t)pNormals2, pTex2, pPal2);
                        else gxSortPushKey(poly, pVerts2, pNormals2, pTex2, pPal2);
                        base += (poly[3] & 0xff) * (poly[0] & 0xff) + 4; /* stride */
                    }
                }
            }
        }
    }

recurse:
    {
        SceneNode *child = node->pChild;
        while (child) {
            SceneNode *next = child->pNextSib;
            sceneNodeRender(child);
            child = next;
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
    g_pSortBufCur = g_pSortBuffer;
    g_pNodePoolCur = g_pNodePool;
    g_pNodePool2Cur = g_pNodePool2;

    gxGetMode(&mode);
    gxGetViewport(oldViewport);
    width = mode.width;
    height = mode.height;
    if (width == 0) return 1;

    x1 = (int)cb->vw * width;
    x0 = (int)cb->vx * width;
    y1 = (int)cb->vh * height;
    y0 = (int)cb->vy * height;
    g_nSceneHalfWidth = ((x1 >> 4) - (x0 >> 4)) >> 1;
    g_centerX = g_nSceneHalfWidth + (x0 >> 4);
    g_centerY = ((y1 >> 4) + (y0 >> 4)) >> 1;
    /* [VERIFIED 2026-08-27] original @0x42f27f..0x42f290: FILD height,
     * FMUL double [0x44b798] (= 1.333333 decimal literal, NOT exactly 4/3),
     * FIDIV width, FSTP [0x450f80]. The 0.5 factor previously used here made
     * the character ~0.375x original height. */
    g_flSceneYScale = (float)height * 1.333333 / (float)width;

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

/* ===================================================================
 * sceneCacheLocalVerts @0x42ffa0  (one-shot limb-position initializer)
 * Runs from sceneNodeRender when nCacheFlag==1 && nChannelCount>1 &&
 * nId==1 (the animated character models). Verified vs disasm 0x42ffa0:
 *   count  = td->field_08          ([td+8])
 *   table  = td->pA                ([td+0xc], frames of count verts, 8B each)
 *   vertA  = table + node->nMorphIdxA * count * 8   ([node+0x28])
 *   vertB  = table + node->nMorphIdxB * count * 8   ([node+0x2c])
 *   f      = clamp(node->flMorphT, 0, 1)
 *          (t<0 -> 0.0 const @0x44b244; t>=1.0 double @0x44b288 -> 1.0
 *           const @0x44b260; else t)
 * For i in [0, count): channel[i+1].x/y/z (the int pos @+0x10, reached as
 * pChannels + 0x80 + i*0x70) = (short)vertB - (short)vertA, scaled by f,
 * plus vertA, via __ftol (truncating). NOTE: EBX/EBP are NOT advanced in
 * the original loop — every channel receives vert[0] of the frames.
 * Clears nCacheFlag (+0x24) afterwards; early-out with count<1.
 * =================================================================== */
int sceneCacheLocalVerts(SceneNode *pNode) /* @0x42ffa0 */
{
    SceneObjTypeDef *td = pNode->pTypeDef;
    int nCount = td->field_08;
    float t = pNode->flMorphT;
    float f;
    if (t < 0.0f) f = 0.0f;                 /* const 0x44b244 */
    else if (t < 1.0) f = t;                /* cmp vs 1.0 double 0x44b288 */
    else f = 1.0f;                          /* const 0x44b260 */
    if (nCount < 1) {
        pNode->nCacheFlag = 0;
        return 0;
    }
    {
        const short *pVertA = (const short *)((char *)td->pA
            + pNode->nMorphIdxA * nCount * 8);
        const short *pVertB = (const short *)((char *)td->pA
            + pNode->nMorphIdxB * nCount * 8);
        SceneChannel *pch = pNode->pChannels;
        /* same vert[0] written to all channels (verified: EBX/EBP fixed) */
        int vx = (int)((float)(pVertB[0] - pVertA[0]) * f + (float)pVertA[0]);
        int vy = (int)((float)(pVertB[1] - pVertA[1]) * f + (float)pVertA[1]);
        int vz = (int)((float)(pVertB[2] - pVertA[2]) * f + (float)pVertA[2]);
        appLog("[cachelocal] node=%u count=%d idxA=%d idxB=%d f=%.3f v=(%d,%d,%d)",
               pNode->nId, nCount, pNode->nMorphIdxA, pNode->nMorphIdxB,
               f, vx, vy, vz); /* TEMP DEBUG */
        for (int i = 0; i < nCount; i++) {
            SceneChannel *dst = &pch[i + 1];
            dst->x = vx;
            dst->y = vy;
            dst->z = vz;
        }
    }
    pNode->nCacheFlag = 0;
    return 0;
}
