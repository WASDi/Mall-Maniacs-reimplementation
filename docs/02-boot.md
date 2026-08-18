# Mall Maniacs (maniac.exe) — 02. Entry / init / main loop

[Back to README](README.md)

Status: The original executable's entry, initialization, state/menu loop, and
input routing are verified from decompilation, xrefs, imports, and asset/string
evidence.

## Purpose and scope

This document records the boot contract used by the rebuild. Detailed
state-machine, renderer, configuration, and DirectInput notes remain in
[03-gameflow.md](03-gameflow.md), [04-renderer.md](04-renderer.md),
[06-config.md](06-config.md), and [12-input.md](12-input.md).

## Verified boot contract

- **Entry and initialization:** `entry` (`0x43f18e`) transfers to `WinMain`
  (`0x4160a0`), which creates the Mall Maniacs window and runs the message
  loop. `gameInit` (`0x409d90`) loads the XOR-`0x55`-obfuscated
  `sommar.sol` save and `maniac.cfg` configuration, selects `GXGLIDE.DLL`
  or the `GXSOFT.DLL` fallback, and initializes the 640x480, 16-bit GX
  renderer and engine managers.
- **Frame dispatch:** The idle message-loop path calls `gameFrameUpdate`
  (`0x41a8c0`) while no game world is active and `gameRunFrame`
  (`0x40ad80`) during gameplay. The former advances time, polls keyboard
  input, invokes the current state through `g_pStateFunc` (`0x45a6f8`), and
  presents the frame; the latter bypasses the menu/state loop for gameplay.
- **Menu handoff:** `menuInit` (`0x419c20`) performs the first-frame menu
  asset setup, starts the intro or configured top-level state, and is reused
  after a world unload. This re-entry also consumes deferred actions queued by
  the `request` command. The state machine and transitions are documented in
  [03-gameflow.md](03-gameflow.md).
- **Input routing:** `WindowProc` (`0x4161b0`) sends character and key events
  to either `dispatchKeyEvent` (`0x41ade0`) for menu states or
  `gameKeyHandler` during gameplay. DirectInput keyboard polling uses the
  original key mapping and debounce behavior; the device setup and polling
  details are in [12-input.md](12-input.md).

## Evidence and limitations

The flow above is verified from decompilation, cross-references, imported API
usage, executable strings, and the observed asset names. It describes the
original binary; the rebuild currently reproduces the offline GX software
GUI through the main menu, while DirectInput, gameplay, networking, console,
and other deferred systems remain outside its implemented scope. Renderer and
configuration details are kept in [04-renderer.md](04-renderer.md) and
[06-config.md](06-config.md).

## Next direction

Implement one real main-menu row target—preferably game-type selection or the
options screen—then add the corresponding input path as needed. Continue from
the current rebuild milestone in [16-rebuild.md](16-rebuild.md); gameplay
remains after the GUI states.
