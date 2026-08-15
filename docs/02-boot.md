# Mall Maniacs (maniac.exe) — 02. Entry / init / main loop

[Back to README](README.md)

## 3. Entry / initialization flow [VERIFIED]
- `entry` @0x43f18e -> `WinMain` @0x004160a0 [renamed].
- `initWindowAndInput` @0x004165f0 [renamed] — sets FX_GLIDE_NO_SPLASH,
  registers window class "Mall Maniacs", creates window (DAT_00459cd0=hwnd),
  DirectInputCreateA(DIRECTINPUT_VERSION 0x300), creates keyboard device
  (GUID @0x44b740, c_dfDIKeyboard, SetCooperativeLevel hwnd+6) stored in
  DAT_00459cec, and mouse device (GUID @0x44b730, c_dfDIMouse, coop 5) stored
  in DAT_00459cd8. WNDPROC = `WindowProc` @0x004161b0 [created].
- `gameInit` @0x00409d90 [renamed]:
  - Constructs manager objects (mStringCtorEmpty @0x435150, gxVec2SetAngleZero
    @0x434f90 on statics).
  - Loads `sommar.sol` (save) + `maniac.cfg` (config), XOR ^0x55 decrypt.
  - Picks graphics driver: config value -> DRIVERS\GXGLIDE.DLL ->
    DRIVERS\GXSOFT.DLL; DAT_004580c4 = 1 (glide) / 2 (software).
  - 640x480 @ 16bpp -> `gxInit` @0x004332f0.
  - Engine instance DAT_00455e60 = FUN_00401000(heap 0x60) (constructor that
    inits 4 sub-objects at +0x14/+0x20/+0x2c/+0x58 via gxVec2SetAngleZero
    @0x434f90).
- Console/2D/error helpers [renamed]: consoleHandleKey @0x4086e0 (in-game
  console key handler: console buffer DAT_004551e0, cursor DAT_00455d34,
  command-table dispatch via PTR_actionCmd_0044b308, Enter/Esc/tab-complete),
  consoleClearLines @0x4086c0 (g_acConsoleLines @0x4551e0 +
  g_nConsoleLineCount @0x455d34),
  gxDrawQuadColor @0x414470 (2D filled 0xff-alpha quad via gxDrawPolygon
  @0x2004; 51 callers), fatalError @0x414570 (teardown: scenNameTableFree,
  gxUnloadDriver, FUN_0043ee0f, then MessageBoxA "Mall Maniacs - Error"
  @ s_Mall_Maniacs___Error_0044ffe4 + exitProc(-1)).
- WinMain message loop: PeekMessageA/GetMessageA/TranslateMessage/DispatchMessageA.
  When idle: if DAT_004580f8==0 -> `gameFrameUpdate` @0x0041a8c0; else ->
  `gameRunFrame` @0x0040ad80 (gameplay loop, states bypassed).

## 4. Main loop / frame [VERIFIED]
- `gameFrameUpdate` @0x0041a8c0 [renamed] — delta time via `getGameTime`
  @0x0040dfe0 (timeGetTime-based accumulator), calls `pollKeyboard`
  @0x00416a10 (DI GetDeviceState + key-down event dispatch with 200ms debounce),
  then `(*DAT_0045a6f8)(0,0,0)` (current mode/state update fn), renders
  background polys via `gxDrawPolygon`, then `gxFlip`/`gxClearScreen`.
- First frame triggers `menuInit` @0x00419c20 [renamed] (when DAT_0045a658==0):
  loads intro .tga logos, menu MERGED%02d.tpg + END%02d.tpg, fonts
  (tiny/menysmall/meny), menu textures (fling00..sec500, sign100-300),
  anim\winner.anm + anim\run.anm, endscene.sen + characters.sen
  (DAT_004580b4), camera (DAT_004588f8), precomputes static tile/vertex data
  (0x45c3a4, 0x45a786 areas). Sets initial state fn DAT_0045a6f8 (intro
  timeline or config-driven toplevel). Also re-invoked by gameFrameUpdate and
  `dispatchKeyEvent` @0x41ade0 whenever DAT_0045a658==0 (e.g. after
  unloadGameWorld) — this is where a pending deferred action (DAT_0045a710,
  set by "request") gets picked up into g_pStateFunc.
- `dispatchKeyEvent` @0x0041ade0 [renamed]: if DAT_0045a658==0 -> menuInit();
  if g_pStateFunc!=0 -> call (1,key,type); else if key 7 (Esc) type 2 ->
  g_nLevelIdx=-1 + unloadGameWorld + "quit" + exitProc.
- Input routing [VERIFIED]: gameplay (DAT_004580f8!=0) keys come from
  `pollKeyboard` @0x416a10 (DI, 200ms debounce) and WindowProc @0x4161b0
  (WM_KEYDOWN 0x100 -> gameKeyHandler(key,1), WM_CHAR 0x102 ->
  gameKeyHandler(char,0)); menu/state keys via WindowProc WM_CHAR ->
  dispatchKeyEvent. WindowProc WM_APPMSG 0x3b9 = MCI CD-track finished ->
  replay current music track (loop). Key id map (DIK_*): 0=Right(0xCD),
  1=Left(0xCB), 2=Up(0xC8), 3=Down(0xD0), 6=Enter(0x1C), 7=Esc(0x01).
