# auto-ghidra — Mall Maniacs reverse-engineering workspace

## Project
- Binary: `/home/wasd/MallManiacsUnmodified/maniac.exe` (Mall Maniacs, 1999,
  AddGames/UDS). Ghidra project: `/home/wasd/ghidra/MMUnmod` (program `maniac.exe`).
- Game data files in `/home/wasd/MallManiacsUnmodified/` are evidence for file formats
  (`.sen`, `.tpg`, `.tga`, `.ai`, `.eo`, XOR-obfuscated `maniac.cfg`/`config.mm`,
  plaintext temp `sommar.sol`).
- `gxSoft.dll` is also loaded into Ghidra. Don't try to import it again.

## Goal
Cross-platform reimplementation of the Ghidra view of `maniac.exe` as portable
C source in `src/`, building natively (64-bit) via CMake as `maniac_rebuild`
with offline GUI and single-player functionality.

- Authoritative plan: `cross_platform_plan.md` — stack (SDL2 + OpenGL 3.3 +
  CMake), architectural seams, phases, and verification gates. Follow its phase
  order; do not re-plan architecture in `docs/`.
- Active development and verification target: **Linux x86-64**. Defer
  Windows/macOS build/run claims until those targets are actually verified.
- The Win32/MinGW/`GXSOFT.DLL` era (`maniac_rebuild.exe` under Wine) is
  historical — see `docs/16-rebuild.md`. Do not extend it; record new status
  in `docs/17-cross-platform.md`.

## Implementation
- Do not reimplement system libraries, runtime code, import stubs, or compiler glue.
  Also do not reimplement features that exist in standard libraries, such as math, string, and other utils.
  Network features and the in-game console are out of scope (removed, not stubbed).
- Do not move the architectural seams defined in `cross_platform_plan.md`
  (graphics/sound/input/time boundaries, `src/` platform-file layout).
- Give every unresolved dependency a documented contract and a safe temporary `TODO`
  stub in `src/stubs.c`/`src/stubs.h`.
- Reproduce initialization and state-machine behavior subsystem by subsystem, while
  prioritizing dependencies of the next visible single-player feature.

## Build and verification
- Primary build: `cmake -S . -B build && cmake --build build` (output
  `maniac_rebuild`), plus `ctest`. See `cross_platform_plan.md` for deps,
  flags, source list, and per-phase verification gates.
- Legacy `Makefile` (32-bit MinGW `maniac_rebuild.exe`) is retired. Do not use
  it unless explicitly asked for a Wine comparison reference.
- Check `docs/README.md` for documentation. `docs/16-rebuild.md` is frozen Win32 history.
- Follow `XDOTOOL_NAVIGATION.md` to start the game and take screenshots for validation.

## Working practices
- Use the Ghidra MCP bridge (`ghidra_*`) for decompilation, cross-references, naming,
  prototypes, comments, structs, and string searches. Pass `program=maniac.exe` when
  multiple programs are open.
- Keep Ghidra as the reference for the original binary. Update it only when
  investigation discovers original-binary facts (types, structures, names,
  comments, behavior).
- Save the Ghidra program periodically; do not commit unless asked.
- To run ghidra scripts, follow `Ghidra_scripts.md`.
- Inspect assembly closely enough for the reconstructed source to match the original. It should be logically equivalent, simplifications are accepted.
- Follow established patterns and code format and comment structures.
- After every thinking step, output a one sentence summary of the progress.

## Rules

VERY IMPORTANT! REMEMBER THESE:

- Always look at both `decompile_function` and `disassemble_function` when reimplementing a function. The decompile serves as an overall structure, but it is lossy so the disassembly needs to be thoroughly verified so its logic matches the reimplemented code.
- Use precise types rather than Ghidra undefined placeholders. Give proper names to everything (not ghidra placeholder names like "param_1" or "iVar3" etc). Use `set_function_prototype` and  `create_struct` to come up with structs the the original code likely had. Don't use raw access `obj + 0x...`, instead use `obj.field`.
- Reimplemented functions must ONLY call functions also called by the original binary to preserve call hierarchy. Report any violation found in existing code. Do not invent new functions, except for temporary debug purposes and logging or utils in `src/custom_helpers.c`.
- Comment every reimplemented function and global with its original address. Write comment above function declaration.
- Implement called functions fully or provide documented stubs in `src/stubs.c` without changing callers or interfaces. Keep the original calling conventions in Ghidra, but omit convention keywords from rebuild declarations as required by the project style.
- The new code and the ghidra view should be in sync. After writing new code, update ghidra with newly discovered information such as function types and structs.

The ghidra script `~/ghidra_scripts/TrackRebuildDetailed.java` reports overall progress in `/tmp/opencode/tracked-rebuild-detailed.txt`.
