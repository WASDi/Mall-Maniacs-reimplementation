#include "compat_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include "scene.h"
#include "scene_system.h"
#include "gx.h"
#include "pool.h"
#include "time.h"
#include "util.h"
#include "custom_helpers.h"

/* =====================================================================
 * Scene graph + software 3D renderer (maniac.exe 0x42xxxx/0x43xxxx).
 * Reimplemented from Ghidra decompilations; trig uses sinf/cosf (the
 * original used a 0x400-entry sin table — numerically equivalent).
 * World->screen projection in sceneNodeRender is best-effort pending
 * assembly-level verification of the exact clip/divide formulas.
 * ===================================================================== */

/* --- globals --- */
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


/* trig result globals (used by callers that read the FPU result) */
static float g_flMathSin = 0.0f;
static float g_flMathCos = 0.0f;
const float g_flHalf = 0.5f; /* @0x44b274 shared 0.5f literal (also used by aiSteerToTarget, menus, ...) */

/* The engine stores all rotation angles as int16 "binary degrees":
 * 65536 = 360 degrees. mathSinDeg/mathCosDeg (@0x42d030/@0x42d050) take the
 * int16 angle, FILD it and multiply by the double constant @0x44b788
 * (= 2*pi/65536, exact stored value below) before FSIN/FCOS.
 * mathAtan2Deg (@0x42d010) FPATANs and multiplies by @0x44b780
 * (= 65536/(2*pi), exact stored value below) before ftol. */
const double g_dblBdgToRad = 9.58737992553711e-05;             /* @0x44b788 */
const double g_dblRadToBdg = 10430.378349108529;               /* @0x44b780 */
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
 * Original stores the right-child POINTER as raw bits in tree[1] (float
 * slot holds address). 64-bit port: store the right-child float OFFSET
 * from the tree base as a uint32 instead (0 = none) — same 8-byte node
 * layout and 0x3ff8 buffer, no pointer truncation. No reconstructed
 * consumer walks this tree yet (trig uses g_pSinTable/mathSinDeg).
 * TODO: original used x87 fsin via float10; sin(double) is numerically close. */
static void mathSinTreeBuildRec(int a, int b, float *tree, float *base)
{
    int middle;
    int leftNodes;
    float *right;

    if (a + 1 == b) {
        tree[0] = (float)(((double)a + 0.5) * g_dblTrigStep);
        /* tree[1] = 0 (no right child) */
        memset(&tree[1], 0, sizeof(float));
        return;
    }
    middle = a + (b - a) / 2;
    tree[0] = (float)sin((double)middle * g_dblTrigStep); /* TODO: fsin(float10) */
    leftNodes = 2 * (middle - a) - 1;
    mathSinTreeBuildRec(a, middle, tree + 2, base);
    right = tree + 2 + leftNodes * 2;
    {
        uint32_t off = (uint32_t)(right - base);
        memcpy(&tree[1], &off, sizeof(off));
    }
    mathSinTreeBuildRec(middle, b, right, base);
}
void mathSinTreeBuild(int a, int b, float *tree) /* @0x42efb0 */
{
    mathSinTreeBuildRec(a, b, tree, tree);
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
    g_pSortBuffer = malloc(nSortBufCount * 5 * sizeof(uintptr_t)); /* @0x45e914 (5 native slots/entry) */
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
    rc->bFlagA = 0;
    chanBuildRotMatrix(rc); /* @0x42f030 */
    /* original @0x42ef20: byte [0x45e823] = 1 — the root channel's bFlagB is
     * set to 1 HERE and never cleared (the per-frame dirty-clear loops stop
     * at g_rootNode), which terminates chanCalcWorldTransform's parent walk
     * at the root. Zeroing it makes the walk recurse (root,0) -> (NULL,0)
     * forever (stack overflow in the first mode-6 sceneNodeGetPos, e.g.
     * playerAnimSfxUpdate cart-aim on the first gameplay frame). */
    rc->bFlagB = 1;
    /* Copy matr → wmat (original copies 9 floats from 0x45e834 to 0x45e858) */
    memcpy(rc->wmat, rc->matr, sizeof(rc->wmat));
    g_pSceneRoot = &g_rootNode; /* @0x4588f8 */
    g_nSceneFlags = nFlags & 0xffffffef; /* @0x45e920 */
    g_nSceneFlagTexAnim = ((int)(char)nFlags & 0x10U) >> 4; /* @0x45e924 */
    return 1;
}

/* sceneFreeAllNodes @0x42f150 — free every node reachable from the
 * g_pSceneNodeList head via sceneNodeFree(n,1). The head aliases the root
 * node's pChild so it advances as each node unlinks; capturing the next
 * sibling before the free keeps the loop identical to the original. */
int sceneFreeAllNodes(void) /* @0x42f150 */
{
    while (g_pSceneNodeList != NULL) {
        SceneNode *pNext = ((SceneNode *)g_pSceneNodeList)->pNextSib;
        sceneNodeFree((SceneNode *)g_pSceneNodeList, 1);
        g_pSceneNodeList = pNext;
    }
    return 1;
}

/* sceneSystemClose @0x42f180 — scene/render system close: free all scene
 * nodes, then the sort buffer and node/mesh pools from sceneSystemInit.
 * roundStartInit pairs sceneSystemInit...sceneSystemClose around the
 * level texture-load pass. */
int sceneSystemClose(void) /* @0x42f180 */
{
    sceneFreeAllNodes();
    memFree(g_pSortBuffer);
    memFree(g_pNodePool);
    memFree(g_pNodePool2);
    memFree(g_pMeshPool);
    return 1;
}
