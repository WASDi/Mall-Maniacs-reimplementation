# auto-ghidra — Mall Maniacs reverse-engineering workspace

## Project
- Binary: `/home/wasd/MallManiacsUnmodified/maniac.exe` (Mall Maniacs, 1999,
  AddGames/UDS). Ghidra project: `/home/wasd/ghidra/MMUnmod` (program `maniac.exe`).
- Game data files in `/home/wasd/MallManiacsUnmodified/` are evidence for file formats
  (`.sen`, `.tpg`, `.tga`, `.ai`, `.eo`, XOR-obfuscated `maniac.cfg`/`config.mm`,
  plaintext temp `sommar.sol`).
- `gxSoft.dll` is also loaded into Ghidra. Don't try to import it again.

## Goal
Reimplement the Ghidra view of `maniac.exe` as source in `src/maniac.c` and
per-subsystem files. The result must build as
`/home/wasd/MallManiacsUnmodified/maniac_rebuild.exe` and provide an offline
GUI and single-player functionality. Work in small chunks, following the next
milestone in `docs/16-rebuild.md`.

## Implementation
- Do not reimplement system libraries, runtime code, import stubs, or compiler glue.
  Network features and the in-game console are out of scope.
- Build 32-bit Windows with `i686-w64-mingw32-gcc` and the needed original system
  libraries (`KERNEL32`, `USER32`, `GDI32`, `WINMM`). Skip DirectInput, DirectSound,
  and Winsock. Use `DRIVERS\GXSOFT.DLL` through its GX interface, not a GDI backend.
- Use Ghidra's decompilation, disassembly, and `docs/` as behavioral and layout
  evidence, not as source to compile. Feed verified names, types, structs, and
  `gxSoft.dll` findings back into Ghidra, and update `docs/16-rebuild.md` after each
  chunk.
- Give every unresolved dependency a documented contract and a safe temporary `TODO`
  stub in `src/stubs.c`/`src/stubs.h` or the relevant subsystem. Include the original
  address when known; never comment out unresolved calls or change their interfaces.
- Reproduce initialization and state-machine behavior subsystem by subsystem, while
  prioritizing dependencies of the next visible single-player feature.
- Build with `build.sh`, which writes to `/home/wasd/MallManiacsUnmodified/maniac_rebuild.exe`.
  Use `run.sh` for the Wine crash smoke test; confirm initialization and asset loading
  in the logs. Do not modify the original executable or take screenshots.

## Tools
- Use the Ghidra MCP bridge (`ghidra_*`) for decompilation, cross-references, naming,
  prototypes, comments, structs, and string searches. Pass `program=maniac.exe` when
  multiple programs are open.

## Conventions
- Name symbols from evidence and mark hypotheses in comments or documentation.
- Use subsystem prefixes such as `gx*`, `net*`, `config*`, `scene*`, `eventObject*`,
  and `player*`. Addresses are 32-bit absolute, based at `0x400000`.

## Working practices
- Prefer the highest-level MCP operation and avoid destructive changes without evidence.
- Save the Ghidra program periodically; do not commit unless asked.
- Use `run_ghidra_script` as documented in `Ghidra_scripts.md`, not `run_script_inline`.
- Inspect assembly closely enough for the reconstructed source to match the original.

Keep each document focused on a concise current-state overview: preserve verified
findings, evidence, limitations, and next direction; drop exhaustive histories,
step-by-step work logs, duplicate notes, and complete symbol inventories.

See `docs/README.md` for subsystem documentation.

## Function signatures

Keep each reimplemented function's return type, parameter types and parameter names
synchronized with Ghidra. Whenever you implement a function or
change its signature, update both sides in the same session:

* After reimplementing a new function, apply the same signature in Ghidra with 
  `set_function_prototype` (if not already the same), including meaningful parameter names.
* Don't forget to declare structs also in ghidra.

Use precise types rather than Ghidra placeholders such as `undefined4` or `undefined *`;
keep the rebuild and Ghidra definitions identical. Ghidra is the reference for the
original binary, so fix any discrepancy on both sides immediately.

- Comment every reimplemented function and global with its original address.
- Preserve the original architecture, call hierarchy, and function logic. Add only
  necessary helpers to `custom_helpers.c`. Do not invent new functions in other files that don't exist in the original binary.
- Implement called functions fully or provide documented stubs in `src/stubs.c` without
  changing callers or interfaces. Keep the original calling conventions in Ghidra, but
  omit convention keywords from rebuild declarations as required by the project style.
- Before reimplementing a function, refine its Ghidra signature with matching and
  meaningful types and parameter names.
- Work on a small number of functions from one subsystem at a time.

When investigating problems, thoroughly verify that the assembly logic matches the reimplemented code.
All source of bugs are mismatches. The original assembly is the source of truth.

The script `TrackRebuildDetailed` reports overall progress.

