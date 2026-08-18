# Mall Maniacs (maniac.exe) — 08. Globals / managers map

[Back to README](README.md)

Status:

The global-data pass is complete enough to support the documented boot, state,
renderer, config, and gameplay analyses. `605` globals have meaningful names and
`571` have refined types; the remaining `DAT_*` names are documented aliases or
artifacts, concentrated in the `g_playerRecords` region. This page is a concise
cross-subsystem map, not an exhaustive symbol inventory.

## Purpose and evidence

The globals connect the original call hierarchy: application initialization
creates the engine and driver state, the frame loop dispatches the current game
state, and menu/gameplay transitions update shared level and player data. Roles
below were established from decompilation and xrefs; detailed layouts and
algorithms belong in [02-boot.md](02-boot.md), [03-gameflow.md](03-gameflow.md),
[04-renderer.md](04-renderer.md), [06-config.md](06-config.md), and
[07-gameplay.md](07-gameplay.md).

## Verified global groups

- **Application and lifecycle:** the engine instance at `0x455e60` is created by
  `FUN_00401000`; the window/input handles occupy the `0x459cd0` area. The
  driver selection at `0x4580c4` is `1` for Glide and `2` for software. The
  booted/menu, gameplay-loop, quit, and running flags at `0x45a658`, `0x4580f8`,
  `0x4580f4`, and `0x459cd4` gate the two main-loop paths.
- **State machine:** `g_pStateFunc` at `0x45a6f8` is the typed current-state
  dispatcher, called for frame updates and key events. It has more than forty
  transition writers and is the central shared state variable for the menu and
  single-player flow.
- **Deferred transitions and results:** `0x45a710` is a deferred state/action
  slot set by `requestCmd`, consumed by `menuInit` on menu re-entry, and cleared
  by `unloadGameWorld` at `0x41a684`. The result record at
  `0x45a700..0x45a70c` carries round-end level/rank data into the replay-level,
  high-score, and end-scene paths.
- **Configuration and renderer:** `g_pConfigEnv` at `0x455d38` owns the parsed
  configuration tree; the `driver` value selects the GX driver during
  `gameInit`. `GxDriverApi` at `0x45eb40` and its adjacent software/module/active
  fields hold the loaded renderer interface.
- **Game selection and players:** level, local-player, and player-count globals
  are at `0x458100`, `0x458104`, and `0x458108`; game mode is
  `g_nGameMode` at `0x458120`. `g_apPlayers` at `0x456360` is a typed
  `Player[8]` array. The camera is at `0x4588f8`, and the `characters.sen`
  resource handle is at `0x4580b4`.
- **Timing and presentation:** the animation/intro accumulators at
  `0x45d440`/`0x45d444` feed timeline updates and other time-based presentation
  behavior.

## Current limitations and next direction

The map does not by itself prove every field's ownership or lifetime, and old
`DAT_*` spellings should not be treated as unresolved functionality when a
renamed alias is documented elsewhere. Remaining data-surface cleanup is mainly
the player-record aliases in `0x456210..0x457db0`; avoid duplicating that work in
this overview.

For the rebuild, use these globals as the shared-state checklist while keeping
Ghidra types, function signatures, and source declarations synchronized. The
next useful slice is one real menu target—preferably `stateGameTypeSelect`
(`0x41c010`) or `stateOptions` (`0x41c6a0`)—before wiring deeper gameplay state.
