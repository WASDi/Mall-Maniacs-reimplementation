# Mall Maniacs (maniac.exe) — 08. Globals / managers map

[Back to README](README.md)

## 10. Globals / managers (initial map)
- DAT_00455e60 — engine instance ptr (FUN_00401000 ctor).
- DAT_00459cd0/00459ce0 — HWND; DAT_00459cdc — hinstance; DAT_00459ce8 —
  IDirectInput; DAT_00459cec — keyboard device; DAT_00459cd8 — mouse device.
- DAT_004580c4 — driver id (1=glide, 2=software).
- DAT_0045a658 — menu/engine booted flag; DAT_004580f8 — gameplay-loop flag;
  DAT_004580f4 — quit/post-quit flag; DAT_00459cd4 — game-running flag.
- DAT_0045a6f8 — current state update fn ptr (the mode dispatcher).
- DAT_0045a710 — deferred-action slot (fn ptr set by requestCmd; picked up by
  menuInit; cleared by unloadGameWorld @0x41a684).
- DAT_0045a700/0045a704/0045a708/0045a70c — round-result record (level / rank /
  ...) written at round end, consumed by statePlayLevel / stateVahiScore /
  gameOverLoadHighScores / endScene.
- DAT_0045d440/_DAT_0045d444 — time-based animation/intro accumulators.
- DAT_00458100 — selected level; DAT_00458104 — local player idx;
  DAT_00458108 — player count; DAT_00456360 — player array base.
- GxDriverApi @0x0045eb40 + DAT_0045ebbc/ebc0/ebc4 (software flag, hModule,
  active flag).
- DAT_004588f8 — camera object; DAT_004580b4 — characters.sen handle.
- FUN_00401000 = engine object ctor; FUN_00434f90 sets {1.0f,0}; FUN_00435150
  allocates a byte flag object.
