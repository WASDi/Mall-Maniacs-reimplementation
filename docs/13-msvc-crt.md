# Mall Maniacs (maniac.exe) — 13. Statically-linked MSVC CRT

[Back to README](README.md)

Status:

The 130-function statically linked MSVC6 CRT and C++ exception-runtime region
from `0x43c850` through `0x44966c` is fully mapped and renamed. This analysis
is complete as a reverse-engineering reference; the rebuild deliberately does
not reproduce the CRT or other system/compiler runtime code.

## Purpose and scope

This document identifies the runtime boundary around the game code. The
`stream*` names cover the iostream/file serialization layer used by config
saves. The `crt*` and `eh*` names cover the statically linked MSVC6 services
used by the executable, including I/O, allocation, locale and conversion,
environment and timezone handling, TLS/`errno`, startup/exit, locking, and
C++ exception handling.

The original range includes the MSVC small-block heap and a secondary
large-block allocator, FILE-descriptor and stream buffering state, CRT lock
tables, ctype/code-page tables, environment and timezone globals, and the
thread-local per-thread-data/`errno` machinery. These are useful for tracing
call paths, but are not rebuild targets.

## Key findings

- The region is a statically linked MSVC6 runtime, not game logic. Its names
  use the `stream*`, `crt*`, and `eh*` prefixes and retain original entry
  addresses for cross-referencing.
- The stream layer at `0x43c850-0x43dfbb` supports config-save I/O and
  serialization. CRT I/O, buffering, allocation, conversion, and TLS/`errno`
  services surround it; C++ EH uses the MSVC signatures and magic values
  `0x19930520` and `0xe06d7363`.
- The allocator evidence includes the MSVC small-block heap, a heap2
  large-block path, `HeapAlloc`/`VirtualAlloc`, and lock-by-index wrappers.
  This explains runtime ownership and synchronization seen in xrefs without
  making those implementation details part of the rebuild.
- Environment, locale/code-page, timezone/DST, FILE-descriptor, and per-thread
  data globals were identified from their API xrefs and initialization paths.

## Evidence and limitations

The mapping is based on decompilation, disassembly, API xrefs, initialization
paths, and the observed MSVC EH signatures. Addresses in this overview are
entry/range anchors for the Ghidra program `maniac.exe`; they are not source
locations. Some individual global roles remain hypotheses where the binary
only exposes indirect table access, and the document intentionally omits the
former every-symbol inventory.

The CRT is external-runtime infrastructure for Phase 2. Do not port its heap,
stdio, locale, startup, or EH internals into `src/`; use the corresponding
MinGW/Win32 facilities and keep unresolved game behavior in the documented
rebuild stubs. The stream/config boundary is the exception: implement only
the game-facing serialization behavior when a rebuild milestone requires it.

## Next useful direction

Continue reconstruction in the game-owned subsystems described by
`docs/16-rebuild.md`, beginning with the next visible single-player GUI state.
Use this CRT map only to distinguish game functions and data from compiler or
runtime support when following xrefs; update this overview only when the
runtime boundary or its relevance to the rebuild changes.
