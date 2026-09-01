# Mall Maniacs (maniac.exe) — 16. Rebuild (maniac_rebuild.exe)

[Back to README](README.md)

## Current status

The rebuild provides the offline GUI flow through `DRIVERS\GXSOFT.DLL`: six-logo intro → five-row main menu → game-type, character, and map selection with keyboard navigation, quit confirmation, shutdown, menu sfx, animated textured/lit 3D character preview, and five-entry map carousel.

Selecting a map parses `run <level>` and `roundStartInit @0x40a4d0` executes its two-pass init (GX re-init, pool, `scenNameTableInit`, `levelSceneTexturesLoad @0x4104b0`, second `scenNameTableInit`, `configMasterLoad`, `levelSetup @0x4108a0` with `SEN`/`TPG`/`ph`-scene loading and `scenNameToId @0x431ed0` mesh resolution). The mall, player+cart and AI (`playerSetupCharacters @0x41b6e0`, `levelObjectsCartsCameraInit @0x411b70` with floor raycast `sceneRayFindNearest @0x42a750` / `zoneWall*`) render with the config-driven follow camera, and `renderGameHud @0x412810` draws the shopping-list, `Varor` counter, timer, checkout bar, face, rank, results/quit prompts and `Klara!/Färdiga!/Gå!!` countdown. Round clock (`roundLogicUpdate @0x40beb0`, `levelDirectorInits @0x40bdf0`, limit 7000 ms / 175×40 ms ticks) drives `g_nGamePhase @0x458124`; quest bank (`questLoad @0x40ffa0`, 430 records), AI movement/pickup (`playerUpdateAI @0x40b510`), item targeting and cart/goods/item-slot pointers (`gameObjectUpdate @0x40cf40`), thrown-item/shot collision (`objShotCollide @0x4035e0`, `objCollideCheck @0x404ac0`), player physics (`gameUpdate @0x426ee0` cluster), in-game input (`gameKeyHandler @0x40db80`), SndEmitter 3D sfx (`sndPlaySfx3D @0x42bcd0`), and `levelEventDirector_L0 @0x416fd0` are live.

The original AI decision machine is live: `playerUpdateDispatch @0x4010e0` (already live) now drives `playerAiUpdate @0x401160` (was a stub) — the SETTARGETITEM @0x4015c0 → GOTOITEM (aiPathfindToTarget @0x401800) → GRABOBJECT @0x401eb0 → PUTOBJECTINCART @0x401fb0 → RELEASECART / RETURNHOME @0x402060 cycle, with `aiSteerToTarget @0x401ae0` writing the input-impulse channels and the live NavBuoy Dijkstra cluster (`navPointFindNearestInYRange @0x4259d0` + `navPointPickCheapestLink @0x425bd0`) as its pathfinder. Pickup meshes come from `g_apLevelItemSlots[id-1].nMeshId` (the `items[%d]/mesh` scenNameToId pointer written by levelSetup); the suspected separate grabb-mesh table @0x4583b8 is that same slot storage.

## Scope and build

Target is `/home/wasd/MallManiacsUnmodified/maniac_rebuild.exe`, built 32-bit Windows with `i686-w64-mingw32-gcc` and `KERNEL32`, `USER32`, `GDI32`, `WINMM`. Rendering uses the original software rasterizer via `GXSOFT.DLL`; no GDI backend. The implementation covers the offline GUI and single-player path while preserving the original call hierarchy. Reimplemented functions/globals carry original-address comments; detailed notes belong in subsystem docs.

## Implemented subsystems

- **Application and menu:** window/frame loop, GX init, intro timeline, main/type/character/map/quit menus and keyboard routing.
- **Character preview:** scene-graph, camera, node/mesh/material binding, polygon sort/draw, `TPG` textures, animation playback.
- **Map selection:** `stateLevelSelect @0x41b900`, strip/section textures, lock checks, arrow wobble/nav, level-entry dispatch.
- **Gameplay entry:** `runCmd @0x4084c0`, two-pass `roundStartInit @0x40a4d0` (`levelSceneTexturesLoad @0x4104b0`, `levelSetup @0x4108a0`), `gameWorldUpdate @0x40b3d0`, `gameObjectUpdate @0x40cf40` (zone-aware item targets, arrows, and 30-slot visibility/bob pass), `gameFrameRender @0x40ae30` / `gameRunFrame @0x40ad80` fixed-step handoff.
- **AI movement/pickup and combat:** `playerUpdateAI @0x40b510` helper set, `actionCmd @0x4067c0` / `commandDispatch`, thrown-item (`playerThrowItemCtor`/`itemThrowUpdate`/`itemMeshFollowUpdate`) and shot collision (`objShotCollide @0x4035e0`, `objCollideCheck`, `objWalkAnimSync @0x409b10`).
- **Player physics:** `gameUpdate @0x426ee0`, `playerUpdateWalkPhysics @0x426fd0` / `OnFoot @0x427730` / `CartPhysics @0x4280b0`, mesh syncs (`@0x428840..@0x428a70`), `zoneAvoidWalls @0x4023e0`, `playerAnimSfxUpdate @0x40c800`.
- **Level world:** `sceneLoadSen @0x432320`, `sceneNodeRender @0x42f8c0` / `sceneRender @0x42f1c0`, `sceneRayFindNearest @0x42a750`, EventObject registry (`eloadCmd @0x406cb0`), `zoneConnCtor @0x42b410`, detail grid (`sceneDetailGridCtor @0x42ad00`).
- **AI nav:** `AiNavNode` FLOOR pipeline (`aiNavNodeCtorScene @0x428cf0` → `aiNavNodeUpdate @0x428d50` → `zoneWallListBuild @0x42a650`), `nloadCmd @0x407e10` NavBuoy point/link graph and Dijkstra cluster (`navPointRelaxCosts @0x425af0` → `navPointPickCheapestLink @0x425bd0`, `navPointFindNearestToXY @0x425c90`, `navPointFindNearestInYRange @0x4259d0`, `navPointPickRandom @0x425aa0`, `navPointResetAllFlags @0x425c70` plus `navPointGetNext`/`GetLinkList`/`RemoveLink`/`Remove`). Consumer live since `playerAiUpdate @0x401160` landed.
- **Player round setup:** `playerSetupCharacters @0x41b6e0`, `playerSetupRound @0x410e90`, `playerSetupSceneObjects @0x411550`, `levelObjectsCartsCameraInit @0x411b70`, `cameraFollowUpdate @0x4020d0`.
- **HUD:** `hudLoadGraphics @0x412700`, HUD fonts (`g_hHudFont* @0x458364..6c`), `renderGameHud @0x412810`, `textDrawWrappedCentered @0x4101d0`.
- **Config/assets:** XOR-0x55 `maniac.cfg`→`sommar.sol` (`configParseFile @0x435890`, `configMasterLoad @0x410350`, `configEnvFind @0x4365c0`), `SEN` `REV2` / `TPG` / `.anm` loaders, `g_apLevelItemSlots @0x4583c8`, quest records (`questLoad @0x40ffa0`).
- **Round logic/audio/foundation:** `roundLogicUpdate @0x40beb0`, `levelDirectorInits @0x40bdf0`, `walkAnimTableEntryCalc @0x433980`, sound bank/DirectSound/3D emitters, pool/file/font/tagged-text helpers. Unresolved deps are stubbed in `src/stubs.c`/`stubs.h`; rebuild helpers in `src/custom_helpers.c`.

## Deferred scope

Zone data, `AR`/`IN` pairing, detail grid, FLOOR and NavBuoy loading plus Dijkstra pathfinding (`eload`/`nload`, 97 nav nodes verified on woodshop; `navPointRelaxCosts`/`PickCheapestLink`/`FindNearest*`/`PickRandom`/`ResetAllFlags` live) are live. Remaining gaps: per-level directors `levelEventDirector_L1..L4` and `roundTeardown @0x40aa10` / full `unloadGameWorld` resource-freeing tail; gameplay quest/sound directors.

`levelEventDirector_L1..L4` + `_Init`/`_Cleanup` is temporarily out of scope as we focus on the first level, L0.

## Next steps

1. `roundTeardown @0x40aa10` / `unloadGameWorld` resource-freeing tail.
2. Gameplay quest and sound directors.

Exercise each increment via character select → level select → gameplay keyboard route (`XDOTOOL_NAVIGATION.md`) with black-frame fallback and tracker re-run.


