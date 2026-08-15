# Mall Maniacs (maniac.exe) — 12. Input / DirectInput

[Back to README](README.md)

## 13. Input / DirectInput subsystem [VERIFIED]
- Window + input init: `initWindowAndInput` @0x004165f0 [renamed] registers the
  window class "Mall Maniacs", creates the window (hwnd @0x459cd0), then
  `DirectInputCreateA` thunk @0x42d000 -> `g_pDInput` @0x459ce8
  (DIRECTINPUT_VERSION 0x300). hInstance @0x459cdc.
- Keyboard device: CreateDevice(GUID_SysKeyboard @0x44b740) ->
  `g_pDInputKeyboard` @0x459cec — SetCooperativeLevel(hwnd, 6),
  SetDataFormat(@0x44b768 = c_dfDIKeyboard), Acquire (vtable+0x1c).
- Mouse device: CreateDevice(GUID_SysMouse @0x44b730) -> `g_pDInputMouse`
  @0x459cd8 — SetCooperativeLevel coop 5, dataformat @0x44b750 (c_dfDIMouse).
- GUIDs verified on disk: SysKeyboard {6F1D2B61, D5A0, 11CF, ...}, SysMouse
  {6F1D2B60, ...}.
- `inputReleaseDevices` @0x00416c30 [renamed]: Release on the 6 init failure
  paths.
- `inputPollKeyboard` @0x00416820 [renamed]: GetDeviceState(0x100) on the
  keyboard, maps key bits -> keydown callback (key, 2); keyboard auto-repeat
  via g_nKeyRepeatStage/Frames/Active/Edge @0x459d3c/0x459d40/0x459d44/0x459d48;
  posts WM_CLOSE (hwnd @0x459cd0) when g_bQuitRequested.
- Key id map (DIK_*): 0=Right (0xCD), 1=Left (0xCB), 2=Up (0xC8), 3=Down (0xD0),
  6=Enter (0x1C), 7=Esc (0x01).
- Message routing / WNDPROC / gameplay key dispatch: see [02-boot.md](02-boot.md)
  §3/§4 — `pollKeyboard` @0x416a10 (DI GetDeviceState + key-down dispatch with
  200ms debounce, distinct from inputPollKeyboard), `dispatchKeyEvent` @0x41ade0,
  WindowProc @0x4161b0 (WM_KEYDOWN 0x100 / WM_CHAR 0x102 -> gameKeyHandler,
  WM_APPMSG 0x3b9 = MCI CD-track finished -> replay).
- Level-editor mouse input (input mode DAT_00458344 == 2, dispatched from
  WindowProc WM_MOUSEMOVE 0x200 / WM_LBUTTONDOWN 0x201): `editorMouseMove`
  @0x426340 (screen->world via 0x43dd10; no mods = objHashFindNearest pick into
  DAT_0045e498, Ctrl = objSetPosXY drag, Shift = draw/move zone-boundary line
  records; feeds console keys to auto-type the "ename" rename command),
  `editorMouseDown` @0x426230 (click toggles edit flag DAT_0045e4a0, then
  moves nearest line-record endpoint), `lineRecordFindNearest` @0x426190
  (nearest line-record midpoint query) — all [renamed].
- Globals summary: hwnd @0x459cd0, hInstance @0x459cdc, g_pDInput @0x459ce8,
  g_pDInputKeyboard @0x459cec, g_pDInputMouse @0x459cd8.
