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
- Prefer the highest-level MCP tool for each operation; avoid destructive ops
  (delete/clear/overwrite) without evidence.
- Run `ghidra_save_program` periodically; do not commit to git unless asked.
- Do not use `run_script_inline`. Instead use `run_ghidra_script` as documented in `Ghidra_scripts.md`.

Keep each document focused on a concise current-state overview: preserve verified findings, evidence, limitations, and next direction, while removing exhaustive histories, step-by-step work logs, duplicate notes, and complete symbol inventories.

For further documentation per subsystem, see `docs/README.md`.

## Phase 1 — Static Analysis & Documentation (COMPLETE)
- 1178/1178 functions documented = 100% — the FUN_* naming drive-down is COMPLETE.
- Major subsystems renamed (game flow, networking, config, renderer, sound, file
  formats, input); the statically-linked MSVC CRT region 0x43c850-0x44966c fully
  mapped/renamed.
- Progress docs in `docs/`.

## Phase 2 — Source Reconstruction & Rebuild (ACTIVE)
**Read `Rebuild.md` — it is the authoritative guide for this phase. ALWAYS read this before starting new work after a compaction of the session.**

Goal: Produce a compilable `src/maniac.c` (with additional .c-files per subsystem) that builds to
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

PARTICULARILY IMPORTANT:
* All reimplemented functions and globals should have a comment saying their original address.
* The original code architecture and call hierarchy should be replicated. Only necessary exceptions go into `custom_helpers.c`.
* When you implement a function, implement it fully including all function calls and symbol references so it matches the original function. Add stubs in `src/ stubs.c` for called functions that are not yet implemented. Don't use custom calling conventions.
* All logic inside functions should be preserved. Do not inline or put logic where it wasn't in the original executable.
* Ghidra is considered the source of truth. Before reimplementing a function, update its signature in ghidra to give correct names and types to parameters.

Prefer working on fewer functions at a time from a single subsystem, and implementing them before moving on to the next chunk of functions.
