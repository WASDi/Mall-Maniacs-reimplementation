# Mall Maniacs (maniac.exe) — 15. Undefined-type retyping

[Back to README](README.md)

Status: the undefined-data cleanup is complete for standalone game globals, and
the mechanically recoverable function prototypes have been applied. The
remaining function work is semantic type refinement; it is intentionally not a
guess-driven drive-down.

## Purpose and scope

This document tracks the `undefined*` census produced by `FindUndefinedTypes.java`
for `maniac.exe`. The original non-CRT scope was 578 game functions and two
standalone game globals. Statically linked MSVC CRT code is not game code and is
excluded, including the CRT function region `0x43c850-0x44966c`, CRT data, and
the 148 `Unwind@0044xxxx` exception-handling stubs.

The console dispatch table at `0x44b320-0x44b43c` and Ghidra switch/jump tables
at `0x41bf3c`, `0x41ef20`, and `0x41ff98` are also deliberately left alone.
Their pointer-like types describe table data rather than ordinary variables and
are not useful targets for speculative retyping.

## Completed work

- **Function prototype pass (2026-08-16):** `RetypeUndefinedFunctions2.java`
  processed 577 game functions using each `HighFunction`'s recovered prototype,
  applying 281 return types and eight parameter updates. The census fell from
  724 to 526 entries, including the 148 excluded `Unwind` stubs; 378 game
  functions still need semantic review.
- **Math correction:** `mathSinDeg` and `mathCosDeg` (`0x42d030`/`0x42d050`)
  now return `long double` (`__cdecl (short nDeg)`). The x87 `fsin`/`fcos`
  results are `float10`, not `float`.
- **Global data pass (2026-08-16):** the two standalone game descriptors at
  `0x44f0e8` and `0x44fdc0` were typed and named
  `g_szSceneNameTable` and `g_szHudPathDescriptor`. A subsequent targeted pass
  resolved the remaining standalone game globals, including Swedish UI strings,
  menu/player data, scene-text state, and network setup fields. Remaining
  undefined global labels are sub-symbols or overlaps of typed parents, not
  independent data items.
- **Important layout evidence:** `g_playerRecords` at `0x456210` is
  `SceneObject[8]` with a `0x374` record stride; `g_apPlayers` is its player-view
  sub-object at `+0x150`. The scene-text glyph area is `void *[16]`, its active
  value is an `int` (confirmed by the loads/stores), and `g_abWsaData` is a
  Winsock startup reference count, not a `WSADATA` object.

## Limitations

The automated pass is exhausted. Of the 378 remaining game functions, 213 have
an `undefined4` return that the decompiler itself still cannot distinguish as a
status/flag, `void`, or another int-sized value; other cases are pointer or
floating-point returns and functions whose parameters remain undefined. At
least 108 functions still carry an `undefined*` parameter. These are valid
ambiguities, not evidence that every item should be forced to a concrete type.

The global pass deliberately treats labels inside typed arrays and overlapping
regions as aliases. In particular, the player-record, scene-text, and sound-bank
regions contain shared or sub-symbol addresses; retyping those labels
independently would corrupt the established layouts.

## Next direction

Manually review the remaining function signatures in small subsystem batches.
Use the decompiled body, callers/xrefs, disassembly, and established structures
to distinguish `void`, status values, pointers, and parameter types; record
only confirmed conclusions as `[VERIFIED]` plate comments. Preserve calling
conventions and meaningful parameter names, update the matching rebuild
signatures when a function is reimplemented, save the Ghidra program, and keep
the detailed queue in `docs/11-issues.md` §15. A new census should be used as a
measurement after each evidence-backed batch, not as a target for speculative
zeroing of the remaining `undefined*` entries.