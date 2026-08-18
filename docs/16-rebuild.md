# Mall Maniacs (maniac.exe) — 16. Rebuild (maniac_rebuild.exe)

[Back to README](README.md)

Status: **Chunk 3 (main menu, menuUpdate @0x41b0b0) DONE + VERIFIED** — the intro
logo timeline (chunk 2) now transitions into the real main menu: five rows
("Spela"/"Nätverk"/"Alternativ"/"Rekord"/"Avsluta") rendered as two-font
token streams, Up/Down wrap navigation, Enter fires each row's target state,
Escape opens the quit-confirm screen (menu\quit.tga) and Space/Enter quits.
Milestone 1 (GUI vertical slice) remains CLOSED + VERIFIED below. The plan in
`/home/wasd/auto-ghidra/Rebuild.md` drives the original `DRIVERS\GXSOFT.DLL`
software rasterizer through its GX driver interface; source lives in
`/home/wasd/auto-ghidra/`.

## Goal
Produce a source project with `src/maniac.c` as its main translation unit that
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

## Source layout (/home/wasd/auto-ghidra/src/)
| File | Role |
|---|---|
| `Rebuild.md` | The plan/constraints being implemented (repo root) |
| `src/maniac.c` | WinMain @0x4160a0, initWindowAndInput @0x4165f0, WindowProc @0x4161b0 (keydown -> state func), 25ms-gated frame loop (gameFrameUpdate @0x41a8c0 timing) + menuFramePost flip/clear |
| `src/menu.c` / `src/menu.h` | Menu subsystem: menuInit @0x419c20 (palette MERGED00.TPG + 6 intro logos + 5 fonts + quit.tga), introUpdate @0x41ae50 (6-logo timeline), menuUpdate @0x41b0b0 (5-row two-font render, nav), stateQuitConfirm @0x4200b0, row-target stubs (stateGameTypeSelect @0x41c010, stateNetworkMenu @0x420190, stateHighScoreTable @0x41dfd0, gotoOptions @0x41d300), menuFramePost (gxFlip + gxClearScreen for menu states); exports PStateFunc/g_pStateFunc/g_flFrameDelta/g_nLastFrameTime + menuUpdate (non-static for the row-target stubs) |
| `src/gx.c` / `src/gx.h` | GX driver adapter: gxInit @0x4332f0, gxShutdown @0x432880, presentFrame @0x410310, gxLoadTpgFile @0x416060, full maniac-side wrapper cluster @0x433310-0x433670 (gxGetMode/gxSnooze/gxFlip/gxClearScreen/gxSetViewport/gxGetViewport/gxResetState/gxLoadTexture/gxCreateSurface/gxDrawPolygon/gxBlitSurface/gxSetOrigin/gxDrawTriangle/gxDrawLine/gxDrawTriUV/gxDrawQuad); `GxMode` + `GxDriverApi` structs |
| `src/util.c` / `src/util.h` | `appLog` → `rebuild.log`, file reads, TGA loader (tgaLoad16 @0x415df0), file-helper cluster (fileOpenMode @0x408cd0, fileCloseStream @0x408d00, fileReadN @0x408d10, fileSeekTell @0x408d30, fileReadRaw @0x408d60, fileReadText @0x408e20, fileGetSizeOpen @0x408ee0, fileGetSize @0x408f20, fileExists @0x408f60) |
| `src/pool.c` / `src/pool.h` | memPool cluster @0x4197a0-0x419bb0 (memPoolSystemInit/Create/Alloc/AllocZero/Free/Destroy/SystemShutdown) simplified to malloc/free over `g_apMemPools` @0x459f70 |
| `src/stubs.c` / `src/stubs.h` | TODO stubs (gameInit @0x409d90, gameFrameUpdate @0x41a8c0, pollKeyboard @0x416a10, and the four menu row targets stateGameTypeSelect @0x41c010, stateNetworkMenu @0x420190, gotoOptions @0x41d300, stateHighScoreTable @0x41dfd0) with contracts + original addresses |
| `build.sh` | Compiles to `/home/wasd/MallManiacsUnmodified/maniac_rebuild.exe` |
| `run.sh` | `cd` game dir, `timeout 5 wine ./maniac_rebuild.exe`, prints `rebuild.log` |

## Milestone 1 — GUI vertical slice [VERIFIED]
Builds clean with `i686-w64-mingw32-gcc src/maniac.c src/gx.c src/util.c src/stubs.c
src/pool.c -lkernel32 -luser32 -lgdi32 -lwinmm`. Under wine from the game dir it logs to
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

## Maniac-side GX wrapper cluster [VERIFIED] (@0x433310-0x433670)
Thin dispatches through `GxDriverApi`, reimplemented in `src/gx.c` with signatures
matched to Ghidra (all `__cdecl`; 0-arg slots are ABI-identical either way):
- `gxGetMode` @0x433310, `gxSnooze` @0x433330, `gxFlip` @0x433340,
  `gxClearScreen` @0x433350, `gxSetViewport` @0x433370, `gxGetViewport`
  @0x433390, `gxResetState` @0x4333b0 (zeroes `nDriverActive` before dispatch),
  `gxCreateSurface` @0x433420.
- `gxLoadTexture` @0x4333d0 — 5-arg; bumps `nDriverActive` when loading
  (data!=0, mode==0), decrements when freeing (data==0, mode!=0).
- `gxDrawPolygon` @0x433440 — software mode (`nSoftwareMode==1`) truncates the
  four (x,y) float vertex pairs to int in place; flags bit 2 (4) repacks the
  color/uv arg into a local 0x20-byte buffer (bytes 0..3,4..5,8..9 then
  0xd,0xf,0x11,0x13,0x15,0x17,0x19,0x1b) before dispatch.
- `gxBlitSurface` @0x433580 — gated on `nDrawEnabled` (+0x60), 9 args.
- `gxSetOrigin` @0x4335d0; `gxDrawTriangle` @0x4335f0; `gxDrawLine` @0x433610,
  `gxDrawTriUV` @0x433640, `gxDrawQuad` @0x433670 — line/tri/quad all gate on
  `pDrawTriangle` (+0x6c) non-NULL then dispatch via their own slot.
`presentFrame` @0x410310 now calls the wrappers (`gxBlitSurface(1,0,0,tex,
0,0,0x280,0x280,0x1e0); gxFlip(); gxClearScreen(1,0)`) instead of poking the
api table directly. `GxDriverApi` @0x45eb40 extended with the decompiled fields
`nDrawEnabled` +0x60, `pDrawTriangle` +0x6c, `pDrawLine` +0x70, `nSoftwareMode`
+0x7c; `GxMode` struct (16 B) created in Ghidra to type `gxGetMode`.

## Foundation chunk — memPool + file helpers [VERIFIED]

First isolated slice toward the menu milestone. Self-contained layer with no
renderer/state-machine dependency; verified by building clean and wiring
`memPoolSystemInit` into the running slice (rebuild.log shows `pool 0 = DEFAULT`
created before the asset loads).

- `src/pool.c`/`src/pool.h` — memPool cluster @0x4197a0-0x419bb0:
  - `memPoolSystemInit` @0x4197a0 zeroes `g_apMemPools` @0x459f70 (0x100
    slots) and creates pool 0 `"DEFAULT"` (s_DEFAULT @0x4500e4).
  - `memPoolCreate` @0x4197d0 (name -> pool index, -1 on fail/full).
  - `memPoolAlloc` @0x419870 / `memPoolAllocZero` @0x419a20 / `memPoolFree`
    @0x419a60 / `memPoolDestroy` @0x419ae0 / `memPoolSystemShutdown` @0x419bb0.
  - Original is a hierarchical slab allocator over a 0x140-B handle (0x40-B
    name @+0, 0x40 slots @+0x40). Per Rebuild.md simplified to malloc/free;
    pool-handle interface preserved so callers keep their contracts.
- `src/util.c` — file-helper cluster, CRT-backed:
  - `fileOpenMode` @0x408cd0 (mode 1 -> `"wb"` @0x44e6c8 else `"rb"` @0x44e6c0,
    FILE* as int, -1 on fail); `fileCloseStream` @0x408d00; `fileReadN`
    @0x408d10; `fileSeekTell` @0x408d30 (0/1/2 -> SEEK_SET/CUR/END);
    `fileReadRaw` @0x408d60 and `fileReadText` @0x408e20 (pool-owned buffers via
    memPoolAlloc, NULL on any failure); `fileGetSizeOpen` @0x408ee0;
    `fileGetSize` @0x408f20 and `fileExists` @0x408f60 (open mode = `"rb"`, the
    same string as g_szCmdFont).
  - Original thunks go to the statically-linked MSVC CRT (fileOpen @0x43e68a =
    fopen, fileRead @0x43e306 = fread, fileSeek @0x43e5a0 = fseek, fileTell
    @0x43e41d = ftell, fileClose @0x43e289 = fclose); CRT used directly per
    Rebuild.md.
- `src/gx.c` — `gxLoadTpgFile` @0x416060: `fileReadRaw(0,path)` ->
  `gxLoadTexture(0,1,path,data,data+0x10000)` -> `memPoolFree(0,data)`.

Build: `./build.sh` clean (-Wall -Wextra). Smoke test: rebuild.log confirms
`memPoolSystemInit` -> pool 0 before MERGED00/intro loads, intro still presents.

Ghidra signatures aligned with the rebuild for all 17 functions (previously
`undefined4`/`int *`/`char` from auto-analysis): memPool cluster return types ->
`int`/`void *`, `memPoolAllocZero(int, size_t)`, `memPoolFree(int, void *)`;
file helpers -> `FILE *` handles (`fileCloseStream`, `fileReadN`,
`fileSeekTell`, `fileGetSizeOpen`), `char *` buffer for `fileReadN`; `fileExists`
and `gxLoadTpgFile` return `int`. `memPoolSystemInit`/`memPoolSystemShutdown`
keep their binary `__stdcall` convention; the rest `__cdecl`. Saved in Ghidra.

## Chunk 2 — intro logo timeline (introUpdate @0x41ae50) [VERIFIED]

All six intro logos now render in the original order/timing instead of a static
`intro_addgames.tga`. Under wine from the game dir (`timeout 22`), rebuild.log
shows the full 17.45s timeline:

```
[winmain] running intro timeline (6 logos)
[intro] logo 1/6 menu\intro_addgames.tga @25 ms
[intro] logo 2/6 menu\intro_och.tga @2603 ms
[intro] logo 3/6 menu\intro_uds.tga @3704 ms
[intro] logo 4/6 menu\intro_samarbete.tga @6279 ms
[intro] logo 5/6 menu\intro_mcd.tga @7379 ms
[intro] logo 6/6 menu\intro_presenterar.tga @9954 ms
[intro] timeline complete (17454 ms) -> menuUpdate
[stub TODO] menuUpdate @0x41b0b0 not implemented (holds last frame)
```

The standard 5s `run.sh` smoke test now shows logo 1 (addgames) then logo 2
(och) plus the start of logo 3 (uds) in the log — the second logo renders
inside the default smoke-test window. `run.sh` also fixed: it used `set -e`,
so `timeout` returning 124 (its exit when it kills the app) aborted the script
before the log was printed; removed `set -e` so the smoke-test output always
appears.

What was implemented in `src/maniac.c` (mirroring the original):
- `g_pStateFunc @0x45a6f8` state-function pointer, called `(type, key,
  keyType)` per `dispatchKeyEvent @0x41ade0`; `type 0` = frame update,
  `type 1` = key event.
- `introUpdate @0x41ae50` — timeline accumulator `g_introFade_2 @0x45d444`
  advanced by `g_flFrameDelta * 25.0` (g_flFrameDelta @0x45a6cc = elapsed ms *
  0.04, so the accumulator tracks real ms). Threshold floats
  @0x44b660-0x44b684 (+17450 @0x44b65c) select the logo; the ~90ms gaps
  (2500-2590, 3590-3680, 6180-6270, 7270-7360, 9860-9950) clear to
  `g_nClearColor` (black) — the original's fade-to-black between logos. Any
  keydown skips to the menu (original skips on key 4, type 2; docs note "any
  key or ENTER" — we accept all keys). ≥17450 ms -> `g_pStateFunc =
  menuUpdate` (@0x41ae50 LAB_0041b082).
- `menuUpdate @0x41b0b0` — TODO stub (Rebuild.md §19): logs once, holds the
  last drawn frame; body to be replaced when the main menu is built.
- `menuInit @0x419c20` (narrowed) now loads all six intro TGAs in the original
  load order (`g_hIntroTexAddgames..g_hIntroTexPresenterar @0x45a618-0x45a62c`,
  paths @0x450794-0x450720), after the MERGED00 palette install, then sets
  `g_nLastFrameTime = timeGetTime()` and `g_pStateFunc = introUpdate` (the
  original selects the intro because `g_menuMode @0x45022c` is preinitialized
  to 0x101 in .data and clears it after; the rebuild always starts with the
  intro).
- Frame loop now gates at 25 ms (`g_nLastFrameTime + 0x19 <= now`) and calls
  `g_pStateFunc(0,0,0)`, matching `gameFrameUpdate @0x41a8c0` timing so the
  timeline advances in real time.
- WindowProc @0x4161b0 forwards non-Escape WM_KEYDOWN to `g_pStateFunc(1,
  vkToKeyId, 2)` (vkToKeyId maps VK_* -> the docs/12-input key ids 0-7; Escape
  still quits the slice).

Ghidra sync: `introUpdate` and `menuUpdate` prototypes refined to
`int (int nType, int nKey, int nKeyType)` `__cdecl` (previously undefined4
params); plate comments added on the threshold table @0x44b660 (full logo/ms
map) and on `g_introFade_2` @0x45d444. Saved.

## Font / text rendering module — ANALYSIS COMPLETE (full disasm verified)

All stack offsets re-derived from the raw disassembly; every earlier
"discrepancy" was a stale-ESP tracking error (missing `PUSH ESI`, and the
6-arg poly call's stack frame). The Ghidra decompiler is correct on all points.

Signature (all `__cdecl`, verified against `textDrawCentered` and the
`menuUpdate` call site @0x41b440):
```c
int     textDraw(font, color, x, y, text);   // @0x409420, returns final x cursor
int     textWidth(font, text);               // @0x409810
int     textDrawCentered(font, color, x, y, text); // @0x409860, x = x - w/2 + 0x140
int     textDrawInt(font, color, x, y, value);     // @0x4098a0 (itoa + textDraw)
int     textIntWidth(font, value);                 // @0x409940 (itoa + textWidth)
gxFont *fontParse(text, texture, posX, posY, param5); // @0x4090c0, memPoolAlloc(g_fontPool,0x510)
gxFont *fontLoad(path, texture, posX, posY, param5);  // @0x409070, fileReadText(0,path)+fontParse
```

`gxFont` object (0x510 bytes, struct created in Ghidra):
```
+0x00 u16 height       +0x02 u16 pad
+0x04 texture (node*)  +0x08 param5
+0x0c u16 globalSpace  +0x2e u8  spacepos (== glyphWidth[0x20], the space char)
+0x0e u8  glyphWidth[256]
+0x10e u16 uvx[256]    +0x30e u16 uvy[256]
```

`textDraw` draw path (per char c):
- gw = glyphWidth[c], U = uvx[c], V = uvy[c], h = height
- verts v0..v3 = {x0,y0} {x1,y0} {x1,y1} {x0,y1}, each `{x<<8, y<<8, 0, color}`
- top color (v0,v1) = g_textColor2, bottom (v2,v3) = g_textColor; `u8 {0,R,G,B}`
- colorUv (0x1c bytes) = `{texture, param5, 0, U,V, gw<<8|U,V, gw<<8|U,h<<8|V, U,h<<8|V}` (u16s)
- `gxDrawPolygon(&v0,&v1,&v2,&v3, color, &colorUv)` — `color` arg doubles as flags
- advances: space `x += globalSpace + spacepos`; char `x += gw + globalSpace`

Tags parsed inline from the text string (decompiler was right; not `[R+0x18]`):
`{X:n}`/`{Y:n}` decimal → x/y cursor; `{RGB:h}` → g_textColor2; `{RGB2:h}` →
g_textColor; `{{` renders literal `{`. Globals renamed in Ghidra:
`g_szTextTagX`@0x44ed40, `g_szTextTagY`@0x44ed3c, `g_szTextTagRGB`@0x44f0d4,
`g_szTextTagRGB2`@0x44f0cc.

Known ambiguity: the char-advance read at 0x4097bd loads `[ESP+0xc8]`, which is
`param_2` if you count the 6 poly args literally but `param_1+0xc` per Ghidra's
call-return-address stack model. The game works, so the intended/introduced
behavior is `font->globalSpace` (`param_1+0xc`); we implement that.

Ghidra sync: prototypes refined to the above with `gxFont *` (struct created);
plate comments added on textDraw/textWidth/textDrawCentered/textDrawInt/
fontParse/fontLoad and the 4 tag globals. Saved.

## Font module — IMPLEMENTED in src/font.c (builds clean)

`src/font.h` + `src/font.c` added to build.sh; `./build.sh` compiles
`maniac_rebuild.exe` with `-Wall -Wextra` clean.

Implemented (all match the Ghidra prototypes exactly):
- `fontPoolCreate`/`fontPoolDestroy` (g_fontPool = memPoolCreate("FONT"))
- `fontDefGetKey` (strstr), `fontParseSkipToValue`/`SkipLine`/`SkipSpaces`
- `fontParse` — full descriptor parser (keys, glyph map, widths, case
  fallback, spacewidth/spacepos, UV tables with atlas-pitch wrapping)
- `fontLoad` — fileReadText(0,path) + fontParse + memPoolFree
- `textWidth`, `textDraw`, `textDrawCentered`, `textDrawInt`, `textIntWidth`
- textDraw tag parser ({X:}/{Y:} dec, {RGB:}/{RGB2:} hex, "{{" literal)
- gxFont struct (0x510 alloc), g_abFontGlyphMap[256], g_textColor/g_textColor2

Note: `g_fontPool` retyped to `int` (pool id, memPoolCreate returns int).
Helper prototypes in Ghidra refined to `char * __cdecl` returns.

Done in chunk 3 below: `fontPoolCreate` + all five `fontLoad` calls are wired into
menuInit @0x419c20 and the main menu is drawn with textDraw (two-font rows).

## Chunk 3 — main menu (menuUpdate @0x41b0b0) [VERIFIED]

Moved the menu/intro subsystem out of maniac.c into `src/menu.c`/`src/menu.h`
(maniac.c keeps only the app/window/loop glue; the frame loop calls
`g_pStateFunc(0,0,0)` then `menuFramePost()`). Builds clean
(`i686-w64-mingw32-gcc ... src/menu.c ...`).

- `menuInit @0x419c20`: memPoolSystemInit, load `menu\MERGED00.TPG` palette,
  load 6 intro logos, `fontPoolCreate` + 5 fonts (tiny/small/normal/200
  variants), load `menu\quit.tga`; `g_nMenuRow=0`, `g_nMenuFadeTarget=0`,
  `g_nLastFrameTime=timeGetTime()`, `g_introFade_2=0`, `g_pStateFunc=introUpdate`.
- `menuUpdate`: 5 rows rendered x=0x226, y=row*0x29+0xbe as two-font token
  streams (small font: a-z + å/ä/ö; normal font: rest), color 0x2004, selected
  row uses the "200" highlight fonts; Up/Down wrap 0<->4, Enter fires the row
  target, Escape opens stateQuitConfirm, Left/Right ignored; navigation is
  logged to rebuild.log.
- Row targets: row 0 "Spela" -> `stateGameTypeSelect @0x41c010`, row 1
  "Nätverk" -> `stateNetworkMenu @0x420190`, row 2 "Alternativ" ->
  `gotoOptions @0x41d300`, row 3 "Rekord" -> `stateHighScoreTable @0x41dfd0`,
  row 4 "Avsluta" -> `stateQuitConfirm @0x4200b0`. The first four are TODO
  stubs in `src/stubs.c` (log + return to menu, per Rebuild.md §19);
  quit-confirm is real:
  presents quit.tga, Space/Enter -> PostQuitMessage(0), any other key ->
  back to menu.
- Input: WindowProc forwards every WM_KEYDOWN to `g_pStateFunc(1, key, 2)`
  (vkToKeyId map: Right=0 Left=1 Up=2 Down=3 Space=4 Enter=6 Esc=7; unknown ->
  4). During the intro any keydown skips to the menu (introUpdate handles it).

Verified interactively under wine (xdotool): Space skips intro -> menu; Down/Down/
Up -> rows 1/2/1; Return on "Nätverk" -> stateNetworkMenu stub; Escape ->
quit-confirm; Escape -> back to menu; Down x4 wraps 0->4; Return on "Avsluta"
-> quit-confirm; Space -> `[menu] quit confirmed`, `[winmain] exiting cleanly`.
Log excerpt:
```
[menu] main menu active (5 rows, two-font render)
[menu] row 1 (Down)
[menu] row 4 'Avsluta' selected
[stub TODO] stateNetworkMenu @0x420190 not implemented (back to menu)
[menu] quit confirmed
```

Ghidra sync (saved): prototypes refined to the state-func convention
`int __cdecl <name>(int nType, int nKey, int nKeyType)` for menuUpdate,
stateQuitConfirm, gotoOptions, stateGameTypeSelect, stateNetworkMenu,
stateHighScoreTable; plate comments on menuUpdate (row table + two-font render)
and stateQuitConfirm; string global `g_szMenuRowNetwork` @0x450820 retyped to
`char[8]` with row-label cluster comment.

### menu.c verification against Ghidra
Re-verified all functions in `src/menu.c` against the open `maniac.exe`:
- **Names** all match: `menuInit @0x419c20`, `introUpdate @0x41ae50`,
  `menuUpdate @0x41b0b0`, `stateQuitConfirm @0x4200b0`, `stateGameTypeSelect
  @0x41c010`, `stateNetworkMenu @0x420190`, `gotoOptions @0x41d300`,
  `stateHighScoreTable @0x41dfd0`, `gameFrameUpdate @0x41a8c0` (mirrored by
  `menuFramePost`). Every reimplemented function carries its original address
  in a comment; the helper functions (menuIsSmallChar/menuRowWidth/menuRowDraw,
  introPresent/introClear) have no direct original address and are labeled as
  rebuild helpers.
- **Call hierarchy** matches: `menuInit -> introUpdate -> menuUpdate`; the
  five-row dispatch table (Spela/Nätverk/Alternativ/Rekord/Avsluta ->
  stateGameTypeSelect/stateNetworkMenu/gotoOptions/stateHighScoreTable/
  stateQuitConfirm) is identical in order and targets; `stateQuitConfirm`
  returns to `menuUpdate`; all four row-target stubs return to `menuUpdate`
  (matching the originals' ESC tails); `menuFramePost` mirrors the
  gameFrameUpdate flip/clear gate. Rendering calls (textWidth/textDraw,
  presentFrame, gxClearScreen/gxFlip, fontPoolCreate/fontLoad/gxLoadTpgFile,
  memPoolSystemInit) mirror the originals' call sites.
- **Stub placement**: the four row-target stubs moved from `src/menu.c` to
  `src/stubs.c`/`src/stubs.h` (per Rebuild.md: "All stubs go into stubs.c");
  `menuUpdate` became non-static (declared in `src/menu.h`) so the stubs can
  return to the main menu. Interfaces and the dispatch table unchanged.
- **Signature fix (this session): `menuInit`**. Disassembly at all three call
  sites (gameFrameUpdate @0x41a8d0, dispatchKeyEvent @0x41adeb,
  stateOptionsExit @0x41c679) pushes one dword (0 / 0 / 1) then `ADD ESP,4`,
  and the callee ends in a plain `RET` — i.e. `__cdecl` with one `int`
  argument. Ghidra previously had `void __stdcall menuInit(void)`. Corrected to
  `void __cdecl menuInit(int nRestartMode)` (0 = first init, 1 = return from
  options + music track 7) and the rebuild signature/caller updated in lockstep
  (`src/menu.h`, `src/menu.c`, `src/maniac.c` calls `menuInit(0)`); rebuild is
  clean.
- **menuUpdate Escape path**: original `case 7` does `g_pStateFunc =
  stateQuitConfirm; return 0;` without touching `g_nMenuFadeTarget`; the rebuild
  now returns early the same way.
- Documented divergences (intentional, in comments): intro skips on any keydown
  (original only key 4); quit-confirm accepts Space/Enter keydown 4/6 (original
  J/Y/j/y char events); stubs log + return to menu instead of the real targets.

## Next milestone (from docs/11-issues.md §15)
Main menu is in. The next isolated chunk is the first row-target state behind
the menu: either the game-type select (`stateGameTypeSelect @0x41c010`,
a real ~1500-byte function) or the options screen (`stateOptions @0x41c6a0`,
the actual `gotoOptions` target). Both still need the real input path
(`pollKeyboard @0x416a10` / DirectInput thunk @0x42d000) for mouse/controller
support; the menu currently drives navigation purely through WM_KEYDOWN
forwarding. Implementing a target state lets the two-font/menu assets be
reused and gives the second GUI screen before wiring up actual gameplay.

## Reimplementation progress tracker
`TrackRebuildProgress.java` is a Ghidra script that enumerates the functions in
the open `maniac.exe`, applies the rebuild scope exclusions, and compares the
remaining function addresses with `@0x...` annotations in `src/maniac.c`. It prints
the implemented count, remaining count, and percentages, and writes the full
per-function report to `rebuild-progress.txt` by default.

Run it through the Ghidra MCP bridge after placing/compiling the script in the
configured Ghidra script directory:

```text
ghidra_run_ghidra_script(
  script_name="TrackRebuildProgress.java",
  program="maniac.exe",
  args="source=/home/wasd/auto-ghidra/src/maniac.c output=/home/wasd/auto-ghidra/rebuild-progress.txt"
)
```

Note: `rebuild-progress.txt` might be stale, re-run `TrackRebuildProgress.java` if unsure.

The default exclusions match `Rebuild.md`: external/imported functions,
thunks/import stubs, the statically linked CRT range `0x43c850-0x44966c`,
CRT/compiler-glue-named functions, console command functions, and functions beginning with
`net`, `mnet`, `stateNet`, `stateNetwork`, `console`, `command`, `startServer`,
or `stateHost`. Override the last prefix group with
`exclude_prefixes=a,b,c` when the rebuild scope changes.
