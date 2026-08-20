# Mall Maniacs (maniac.exe) — 09. Sound subsystem

[Back to README](README.md)

Status:

The original sound system is reconstructed at the subsystem level. It combines
DirectSound output with a software mixer for WAV sound effects and MCI CD-audio
playback for music. The rebuild now reproduces the sample-bank and playback
interface (`src/sound.c`/`src/sound.h`) and plays menu sound effects through
the **same DirectSound streaming path as the original**: DirectSoundCreate +
SetCooperativeLevel + a streaming sound buffer, with per-frame
Lock/Write/Unlock of free write regions. Menu navigation/selection cues
(01_Buttons / 03_Miss2 / 04_kokko) are wired into `menuUpdate` and
`stateGameTypeSelect`.

## Purpose and verified reconstruction

- `sndInitSystem` (`0x437a30`) resets sample banks, initializes DirectSound and
  six secondary mix buffers, creates the software mix buffer, and initializes
  0x100 voice slots. `sndShutdown` (`0x437cb0`) releases the corresponding
  resources; initialization failure sets the mute flag.
- WAV effects are PCM mono 8- or 16-bit files. `sndLoadBankFromDir`
  (`0x437170`) loads digit-prefixed `.wav` files by their leading number into
  the sample-bank pointer table; `sndPlaySfx` (`0x437cf0`) queues requests and
  `sndMixTick` (`0x437c50`) drains and renders them each frame.
- Gameplay effects are loaded from `sound\` at round start and menu effects
  from `sound\menu\` during menu initialization. Both use bank 1, but the
  phases are disjoint because the round loader frees that bank first. On-disk
  evidence confirms the naming convention and missing gameplay indices 14 and
  29; requests for missing samples are silently dropped.
- Positional effects are connected through `sndPlaySfx3D` (`0x42bcd0`) and the
  music-module emitter system. Music is not file-based: `mciPlayCdaudio`
  (`0x416cc0`) plays CD tracks, with menu track 7 and intro track 8 observed in
  the state flow.

## Rebuild status (`src/sound.c`)

The rebuild implements the verified sample-bank and playback interface and
reproduces the original DirectSound streaming output (no WINMM replacement):

- `sndInitSystem` (`0x437a30`) calls `dsoundInitMixer` (DirectSoundCreate,
  SetCooperativeLevel, and a streaming secondary buffer negotiated over the
  six format configs, e.g. 44100/16-bit/stereo accepted under Wine), latches
  the format globals into `sndCreateMixBuffer` (rate table + 0x200-aligned
  wave table + scratch), and seeds the 0x100-slot voice free stack with
  `sndInitVoices` (`0x438160`). Init failure sets `g_bSoundMute` like the
  original. Called from `menuInit` with `(2,4,10)`.
- `sndLoadBankFromDir` (`0x437170`) loads digit-prefixed `.wav` files into bank
  1 from `sound\menu\`. `sndLoadWav` (`0x437420`) parses RIFF/WAVE chunks,
  down-converts 16-bit to 8-bit (keep high byte) and unsigned 8-bit to signed,
  and registers the sample in the 16x16 bank `g_apSndBank @0x45ec94`.
- `sndPlaySfx` (`0x437cf0`) pops a free voice slot (0x2e-byte records in the
  same layout as the original), stores sample/volume/pitch/flags and the
  position pointer `voice+0x24` = `&g_nMixRateDivisor` for `0x400` (the
  original's zeroed window that `sndVolFromPos` reads as a centered origin),
  and marks the voice active. Menu cues match the original: Up/Down → slot 1
  (`01_Buttons`), Enter → slot 3 (`03_Miss2`), game-type-select Escape → slot
  4 (`04_kokko`).
- Per-voice channel volumes are derived each render region exactly like the
  original `sndVoicePriorityUpdate` (`0x4387a0`): `sndVolFromPos` (`0x4389a0`,
  exact original math including the `0x640000/(dist+0x64)` factor and pan
  weighting) → `((posVol*master>>16)*gain)>>0x18` into the 8-bit
  `voice+0x28/+0x29`. For a centered full-gain menu voice this yields vol8 ≈
  63 and a peak per-sample product of ±8k — the same magnitude the original's
  wave-table lookup produces (verified: byte `0x7f`→original ±7874 vs rebuild
  ±8001). The previous rebuild hard-coded vol8 127 (2×), which is what made
  the menu cues sound twice as loud and clip as soon as two overlapped.
- `sndVoicePriorityUpdate` also culls the active chain: when more voices are
  live than the voice cap `g_nMixVoiceCap` (4 for the menu), the quietest
  voices are dropped so at most `cap` render. This reproduces the original's
  loudness-culling, bounding the worst-case overlap sum at ~4·8k = 32k, i.e.
  just under the int16 scratch ceiling like the original. `sndFixedMul`
  (`0x437ed0`, high 32 bits of the product) is used for the env-profile
  loudness index, as in the original. `sndBuildEnvProfile` (`0x437900`) now
  fills and normalizes all 64 average-delta profile entries; leaving those
  entries unset defeated this culling and allowed overlapping effects to
  wrap the signed scratch accumulator.
- `sndMixScratchToBuffer` (`0x4382f0`) preserves the original PCM format
  selector: the negotiated 16-bit stereo mode is signed and receives raw
  scratch samples, while unsigned 8-bit output receives the `0x80` bias (and
  unsigned 16-bit/ signed 8-bit variants use the corresponding original
  branches). Its no-voice path now emits format-appropriate silence too:
  signed PCM is zero, while unsigned PCM receives its midpoint. Writing
  `0x8000` to the signed 16-bit silence path created the audible start/stop
  impulse even though the rendered voice samples were correct.
- `sndMixTick` (`0x437c50`) runs each frame from `gameFrameUpdate` and mirrors
  the original call graph exactly: lock the next free write region
  (`sndGetWriteRegion` @0x438cd0, play-cursor + write-base free-space fence,
  buffer-lost restore), build the two voice chains once
  (`sndMixBuildVoiceChains` @0x437fe0), then loop until no region is free:
  `sndMixRenderRegion` (render one locked region), `sndMixAdvanceUnlock`
  (Unlock + cursor wrap).
- `sndMixRenderRegion` (`0x438090`) reproduces the original step order: latch
  the mixer master volume, `sndVoicePriorityUpdate` (volumes + culling),
  `sndMixScratchReset`, `sndVoiceUpdateFinished` + `sndVoiceRender`
  (render + finish), `sndVoiceAdvancePosition` (advance chain B),
  `sndVoiceReclaimFinished` (recycle finished voices), then
  `sndMixScratchToBuffer` twice into the two locked segment pointers (region
  offset/divisor math identical). The ring-write free-space math mirrors the
  original `@0x438cd0` logic.

`dsoundRelease` (`0x438ff0`), `sndFreeMixBuffers` (`0x438140`), and the
`mathFixedRecip`/`sndFixedMul` helpers complete the implemented slice. The
voice core is now fully reproduced as three faithful functions: `sndVoiceRender`
(`0x438b00`, builds the 7-dword `SndMixRec` from the voice slot + L/R wave-table
rows selected from the 8-bit volumes), `sndMixVoiceCore` (`0x4395b0`, computes
the fixed-point step from pitch/`g_nMixParams[1]` and loops `sndMixStep`),
`sndMixStep` (`0x4396a6`), and `sndMixSamples` (`0x4396ea`, the wave-table
accumulate inner loop). The `SndMixRec` struct and the four signatures are
mirrored in Ghidra. MCI CD-audio (the original's music path) is intentionally
not reproduced.

## Evidence and limitations

The findings are based on Ghidra call paths, data references, renamed sound
functions, and the original files under
`/home/wasd/MallManiacsUnmodified/`. The bank layout, load paths, queueing
behavior, key call sites, ring-write region math, and CD-track selection are
verified. The wine smoke test confirms DirectSound negotiation and the
Lock/render/Unlock region loop; the container's audio cursor does not advance,
so audible output itself is not traced there.

## Next direction

The menu sfx slice is complete. When a working single-player state requires
audio, extend the same sample-bank/playback interface to the gameplay `sound\`
bank (loaded by `roundStartInit` at `0x40a9a6`, which also frees bank 1 first)
and validate positional voices (`sndPlaySfx3D`, emitters, voice chains) and
CD/music behavior separately. The DirectSound output path and the software
scratch mixing are in place; only MCI CD-audio remains unreproduced.
