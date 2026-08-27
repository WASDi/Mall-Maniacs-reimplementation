# Mall Maniacs (maniac.exe) — 01. Binary overview & toolchain

[Back to README](README.md)

Status: Static-analysis baseline complete: 1178/1178 functions in scope are
documented. Subsystem behavior and detailed evidence remain in the linked notes.

## Purpose and baseline

`maniac.exe` is a 1999 32-bit x86 Windows game by AddGames Advertainment AB /
UDS (Sweden), with Swedish menus and a 3D mall-management theme. Its
little-endian PE has image base `0x00400000`, approximately 403 KB of code
(`0x401000-0x44afff`), no exports, one entry point, 1238 identified functions,
and 290 data types.

It combines DirectInput, WinMM/MCI, GDI, and the custom GX graphics engine.
GX selects `GXSOFT.DLL` or 3dfx/OpenGL alternatives through `gxDLLInfo`,
`gxDLLInit`, and `gxDLLExit`; selection uses registry
`Software\UDS\No Fear MTB\Drivers` (`Path`, `Version`) and Glide sets
`FX_GLIDE_NO_SPLASH`.

## Toolchain and imports

It is MSVC6-era, statically links the MSVC C++ runtime, and indicates a
DirectX6-era environment (per `readme.txt`). CRT region `0x43c850-0x44966c`
is mapped and renamed, covering config iostreams, file I/O, memory,
ctype/float, TLS/errno, and C++ EH (`crtCxxFrameHandler @0x4413d4`). See
[06-config.md](06-config.md), [13-msvc-crt.md](13-msvc-crt.md), and
[11-issues.md](11-issues.md) §16.

Named imports are from KERNEL32 (87), USER32 (21), GDI32 (2), DINPUT (1),
WINMM (4), and ADVAPI32 (3); WSOCK32 has 13 ordinal UDP imports and DSOUND
has ordinal 1 (`DirectSoundCreate`). Import Directory: RVA `0x4cff0`.
[VERIFIED 2026-08-10] The `0x4da74-0x4da7e` ordinal-IAT hypothesis was wrong:
those are Ghidra synthetic slots for named KERNEL32 `GetACP`/`GetOEMCP` CRT
imports. WSOCK32/DSOUND are PE imports, not dynamic loads; `LoadLibraryA` is
for GX drivers (and a USER32 message-box wrapper at `0x4479ae`).

## Current status and limits

Architecture, major systems, data formats, globals, and the statically linked
CRT are mapped and renamed. This overview is not a claim that every behavior
or data interpretation is verified; remaining hypotheses and open issues stay
in the subsystem notes.

Source reconstruction starts with the offline GUI and software GX driver;
networking, DirectInput, DirectSound, and other nonessential systems remain
deferred.
