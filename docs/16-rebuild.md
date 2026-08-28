# Mall Maniacs (maniac.exe) — 16. Rebuild (maniac_rebuild.exe)

[Back to README](README.md)

## Current status

The rebuild provides the original offline GUI flow through `DRIVERS\\GXSOFT.DLL`: the six-logo introduction leads to the five-row main menu, game-type selection, character selection, and map selection. Keyboard navigation, row dispatch, quit confirmation, clean shutdown, menu sound effects, the animated, textured, lit 3D character preview, and the five-entry map carousel are functional.

The character-selection scene uses the original `SEN`/`TPG` asset pipeline and scene-graph, animation, projection, material, and polygon-rendering paths. All ten character models can be selected and previewed; pressing Enter enters `stateLevelSelect`, where locked/unlocked map previews, five-pixel `sinf` arrow wobble, arrow navigation, Escape return, and the original five level-entry contracts are implemented.

## Scope and build

The target is `/home/wasd/MallManiacsUnmodified/maniac_rebuild.exe`, built for 32-bit Windows with `i686-w64-mingw32-gcc` and the required `KERNEL32`, `USER32`, `GDI32`, and `WINMM` libraries. Rendering uses the original software rasterizer through `GXSOFT.DLL`; a GDI backend is not used.

The implementation covers the offline GUI and the available single-player character-selection path while preserving the original call hierarchy. Reimplemented functions and globals retain original-address comments in source. Detailed reverse-engineering notes belong in subsystem documentation.

## Implemented subsystems

- **Application and menu:** window/frame-loop handling, GX initialization, intro timeline, main menu, game-type selection, mode/player policy, character selection, map selection, quit confirmation, and keyboard-driven navigation.
- **Character preview:** scene initialization and rendering, camera transforms, node hierarchy, mesh/material binding, polygon sorting and drawing, texture loading, character cycling, and animation playback.
- **Map selection:** `stateLevelSelect @0x41b900`, level-strip and section textures, lock checks, animated arrow rendering, five level-entry targets, and Escape return to character selection.
- **Assets and animation:** `SEN` `REV2` chunk loading, `MESH`/`MAPI`/`TNAM`/`COLS`/`SUBO` data, `TPG` surfaces, and the `.anm` loader and playback cluster.
- **Audio:** menu sound-bank loading, DirectSound streaming and voice mixing, positional volume, priority culling, and per-frame mixing.
- **Foundation:** pool allocation, file helpers, font parsing/rendering, tagged text, and integer formatting used by the GUI.
- **Deferred contracts:** unresolved original dependencies have documented safe stubs in `src/stubs.c` and `src/stubs.h`; rebuild-only helpers are isolated in `src/custom_helpers.c`.

## Deferred scope

Network features, the in-game console, DirectInput polling, DirectSound music paths, MCI CD audio, registry-based driver selection, and the remaining network/options/high-score menu targets are not implemented. Their current behavior is intentionally limited to documented stubs or returning to the menu; they do not claim functionality that is absent.
