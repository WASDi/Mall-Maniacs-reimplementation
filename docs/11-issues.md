# Mall Maniacs (maniac.exe) — 11. Unknowns / current state

[Back to README](README.md)

Status:

Static analysis is no longer blocked by unnamed functions or broad subsystem
unknowns. The remaining reverse-engineering work is semantic refinement and
source reconstruction; the active implementation status is tracked in
[16-rebuild.md](16-rebuild.md).

## Purpose and scope

This page records unresolved questions, evidence limits, and the next useful
analysis direction. Detailed subsystem findings belong in the linked documents,
and rebuild implementation details belong in [16-rebuild.md](16-rebuild.md).

## Current analysis state

- All 1178 functions in `maniac.exe` are documented and named. Game flow,
  networking, configuration, rendering, sound, file formats, input, and the
  statically linked MSVC CRT region are mapped at the subsystem level.
- The data surface is semantically named: the final naming pass renamed 605
  globals and typed 571. Remaining `DAT_*` labels are documented protected
  aliases, string-interior or padding items, below-image-base operand artifacts,
  or two known bogus items—not unidentified logical globals.
- Major structures and behaviors are cross-referenced in the subsystem docs,
  including the scene/player record layout, level-event directors, config
  serialization, `.sen`/`.tpg`/`.ai`/`.eo` handling, the software sound mixer,
  and the network lobby protocol.

## Open questions and limitations

- `g_nScoreTableTick` at `0x45d43c` is still marked `[HYPOTHESIS]` as the
  per-frame counter used by `stateHighScoreTable` at `0x41dfd0`; verify it on a
  later decompilation pass.
- A small amount of low-priority CRT data remains less precisely typed, notably
  FILE structures, stream buffers, `_environ` storage, and timezone scalars.
  Do not retype known aliases such as `g_apPlayers` or the overlapping
  scene-text/sound-bank region without new evidence.
- Static analysis alone does not establish every field meaning or runtime
  invariant. Confirm hypotheses with Ghidra cross-references and, where useful,
  the original data files in `/home/wasd/MallManiacsUnmodified/`; preserve
  uncertainty in comments rather than promoting guesses to facts.
- The rebuild is intentionally incomplete outside the verified offline GUI
  path. Network, DirectInput, DirectSound, console, and gameplay states remain
  deferred; their original call sites are represented by deliberate stubs where
  required.

## Verified rebuild boundary

The source project builds `maniac_rebuild.exe` with
`i686-w64-mingw32-gcc` and uses the original `DRIVERS\GXSOFT.DLL`. Wine
verification confirms the window/driver initialization, palette and asset
loading, six-logo intro, five-row main-menu navigation, quit confirmation, and
clean shutdown. The first four menu rows currently return through TODO targets;
this is the known boundary, not a completed gameplay path.

## Next direction

1. Reconstruct one real menu target, preferably `stateGameTypeSelect`
   (`0x41c010`) or `stateOptions` (`0x41c6a0`), using the existing GX and font
   paths.
2. Add the original keyboard path through `pollKeyboard` (`0x416a10`) when the
   target state needs it, then continue toward the first offline single-player
   state.
3. In parallel, resolve the score-counter hypothesis and only the remaining
   type/alias questions that materially support the next rebuild slice. Treat
   deletion of below-image-base `DAT_*` operand artifacts as optional cleanup,
   not analysis progress.

Continue to update this overview, the relevant subsystem document, and
`16-rebuild.md` after each completed chunk; save the Ghidra program regularly.
