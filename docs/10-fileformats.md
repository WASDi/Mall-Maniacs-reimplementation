# Mall Maniacs (maniac.exe) — 10. File formats

[Back to README](README.md)

## 12. File formats (on-disk evidence)
- .tpg textures (incl. MERGED%02d, END%02d), .tga (intro logos), .sen scenes,
  .ai, .eo, maniac.cfg/sommar.sol (XOR ^0x55), quest.txt, anim\*.anm,
  sound\menu\ (MCI). Data dir /home/wasd/MallManiacsUnmodified/ is the
  primary format evidence (5 scene_* dirs + menu + sound + Drivers).
- .sen scene format [VERIFIED — MALL1_ICA.SEN on disk + sceneLoadSen @0x432320]:
  - Header: 4cc "REV2" + u32 total file size.
  - Then chunk records {4cc tag, u32 size (LE), data}. Chunks seen on disk:
    MESH (179) + NAME (179) pairs, then single SUBO, COLS, MAPI, TNAM, OBJI,
    ONAM. (Loader also handles TANI, KEEP, TEMP, LEEP, PBJI.)
  - MESH @0x45e948: 8-byte entries {name*, data*} (mesh geometry + index);
    NAME @0x45eab4: chunk-name string table. TNAM @0x45e990: object-name table.
    SUBO @0x45eb30 sub-object data; COLS @0x45eb28 collision (u32 size>>2 =
    entry count); MAPI @0x45eb20 map geometry (u32 size>>4 = entries, built via
    FUN_00432260); TANI @0x45e940 animation data; KEEP/TEMP parsed via
    senChunkParse @0x432c00; OBJI @0x45eaa4 object instances (u32 size>>5 =
    32-byte records); ONAM @0x45e934 object-name region.
  - OBJI record (32 bytes): +0x04 type (1=scenery object, 3=emitter), +0x08
    object class, +0x18/+0x1a/+0x1c position shorts, +0x1e mesh index (into
    MESH). Type 1 -> sceneryObjAlloc @0x00430200; type 3 -> musicEmitterAlloc
    or sceneNodeAllocChild @0x4319e0. Scene instance id from FUN_004197d0
    ("SCENERY %d").
  - .sen loader helper cluster [renamed]: scenSetDir @0x432e60 (copies scene
    dir string into DAT_0045e950), scenExpandNameList @0x432dd0 (expands the
    NUL-separated NAME/TNAM tables), scenNameTableInit @0x431cb0 /
    scenNameTableFree @0x431e00 (name table lifecycle), scenNameToId @0x431ed0
    / scenNameToIdEx @0x431e20 (name -> packed id, ex keeps occurrence),
    scenIdToName @0x431f90, scenNameMatchCollect @0x432060 (collect matches by
    prefix into a list).
- .eo EventObject format [VERIFIED — eloadCmd @0x406cb0 + esaveCmd @0x4072a0 +
  eventObjAddLine @0x4147c0; on-disk ica/aqua/orientmall/futuremall/woodshop.eo]:
  - Config-tree text format. One section per object; object NAME IS ITS ID
    (numeric `_%03d` like _001/_201, or packed 4cc like PUNK/goal/ARE1/IN08).
    Non-numeric names serialize via `%-4.4s`/`%4.4s`.
  - Section syntax: `name` or `name[N]` — `[N]` declares N ANONYMOUS polygon
    sub-objects that all share the section name as their id (e.g. PUNK[2] =
    2 zones both id "PUNK"; explains objFindById occurrence-index lookup).
  - Keys per object:
      x = int -> +0x38 fPosX (center)
      y = int -> +0x3c fPosZ
      h = int -> +0x40 fHeight (zone vertical lower bound)
      h2 = int -> +0x44 fHeight2 (upper bound; objContainsPoint3D height test)
      values { v = N x5 } -> nValue0..4 at +0x10 (ints). ZONE objects use
        v0/v1 = bbox half-extents X/Z (gameObjectUpdate between-zones retarget);
        v2/v3 used by c_ac checkout zones (4000 / flag 1). PICKUP EventObjects
        (spawned by itemThrowUpdate on item land): v0 = spawn-X int, v1 = 1
        present marker, +0x24 pBackRef = thrown-item node back-pointer.
      line [M] { x1= y1= x2= y2= } xM -> M line records, each 0x20 bytes
        (lineRecordCtor @0x4145d0 + lineRecordNormal @0x414610), prepended to
        the +4 list. Record = {+0x00 x1, +0x04 y1, +0x08 x2, +0x0c y2 (floats,
        RELATIVE to origin +0x38/+0x3c), +0x10 nx, +0x14 ny (unit normal),
        +0x18 pNext, +0x1c pPrev}. Records form a 2D zone POLYGON (closed
        directed edge loop): objContainsPoint @0x414bb0 ray-casts across them.
  - Zone/object catalogue by level (levels[] in maniac.cfg; loaded via the
    `startup` console commands "eload <scene>.eo" / "nload <scene>.ai"):
      L0 ica.eo:      goal only
      L1 woodshop.eo: PUNK[2] + goal
      L2 orientmall.eo: IN00-10 + AR00-10 room graph, goal, tele[4], gong
      L3 aqua.eo:     PUNK[2], c_ac[5], mvnc[10], goal
      L4 futuremall.eo: ARE1/ARE2/ARE4 zones + EXI1-4 exits, goal, c_ac[6],
        tele[5], mvnc[2], hurl (all with line polygons except gong/point items)
  - ids consumed by code: "goal" checkout (DAT_0044f4e4), "PUNK" default zone
    (DAT_0044f538), "ARE1"/"ARE2"/"ARE4" level-4 zones, "EXI1..4" zone exits,
    "AR%02.2d"/"IN%02.2d" room-interior graph (zoneConnCtor).
  - Objects with id == 0x1f are skipped on load ("Burger EventObject found and
    ignored."). Editing via eloadCmd/esaveCmd/enameCmd(rename->objHashRehash)/
    eheightCmd/evalueCmd; current selection in DAT_0045e498.
- .tpg textures [VERIFIED — layout in gxSoft.dll pLoadTexture @0x100019b0
  (renamed gxLoadTexture) + on-disk byte analysis; every .tpg is 0x10400 =
  66560 bytes]:
  - Fixed single-texture file, no header/magic. Layout:
      +0x00000  pixels[256*256]  8-bit palette indices  (0x10000 = 65536 B)
      +0x10000  palette[256]     4-byte RGBA entries     (0x400  = 1024 B)
    Total 0x10400. Texture is 256x256 indexed-colour; the last 0x400 bytes
    are the RGBA palette (alpha byte is constant 0xcd=205 on real files).
  - Load path: fileReadRaw @0x408d60 maps file -> gxLoadTexture(0,1,name,data,data+0x10000);
    the maniac wrapper @0x004333d0 dispatches to driver pLoadTexture. In
    gxSoft.dll the loader (a) copies 0x4000 dwords = 0x10000 B from data into
    the texture's aligned pixel buffer (+0x24, aligned to 0x10000 on the
    0x20000 malloc), (b) copies 0x100 * 4 B from data+0x10000 (the palette)
    into the node at +0x2c, (c) stores the 32-char texture name at node+0.
  - Texture node = 0x42c B linked list (head DAT_10010fbc): +0 pNext, +4
    name[0x20], +0x24 aligned pixel buffer, +0x28 raw malloc, +0x2c
    palette[256], +0x23 NUL, name copy via strncpy(,31). Allocation by
    gxTextureNodeAlloc @0x10001e90, free by gxTextureNodeFree @0x10001ee0
    (gxLoadTexture called with param_4==0 frees by name/handle).
  - On first-ever texture load the driver builds colour-conversion tables:
    DAT_10010fc0 = 0x8000-entry RGB555->palette-index lookup (nearest colour
    quantiser), DAT_10109218 (key table, entries with R,G,B all <8 = transparent
    key colour), DAT_1010912c (2-colour average blend), DAT_10109334 (float-
    colour blend). It then creates the backing DirectDraw surface via
    DAT_1006c858(+0x14 CreateSurface) and attaches via DAT_1006c81c(+0x7c).
- anim\*.anm animation files [VERIFIED — on-disk format + playback decoded;
  see docs/14-animations.md]: header "ANM"+version, tableA mesh names, tableB
  16 named channel values 0..15, then keyframe track headers {u16 count, recs}.
  Records: op1/2 pos/facing target, op3/4 mesh-bound pos/pos+orient, op5
  sub-pos channel (tableB value), op6 pos. Each track = one animation frame.
  Loader: anmLoadFile @0x433a50 (open + dispatch), anmLoad @0x433a90 (reads
  header + anim data via dataReadU8/U16/U32 @0x433ee0/0x433ef0/0x433f10,
  stores pointer into DAT_0045ebc8, bumps refcount DAT_0045ebcc),
  anmCalcSize @0x433f40, anmFree @0x434050 (refcount-decrements; destroys the
  whole pool at 0). Ref-counted holder: anmSetAlloc @0x4344d0 (0x18-B holder,
  anm at +0x14), anmSetMeshSlot @0x434530 (binds anm to a mesh slot),
  anmSetFree @0x434500 (frees anm at +0x14 via anmFree + memPoolFree(0)).
  Runtime decoder: sceneObjectAnimStep @0x434540. Characters/carts load
  stand/walk/run/throw/get .anm per [characters] config.
- .tga images (intro logos) [loader cluster renamed]: imageLoadByMode @0x4102e0
  dispatches on DAT_004580c4; tgaLoad16 @0x415df0 = raw 16-bit TGA (0x4b000 =
  320x240x2 bytes, header at +0x12), tgaLoad16Pal @0x415ec0 = 16-bit TGA with
  24-bit palette conversion. gxLoadTpgFile @0x416060 = .tpg -> texture via
  gxLoadTexture(0,1,name,pixels,palette).
- .ai navpoint files [VERIFIED — nloadCmd @0x407e10 / nsaveCmd @0x408120 /
  on-disk ica/woodshop/orientmall/aqua/futuremall.ai]: plain DFF config text,
  one block per navpoint named with the 4-digit id `%04d`:
      `0001 { ID = 1  X = 18000  Y = 0  Z = 3000  NLINKS = 3
              LINKS { LINK = 2  LINK = 41  LINK = 72 } }`
  - Parsed by configParseFile (config tree); navPointCtor @0x00425760 builds a
    0x60-byte `NavPoint` record per block, linked via +0x5c into g_pNavPointHead
    @0x0045d4d0 (g_pNavPointTail @0x0045d4d4, count g_nNavPointCount @0x0045d4d8;
    append by navPointListAdd @0x004258e0). Link ids resolved to pointers with
    navPointFindById @0x00425c50, added by navPointAddLink @0x004257f0 (max 8).
  - NavPoint layout (struct `NavPoint`, 0x60 B): +0 nId (nsave renumbers 1..N),
    +4 aLink[8] (NavPoint*), +0x24 aLinkDist[8] float (2D dist to link, XZ),
    +0x44 fUnknown (-1.0f = unset), +0x48 nLinkCount, +0x4c/+0x50/+0x54
    nPosX/nPosY/nPosZ (world coords), +0x58 nFlags (reset by
    navPointResetAllFlags @0x00425c70), +0x5c pNext. X/Z are the walk plane;
    Y is height (all 0 on disk).
  - Per-level counts on disk: ICA 121, Woodshop 142, Orient 293, Aqua 192,
    Future 253; max links 4-5 per node. NLINKS always equals LINK count.
  - Use: AI pathfinding (aiPathfindToTarget @0x00401860 -> navPointFindNearestInYRange
    @0x004259d0; navPointRelaxCosts @0x00425af0 = Dijkstra-style cost relax;
    navPointPickCheapestLink @0x00425bd0; navPointPickRandom @0x00425aa0).
    Dev/editor: nloadCmd/ncleanCmd/nsaveCmd + WindowProc editor click
    (navPointEditorClick @0x00425d30, navPointEditorZoneMove @0x00425f70,
    g_pNavPointSel @0x0045e480). List freed by roundTeardown @0x40acf9.
- maniac.cfg (XOR ^0x55, "DFF version" text config) [VERIFIED on disk]:
  - [menu] players[] with controller "MM_CONT_KEY1"/"MM_CONT_AI", char_no,
    startpos_no, cart.
  - [levels[]] 5 malls: L0 ICA (Scene_ica/mall1_ica.sen, music 2), L1 Woodshop
    (scene_wood/woodmall.sen, music 3), L2 Orient (scene_orient/orientmall.sen,
    music 4), L3 Aqua (scene_aqua/aquamall.sen, music 5), L4 Future
    (scene_future/futuremall.sen, music 6); each has start_positions[8] and a
    startup{ "nload X.ai" "eload X.eo" } sequence (run via commandDispatch in
    levelSetup).
  - [items[]] 30 collectibles: Swedish brand names + mesh, e.g. #1 Philips
    Powerlife (philipsbatterier), #16 Felix Pommes Strips (FELIXPO),
    #26-30 generic "bItem26".. (BLUEBOX). Item id = index+1 (modes !=4) or
    index+201 (mode 4) -> item-slot binding in roundStartInit.
  - [objects] obj_scene_file="objects.sen", char_scene_file="characters.sen".
  - [characters] name + anim{stand/walk/run/throw/get .anm}; [carts]
    REDCART/GREENCART weight=20 friction=0.99 handle_pos; [camera] pos/aim.
  - [master] acc=64, friction=0.70, rotate_acc=0.032, acc_WC=40.5,
    rotate_acc_WC=0.026, obj_update_time=40 (-> g_nObjUpdateTime; director gate
    = 40*idleFrames >= 10000, i.e. ~250 frames), log_max_level=4,
    log_to_file=0, log_file_name="fet.txt", mapX/mapY/mapZoom, ai_mode.
- Save files: `config.mm` = the on-disk save, XOR ^0x55 obfuscated
  (fshi0* high-score keys: face/time/name/diff, toplevel, driver, master
  tuning). `sommar.sol` = plaintext TEMP copy used only during load/save:
  loadCmd builds config.mm -> sommar.sol (XOR-copy), parses sommar.sol,
  deletes it; saveCmd serializes tree, sommar.sol -> config.mm (XOR-copy),
  deletes sommar.sol. `config.mm XOR sommar.sol` == all 0x55. (FIXED: the
  obfuscated file is config.mm, NOT sommar.sol — see 06-config.md.)
