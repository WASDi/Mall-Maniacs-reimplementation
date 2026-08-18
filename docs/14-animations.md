# Mall Maniacs (maniac.exe) — 14. Animations (.anm)

[Back to README](README.md)

## Status

The `.anm` format and the player playback path are reconstructed to a useful,
evidence-backed level. The on-disk layout and runtime representation match all
20 shipped files in `/home/wasd/MallManiacsUnmodified/anim/`; animation support
has not yet been added to the rebuild.

## Purpose and evidence

`.anm` files contain character and cart keyframes. Each keyframe is a sequence
of records that updates skeletal sub-position channels and, for some records,
an object's position or orientation. The reconstruction comes from
`anmLoad @0x433a90`, its sizing pass `anmCalcSize @0x433f40`, and the runtime
decoder `sceneObjectAnimStep @0x434540`; the loader/refcount helpers are
`anmLoadFile @0x433a50`, `anmFree @0x434050`, and the `anmSet*` functions near
`0x4344d0`.

## Verified format and runtime

- Shipped files begin with `ANM` and version `2`. The loader accepts versions
  `1` and `2` and currently parses them identically.
- The header contains a mesh-name table, a named channel table, and a sequence
  of tracks. A track is one runtime frame: its records are applied together,
  then playback advances to the next track. The loader allocates the animation
  and frame/record data from one shared pool.
- Shipped files have no mesh-name entries, sixteen channel entries named
  `roland` or `roland_4`, and use only record types `5` and `6`. Type `5` writes
  three signed 16-bit values to a selected sub-position channel; type `6`
  writes an object's three-component position. All bytes in every file are
  accounted for, with no padding.
- The loader stores the frame index/count, frame headers, bound character mesh,
  optional master node, and position/facing targets. `sceneObjectAnimStep`
  processes a frame, advances or loops when requested, and applies the master
  node targets. Position records use the runtime's verified Y/Z sign conversion.

## Verified player playback chain

`playerSetupSceneObjects @0x411550` loads eleven player animations and binds
each holder to the character mesh and two named sub-meshes. The per-frame driver
`playerAnimSfxUpdate @0x40c800` resets, steps, and holds animations through the
state at `player+0x2f0`; it alternates run/stand playback and updates observed
limb channels `1, 2, 3, 4, 6, 7`. Holding-goods playback and interpolated winner
playback are also present. `roundStartInit @0x40a4d0` supplies the start pose;
event animation helpers include `eventAnimReset @0x434270`,
`eventAnimApply @0x434290`, and `eventAnimStep @0x434090`.

## Limitations and next direction

The exact sixteen-channel-to-body-part map, the relationship between the three
mesh slots and those channels, and the player state-to-animation mapping remain
open. Record types `1`–`4` are inferred from the runtime decoder rather than
shipped-file observations; they likely cover master-node targets and
mesh-bound position/orientation records used by director or event animations.
There is no version-1 sample to establish a difference from version 2.

Next, use director/event call sites and any corresponding assets to validate
record types `1`–`4` and resolve the channel/state mappings. When gameplay is
implemented in the rebuild, add the loader and stepper behind the verified
interfaces rather than extending the current GUI milestone prematurely.
