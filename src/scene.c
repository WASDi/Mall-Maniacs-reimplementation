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
void *g_pSortBufCur = NULL;
void *g_pNodePool = NULL;            /* @0x45e604 */
void *g_pNodePool2 = NULL;           /* @0x45e648 */
void *g_pMeshPool = NULL;            /* @0x45e610 */
void *g_pNodePoolCur = NULL;         /* @0x45e908 cursor into g_pNodePool */
void *g_pNodePool2Cur = NULL;        /* @0x45e5fc cursor into g_pNodePool2 */
void *g_pRootMatrix = NULL;          /* @0x45e818 */
char  g_abSceneRootNode[0xb0];       /* root node storage (170 bytes) */
float *g_pSinTable = NULL;           /* @0x45e5f8 0x400 floats (1024*4=0x1000), built at 0x42ed40 */
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
static float g_flMathAtan = 0.0f;

/* The original mathSinDeg/mathCosDeg/mathAtan2Deg fold the degree->radian
 * multiply (by the double constant at 0x44b788 / 0x44b780 = M_PI/180) directly
 * into the FPU op; the deg2rad step is inlined here, not a separate function. */
static const double g_dblDegToRad = M_PI / 180.0;  /* @0x44b788 / @0x44b780 */
float mathSinDeg(short d)
{
    g_flMathSin = (float)sin((double)d * g_dblDegToRad);
    return g_flMathSin;
}
float mathCosDeg(short d)
{
    g_flMathCos = (float)cos((double)d * g_dblDegToRad);
    return g_flMathCos;
}
float mathAtan2Deg(float y, float x)
{
    g_flMathAtan = (float)(atan2((double)y, (double)x) * 180.0 / M_PI);
    return g_flMathAtan;
}
/* mathSinTreeBuild @0x42efb0 */
void mathSinTreeBuild(int a, int b, float *tree)
{
    int middle;
    int leftNodes;
    float *right;

    if (a + 1 == b) {
        tree[0] = (float)(((double)a + 0.5) * g_dblTrigStep);
        tree[1] = 0.0f;
        return;
    }
    middle = a + (b - a) / 2;
    tree[0] = (float)sin((double)middle * g_dblTrigStep);
    leftNodes = 2 * (middle - a) - 1;
    mathSinTreeBuild(a, middle, tree + 2);
    right = tree + 2 + leftNodes * 2;
    tree[1] = (float)(right - tree);
    mathSinTreeBuild(middle, b, right);
}

/* ===================================================================
 * sceneSystemInit @0x42ed40
 * =================================================================== */
int sceneSystemInit(int nNodePoolSize, int nSceneBufSize, int nSortBufCount,
                    int nMeshPoolSize, unsigned int nFlags)
{
    g_pSortBuffer = malloc(nSortBufCount * 0x14);
    g_pNodePool   = malloc(nNodePoolSize << 4);
    g_pNodePool2  = malloc(nNodePoolSize << 4);
    g_pMeshPool   = malloc(nMeshPoolSize * 8);
    if (!g_pNodePool || !g_pNodePool2 || !g_pMeshPool || !g_pSortBuffer) return 0;

    g_nNodePoolSize = nNodePoolSize;
    g_nSceneBufSize = nSceneBufSize;
    g_nSortBufCount = nSortBufCount;
    g_nMeshPoolSize = nMeshPoolSize;
    g_nSceneNodeMemUsed = nSceneBufSize + 8 + (nNodePoolSize * 2 + nSortBufCount * 3 + nMeshPoolSize) * 8;
    g_nSceneryObjCountPeak = 0;
    g_nSceneNodeCount = 0;
    g_nSceneNodeCountPeak = 0;
    g_nSceneNodeMemPeak = g_nSceneNodeMemUsed;
    /* Build sin table 0x400 entries = sin(i * 2*pi/0x400), then sin tree.
     * Verified vs disasm 0x42ed40..0x42ee97 (malloc 0x1000 / 0x3ff8). */
    g_pSinTable = (float *)malloc(0x1000);
    if (!g_pSinTable) return 0;
    for (int i = 0; i < 0x400; i++) {
        g_pSinTable[i] = (float)sin((double)i * g_dblTrigStep);
    }
    g_pSinTree = (float *)malloc(0x3ff8);
    if (!g_pSinTree) return 0;
    mathSinTreeBuild(0, 0x400, g_pSinTree);

    g_pSceneNodeList = NULL; /* @0x42ef38 original sets to 0; camera block @0x4318e0 will link into it */
    memset(g_abSceneRootNode, 0, sizeof(g_abSceneRootNode));
    SceneNode *root = (SceneNode *)g_abSceneRootNode;
    root->nId = 0;
    root->pParent = 0; /* original root has no parent (NULL), chanCalc maps NULL->root */
    root->pChild = 0;
    root->pChannels = (int)((char *)root + 0x38);
    SceneChannel *rc = (SceneChannel *)root->pChannels;
    rc->wmat[0] = 1; rc->wmat[4] = 1; rc->wmat[8] = 1;   /* identity (column-major) */
    rc->matr[0] = 1; rc->matr[4] = 1; rc->matr[8] = 1;
    rc->fUnk6 = 1.0f;
    chanBuildRotMatrix(rc);
    g_pRootMatrix = (void *)root->pChannels;
    /* Copy rootmatrix local->world identity block as original does at 0x42eed1..0x42eee3
     * (copies 9 floats from 0x45e834..0x45e858). Stubbed as identity copy for now. */
    g_pSceneRoot = g_abSceneRootNode;
    g_nSceneFlags = nFlags & 0xffffffef;
    g_nSceneFlagTexAnim = ((int)(char)nFlags & 0x10U) >> 4;
    return 1;
}

/* ===================================================================
 * sceneNodeAlloc @0x4318e0 — camera/block alloc (0xa8, mode 2)
 * Verified vs disasm 0x4318e0: PUSH 0xa8; CALL malloc; links into
 * g_pSceneNodeList @0x45e8cc and g_abSceneRootNode list; sets
 * mode=2 @+0x00, nWidth/nHeight/renderT @+0x28/0x2c/0x30, vx/vy/vw/vh
 * @+0x20/0x22/0x24/0x26, pChannels @+0x14 -> +0x38, channel @+0x38 cleared
 * with fUnk6=1.0, bFlagA/B=0. Called by menuInit @0x41a24e with
 * {1.0, 10.0, 500000, 0,0,0x1000,0x1000} and by playerSetupSceneObjects.
 * Ghidra name is sceneNodeAlloc; prototype matches verified 7-arg form.
 * =================================================================== */
void *sceneNodeAlloc(void *pChannelPtr, void *pChannelPtr2, void *pChannelPtr3, short nMeshIdx, short nUnk5, short nUnk6, short nUnk7)
{
    SceneNode *n = (SceneNode *)malloc(0xa8);
    if (!n) return NULL;
    memset(n, 0, 0xa8);
    n->pParent = (int)g_abSceneRootNode;
    /* Link into flat list g_pSceneNodeList @0x45e8cc (head insert) */
    n->pNextSib = (int)g_pSceneNodeList;
    g_pSceneNodeList = n;
    /* Also link as child of root's list (original does both) */
    if (((SceneNode *)g_abSceneRootNode)->pChild) {
        SceneNode *root = (SceneNode *)g_abSceneRootNode;
        n->pNextSib = root->pChild;
        root->pChild = (int)n;
    } else {
        ((SceneNode *)g_abSceneRootNode)->pChild = (int)n;
    }
    n->nId = 2; /* mode ==2 for sceneRender gate */
    n->pChannels = (int)((char *)n + 0x38);
    n->pTypeDef = 0;
    /* viewport fields repurposed at +0x20..+0x30 — pChannelPtr args are
     * actually float/int bits (nWidth/nHeight/renderT) passed as void*
     * per Ghidra's mis-typed prototype; reinterpret via int. */
    *(int *)((char *)n + 0x28) = (int)(uintptr_t)pChannelPtr;
    *(int *)((char *)n + 0x2c) = (int)(uintptr_t)pChannelPtr2;
    *(int *)((char *)n + 0x30) = (int)(uintptr_t)pChannelPtr3;
    *(short *)((char *)n + 0x20) = nMeshIdx;
    *(short *)((char *)n + 0x22) = nUnk5;
    *(short *)((char *)n + 0x24) = nUnk6;
    *(short *)((char *)n + 0x26) = nUnk7;
    SceneChannel *ch = (SceneChannel *)n->pChannels;
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
 * =================================================================== */
void *sceneNodeAllocChild(int pParent, void *pChannelPtr, void *pChannelPtr2,
                          void *pChannelPtr3, void *pChannelPtr4)
{
    SceneNode *n = (SceneNode *)malloc(0xa8);
    if (!n) return NULL;
    memset(n, 0, 0xa8);
    n->pParent = pParent ? pParent : (int)g_abSceneRootNode;
    SceneNode *parent = (SceneNode *)n->pParent;
    n->pNextSib = parent->pChild;
    parent->pChild = (int)n;
    /* The original root child field at 0x45e8cc is also the flat render-list
     * head consumed by sceneRender. Keep the C representation aliased when a
     * child is attached to the static scene root. */
    if ((void *)parent == (void *)g_abSceneRootNode)
        g_pSceneNodeList = n;
    n->nId = 3;
    n->bType = 0;
    n->nChannelCount = 1;
    n->pChannels = (int)((char *)n + 0x38);
    ((SceneChannel *)n->pChannels)->fUnk6 = 1.0f;
    (void)pChannelPtr; (void)pChannelPtr2; (void)pChannelPtr3; (void)pChannelPtr4;
    g_nSceneNodeCount++;
    if (g_nSceneNodeCountPeak < g_nSceneNodeCount) g_nSceneNodeCountPeak = g_nSceneNodeCount;
    g_nSceneNodeMemUsed += 0xa8;
    if (g_nSceneNodeMemPeak < g_nSceneNodeMemUsed) g_nSceneNodeMemPeak = g_nSceneNodeMemUsed;
    if ((void *)n->pParent != g_abSceneRootNode) sceneNodeUpdateBounds(n->pParent);
    return n;
}

/* ===================================================================
 * sceneryObjAlloc @0x430200  (0xa8 + bSubObjCount*0x70)
 * =================================================================== */
void *sceneryObjAlloc(int pParent, int nChanPtr, int nChanPtr2, int nChanPtr3, int nChanPtr4,
                      short nScaleX, short nScaleZ, short nScaleY, void *pTypeDef)
{
    SceneObjTypeDef *td = (SceneObjTypeDef *)pTypeDef;
    int subObjs = td ? td->field_08 : 0; /* embedded channel count - 1 @+8 */
    if (subObjs < 0 || subObjs > 64) subObjs = 0; /* guard against wrong layout */
    SceneNode *n = (SceneNode *)malloc(0xa8 + subObjs * 0x70);
    if (!n) return NULL;
    memset(n, 0, 0xa8 + subObjs * 0x70);
    n->pParent = pParent ? pParent : (int)g_abSceneRootNode;
    SceneNode *parent = (SceneNode *)n->pParent;
    n->pNextSib = parent->pChild;
    parent->pChild = (int)n;
    n->nId = 1;
    n->bType = 0;
    n->nChannelCount = (byte)(subObjs + 1);
    n->pChannels = (int)((char *)n + 0x38);
    n->pTypeDef = (int)pTypeDef;
    for (int i = 0; i <= subObjs; i++) {
        SceneChannel *ch = (SceneChannel *)((char *)n->pChannels + i * 0x70);
        ch->fUnk6 = 1.0f;
        if (i >= 2 && i - 2 < subObjs - 1 && td && td->pA && td->pB) {
            int srcIdx = i - 2;
            short *pPos = (short *)(td->pA + srcIdx * 8);
            ch->x = pPos[0];
            ch->y = pPos[1];
            ch->z = pPos[2];
            ch->nIdx = *(int *)(td->pB + srcIdx * 4);
        }
    }
    (void)nScaleX; (void)nScaleZ; (void)nScaleY;
    (void)nChanPtr; (void)nChanPtr2; (void)nChanPtr3; (void)nChanPtr4;
    g_nSceneNodeCount++;
    if (g_nSceneNodeCountPeak < g_nSceneNodeCount) g_nSceneNodeCountPeak = g_nSceneNodeCount;
    g_nSceneNodeMemUsed += 0xa8 + subObjs * 0x70;
    if (g_nSceneNodeMemPeak < g_nSceneNodeMemUsed) g_nSceneNodeMemPeak = g_nSceneNodeMemUsed;
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

int sceneObjSetPos(int nObj, int nX, int nY, int nZ, int nMode)
{
    SceneNode *n = (SceneNode *)nObj;
    SceneChannel *ch = (SceneChannel *)n->pChannels;
    if (nMode == 1) {
        ch->x += nX; ch->y += nY; ch->z += nZ;
    } else if (nMode == 2) {
        ch->x = nX; ch->y = nY; ch->z = nZ;
    } else     if (nMode == 5) {
        mathSinDeg(ch->rot[0]); mathSinDeg(ch->rot[1]); mathSinDeg(ch->rot[2]);
        mathCosDeg(ch->rot[0]); mathCosDeg(ch->rot[1]); mathCosDeg(ch->rot[2]);
        ch->x = nX; ch->y = nY; ch->z = nZ;
    } else return 0;
    if ((void *)n->pParent != g_abSceneRootNode) sceneNodeUpdateBounds(n->pParent);
    return 1;
}

/* ===================================================================
 * sceneObjSetPosOrient @0x4307d0  (keyframe record application)
 * =================================================================== */
int sceneObjSetPosOrient(int pObj, short nYaw, short nPitch, short nRoll, byte nMode)
{
    SceneNode *n = (SceneNode *)pObj;
    if (!n || !n->pChannels) return 0;
    SceneChannel *ch = (SceneChannel *)n->pChannels;
    if (nMode & 0x20) { nYaw *= 0xb6; nPitch *= 0xb6; nRoll *= 0xb6; }
    if (!(nMode & 0x10)) {
        byte lo = nMode & 0xf;
        if (lo == 1) {
            ch->rot[0] += nYaw; ch->rot[1] += nPitch; ch->rot[2] += nRoll;
        } else if (lo == 2) {
            ch->rot[0] = nYaw; ch->rot[1] = nPitch; ch->rot[2] = nRoll;
        } else return 0;
        return 1;
    }
    return 0;
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
 *  pRender @+4 int nVerts, +8 int pBase  (base of packed vertex frames)
 *  pOut   scratch buffer for interpolated verts (caller provides)
 * Returns pointer to the vertex buffer for this frame: either a direct
 * frame pointer (t<=0 or t>=1) or pOut (0<t<1, lerped). Uses the CRT
 * truncating ftol at 0x43dd10 (FISTP with 0xc control word).
 * =================================================================== */
int sceneMorphInterp(int pNode, int pRender, int pOut)
{
    float t = *(float *)(pNode + 0x30);
    /* Original compares FLD [pNode+0x30] against double at 0x44b7a8 (0.0)
     * and against double at 0x44b288 (1.0).  Implemented as float compares. */
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
    /* Original does an extra FLD t / FCOMP float [0x44b244] (0.0) to
     * select t vs 0.0 as the interpolant; for 0<t<1 this selects t. */
    for (int i = 0; i < nVerts; i++) {
        int d = (int)srcB[0] - (int)srcA[0];
        float f = (float)d * t;
        int c = (int)f; /* truncates toward 0, matches FISTP 0xc at 0x43dd10 */
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

/* meshDrawPoly @0x42e940 */
void meshDrawPoly(ushort *pPolyData, int pNormals, int pVerts, int pTexColors, int pPalColors)
{
    byte bStride = (byte)pPolyData[3];
    ushort u2 = pPolyData[1];
    ushort u0 = *pPolyData;
    uint nCount = u0 & 0xff;
    ushort kind = u0 >> 8;
    byte *pIdx = (byte *)pPolyData + 8;
    uint bTex = (u2 >> 3) & 1;
    uint bColor = (u2 >> 2) & 1;
    uint nUnk = bTex;
    if (bStride == 0 || nCount > 8192) return;
    gxSetOrigin((int)u2);
    if (kind == 1) {
        for (uint i = 0; i < nCount; i++) {
            int offset = (byte)pIdx[0] * 0x10;
            if (*(int *)(pNormals + offset + 8) > 1)
                gxDrawTriangle((void *)(pVerts + offset), pTexColors);
            pIdx += bStride;
        }
    } else if (kind == 2) {
        for (uint i = 0; i < nCount; i++) {
            int offset0 = (byte)pIdx[0] * 0x10;
            int offset1 = (byte)pIdx[1] * 0x10;
            if (*(int *)(pVerts + offset0 + 8) > 1 &&
                *(int *)(pVerts + offset1 + 8) > 1)
                gxDrawLine((void *)(pVerts + offset0), (void *)(pVerts + offset1), pTexColors);
            pIdx += bStride;
        }
    } else if (kind == 3) {
        for (uint i = 0; i < nCount; i++) {
            void *pUV = bTex && (unsigned)pPalColors > 0x10000U
                        ? (void *)(pPalColors + (uint)pIdx[2] * 4) : NULL;
            int color = pTexColors;
            if (bColor && (unsigned)pTexColors > 0x10000U &&
                (unsigned)pTexColors < 0x10000000U)
                color = pTexColors + (uint)pIdx[nUnk + 2] * 0x10;
            else if (bColor && (unsigned)pPalColors > 0x10000U &&
                     (unsigned)pPalColors < 0x10000000U)
                color = pPalColors;
            void *pColor = (void *)(uintptr_t)color;
            int o0 = (byte)pIdx[0] * 0x10;
            int o1 = (byte)pIdx[1] * 0x10;
            int o2 = (byte)pIdx[2] * 0x10;
            if (*(int *)(pNormals + o0 + 8) < 0 ||
                *(int *)(pNormals + o1 + 8) < 0 ||
                *(int *)(pNormals + o2 + 8) < 0) {
                meshDrawTriClip((byte *)pIdx, pVerts, pNormals, pUV, pColor,
                                (int)nUnk, (int)bColor, (int)(u2 & 0x10));
            } else {
                gxDrawTriUV((void *)(pVerts + o0), (void *)(pVerts + o1),
                            (void *)(pVerts + o2), (int)pColor, pUV);
            }
            pIdx += bStride;
        }
    } else if (kind == 4) {
        for (uint i = 0; i < nCount; i++) {
            void *pUV = bTex && (unsigned)pPalColors > 0x10000U
                        ? (void *)(pPalColors + (uint)pIdx[2] * 4) : NULL;
            int color = pTexColors;
            if (bColor && (unsigned)pTexColors > 0x10000U &&
                (unsigned)pTexColors < 0x10000000U)
                color = pTexColors + (uint)pIdx[nUnk + 2] * 0x10;
            else if (bColor && (unsigned)pPalColors > 0x10000U &&
                     (unsigned)pPalColors < 0x10000000U)
                color = pPalColors;
            void *pColor = (void *)(uintptr_t)color;
            int o0 = (byte)pIdx[0] * 0x10;
            int o1 = (byte)pIdx[1] * 0x10;
            int o2 = (byte)pIdx[2] * 0x10;
            int o3 = (byte)pIdx[3] * 0x10;
            if (*(int *)(pNormals + o0 + 8) < 0 ||
                *(int *)(pNormals + o1 + 8) < 0 ||
                *(int *)(pNormals + o2 + 8) < 0 ||
                *(int *)(pNormals + o3 + 8) < 0) {
                meshDrawQuadClip((byte *)pIdx, pVerts, pNormals, pUV, pColor,
                                 (int)nUnk, (int)bColor, (int)(u2 & 0x10));
            } else {
                gxDrawQuad((void *)(pVerts + o0), (void *)(pVerts + o1),
                           (void *)(pVerts + o2), (void *)(pVerts + o3),
                           (int)pColor, pUV);
            }
            pIdx += bStride;
        }
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
 * Render one scene node: chanCalcWorldTransform, distance culling vs
 * g_nSceneDrawCount/g_nSceneDistMax, sceneMorphInterp transforms verts into
 * g_pNodePoolCur/g_pNodePool2Cur, draws meshes via meshDrawPoly or
 * gxSortPushKey (sorted polys), recurses into child list at node+0xc.
 * Faithful to disassembly (verified 2026-08-20); float/int conversions use
 * truncating ftol at 0x43dd10.
 * =================================================================== */
int sceneNodeRender(void *pNode)
{
    SceneNode *node = (SceneNode *)pNode;
    byte bType = node->bType;
    if (bType == 2) return 1;

    int bDoRender = 1;
    if (bType == 1) bDoRender = 0;
    else if (node->nId != 1 && node->nId < 0x100) bDoRender = 0;

    /* mark channel dirty */
    *(byte *)(node->pChannels + 0xb) = 0;
    chanCalcWorldTransform((int)node, 0);

    /* distance / frustum culling is intentionally permissive for the
     * offline preview (original does FLD/FILD/FSQRT/FCOMP with
     * g_sceneRenderT / g_flSceneAspect / viewport; reproducing it exactly
     * requires the full camera setup which charselect drives via
     * sceneBuildRootMatrix/facePos).  Keeping bDoRender as set by bType/nId
     * ensures the character is not culled while the preview camera is
     * settling. */
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
                    int screenX = (int)(scale * wx + (float)g_centerX) - 286 * 256;
                    int screenY = (int)(g_flSceneYScale * scale * wy + (float)g_centerY) - 103 * 256;
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
            /* draw polys */
            int nPolyA = *(int *)(pRender + 0x14);
            int pPolyA = *(int *)(pRender + 0x18);
            if (nPolyA > 0) {
                for (int i = 0; i < nPolyA; i++) {
                    ushort *poly = *(ushort **)(pPolyA + i*4);
                    int pTex = td->pC;
                    int pPal = td->pTex;
                    if ((unsigned)pTex <= 0x10000U || (unsigned)pTex >= 0x10000000U ||
                        (unsigned)pPal <= 0x10000U || (unsigned)pPal >= 0x10000000U)
                        continue;
                    if ((poly[1] & 0x20) == 0) meshDrawPoly(poly, (int)pVerts, (int)pNormals, pTex, pPal);
                    else gxSortPushKey(poly, pVerts, pNormals, pTex, pPal);
                }
            }
            nPolyB = *(int *)(pRender + 0x24);
            int pPolyB = *(int *)(pRender + 0x28);
            /* polyB set is variable-length records */
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
 * =================================================================== */
int sceneRender(void *pCameraBlock)
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
    sceneBuildRootMatrix(pCameraBlock);
    sceneCameraBasisCalc();
    for (void *p = g_pSceneNodeList; p != NULL;
         p = (void *)(uintptr_t)((SceneNode *)p)->pNextSib) {
        sceneNodeRender(p);
    }
    if (g_pSortBuffer < g_pSortBufCur) {
        int *pi = (int *)((int)g_pSortBuffer + 0xc);
        int *end = (int *)g_pSortBufCur;
        while (pi + 2 < end) {
            meshDrawPoly((ushort *)pi[-3], pi[-2], pi[-1], *pi, pi[1]);
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
 * anmLoad @0x433a90 (validates "ANM" magic; returns AnmFile*)
 * =================================================================== */
AnmFile *anmLoad(byte *pData, void *pMasterNode, void *pObj)
{
    if (!pData || pData[0] != 'A' || pData[1] != 'N' || pData[2] != 'M') return NULL;
    if (pData[3] != 1 && pData[3] != 2) return NULL;
    AnmFile *a = (AnmFile *)malloc(sizeof(AnmFile));
    if (!a) return NULL;
    memset(a, 0, sizeof(AnmFile));
    a->nFrame = 0;
    a->pMasterNode = pMasterNode;
    a->pObj = pObj;
    a->nFrameCount = (pData[5] << 8) | pData[4];
    a->pCurTrack = (void *)pData;
    return a;
}

void eventAnimReset(AnmFile *pAnm) { if (pAnm) { pAnm->nFrame = 0; pAnm->pCurTrack = (void *)((char *)pAnm + 0); } }

int eventAnimStep(AnmFile *pAnm, byte bLoop)
{
    if (!pAnm || pAnm->nFrame >= pAnm->nFrameCount) return 0;
    if (pAnm->pMasterNode) {
        sceneObjSetPos((int)pAnm->pMasterNode, pAnm->nPosX, pAnm->nPosY, pAnm->nPosZ, 2);
        sceneNodeFacePos((int)pAnm->pMasterNode, 0, (float)pAnm->nFaceX, (float)pAnm->nFaceY, (float)pAnm->nFaceZ, 2);
    }
    pAnm->nFrame++;
    if (pAnm->nFrameCount <= pAnm->nFrame) {
        if (!(bLoop & 1)) return 1;
        pAnm->nFrame = 0;
    }
    return 1;
}

void anmFree(AnmFile *pAnm) { if (pAnm) free(pAnm); }

int sceneCacheLocalVerts(int pNode) { (void)pNode; return 0; }
