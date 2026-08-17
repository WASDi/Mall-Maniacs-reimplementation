# auto-ghidra — Mall Maniacs (maniac.exe) RE workspace

## Project
- Binary: `/home/wasd/MallManiacsUnmodified/maniac.exe` (Mall Maniacs, 1999,
  AddGames/UDS). Ghidra project: `/home/wasd/ghidra/MMUnmod` (program `maniac.exe`).
- Game data files in `/home/wasd/MallManiacsUnmodified/` are evidence for file formats
  (`.sen`, `.tpg`, `.tga`, `.ai`, `.eo`, XOR-obfuscated `maniac.cfg`/`config.mm`,
  plaintext temp `sommar.sol`).

## Tools
- Ghidra MCP bridge: tools are `ghidra_*` (decompile_function, get_function_xrefs,
  rename_function_by_address, set_function_prototype, set_plate_comment,
  create_struct, list_strings, search_strings, archive_ingest_*, etc.).
- Instance is auto-selected; always pass `program=maniac.exe` when multiple open.

## Conventions
- Evidence-driven naming only; mark hypotheses in comments/docs.
- Consistent subsystem prefixes (e.g. `gx*` renderer, `net*`, `config*`,
  `scene*`, `eventObject*`, `player*`).
- Addresses are 32-bit absolute (base 0x400000).

## Instructions
- When completing a chunk of work, always update documentation before moving on to the next task.
- When researching, focus on the core game code and not statically linked libraries.
- Prefer the highest-level MCP tool for each operation; avoid destructive ops
  (delete/clear/overwrite) without evidence.
- Run `ghidra_save_program` periodically; do not commit to git unless asked.
- Do not use `run_script_inline`. Instead use `run_ghidra_script` as documented in `Ghidra_scripts.md`.

## Phase 1 — Static Analysis & Documentation (COMPLETE)
- 1178/1178 functions documented = 100% — the FUN_* naming drive-down is COMPLETE.
- Major subsystems renamed (game flow, networking, config, renderer, sound, file
  formats, input); the statically-linked MSVC CRT region 0x43c850-0x44966c fully
  mapped/renamed.
- Progress docs in `docs/`.

## Phase 2 — Source Reconstruction & Rebuild (ACTIVE)
**Read `Rebuild.md` — it is the authoritative guide for this phase.**

Goal: Produce a compilable `src/maniac.c` that builds to
`/home/wasd/MallManiacsUnmodified/maniac_rebuild.exe` with offline GUI and
single-player functionality. When continuing work, follow "Next milestone" in `docs/16-rebuild.md`.
Pick a small chunk of work at a time.

## VERY IMPORTANT — function signatures must match and stay in sync

The reimplemented function signature (return type, parameter types, parameter
names, calling convention) MUST match the Ghidra definition exactly, and the
two MUST be kept up to date together. Every time you reimplement a function —
or make any change to a signature on either side — update BOTH sides in the
same session:

1. Apply the exact same signature in Ghidra via `set_function_prototype` (or
   `validate_function_prototype` first), including the calling convention
   (`__cdecl`/`__stdcall` as Ghidra analyzed) and meaningful parameter names.
2. Change the rebuild source to the identical signature, then rebuild
   (`./build.sh`) to confirm it compiles.
3. Save the Ghidra program (`ghidra_save_program`) and note the sync in
   `docs/16-rebuild.md`.

Do not rely on Ghidra's auto-analysis defaults (`undefined4`, `int *`, bare
`char`, `undefined *`) — these are placeholders, not signatures. Refine them to
the precise types (`int`, `FILE *`, `size_t`, `LPCSTR`, ...) the
reimplementation uses, then keep the two in lockstep. A drift between Ghidra
and the rebuild is a bug: the Ghidra definition is the reference for the
original binary, and the rebuild is the ground truth for the replacement. If a
discrepancy is found, fix both sides immediately.

Prefer working on fewer functions at a time from a single subsystem, and implementing them before moving on to the next chunk of functions.
