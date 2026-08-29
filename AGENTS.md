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
GUI and single-player functionality.

## Implementation
- Do not reimplement system libraries, runtime code, import stubs, or compiler glue.
  Also do not reimplement features that exist in standard libraries, such as math, string, and other utils.
  Network features and the in-game console are out of scope.
- Build 32-bit Windows with `i686-w64-mingw32-gcc` and the needed original system
  libraries (`KERNEL32`, `USER32`, `GDI32`, `WINMM`). Skip DirectInput, DirectSound,
  and Winsock. Use `DRIVERS\GXSOFT.DLL` through its GX interface, not a GDI backend.
- Check `docs/README.md` for documentation. Update `docs/16-rebuild.md` with current status (not "work performed").
- Give every unresolved dependency a documented contract and a safe temporary `TODO`
  stub in `src/stubs.c`/`src/stubs.h`.
- Reproduce initialization and state-machine behavior subsystem by subsystem, while
  prioritizing dependencies of the next visible single-player feature.
- Build with `make`, which writes to `/home/wasd/MallManiacsUnmodified/maniac_rebuild.exe`.
  Read `XDOTOOL_NAVIGATION.md` for triggering keyboard events to navigate the in-game menu during verification.

## Working practices
- Use the Ghidra MCP bridge (`ghidra_*`) for decompilation, cross-references, naming,
  prototypes, comments, structs, and string searches. Pass `program=maniac.exe` when
  multiple programs are open.
- Save the Ghidra program periodically; do not commit unless asked.
- To run ghidra scripts, follow `Ghidra_scripts.md`.
- Inspect assembly closely enough for the reconstructed source to match the original. It should be logically equivalent, simplifications are accepted.
- Follow established patterns and code format and comment structures.

## Rules

VERY IMPORTANT! REMEMBER THESE:

- Always look at both `decompile_function` and `disassemble_function` when reimplementing a function. The decompile serves as an overall structure, but it is lossy so the disassembly needs to be thoroughly verified so its logic matches the reimplemented code.
- Use precise types rather than Ghidra undefined placeholders. Give proper names to everything (not ghidra placeholder names like "param_1" or "iVar3" etc). Use `set_function_prototype` and  `create_struct` to come up with structs the the original code likely had. Don't use raw access `obj + 0x...`, instead use `obj.field`.
- Reimplemented functions must ONLY call functions also called by the original binary to preserve call hierarchy. Report any violation found in existing code. Do not invent new functions, except for temporary debug purposes and logging or utils in `src/custom_helpers.c`.
- Comment every reimplemented function and global with its original address. Write comment above function declaration.
- Implement called functions fully or provide documented stubs in `src/stubs.c` without changing callers or interfaces. Keep the original calling conventions in Ghidra, but omit convention keywords from rebuild declarations as required by the project style.
- The new code and the ghidra view should be in sync. After writing new code, update ghidra with newly discovered information such as function types and structs.

The ghidra script `~/ghidra_scripts/TrackRebuildDetailed.java` reports overall progress in `/tmp/opencode/tracked-rebuild-detailed.txt`.
