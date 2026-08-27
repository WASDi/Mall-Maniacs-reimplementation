#ifndef SEN_H
#define SEN_H

/* sen.h — SEN scene-file loader cluster (reimplementation of the maniac.exe
 * .SEN reading pipeline). Faithful to the Ghidra decompilation/disassembly:
 *
 *   sceneLoadSen               @0x432320  open REV2 file, iterate chunks, fixup meshes
 *   senChunkParse              @0x432c00  chunk-tag dispatch for nested KEEP/TEMP data
 *   sceneMeshFixup             @0x4320f0  relocate mesh node pointer fields
 *   sceneCreateTextureSurfaces @0x432260  bind texture ids to surfaces
 *
 * The .SEN format is: 4cc "REV2" + u32 total size, then a chain of chunk
 * records {int tag, int size, data}. Top-level tags handled by sceneLoadSen:
 * MESH (mesh geometry node), EMAN (mesh name string), ONAM (object name
 * list), TNAM (object name table), SUBO (sub-object data), COLS (collision),
 * MAPI (map geometry), TANI (text anim), KEEP/TEMP (contain nested chunks
 * parsed by senChunkParse), OBJI (object instances). For CHARACTERS.SEN only
 * MESH + EMAN chunks are present; the full object-instantiation path is
 * deferred and stubbed as sceneInstantiateObjects in stubs.c (out of scope
 * for the menu preview per AGENTS.md). */

#include <windows.h>

/* sceneLoadSen @0x432320 — load a .SEN scene file. Returns a scene-instance
 * handle (the memPool that owns the loaded data) or 0 on failure. param_2 is
 * an optional output pointer for the first scene object (unused by the menu).
 * The returned pool is intentionally NOT destroyed here (caller owns it). */
int  sceneLoadSen(LPCSTR path, int *param_2);

/* senChunkParse @0x432c00 — parse a chain of nested .sen chunk records
 * between pData and pDataEnd. Populates the mesh/object/name tables. Returns 1. */
int  senChunkParse(byte *pData, byte *pDataEnd);

/* sceneMeshFixup @0x4320f0 — relocate the pointer fields of a loaded mesh
 * node. pMesh = node base (relocated MESH bytes = SceneObjTypeDef), pNames =
 * object-name table, pMapGeom = &g_pMapGeom (NULL or pointer to map-geometry
 * base; when non-null the +0x20/+0x28 and vertex fixups use it). Returns 1. */
int sceneMeshFixup(int pMesh, void *pNames, int pMapGeom);

/* sceneCreateTextureSurfaces @0x432260 — bind a list of texture ids to
 * surfaces (via gxCreateSurface). pTexIdList is an array of 0x10-byte records
 * where the first dword is the texture id; pszFilenames is a packed list of
 * NUL-terminated names (one per unique id up to maxId+1). Returns 1 on success,
 * 0 on gxCreateSurface failure. */
int  sceneCreateTextureSurfaces(int *pTexIdList, int nCount, char *pszFilenames);

/* --- mesh-table registry (shared with scene.c / scenNameToId) ---
 * g_pMeshTable holds 8-byte entries {char *name, void *pMeshData}; pMeshData is
 * the raw MESH chunk bytes, which are a serialized SceneObjTypeDef. */
extern void *g_pMeshTable;      /* @0x45e930 */
extern int   g_nMeshTableCount; /* @0x45e994 */
extern char g_szSceneDir[];     /* @0x45e950 scene base dir (set by scenSetDir) */
extern char *g_pObjNameTable;   /* @0x45e990 TNAM packed names */
extern int g_nObjNameTableSize; /* @0x45eaa8 */
extern int *g_pMapGeom;         /* @0x45eb20 MAPI base */
extern int g_nMapGeomCount;     /* @0x45eb24 */
extern char *g_pColsData;       /* @0x45eb28 COLS base */
extern int g_nColsCount;        /* @0x45eb2c */
extern char *g_pSubObjData;     /* @0x45eb30 SUBO base */

#endif /* SEN_H */
