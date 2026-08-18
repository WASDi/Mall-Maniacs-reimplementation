# Mall Maniacs (maniac.exe) — 12. Input / DirectInput

[Back to README](README.md)

Status:

The original input subsystem is verified, including DirectInput device setup,
keyboard polling, message routing, and the level-editor mouse path. DirectInput
is intentionally deferred in `maniac_rebuild.exe`; the current GUI slice uses
window messages for menu input. The rebuild should return to the original poll
path when gameplay input or controller/mouse support becomes a milestone.

## Purpose and verified findings

- `initWindowAndInput` @0x004165f0 creates the "Mall Maniacs" window and calls
  `DirectInputCreateA` @0x0042d000 with version `0x300`. It creates keyboard and
  mouse devices using the standard system GUIDs (descriptors @0x44b740 and
  @0x44b730, verified on disk), the original keyboard/mouse data formats
  (@0x44b768 and @0x44b750), and cooperative levels 6 and 5. The handles are
  retained in the input globals near `0x459cd0`; `inputReleaseDevices`
  @0x00416c30 covers initialization failure paths.
- `inputPollKeyboard` @0x00416820 reads the 256-byte keyboard state, translates
  the confirmed directional/menu `DIK_*` values (Right 0/0xcd, Left 1/0xcb,
  Up 2/0xc8, Down 3/0xd0, Enter 6/0x1c, Escape 7/0x01), and emits keydown
  callbacks. Its repeat state uses the stage/frame/active/edge globals near
  `0x459d3c`; a quit request posts `WM_CLOSE`.
- The main-loop `pollKeyboard` @0x00416a10 is a separate path: it also reads
  DirectInput but applies a 200 ms key-down debounce before dispatching to
  gameplay. `WindowProc` @0x004161b0 routes `WM_KEYDOWN`/`WM_CHAR` to gameplay
  or menu handlers, while `dispatchKeyEvent` @0x0041ade0 invokes the active
  state. Full boot and message-loop routing is documented in
  [02-boot.md](02-boot.md) §3–§4.
- In the editor input mode, `WM_MOUSEMOVE` and `WM_LBUTTONDOWN` support nearest
  object selection, Ctrl-drag positioning, and Shift-based zone-boundary line
  editing. This path is evidence for the original editor behavior, not a
  rebuild feature.

## Rebuild status and next direction

`src/maniac.c` currently implements only the window portion of
`initWindowAndInput`; its `WindowProc` forwards `WM_KEYDOWN` directly to the
active menu state. `pollKeyboard` @0x00416a10 remains a logged stub, and the
rebuild does not link DirectInput or implement the original keyboard repeat,
gameplay, or editor mouse paths. This is consistent with the scope and current
milestone in [16-rebuild.md](16-rebuild.md); implement the real poll/dispatch
path after a GUI row-target state, when it is needed for gameplay or additional
input devices.
