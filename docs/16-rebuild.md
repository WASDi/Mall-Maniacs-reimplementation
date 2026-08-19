# Mall Maniacs (maniac.exe) — 16. Rebuild (maniac_rebuild.exe)

[Back to README](README.md)

Status:

The rebuild now runs the original offline GUI path through
`DRIVERS\GXSOFT.DLL`: the six-logo introduction transitions to the five-row
main menu, which supports keyboard navigation, row dispatch, quit confirmation,
and clean shutdown. The next work is a real row-target screen.

## Goal and scope

Produce `/home/wasd/MallManiacsUnmodified/maniac_rebuild.exe` from a compilable
source project, preserving the original call hierarchy and providing offline
GUI and single-player functionality.

The rebuild targets 32-bit Windows with `i686-w64-mingw32-gcc`, uses only the
required `KERNEL32`, `USER32`, `GDI32`, and `WINMM` libraries, and uses the
original software rasterizer rather than a GDI backend. Network, console,
DirectInput, DirectSound, and other out-of-scope systems remain deferred.

Reimplemented functions and globals retain their original addresses in source
comments, and their source signatures use the compiler's default calling
convention. Detailed reverse-engineering notes belong in subsystem
documentation, not in this progress overview.

## Current implementation

- **Application loop:** `src/maniac.c` creates the 640x480 window, initializes
  GX, dispatches frame and keyboard events, and performs the 25 ms frame gate.
- **Menu flow:** `src/menu.c` implements `menuInit` (`0x419c20`), the intro
  timeline (`introUpdate`, `0x41ae50`), the main menu (`menuUpdate`,
  `0x41b0b0`), and quit confirmation (`stateQuitConfirm`, `0x4200b0`).
- **Rendering:** `src/gx.c` adapts the maniac-side GX wrappers to
  `GXSOFT.DLL`; indexed TGA assets and the `MERGED00.TPG` palette render at
  the original 640x480 resolution.
- **Foundation:** `src/pool.c` and `src/util.c` provide the reconstructed pool
  and file-helper interfaces used by the menu and font code. The pool now
  preserves the original 64-subpool × 64-chunk × 16-slot hierarchy, size-marked
  records, pool-wide search/free, and finite-capacity behavior; invalid-handle
  failure behavior remains intentionally non-defensive like the original.
- **Fonts:** `src/font.c` implements the original font/text range
  `0x408f90–0x409940`: descriptor parsing, atlas UV setup, tagged text
  rendering, centering, and integer formatting, including the two-font menu
  rows. The original static utility `fmtParseInt` (`0x43e860`) is represented
  by the CRT-compatible `strtol` substitution; `strFindSubstring` is likewise
  represented by `strstr`. A standalone visualizer `tests/render_font.c`
  reuses `fontLoad`/`fontParse` and decodes the paired `.tpg` directly,
  rendering all 256 glyphs to a BMP; it generated the `tests/font_*.bmp`
  set for the menu and per-scene hud fonts.
- **Stubs and helpers:** `src/stubs.c` contains deferred original functions;
  rebuild-only support code is isolated in `src/custom_helpers.c`. The three
  void stubs (`gameInit @0x409d90`, `gameFrameUpdate @0x41a8c0`, and
  `pollKeyboard @0x416a10`) use `void(void)` with the default C convention and
  are logged no-ops with no false success return. Ghidra's
  `pollKeyboard` body actually polls DirectInput, debounces key IDs, dispatches
  `(key, 2)`, clears the quit flag, and may post `WM_CLOSE`; those effects are
  intentionally absent because the slice uses queued window messages. The
  four menu row entries remain deliberate `int(int, int, int)`
  replacement stubs: they log once, return `0`, and route back to `menuUpdate`;
  their original rendering/gameplay/network logic remains deferred.

## Verified behavior

`./build.sh` completes cleanly with `-Wall -Wextra` and produces
`maniac_rebuild.exe`. Wine verification confirms:

1. The GX driver initializes and installs the palette from `MERGED00.TPG`.
2. All six intro logos play in the original order and timing, then enter the
   main menu; only the Space/fire key skips the intro, while unmapped window
   keys are ignored.
3. Up/Down navigation wraps across `Spela`, `Nätverk`, `Alternativ`, `Rekord`,
   and `Avsluta`; Enter dispatches the selected row.
4. Escape opens quit confirmation. Enter selects `Avsluta` without immediately
   cancelling the newly displayed screen; Escape returns to the menu, while
   the original J/Y character confirmations exit cleanly.

The first four row targets currently log a TODO message and return to the main
menu. This is intentional until those states are reconstructed.

The stub contract review also confirmed that `gotoOptions @0x41d300` is an
intentional replacement, not an incomplete copy of the original: the original
copies `g_nGfxMode @0x4580c4` to `g_nRendererMode @0x45a390` and enters
`stateOptions @0x41c6a0`, while the rebuild keeps both effects deferred and
returns safely to `menuUpdate`.

## Next milestone

Implement one real row-target state, preferably the game-type select
(`stateGameTypeSelect`, `0x41c010`) or the options screen reached through
`gotoOptions` (`stateOptions`, `0x41c6a0`). Reuse the existing menu assets and
text renderer, then add the real input path (`pollKeyboard`, `0x416a10`) when
mouse/controller support is required. Gameplay remains after the GUI states.

## Progress tracking

`TrackRebuildProgress.java` compares address annotations in the rebuild source
with the in-scope functions in Ghidra and writes `rebuild-progress.txt`. Re-run
it when an implementation chunk changes; the report may otherwise be stale.

Continue to follow `Rebuild.md` for constraints and update this overview,
Ghidra, and relevant subsystem documentation after each completed chunk.

## Fidelity and limitations

- Source audits for the application loop, menu, GX, pool, utility, font, and
  stub code are complete. Reimplemented symbols retain original-address
  comments and match the verified Ghidra parameter and return types; only the
  required Win32 entry/callback declarations use explicit ABI markers.
- The GXSOFT path preserves the original wrapper hierarchy, polygon conversion
  branch, packed UV data, font rendering flow, file-helper behavior, and
  64-subpool × 64-chunk × 16-slot pool capacity. The CRT-compatible
  `strtol`/`strstr` substitutions and explicit vertex initialization are the
  only known low-level deviations in the implemented slice.
- Deferred behavior includes DirectInput polling, audio, scene/gameplay,
  networking, registry-based driver selection, and real menu row targets.
  `gotoOptions` and the first four row handlers intentionally return to the
  menu instead of claiming those states are implemented.
