# Mall Maniacs (maniac.exe) — 14. Animations (.anm)

[Back to README](README.md)

## 14. Animations — .anm format + playback

The `.anm` files are the character/cart skeletal-animation data. Each is a
sequence of KEYFRAMES; every keyframe is a stream of opcode records that move
sub-mesh channels and (optionally) the object itself. Structure fully decoded
from `anmLoad @0x433a90` + `anmCalcSize @0x433f40` (loader), verified byte-exact
against all 20 files in `/home/wasd/MallManiacsUnmodified/anim/`, and cross-checked
against the runtime stream decoder `sceneObjectAnimStep @0x434540`.

Evidence status: on-disk layout + record sizes [VERIFIED]; opcode 5/6 semantics
[VERIFIED] (appear in files, confirmed in runtime decoder); opcode 1-4 semantics
[HYPOTHESIS] (never present in the 20 on-disk files; inferred only from the
runtime decoder); exact bone/channel→body-part map [OPEN].

## Loader cluster

- `anmLoadFile @0x433a50` — `fileReadRaw(0,name)` → `anmLoad` → free raw bytes.
  Args (param_2 = master node, param_3 = character mesh obj) stored in the
  resulting anim struct (see runtime layout).
- `anmLoad @0x433a90` — parses header/tables/tracks into one pool allocation;
  resolves names via `scenNameToIdEx @0x431e20`; bumps refcount `DAT_0045ebcc`;
  creates the pool `DAT_0045ebc8` (memPoolCreate) on first use.
- `anmCalcSize @0x433f40` — dry pass over the same structure to size the
  allocation: `0x38 + trackCount*8 + Σ(record sizes)`.
- Byte readers: `dataReadU8/U16/U32 @0x433ee0/0x433ef0/0x433f10`.
- `anmFree @0x434050` — refcount-decrements; destroys whole pool at 0.
- `anmSetAlloc @0x4344d0` — wraps an anmLoad result in a 0x18-B ref holder
  (anm ptr at +0x14). `anmSetMeshSlot @0x434530` = holder[slot]=mesh.
  `anmSetFree @0x434500` = anmFree(holder+0x14) + memPoolFree(holder); called
  from `roundTeardown @0x40aa10`.

## On-disk format [VERIFIED]

All 20 files are version 2, header `41 4e 4d 02` ("ANM" + 0x02); the loader
accepts version 1 or 2 and parses both identically.

```
+0x00  "ANM"           magic (3 bytes)
+0x03  u8              version (1 or 2; all shipped files are 2)
+0x04  u16             tableA count         (0 in all 20 files)
       tableA entries: { u8 len, name[len] }          -- mesh-name table
       u16             tableB count         (16 in all 20 files)
       tableB entries: { u8 len, name[len], u16 val } -- named channel table
       u16             frame/track count
       per frame:
         u16           record count
         records       opcode byte + payload (see below)
```

- tableA (count = 0 here) is resolved to scene ids via `scenNameToIdEx` and
  used only by record types 3/4 as the mesh reference.
- tableB is 16 entries `roland` (idle/action anims) or `roland_4` (walk/run
  anims) with values 0..15. The VALUE is the runtime sub-position channel
  index (record type 5); the names resolve via `scenNameToIdEx` but are not
  otherwise consumed by the loader — likely vestigial labels for the 16-part
  character skeleton.

### Record types (on-disk → runtime stream)

| op | on-disk bytes | → runtime stream | runtime opcode (sceneObjectAnimStep) |
|----|---------------|------------------|--------------------------------------|
| 1  | 1+3×u32  = 13 | 0x10 | pos target → state[8..10] (master node; negate cols 2,3) |
| 2  | 1+3×u32  = 13 | 0x10 | facing target → state[0xb..0xd] (master node; negate 2,3) |
| 3  | 1+u16+3×u32 = 15 | 0x14 | `sceneObjSetPos` each listed obj; +4 = tableA[idx] mesh id |
| 4  | 1+u16+3×u16 = 9  | 0x10 | `sceneObjSetPosOrient`; +4 = tableA[idx] mesh id |
| 5  | 1+u16+3×i16 = 9  | 0x08 | `sceneObjSetSubPos(channel=byte@+1, 3×i16)` |
| 6  | 1+u16+3×u32 = 15 | 0x10 | `sceneObjSetPos` (3×u32; u16 discarded) |

Negated columns 2/3 (Z, Y) in ops 1-4/6: the on-disk Y/Z are stored positive;
the runtime applies world-space sign flip when writing node position.

### Runtime stream layout [VERIFIED]

`anmLoad` returns the 0x38-B anim struct (one pool allocation):

```
+0x00  int  frame index
+0x04  int  frame count            (= on-disk track count)
+0x08  ptr  frame headers          (loop start / first frame)
+0x0c  ptr  current frame header
+0x10  int  (0)
+0x14  ptr  pool DAT_0045ebc8
+0x18  obj  param_3 = character mesh object
+0x1c  obj  param_2 = master node (0 for players)
+0x20..+0x37  pos target [8..10], facing target [0xb..0xd] (runtime state)
+0x38  frame header array: frameCount × { u32 record count, ptr records }
      (records decoded contiguously after the header array)
```

`sceneObjectAnimStep(anmSetHolder*, loopFlag)` — consumed via `param_1[5]` =
anmSet holder +0x14 = this struct:
- per frame, processes `record count` records (the switch above);
- frame++, advances to next frame header (+8);
- at frame count: returns 1; loops (frame=0, ptr=+0x08) iff `loopFlag&1`;
- after the frame's records, snaps the master node (+0x1c) to the pos/facing
  targets set by ops 1/2.

So on-disk TRACK = runtime FRAME (keyframe). One frame = a set of simultaneous
channel moves. Record ops 5/6 dominate shipped anims; ops 1-4 appear only in
director/event anims.

### What the 20 shipped files contain [VERIFIED, parser]

All: tableA=0, tableB=16 ("roland"/"roland_4", values 0..15), only op 5 and 6
used. Observed frame counts: cash_sit 2, s_loser/s_oops 2, s_grab 4, s_pick2 4,
s_pick1 5, s_throw2 6, s_clap 8, s_pick 10, s_fl_pick2/s_flpick2 10,
s_throw1 10, s_fl_pick1/s_flpick1 11, s_run 16, s_winner 20, s_stand2 24,
stand 24, s_stand 36. (stand.anm = 24-frame variant, not the 36-frame s_stand
used by players.) File sizes 317-2627 B; every byte accounted for (no padding).

## Playback chain [VERIFIED]

- `playerSetupSceneObjects @0x411550` loads the 11 player ANMs
  (pick1,pick2,flpick1,flpick2,throw1,throw2,run,stand,grab,oops,winner) →
  anmSet holders at `player+0x2a8..0x2d0`, each bound to 3 mesh slots via
  `anmSetMeshSlot` (slot 0 = char mesh `player+0x30`, slots 1-2 = `N_<name>`
  sub-meshes `player+0x34/+0x38`). Called from `roundStartInit @0x40a8c2`.
- `playerAnimSfxUpdate @0x40c800` — per-frame anim driver (called from the
  player-update path):
  - anim-state machine at `player+0x2f0`: 1/4/7 → `eventAnimReset` +
    advance to 2/5/8; 2/5/8 → `sceneObjectAnimStep(loop=1)` + advance to
    3/6/9; 3/6/9/0xf → hold. Three anims × {reset, play, hold} phases; exact
    anim mapping [OPEN].
  - movement: alternates run holder (+0x2c0) / stand holder (+0x2c4) on the
    frame-parity global `DAT_00458954`; limb sub-channels 1,2,3,4,6,7 written
    from `sceneNodeGetChannelPos` of the cart-mesh node (`sceneObjSetSubPos`).
  - holding-goods branch (`player+0x158` bit 2) steps the +0x2cc holder.
  - results screen: winner (+0x2d0) via `sceneObjectAnimStepInterp @0x4347c0`.
- `roundStartInit @0x40a4d0` also steps `sceneObjectAnimStep` (start pose).
- Related steppers (event-system anims): `eventAnimReset @0x434270`,
  `eventAnimApply @0x434290`, `eventAnimStep @0x434090`.

## Open questions

- tableB "roland"/"roland_4": exact 16-channel → body-part map (channel usage
  seen: 1,2,3,4,6,7 in `playerAnimSfxUpdate`). "roland" is presumably a stock
  character skeleton name.
- Opcodes 1-4 never appear in shipped files — only director/event anims would
  exercise them (master-node walk targets + mesh-bound pos/orient records).
- The 3 mesh slots ↔ 16 channels relationship (how a keyframe moves meshes vs.
  node channels).
- `player+0x2f0` state machine values 1-9 ↔ which of the 11 anims.
- Version-1 vs version-2 difference (no v1 file to compare; loader treats
  identically).
