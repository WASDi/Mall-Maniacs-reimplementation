# Cross-platform plan: SDL2 + OpenGL 3.3 (simple)

Goal: remove all Windows dependencies so `maniac_rebuild` builds natively (64-bit)
on Linux, Windows, and macOS, rendering with OpenGL 3.3 (hardware accelerated
when available). The end result favors easy migration and simple code over
pixel-faithful reproduction of the original software renderer's quirks.

Active development and verification target: Linux x86-64. Keep the architecture
portable, but defer Windows and macOS build/run claims until those targets are
actually verified. Every numbered phase must compile and link on Linux x86-64;
only the stated integration milestones must launch the game. Until a subsystem
is replaced, use documented, safe temporary compatibility implementations so
the phase still links; these are build bridges, not claims of working features.

Stack: **SDL2** (window, input, timing, audio, message boxes) + **OpenGL 3.3 core**
(single shader, one streaming VBO, minimal batching) + **CMake** (native 64-bit
toolchain per OS). GXSOFT.DLL is fully replaced by an in-tree backend. The
DirectSound software mixer is fully replaced by simple SDL2 voices.

## Architectural seams (do not move)

- Game logic reaches graphics only through the `GxDriverApi` table (`src/gx.h`)
  via the `src/gx.c` wrappers (`gxDrawPolygon`, `gxDrawTriUV/Quad/Triangle/Line`,
  `gxBlitSurface`, `gxFlip`, `gxClearScreen`, `pLoadTexture/pCreateSurface`,
  viewport/origin/state). Keep the table and wrappers; `gxLoadDriver` installs
  the built-in GL backend instead of loading a DLL.
- Keep `g_abInputKeyHeld[8]` with the `0x80` held-bit convention and the
  `pollKeyboard @0x416a10` (200 ms debounce), `pollKeyboardGame @0x416820`
  (Space tap/hold/double-tap machine), `dispatchKeyEvent @0x41ade0` logic
  untouched; only the event source changes (SDL2 instead of `WindowProc`).
- Keep `getGameTime @0x40dfe0` gating (`g_nClock/g_nClockCache/g_nFrameDue`)
  while swapping the clock source to SDL2.
- Keep public sound entry names (`sndInitSystem @0x437a30`,
  `sndShutdown @0x437cb0`, `sndPlaySfx @0x437cf0`, `sndMixTick @0x437c50`,
  emitter link/update/free) so callers in `gameRunFrame`/menu code are
  untouched; their internals are replaced (see Phase 4).
- New code lives flat in `src/` next to the game code:
  `src/compat_types.h`, `src/platform_sdl2.c/.h`, `src/gx_sdl2_gl.c/.h`,
  `src/audio_sdl2.c/.h`. Game-logic files never call SDL/GL directly; only
  these three translation units plus the thin `gx.c` installer do.

## Phase 1 — Portable foundation + 64-bit correctness

1. Add `src/compat_types.h`: `DWORD→uint32_t`, `WORD→uint16_t`,
   `BYTE→uint8_t`, `LPCSTR→const char*`, `LPSTR→char*`, `MAX_PATH→260`,
   `HWND/HINSTANCE/HMODULE→void*` opaque, `ZeroMemory→memset`,
   `TRUE/FALSE`. Replace `#include <windows.h>/<mmsystem.h>/<dsound.h>` in
   all ~47 files with it.
2. Trivial swaps: `wsprintfA→snprintf`, `lstrcpyA→strcpy`
   (`player_setup.c`, `gameplay.c`, `util.c`).
3. 64-bit pointer audit (required before CMake 64-bit is green):
   - Audit pointer casts and storage (including the estimated `~69`
     `(int)(size_t)ptr` casts) and use pointer-sized types only for native
     runtime pointers. Keep serialized/on-disk records explicitly fixed-width;
     do not widen their fields or change record sizes/strides. Parse disk
     records into native runtime structures where pointer-sized fields are
     needed, preserving and testing the original file-format layout.
     Texture handles stay `int` via a backend `int→GLuint` registry (no
     pointer truncation).
   - `fileOpenMode @0x408cd0` round-trips `FILE*` through `int`
     (`sen.c:460`) → return `FILE*` (`util.h/c`, `sen.c`).
   - Investigate `sen.c:316` and the apparent pointer-in-`int`, and inspect
     original assembly/Ghidra for `scene_system.c:119` before replacing the
     apparent pointer-in-float. Model each according to its actual role;
     do not assume `intptr_t` is a valid on-disk representation. Audit raw
     `+0x…` offset math in `sen/scene_transform/scene_render` for pointer-size
     assumptions. Keep format/layout assertions for serialized data active
     on every host; isolate any genuinely 32-bit runtime-layout assertions.
4. Add `CMakeLists.txt`: `find_package(SDL2)`, OpenGL
   (`-lGL` / `-framework OpenGL` / `opengl32`), an explicit source list (no
   `glob src/*.c`), output
   `maniac_rebuild`, `-DMANIAC_DATA_DIR`, `-Wall -Wextra
   -Wpointer-to-int-cast`. Linux deps: `libsdl2-dev libgl1-mesa-dev`;
   macOS: `brew install sdl2`; Windows: vcpkg/fetchcontent SDL2. Keep the
   old MinGW `Makefile` until the native CMake build is established. Add any
   temporary Linux-compatible declarations/adapters and documented safe
   `TODO` stubs in `src/stubs.c`/`src/stubs.h` needed for all selected sources
   to compile and link; do not leave unresolved platform symbols or use
   Windows APIs in the Linux target. Each stub must state its contract and
   safe temporary behavior.

Verify: clean native Linux x86-64 compile and link; existing applicable unit
tests pass. Game launch is not required yet. Record which temporary
compatibility implementations remain and their contracts.

## Phase 2 — SDL2 window, input, time, dialog

New `src/platform_sdl2.c/.h`; edits to `src/maniac.c`, `src/time.c`,
  `src/util.c:fatalError`, and the minimal game-data path setup in `src/game.c`.

- Entry: `WinMain @0x4160a0` → `int main(int argc, char **argv)`.
  SDL2 window 640×480 + GL 3.3 core context. `PeekMessageA` loop →
  `SDL_PollEvent`. Resolve the asset dir from `argv[0]` / `MM_DATA_DIR` /
  `SDL_GetBasePath()` and use it for asset reads; if legacy code temporarily
  requires the install directory as CWD, do not let that redirect writes.
  Route config/temp writes to a user-writable directory (for example,
  `SDL_GetPrefPath`) before launching `gameInit`; Phase 5 completes the
  canonical filesystem boundary and fixes all remaining write paths.
- Keyboard: map SDL scancodes to channels Right 0 / Left 1 / Up 2 / Down 3 /
  Space 4 / Enter 6 / Escape 7 in `g_abInputKeyHeld`. `SDL_KEYDOWN/UP`
  replaces `WM_KEYDOWN/UP`; `SDL_TEXTINPUT` (`SDL_StartTextInput`) replaces
  `WM_CHAR`, routed to `gameKeyHandler @0x40db80` / `g_pStateFunc` exactly
  as `WindowProc @0x4161b0` did.
- Focus: `SDL_WINDOWEVENT_FOCUS_LOST/GAINED` reproduces `WM_ACTIVATE @0x4161f6`
  (`g_nFrameDue @0x459cd4` freeze, `gxSnooze @0x433330` once on deactivate,
  `GxMode` + `gxInit @0x4332f0` re-init on reactivate with the clock-cache
  save/restore).
- Time (`src/time.c`): `timeGetTime()` → `SDL_GetTicks()` (or `SDL_GetTicks64`
  truncated compatibly); `timeBeginPeriod/EndPeriod` → no-ops (keep
  `@0x40dfd0/@0x40e030` address comments noting the deviation).
  `mciPlayCdaudio @0x416cc0` / `mciStopCdaudio` stay documented silent stubs
  (CD audio out of scope); drop the `HWND` parameter residue.
- Dialog (`src/util.c:fatalError @0x414570`): `MessageBoxA(g_hWnd,…)` →
  `SDL_ShowSimpleMessageBox` + stderr log + `exit(-1)`.

Verify: compile and link on Linux x86-64, then launch with a real display and
OpenGL 3.3-capable context (desktop display or Xvfb with Mesa software
rendering).
Confirm the window, `gameInit @0x409d90`, frame loop, and input behavior; do not
use SDL's dummy video driver as an OpenGL or launch test.

## Phase 3 — OpenGL 3.3 backend (simple, good-looking)

New `src/gx_sdl2_gl.c/.h`, implementing the `g_driver.api` slots. Edits to
`src/gx.c` limited to deleting the `LoadLibraryA/GetProcAddress/FreeLibrary`
loader; `gxLoadDriver(NULL)` creates the backend, `gxUnloadDriver` destroys
it. `GxMode.hInstance/hwnd` become ignored fields.

- Before implementing the backend, document and verify the existing GX
  boundary contracts: texture versus surface handles and ownership/freeing,
  palette and color-key behavior, vertex formats, viewport/scissor, origin,
  clear, blending, and ordering/state transitions. Treat intentional port
  differences as deviations, not as assumptions about the original.
- Pipeline: one shader program (ortho MVP for 640×480 + `RGBA8`,
  `GL_NEAREST` sampler + per-vertex color), one `GL_DYNAMIC_DRAW` VBO
  (~64K verts) + one VAO. Draw functions (`pDrawPolygon`, `pDrawTriUV`,
  `pDrawQuad`, `pBlitSurface` fullscreen quad) convert 8.8 fixed-point `x/y`
  and UVs (`/256.0f`, `nSoftwareMode = 0`) and **append** triangles to a
  CPU-side batch; flush in command order at every draw-affecting state or
  ordering boundary (including clear, viewport/scissor, origin, blend, texture,
  and surface operations), then swap at `pFlip`. Handle batch-capacity overflow
  by flushing safely rather than dropping or overwriting vertices. Uploads may
  be coalesced where that preserves the recorded contract; never defer work
  across a state boundary that changes its meaning.
- Textures: each 256×256 8-bit indexed `.tpg` + its own 256-entry palette is
  expanded to RGBA8 at `pLoadTexture` time; `int handle → GLuint` registry
  preserves the original `int` texture ABI. The `g_nGfxMode==1` RGB555
  fullscreen path (`tgaLoad16`) converts directly. No palette-LUT shader, no
  `RGB555`/dither LUTs.
- State: `pSetViewport → glViewport/glScissor`, `pClearScreen → glClear`,
  `pSnooze/pResetState` trivial, `nDriverActive` accounting preserved.
  Textured/opaque/alpha maps to a single blend on/off (`glBlendFunc`);
  color-key and exotic `pSetOrigin` bits are not replicated. Depth test off;
  scene draw order (already sorted by the game) defines occlusion.
  Perspective-correct UVs are used; the original's affine warps, float
  rounding streaks, backface-cull `<1` rule, `z>=0` near test, bucket painter
  sort, and half-texel paths are intentionally not reproduced.
- `pDrawTriangle/pDrawLine` (no-op `RET` stubs in GXSOFT) stay no-ops.
- Backslash asset literals (`DRIVERS\…`, `menu\…`, `sound\…`) resolve through
  the Phase 5 path normalizer or are rewritten to `/`.

Verify: six-logo intro → 5-row menu → type/char/map select render; mall +
  carts + HUD in gameplay; screenshot-compare vs. Wine reference via the
  existing `XDOTOOL_NAVIGATION.md` flow (close match expected, not
  pixel-identical). Run using a real display/OpenGL context, not a dummy video
  driver, and record visible differences from the original renderer.

## Phase 4 — SDL2 audio (full mixer replacement)

New `src/audio_sdl2.c/.h`; rewrite the DirectSound block in `src/sound.c`
(`g_pDSoundObj` globals, `dsoundInitMixer @0x439050`, `sndGetWriteRegion
@0x438cd0`, `sndMixAdvanceUnlock @0x438f20`, `sndClearMixBuffer @0x438bb0`,
wavetable `sndBuildWaveTable @0x4381c0`, mix-core `sndMixVoiceCore @0x4395b0`
/ `sndMixStep` / `sndMixSamples`, scratch→buffer conversion, 4-int region
descriptor, `WAVEFORMATEX/DSBUFFERDESC`, `IDirectSound*` calls,
`DirectSoundCreate/SetCooperativeLevel`, play-cursor/write-ahead ring).

- `audioInit()`: open one SDL2 device and honor its obtained sample format,
  channel count, and rate (convert/resample as required); do not assume the
  requested 44.1 kHz stereo format was granted. The SDL audio callback owns
  time-critical mixing independently of frame ticks. Define synchronization
  for callback-visible voice/emitter state and safe behavior if no audio device
  is available. `sndLoadWav @0x437420` keeps the RIFF parser (8/16-bit mono
  PCM → device samples); delete the 16→8 downsample and 64-entry
  envelope-profile code only after verifying there are no remaining consumers.
  `sndLoadBankFromDir @0x437170` enumerates via the Phase 5 directory helper
  (digit-prefix + `.WAV` extension handling, bank/slot semantics unchanged).
- Voices: `sndPlaySfx @0x437cf0` starts one SDL voice per cue (volume
  0..0xffff → gain, pitch → simple resample ratio). `sndMixTick @0x437c50`
  retains its original caller-visible role; callback mixing must not make voice
  progress frame-rate-dependent. Preserve looping, pitch, volume, emitter
  update/free, completion, and reclamation semantics, using synchronization so
  emitters/voices cannot be freed while the callback uses them. The
  `SndEmitter @0x42bcd0` API stays, but gain/pan uses a simple distance+pan
  formula instead of the `sndVolFromPos @0x4389a0` sqrt/pan-mode tables.

Verify: compile and link on Linux x86-64; exercise menu/gameplay sound through
a real audio device. Test looping, pitch, volume, emitter updates/freeing, and
completion/reclamation; test the mixer core offline without using SDL's dummy
audio driver. Do not use SDL dummy video or audio drivers for verification.
Document behavior when a device cannot be opened. No
`dsound.h/DirectSound` dependency remains in the native target.

## Phase 5 — Filesystem, tests, docs, Ghidra sync

- Paths: promote `fopen_normalized (util.c:19)` to the canonical resolver for
  separator normalization and data-directory resolution only. Do not lowercase
  paths or add a case-insensitive fallback: inspect the actual assets and fix
  literal and dynamically constructed asset names to match their exact casing.
  Separate read-only asset lookup from writable user config/temp data; resolve
  `maniac.cfg`, the hardcoded `record.c:67` config path, and `sommar.sol` to
  appropriate user-writable locations rather than requiring writes beside
  installed assets. Provide a directory-enumeration abstraction with
  platform-specific implementations (not POSIX `dirent.h` alone), and test
  exact-case asset names.
- Build: drop `-m32 -mwindows -lkernel32 -luser32 -lgdi32 -lwinmm -ldsound`;
  retire the legacy MinGW `Makefile` path once CMake compiles, links, and runs
  the Linux target. Linux remains the active development and verification
  platform; defer Windows/macOS build and runtime verification rather than
  presenting them as already supported.
- Tests: adjust `tests/*.c` off Win32 (`Z:\` asset path in
  `test_event_item_positions.c` → CMake data-dir variable); add tests that
  do not require an SDL dummy driver. Use a real display/OpenGL context for
  graphics smoke tests and a real audio device for audio integration tests;
  keep suitable format/mixer unit tests runnable offline.
- Keep Ghidra as the reference for the original binary. Update it when
  investigation discovers original-binary facts (types, structures, names,
  comments, or behavior); document intentional SDL/OpenGL port deviations
  separately rather than treating them as original behavior.
- Docs: use `docs/17-cross-platform.md` for future documentation in addition
  to this file. Do not document what this file already says.

## Suggested order (every numbered phase and integration subphase compiles and links)

1. Phase 1 → Linux x86-64 CMake compile/link + applicable tests; document
   temporary compatibility implementations.
2. Phase 2 → Linux compile/link, then real-display launch and input test.
3. Phase 3 (2D: textures/blit/flip/clear/polygon) → intro + menu screenshots.
4. Phase 3 (3D: triUV/quad in gameplay mall) → level screenshots.
5. Phase 4 → audio contract tests + real-device sfx check.
6. Phase 5 → exact-case paths, user-writable config/temp paths, cross-platform
   filesystem boundary, docs, and Ghidra synchronization. Add Windows/macOS
   verification later when those environments are available.

## Verification gate

- At the end of every numbered phase and integration subphase,
  `cmake -S . -B build && cmake --build
  build` compiles and links on Linux x86-64. Launch is required at the Phase 2
  and Phase 3 integration milestones using a real display and GL 3.3-capable
  context; navigate intro → menu → map → gameplay via
  `XDOTOOL_NAVIGATION.md` (or SDL synthetic events).
- `grep -rn "windows.h\|dsound.h\|mmsystem.h\|DirectSound\|LoadLibrary\|FindFirstFile\|MessageBoxA\|timeGetTime" src/` is clean (except address-comment history notes).
- Keep simulation/math behavior separate from the rendering port, but do not
  assume changed rasterization is behavior-neutral. Compare real menu and
  gameplay paths against the reference and document known visual/audio
  differences. Verify Windows/macOS only when target environments are
  available; Linux verification alone does not establish support on those OSes.
