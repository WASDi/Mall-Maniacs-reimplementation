# Mall Maniacs (maniac.exe) — 03. Game state machine / flow

[Back to README](README.md)

Status:

The original game-flow state machine is fully mapped and documented at the
behavioral level. The verified offline path is intro → main menu → game-type
selection → character selection → level selection → `run <level>`; gameplay
returns through results, deferred menu actions, high scores, and end scenes.
The rebuild currently implements only the intro, main menu, and quit-confirm
states; the first real row-target state is the next useful slice.

## Purpose and evidence

`DAT_0045a6f8` is the current-state function pointer, renamed
`g_pStateFunc` at `0x0045a6f8`. State functions receive `(type, a, b)`;
`type == 0` is a frame update and `type == 1` is a key event. More than 40
writers establish transitions, and `dispatchKeyEvent` (`0x0041ade0`) forwards
keyboard events to the active state. The flow below is based on decompilation,
cross-references, strings, and observed transition tables; addresses identify
the original binary functions.

## Current-state flow

- **Startup and menu:** `introUpdate` (`0x0041ae50`) advances the six-logo
timeline and allows a key to skip to `menuUpdate` (`0x0041b0b0`). The five
rows are `Spela`, `Nätverk`, `Alternativ`, `Rekord`, and `Avsluta`; selection
wraps, Enter dispatches the selected target, and Escape opens quit confirm
(`0x004200b0`).
- **Offline game setup:** `stateGameTypeSelect` (`0x0041c010`) cycles the four
modes and selects a mode initializer. Those initializers set
`g_nGameMode` (`0x00458120`) and player-count policy before
`stateCharacterSelect` (`0x0041efa0`) and `stateLevelSelect` (`0x0041b900`).
The five level initializers set `g_nLevelIdx` (`0x00458100`), prepare player
data and the world, then dispatch `run 0` … `run 4` to enter gameplay.
- **Options and records:** `gotoOptions` (`0x0041d300`) enters
`stateOptions` (`0x0041c6a0`), which exposes difficulty and renderer choices.
`stateHighScoreTable` (`0x0041dfd0`) is also reachable directly from the
menu; `gameOverLoadHighScores` (`0x0041dcb0`) and
`stateHighScoreEntry` (`0x0041d320`) handle new records. The shared `fshi` /
`vahi` tables use keys such as `fshi2dtime0` and persist through the config
command layer.
- **Deferred transitions:** `requestCmd` (`0x0041a730`) stores actions in the
deferred-action slot at `0x0045a710`. Verified actions are high-score entry,
Varujakten score display, replaying a level, and the end scene. `menuInit`
(`0x00419c20`) executes a pending action after world teardown, making this
the return path from results to setup, records, or the menu.
- **Network:** `stateNetworkMenu` (`0x00420190`) branches to host setup/lobby or
client setup/connect/lobby, then `stateStartGame` (`0x00422840`) copies lobby
selections into player records and dispatches `run/%d`. This path is mapped
but remains outside the rebuild scope.

## Gameplay boundary and verified behavior

`runCmd` (`0x004084c0`) rejects a running game, calls scene setup
(`0x0040a4d0`), sets the running flags, and starts `roundStart` (`0x0040bdf0`).
Each frame follows `gameRunFrame` (`0x0040ad80`) → `gameWorldUpdate`
(`0x0040b3d0`) → per-player updates → `gameFrameRender` (`0x0040ae30`) and
`renderGameHud` (`0x00412810`). `roundLogicUpdate` (`0x0040beb0`) tracks the
timer, mode-specific win conditions, item spawning, and the network-ready
handshake. A win sets the results screen and winner; `gameKeyHandler`
(`0x0040db80`) either kills the round, requests the next level, or requests
the end scene. `killCmd` (`0x00407870`) performs the full world teardown and
returns to the state loop.

The HUD evidence confirms mode-specific score/time displays, inventory and
rank indicators, quit prompts, phase banners, and scroll text. The end-scene
path (`endScene`, `0x00424ef0`, then `stateEndSceneShow`, `0x00425190`) loads
the ending scene and animation set before returning to the menu.

## Limitations and next direction

This document describes the original binary, not a claim that every state is
implemented in the rebuild. Network, console-driven entry points, detailed
level directors, and gameplay rendering remain reconstructed evidence rather
than rebuild functionality. The confirmed dead-option use of the zeroed buffer
at `0x004550d8` is not a real state function.

Implement one real offline row target next—preferably
`stateGameTypeSelect` (`0x0041c010`) or `stateOptions` (`0x0041c6a0`)—using the
existing menu assets and text renderer. Then add character/level selection and
the gameplay boundary incrementally; keep detailed subsystem notes in their
dedicated documents and update `docs/16-rebuild.md` with each completed slice.
