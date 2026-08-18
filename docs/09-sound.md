# Mall Maniacs (maniac.exe) — 09. Sound subsystem

[Back to README](README.md)

Status:

The original sound system is statically reconstructed at the subsystem level.
It combines DirectSound output with a software mixer for WAV sound effects and
MCI CD-audio playback for music. Sound is not yet part of the rebuild: the
current offline GUI milestone intentionally defers DirectSound and related
runtime integration.

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

## Evidence and limitations

The findings are based on Ghidra call paths, data references, renamed sound
functions, and the original files under
`/home/wasd/MallManiacsUnmodified/`. The bank layout, load paths, queueing
behavior, key call sites, and CD-track selection are verified; the detailed
voice-rendering and DirectSound buffer behavior remains a closed static
reconstruction and has not been validated with runtime audio tracing.

## Next direction

Keep sound deferred while the rebuild advances through the GUI and gameplay
states. When a working single-player state requires audio, first add a narrow
sample-bank and playback interface using the verified WAV naming and bank
semantics, then validate positional voices and CD/music behavior separately;
do not pull DirectSound into the rebuild before that dependency is needed.
