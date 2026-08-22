# Mall Maniacs (maniac.exe) — 16. Rebuild (maniac_rebuild.exe)

[Back to README](README.md)

Status:

The rebuild now runs the original offline GUI path through
`DRIVERS\GXSOFT.DLL`: the six-logo introduction transitions to the five-row
main menu, which supports keyboard navigation, row dispatch, quit confirmation,
and clean shutdown. The first real row-target state is implemented: "Spela"
enters the four-mode game-type select (`stateGameTypeSelect`), whose Enter
targets (the `modeInit*` initializers) set the game mode and player-count
policy before handing off to the still-deferred character select.

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
  `0x41b0b0`), the game-type select (`stateGameTypeSelect`, `0x41c010`) with
  its four `modeInit*` initializers (`0x41bf60`–`0x41bfe0`), and quit
  confirmation (`stateQuitConfirm`, `0x4200b0`). The game-type select renders
  the four modes (`Varujakten`/`Matkrig`/`Frågesporten`/`Vagnrace`) in the
  same two-font row style as the main menu; Up/Down wrap the selection, Enter
  selects the mode initializer, and Escape returns to the main menu. The
  mode initializers set `g_nGameMode` (`0x458120`) and `g_nPlayerCount`
  (`0x458108`; 0 = auto-derive except Frågesporten, which forces 1) before
  handing off to `stateCharacterSelect` (`0x41efa0`), still a TODO stub.
- **Sound:** `src/sound.c`/`src/sound.h` reproduce the maniac sound
  subsystem (`sndInitSystem @0x437a30`, `sndLoadBankFromDir @0x437170`,
  `sndLoadWav @0x437420`, `sndPlaySfx @0x437cf0`, `sndMixTick @0x437c50`,
  positional `sndVolFromPos @0x4389a0`, priority `sndVoicePriorityUpdate
  @0x4387a0`, `sndFixedMul @0x437ed0`) over the same DirectSound streaming
  path as the original (DirectSoundCreate + SetCooperativeLevel + streaming
  buffer, per-frame Lock/Write/Unlock of free write regions). `menuInit`
  initializes it and loads the `sound\menu\` bank;
  `menuUpdate`/`stateGameTypeSelect` play the original feedback cues
  (Up/Down = 01_Buttons, Enter = 03_Miss2, game-type Escape = 04_kokko);
  `gameFrameUpdate` calls `sndMixTick(0)` each frame, which reproduces the
  original call graph exactly: `sndMixBuildVoiceChains` (@0x437fe0) →
  per-region `sndMixRenderRegion` (@0x438090, latch the mixer master volume,
  priority/volume update + culling, scratch reset, finish-check + `sndVoiceRender`,
  chain advance, `sndVoiceReclaimFinished`, `sndMixScratchToBuffer` twice) →
  `sndMixAdvanceUnlock`. Per-voice L/R volumes are derived per region through
  the original position/gain chain (`sndVolFromPos` →
  `((posVol*master>>16)*gain)>>0x18`); plain sfx render centered at vol8 ≈ 63
  with the same ±8k per-sample magnitude as the original (the earlier
  hard-coded 127 was 2× and clipped on overlap). `sndVoicePriorityUpdate`
  culls the active chain to `g_nMixVoiceCap` (4 in the menu), matching the
  original's loudness culling. Rendering uses the reproduced wave-table core
  `sndVoiceRender` (`0x438b00`) → `sndMixVoiceCore` (`0x4395b0`) →
  `sndMixStep` (`0x4396a6`) → `sndMixSamples` (`0x4396ea`) with the
  `SndMixRec` record, synced and mirrored into Ghidra. MCI CD-audio is the
  only intentionally unreproduced path (docs/09-sound.md). The 64-entry
  `sndBuildEnvProfile` (`0x437900`) normalization is covered by
  `tests/test_sound_profile.c`; this prevents unset envelope data from
  bypassing loudness culling and clipping overlapping effects.
  `sndMixScratchToBuffer` (`0x4382f0`) is covered by
  `tests/test_sound_output.c`; it now keeps the negotiated 16-bit stereo
  stream signed instead of applying an incorrect unsigned `0x8000` bias, and
  emits zero for signed-format silence so voice start/stop boundaries do not
  generate a full-scale impulse.
- **Rendering:** `src/gx.c` adapts the maniac-side GX wrappers to
  `GXSOFT.DLL`; indexed TGA assets and the `MERGED00.TPG` palette render at
  the original 640x480 resolution.
- **Foundation:** `src/pool.c` and `src/util.c` provide the reconstructed pool
  and file-helper interfaces used by the menu and font code. The pool now
  preserves the original 64-subpool × 64-chunk × 16-slot hierarchy, size-marked
  records, pool-wide search/free, and finite-capacity behavior; invalid-handle
  failure behavior remains intentionally non-defensive like the original.
- **Fonts:** `src/font.c` implements the original font/text range
  `0x408f90–0x409940`: descriptor parsing, atlas UV setup, tagged text
  rendering, centering, and integer formatting, including the two-font menu
  rows. The original static utility `fmtParseInt` (`0x43e860`) is represented
  by the CRT-compatible `strtol` substitution; `strFindSubstring` is likewise
  represented by `strstr`. A standalone visualizer `tests/render_font.c`
  reuses `fontLoad`/`fontParse` and decodes the paired `.tpg` directly,
  rendering all 256 glyphs to a BMP; it generated the `tests/font_*.bmp`
  set for the menu and per-scene hud fonts.
- **Stubs and helpers:** `src/stubs.c` contains deferred original functions;
  rebuild-only support code is isolated in `src/custom_helpers.c`. The three
  void stubs (`gameInit @0x409d90`, `gameFrameUpdate @0x41a8c0`, and
  `pollKeyboard @0x416a10`) use `void(void)` with the default C convention and
  are logged no-ops with no false success return. Ghidra's
  `pollKeyboard` body actually polls DirectInput, debounces key IDs, dispatches
  `(key, 2)`, clears the quit flag, and may post `WM_CLOSE`; those effects are
  intentionally absent because the slice uses queued window messages. The
  remaining menu row entries (`stateNetworkMenu @0x420190`, `gotoOptions
  @0x41d300`, `stateHighScoreTable @0x41dfd0`) and the character-select target
  (`stateCharacterSelect @0x41efa0`) remain deliberate `int(int, int, int)`
  replacement stubs: they log once, return `0`, and route back to the menu (or
  game-type select); their original rendering/gameplay/network logic remains
  deferred.

## Verified behavior

1. The GX driver initializes and installs the palette from `MERGED00.TPG`.
2. All six intro logos play in the original order and timing, then enter the
   main menu; only the Space/fire key skips the intro, while unmapped window
   keys are ignored.
3. Up/Down navigation wraps across `Spela`, `Nätverk`, `Alternativ`, `Rekord`,
   and `Avsluta`; Enter dispatches the selected row.
4. Enter on `Spela` enters the game-type select. Up/Down wrap across the four
   modes (`Varujakten`, `Matkrig`, `Frågesporten`, `Vagnrace`); Escape returns
   to the main menu; Enter on a mode logs the selected mode, sets
   `g_nGameMode`/`g_nPlayerCount`, and reaches the (TODO) character select,
   which logs once and returns to the game-type select.
5. Escape opens quit confirmation. Enter selects `Avsluta` without immediately
   cancelling the newly displayed screen; Escape returns to the menu, while
   the original J/Y character confirmations exit cleanly.
6. Sound initializes through DirectSound: the driver negotiates
   44100/16-bit/stereo, and all five `sound\menu\` WAVs load into bank 1.
   Menu navigation/selection queues effects through `sndPlaySfx`, and
   `sndMixTick` locks DirectSound write regions, renders the voices, and
   unlocks them each frame.

The remaining row targets currently log a TODO message and return to the main
menu. This is intentional until those states are reconstructed.

## Next milestone

Implement the next real row-target state, preferably the character select
(`stateCharacterSelect`, `0x41efa0`, now reachable from every `modeInit*`) or
the options screen reached through `gotoOptions` (`stateOptions`, `0x41c6a0`).
Reuse the existing menu assets and text renderer, then add the real input path
(`pollKeyboard`, `0x416a10`) when mouse/controller support is required.
Gameplay remains after the GUI states.

## Progress tracking

`TrackRebuildProgress.java` compares address annotations in the rebuild source
with the in-scope functions in Ghidra and writes `rebuild-progress.txt`. Re-run
it when an implementation chunk changes; the report may otherwise be stale.

Continue to follow `Rebuild.md` for constraints and update this overview,
Ghidra, and relevant subsystem documentation after each completed chunk.

## Fidelity and limitations

- Source audits for the application loop, menu, GX, pool, utility, font, sound,
  and stub code are complete. Reimplemented symbols retain original-address
  comments and match the verified Ghidra parameter and return types; only the
  required Win32 entry/callback declarations use explicit ABI markers.
- The GXSOFT path preserves the original wrapper hierarchy, polygon conversion
  branch, packed UV data, font rendering flow, file-helper behavior, and
  64-subpool × 64-chunk × 16-slot pool capacity. The CRT-compatible
  `strtol`/`strstr` substitutions, explicit vertex initialization, and the
  DirectSound mixer's direct per-sample multiply (in place of the original
  register-based wave-table `sndMixVoiceCore` inner loop) are the only known
  low-level deviations in the implemented slice.
- Deferred behavior includes DirectInput polling, MCI CD-audio
  (the original music path), networking, registry-based driver selection, and
  real menu row targets. `stateNetworkMenu`, `gotoOptions`, and
  `stateHighScoreTable` intentionally return to the menu instead of claiming
  those states are implemented.

## Character-select 3D preview (scene graph) — implemented 2026-08-23

The character-select 3D preview now uses the original scene-graph call
hierarchy instead of invented helpers. Wired in `src/charselect.c`
(`stateCharacterSelect` frame path) and implemented in `src/scene.c` /
`src/scene.h`:

- On first entry: `sceneSystemInit(100000,40000,40000,2000,2)` then
  `sceneLoadSen("menu\\CHARACTERS.SEN")` (populates the shared mesh table in
  `sen.c`; 36 MESH/NAME pairs confirmed loaded).
- Per character: `sceneNodeAllocChild` → `sceneObjSetPosOrient` →
  `scenNameToId(g_apCharSceneNames[idx])` (resolves e.g. `ROLAND` to its MESH
  bytes = a serialized `SceneObjTypeDef`) → `sceneryObjAlloc` → `anmLoad`
  (returns NULL when no RUN anim loaded; harmless) → `eventAnimReset`.
- Per frame: `sceneObjSetPosOrient` (model yaw), `eventAnimStep`, camera set
  via `sceneObjSetPos(g_pSceneRoot,…)` + `sceneNodeFacePos(g_pSceneRoot,…)`,
  then `sceneRender(g_pSceneRoot)`.

Key correctness fix: `SceneNode` embeds its `SceneChannel` in the node tail
(offset +0x38) so the 0x70-byte channel fits within 0xa8 and `pChannels`
(field +0x28) points at it — previously the channel base overlapped the
`pChannels` pointer field, so `sceneObjSetPosOrient` corrupted the pointer and
crashed (`Unhandled page fault` in `sceneObjSetPosOrient`).

Limitations / next steps: the world→screen projection in `sceneNodeRender`
is best-effort (the decompiler loses precision on the clip/divide math — needs
assembly verification); `sceneNodeRender` currently treats small values as
chunk-relative offsets and bounds-checks `nVerts` to avoid crashes.

## Struct sync (mesh/scene structs) — verified 2026-08-24

- `sceneLoadSen` @0x432320 / `senChunkParse` @0x432c00 / `sceneMeshFixup`
  @0x4320f0 are now verified against disassembly and are faithful: the REV2
  chunk walk, MESH/EMAN/OBJI/MAPI/TANI/ONAM/TNAM/SUBO/KEEP/TEMP/COLS dispatch,
  and the `sceneMeshFixup` relocation passes (`+0xc,+0x10,+0x14,+0x20,+0x28,
  +0x30` on the mesh header; the per-subobj loop at `pRender+8` stride `0x30`
  relocating `+0x8,+0x24,+0x10,+0x18` and the vertex array via
  `[g_pMapGeom+0x10]` when present) all match. The 36 MESH/NAME pairs in
  `menu\CHARACTERS.SEN` load and relocate with no page fault.
- Fixed two `SceneNode` / `SceneChannel` layout bugs in `src/scene.h` that made
  the C structs drift from both Ghidra and the original binary:
  1. `SceneNode` was missing the `+0x1c` field, so `pTypeDef` landed at `+0x1c`
     instead of `+0x20` (self-consistent but not faithful). Added `unk1c`.
  2. `SceneChannel` was unpadded, so the float fields shifted the struct to
     116 bytes and the `N*0x70` channel stride was wrong. Added
     `__attribute__((packed))` to both `SceneChannel` (now 0x70) and
     `SceneNode` (now 0xa8). Verified via `offsetof`: `ch` @+0x38, `wmat`
     @+0x40, `wx` @+0x64 — matching Ghidra.
- Ghidra `SceneObjTypeDef` (52B) / `SceneObjRenderInfo` (48B) / `SceneNode`
  (0xa8) / `SceneChannel` (0x70) structs recreated/synced (offsets + sizes
  confirmed; the type-def structs are all-int so no packing needed).
- `sceneMorphInterp` @0x4300d0 verified instruction-by-instruction: `t<=0`
  returns `base+idxA*nVerts*8`, `t>=1` returns `base+idxB*nVerts*8`, else lerps
  with the truncating ftol `0x43dd10`.

Remaining: make the `sceneNodeRender` projection pixel-correct (currently
best-effort world→screen divide/clip).
