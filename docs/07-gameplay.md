# Mall Maniacs (maniac.exe) — 07. Gameplay / objects / players

[Back to README](README.md)

Status:

The core single-player gameplay model is mapped and renamed in Ghidra. Player
records, carts, item collection, AI steering, zone navigation, level event
directors, and the relevant on-disk `.ai`/`.eo` data have verified call paths
and layouts. This document is now a progress overview; detailed `.sen`, `.ai`,
`.eo`, and animation format notes remain in [10-fileformats.md](10-fileformats.md)
and [14-animations.md](14-animations.md).

## Purpose and scope

This subsystem connects the per-frame gameplay flow to the scene and data
models: `gameWorldUpdate` (`0x40b3d0`) drives `gameUpdate` (`0x426ee0`), which
updates players, physics, AI, thrown items, units, and scene channels. The
round setup/teardown path loads the level scene and configuration, binds item
and event objects, builds navigation data, and dispatches the per-level event
director from `roundLogicUpdate`.

The primary evidence is Ghidra decompilation and xrefs, corroborated by the
original `maniac.cfg`, `.sen`, `.ai`, `.eo`, and animation files. Names and
structures below are conclusions from that evidence; unverified details are
not presented as final behavior.

## Current gameplay model

- **Players and scene objects:** `g_playerRecords` (`0x456210`) contains eight
  records of stride `0x374`; `g_apPlayers` (`0x456360`) is the player view.
  The record is also the player `SceneObject`, with the scene-object base at
  player view minus `0x150`. The AI controller is at player `+0x1c4`, and the
  movement state is at `+0x314`. Verified fields cover character/local state,
  score and progress, ten-slot shopping lists, collected flags, cart/held-item
  state, animation state, and local versus AI control.
- **Walk/cart representation:** Each player has walk, position, and cart
  `WorldNode` sub-objects. Child meshes select named channels; channel values
  are averaged for height and angle, then copied between cart and character
  nodes so the mesh follows the cart. `playerSetupRound` (`0x410e90`) loads
  character/cart assets and computes movement constants from `[master]`, cart,
  and character statistics. Grab/release cart and drop/collect item actions
  are routed through the original command/action path.
- **AI:** `playerUpdateAI` (`0x40b510`) runs the mapped `AiController` states:
  choose a target, approach it, grab an object, put it in the cart, release or
  grab the cart, and return to the goal. `aiSteerToTarget` (`0x401ae0`) and
  `aiPathfindToTarget` provide steering and waypoint selection. The `.ai`
  files contain a separate navpoint graph used by the cost-relaxation path
  search; the scene-object AI navigation graph and the zone-wall/movement-mesh
  graph are separate systems. Collision snaps, node-channel synchronization,
  and camera wall avoidance are part of the same update chain.
- **Shopping and targeting:** `.eo` line records are closed zone polygons.
  `gameObjectUpdate` (`0x40cf40`) resolves the player’s current zone, selects
  the nearest uncollected list item, and retargets through `EXI*` connectors
  when the target is in another zone. After all required items, the target is
  `goal`. Level 4 uses `ARE1`/`ARE2`/`ARE4`; the other levels primarily use
  `PUNK`. Thirty configured item slots are bound to EventObjects at round start;
  mode 4 uses the alternate item id range and `CHECKFLAG` assets.

## Distinct object systems

These systems interact but must not be merged during reconstruction:

1. **EventObject registry:** a hashed `0x50`-byte object with numeric or packed
   four-character ids, values, height bounds, and linked polygon lines. It is
   loaded from `.eo` config sections and queried by id and occurrence. The
   `0x1f` Burger EventObject is deliberately skipped during ordinary `.eo`
   loading.
2. **Scene scenery:** `.sen` `OBJI` records create scenery objects with mesh
   arrays and animation operations such as position, orientation, and
   sub-mesh updates. The event animation stream is a positioning stream, not a
   general object-spawn format.
3. **Thrown/pickup items:** a separate bounded linked list updates gravity and
   bounce; a landed item creates a temporary pickup EventObject. Cart adoption,
   release, held-item meshes, and collection are updated in `gameUpdate`.
4. **Unit objects:** the `obj*` list supplies the independent gameplay-unit,
   collision, turret, and shot operations used by on-foot control and turret
   orientation. It is not the EventObject hash or the player scene-object
   array.

## Level directors and bonus item

Five per-level directors are initialized by `levelDirectorInits` (`0x40bdf0`),
dispatched by `roundLogicUpdate`, and cleaned up by `roundTeardown` (`0x40aa10`).
Their shared verified presentation uses the cashier, MCDMAN, Burger, signs,
and animation assets; Future Mall additionally creates ambient emitters. The
sequence is time-gated by `obj_update_time` and an idle-frame counter, not by
score: it animates MCDMAN, throws the Burger, then registers a class-`0x1f`
pickup. `playerAiGrabItem` (`0x40ea20`) attaches that item to the player,
removes the temporary EventObject, and the goal delivery completes the bonus
flow and re-arms the director.

The verified level catalogue is: ICA (`ica.eo`, goal), Woodshop
(`woodshop.eo`, `PUNK`), Orient (`orientmall.eo`, `IN`/`AR` room graph), Aqua
(`aqua.eo`, `PUNK`, checkout/conveyor zones), and Future
(`futuremall.eo`, `ARE*`, exits, teleports, and special zones). The startup
commands load each level’s `.ai` and `.eo`; teardown releases navigation,
thrown-item, detail-grid, and director resources.

## Quest subsystem

The Frögesporten trivia path is separately verified: `questLoad` (`0x40ffa0`)
parses XOR-`0x55` `quest.txt` records into a linked list, `questPickRandom`
selects by category, and the centered wrapped-text helpers render the result.
Records are destroyed during round teardown.

## Limitations and next direction

This is static reverse engineering, not a claim that every gameplay path has
been dynamically replayed. Some low-level fields and exact tuning constants
remain implementation details, and network synchronization, sound, and the
in-game console are outside the rebuild scope.

The rebuild currently stops at the offline GUI/menu vertical slice (see
[16-rebuild.md](16-rebuild.md)); gameplay has not yet been ported. After the
next GUI state, the useful gameplay milestone is a single-player round slice:
load one level’s scene/config/`.ai`/`.eo`, construct one local player and its
nodes, run walk/cart physics and item targeting, then add camera, HUD, event
director, and teardown behavior incrementally. Preserve the original call
hierarchy and keep every reimplemented signature synchronized with Ghidra.
