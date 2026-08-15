# Mall Maniacs (maniac.exe) — 07. Gameplay / objects / players

[Back to README](README.md)

## 9. Gameplay / objects / players
- Player struct [VERIFIED, structs created]:
  - `Player` (stride 0x374): +0x000 nCharIdx, +0x004 bIsLocal, +0x008 bFlags
    (bit 0x20 = heading to checkout), +0x00c nScore, +0x01c nProgress (0-100,
    HUD progress bar), +0x024 pObj (object handle, low byte = present),
    +0x030 nItemCount (Matkrig carried count, target 5), +0x034 nListItemId[10]
    (shopping-list item-name ids; also Vagnrace time slot), +0x05c
    nListCollected[10] (per-slot collected flags), +0x08c pFaceObj (avatar/
    face sprite), +0x190 fPosPrevX +0x194 fPosPrevZ (prev-tick net position),
    +0x198 nAnimState (net anim: 2|5|6), +0x1a0 nAnimBusy, +0x1b0 fPosX
    +0x1b4 fPosZ (network-decoded world position), +0x1bc nNetWait,
    +0x1c0 nActionFlags (OR'd net sub-opcodes), +0x18c nState
    (0=local, 2=ai), +0x1c4 aiCtrl (AiController).
  - `AiController` (at player+0x1c4): +0x00 pSceneObj, +0x04 nAiState (0-9:
    0 idle/scan, 1 approach, 2 cart action, 3 throw, 4 pickup, 5 release cart,
    6 return, 7 end, 9 done), +0x08 nPathResult (waypoint FUN_004259d0),
    +0x14/+0x18 fPosX/fPosZ, +0x1c fAngle, +0x20/+0x24 fTargetX/fTargetZ,
    +0x28 fPosY, +0x2c/+0x30 fWaypointX/fWaypointZ, +0x40 nObjMode,
    +0x48 nAnimFrame, +0x4c nAnimTimer, +0x144/+0x148 byte flags.
    (AiController fields indexed by playerAiUpdate @0x00401160 + playerUpdateDispatch
    @0x004010e0 which pass player+0x1c4. State machine DRIVERS: aiStateCartAction
    @0x401e40, aiStatePickup @0x401fb0, aiStateReturnHome @0x402060, throw
    @0x401eb0, aiPathfindToTarget @0x401800, approach @0x401ae0. syncAiAnimToSceneObj
    @0x4015a0 copies animFrame/animTimer from obj+0x2e0/+0x2e4 back into the AI.)
  - Cart system [VERIFIED]:
    - Config objects/carts[%d] (REDCART/GREENCART): object_name mesh,
      handle_pos, object_pos, friction. Cart mesh loaded per player in
      playerSetupRound @0x410e90 (also loads characters[%d]/object_name,
      copies char stats DAT_004501b4+charIdx*0xc, sets the 10-slot shopping
      list +0x34/+0x5c by mode: mode 4 ids i+0xc9 then 200, and precomputes
      per-player accel/turn constants from [master] acc/friction/rotate_acc/
      acc_WC/rotate_acc_WC).
    - Player scene object cart fields: +0x174 hasCart (0/1; position/angle/Y
      accessors sceneObjGetPosXZ @0x4099d0 / sceneObjGetAngle @0x409a90 /
      sceneObjGetHeight @0x409ad0 switch between walk mesh +0x224 / pos +0x264
      and cart mesh +0x2a4 sub-objects, +0x20/+0x24 = x/z in each sub-object),
      +0x17c held-item slot (1 elem), +0x180 mode, +0x184 cartIdx,
      +0x1f0/+0x270 float movement (turn-damping) constants (see below),
      +0x2e0/+0x2e4 animFrame/animTimer, +0x2e8 nMode (3 = skip grab).
      Sub-objects at player+0xd4/+0x114/+0x154 = 0x48-byte world nodes
      (worldNodeCtor @0x402a20: +0x20/+0x24/+0x28 pos, +0x30..+0x38 scale,
      +0x44 parent link (char-name string addr for nodes 1 and 3)).
    - CHANNEL / CHILD-MESH MECHANISM [VERIFIED]: each world node owns a child-mesh
      list at +0x08. Children are 0x4c-byte entries (nodeAddChildMesh @0x4053a0):
      +0x00 meshIdx, +0x0c channelKey, +0x10/+0x14/+0x1c/+0x20 local transform,
      +0x2c..+0x38 world coords, +0x40 height value, +0x48 next. A child's +0x0c
      key selects a "channel". nodeChannelAvgFloat @0x4050c0(node,key) averages the
      +0x40 height values over children with matching key (used for pos/Y/angle
      everywhere: net, items, AI). The player scene-obj header stores the channel
      keys at scene-obj +0x10 (g_apPlayers-0x140) = ANGLE channel and +0x14
      (g_apPlayers-0x13c) = HEIGHT channel; sceneObjGetAngle reads node
      +0x224/+0x2a4 with key +0x10, sceneObjGetHeight reads +0x264/+0x2a4 with
      key +0x14. nodeSetTransformFromChannels @0x404e00 rewrites child world
      coords from a node transform; while riding, syncCartNodeChannelsToWalkPos
      @0x40e040 copies the cart node channels into the walk/pos nodes so the
      character mesh follows the cart.
    - MOVEMENT CONSTANTS [VERIFIED — NOT pointers]: scene-obj +0x1f0 (walk mode)
      and +0x270 (cart mode) are FLOATS computed in playerSetupRound
      (player+0xa0/+0x120): walk const = (charStat0*c+b)*acc / (1 - friction),
      cart const = (charStat0*c+b)*acc_WC / (1 - (friction+cartFriction)*c).
      aiPathfindToTarget/FUN_00401ae0 multiply them by ai+0x44 speed to gate the
      turn (compared against node+0x3c). Per-player char stats (acc/turn/.., ints
      cast to float) live at player +0x10/+0x14/+0x18 from
      DAT_004501b4+charIdx*0xc (unlisted Player fields).
    - Actions via commandDispatch(obj, "action grab cart"/"action release
      cart"/"action drop item"); player console actions "grab cart"/"release
      cart"/"ride cart". playerFindCart @0x40e180 = can-grab test (walk-mesh
      distance + held slot empty). playerCollectItem @0x40f1f0 fills the
      collected-item array, turns the thrown item node into a bobbing held
      item (sceneObjSetClassMesh + random offset) while < 3 collected, else
      removes it; mode 3 counts only. objGetPos @0x40f1b0 = EventObject
      position helper (id, occurrence). objGetCheckoutPos @0x40f6e0 = "goal"
      EventObject position (aiStateReturnHome paths back to it).
  - Global `g_apPlayers` @0x00456360 typed Player[8]; count DAT_00458108,
    local idx DAT_00458104; playerSetupCharacters @0x0041b6e0 seeds charIdx +
    RNG, writes idx at player-base-4.
  - Per-player gameplay/AI update entry points [renamed]:
    playerUpdateAI @0x40b510 (per-player AI state machine driver, called from
    gameWorldUpdate @0x40b3d0), playerCheckTurn @0x40e670 (angle-normalisation +
    blocked-turn test vs _DAT_0044b2b0/_0044b2a8/_0044b2a0/_0044b29c/_0044b298,
    +0x174 flag), playerFindNearestTarget @0x40ed10 (nearest object type 0x1f),
    playerCheckTargetRange @0x40f4d0 (in-range test vs DAT_0044b570),
    playerCheckBlocked @0x40ec40 (uses playerFindNearestTarget),
    playerUpdateOrientToTurret @0x40e5b0 (reads turret angle via objPolarPosLookup
    + objListFindFloat + nodeChannelAvgFloat, writes via nodeSetTransformFromChannels;
    called from playerUpdateAI and netClientReceiveGameMsg @0x415420).
  - AI steering / state-machine cluster [renamed]:
    aiSteerToTarget @0x401ae0 (steering toward target, computes +0x2e0 turn dir
    + +0x2e4 speed, used by aiPathfindToTarget + playerAiUpdate); aiStateSetTargetItem
    @0x4015c0 (state 0 — scans item slots via objGetPos, picks nearest, sets
    target id + navpoint, uses navPointPickRandom/FindNearestInYRange for
    fallback); aiStateGrabObject @0x401eb0 (state 3 — pathfinds to target item,
    commandDispatch "action drop/get item", walks held-item slots +0x17c..+0x17f);
    aiStatePutObjectInCart (was aiStatePickup) @0x401fb0 (state 4 — drops item
    into cart via "action drop item"); aiStateReturnHome (state 7). AiController
    states fully mapped from AIMODE_* debug strings: 0=SETTARGETITEM, 1=GOTOITEM,
    2=GOTORANDITEM, 3=GRABOBJECT, 4=PUTOBJECTINCART, 5=RELEASECART, 6=GRABCART,
    7=GOAL, 9=done.
  - Movement-steering sub-object (`moveState`): moveStateCtor @0x401000 inits the
    0x60-byte block at player+0x314 (4 gxVec2 fields, flag byte at +0x51 set by
    moveStateSetSnapFlag @0x4020c0 after zone-collision snaps in
    playerUpdateWalkPhysics @0x426fd0 / playerUpdateCartPhysics @0x4280b0).
  - Camera follow: cameraFollowUpdate @0x4020d0 (moves camera DAT_004588f8 toward
    player with zone-wall avoidance via zoneAvoidWalls @0x4023e0);
    mathSegIntersectBounded @0x4028a0.
  - levelSceneTexturesLoad @0x4104b0 / configMasterLoad @0x410350 [renamed].
  - UNIFIED PLAYER/SCENE-OBJECT MODEL [VERIFIED — aiControllerCtor @0x401090]:
    the player record IS its scene object. Internal base = g_apPlayers[i] - 0x150
    (AiController pSceneObj = that base, computed as
    &g_apPlayers[i].aiCtrl - 3*0x149 + 0xc7). All scene-object fields below use
    scene-obj-relative offsets = g_apPlayers-relative + 0x150. The three world
    nodes allocated in playerSetupRound at g_apPlayers+0xd4/+0x114/+0x154 ARE
    the scene-obj +0x224/+0x264/+0x2a4 sub-objects.
  - Player record array + ctor chain [VERIFIED]:
    g_playerRecords @0x456210 = base of the 8 x 0x374-byte player record array
    (array-init chain playerArrayCtorInit @0x409c40 -> FUN_0043eb5d(&0x456210,
    0x374, 8, playerRecordCtor @0x409c90, dtor playerRecordDtor @0x409d30)).
    Player view at +0x150 = g_apPlayers @0x456360 (Player[8]); scene-object part
    at +0x0; moveState sub-object at +0x314 = first 0x60B of AiController @ +0x1c4
    (playerRecordCtor builds mStrings at +0/+8, 6 gxVec2 fields, moveStateCtor
    @0x401000). Array ends at 0x457db0 (adjacent to g_scenePathString).
    playerRecordInitDefaults @0x40a150, configStringDtor @0x407140.
    playerGetPos @0x409a10 = position getter dispatching to cart/walk sub-obj.
  - SceneObject (interactive/registry object, found via objFindById by id):
    Ghidra struct `SceneObject` (764 B) = the unified player record view
    (typed `SceneObject[8]` at g_playerRecords @0x456210). VERIFIED fields:
    +0x008 nId, +0x010/+0x014 nHalfExtentX/Z, +0x038/+0x03c flPosX/flPosZ,
    +0x048 pHashNext, +0x174 nHasCart, +0x17c nHeldItemId, +0x180 nMode,
    +0x184 nCartIdx, +0x1f0 flWalkTurnConst, +0x224 pWalkNode,
    +0x264 pPosNode, +0x270 flCartTurnConst, +0x2a4 pCartNode (WorldNode
    0x48-B sub-objects; playerGetPos @0x409a10 dispatches to cart/walk
    sub-obj), +0x2d8 nChannelsDirty, +0x2dc nCtrlType (2=AI), +0x2e0 flAnimFrame,
    +0x2e4 flAnimTimer (synced by FUN_004015a0), +0x2e8 nAnimState (queued
    action mode, 0=idle, 1-7 actionCmd), +0x2f0 nAnimBusy, +0x2f4 nAnimBusyReload,
    +0x2f8 nAnimBusyTimer (0xf = wait/anim-complete).
    sceneObjGetPosXZ @0x004099d0 returns {x,z} from sub-object at +0x2a4
    (when hasCart) else +0x224, each +0x20/+0x24.
   - FOUR object systems (don't conflate):
     1) EventObject / SceneObject registry [VERIFIED]: 0x50-byte block
        (Ghidra struct `EventObject`, 20 fields) + lineRecord struct (0x20 B).
        id-hash table @0x459500 (0xff buckets, bucket = id % 0xff).
        Layout: +0x00 nType (0), +0x04 pLineHead (lineRecord polygon list),
        +0x08 nId (numeric or packed 4cc), +0x0c bFlags byte,
        +0x10..+0x20 nValue0..nValue4 (ints; ZONE objects: nValue0/nValue1 =
        bbox half-extents X/Z read as `(float)*(int*)(obj+0x10)` in
        gameObjectUpdate's between-zones retarget test; PICKUP objects:
        nValue0 = spawn X int, nValue1 = 1 present marker),
        +0x24 pBackRef (pickup: back-pointer to thrown item node),
        +0x28..+0x34 reserved (0), +0x38 fPosX +0x3c fPosZ +0x40 fHeight
        +0x44 fHeight2 (zone vertical bounds, h/h2), +0x48 pHashNext
        +0x4c pHashPrev.
        API: sceneObjCtor3 @0x4146a0 (id,x,z,val), sceneObjCtor4 @0x414700
        (id,x,y,h,h2), objHashRegister @0x4148f0, objHashRehash @0x414850
        (change id, backs console 'ename'), objFindById @0x414a90
        (bucket chain, +0x08==id, occurrence index), objHashFirst @0x414a60 /
        objHashNext @0x4149f0 (iterate all buckets), objHashDtor @0x414760
        (recursive teardown: unlink bucket DAT_00459500[id%0xff], free +0x4 sub
        via objSubDtor @0x414680, recurse +0x48), objHashRemoveFree @0x414990
        (unlink from +0x48/+0x4c doubly-linked chain then dtor + delete).
        evalueCmd @0x407520 writes
        nValue0..4; eheightCmd @0x407660 writes fHeight/fHeight2 (h, h2, h2+h).
     2) sceneryObjAlloc @0x00430200 (from .sen OBJI): 0xa8 + nMeshes*0x70 block,
        +0x00 type=1, +0x03 mesh count, +0x04 class-llist next, +0x10 class ptr,
        +0x14 mesh array ptr, +0x20 type descriptor, +0x38/+0x3a/+0x3c shorts,
        +0x3e scale 1.0f, +0x44 angle, +0x48/+0x4c/+0x50 pos XYZ.
        Mesh ops (used by eventAnim keyframes): sceneObjSetPos @0x430660
        (mode 1=delta, 2=abs, 5=oriented), sceneObjSetPosOrient @0x4307d0,
        sceneObjSetSubPos @0x430a90 (sub-mesh idx *0x70 into +0x14 array),
        sceneObjResetFlags @0x430620, sceneObjSetClassMesh @0x430db0
        (class-list insert + set mesh index at mesh+0xc), sceneMeshFixup
        @0x4320f0 (.sen MESH pointer relocation).
        Scene-object animation command stream (per-frame step over opcode
        records; anim state @ state[5]: [0]frame [1]frames [2]loopStart
        [3]dataPtr [7]masterNode, opcodes 1=pos-tgt 2=facing-tgt 3/6=move
        obj(s) 4=move+orient 5=channel sub-pos, then snap master):
        sceneObjectAnimStep @0x434540 (absolute; playerAnimSfxUpdate +
        roundStartInit) and sceneObjectAnimStepInterp @0x4347c0 (lerp-halfway
        variant, results-screen winner only). Both target up to 5 objects
        (0-terminated float/int id list).
     2b) sceneTextAnim manager (scrolling/blinking scene signs; glyph record
        stream: [0]startChar [1]charIdx [2]width [3]frameCnt [4]glyphCount
        [5]loopChar [7]repeat + glyph idxs, glyph = 0x10-B block pixels at
        +8..+0xf): sceneTextAnimReset @0x434a50 (clear 16 slots + active flag),
        sceneTextAnimAdd @0x434a90 (register slot; called from sceneLoadSen
        per animated texture), sceneTextAnimUpdate @0x434b00 (gameWorldUpdate
        tick, +1 frame/step), sceneTextAnimClose @0x434bf0.
        Globals g_apSceneTextAnimData/Owner/Glyphs + g_anSceneTextAnimSize
        @0x45ec10/0x45ec50/0x45ec90/0x45ebd0 (uint[16] each), g_bSceneTextAnimActive
        @0x45ecd0. roundStartInit wraps the level texture-load pass with
        Reset...Close.
     3) Thrown/pickup item nodes: linked list DAT_0045896c (next at +4),
        per-frame itemThrowUpdate @0x40f950 (gravity/bounce; on land spawns a
        registered EventObject pickup via sceneObjCtor3 + objHashRegister;
        proximity pickup -> net sub-cmd 0x3d + sfx 0x16, frees node, clears
        player+0x94 "item thrown" flag). thrownItemFree @0x40f8d0 (list unlink;
        list heads g_pThrownItemHead/Tail/Count @0x45896c/0x458970/0x458974,
        max 10). itemMeshFollowUpdate @0x40fde0 (gameUpdate 2nd item pass: clamp
        rest height, re-align scene mesh +0x24 to obj node +0x10). Cart/item net
        actions (net sub 30/31): playerGrabCart @0x40e910 (adopt touched cart
        mesh or spawn new via sceneryObjAlloc), playerReleaseCart @0x40f3e0
        (collect item or drop held cart +0x17c with mesh).
        All updated from gameUpdate @0x426ee0
        (called from gameWorldUpdate @0x40b4bb), which also runs per-player
        update + AI passes.
     4) Unit object subsystem (obj*) — gameplay-unit list head DAT_004550d4,
        driven from gameUpdate @0x426ee0. Unit = 0x44-byte record {+0x00/+0x04
        next/prev, +0x08 turret list, +0x0c turret list 2, +0x10 shot list,
        +0x14 shot count, +0x18/+0x1c flags/type, +0x20/+0x24 pos x/z,
        +0x28/+0x2c prev pos x/z, +0x30 angle}. API (renamed):
          Lifecycle: objDtor @0x402ab0, objListPush @0x4055c0 (links into
            DAT_004550d4), objUpdateAll @0x4055f0 (calls physics + fire per unit),
            objUpdatePhysics @0x405680, objUpdateFire @0x405e10, objWalkAnimSync
            @0x409b10 (calls syncCartNodeChannelsToWalkPos).
          Collision: objCollideCheck @0x404ac0, objShotCollide @0x4035e0,
            objDistTo @0x404fe0, objAngleTo @0x405010, objDistToPoint @0x405050,
            objAngleToPoint @0x405080, mathSegIntersect @0x406130.
            objSegCollideCollect @0x402ba0 = moving-segment vs node +0x38 segment
            lists, collects owner ids (only caller objShotCollide).
          Position: objSetPos @0x404ef0, objMovePolar @0x404f10 (polar move),
            objSetAngle @0x404f70.
          Turrets: objFindTurret @0x405120, objTurretAdd @0x405280,
            objTurretSetValue @0x405370, objTurretListFree @0x402b40,
            objTurretListFree2 @0x402b70.
          Shots: objShotAdd @0x4054e0, objShotCtor @0x406050 (0x44-byte shot),
            objShotListFree @0x406110, objShotListClear @0x405590.
          Lookup: objPolarPosLookup @0x405140 / objPolarPosLookup2 @0x4051c0
            (identical twins — polar pos via distance+angle lookup tables),
            objListFindFloat @0x405240 (scan list for float field match),
            objHashNextSame @0x414a40 (same-bucket hash walk).
        Used by playerUpdateOrientToTurret @0x40e5b0 (objPolarPosLookup +
        objListFindFloat + nodeChannelAvgFloat).
        playerUpdateOnFoot @0x427730 [RENAMED] = the on-foot player controller,
        run from gameUpdate right after playerUpdateWalkPhysics when
        nHasCart==0. (a) Turret aim: eases each player-turret aim angle (+0x40)
        toward its target object (+0x3c), angular velocity (+0x44) clamped to
        _DAT_0044b58c, nearest target wins. (b) Movement: builds the vector
        from the turret inputs (sum of turret aim offsets - 2x sum of target
        velocities, rotated via gxVec2RotateAdd, damped when objDistTo walk
        node < _DAT_0044b27c), writes output mesh channels +0x24c/+0x250 via
        mathVec2Polar, then objMovePolar + objSetAngle(param_1+0x258). Turret
        world angle (+0x30) wrapped to [-pi,pi]. (c) Hazard zones per level
        (objContainsPoint on node-channel avg): g_nLevelIdx==3 -> inside
        DAT_00450df4 zone teleports home (sceneObjSetPosOrient 0x1a0a) + snaps
        move angle; g_nLevelIdx==2/4 -> sets flag 0x10, plays hurt sfx
        (sndPlaySfx3D id 7), knocks turret aim back; DAT_00450dec zone steers
        toward it, DAT_00450de4 zone does nodeSetTransformFromChannels.
     5) AI navigation graph (aiNavNode*, scene-object node graph, distinct from
        the navpoint grid — see 10-fileformats.md §12). Records linked through
        DAT_0045e5cc/e5d0/e5d4 (head/tail/count?). Renamed:
        aiNavEdgeCtor @0x428c70, aiNavNodeCtor @0x428cb0 (initialises node +
        calls FUN_00431ae0 + sceneNodeGetPos + aiNavNodeUpdate @0x428d50),
        aiNavNodeCtorScene @0x428cf0 (variant that re-reads scene node data),
        aiNavNodeAddEdge @0x429ba0 (links edge to node).
        AI movement uses playerUpdateAI @0x40b510 with target checks
        (playerCheckTargetRange @0x40f4d0 vs DAT_0044b570, playerCheckTurn
        @0x40e670).
     6) Player node channel-sync passes [RENAMED — per-subobject mesh-follows-
        node passes, same clamp + sceneObjSetPosOrient + objPolarPosLookup +
        nodeChannelAvgFloat pattern as itemMeshFollowUpdate @0x40fde0]. Called
        per player from gameUpdate @0x426ee0 and playerUpdateCartPhysics
        @0x4280b0:
        - syncPosNodeChannelsToMesh @0x428840: syncs pos node +0x264 channels
          to scene mesh +0x14 (clamps +0x244 via node +0x40, +0x234 via
          node +0x3c; heading from +0x260 or node +0x38).
        - syncWalkNodeChannelsToMesh @0x428990: syncs walk node +0x224 channels
          to scene mesh +0x10 (clamps +500 via node +0x3c).
        - syncCartNodeChannelsToMeshes @0x428a70: syncs cart node +0x2a4
          channels to BOTH meshes (+0x14 and +0x10), then height-blends:
          when nodeChannelAvgFloat(node,+0x14) - (node,+0x10) leaves the
          window [_DAT_0044b704, _DAT_0044b464] it calls syncCartNodeChannels
          ToWalkPos @0x40e040. Cart-riding only (gameUpdate).
     7) AI movement-mesh / zone-wall region 0x428840-0x42a7xx [RENAMED]:
        nav-mesh list head DAT_0045e5e0 (doubly-linked via +0x40/+0x44; each
        mesh holds two connection lists: +0x38 cross-mesh links and +0x3c
        wall segments). Build path = zoneWallListBuild @0x42a650 (called from
        levelSceneTexturesLoad @0x4010869):
        - zoneWallListFree @0x42a150 frees conn lists + mesh records.
        - zoneConnMergeDupesInMesh @0x429d60: merge AABB-overlapping conns
          within one mesh's +0x3c list (tol ±2).
        - zoneWallCalcPlane @0x42a1c0: derive mesh plane (pos +0xc/+0x10/
          +0x14, slopes +0x4/+0x8, avg +0x0) from +0x3c conns.
        - zoneConnMergeDupesCrossMesh @0x429e90: merge AABB-overlapping conns
          ACROSS meshes (tol ±2 XZ / ±2500 height); unlinks + frees the dupes,
          then re-links via zoneConnLink @0x429c90 to record cross-mesh
          adjacency (conn+0x8 = peer mesh).
        - zoneWallMergeDupesSameDir @0x42a360: 2nd dedup — merges conns with
          equal polar dir (+0x1c/+0x20) + owner (+0x8) in BOTH lists (tol ±1).
        - Ray queries (used by AI/goal targeting): sceneRayFindNearest
          @0x42a750 / sceneRayFindSorted @0x42a7c0 walk the mesh list applying
          a plane test (mesh +0x4/+0x8 slopes vs point) then zoneWallCircleHit
          @0x42aac0 (radius vs +0x38 conn segments); zoneWallPointSide
          @0x42a870 decides which side of the nearest conn a point lies on.
        - sceneDetailGridCtor @0x42ad00 (DAT_0045838c, called from levelSetup)
          builds the per-level detail/LOD grid; helpers sceneDetailGridAddRow
          @0x42b000, sceneDetailGridCheckMesh @0x42b0d0 (posCmd), sceneDetail
          GridUpdate @0x42b1d0 (gameFrameRender), sceneDetailGridSetRoot
          @0x42b350, sceneDetailGridFree @0x42b190 (roundTeardown), plus
          sceneCollectMeshHandles @0x42b360 (levelSceneTexturesLoad) and
          sceneMeshBBox @0x42ba40.
  - Level event directors (per-level, dispatched from roundLogicUpdate on
    g_nLevelIdx): levelEventDirector_L0..L4 @0x416fd0/0x4178c0/0x418000/
    0x418a30/0x4192a0; inits from levelDirectorInits @0x40bdf0 (L0 @0x416db0,
    L1 @0x4175e0, L2 @0x417dc0, L3 @0x4186c0, L4 @0x418f30); cleanups
    @0x416f70/0x417860/0x417fa0/0x4189d0/0x419240 called from roundTeardown.
  - Director structure [VERIFIED — levelEventDirector_L0]: all 5 use the same
    "MCDMAN bonus task" presentation. Assets: KASSOERSKA cashier mesh + cash_sit/
    s_winner anims; MCDMAN mascot + s_stand/flpick1/flpick2/throw1/throw2 anims;
    hidden BURGER object; SIGN_FELIXPO ambient mesh (4 sub-meshes rotated via
    sceneObjSetSubPos + fsin). L4 additionally spawns 5 looping 3D ambient
    emitters. levelDirectorInits also loads GRIND2..GRIND0x10 skate-rail meshes.
  - Sequence state machine (DAT_00459d7c 0-9): TIME-GATED, not score-gated —
    idle frames count in DAT_00459d78 and the sequence starts when
    g_nObjUpdateTime (config obj_update_time) * DAT_00459d78 >= 10000:
      s1-2: blink flpick1 anim (apply/step alternate each frame)
      s3-4: blink flpick2 anim
      s5-6: attach BURGER to MCDMAN (sceneObjSetClassMesh mesh idx 8) + blink throw1
      s7-8: detach BURGER + throw-move it to target (sceneObjSetPos mode 5) + blink throw2
      s9:   spawn class-0x1f item (sceneObjCtor3 id 0x1f) at BURGER pos,
            +0x10 = posx, +0x14 = BURGER scene-object (nValue1), objHashRegister;
            re-arm when FUN_004305b0 reports the object collected.
    A trailing ambient anim (s_winner on results screen, else cash_sit) blinks
    every other frame.
  - CLASS-0x1f BONUS ITEM pickup [VERIFIED — playerAiGrabItem @0x40ea20]:
    proximity-grab (via playerCheckBlocked + playerFindNearestTarget @0x40ed10,
    filters obj+0x08 == 0x1f): held slot +0x17c = 0x1f, player +0x40 = the BURGER
    scene-object read from EventObject +0x14, attached via sceneObjSetClassMesh
    (class 8) + raised to y=300, EventObject removed via objHashRemoveFree.
    Delivery in playerUpdateAI nAnimState 7 (sfx g_nLevelIdx+0xf + progress=100);
    cleanup nAnimState 0xc hides the BURGER, re-arming the L0 sequence.
  - eventAnim keyframe record types [DECODED — all are positioning ops, NOT
    spawn/emit]: frame table = array of {int count, int *records} (8B/frame,
    param_1[3] walks frames). Record layouts:
      type 1 = 0x10B set offset-channel A target (to param_1[8..10])
      type 2 = 0x10B set offset-channel B target (to param_1[0xb..0xd])
      type 3 = 0x14B (5 dwords) sceneObjSetPos(mesh, x,y,z, mode2) on obj[6]
      type 4 = 0x10B sceneObjSetPosOrient (position + orientation)
      type 5 = 8B    sceneObjSetSubPos(obj[6], meshIndex, x,y,z, mode)
      type 6 = 0x10B sceneObjSetPos(mesh, x,y,z, mode2) on obj[6]
- Scene/level files: .sen scenes (sceneLoadSen @0x432323 — format in
  [10-fileformats.md](10-fileformats.md) §12), .tpg textures, .ai (per-scene
  AI), .eo EventObjects, config sections objects/*. Object creation strings:
  "Creating objects...", "Setting up objects...", "Loading ObjectScene <%s>".
- EventObjects [VERIFIED]: loaded/saved as config-tree sections (see
  [10-fileformats.md](10-fileformats.md) §12); on-disk keys x/y/h/h2 ->
  +0x38/+0x3c/+0x40/+0x44, values{5} -> +0x10, line[%d]{x1,y1,x2,y2} ->
  +0x20-byte line records (eventObjAddLine @0x4147c0, lineRecordCtor @0x4145d0,
  lineRecordNormal @0x414610) linked from +4. Objects with id == 0x1f
  are skipped on load ("Burger EventObject found and ignored."). Editing
  console commands: eloadCmd @0x406cb0, esaveCmd @0x4072a0, enameCmd
  (objHashRehash), eheightCmd, evalueCmd; DAT_0045e498 = currently selected
  EventObject. nopDebugStub @0x401590 = pure-RET debug no-op.
- Shopping-zone navigation [VERIFIED — gameObjectUpdate @0x40cf40 +
  objContainsPoint @0x414bb0 + objHashFindNearest @0x426100]:
  - Line records double as ZONE POLYGONS. objContainsPoint ray-casts across an
    object's line list (segments have precomputed unit normals) to test whether
    a world point (x,z) is inside the zone.
  - Zone/exit ids are packed 4cc strings: PUNK (levels 0-3 default), ARE1/ARE2/
    ARE4 (level 4), exits EXI1..4, checkout "goal" (DAT_0044f4e4). Constants at
    0x44f518-0x44f550. Level-4 zone resolution order ARE4->ARE2->ARE1; zone->
    exit map (when target zone != player zone): in ARE4 -> EXI1, in ARE1 -> EXI4,
    in ARE2 -> EXI2 (target in ARE4) else EXI3. Non-level-4 between-zones
    retarget uses the bbox test |obj.X - player.X| < nValue0 && |obj.Z - player.Z|
    < nValue1 (see EventObject layout).
  - Targeting (local player): resolve current zone via objContainsPoint; pick
    nearest uncollected list item (Player +0x34 id list, +0x5c collected, chosen
    slot -> +0x84) with objFindById(id, occurrence) + distance; when all 10
    collected target "goal"; modes 3/4 use g_nCurrentItemId. If target zone !=
    player zone, retarget the zone exit connector (EXI*) to navigate there.
    A rotating pointer mesh (DAT_00458114/5811c navigating, DAT_00458110/58118
    over target) is animated via sceneObjSetPosOrient + fsin.
  - 30 per-level item slots DAT_004583e4 + i*0xb {+0 meshHandle, +4 sceneObj,
    +8 subObj, +0xc active flag} populated by levelSetup @0x4108a0 from config
    items/%d (mode 4 uses CHECKFLAG mesh); gameObjectUpdate resets each slot's
    object when no longer visible and repositions stragglers.
  - Item slots are bound to EventObjects by id at roundStartInit @0x40a4d0
    (called from runCmd @0x4084c0): slot+0xc = objFindById(i+1) for modes != 4,
    objFindById(i+201) for mode 4 (matches gameObjectUpdate's +0xc9 check);
    modes != 4 spawn the slot mesh at y-1000 so items "fall" on round start.
  - AI pathfinding zone graph: roundStartInit links .eo zone objects "AR%02.2d"
    and "IN%02.2d" (0..98; format strings @0x44f31c/@0x44f328, NO dash — matches
    on-disk AR00/IN08) via zoneConnCtor @0x42b410 (inObj, arObj, detailLevels)
    into connection nodes in the DAT_0045e5e8 list; each node holds a mesh-index
    array (zoneConnCollectMeshes @0x42b490) for geometry intersecting the AR
    zone. zoneConnUpdateCulling @0x42b8f0 hides/shows room interiors per frame
    based on objContainsPoint3D @0x414b60 against the IN zone (vertical bounds
    from +0x40/+0x44, tolerance ±DAT_0044b44c). Nodes freed by roundTeardown
    @0x40aa10 via zoneConnUnlink @0x42b890. L2 orientmall.eo carries the full
    room graph (IN00-10/AR00-10); other levels define none so the graph is
    empty there.
  - Level -> asset map [VERIFIED — maniac.cfg levels[] + startup commands]:
      L0 ICA     ica.eo (goal only)          L3 Aqua     aqua.eo (PUNK[2], c_ac[5],
      L1 Woodshop woodshop.eo (PUNK[2])         mvnc[10], goal)
      L2 Orient  orientmall.eo (IN/AR graph,  L4 Future   futuremall.eo (ARE1/2/4 +
        tele[4], gong, goal)                   EXI1-4, tele[5], mvnc[2], c_ac[6],
                                               hurl, goal)
    PUNK zones are shared-id polygons (PUNK[2] = two zone polygons both id
    "PUNK" -> objFindById occurrence scan). c_ac = checkout-area zones, mvnc =
    conveyor zones, tele = teleporters, gong = gong trigger (point, no lines),
    hurl = throwing zone. .eo loaded via the per-level startup command sequence
    ("nload X.ai", "eload X.eo") run by commandDispatch in levelSetup.
  - Round teardown: roundTeardown @0x40aa10 (killCmd @0x407870 / WinMain
    @0x4160a0) shuts down net/sound/MCI, frees zone connections + thrown-item
    list + detail levels, and runs per-level director cleanup
    (levelEventDirector_L0..L4_Cleanup @0x416f70/0x417860/0x417fa0/0x4189d0/
    0x419240, each freeing the director's event meshes, e.g. L0 frees
    DAT_00459d9c..00459db4).

## Quest subsystem (Frögesporten trivia) [VERIFIED, renamed]
- `questLoad` @0x40ffa0 parses quest.txt (XOR ^0x55), lines
  "1|2|3 <question>? J|N" -> records at g_pQuestHead/Tail @0x458978/0x45897c
  (record +0x4 next, +0xc category). questRecordCtor @0x40fec0,
  questRecordDtor @0x40ff50 (round teardown), questPickRandom @0x4010290
  (random record of category). textDrawWrappedCentered @0x40101d0 (word-wrap +
  textDrawCentered).
