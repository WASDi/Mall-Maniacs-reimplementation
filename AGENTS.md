# auto-ghidra — Mall Maniacs (maniac.exe) RE workspace

## Project
- Binary: `/home/wasd/MallManiacsUnmodified/maniac.exe` (Mall Maniacs, 1999,
  AddGames/UDS). Ghidra project: `/home/wasd/ghidra/MMUnmod` (program `maniac.exe`).
- Goal: static de-obfuscation + semantic reconstruction (rename FUN_*, retype,
  build structs/enums, map architecture). Progress docs in `docs/` (index =
  `docs/README.md`, working queue = `docs/11-issues.md` — open unknowns/next
  steps §14/§15, short completed-work summaries §16).
- Progress: 1178/1178 documented functions = 100% (maniac.exe) — the
  FUN_* naming drive-down is COMPLETE. Major subsystems renamed (game flow,
  networking, config, renderer, sound, file formats, input); the statically-
  linked MSVC CRT region 0x43c850-0x44966c is fully mapped/renamed. Completed:
  AI movement-mesh/zone-graph region 0x428840-0x42a7xx, SceneObject struct
  (typed SceneObject[8] @0x456210), class-0x1f MCDMAN bonus-item pickup verified
  end-to-end, CRT naming tail (stdio/locale/ctype/strtold/tz/env clusters).
- Ongoing work per docs/11-issues.md §15: semantics polish (verify hypothesis
  plate comments -> [VERIFIED]), retype CRT/scene globals, optional
  archive_ingest_program into the cross-version archive.
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

## Safety (MANDATORY)
- **If any script/tool call fails with an unexpected error, ABORT immediately**
  and report before proceeding. Do not retry blindly or mask errors.
- Prefer the highest-level MCP tool for each operation; avoid destructive ops
  (delete/clear/overwrite) without evidence.
- Run `ghidra_save_program` periodically; do not commit to git unless asked.

## Key starting points (verified)
- entry 0x43f18e -> WinMain 0x004160a0 (loop) -> init 0x00409d90 -> frame 0x0041a8c0
- Renderer: gxInit @0x432f0, gxDrawPolygon @0x33440, driver pick via GXSOFT/GXGLIDE
  (see docs/04-renderer.md)
- Networking: netInit @0x426b00, mnet* sockets, UDP GS_* protocol
  (see docs/05-networking.md)
- Input: initWindowAndInput @0x004165f0 -> DirectInputCreateA thunk @0x42d000
  (see docs/12-input.md)

## Instructions
- When completing a chunk of work, always update documentation before moving on to the next task.
- When researching, focus on the core game code and not statically linked libraries.
