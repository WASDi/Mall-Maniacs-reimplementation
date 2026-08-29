# Mall Maniacs (maniac.exe) — 15. Undefined-type retyping

[Back to README](README.md)

Status: **COMPLETE (2026-08-20).** The final census is 213 = 65 network
(`net*`/`mnet*`/`stateNet*`/`startServer`/`n*Cmd`, out of scope) + 148
`Unwind@0044xxxx` stubs. **Zero game functions remain with undefined types.**
The remaining network functions are deferred to a dedicated network pass; the
Undefined-type function drive-down is done.

## Purpose and scope

This document tracks the `undefined*` census produced by `FindUndefinedTypes.java`
for `maniac.exe`. The original non-CRT scope was 578 game functions and two
standalone game globals. Statically linked MSVC CRT code is not game code and is
excluded, including the CRT function region `0x43c850-0x44966c`, CRT data, and
the 148 `Unwind@0044xxxx` exception-handling stubs.

The console dispatch table at `0x44b320-0x44b43c` and Ghidra switch/jump tables
at `0x41bf3c`, `0x41ef20`, and `0x41ff98` are deliberately left alone: their
pointer-like types describe table data, not ordinary variables.

## Completed work

- **Function prototype pass (2026-08-16):** `RetypeUndefinedFunctions2.java`
  applied each `HighFunction`'s recovered prototype across 577 game functions
  (281 return types, eight parameter updates), dropping the census 724 → 526.
- **Math correction:** `mathSinDeg`/`mathCosDeg` (`0x42d030`/`0x42d050`) return
  `long double` — x87 `fsin`/`fcos` results are `float10`, not `float`.
- **Global data pass (2026-08-16):** standalone game descriptors at `0x44f0e8`
  and `0x44fdc0` typed/named (`g_szSceneNameTable`, `g_szHudPathDescriptor`);
  a later targeted pass resolved the remaining standalone game globals
  (Swedish UI strings, menu/player data, scene-text state, network setup
  fields). Remaining undefined global labels are sub-symbols/overlaps of typed
  parents, not independent items.

### Evidence-backed structs created

`ConfigNode` (28B), `ConfigToken` (20B), `ConfigBlockNode` (44B),
`ConfigValueNode` (40B), `ConfigStringNode` (36B), `ConfigEnv` refined
(`pTokenHead`/`pTokenTail`/`pRoot` node pointers), `MString` (8B: `char *pPsz`
+0, `int nLen` +4), `gxVec2` (8B), `AnmFile` (56B), `AnmSet` (24B),
`PlayerRecord` (884B = 0x374, the `g_playerRecords` stride; `g_apPlayers` is its
player-view sub-object at +0x150), `MovieDb` (28B = ConfigEnv + `szMovie`),
`ShotObj` (68B), `QuestRecord` (24B), `WorldNode` (72B). Scene-text glyph area
is `void *[16]`; `g_abWsaData` is a Winsock startup refcount, not `WSADATA`.

### Signature conventions established (2026-08-20 batches)

- **mString/config:** all 14 `mString*` + ~33 `config*` functions retyped
  (ctors, parser, tree builder, save/serialize, find/get/set, frees,
  predicates).
- **Vector/math/obj/scene:** seven `gxVec2*`/polar helpers, `mathSegIntersect*`
  → `int (..., float *pOut)`, `mathFixedRecip` → `uint (uint)`, `objSetPos`/
  `objMovePolar`/`objSetAngle`/`objGetPos` thiscall, six scene pos/transform
  helpers (mode-switched getters/setters over mesh-array +0x14 and channel
  +0x10/+0x14/+0x18 layouts).
- **Animation:** `anmLoadFile`/`anmLoad`/`anmCalcSize`/`anmFree`/`dataReadU8`,
  the `anmSet*` binders, and playback consumers `eventAnimStep`/
  `eventAnimReset`/`sceneObjectAnimStep`.
- **Scene name-table:** `.sen` lookup cluster (`scenNameTableInit/Free`,
  `scenNameToId(Ex)`, `scenIdToName`, `scenNameMatchCollect`, `scenSetDir`,
  `senChunkParse`); fixed a mis-decompile where `char param_1` sign-extended
  0x45xxxx string addresses.
- **Menu/flow state:** 23 state functions on the dispatch convention
  `int (int nType, int nKey, int nKeyType)` — `gameFrameUpdate` @0x41a8c0
  calls `(0,0,0)` per frame, `dispatchKeyEvent` @0x41adf0 `(1,nKey,nKeyType)`.
  `quitGame` kept 2 params (direct `quitGame(0,0)`, not a StateFunc).
- **Player-record/init cluster:** `playerRecordCtor/Dtor/InitDefaults`
  (`__thiscall`), `roundStartInit` → `void (void)` (params are scratch for
  fmtSprintf buffers), `gameKeyHandler` → `void (int nKey, int nKeyType)`,
  `movieDbCtor/Dtor` (`__fastcall`).
- **Console commands:** 12-byte cmd table at `0x44b308` (`fn`/+0, `name`/+4)
  walked by `commandDispatch`; every handler is
  `int (int nContext, LPCSTR pszArgs)` with nContext = 0 or
  `PlayerRecord*` (`g_playerRecords + nLocalPlayerIdx*0x374`). Retyped
  `commandDispatch`, `commandDispatchForPlayer`, `consoleHandleKey` (line
  editor over `g_acConsoleLines`), `cmdNameStartsWith`, and 22 handlers
  (config, editor-obj, movie demo, help/kill/log/quit/reset/run/status, etc.).
  Network `n*Cmd`/`startServer` left for the network pass.
- **Misc game helpers A+B:** `fontPoolCreate/Destroy`, `sceneObjGetPosXZ`,
  `requestCmd` (deferred state change via `g_pResumeStateFunc`),
  `objShotCtor`/`objShotListFree`, `configStringDtor`, `questRecordCtor`,
  `objHashRehash`/`objHashRemoveFree`/`objHashFirst`, `objContainsPoint(3D)`,
  `tgaLoad16`, `mciPlayCdaudio`, `textDrawWrappedCentered`, `gxDrawQuadColor`,
  `worldNodeCtor`/`nodeSetTransformFromChannels`/`nodeAddChildMesh`/
  `cameraSetClassMeshes`.

`[VERIFIED]` plate comments were added to all confirmed entries in each batch.

### Final batches (2026-08-20, census 327 → 213)

- **Scene-system (10):** scene node get/set/pos helpers, `sceneSystemInit`
  `0x42ed40`, `sceneFreeAllNodes`, `sceneSystemClose`, `sceneRender`,
  `sceneNodeRender`, `sceneNodeFree`, `sceneObjResetFlags`, `sceneSetCurrentObj`,
  `sceneNodeFacePos`, `sceneCreateTextureSurfaces`, `gxLoadDriver`,
  `gxUnloadDriver`, `gxRegRead*`.
- **GX/sound-load (15):** `gxLoadDriver`/`gxUnloadDriver`, `gxRegRead*`,
  texture-surface and palette-loading cluster.
- **Music/mixer (15):** `music*`/`dsound*` cluster incl. `sndPlaySfx3D`
  `0x42bcd0` (`__thiscall`, 11 params), `sndMixBuildVoiceChains` `0x437fe0`,
  `sndMixRenderRegion` `0x438090`.
- **Renderer/mesh (13):** `meshDrawTriClip`/`meshDrawQuadClip`
  (`0x42d070`/`0x42daf0`, interp flags), `meshDrawPoly` `0x42ea30`
  (type word 0/1=tri, 2=line, 3/4=UV-clipped), `gxSortPushKey` `0x42ecf0`
  (5-dword sort record), `sceneryObjAlloc` `0x430200`, `musicEmitterAlloc`
  `0x431b00`, `walkAnimTableEntryCalc` `0x433980`, `sceneTextAnimAdd/Close`
  (`0x434a90`/`0x434bf0`, 16 slots), `playerAnimOrientFromDir` `0x4336b0`,
  `scenExpandNameList`, `gxSnooze` (WM_ACTIVATEAPP), `gxLoadTexture`
  (mode 0=load/1=free).
- **Player-AI (9):** `aiControllersInit` `0x401040`, `aiControllerCtor`
  `0x401090`, `playerUpdateDispatch` `0x4010e0`, `playerAiUpdate` `0x401160`
  (states 0-9), `syncAiAnimToSceneObj` `0x4015a0`, `aiPathfindToTarget`
  `0x401800` (nav-waypoint steering; ret 0/1/2), `zoneAvoidWalls` `0x4023e0`,
  `objTurretListFree(2)`.
- **Obj/item AI (9):** `objCollideCheck`, `objTurretAdd`/`objTurretSetValue`
  (turret node 0x1c B: +0 type, +4 angle, +0xc id), `objShotAdd` (16-arg mirror
  of `objShotCtor`), `syncCartNodeChannelsToWalkPos`, `playerFindCart`,
  `aiStateCartApproach`, `playerCheckTurn`, `playerAiGrabItem` (CLASS-0x1f
  bonus / MCDMAN burger).
- **Item pickup/throw (9):** `playerCollectItem`, `aiCollectItem`,
  `playerCheckTargetRange`, `aiCheckItemRange`, `objGetCheckoutPos`,
  `playerThrowItemCtor` (0x48 B, cap 10, list head DAT_0045896c),
  `itemThrowUpdate`, `navPointGetLinkList`, `navPointRemoveLink`.
- **Nav/aiNav (9):** `navPointRemove`, `navPointGetNext`, `navPointRelaxCosts`
  (Dijkstra), `navPointPickCheapestLink`, `aiNavEdgeCtor` (0x3c B),
  `aiNavNodeCtor` (0x48 B), `aiNavNodeUpdate`, `aiNavNodeAddEdge`,
  `zoneConnLink`.
- **AI states + scene ray/zone/detail (11):** `aiStateTurnToBlocked` `0x40ef60`,
  `aiStateGrabObject` `0x401eb0` (state 3), `aiStatePutObjectInCart`
  `0x401fb0` (state 4), `aiStateReturnHome` `0x402060` (state 7, L3 +8000 /
  L4 +2000 offsets), `sceneRayFindSorted` `0x42a7c0`, `zoneWallPointSide`
  `0x42a870`, `zoneWallCircleHit` `0x42aac0`, `sceneDetailGridCtor` `0x42ad00`
  (detail/culling grid, `_`-prefixed node names), `sceneDetailGridSetRoot`
  `0x42b350`, `zoneConnCtor` `0x42b410` (AR/IN conn, list DAT_0045e5e8),
  `sceneMeshBBox` `0x42ba40` (AABB of indexed tri/quad prims).

### Nav-node structs (2026-08-29)

`AiNavNode` (0x48 B) and `AiNavEdge` (0x3c B) declared from the
`aiNavNodeCtor`/`aiNavNodeUpdate`/`aiNavNodeAddEdge`/`aiNavEdgeCtor` cluster
cross-checked against `zoneWallCalcPlane`, `zoneAvoidWalls`, `zoneConnLink` and
`sceneRayFindNearest`. Node: `flAvgY` +0 (vertical axis; walkable plane test
`y = (x-nRefX)*flSlopeX + (z-nRefZ)*flSlopeZ + nRefY`, plane ref +0x0c..+0x14,
normal +0x18..+0x20 with Y >= 0.6 walkable), scene pos ints +0x2a..+0x34
(unaligned by design), conn/edge list heads +0x38/+0x3c, `pNext`/`pPrev`
+0x40/+0x44. Prototypes applied to all five aiNav functions and
`g_pNavNodeList` @0x45e5e0 retyped `AiNavNode *`. Ghidra forbids retyping the
thiscall auto-parameter, so `this` was replaced with an explicit ECX-storage
parameter via a script (`~/ghidra_scripts/RetypeNavThis.java`,
`updateFunction(..., CUSTOM_STORAGE, ...)`); the decompiler now shows
`AiNavNode *this` and field access (`this->pEdgeList`, `g_pNavNodeList->pPrev`).
Mirrored in `src/zone.h` (packed typedefs); `stubs.c`/`level.c` updated.

### Nav globals (2026-08-29)

The `g_pNav*` globals are now typed, and the nav-mesh data structures they
point into are declared in both Ghidra and `src/scene.h`:

- `SceneMeshPrim` (new): one indexed-primitive record in the
  `SceneObjRenderInfo.pPolyA` array — `{bFans +0, bType +1 (1=points,
  2=lines, 3=tri fans, 4=quad list), wFlags +2, bFanIdxCount +6}` header,
  then `bFans * bFanIdxCount` shorts per fan; the first `bType` bytes of
  each fan are vertex indices (nav/renderer read only their low bytes,
  e.g. `meshDrawPoly` @0x42e940 and `aiNavNodeUpdate` both step
  `bFanIdxCount*2` bytes). Layout verified against PHWOODMALL.SEN: FLOOR
  prims live in the SUBO chunk and the in-file `pPolyA` entries are
  offsets from `g_pSubObjData` (sceneMeshFixup relocates them against
  `*(0x45eb30)`, or pMesh when SUBO is absent).
- `SceneObjRenderInfo` (0x30 B) fixed: `pVerts` +8 is `short *`
  (short[4] stride 8), `pPolyA` +0x18 / `pPolyB` +0x2c are
  `SceneMeshPrim **`, `pGroups` +0x20 is `SceneGroupInfo *` (new, 0xc B).
  `g_pNavMeshData` @0x45e5d4 retyped `SceneObjRenderInfo *`.
- `SceneObjTypeDef` (0x34 B) fixed: `pA/pB/pTex/pC/pD` typed pointers,
  `pRender` +0x14 `SceneObjRenderInfo *`;
  `sceneNodeGetMesh` @0x431ae0 now returns `SceneObjTypeDef *` so
  `aiNavNodeCtorScene` decompiles as
  `g_pNavMeshData = sceneNodeGetMesh(pSceneObj)->pRender`.
- `g_pNavTriCur` @0x45e5d0 / `g_pNavTriEnd` @0x45e5d8 retyped `byte *`
  (fan-stream cursor/end); `g_nNavTriIdx` @0x45e5cc stays `int`.
- Nav-point list: `g_pNavPointHead/Tail` @0x45d4d0/4 and
  `g_pNavPointSel` @0x45e480 retyped `NavPoint *`; `NavPoint.pNext`
  +0x5c is now `NavPoint *` (singly linked head/tail list —
  `navPointListAdd`/`navPointRemove`/`navPointListFreeAll` all show
  typed field access). `NavPoint` (0x60 B) mirrored into `src/zone.h`
  together with the nav global externs for the milestone-3 `.ai`
  loader (`nloadCmd` @0x407e10).

## Limitations

The automated pass is exhausted. **Final census 213 = 65 network + 148 Unwind
stubs; no game functions remain undefined.** The 65 network entries are a
cohesive subsystem (Winsock message queue, UDP server/client, lobby states,
`n*Cmd` handlers) deferred to a dedicated network pass, not part of the game
function drive-down.

The global pass deliberately treats labels inside typed arrays and overlapping
regions as aliases — independently retyping the player-record, scene-text, and
sound-bank sub-labels would corrupt the established layouts.

## Next direction

The undefined-type function drive-down for game code is **done**. Remaining
census entries are exclusively network (`mnet*`, `net*`, `stateNet*`,
`startServer`, `netMsgQueue*`, `n*Cmd`) and MSVC/CRT (`Unwind@0044xxxx`) code,
both out of scope per project conventions. If a future network pass is
undertaken, it should start from the 65 entries listed in the census output;
the command-dispatch table at `0x44b308` already maps the `n*Cmd` handlers.

Semantic refinement of already-typed signatures can continue opportunistically,
but there is no remaining undefined-type backlog to drive down.

Census history (2026-08-20): 724 → 526 (prototype pass) → 441 (config/mString)
→ 423 (vector/math/obj/scene) → 412 (animation) → 404 (scene name-table) → 381
(menu/flow) → 374 (player-record/init) → 348 (console) → 344 (helpers A) → 327
(helpers B) → 257 (scene/GX/sound/music/mixer/dsound/scene-node) → 213
(renderer/mesh, player-AI, obj/item AI, item pickup/throw, nav/aiNav, AI
states, scene ray/zone/detail). Final breakdown: 65 network + 148 Unwind stubs
= 0 game functions. The 137 undefined global labels are switch/jump tables,
the console dispatch table, driver/API vtable pointers, and
`g_crtFscanfCore` — all deliberately left alone.
