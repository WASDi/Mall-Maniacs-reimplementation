# Mall Maniacs (maniac.exe) — 10. File formats

[Back to README](README.md)

## Current state

The game-data directory `/home/wasd/MallManiacsUnmodified/` is the primary
evidence set: five level directories plus menu, sound, driver, and animation
assets. The formats below are sufficiently understood to guide loader
reconstruction; detailed config and animation internals remain in
[06-config.md](06-config.md) and [14-animations.md](14-animations.md).

## Verified formats

- **`.sen` scenes** — `sceneLoadSen @0x432320` reads a `REV2` header, total
  size, then little-endian `{tag, size, data}` chunks. Shipped scenes contain
  `MESH`/`NAME` tables, `SUBO`, `COLS`, `MAPI`, `TNAM`, `OBJI`, and `ONAM`; the
  loader also recognizes `TANI`, `KEEP`, `TEMP`, `LEEP`, and `PBJI`. `OBJI`
  entries are 32 bytes: type, class, short position, and mesh index. The
  name-table helpers at `0x431cb0`–`0x432060` resolve mesh and object names.
  Chunk payloads are identified, but the complete geometry and collision
  payload schemas are not yet reconstructed.

- **`.eo` EventObjects** — verified as DFF/config-tree text through
  `eloadCmd @0x406cb0`, `esaveCmd @0x4072a0`, and `eventObjAddLine @0x4147c0`.
  A section name is the object id; `name[N]` declares anonymous polygon
  sub-objects sharing that id. `x`, `y`, `h`, and `h2` define the center and
  vertical bounds, `v` values carry object-specific state, and `line[M]`
  records form closed 2D zone polygons. The level catalogue and consumers are
  known: `goal`, `PUNK`, `ARE1`/`ARE2`/`ARE4`, `EXI1..4`, and `AR##`/`IN##` room
  graph ids. The five shipped levels were inspected; a legacy object id
  `0x1f` is skipped by the loader.

- **`.tpg` textures** — verified byte-for-byte and in `gxSoft.dll`:
  every file is `0x10400` bytes with no header, containing `256*256` 8-bit
  indices followed by 256 RGBA palette entries. Real palettes use alpha
  `0xcd`. `gxLoadTexture @0x100019b0` copies both regions into its texture
  node; the maniac wrapper is `0x004333d0`. This is the format already used by
  the rebuild's GXSOFT rendering path.

- **`.anm` animations** — the layout and playback are fully decoded; see
  [14-animations.md](14-animations.md). The loader accepts `ANM` versions 1
  and 2; all 20 shipped files are version 2 and use 16 named channel values
  plus per-frame opcode records. Runtime loading begins at `anmLoadFile
  @0x433a50`, and playback is `sceneObjectAnimStep @0x434540`.

- **`.ai` navigation** — verified as plain DFF/config text loaded by
  `nloadCmd @0x407e10`. Each four-digit block creates a `0x60`-byte navpoint
  with world position and up to eight links. Shipped counts are ICA 121,
  Woodshop 142, Orient 293, Aqua 192, and Future 253. Links feed the
  navpoint nearest-node and Dijkstra-style pathfinding helpers, but the editor
  is out of scope for the current rebuild milestone.

- **`.tga` images** — intro logos use the image loader at `imageLoadByMode
  @0x4102e0`; `tgaLoad16 @0x415df0` handles raw 16-bit TGA data and
  `tgaLoad16Pal @0x415ec0` handles the palette-conversion variant. `.tpg`
  conversion for texture-backed images is dispatched by `gxLoadTpgFile
  @0x416060`.

- **`maniac.cfg` and saves** — `maniac.cfg` is XOR-`0x55` DFF text describing
  menu players, the five levels, eight start positions per level, items,
  object/character scenes, animation names, carts, camera, and master tuning.
  Each level startup loads its `.ai` and `.eo` files. The on-disk save is
  `config.mm`, also XOR-`0x55`; `sommar.sol` is the temporary plaintext copy
  created during load/save and then deleted. This corrects the earlier mistaken
  claim that `sommar.sol` itself was obfuscated; details are in
  [06-config.md](06-config.md).

## Remaining evidence and limitations

`quest.txt` and `sound\\menu\\` are catalogued as game-data evidence, but their
formats and runtime contracts are not a current reconstruction target. The
`.sen` geometry/collision payloads, the exact animation channel-to-body-part
map, and animation opcodes 1–4 remain incomplete or partly hypothesis-driven;
the shipped character files exercise only opcodes 5 and 6. No format claim
here substitutes for runtime validation against the original loaders.

## Next direction

Keep the verified `.tpg` path as the asset-format baseline while the rebuild
advances through GUI states. When gameplay loading begins, implement the
shared config parser first, then load one level's `.sen`, `.ai`, and `.eo`
together, validating object ids, navpoint links, and scene-name resolution
against the original call chain. Defer undocumented payloads and editor-only
commands until a visible single-player feature requires them.
