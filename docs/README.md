# Mall Maniacs (maniac.exe) — Architecture Notes

Status: WORK IN PROGRESS. Substantial progress made (networking, GX renderer,
game state machine mapped and renamed: full single-player flow, deferred-action
"request" mechanism, results/game-over flow; navpoint subsystem + .ai/.tpg file
formats closed; AI movement-mesh / zone-wall graph region 0x428840-0x42a7xx
renamed + decoded; statically-linked MSVC CRT region 0x43c850-0x44966c fully
mapped and renamed: config-save iostream stream layer, low-level CRT file I/O,
memory (SBH/heap2 allocators), ctype/float helpers, TLS/errno, C++ EH runtime,
C++-EH vec-ctor/dtor, atexit/exit + lock cluster, cmdline/env startup).
1178/1178 functions documented = 100%. Data surface fully named (pass 5c):
605 globals renamed, 571 typed; only documented aliases/artifacts remain as
DAT_* (see 11-issues.md §16 pass 5c). Only facts marked [VERIFIED]
were confirmed by reading decompilation/xrefs; everything else is hypothesis.

## How to use these notes

Split from the former single `architecture.md` into per-subsystem files so a
work session loads only what it needs. Pick a file by subsystem (below) or by
address range (map at the bottom). `11-issues.md` is the working set — it holds
the open unknowns and the priority-ordered next-steps queue.

## Index

| # | File | Covers | Key addresses |
|---|---|---|---|
| 01 | [01-binary.md](01-binary.md) | Binary overview, toolchain/platform, dynamic imports | image base 0x400000 |
| 02 | [02-boot.md](02-boot.md) | Entry, WinMain, gameInit, main loop, input routing | 0x4160a0 WinMain, 0x409d90 gameInit, 0x41a8c0 gameFrameUpdate |
| 03 | [03-gameflow.md](03-gameflow.md) | State machine, menus, deferred actions, results/high-scores, per-frame gameplay, HUD, round logic | g_pStateFunc 0x45a6f8, roundLogicUpdate 0x40beb0, gameWorldUpdate 0x40b3d0 |
| 04 | [04-renderer.md](04-renderer.md) | GX driver API + wrapper calls | GxDriverApi 0x45eb40, gxInit 0x432f0 |
| 05 | [05-networking.md](05-networking.md) | net* protocol layer, mnet* UDP sockets, GS_* protocol, gameplay message layer | 0x426xxx net*, 0x439730-0x43c640 mnet* |
| 06 | [06-config.md](06-config.md) | Config tree, XOR save/load, console command table, string class | g_pConfigEnv 0x455d38, commandDispatch 0x408b60 |
| 07 | [07-gameplay.md](07-gameplay.md) | Player/AiController/SceneObject structs, object systems, level event directors | g_apPlayers 0x456360, sceneryObjAlloc 0x430200 |
| 08 | [08-globals.md](08-globals.md) | Global memory map / manager objects | various |
| 09 | [09-sound.md](09-sound.md) | DSOUND mixer, sample banks, 3D emitters, MCI CD music, dynamic import slots, movie recorder | 0x437xxx snd*, 0x416cc0 mciPlayCdaudio |
| 10 | [10-fileformats.md](10-fileformats.md) | On-disk formats: .sen (VERIFIED), .tpg/.ai/.eo, save files | sceneLoadSen 0x432320 |
| 11 | [11-issues.md](11-issues.md) | Unknowns / hypotheses + next-steps queue + short summaries of completed work | — |
| 12 | [12-input.md](12-input.md) | Input / DirectInput subsystem: keyboard/mouse devices, key polling, repeat | inputPollKeyboard 0x416820 |
| 13 | [13-msvc-crt.md](13-msvc-crt.md) | Statically-linked MSVC CRT (0x43c850-0x44966c): EH runtime, SBH/heap2 allocators, exit/lock/startup, stdio/ctype/format, strtold/tz/env naming | crtCxxFrameHandler 0x43dde1, crtSbhInit 0x443fc0 |
| 14 | [14-animations.md](14-animations.md) | .anm animation format (keyframe stream), loader cluster, playback chain | anmLoad 0x433a90, sceneObjectAnimStep 0x434540 |
| 15 | [15-retyping.md](15-retyping.md) | Undefined-type retyping plan (578 game funcs + 2 game globals; skips dispatch/switch tables + CRT) | census: FindUndefinedTypes.java |

## Address-range → file map (approximate, for lookup only)

- `0x401000-0x40b000` engine boot + gameplay entry → [02-boot.md](02-boot.md), [03-gameflow.md](03-gameflow.md)
- `0x40b000-0x40d000` gameWorldUpdate / roundLogicUpdate / gameObjectUpdate → [03-gameflow.md](03-gameflow.md)
- `0x40d000-0x416000` player AI / scene setup / net game layer → [03-gameflow.md](03-gameflow.md), [05-networking.md](05-networking.md), [07-gameplay.md](07-gameplay.md)
- `0x416000-0x42a000` state machine / menu / endscene / high-scores → [03-gameflow.md](03-gameflow.md), [12-input.md](12-input.md)
- `0x426000-0x43c000` net protocol + mnet sockets → [05-networking.md](05-networking.md)
- `0x432000-0x437000` GX renderer + scene loader + .anm anim cluster → [04-renderer.md](04-renderer.md), [10-fileformats.md](10-fileformats.md), [14-animations.md](14-animations.md)
- `0x437000-0x43a000` sound subsystem → [09-sound.md](09-sound.md)
- `0x43e000-0x440000` file wrappers / config / console → [06-config.md](06-config.md)
- `0x43c850-0x44966c` statically-linked MSVC CRT (iostream stream layer,
  low-level file I/O, SBH/heap2 memory allocators, ctype/float, TLS/errno,
  C++ EH runtime + vec-ctor/dtor, atexit/exit + locks, cmdline/env startup) →
  [13-msvc-crt.md](13-msvc-crt.md), [06-config.md](06-config.md),
  [11-issues.md](11-issues.md) §16
- `0x440000-0x500000` .rdata/.data constants, strings, globals → [08-globals.md](08-globals.md), [01-binary.md](01-binary.md)

## Tooling

- Running Ghidra Java scripts via the MCP bridge (stale-cache workaround, manual
  javac compile, inline-script gotcha) → [`Ghidra_scripts.md`](../Ghidra_scripts.md)
- Data-surface bulk apply (pass 5c) used `ApplyDatTypes.java` (name+type from an
  embedded plan) + `VerifyDatTypes.java` (DAT_* census); both in
  `/home/wasd/ghidra_scripts/`, plans in `/tmp/opencode/finalplan.json`/`.tsv`.
- Remaining `DAT_*` lookup: [`dat-alias-lookup.txt`](dat-alias-lookup.txt) maps
  every remaining program `DAT_*` (all in the `g_playerRecords` region
  0x456210-0x457db0) to its record index + offset + logical field
  (e.g. `DAT_00456240` = `g_playerRecords[0].pCharMesh`).

## Key starting points (verified)

- entry 0x43f18e -> WinMain 0x004160a0 (loop) -> init 0x00409d90 -> frame 0x0041a8c0
- Renderer: FUN_004332f0 (gfx init), FUN_00433440 (poly draw), driver pick via GXSOFT/GXGLIDE
- Networking: FUN_00426b00 area ("net:" strings), UDP GS_* protocol
- Input: DirectInputCreateA thunk 0x0042d000 (called from 0x004165f0)
