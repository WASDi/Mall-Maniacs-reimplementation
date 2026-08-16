# Mall Maniacs (maniac.exe) — 04. GX renderer

[Back to README](README.md)

## 6. GX renderer driver API [VERIFIED] (renamed + struct)
- Driver DLL interface (exports): `gxDLLInfo` (struct: version at +0x100 ==
  0x103, +0x102 minor, +0x104 setting-count then 0xc-byte setting records of
  types 1-5 for a driver-config UI), `gxDLLInit(GxDriverApi*)`,
  `gxDLLExit`. `gxLoadDriver` @0x00432ea0 [renamed] (also reads registry
  config if no filename given); `gxUnloadDriver` @0x00433280 [renamed].
- Struct `GxDriverApi` @0x0045eb40 (136 bytes) — table of fn pointers. Struct
  offsets (Ghidra struct `GxDriverApi`, base 0x45eb40):
  +0x00 pField_0, +0x04 pSetMode, +0x08 pGetMode, +0x0c pSnooze, +0x10/+0x14
  pField_10/14, +0x18 pFlip, +0x1c pField_1c, +0x20 pClearScreen,
  +0x24/+0x28/+0x2c/+0x30/+0x34 pField_24..34, +0x38 pSetViewport, +0x3c
  pGetViewport, +0x40 pResetState, +0x44 pLoadTexture, +0x48 pField_48,
  +0x4c pCreateSurface, +0x50/+0x54/+0x58 pField_50/54/58, +0x5c pDrawPolygon,
  +0x60 nDrawEnabled, +0x64 pBlitSurface, +0x68 pSetOrigin, +0x6c pDrawTriangle,
  +0x70 pDrawLine, +0x74 pDrawTriUV, +0x78 pDrawQuad, +0x7c nSoftwareMode,
  +0x80 pDriverModule, +0x84 nDriverActive.
  (NOTE: earlier notes listed these as 0x44/0x48/... — that was the absolute
  address of each field; struct-relative offsets are -0x40.)
- Renamed wrappers (thin dispatch through the table):
  gxInit 0x332f0, gxGetMode 0x33310, gxSnooze 0x33330, gxFlip 0x33340,
  gxClearScreen 0x33350, gxSetViewport 0x33370, gxGetViewport 0x33390,
  gxResetState 0x333b0, gxLoadTexture 0x333d0, gxCreateSurface 0x33420,
  gxDrawPolygon 0x33440 (converts float coords -> fixed point if software mode),
  gxBlitSurface 0x33580, gxSetOrigin 0x335d0, gxDrawTriangle 0x335f0,
  gxDrawLine 0x33610, gxDrawTriUV 0x33640, gxDrawQuad 0x33670.
- Texture load: FUN_00408d60 memory-maps .tpg -> gxLoadTexture(0,1,name,data,
  data+0x10000) returns handle; handles cached in DAT_0045a6xx. Fonts loaded
  via FUN_00409070(txt + tpg). gxBlitSurface used for full-screen HUD/tga.
  .tpg layout VERIFIED (see [10-fileformats.md](10-fileformats.md) §12):
  256x256 8-bit indexed pixels (0x10000 B) + 256 RGBA palette (0x400 B),
  parsed inside the driver (gxSoft.dll gxLoadTexture @0x100019b0).
- Font pool [renamed]: fontPoolCreate @0x408f90 / fontPoolDestroy @0x408fc0
  (g_fontPool @0x455d3c; g_textColor/g_textColor2 @0x455e40/0x455e44 = 0xffffff,
  markup "RGB"/"RGB2").
- Driver gxSoft.dll (imported into MMUnmod project as /Drivers/gxSoft.dll,
  image base 0x10000000): gxDLLInit @0x10001000 fills the GxDriverApi table
  (pLoadTexture = gxLoadTexture @0x100019b0); texture nodes are a 0x42c-B
  linked list (head DAT_10010fbc, name@+4, aligned pixel buf @+0x24, raw
  malloc @+0x28, palette @+0x2c; gxTextureNodeAlloc/Free @0x10001e90/ee0).
  First texture load builds colour quantiser tables (RGB555->index @0x10010fc0,
  key/transparent table @0x10109218, 2-colour blend @0x1010912c, float blend
  @0x10109334) then creates the DirectDraw surface (DAT_1006c858 vtable
  CreateSurface @+0x14) and attaches it (DAT_1006c81c @+0x7c).
- Rebuild (maniac_rebuild.exe): the driver contract used by the GUI vertical
  slice — gxSetMode @0x10001130, mode-struct layout, 8-bit framebuffer
  DAT_10020fc0, palette-from-first-load rule, presentFrame @0x410310 — is
  documented in [16-rebuild.md](16-rebuild.md).
- Driver registry config helpers [renamed] (HKEY_CURRENT_USER, path
  `Software\UDS\No Fear\MTB Drivers\...`; config fatal-error on missing):
  gxRegSplitKeyValue @0x434cd0 (split "Key\ValueName" at last '\'),
  gxRegReadBinary @0x434c10 (REG_BINARY), gxRegReadDword @0x434d40 (REG_DWORD),
  gxRegReadFloat @0x434e00 (string read + sscanf "%f" @0x451148; used for
  float driver settings). Error boxes: gxErrorBox @0x434f10 (MessageBoxA +
  ShowCursor), gxFatalErrorExit @0x434ef0 (gxErrorBox + exitProc(-1)).
  Consumed by gxLoadDriver @0x432ea0 / gxUnloadDriver @0x433280.
- Walk-anim limb-swing table [renamed]: g_awWalkAnimTable @0x458138 (0x80
  entries {angle short, value short}; short[0x100]). Precomputed by
  walkAnimTableEntryCalc @0x433980 (roundStartInit loop, circle geometry
  0xd2/0x118, binary-search tangent angle); consumed by playerAnimOrientFromDir
  @0x4336b0 (clamps movement speed to 0x10..0x7f, looks up the swing angle +
  limb offset, builds the orientation matrix and outputs 2 x 3 euler angles).
  Drives the limb sub-objects (3,4,6,7) in playerAnimSfxUpdate @0x40c800.

## Scene-graph render + math helpers [renamed]
- Math: mathSinDeg @0x42d030, mathCosDeg @0x42d050, mathAtan2Deg @0x42d010
  (float-degree trig; used by scene build / player turn code).
  gxVec2 helpers: gxVec2Set @0x434fa0, gxVec2SetAngleZero @0x434f90,
  gxVec2FromPolar @0x434fc0, gxVec2Add @0x434fe0, gxVec2Sub @0x435020,
  gxVec2RotateAdd @0x435090.
- Channel/world-transform pipeline (4x3 transforms): chanBuildRotMatrix
  @0x42f030, chanCalcWorldTransform @0x42f6e0 (recursive parent-chain world
  transform), mat3x3Mul @0x42f7d0, sceneBuildRootMatrix @0x42f520 (builds the
  root inverse/view matrix at DAT_0045e8d4).
- Scene render system [renamed]: sceneSystemInit @0x42ed40 (allocates sort
  buffer DAT_0045e914 + node/mesh pools + 0x400 sin table + binary sin tree,
  clears the per-frame callback slots 0x45e650..0x45e810),
  sceneSystemClose @0x42f180 (sceneFreeAllNodes @0x42f150 + free pools),
  sceneRender @0x42f1c0 (main pass: aspect/viewport from the camera block,
  pre-render callbacks -> sceneBuildRootMatrix -> sceneCameraBasisCalc ->
  sceneNodeRender over DAT_0045e8cc -> drain sort buffer via meshDrawPoly ->
  post-render callbacks -> restore viewport/identity),
  sceneCameraBasisCalc @0x42f460 (camera right/up/forward @0x45e894-0x45e8b4
  from the view matrix), mathSinTreeBuild @0x42efb0 (recursive binary sin
  lookup tree). roundStartInit pairs sceneSystemInit/Close around the level
  texture-load pass.
- Mesh draw / sorting: meshDrawPoly @0x42e940 (draws triangle/line/quad via
  gxDrawTriangle/gxDrawLine/gxDrawQuad), gxSortPushKey @0x42ecf0 (pushes a
  5-dword sort key into the draw-sort buffer DAT_0045e90c),
  sceneCacheLocalVerts @0x42ffa0, sceneMorphInterp @0x4300d0 (morph-target
  interpolation).
- Scene node lifecycle/hierarchy (world node + channel child-mesh list):
  sceneNodeAlloc @0x4318e0 (0xa8-B node, type 2, linked into root
  DAT_0045e8c0, node-type counters DAT_0045e638-0045e644), sceneNodeAllocChild
  @0x4319e0 (child node, type 3), sceneNodeFree @0x430460 (recursive child
  free), sceneNodeSetPos @0x431590, sceneNodeGetPos @0x431270 (channel-based:
  type 2 = local, 4 = world, 6 = relative), sceneNodeGetPosWorld @0x430e80,
  sceneNodeGetChannelPos @0x4315e0, sceneNodeFacePos @0x431030 (orient node to
  face a point), sceneNodeUpdateBounds @0x4303c0, sceneNodeSetHiddenFlag
  @0x4305c0 / sceneNodeGetHiddenFlag @0x4305b0 (+2 flag byte),
  sceneMatBuildOrient @0x431730 (build orientation matrix from channels).
- Node render: sceneNodeRender @0x42f8c0 (recursive mesh render using
  chanCalcWorldTransform per node). Ray/segment tests: sceneRayFindNearest
  @0x42a750, sceneRayFindSorted @0x42a7c0 (insertion-sort candidates),
  mathSegIntersect @0x406130.
- Scene-object accessors [renamed]: sceneObjGetPos @0x4317e0 (first-mesh pos
  shorts, mode&0xf==2, x0xb6 when mode&0xf0==0x20), sceneObjSetSubOrient
  @0x431110 (euler-angle 3x3 matrix lerped into sub-mesh +0x1c by 0.5),
  sceneSetCurrentObj @0x430d90 (sets DAT_0045e810/0045e608 = current obj +
  channel mode read by sceneNodeGetPos), sceneFindByName @0x431fd0 (ids from
  the 8-B {name*,id} table DAT_0045eb10 by substring; roundStartInit hides
  "HIDE ME!" meshes with it).
