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
 * parsed by senChunkParse), OBJI (object instances — instantiated into the
 * scene graph by the inline loop at the end of sceneLoadSen). */

#include "compat_types.h"

/* sceneLoadSen @0x432320 — load a .SEN scene file. Returns a scene-instance
 * handle (the memPool that owns the loaded data) or 0 on failure. param_2 is
 * an optional output pointer for the first scene object (unused by the menu).
 * The returned pool is intentionally NOT destroyed here (caller owns it). */
int  sceneLoadSen(LPCSTR path, int *param_2);

/* senChunkParse @0x432c00 — parse a chain of nested .sen chunk records
 * between pData and pDataEnd. Populates the mesh/object/name tables. Returns 1. */
int  senChunkParse(byte *pData, byte *pDataEnd);

/* (Superseded by the native-expansion sceneMeshFixup below; the
 * original int-based contract is kept in Ghidra.) */

/* sceneCreateTextureSurfaces @0x432260 — bind a list of texture ids to
 * surfaces (via gxCreateSurface). pTexIdList is an array of 0x10-byte records
 * where the first dword is the texture id; pszFilenames is a packed list of
 * NUL-terminated names (one per unique id up to maxId+1). Returns 1 on success,
 * 0 on gxCreateSurface failure. */
int  sceneCreateTextureSurfaces(int *pTexIdList, int nCount, char *pszFilenames);

/* Geometry-base block for sceneMeshFixup (mirrors the original's 5-global
 * block at 0x45eb20: MAPI ptr/count, COLS ptr/count, SUBO ptr). Pointer-
 * sized on 64-bit; the fixup reads it by field, never by numeric offset. */
typedef struct SenGeom {
    void *pMapGeom;      /* MAPI chunk base (NULL when absent) */
    int   nMapGeomCount;
    void *pColsData;     /* COLS chunk base */
    int   nColsCount;
    void *pSubObjData;   /* SUBO chunk base */
} SenGeom;

/* sceneMeshFixup @0x4320f0 — expand a raw MESH chunk into native runtime
 * structs (SceneObjTypeDef + SceneObjRenderInfo array + pointer arrays),
 * allocated from the scene pool. Returns the native typedef, NULL on
 * allocation failure. pImage is the raw chunk base; RVAs resolve against
 * it (or the geom bases exactly as the original). */
struct SceneObjTypeDef *sceneMeshFixup(void *pImage, void *pNames, SenGeom *pGeom, int nPool);

/* --- mesh-table registry (shared with scene_render.c / scenNameToId) ---
 * g_pMeshTable holds 8-byte entries {char *name, void *pMeshData}; pMeshData is
 * the raw MESH chunk bytes, which are a serialized SceneObjTypeDef. */
/* 64-bit port: entries carry native pointers (mesh data or scene nodes),
 * so the id is pointer-sized (8-byte entries on 32-bit, 16 on 64-bit).
 * Serialized name/id pairs on disk are parsed into this at load. */
typedef struct ScenNameEntry {
    char *pszName;   /* +0x00 */
    void  *pId;      /* +0x04 mesh data (mesh table) / scene node (scene-obj table) */
} ScenNameEntry;

/* Mesh-table entry view (same storage): name + MESH data pointer. */
typedef struct MeshTableEntry {
    char *name;      /* +0x00 */
    void *data;      /* +0x04 MESH chunk (native typedef after sceneMeshFixup) */
} MeshTableEntry;

extern MeshTableEntry *g_pMeshTable; /* @0x45e930 */
extern int   g_nMeshTableCount; /* @0x45e994 */
extern ScenNameEntry *g_pScenObjTable; /* @0x45eb10 {name,node} scene-object table */
extern char g_szSceneDir[];     /* @0x45e950 scene base dir (set by scenSetDir) */
extern char *g_pObjNameTable;   /* @0x45e990 TNAM packed names */
extern int g_nObjNameTableSize; /* @0x45eaa8 */
extern int *g_pMapGeom;         /* @0x45eb20 MAPI base */
extern int g_nMapGeomCount;     /* @0x45eb24 */
extern char *g_pColsData;       /* @0x45eb28 COLS base */
extern int g_nColsCount;        /* @0x45eb2c */
extern char *g_pSubObjData;     /* @0x45eb30 SUBO base */

/* scenNameTableInit @0x431cb0 — allocate and reset the mesh, scene-object,
 * and name tables for a scene load. Returns zero if any allocation fails. */
int scenNameTableInit(int nMeshCount, int nScenObjCap);

/* scenNameTableFree @0x431e00 — destroy the scene name-table pool. */
int scenNameTableFree(void);

/* scenSetDir @0x432e60 — copy pszDir into g_szSceneDir @0x45e950 (NULL
 * clears it). The dir is prefixed to every mesh name (EMAN) and, via
 * scenExpandNameList, to every object name (ONAM) of subsequently
 * loaded .sen files. */
int scenSetDir(LPCSTR pszDir);

/* scenExpandNameList @0x432dd0 — expand the packed ONAM copy in
 * [pList, pEnd) in place by inserting pszDir in front of every name;
 * returns the number of bytes added. Called by sceneLoadSen when
 * g_szSceneDir is non-empty. */
int scenExpandNameList(char *pList, void *pEnd, char *pszDir);

#endif /* SEN_H */
