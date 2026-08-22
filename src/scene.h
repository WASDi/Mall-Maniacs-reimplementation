#ifndef SCENE_H
#define SCENE_H

#include "gx.h"
#include "pool.h"

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
 *   meshDrawPoly @0x42e940      gxSortPushKey @0x42ecf0         anmLoad @0x433a90
 *   eventAnimReset @0x434270    eventAnimStep @0x434090         anmFree @0x434050
 *
 * The SceneObjTypeDef is the MESH chunk serialized in a .sen file; scenNameToId
 * returns its raw bytes (see sen.c). The SceneNode is a 0xa8 (+subobj*0x70) record. */

/* --- shared mesh registry (defined in sen.c) --- */
extern void *g_pMeshTable;     /* @0x45e930 8-byte entries {char*name, void*meshData} */
extern int   g_nMeshTableCount;/* @0x45e944 */
extern void *g_pSceneRoot;     /* @0x4588f8 scene root (set by sceneSystemInit) */

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
    int pA;              /* +0x0c  relocated if field_08>0 */
    int pB;              /* +0x10  relocated if field_08>0 */
    int pRender;         /* +0x14  relocated -> SceneObjRenderInfo */
    int field_18;        /* +0x18 */
    int nTex;            /* +0x1c  texture count for sceneCreateTextureSurfaces */
    int pTex;            /* +0x20  relocated (or via g_pMapGeom) */
    int field_24;        /* +0x24 */
    int pC;              /* +0x28  relocated (or via g_pMapGeom+8) */
    int field_2c;        /* +0x2c */
    int pD;              /* +0x30  relocated */
} SceneObjTypeDef;

/* --- render-info sub-struct pointed to by SceneObjTypeDef.pRender (+0x14).
 * Layout verified by parsing menu\CHARACTERS.SEN (ROLAND mesh at 0x4b30,
 * pRender=0xe8) and the sceneNodeRender @0x42f8c0 / sceneMeshFixup @0x4320f0
 * / sceneMorphInterp @0x4300d0 disassembly. Offsets are file offsets
 * relocated by sceneMeshFixup (pVerts, pGroups etc become absolute).
 *  nVerts/pVerts at +0x04/+0x08 (sceneMorphInterp), nPolyA/pPolyA at
 *  +0x14/+0x18, nGroups/pGroups at +0x1c/+0x20, nPolyB at +0x24,
 *  pPolyB at +0x2c. */
typedef struct SceneObjRenderInfo {
    int field_00;        /* +0x00 */
    int nVerts;          /* +0x04 vertex count (sceneMorphInterp) */
    int pVerts;          /* +0x08 offset to vertex frames (x,y,z,w) */
    int field_0c;        /* +0x0c */
    int field_10;        /* +0x10 */
    int nPolyA;          /* +0x14 poly group A count */
    int pPolyA;          /* +0x18 poly group A ptr (relocated) */
    int nGroups;         /* +0x1c group count for sceneNodeRender loop */
    int pGroups;         /* +0x20 pointer to group array (relocated) */
    int nPolyB;          /* +0x24 poly group B count */
    int field_28;        /* +0x28 */
    int pPolyB;          /* +0x2c poly group B ptr (relocated) */
} SceneObjRenderInfo;

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

/* --- scene node: 0xa8 bytes (+ bSubObjCount*0x70). Layout from
 * sceneNodeRender / chanCalcWorldTransform assembly:
 *   +0  nId    (word)  render gate: ==1 or >=0x100 to draw td
 *   +2  bType  (byte)  type discriminator (1 skip-td, 2 exit)
 *   +c  pChild (int)   first child (recursion)
 *   +14 pChannels (int) pointer to embedded SceneChannel at +0x38
 *   +20 pTypeDef (int)  SceneObjTypeDef* (relocated)
 * The 0x70-byte channel is embedded at +0x38. */
typedef struct __attribute__((packed)) SceneNode {
    unsigned short nId;         /* +0 */
    unsigned char  bType;       /* +2 */
    unsigned char  nChannelCount; /* +3 channel count */
    int  pParent;               /* +4 */
    int  pNextSib;              /* +8 */
    int  pChild;                /* +0xc  first child (recursion) */
    int  unk10;                 /* +0x10 */
    int  pChannels;             /* +0x14 -> node + 0x38 */
    int  unk18;                 /* +0x18 */
    int  unk1c;                 /* +0x1c (per-node render distance bound) */
    int  pTypeDef;              /* +0x20 SceneObjTypeDef* (mesh) */
    int  unk24;                 /* +0x24 */
    int  unk28;                 /* +0x28 */
    int  unk2c;                 /* +0x2c */
    int  unk30;                 /* +0x30 */
    int  unk34;                 /* +0x34 */
    SceneChannel ch;            /* +0x38 (0x70) embedded channel */
} SceneNode;                    /* 0xa8 */

/* camera/root block passed to sceneRender (mode==2 @+0, viewport rect @+0x20) */
typedef struct SceneCameraBlock {
    short mode;         /* +0  == 2 */
    short unk2;
    int   unk4;
    int   unk8;
    int   unkC;
    int   unk10;
    int   unk14;
    int   unk18;
    short vx;           /* +0x20 virtual rect x (short, *width) */
    short vy;           /* +0x22 */
    short vw;           /* +0x24 */
    short vh;           /* +0x26 */
    int   nWidth;       /* +0x28 */
    int   nHeight;      /* +0x2c */
    float renderT;      /* +0x30 */
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
extern void *g_pRootMatrix;        /* @0x45e818 camera/world matrix */
extern char  g_abSceneRootNode[0xb0]; /* root node storage */
extern int   g_nSceneNodeCount;
extern int   g_nSceneNodeMemUsed;
extern int   g_nSceneryObjCountPeak;
extern int   g_nSceneNodeCountPeak;
extern int   g_nSceneNodeMemPeak;
extern int   g_nSceneWidth;
extern int   g_nSceneHeight;
extern float g_flSceneAspect;
extern float g_sceneRenderT;
extern int   g_nSceneDistMax;
extern int   g_nSceneDrawCount;
extern float g_gxClipTest;
extern float g_gxClipTest_2;
extern float g_gxClipTest_3;
extern float g_gxClipTest_4;
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

/* animation file (anmLoad) */
typedef struct AnmFile {
    int   nFrame;          /* +0 */
    int   nFrameCount;     /* +4 */
    void *pTrackBase;      /* +8 */
    void *pCurTrack;       /* +0xc */
    int   nLoopStart;      /* +0x10 */
    void *pPool;           /* +0x14 */
    void *pMasterNode;     /* +0x18 */
    void *pObj;            /* +0x1c */
    int   nPosX, nPosY, nPosZ;
    int   nFaceX, nFaceY, nFaceZ;
} AnmFile;

/* --- prototypes --- */
int  sceneSystemInit(int nNodePoolSize, int nSceneBufSize, int nSortBufCount, int nMeshPoolSize, unsigned int nFlags);
void *sceneNodeAllocChild(int pParent, void *pChannelPtr, void *pChannelPtr2, void *pChannelPtr3, void *pChannelPtr4);
void *sceneryObjAlloc(int pParent, int nChanPtr, int nChanPtr2, int nChanPtr3, int nChanPtr4,
                      short nScaleX, short nScaleZ, short nScaleY, void *pTypeDef);
 int  scenNameToId(LPCSTR pszName);
int  sceneObjSetPos(int nObj, int nX, int nY, int nZ, int nMode);
int  sceneObjSetPosOrient(int pObj, short nYaw, short nPitch, short nRoll, byte nMode);
int  sceneNodeGetPosWorld(int nNode, float *pOutXYZ, int nMode);
int  sceneNodeFacePos(int pNode, int nChannel, float flX, float flY, float flZ, int nMode);
void sceneNodeFree(void *pNode, int nFreeChildren);
void sceneNodeUpdateBounds(int nNode);
int  sceneRender(void *pCameraBlock);
void sceneBuildRootMatrix(void *pRootNode);
void sceneCameraBasisCalc(void);
int  sceneNodeRender(void *pNode);
int  sceneMorphInterp(int param_1, int param_2, int param_3);
void chanCalcWorldTransform(int param_1, int param_2);
void meshDrawPoly(ushort *pPolyData, int pNormals, int pVerts, int pTexColors, int pPalColors);
void gxSortPushKey(void *pMesh, void *pVerts, void *pNormals, int pTex, int pPalette);
AnmFile *anmLoad(byte *pData, void *pMasterNode, void *pObj);
void eventAnimReset(AnmFile *pAnm);
int  eventAnimStep(AnmFile *pAnm, byte bLoop);
void anmFree(AnmFile *pAnm);
void mat3x3Mul(float *a, float *b, float *out);
void chanBuildRotMatrix(short *pRot);
int  sceneCacheLocalVerts(int pNode);

#endif /* SCENE_H */
