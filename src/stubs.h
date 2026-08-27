#ifndef STUBS_H
#define STUBS_H

#include <windows.h>

/* =====================================================================
 * Declared interfaces for unfinished behavior.
 * Each is a TODO stub: logs and performs a documented no-op or safe
 * placeholder transition so the vertical slice proceeds; callers keep their
 * contract unchanged when the stub body is later replaced. Original
 * target/address noted where one exists.
 * ===================================================================== */

/* gameInit @0x409d90 — now implemented in src/game.c (GX path + guard).
 * Original full init also handled player records, file XOR, config, etc.
 * — those sub-blocks are intentionally deferred (see game.c). */
void gameInit(void);

/* Main-menu row targets (menu.c dispatch table g_kMenuRowTarget, entered
 * from menuUpdate @0x41b0b0 on Enter). Contract: state-func convention
 * (nType 0 = frame update, nType 1 + nKeyType 2 = keydown); each sets
 * g_pStateFunc to the next state. TODO stubs: log once + return to the
 * menu. Interfaces stay fixed when the real bodies replace them.
 * gotoOptions @0x41d300 is now implemented in options.c (Alternativ ->
 * Svårighetsgrad, Grafik ignored). */
int stateNetworkMenu(int nType, int nKey, int nKeyType);     /* @0x420190 */
/* stateHighScoreTable @0x41dfd0 — now implemented in record.c */

/* stateCharacterSelect @0x41efa0 — now implemented in charselect.c */

/* sceneInstantiateObjects — documented stub for the out-of-scope scene-graph
 * population performed at the end of sceneLoadSen @0x432320 (OBJI objects,
 * scenery nodes, music emitters). For menu\CHARACTERS.SEN the object-instance
 * and map-geometry counts are zero, so this is never reached by the offline
 * menu preview; it is a safe no-op stub. When gameplay lands, the real body
 * rebuilds the scene graph from g_pObjInstances / g_pMapGeom / g_pTextAnimData.
 * Contract: takes the owning memPool handle (unused), returns 1 for success. */
int sceneInstantiateObjects(int pool);

/* Early-init deferred stubs — keep call hierarchy intact for
 * TrackRebuildDetailed. Real implementations will replace these when
 * the scene / string subsystems land. */

/* scenNameTableInit @0x431cb0 — (re)initialize scene name/mesh tables.
 * Original allocates g_pScenePool/mesh tables via memPoolCreate/Alloc.
 * Stub: no allocation (guarded) but preserves call graph; returns 1. */
int scenNameTableInit(int nMeshCount, int nScenObjCap);

/* scenSetDir @0x432e60 — set scene base directory g_szSceneDir[64].
 * Stub: no copy, returns 1. */
int scenSetDir(LPCSTR pszDir);

/* sceneFindByName @0x431fd0 — find ids by substring via strFindSubstring.
 * Stub: returns 0 matches. */
int sceneFindByName(int *pOut, int nMax, char *pszSubstr);

/* sceneNodeSetHiddenFlag @0x4305c0 — set hidden flag at node+2, nMode
 * 1→1, 2→1+recurse children, 3→2. Stub: sets flag if node non-null,
 * guarded self-call preserves 1/1 recursion count without infinite loop. */
int sceneNodeSetHiddenFlag(int pNode, int nMode);

/* mStringAssignCopy @0x435440 — MString deep copy (thiscall). Original:
 * memFreeDirect + operator_new + memcpy. Stub: no-op, returns this. */
void *mStringAssignCopy(void *pThis, void *pSrc);

#endif /* STUBS_H */
