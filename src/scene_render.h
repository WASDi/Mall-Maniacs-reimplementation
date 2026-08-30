#ifndef SCENE_RENDER_H
#define SCENE_RENDER_H

#include "scene.h"

/* scene_render.h — query, detail grid, morph, poly, camera and render
 * (maniac.exe 0x42b360/0x42f8c0/0x42f1c0 etc). */

int scenNameToId(LPCSTR pszName); /* @0x431ed0 */
int sceneCollectMeshHandles(int *pOut, int nMax, const char *pszFilter); /* @0x42b360 */
int sceneFindByName(SceneNode **pOut, int nMax, const char *pszFilter); /* @0x431fd0 */
int scenNameToIdEx(LPCSTR pszName); /* @0x431e20 */

void sceneMeshBBox(SceneNode *pNode, int *pOutBBox); /* @0x42ba40 */
void *sceneDetailGridCtor(SceneDetailGrid *pGrid, int nRootNode, int nCols,
                          int nRows, int nCellSize); /* @0x42ad00 */
void sceneDetailGridSetRoot(SceneDetailGrid *pGrid, SceneNode *pRootNode); /* @0x42b350 */
void sceneDetailGridAddRow(SceneDetailGrid *pGrid, int *pHandles, int nCount); /* @0x42b000 */

void *sceneMorphInterp(SceneNode *pNode, SceneObjRenderInfo *pRender, void *pOut); /* @0x4300d0 */
void meshDrawTriClip(byte *pIdxList, int pVerts, int pNormals, void *pUV,
                     void *pColor, int nUnk, int bInterpColor, int bInterpUV); /* @0x42d070 */
void meshDrawQuadClip(byte *pIdxList, int pVerts, int pNormals, void *pUV,
                      void *pColor, int nUnk, int bInterpColor, int bInterpUV); /* @0x42daf0 */
void meshDrawPoly(ushort *pPolyData, int pNormals, int pVerts, int pTexColors, int pPalColors); /* @0x42e940 */
void gxSortPushKey(void *pMesh, void *pVerts, void *pNormals, int pTex, int pPalette); /* @0x42ecf0 */
void sceneCameraBasisCalc(void); /* @0x42f460 */
void sceneBuildRootMatrix(SceneNode *pRootNode); /* @0x42f520 */
int sceneNodeRender(SceneNode *pNode); /* @0x42f8c0 */
int sceneRender(void *pCameraBlock); /* @0x42f1c0 */
int sceneCacheLocalVerts(SceneNode *pNode); /* @0x42ffa0 */
void *sceneRayFindNearest(float flZ, float flX, float flHeight,
                          float flMaxDist, float flRadius); /* @0x42a750 */

#endif /* SCENE_RENDER_H */
