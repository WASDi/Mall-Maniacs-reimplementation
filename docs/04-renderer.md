# Mall Maniacs (maniac.exe) — 04. GX renderer

[Back to README](README.md)

Status:

The original GX boundary and software-driver behavior are verified well enough
to support the rebuild's GUI vertical slice. `maniac_rebuild.exe` loads the
original `DRIVERS\GXSOFT.DLL`, renders indexed GUI assets at 640x480, and
presents frames through the reconstructed maniac-side wrappers. The full
scene-graph renderer is documented and named in Ghidra, but is not yet part of
the rebuild.

## Purpose and verified contract

GX is the game's pluggable graphics boundary. The original `gxLoadDriver`
(`0x00432ea0`) loads a driver and installs its `GxDriverApi` function table;
`gxUnloadDriver` (`0x00433280`) tears it down. The driver exports
`gxDLLInfo`, `gxDLLInit(GxDriverApi *)`, and `gxDLLExit`. The 136-byte
`GxDriverApi` table covers mode setup, viewport/state, texture and surface
operations, indexed drawing, blitting, and frame presentation.

The wrappers are mostly thin dispatches. The rebuild separates the original
driver-load boundary (`gxLoadDriver`, `0x00432ea0`, narrowed to the known
`GXSOFT.DLL`) from the original mode wrapper (`gxInit`, `0x004332f0`), while
registry selection and driver-info validation remain deferred. `gxUnloadDriver`
(`0x00433280`) is represented by the narrowed `gxUnloadDriver` implementation.
`gxDrawPolygon` (`0x00433440`) preserves the original `nSoftwareMode == 1`
branch: it applies the original X/Y float scales and truncating conversion to
each vertex before dispatch. The active GXSOFT path leaves that flag at zero,
so its original 8.8 fixed-point coordinates pass through to GXSOFT for clipping
and sampling. The rebuild currently needs mode setup, texture
loading, blitting, flipping, and clearing; the remaining table entries can be
added as visible features require them.

## Assets and software-driver evidence

- `.tpg` is a fixed 0x10400-byte texture: 256×256 8-bit indexed pixels
  followed by 256 RGBA palette entries. The maniac loader maps the file and
  calls the driver's `pLoadTexture`; `gxSoft.dll` copies both regions into a
  texture node. See [10-fileformats.md](10-fileformats.md) §12.
- The imported `gxSoft.dll` (`0x10000000`) fills the API table in
  `gxDLLInit` and maintains the software texture list. Its first texture load
  initializes palette/colour-conversion state and the backing DirectDraw
  surface; this explains the rebuild's palette-from-first-load rule.
- Font descriptors and tagged text use the original font-pool interface.
  `textDraw` (`0x409420`) builds four fixed-point vertices and the `0x1c`-byte
  texture/UV record before calling `gxDrawPolygon`. Its explicit zero alpha
  byte normalizes a vertex byte that the original leaves unwritten; this has
  no observed visual effect.
  The full font implementation is summarized in [16-rebuild.md](16-rebuild.md);
  its static `fmtParseInt`/`strFindSubstring` dependencies are substituted by
  `strtol`/`strstr` within the rebuild scope.

## Scene renderer

The original scene path is reconstructed at the behavioral level: scene
initialization owns node/mesh pools and draw-sort state; `sceneRender`
constructs the camera basis and root transform, recursively renders scene
nodes, sorts mesh primitives, and restores the viewport. Channel-based 4×3
transforms, camera math, mesh morphing, node hierarchy, and ray/segment tests
are named and documented in Ghidra. These findings provide the path for later
single-player scene work, but are not an exhaustive per-symbol inventory here.

## Rebuild status, limitations, and next direction

`src/gx.c` implements the narrow vertical-slice adapter: it loads the known
`DRIVERS\GXSOFT.DLL` directly, calls `gxDLLInit`, then invokes the separate
`gxInit`/`pSetMode` wrapper, loads `.tpg` assets, and presents frames with
blit/flip/clear. `gxDrawPolygon` now preserves the original local packed-record
call contract for both flag paths. The mode contract, 8-bit framebuffer, and
`presentFrame` behavior are verified in [16-rebuild.md](16-rebuild.md), and the
GUI path is verified under Wine.

The rebuild intentionally defers registry-based driver selection and settings,
alternate drivers, DirectDraw internals, focus-loss snooze/reinitialization,
the animated menu decoration pass, and the original scene/gameplay render
path. The next useful renderer work is to extend the existing adapter only
when the next real GUI or single-player state needs it, then connect the
verified scene-node and mesh pipeline incrementally.
