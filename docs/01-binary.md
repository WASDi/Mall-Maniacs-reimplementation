# Mall Maniacs (maniac.exe) — 01. Binary overview & toolchain

[Back to README](README.md) · Status: WORK IN PROGRESS. Only facts marked
[VERIFIED] were confirmed by reading decompilation/xrefs; everything else is
hypothesis.

## 1. Binary overview
- File: `/home/wasd/MallManiacsUnmodified/maniac.exe` — Mall Maniacs, 1999,
  AddGames Advertainment AB / UDS (Sweden). 3D "build & manage the mall" game;
  Swedish-language menus (Spela/Alternativ/Rekord/Avsluta, "Ja"/"J").
- PE 32-bit x86, MSVC toolchain (CRT runtime strings present), little-endian,
  image base 0x00400000, ~403 KB (.text 0x401000-0x44afff).
- 1238 functions; 290 data types. No exports; single entry.
- Uses DirectX (DirectInput) + WinMM (MCI) + GDI + custom "GX" graphics engine
  with pluggable drivers (GXSOFT.DLL software, GXGLIDE.DLL 3dfx, gxOpenGl.dll).
  WSOCK32.dll + DSOUND.dll loaded dynamically at runtime (not in import table).

## 2. Toolchain/platform observations
- MSVC C++ runtime statically linked (operator_new @0x43dd42, __ftol @0x43dd10,
  EH unwind frames 0x449x). MSVC6-era, DirectX6 required (per readme.txt).
  The full CRT region 0x43c850-0x44966c is now mapped + renamed (iostream
  stream layer for config save, low-level file I/O, memory/ctype/float helpers,
  TLS/errno, C++ EH runtime incl. crtCxxFrameHandler @0x4413d4) — map in
  [06-config.md](06-config.md) and [11-issues.md](11-issues.md) §16.
- Dynamic DLL loads: GX driver DLLs (LoadLibraryA + GetProcAddress for
  `gxDLLInfo`/`gxDLLInit`/`gxDLLExit`). WSOCK32/DSOUND are static ordinal
  imports (NOT dynamic — see §1).
- GX driver selection stored in registry `Software\UDS\No Fear MTB\Drivers`
  (keys: Path, Version; "No Fear MTB" = engine codename). FX_GLIDE_NO_SPLASH
  env var set before loading Glide.
- Imports: KERNEL32 (87), USER32 (21), GDI32 (2), DINPUT (1), WINMM (4),
  ADVAPI32 (3) by name; **WSOCK32 (13 ordinals)** + **DSOUND (1 ordinal)** by
  ordinal. [VERIFIED 2026-08-10] The "ordinal imports at ~0x4da74-0x4da7e"
  hypothesis was wrong: those addresses are Ghidra's synthetic IAT slot numbers
  for by-name KERNEL32 `GetACP`/`GetOEMCP` (the MSVC CRT code-page init pair).
  The real ordinal-only imports are WSOCK32/DSOUND, present in the PE import
  table (Import Directory @RVA 0x4cff0): WSOCK32 = socket/bind/closesocket/
  htons/ntohs/inet_addr/gethostbyname/recvfrom/sendto/setsockopt/WSAStartup/
  WSACleanup/WSAGetLastError, DSOUND #1 = DirectSoundCreate. WSOCK32/DSOUND
  are NOT loaded dynamically (LoadLibraryA is used only for GX driver DLLs and
  a runtime user32 MessageBox wrapper @0x4479ae); corrected the earlier
  "not in import table" note.
