# Mall Maniacs (maniac.exe) — 16. Rebuild (maniac_rebuild.exe)

[Back to README](README.md)

Status: **Milestone 1 (GUI vertical slice) CLOSED + VERIFIED.** The plan in
`/home/wasd/auto-ghidra/Rebuild.md` drives the original `DRIVERS\GXSOFT.DLL`
software rasterizer through its GX driver interface; source lives in
`/home/wasd/auto-ghidra/`.

## Goal
Produce a source project with `maniac.c` as its main translation unit that
compiles to `/home/wasd/MallManiacsUnmodified/maniac_rebuild.exe` with the
required offline GUI + single-player functionality of the original
`maniac.exe`. Constraints (Rebuild.md):
- Do NOT reimplement system libraries (MSVC CRT, imported APIs, C++ runtime,
  import stubs, compiler glue). Network + in-game console out of scope.
- 32-bit Windows, `i686-w64-mingw32-gcc`; link ONLY `KERNEL32`, `USER32`,
  `GDI32`, `WINMM` as actually used. No DirectInput/DirectSound/Winsock while
  their features are out of scope. Use the original `DRIVERS\GXSOFT.DLL` via
  its GX driver interface — no GDI backend.
- Comment every reimplemented function/global with its original address.
- Update Ghidra + `docs/` after each chunk so a new agent can continue.

## Source layout (/home/wasd/auto-ghidra/)
| File | Role |
|---|---|
| `Rebuild.md` | The plan/constraints being implemented |
| `maniac.c` | WinMain @0x4160a0, window @0x4165f0, WndProc @0x4161b0, asset load, present loop |
| `gx.c` / `gx.h` | GX driver adapter: gxInit @0x4332f0, gxLoadTexture, presentFrame @0x410310, gxShutdown @0x432880; `GxMode` + `GxDriverApi` structs |
| `util.c` / `util.h` | `appLog` → `rebuild.log`, file reads, TGA loader (tgaLoad16 @0x415df0) |
| `stubs.c` / `stubs.h` | TODO stubs (gameInit @0x409d90, gameFrameUpdate @0x41abc0, inputPollKeyboard @0x416800) with contracts + original addresses |
| `build.sh` | Compiles to `/home/wasd/MallManiacsUnmodified/maniac_rebuild.exe` |
| `run.sh` | `cd` game dir, `timeout 5 wine ./maniac_rebuild.exe`, prints `rebuild.log` |

## Milestone 1 — GUI vertical slice [VERIFIED]
Builds clean with `i686-w64-mingw32-gcc maniac.c gx.c util.c stubs.c
-lkernel32 -luser32 -lgdi32 -lwinmm`. Under wine from the game dir it logs to
`rebuild.log` and presents `intro_addgames.tga` until closed or Escape:

```
[winmain] === start ===
[init] window created (hwnd=...)
[winmain] gxSetMode done (640x480, bpp forced by driver)
[assets] MERGED00.TPG loaded (66560 bytes, palette set)
[assets] intro_addgames.tga -> 640x480 index buffer @...
[winmain] presenting intro_addgames.tga continuously
[winmain] exiting cleanly
```

Slice flow (mirrors original call order):
1. Register class "MallManiacsRebuild", create 640x480 window (title "Mall
   Maniacs", style 0xcf0000) — initWindowAndInput @0x4165f0 (DirectInput part
   deferred).
2. `gxDLLInit(&api)` from `DRIVERS\GXSOFT.DLL`; `pSetMode(mode{640,480,16,
   hInstance, hwnd})` — gxInit @0x4332f0.
3. Load `menu\MERGED00.TPG` via `gxLoadTexture(0,1,"MERGED00",data,
   data+0x10000)` — installs the DirectDraw 8-bit palette (menuInit @0x419c20
   order).
4. Load `menu\intro_addgames.tga` pixels (8-bit indexed 640x480, offset 804,
   0x4b000 B) — tgaLoad16 @0x415df0.
5. Loop: `presentFrame(pixels)` = `gxBlitSurface(1,0,0,tex,0,0,0x280,0x280,
   0x1e0); gxFlip(); gxClearScreen(1,0)` — presentFrame @0x410310.
6. On close/Escape: `gxDLLExit()` + `FreeLibrary` — gxUnloadDriver @0x432880.

## Driver contract [VERIFIED] (decompiled from gxSoft.dll)
- `gxSetMode` @0x10001130 (cdecl, returns 1 on success): forces width/height to
  640x480 (globals DAT_1000e048/4c = 0x280/0x1e0) and the bpp byte @+4 to 8;
  requires hInstance (+8) and hwnd (+0xc) BOTH non-zero (else fails); then
  DirectDrawCreate → SetCooperativeLevel(hwnd,0x53) → SetDisplayMode(640,480,8)
  → CreateSurface(flags 0x21) → CreatePalette from the 1024-byte palette buffer
  DAT_1006bff0 (RGBA) → SetPalette → ShowWindow(hwnd,5)/SetFocus/UpdateWindow.
- gxSetMode writes the *forced* values back into the caller's mode struct. The
  rebuild's `GxMode` matches maniac's stack layout from gameInit @0x40a0cd
  (16 bytes: u16 width@+0, u16 height@+2, u8 bpp@+4, pad, u32 hInstance@+8,
  u32 hwnd@+0xc).
- Render path is 8-bit indexed: driver keeps a 640x480 software framebuffer
  DAT_10020fc0 (0x4b000 B). gxClearScreen @0x10001730 fills it with 0x12c00
  dwords of (color&0xff). gxBlitSurface @0x10002950 copies source pixels into
  it (dst = fb + dstX + dstY*0x280 dwords; 640 B/row × 480 rows). gxFlip
  @0x10001520 blits the framebuffer into the DirectDraw surface (FUN_10002810:
  raw dword copy via Lock/Unlock helpers gxField10/gxField14) and calls
  IDirectDrawSurface::Flip.
- The DirectDraw 8-bit palette comes from the FIRST gxLoadTexture call
  (DAT_10010fbc==0 branch fills DAT_1006bff0 from the .tpg RGBA palette), so a
  .tpg must be loaded before blitting indexed art. The rebuild loads
  menu\MERGED00.TPG first (shares 249/256 palette entries with the intro TGAs),
  exactly as menuInit @0x419c20 orders its loads.
- tgaLoad16 @0x415df0 (intro logos are 8-bit indexed 640x480 type-1 TGAs):
  pixel-data offset = 18 + idlen + (cmap_len*cmap_depth/8); for
  intro_addgames.tga = 18+18+(256*24/8) = 804, then 0x4b000 B of index bytes
  (stride 0x280). Descriptor byte @+17: bit 0x20 set = top-left origin (no
  vertical flip) — intro TGAs have desc = 0x20, so no flip.
- Driver API table (filled by gxDLLInit @0x10001000, struct `GxDriverApi`
  @0x45eb40 in maniac, 136 B): +0x04 pSetMode, +0x08 pGetMode, +0x0c pSnooze,
  +0x10/+0x14 gxField10/14 (Lock/Unlock helpers), +0x18 pFlip, +0x1c null,
  +0x20 pClearScreen, +0x38 pSetViewport, +0x3c pGetViewport, +0x40
  pResetState, +0x44 pLoadTexture, +0x48 pUpdate (gxDLLUpdate), +0x4c
  pCreateSurface, +0x50/+0x54/+0x58 data ptrs, +0x5c pDrawPolygon, +0x60 pUpdate,
  +0x64 pBlitSurface, +0x68 pSetOrigin, +0x6c/+0x70 data ptrs, +0x74 pDrawTriUV,
  +0x78 pDrawQuad. Maniac extension: +0x80 pDriverModule, +0x84 nDriverActive.
- Maniac wrappers (thin dispatch): gxInit 0x332f0, gxGetMode 0x33310, gxSnooze
  0x33330, gxFlip 0x33340, gxClearScreen 0x33350, gxSetViewport 0x33370,
  gxGetViewport 0x33390, gxResetState 0x333b0, gxLoadTexture 0x333d0,
  gxCreateSurface 0x33420, gxDrawPolygon 0x33440, gxBlitSurface 0x33580,
  gxSetOrigin 0x335d0, gxDrawTriangle 0x335f0, gxDrawLine 0x33610,
  gxDrawTriUV 0x33640, gxDrawQuad 0x33670. Texture nodes are a 0x42c-B linked
  list (head DAT_10010fbc, name@+4, aligned pixel buf @+0x24, raw malloc @+0x28,
  palette @+0x2c; gxTextureNodeAlloc/Free @0x10001e90/ee0).

## Next milestone (from docs/11-issues.md §15)
Extend the static intro-logo screen into the main menu (menuUpdate @0x41b0b0)
using gxDrawQuad/gxDrawPolygon + the font system (fontPoolCreate @0x408f90,
fontLoad) now that the driver's poly/TriUV/Quad entries are mapped, then absorb
input (pollKeyboard @0x416a10 / DirectInput thunk @0x42d000) and the menu state
machine.