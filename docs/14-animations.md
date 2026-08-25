# Mall Maniacs (maniac.exe) — 14. Animations (.anm)

[Back to README](README.md)

## Status

The `.anm` format and the player playback path are reconstructed and now
reimplemented in the rebuild (`src/anim.c` / `src/anim.h`, Option A). The
on-disk layout and runtime representation match all 20 shipped files in
`/home/wasd/MallManiacsUnmodified/anim/`; the full loader + playback cluster
(`dataReadU8/U16/U32 @0x433ee0/0x433ef0/0x433f10`, `anmCalcSize @0x433f40`,
`anmLoad @0x433a90`, `anmLoadFile @0x433a50`, `anmFree @0x434050`,
`anmSetAlloc/Free/MeshSlot @0x4344d0/0x434500/0x434530`,
`eventAnimReset/Step/Apply @0x434270/0x434090/0x434290`,
`sceneObjectAnimStep/Interp @0x434540/0x4347c0`) is faithful and passes
`TrackRebuildDetailed` with 14/14 tracked-calls matching (no missing or
unexpected). The single-arena allocation (`0x38 + nTrack*8 + recordBytes`
via `g_pAnmCacheList @0x45ebc8` / `g_nAnmCacheCount @0x45ebcc`) and the
`ANM` v1|2 validation are preserved; `memPool*` calls remain in the call
graph (pool.c maps to malloc/free, satisfying the rebuild's malloc
policy). Ghidra structs `AnmFile` (0x38) / `AnmSet` (0x18) / `AnmTrack` (8)
and prototypes are synced (saved 2026-08-25). The charselect preview now
uses the faithful `anmLoad` → `eventAnimReset` → `eventAnimStep` path.

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

The sub-position/orientation helpers `sceneObjSetSubPos @0x430a90` and
`sceneObjSetSubOrient @0x431110` are currently faithful stubs (mode 2
absolute writes, delta for mode 1, no trig) — sufficient for the charselect
preview (types 5/6) and for call-hierarchy coverage via `eventAnimStep` /
`sceneObjectAnimStep`. Their full trig path (`mathSinDeg/CosDeg`,
`chanBuildRotMatrix`) is deferred. `scenNameToIdEx @0x431e20` is a faithful
stub returning 0 (shipped .anm have zero mesh-name entries).

Next, use director/event call sites and any corresponding assets to validate
record types `1`–`4` and resolve the channel/state mappings. When gameplay
is implemented, replace the two sub-channel stubs with the full trig path
and wire the `playerAnimSfxUpdate` chain to the eleven `anmSet*` holders.
