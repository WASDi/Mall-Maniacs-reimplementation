# Differential tests: original vs rebuild

Runs a function from the original `maniac.exe` in Unicorn and the same
function from a 32-bit build of `src/`, using identical inputs and memory.
It then compares the return value, the arena, every synced global, the
heap, the allocation sequence and the `rand()` seed.

```bash
python3 -m venv tests/difftest/.venv
tests/difftest/.venv/bin/pip install unicorn==2.1.4 pefile pyelftools
tests/difftest/build.sh          # 32-bit driver + globals map (needs gcc-multilib)
tests/difftest/.venv/bin/python tests/difftest/difftest.py -n 1000 [func ...]
```

## Files
- `build.sh` builds `build-difftest/rebuild_driver`, with `-m32
  -malign-double -O0`. At `-O0`, `static const` values survive, so they can
  be checked. Missing backend symbols become trap stubs. The script then
  reruns `gen_globals.py`, because rebuild addresses change with every link.
- `gen_globals.py` turns the `/* @0x4xxxxx */` comments on global
  definitions into `build-difftest/globals_map.json`, and reports:
  - `size`: rebuild `sizeof` differs from Ghidra's data item size.
  - `overlap`: e.g. an original struct array split into parallel arrays.
  - `init`: the rebuild's initial bytes differ from the original image.
    This catches wrong constants.
  - `missing`: annotated, but not found in the driver.
- `ghidra_data.tsv` is a Ghidra export of labeled data, made with
  `~/ghidra_scripts/DumpGlobalData.java` (see `Ghidra_scripts.md`). It
  gives sizes, plus names for unsynced addresses.
- `harness.py` is the engine. `cases_math.py` and `cases_world.py` hold
  the generators. `difftest.py` is the command line.

## How the two sides line up
- **Struct layout**: the 32-bit build sets `MANIAC_32BIT_RUNTIME_LAYOUT`,
  so runtime structs have their original offsets.
- **Arena**: test objects live at `0x10000000` on both sides.
- **Heap**: allocations come from one 16-byte bump allocator at
  `0x20000000` on both sides. In the emulator it answers the CRT entry
  points (`operator_new`, `_malloc`, `crtCalloc`, `crtRealloc`,
  `memFree`). In the driver it sits behind `-Wl,--wrap=malloc,...`. Equal
  allocation sequences therefore give equal pointers. Frees are logged,
  never executed.
- **Globals**: every writable, cleanly mapped global (no `size`, `overlap`
  or `init` issue) is written before each call, at its original address on
  one side and its rebuild address on the other, then read back. The
  baseline is the original image's initial bytes, with pointers set to
  NULL. Cases override values by name. `('addr', name, off)` gives a
  pointer to a global that is correct on each side. Reads and writes the
  original makes to unsynced `.data` are listed per function, naming gaps
  in the map.
  A `sync:0xNN` tag in the definition comment syncs only that prefix
  (e.g. `g_rootNode`, of which the original keeps only the 0x38-byte header).
- **Pointers to globals**: a case's `arena_ptrs` list of
  `(offset, global, add)` is written as each side's own address before
  the call. When comparing, a rebuild dword that differs from the original
  and points into a rebuild global is read as the original's address.
- **`rand()`**: the MSVC LCG on a per-case seed, hooked at `_rand`
  @0x43ea7c and wrapped in the driver. A different final seed means a
  different number of `rand()` calls.
- **Emulator setup**:
  - FPU control word `0x27F` (53-bit precision), so `double` math in the
    rebuild reproduces the original's x87 results.
  - A GDT gives FS a fake TEB, so MSVC SEH prologues (`MOV EAX,FS:[0]`)
    run. SS/DS/ES get a flat segment.
  - Float returns are read from `FP[TOP]`: Unicorn's `ST0` register is
    physical FP0.
  - Imports trap and are reported as `error`, naming the import that
    needs a model.

Arguments are given in the original's logical order, up to 12. For
`thiscall` and `fastcall` with one argument, `args[0]` goes in ECX.

## Verdicts
- `exact`: bit-identical results.
- `noise`: floats within `--ulps` (default 4).
- `MISMATCH`: anything else.

The script exits non-zero on any `MISMATCH` or `error`.

## Known failures
None: every function in `TESTS` matches. `sndPlaySfx3D` cases build
older emitters with real music-node `SceneNode`s in the arena (under an
optional pos node or the root), so both dedupe branches and
`musicEmitterAlloc` @0x431b00 are covered.
