#ifndef SEN_H
#define SEN_H

/* sen.h — SEN scene-file loader cluster (reimplementation of the maniac.exe
 * .SEN reading pipeline). Faithful to the Ghidra decompilation:
 *
 *   sceneLoadSen        @0x432320  open REV2 file, iterate chunks, fixup meshes
 *   senChunkParse       @0x432c00  chunk-tag dispatch for nested KEEP/TEMP data
 *   sceneMeshFixup      @0x4320f0  relocate mesh node pointer fields
 *   sceneCreateTextureSurfaces @0x432260  bind texture ids to surfaces
 *
 * The .SEN format is: 4cc "REV2" + u32 total size, then a chain of chunk
 * records {int tag, int size, data}. Top-level tags handled by sceneLoadSen:
 * MESH (mesh geometry node), EMAN (mesh name string), ONAM (object name
 * list), TNAM (object name table), SUBO (sub-object data), COLS (collision),
 * MAPI (map geometry), TANI (text anim), KEEP/TEMP (contain nested chunks
 * parsed by senChunkParse), OBJI (object instances). For CHARACTERS.SEN only
 * MESH + EMAN chunks are present, so the scene-graph instantiation path does
 * not run (it is stubbed per Rebuild.md, out of scope for the menu preview).
 *
 * Rendering of the selected character is performed by the original scene
 * system (sceneNodeAllocChild / sceneryObjAlloc / anmLoad), not by this
 * loader cluster. */

#include <windows.h>

/* sceneLoadSen @0x432320 — load a .SEN scene file. Returns a scene-instance
 * handle (the memPool that owns the loaded data) or 0 on failure. param_2 is
 * an optional output pointer for the first scene object (unused by the menu).
 * The returned pool is intentionally NOT destroyed here (caller owns it). */
int  sceneLoadSen(LPCSTR path, int *param_2);

/* senChunkParse @0x432c00 — parse a chain of nested .sen chunk records
 * between pData and pDataEnd. Populates the mesh/object/name tables. */
int  senChunkParse(byte *pData, byte *pDataEnd);

/* sceneMeshFixup @0x4320f0 — relocate the pointer fields of a loaded mesh
 * node (param_1 = node base, param_2 = object-name table, param_3 =
 * &g_pMapGeom). */
void sceneMeshFixup(int param_1, char param_2, int param_3);

/* sceneCreateTextureSurfaces @0x432260 — bind a list of texture ids to
 * surfaces (via gxCreateSurface). Returns 1 on success. */
int  sceneCreateTextureSurfaces(int *pTexIdList, int nCount, char *pszFilenames);

/* --- mesh-table registry (shared with scene.c / scenNameToId) ---
 * g_pMeshTable holds 8-byte entries {char *name, void *pMeshData}; pMeshData is
 * the raw MESH chunk bytes, which are a serialized SceneObjTypeDef. */
extern void *g_pMeshTable;
extern int   g_nMeshTableCount;

#endif /* SEN_H */
