"""Engine for the differential tests: original maniac.exe vs the 32-bit rebuild.

The original function runs in Unicorn on the PE image of maniac.exe; the
rebuild function runs natively in build-difftest/rebuild_driver (build.sh).
Both sides see identical memory at identical addresses:

  arena    ARENA_BASE  objects a test case builds (pointers are equal)
  heap     HEAP_BASE   one 16-byte-aligned bump allocator on both sides
                       (CRT entry points hooked here, malloc/free wrapped there)
  globals  every writable, cleanly mapped global in globals_map.json
           (gen_globals.py), at its original address resp. rebuild address

rand() is the MSVC LCG on a per-case seed. After each call the return
value, arena, globals, heap bytes, allocation log and final seed are
compared.
"""
import bisect
import json
import math
import os
import struct
import subprocess

import pefile
from unicorn import (Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE,
                     UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE)
from unicorn import x86_const as X

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
EXE = "/home/wasd/MallManiacsUnmodified/maniac.exe"
DRIVER = os.path.join(ROOT, "build-difftest", "rebuild_driver")
GLOBALS_MAP = os.path.join(ROOT, "build-difftest", "globals_map.json")
GHIDRA_DATA = os.path.join(HERE, "ghidra_data.tsv")

ARENA_BASE = 0x10000000
ARENA_SIZE = 0x10000
HEAP_BASE = 0x20000000
HEAP_SIZE = 0x100000
STACK_BASE = 0x00100000
STACK_SIZE = 0x00100000
TRAP_BASE = 0x7FF00000      # one 4-byte slot per import; executing one = test error
RET_SENTINEL = 0x7FFF0000   # return address pushed for the call under test
MAX_INSNS = 5_000_000
TEB_BASE = 0x7FFD0000      # fake thread block: FS:[0] SEH chain head (-1 = none)
GDT_BASE = 0x7FFE0000
FPCW_MSVC = 0x027F          # MSVC CRT default: 53-bit precision, round-nearest
DATA_LO = 0x0044E000        # original .data (writable globals)

ALLOC_KINDS = ("malloc", "calloc", "realloc", "free")

# Original CRT entry points answered by the emulator instead of being run
# (they reach HeapAlloc / TLS). name: (address, cdecl arg count)
CRT_HOOKS = {
    "operator_new": (0x43DD42, 1),
    "_malloc":      (0x43EE5F, 1),
    "crtCalloc":    (0x445EA9, 2),
    "crtRealloc":   (0x44383C, 2),
    "memFree":      (0x43EF99, 1),
    "_rand":        (0x43EA7C, 0),
}


class EmuError(Exception):
    pass


def _gdt_desc(base, limit, access=0x93, flags=0xC):
    """Flat 32-bit data-segment descriptor (present, ring 0, writable)."""
    return struct.pack("<Q", (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (access << 40) |
                       (((limit >> 16) & 0xF) << 48) | (flags << 52) | ((base >> 24) << 56))


# ---------------------------------------------------------------- globals

class GlobalMap:
    """Synced globals plus Ghidra's labels for naming anything else."""

    def __init__(self, image_read):
        with open(GLOBALS_MAP) as f:
            entries = json.load(f)
        self.by_name = {}
        for e in entries:
            self.by_name.setdefault(e["name"], e)
        self.synced = [e for e in entries if e["writable"] and not e["issues"]]
        # Baseline = the original's initial image bytes; pointers start NULL
        # because original and rebuild pointer values cannot agree.
        self.baseline = {}
        for e in self.synced:
            self.baseline[e["name"]] = bytes(e["size"]) if e["pointer"] else image_read(e["orig"], e["size"])
        self.ranges = sorted((e["orig"], e["orig"] + e["size"]) for e in self.synced)
        self.labels = []
        with open(GHIDRA_DATA) as f:
            for line in f:
                if not line.startswith("#"):
                    a, size, name, typ, _ = line.rstrip("\n").split("\t")
                    self.labels.append((int(a, 16), int(size), name, typ))
        self.labels.sort()
        self.label_addrs = [l[0] for l in self.labels]

    def is_synced(self, addr):
        i = bisect.bisect_right(self.ranges, (addr, 0xFFFFFFFF)) - 1
        return i >= 0 and self.ranges[i][0] <= addr < self.ranges[i][1]

    def label(self, addr):
        i = bisect.bisect_right(self.label_addrs, addr) - 1
        if i >= 0:
            a, size, name, typ = self.labels[i]
            if addr < a + max(size, 1):
                return name if addr == a else "%s+0x%x" % (name, addr - a)
        return "0x%x" % addr

    def resolve(self, value, side):
        """Case values: bytes, int (u32), float (f32), ('addr', name, off)."""
        if isinstance(value, tuple) and value[0] == "addr":
            e = self.by_name[value[1]]
            return struct.pack("<I", (e["orig"] if side == "orig" else e["reb"]) + value[2])
        if isinstance(value, float):
            return struct.pack("<f", value)
        if isinstance(value, int):
            return struct.pack("<I", value & 0xFFFFFFFF)
        return bytes(value)

    def patch_arena(self, arena, ptrs, side):
        """Arena copy with [(offset, global name, add)] written as that side's address."""
        out = bytearray(arena)
        for off, name, add in ptrs:
            out[off: off + 4] = self.resolve(("addr", name, add), side)
        return out

    def to_orig(self, r_mem, o_mem):
        """r_mem with each aligned dword that differs from o_mem, and points into a
        rebuild global at the original's matching address, rewritten to that address."""
        if not hasattr(self, "_reb_ranges"):
            self._reb_ranges = sorted((e["reb"], e["reb"] + e["size"], e["orig"])
                                      for e in self.by_name.values())
            self._reb_starts = [r[0] for r in self._reb_ranges]
        out = bytearray(r_mem)
        for i in range(0, min(len(out), len(o_mem)) - 3, 4):
            if out[i: i + 4] == o_mem[i: i + 4]:
                continue
            v, = struct.unpack_from("<I", out, i)
            j = bisect.bisect_right(self._reb_starts, v) - 1
            if j >= 0 and v < self._reb_ranges[j][1]:
                t = v - self._reb_ranges[j][0] + self._reb_ranges[j][2]
                if struct.unpack_from("<I", o_mem, i)[0] == t:
                    struct.pack_into("<I", out, i, t)
        return out

    def state(self, overrides, side):
        """[(address on that side, bytes)] for all synced globals."""
        out = []
        for e in self.synced:
            data = self.baseline[e["name"]]
            if e["name"] in overrides:
                v = self.resolve(overrides[e["name"]], side)
                data = (v + data[len(v):])[: e["size"]]
            out.append((e["orig"] if side == "orig" else e["reb"], data))
        return out


# ---------------------------------------------------------------- original

def x87_to_float(reg):
    if isinstance(reg, int):  # packed 80-bit: exponent/sign in bits 64..79
        mant, exp = reg & 0xFFFFFFFFFFFFFFFF, (reg >> 64) & 0xFFFF
    else:
        mant, exp = reg
    sign = -1.0 if exp & 0x8000 else 1.0
    exp &= 0x7FFF
    if exp == 0x7FFF:
        return sign * math.inf if (mant << 1) & 0xFFFFFFFFFFFFFFFF == 0 else math.nan
    if mant == 0:
        return sign * 0.0
    return sign * math.ldexp(mant, exp - 16383 - 63)


class Original:
    """maniac.exe mapped at its image base inside Unicorn."""

    def __init__(self, path=EXE):
        pe = pefile.PE(path)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        size = (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF
        image = bytearray(size)
        image[: pe.OPTIONAL_HEADER.SizeOfHeaders] = pe.get_data(0, pe.OPTIONAL_HEADER.SizeOfHeaders)
        for s in pe.sections:
            raw = s.get_data()
            image[s.VirtualAddress: s.VirtualAddress + len(raw)] = raw
        # Point every IAT slot at a trap address and remember its name.
        self.traps = {}
        n = 0
        for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            dll = entry.dll.decode()
            for imp in entry.imports:
                addr = TRAP_BASE + 4 * n
                name = imp.name.decode() if imp.name else "ord%d" % imp.ordinal
                self.traps[addr] = "%s!%s" % (dll, name)
                struct.pack_into("<I", image, imp.address - self.base, addr)
                n += 1
        self.pristine = bytes(image)  # initial image values (globals baseline)
        self.image = bytes(image)
        self.image_end = self.base + size

    def read_image(self, addr, size):
        return self.pristine[addr - self.base: addr - self.base + size]

    def call(self, addr, conv, args, arena, globals_state, globals_map, seed):
        """Run the original function. conv: 'cdecl' | 'stdcall' | 'thiscall'."""
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(self.base, len(self.image))
        uc.mem_write(self.base, self.image)
        for base, size in ((ARENA_BASE, ARENA_SIZE), (HEAP_BASE, HEAP_SIZE), (STACK_BASE, STACK_SIZE),
                           (TRAP_BASE, 0x10000), (RET_SENTINEL, 0x1000), (TEB_BASE, 0x1000),
                           (GDT_BASE, 0x1000)):
            uc.mem_map(base, size)
        # FS -> TEB so MSVC SEH prologues (MOV EAX,FS:[0]) work.
        # Loading a GDT makes every segment register resolve through it, so
        # SS/DS/ES get a flat 4 GiB segment (4K granularity) as well.
        gdt = bytes(8) + _gdt_desc(TEB_BASE, 0xFFF) + _gdt_desc(0, 0xFFFFF)
        uc.mem_write(GDT_BASE, gdt)
        uc.reg_write(X.UC_X86_REG_GDTR, (0, GDT_BASE, len(gdt) - 1, 0))
        for reg in (X.UC_X86_REG_SS, X.UC_X86_REG_DS, X.UC_X86_REG_ES):
            uc.reg_write(reg, 2 << 3)
        uc.reg_write(X.UC_X86_REG_FS, 1 << 3)
        uc.mem_write(TEB_BASE, struct.pack("<II", 0xFFFFFFFF, STACK_BASE + STACK_SIZE))
        uc.mem_write(ARENA_BASE, bytes(arena))
        for gaddr, data in globals_state:
            uc.mem_write(gaddr, data)

        stack_args = list(args)
        if conv == "thiscall":
            uc.reg_write(X.UC_X86_REG_ECX, stack_args.pop(0))
        sp = STACK_BASE + STACK_SIZE - 0x100
        for a in reversed(stack_args):
            sp -= 4
            uc.mem_write(sp, struct.pack("<I", a & 0xFFFFFFFF))
        sp -= 4
        uc.mem_write(sp, struct.pack("<I", RET_SENTINEL))
        uc.reg_write(X.UC_X86_REG_ESP, sp)
        uc.reg_write(X.UC_X86_REG_FPCW, FPCW_MSVC)

        st = dict(heap=HEAP_BASE, allocs=[], seed=seed, trap=None,
                  unsynced_w=set(), unsynced_r=set())

        def bump(kind, n):
            a = st["heap"]
            st["heap"] += (n + 15) & ~15
            if st["heap"] > HEAP_BASE + HEAP_SIZE:
                raise EmuError("emulated heap exhausted")
            st["allocs"].append((kind, n, a))
            return a

        def crt_hook(uc_, address, size, name):
            esp = uc_.reg_read(X.UC_X86_REG_ESP)
            ret, a0, a1 = struct.unpack("<III", uc_.mem_read(esp, 12))
            if name in ("operator_new", "_malloc"):
                eax = bump(0, a0)
            elif name == "crtCalloc":
                eax = bump(1, a0 * a1)
                uc_.mem_write(eax, bytes(a0 * a1))
            elif name == "crtRealloc":
                eax = bump(2, a1)
                old = next((n for k, n, p in reversed(st["allocs"]) if p == a0 and k != 3), 0)
                if a0:
                    uc_.mem_write(eax, bytes(uc_.mem_read(a0, min(old, a1))))
            elif name == "memFree":
                if a0:
                    st["allocs"].append((3, 0, a0))
                eax = 0
            else:  # _rand
                st["seed"] = (st["seed"] * 0x343FD + 0x269EC3) & 0xFFFFFFFF
                eax = (st["seed"] >> 16) & 0x7FFF
            uc_.reg_write(X.UC_X86_REG_EAX, eax)
            uc_.reg_write(X.UC_X86_REG_ESP, esp + 4)
            uc_.reg_write(X.UC_X86_REG_EIP, ret)

        def on_trap(uc_, address, size, user):
            st["trap"] = self.traps.get(address, hex(address))
            uc_.emu_stop()

        def on_write(uc_, access, address, size, value, user):
            if DATA_LO <= address < self.image_end and not globals_map.is_synced(address):
                st["unsynced_w"].add(globals_map.label(address))

        def on_read(uc_, access, address, size, value, user):
            if DATA_LO <= address < self.image_end and not globals_map.is_synced(address):
                st["unsynced_r"].add(globals_map.label(address))

        for name, (haddr, _) in CRT_HOOKS.items():
            uc.hook_add(UC_HOOK_CODE, crt_hook, user_data=name, begin=haddr, end=haddr)
        uc.hook_add(UC_HOOK_CODE, on_trap, begin=TRAP_BASE, end=TRAP_BASE + 0xFFFF)
        uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=DATA_LO, end=self.image_end)
        uc.hook_add(UC_HOOK_MEM_READ, on_read, begin=DATA_LO, end=self.image_end)
        try:
            uc.emu_start(addr, RET_SENTINEL, count=MAX_INSNS)
        except UcError as e:
            raise EmuError("original faulted at 0x%x: %s" % (uc.reg_read(X.UC_X86_REG_EIP), e))
        if st["trap"]:
            raise EmuError("original called import %s (needs a model)" % st["trap"])
        if uc.reg_read(X.UC_X86_REG_EIP) != RET_SENTINEL:
            raise EmuError("original did not return within %d instructions" % MAX_INSNS)
        return dict(
            eax=uc.reg_read(X.UC_X86_REG_EAX),
            # UC_X86_REG_ST0 is physical FP0; the stack top is FP[TOP].
            st0=x87_to_float(uc.reg_read(X.UC_X86_REG_FP0 + ((uc.reg_read(X.UC_X86_REG_FPSW) >> 11) & 7))),
            arena=bytearray(uc.mem_read(ARENA_BASE, len(arena))),
            globals=[bytes(uc.mem_read(a, len(d))) for a, d in globals_state],
            heap=bytes(uc.mem_read(HEAP_BASE, st["heap"] - HEAP_BASE)),
            allocs=st["allocs"], seed=st["seed"],
            unsynced_w=st["unsynced_w"], unsynced_r=st["unsynced_r"])


# ---------------------------------------------------------------- rebuild

class Rebuild:
    """The 32-bit rebuild driver as a persistent subprocess."""

    def __init__(self):
        self.proc = subprocess.Popen([DRIVER], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def call(self, name, ret_kind, args, arena, globals_state, seed):
        args = [a & 0xFFFFFFFF for a in args] + [0] * (12 - len(args))
        req = [struct.pack("<64sII12IIII", name.encode(), ret_kind, len(args), *args,
                           len(arena), len(globals_state), seed & 0xFFFFFFFF), bytes(arena)]
        req += [struct.pack("<II", a, len(d)) for a, d in globals_state]
        req += [d for _, d in globals_state]
        try:
            self.proc.stdin.write(b"".join(req))
            self.proc.stdin.flush()
        except BrokenPipeError:
            pass
        hdr = self.proc.stdout.read(24)
        if len(hdr) != 24:
            raise EmuError("rebuild driver died (rc=%s)" % self.proc.wait())
        eax, st0, seed_out, heap_used, n_allocs = struct.unpack("<IdIII", hdr)
        out = dict(eax=eax, st0=st0, seed=seed_out)
        out["arena"] = bytearray(self.proc.stdout.read(len(arena)))
        out["globals"] = [self.proc.stdout.read(len(d)) for _, d in globals_state]
        out["heap"] = self.proc.stdout.read(heap_used)
        raw = self.proc.stdout.read(12 * n_allocs)
        out["allocs"] = [struct.unpack_from("<III", raw, 12 * i) for i in range(n_allocs)]
        return out


# ---------------------------------------------------------------- comparison

def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def fbits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def ulp_dist(a, b):
    """Distance in float32 ULPs between two raw 32-bit patterns."""
    def key(u):
        return u if u < 0x80000000 else 0x80000000 - u
    return abs(key(a) - key(b))


FIELD_FMT = {"f32": "<f", "i32": "<i", "u32": "<I", "i16": "<h", "u16": "<H", "u8": "<B", "i8": "<b"}
FIELD_SIZE = {"f32": 4, "i32": 4, "u32": 4, "i16": 2, "u16": 2, "u8": 1, "i8": 1}


class Verdict:
    ORDER = ["exact", "noise", "MISMATCH"]

    def __init__(self):
        self.level = "exact"
        self.notes = []

    def add(self, level, note):
        if self.ORDER.index(level) > self.ORDER.index(self.level):
            self.level = level
        self.notes.append(note)


def _cmp_fields(v, region, o_mem, r_mem, fields, float_ulps):
    covered = set()
    for name, off, typ in fields:
        size = FIELD_SIZE[typ]
        covered.update(range(off, off + size))
        ob, rb = o_mem[off: off + size], r_mem[off: off + size]
        if ob == rb:
            continue
        ov = struct.unpack(FIELD_FMT[typ], ob)[0]
        rv = struct.unpack(FIELD_FMT[typ], rb)[0]
        if typ == "f32":
            d = ulp_dist(struct.unpack("<I", ob)[0], struct.unpack("<I", rb)[0])
            v.add("noise" if d <= float_ulps else "MISMATCH",
                  "%s%s: original %r, rebuild %r (%d ulp)" % (region, name, ov, rv, d))
        else:
            v.add("MISMATCH", "%s%s: original %r, rebuild %r" % (region, name, ov, rv))
    n = min(len(o_mem), len(r_mem))
    stray = [i for i in range(n) if o_mem[i] != r_mem[i] and i not in covered]
    if stray:
        v.add("MISMATCH", "%sunlisted bytes differ at %s" % (region, ", ".join("+0x%x" % i for i in stray[:8])))


def describe(data):
    if len(data) == 4:
        i, = struct.unpack("<i", data)
        f, = struct.unpack("<f", data)
        return "%d / %r" % (i, f)
    if len(data) <= 8:
        return data.hex()
    return "%d bytes" % len(data)


def compare(case, o, r, gmap, float_ulps):
    v = Verdict()
    ret = case.get("ret", "void")
    if ret == "int" and o["eax"] != r["eax"]:
        v.add("MISMATCH", "return EAX: original 0x%x, rebuild 0x%x" % (o["eax"], r["eax"]))
    elif ret == "float":
        d = ulp_dist(fbits(o["st0"]), fbits(r["st0"]))
        if d:
            v.add("noise" if d <= float_ulps else "MISMATCH",
                  "return ST0: original %r, rebuild %r (%d ulp)" % (o["st0"], r["st0"], d))

    # Pointers to globals differ by side; compare them as original addresses.
    r_arena, r_heap = gmap.to_orig(r["arena"], o["arena"]), gmap.to_orig(r["heap"], o["heap"])
    _cmp_fields(v, "", o["arena"], r_arena, case.get("fields", []), float_ulps)

    for e, ob, rb in zip(gmap.synced, o["globals"], r["globals"]):
        if ob == rb:
            continue
        floaty = "float" in e["decl"] and e["size"] == 4
        if floaty:
            d = ulp_dist(struct.unpack("<I", ob)[0], struct.unpack("<I", rb)[0])
            v.add("noise" if d <= float_ulps else "MISMATCH",
                  "global %s: original %r, rebuild %r (%d ulp)" % (e["name"], struct.unpack("<f", ob)[0],
                                                                    struct.unpack("<f", rb)[0], d))
        else:
            v.add("MISMATCH", "global %s @0x%x: original %s, rebuild %s" % (e["name"], e["orig"], describe(ob), describe(rb)))

    oa = [(ALLOC_KINDS[k], n) for k, n, _ in o["allocs"]]
    ra = [(ALLOC_KINDS[k], n) for k, n, _ in r["allocs"]]
    if oa != ra:
        v.add("MISMATCH", "allocations: original %s, rebuild %s" % (oa[:6], ra[:6]))
    else:
        _cmp_fields(v, "heap", o["heap"], r_heap, case.get("heap_fields", []), float_ulps)
    if o["seed"] != r["seed"]:
        v.add("MISMATCH", "rand seed after call: original 0x%x, rebuild 0x%x (different rand() call count)"
              % (o["seed"], r["seed"]))
    return v


def run_case(orig, reb, gmap, spec, case, float_ulps):
    addr, conv, sym = spec
    seed = case.get("seed", 1)
    o_state = gmap.state(case.get("globals", {}), "orig")
    r_state = gmap.state(case.get("globals", {}), "reb")
    ptrs = case.get("arena_ptrs", [])
    o = orig.call(addr, conv, case["args"], gmap.patch_arena(case["arena"], ptrs, "orig"), o_state, gmap, seed)
    r = reb.call(sym, 1 if case.get("ret") == "float" else 0, case["args"],
                 gmap.patch_arena(case["arena"], ptrs, "reb"), r_state, seed)
    return compare(case, o, r, gmap, float_ulps), o
