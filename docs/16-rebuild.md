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
  and file-helper interfaces used by the menu and font code. The pool keeps
  ownership records so live allocations are reclaimed by pool destruction and
  cross-pool frees are rejected; the original slab hierarchy and invalid-handle
  behavior remain deferred.
- **Fonts:** `src/font.c` implements the original font/text range
  `0x408f90–0x409940`: descriptor parsing, atlas UV setup, tagged text
  rendering, centering, and integer formatting, including the two-font menu
  rows. The original static utility `fmtParseInt` (`0x43e860`) is represented
  by the CRT-compatible `strtol` substitution; `strFindSubstring` is likewise
  represented by `strstr`.
- **Stubs and helpers:** `src/stubs.c` contains deferred original functions;
  rebuild-only support code is isolated in `src/custom_helpers.c`.

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

The font comparison is complete: direct function control flow and the
`gxDrawPolygon` glyph contract are implemented inside the original
`textDraw`/`textDrawInt`/`textIntWidth` boundaries. The only known font
fidelity limitation is malformed-input behavior in the deferred static numeric
parser substitution.
The GX adapter now separates driver loading from `gxInit`/mode setup, and the
pool/file review corrected pool initialization return semantics and the
file-seek handling for all nonzero end-relative modes. The GX polygon adapter
now preserves the original `nSoftwareMode == 1` branch from
`gxDrawPolygon @0x433440`: it applies the original X/Y float scales and
truncating conversion only when that flag is set. The active GXSOFT path leaves
the flag at zero, so `textDraw`'s 8.8 fixed-point vertices remain unchanged for
GXSOFT; the packed color/UV record remains identical to the original wrapper.
