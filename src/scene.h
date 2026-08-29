#ifndef SCENE_H
#define SCENE_H

#include "gx.h"
#include "pool.h"
#include "anim.h"

#ifndef ushort
typedef unsigned short ushort;
typedef unsigned int   uint;
#endif

/* scene.h — scene graph + software 3D renderer cluster (maniac.exe 0x42xxxx/0x43xxxx).
 * Faithful reimplementation of the charselect 3D preview pipeline:
 *   sceneSystemInit @0x42ed40   sceneNodeAllocChild @0x4319e0   sceneryObjAlloc @0x430200
 *   scenNameToId @0x431ed0      sceneObjSetPos @0x430660        sceneObjSetPosOrient @0x4307d0
 *   sceneNodeGetPosWorld @0x430e80  sceneNodeFacePos @0x431030  sceneNodeFree @0x430460
 *   sceneRender @0x42f1c0       sceneBuildRootMatrix @0x42f520  sceneCameraBasisCalc @0x42f460
 *   sceneNodeRender @0x42f8c0   sceneMorphInterp @0x4300d0      chanCalcWorldTransform @0x42f6e0
 *   meshDrawPoly @0x42e940      gxSortPushKey @0x42ecf0         (anim cluster in anim.h)
 *
 * The SceneObjTypeDef is the MESH chunk serialized in a .sen file; scenNameToId
 * returns its raw bytes (see sen.c). The SceneNode is a 0xa8 (+subobj*0x70) record. */

/* --- shared mesh registry (defined in sen.c) --- */
extern void *g_pMeshTable;     /* @0x45e930 8-byte entries {char*name, void*meshData} */
extern int   g_nMeshTableCount;/* @0x45e994 */

/* --- SceneObjTypeDef: a MESH chunk (serialized in a .sen). sceneMeshFixup
 * relocates the pointer fields at +0xc,+0x10,+0x14,+0x20,+0x28,+0x30 (these
 * are offsets that become absolute). The renderer reads +0x14 as a pointer to
 * a SceneObjRenderInfo sub-struct. Verified against CHARACTERS.SEN dump
 * (ROLAND: hdr {1,1,15} pRender 0xe8 at file 0x4c20) and disassembly of
 * sceneMeshFixup @0x4320f0 and sceneryObjAlloc @0x430200. */
typedef struct SceneObjTypeDef {
    int field_00;        /* +0x00  (hdr0, low byte = bSubObjCount for sceneryObjAlloc) */
    int nSubObjs;        /* +0x04  count for sceneMeshFixup loop */
    int field_08;        /* +0x08  if >0 then pA/pB are relocated; low byte+1 = nChannelCount */
    void *pA;            /* +0x0c  relocated if field_08>0 (short[4] pos array) */
    void *pB;            /* +0x10  relocated if field_08>0 (int idx array) */
    struct SceneObjRenderInfo *pRender; /* +0x14  relocated -> SceneObjRenderInfo */
    int field_18;        /* +0x18 */
    int nTex;            /* +0x1c  texture count for sceneCreateTextureSurfaces */
    void *pTex;          /* +0x20  relocated (or via g_pMapGeom) */
    int field_24;        /* +0x24 */
    void *pC;            /* +0x28  relocated (or via g_pMapGeom+8) */
    int field_2c;        /* +0x2c */
    void *pD;            /* +0x30  relocated */
} SceneObjTypeDef;

/* --- SceneObjRenderInfo sub-struct pointed to by SceneObjTypeDef.pRender (+0x14).
 * Layout verified by parsing menu\CHARACTERS.SEN (ROLAND mesh at 0x4b30,
 * pRender=0xe8) and the sceneNodeRender @0x42f8c0 / sceneMeshFixup @0x4320f0
 * / sceneMorphInterp @0x4300d0 disassembly. Offsets are file offsets
 * relocated by sceneMeshFixup (pVerts, pPolyA etc become absolute).
 *  nVerts/pVerts at +0x04/+0x08 (sceneMorphInterp), nPolyA/pPolyA at
 *  +0x14/+0x18 (aiNavNodeUpdate), nGroups/pGroups at +0x1c/+0x20,
 *  nPolyB at +0x24, pPolyB at +0x2c. sceneMeshFixup relocates pVerts,
 *  pPolyA, pGroups, pPolyB and (nPolyA + nPolyB) consecutive pointers at
 *  pPolyA against pMesh (or *(g_pMapGeom+0x10) = g_pSubObjData when a
 *  MAPI chunk is present). g_pNavMeshData points at this struct. */
typedef struct SceneGroupInfo {
    int nVerts;          /* +0x00 vertices in group */
    int _pad04;          /* +0x04 */
    int nChanIdx;        /* +0x08 channel index for group */
} SceneGroupInfo;        /* 0x0c */

/* One indexed-primitive record stored in the SceneObjRenderInfo.pPolyA
 * pointer array. Shared by the renderer (meshDrawPoly @0x42e940) and the
 * AI nav-mesh scan (aiNavNodeUpdate @0x428d50): nFans entries of
 * bFanIdxCount shorts each; the first bType shorts of each entry carry
 * the vertex indices (only their low bytes are used as vertex indices,
 * the rest is per-fan payload). SceneMeshPrim lives in the SUBO chunk
 * data in the .sen file; prim pointers are offsets from g_pSubObjData
 * until sceneMeshFixup relocates them. */
typedef struct __attribute__((packed)) SceneMeshPrim {
    unsigned char bFans;         /* +0x00 fan/entry count (end = prim + bFans*bFanIdxCount*2 + 8) */
    unsigned char bType;         /* +0x01 1=points 2=lines 3=tri fans 4=quad list */
    unsigned short wFlags;       /* +0x02 renderer flags (gxSetOrigin low word; bit2 pal-idx, bit3 tex-color idx) */
    byte bReserved04;            /* +0x04 */
    byte bReserved05;            /* +0x05 */
    unsigned char bFanIdxCount;  /* +0x06 shorts per fan entry; cursor step = this * 2 */
    unsigned char bReserved07;   /* +0x07 */
    byte abFans[1];              /* +0x08 fan entries: first bType bytes = vertex indices */
} SceneMeshPrim;                 /* header + first fan byte (fans continue per bFanIdxCount*2 bytes) */

typedef struct SceneObjRenderInfo {
    int field_00;        /* +0x00 constant 10000000 (0x989680) in shipped meshes */
    int nVerts;          /* +0x04 vertex count (sceneMorphInterp, sceneMeshFixup size stat) */
    short *pVerts;       /* +0x08 short[4] vertex frames {x,y,z,pad}, stride 8 */
    int field_0c;        /* +0x0c if != 0 then +0x10 is a relocated pointer */
    int field_10;        /* +0x10 */
    int nPolyA;          /* +0x14 prim group A count */
    struct SceneMeshPrim **pPolyA; /* +0x18 prim pointer array A (relocated) */
    int nGroups;         /* +0x1c group count for sceneNodeRender loop */
    SceneGroupInfo *pGroups; /* +0x20 pointer to group array (relocated) */
    int nPolyB;          /* +0x24 poly group B count */
    int field_28;        /* +0x28 */
    struct SceneMeshPrim **pPolyB; /* +0x2c poly group B ptr (relocated) */
} SceneObjRenderInfo;    /* 0x30 */

/* --- channel record: 0x70 bytes. Layout verified from disassembly of
 * chanBuildRotMatrix @0x42f030, chanCalcWorldTransform @0x42f6e0 and
 * mat3x3Mul @0x42f7d0. All 3x3 matrices are stored COLUMN-MAJOR
 * (m[col*3 + row]); mat3x3Mul computes out = a * b under that storage.
 *   rot[3]  (+0, int16)  yaw, pitch, roll
 *   fUnk6   (+6, float)  scale multiplier used by chanBuildRotMatrix
 *   bFlagA  (+0xa, byte) set 1 after matr built (skip rebuild)
 *   bFlagB  (+0xb, byte) set 1 after world transform computed (skip)
 *   nIdx    (+0xc, int)  index into parent/self channel list
 *   x,y,z   (+0x10) local translation (int)
 *   matr[9] (+0x1c) local matrix, column-major
 *   wmat[9] (+0x40) world matrix, column-major
 *   wx,wy,wz(+0x64) world translation */
typedef struct __attribute__((packed)) SceneChannel {
    short rot[3];          /* +x   yaw, pitch, roll (int16 degrees) */
    float fUnk6;           /* +6   scale multiplier */
    byte  bFlagA;          /* +0xa set 1 after matr built */
    byte  bFlagB;          /* +0xb set 1 after world transform computed */
    int   nIdx;            /* +0xc parent/self channel index */
    int   x, y, z;         /* +0x10 local translation (int) */
    float matr[9];         /* +0x1c local matrix (column-major) */
    float wmat[9];         /* +0x40 world matrix (column-major) */
    float wx, wy, wz;      /* +0x64 world translation */
} SceneChannel;            /* 0x70 */

/* --- morph output buffer used as pOut in sceneMorphInterp @0x4300d0.
 * The original writes interpolated vertices starting at +4 (header dword
 * skipped).  The first dword is unused padding at the call site (g_pMeshPool). */
typedef struct SceneMorphOut {
    int _pad0;                  /* +0 header (unused) */
    short verts[1];             /* +4 variable-length short[4] per vertex */
} SceneMorphOut;

/* --- scene node: 0xa8 bytes (+ bSubObjCount*0x70). Layout from
 * sceneNodeRender / chanCalcWorldTransform assembly:
 *   +0  nId    (word)  render gate: ==1 or >=0x100 to draw td
 *   +2  bType  (byte)  type discriminator (1 skip-td, 2 exit)
 *   +c  pChild (int)   first child (recursion)
 *   +14 pChannels (int) pointer to embedded SceneChannel at +0x38
 *   +20 pTypeDef (int)  SceneObjTypeDef* (relocated)
 * The 0x70-byte channel is embedded at +0x38.  Fields at +0x28/0x2c/0x30
 * are morph lerp indices + factor (sceneMorphInterp). */
typedef struct __attribute__((packed)) SceneNode {
    unsigned short nId;         /* +0 */
    unsigned char  bType;       /* +2 */
    unsigned char  nChannelCount; /* +3 channel count */
    struct SceneNode *pParent;  /* +4 */
    struct SceneNode *pNextSib; /* +8 */
    struct SceneNode *pChild;   /* +0xc  first child (recursion) */
    struct SceneNode *pPrevLink;/* +0x10 prev sibling link (for unlink) */
    SceneChannel *pChannels;    /* +0x14 -> node + 0x38 */
    int  nBoundingRadiusA;      /* +0x18 bounding radius part A (sceneNodeUpdateBounds) */
    int  nBoundingRadiusB;      /* +0x1c bounding radius part B (distance gate) */
    SceneObjTypeDef *pTypeDef;  /* +0x20 SceneObjTypeDef* (mesh) */
    int  nCacheFlag;            /* +0x24 ==1 triggers sceneCacheLocalVerts */
    int  nMorphIdxA;            /* +0x28 morph source frame index */
    int  nMorphIdxB;            /* +0x2c morph dest frame index */
    float flMorphT;             /* +0x30 morph lerp factor 0..1 */
    int  nField_34;             /* +0x34 */
    SceneChannel ch;            /* +0x38 (0x70) embedded channel */
} SceneNode;                    /* 0xa8 */

/* Scene root / camera node (set by sceneSystemInit @0x42ed40 and by
 * menuInit's sceneNodeAlloc @0x4318e0). The original passes it directly to
 * sceneObjSetPos/sceneNodeFacePos/sceneRender (e.g. 0x41f7b8), so it is a
 * SceneNode*. Declared after the typedef because of the type. */
/* --- CameraFollowBlock @0x4588f8 — the scene-render/camera-follow block.
 * The four node pointers ARE the globals g_pSceneRoot (+0), 0x4588fc
 * (follow node), g_pCamPosNode (+8, 0x458900) and g_pCamAimNode (+0xc,
 * 0x458904); +0x10/+0x14 (0x458908/0x45890c) hold the follow divisor and
 * snap distance loaded from config objects/camera rot_max/rot_speed by
 * levelObjectsCartsCameraInit @0x411b70. sceneRender @0x42f1c0 receives
 * *(void**)0x4588f8 = pNode (the root node doubles as the camera block:
 * its nId==2 short is the render-mode gate and +0x20..0x30 hold the
 * virtual-rect shorts and width/height/renderT floats written by
 * sceneNodeAlloc @0x4318e0). */
typedef struct CameraFollowBlock {
    SceneNode *pNode;        /* +0x00 camera root node (= g_pSceneRoot) */
    SceneNode *pFollowNode;  /* +0x04 local player char node (cameraSetClassMeshes) */
    SceneNode *pPosNode;     /* +0x08 = g_pCamPosNode */
    SceneNode *pAimNode;     /* +0x0c = g_pCamAimNode */
    int        nDiv;         /* +0x10 cfg objects/camera rot_max (0x458908) */
    int        nSnapDist;    /* +0x14 cfg objects/camera rot_speed (0x45890c) */
} CameraFollowBlock;         /* 0x18 */

extern CameraFollowBlock g_camFollowBlock; /* @0x4588f8..0x45890f */
#define g_pSceneRoot     (g_camFollowBlock.pNode)     /* @0x4588f8 */
#define g_pCamFollowNode (g_camFollowBlock.pFollowNode) /* @0x4588fc */
#define g_pCamPosNode    (g_camFollowBlock.pPosNode)  /* @0x458900 */
#define g_pCamAimNode    (g_camFollowBlock.pAimNode)  /* @0x458904 */

/* Current scene-object context used by sceneNodeGetPos mode 6 (relative
 * positions). Written by sceneSetCurrentObj @0x430d98 (called from
 * playerAnimSfxUpdate per player). */
extern SceneNode *g_pSceneNodeHead;   /* @0x45e810 */
extern int        g_nSceneCurrentObj; /* @0x45e608 channel index of current obj */

/* Per-level detail/culling grid built by sceneDetailGridCtor @0x42ad00
 * (called from levelSetup with (this, 0, 4, 0x400, 10000)). 0x20 bytes.
 * The pCells layout is [level * nRows + col]: row 0 holds the column
 * header node ids, rows 1..nCols-1 the "_<level><col>" detail nodes. */
typedef struct SceneDetailGrid {
    int   nFailed;      /* +0x00 1 = setup failed -> fatalError */
    int   nRootNode;    /* +0x04 root scene node */
    int   nColsFilled;  /* +0x08 column entries registered */
    int   nRows;        /* +0x0c row stride (0x400) */
    int   nCols;        /* +0x10 detail-level count (4) */
    int  *pCells;       /* +0x14 nCols*nRows node ids (memPool) */
    void *pRowBuf;      /* +0x18 nColsFilled * 0x14 mesh bboxes (memPool) */
    float *pColScales;  /* +0x1c nCols squared-distance detail thresholds */
} SceneDetailGrid;      /* 0x20 */

/* camera/root block passed to sceneRender (mode==2 @+0, viewport rect @+0x20)
 * Layout verified vs disasm 0x4318e0 / 0x42f1c0: +0 mode (short, ==2),
 * +0x20 vx/vy/vw/vh (short), +0x28 nWidth (float), +0x2c nHeight (float),
 * +0x30 renderT (float/int bits, used as float via FILD). The original
 * repurposes a SceneNode (0xa8 bytes) as this block — fields at +0x20..+0x30
 * overlap SceneNode.nId/bType/pTypeDef region but sceneRender only reads
 * mode and the viewport fields. For type safety nWidth/nHeight are float. */
typedef struct __attribute__((packed)) SceneCameraBlock {
    short mode;         /* +0  == 2 */
    short unk2;
    int   unk4;
    int   unk8;
    int   unkC;
    int   unk10;
    int   unk14;
    int   unk18;
    int   unk1c;
    short vx;           /* +0x20 virtual rect x (short) */
    short vy;           /* +0x22 */
    short vw;           /* +0x24 */
    short vh;           /* +0x26 */
    float nWidth;       /* +0x28 float (bits moved via int in disasm) */
    float nHeight;      /* +0x2c float */
    float renderT;      /* +0x30 float (stored via int, FILD in sceneNodeRender) */
} SceneCameraBlock;

/* --- globals --- */
extern void *g_pSceneNodeList;     /* flat render list (advanced by +8) */
extern void *g_pSortBuffer;        /* @0x45e914 sort buffer */
extern void *g_pSortBufCur;
extern void *g_pNodePool;          /* @0x45e604 */
extern void *g_pNodePool2;         /* @0x45e648 */
extern void *g_pMeshPool;          /* @0x45e610 */
extern void *g_pNodePoolCur;       /* @0x45e908 cursor into g_pNodePool */
extern void *g_pNodePool2Cur;      /* @0x45e5fc cursor into g_pNodePool2 */
extern void *g_pRootMatrix;        /* @0x45e8d4 camera/world matrix (points at g_rootChannel, orig 0x45e818) */
extern SceneNode g_rootNode;       /* @0x45e8c0 root node storage */
extern float *g_pSinTable;           /* @0x45e5f8 sin table 0x400 */
extern float *g_pSinTree;            /* @0x45e888 sin tree 0x3ff8 */
extern int   g_nSceneNodeCount;
extern int   g_nSceneNodeMemUsed;
extern int   g_nSceneryObjCountPeak;
extern int   g_nSceneNodeCountPeak;
extern int   g_nSceneNodeMemPeak;
extern float g_nSceneWidth;  /* @0x45e900 float */
extern float g_nSceneHeight; /* @0x45e614 float */
extern float g_flSceneAspect;
extern int   g_nSceneHalfWidth;   /* @0x450f70 */
extern int   g_centerX;           /* @0x450f74 */
extern int   g_centerY;           /* @0x450f78 */
extern float g_sceneRenderT;      /* @0x450f7c */
extern float g_flSceneYScale;     /* @0x450f80 */
extern int   g_nSceneDistMax;
extern int   g_nSceneDrawCount;
extern int   g_nNodePoolSize;
extern int   g_nSceneBufSize;
extern int   g_nSortBufCount;
extern int   g_nMeshPoolSize;
extern int   g_nSceneFlags;
extern int   g_nSceneFlagTexAnim;
extern float g_sceneCameraBasis;   /* +0 */
extern float g_sceneCameraBasis_2;
extern float g_sceneCameraBasis_3;
extern float g_sceneCameraBasis_4;
extern float g_sceneCameraBasis_5;
extern float g_sceneCameraBasis_6;
extern float g_sceneCameraBasis_7;

/* --- prototypes --- */
int  sceneSystemInit(int nNodePoolSize, int nSceneBufSize, int nSortBufCount, int nMeshPoolSize, unsigned int nFlags);
int  sceneFreeAllNodes(void);       /* @0x42f150 */
int  sceneSystemClose(void);        /* @0x42f180 */
void *sceneNodeAlloc(void *pChannelPtr, void *pChannelPtr2, void *pChannelPtr3, short nMeshIdx, short nUnk5, short nUnk6, short nUnk7); /* @0x4318e0 */
void *sceneNodeAllocChild(SceneNode *pParent, void *pChannelPtr, void *pChannelPtr2, void *pChannelPtr3, void *pChannelPtr4);
void *sceneryObjAlloc(SceneNode *pParent, int nChanPtr, int nChanPtr2, int nChanPtr3, int nChanPtr4,
                      short nScaleX, short nScaleZ, short nScaleY, void *pTypeDef);
int  scenNameToId(LPCSTR pszName);
int  sceneCollectMeshHandles(int *pOut, int nMax, const char *pszFilter); /* @0x42b360 */
int  sceneFindByName(SceneNode **pOut, int nMax, const char *pszFilter); /* @0x431fd0 */
int  scenNameToIdEx(LPCSTR pszName); /* @0x431e20 */
int  sceneObjSetPos(SceneNode *pObj, int nX, int nY, int nZ, int nMode); /* @0x430660 */
int  sceneObjSetPosOrient(SceneNode *pObj, short nYaw, short nPitch, short nRoll, byte nMode); /* @0x4307d0 */
int  sceneObjSetSubPos(SceneNode *pObj, int nMeshIdx, short nYaw, short nPitch, short nRoll, byte nMode); /* @0x430a90 */
int  sceneObjSetSubOrient(SceneNode *pObj, int nMeshIdx, short nYaw, short nPitch, short nRoll); /* @0x431110 */
int  sceneNodeGetPosWorld(SceneNode *pNode, float *pOutXYZ, int nMode); /* @0x430e80 */
int  sceneNodeGetPos(SceneNode *pNode, int nChannel, int *pOutXYZ, int nMode); /* @0x431270 */
int  sceneNodeSetPos(SceneNode *pNode, void *pXYZ, int nMode); /* @0x431590 — raw 32-bit copy (int channel storage) */
int  sceneNodeSetPosShorts(SceneNode *pNode, short *pAngles, byte nMode); /* @0x431850 */
int  sceneNodeGetChannelPos(SceneNode *pNode, int nChannel, short *pOutAngles,
                            uint nMode, short *pOutAngles2); /* @0x4315e0 */
SceneObjTypeDef *sceneNodeGetMesh(SceneNode *pNode); /* @0x431ae0 */
int  sceneSetCurrentObj(SceneNode *pNodeHead, int nCurrentObj); /* @0x430d98 */
void sceneMeshBBox(SceneNode *pNode, int *pOutBBox); /* @0x42ba40 */
void *sceneDetailGridCtor(SceneDetailGrid *pGrid, int nRootNode, int nCols,
                          int nRows, int nCellSize); /* @0x42ad00 */
void sceneDetailGridSetRoot(SceneDetailGrid *pGrid, SceneNode *pRootNode); /* @0x42b350 */
void sceneDetailGridAddRow(SceneDetailGrid *pGrid, int *pHandles, int nCount); /* @0x42b000 */
int  sceneObjSetClassMesh(int pObj, SceneNode *pClassNode, int nMeshIdx, int nMode); /* @0x430db0 */
int  sceneNodeSetHiddenFlag(SceneNode *pNode, int nMode); /* @0x4305c0 */
int  sceneNodeFacePos(SceneNode *pNode, int nChannel, float flX, float flY, float flZ, int nMode); /* @0x431030 */
void sceneNodeFree(SceneNode *pNode, int nFreeChildren); /* @0x430460 */
void sceneNodeUpdateBounds(SceneNode *pNode); /* @0x4303c0 */
int  sceneRender(void *pCameraBlock);
void sceneBuildRootMatrix(SceneNode *pRootNode); /* @0x42f520 */
void sceneCameraBasisCalc(void);
int  sceneNodeRender(SceneNode *pNode); /* @0x42f8c0 */
void *sceneMorphInterp(SceneNode *pNode, SceneObjRenderInfo *pRender, void *pOut); /* @0x4300d0 */
void chanCalcWorldTransform(SceneNode *pNode, int nChannel); /* @0x42f6e0 */
void meshDrawPoly(ushort *pPolyData, int pNormals, int pVerts, int pTexColors, int pPalColors);
void meshDrawTriClip(byte *pIdxList, int pVerts, int pNormals, void *pUV,
                     void *pColor, int nUnk, int bInterpColor, int bInterpUV); /* @0x42d070 */
void meshDrawQuadClip(byte *pIdxList, int pVerts, int pNormals, void *pUV,
                      void *pColor, int nUnk, int bInterpColor, int bInterpUV); /* @0x42daf0 */
void gxSortPushKey(void *pMesh, void *pVerts, void *pNormals, int pTex, int pPalette);
void mat3x3Mul(float *a, float *b, float *out);
void chanBuildRotMatrix(SceneChannel *ch);
int  sceneCacheLocalVerts(SceneNode *pNode); /* @0x42ffa0 */

/* sceneRayFindNearest @0x42a750 — walk g_pNavNodeList and return the first
 * floor node whose plane height at (flX, flZ) is within flMaxDist above
 * flHeight and whose walls collide with point (flX, flZ) at radius
 * flRadius (zoneWallCircleHit). */
void *sceneRayFindNearest(float flZ, float flX, float flHeight,
                          float flMaxDist, float flRadius);

/* Bungee-degree conversion constants shared by the trig helpers and
 * walkAnimTableEntryCalc (declared in scene.c). */
extern const double g_dblBdgToRad;   /* @0x44b788 = pi/32768 */
extern const double g_dblRadToBdg;   /* @0x44b780 = 65536/(2*pi) */

#endif /* SCENE_H */
