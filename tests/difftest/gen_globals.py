#!/usr/bin/env python3
"""Build the original-address <-> rebuild-symbol map for globals.

Sources:
  * `/* @0x4xxxxx */` comments on global definitions in src/*.c
  * the symbol table of build-difftest/rebuild_driver (address, size)
  * tests/difftest/ghidra_data.tsv (DumpGlobalData.java: Ghidra's size/type)

Writes build-difftest/globals_map.json and prints a validation report:
  size     rebuild sizeof differs from Ghidra's data item size
  overlap  two rebuild globals claim overlapping original ranges
           (usually an original struct array split into parallel arrays)
  init     initial bytes differ between the rebuild and the original image
           (pointer-typed globals are skipped: their values cannot match)
  missing  annotated but not in the driver (e.g. a const folded away)

    tests/difftest/.venv/bin/python tests/difftest/gen_globals.py [-v]
"""
import bisect
import glob
import json
import os
import re
import sys

import pefile
from elftools.elf.elffile import ELFFile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
EXE = "/home/wasd/MallManiacsUnmodified/maniac.exe"
DRIVER = os.path.join(ROOT, "build-difftest", "rebuild_driver")
OUT = os.path.join(ROOT, "build-difftest", "globals_map.json")

ORIG_DATA_START = 0x44E000  # .data; .rdata below it is read-only
ADDR_RE = re.compile(r"@0x(4[0-9a-fA-F]{5})\b")
SYNC_RE = re.compile(r"\bsync:0x([0-9a-fA-F]+)\b")  # sync only this prefix of the global
SKIP_PREFIX = ("extern", "return", "typedef", "#", "}", "case", "default", "if", "else")


def scan_sources():
    """[(file, line, name, orig_addr, is_pointer, decl, sync_len)] for annotated definitions."""
    out = []
    for path in sorted(glob.glob(os.path.join(ROOT, "src", "*.c"))):
        with open(path, encoding="utf-8", errors="replace") as f:
            for lineno, line in enumerate(f, 1):
                if not line[:1].isalpha() and line[:1] != "_":
                    continue
                if line.startswith(SKIP_PREFIX) or "/*" not in line:
                    continue
                decl, comment = line.split("/*", 1)
                m = ADDR_RE.search(comment)
                if not m or "(" in decl:
                    continue
                d = decl.strip()
                if not d.endswith((";", "{", "=")) and "=" not in d:
                    continue
                d = d.split("=", 1)[0].rstrip(";{ ")
                d_noarr = re.sub(r"\[[^\]]*\]", "", d).strip()
                ident = re.findall(r"[A-Za-z_]\w*", d_noarr)
                if len(ident) < 2:
                    continue
                ms = SYNC_RE.search(comment)
                out.append((os.path.relpath(path, ROOT), lineno, ident[-1], int(m.group(1), 16),
                            "*" in d_noarr, d, int(ms.group(1), 16) if ms else None))
    return out


def elf_symbols(path):
    """{(name, file_basename or None): (addr, size)} for data objects."""
    syms = {}
    with open(path, "rb") as f:
        elf = ELFFile(f)
        symtab = elf.get_section_by_name(".symtab")
        cur_file = None
        for s in symtab.iter_symbols():
            t = s["st_info"]["type"]
            if t == "STT_FILE":
                cur_file = os.path.basename(s.name)
                continue
            if t != "STT_OBJECT" or s["st_shndx"] == "SHN_UNDEF":
                continue
            local = s["st_info"]["bind"] == "STB_LOCAL"
            syms[(s.name, cur_file if local else None)] = (s["st_value"], s["st_size"])
        sections = [(sec["sh_addr"], sec["sh_size"], sec) for sec in elf.iter_sections()
                    if sec["sh_addr"] and sec["sh_type"] in ("SHT_PROGBITS", "SHT_NOBITS")]
        def read(addr, size):
            for base, sz, sec in sections:
                if base <= addr and addr + size <= base + sz:
                    if sec["sh_type"] == "SHT_NOBITS":
                        return bytes(size)
                    return sec.data()[addr - base: addr - base + size]
            return None
        def writable(addr):
            return any(base <= addr < base + sz and sec["sh_flags"] & 1  # SHF_WRITE
                       for base, sz, sec in sections)
        init = {k: read(a, n) for k, (a, n) in syms.items()}
        wr = {k: writable(a) for k, (a, n) in syms.items()}
    return syms, init, wr


def pe_reader(path):
    pe = pefile.PE(path, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    def read(addr, size):
        rva = addr - base
        for s in pe.sections:
            if s.VirtualAddress <= rva and rva + size <= s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
                raw = s.get_data()
                off = rva - s.VirtualAddress
                chunk = raw[off: off + size]
                return chunk + bytes(size - len(chunk))  # tail past raw data is zero-filled
        return None
    return read


def ghidra_items():
    items = {}
    with open(os.path.join(HERE, "ghidra_data.tsv")) as f:
        for line in f:
            if line.startswith("#"):
                continue
            addr, size, name, typ, blk = line.rstrip("\n").split("\t")
            items[int(addr, 16)] = (int(size), name, typ)
    return items


def main():
    verbose = "-v" in sys.argv
    defs = scan_sources()
    syms, init, wr = elf_symbols(DRIVER)
    pe_read = pe_reader(EXE)
    gitems = ghidra_items()
    gaddrs = sorted(gitems)

    entries, issues = [], {"size": [], "overlap": [], "init": [], "missing": []}
    for file, line, name, orig, is_ptr, decl, sync_len in defs:
        key = (name, os.path.basename(file)) if (name, os.path.basename(file)) in syms else (name, None)
        if key not in syms:
            issues["missing"].append("%s:%d %s @0x%x" % (file, line, name, orig))
            continue
        reb, size = syms[key]
        if sync_len is not None:
            size = min(size, sync_len)
        g = gitems.get(orig)
        gsize = g[0] if g else None
        e = dict(name=name, file=file, line=line, orig=orig, reb=reb, size=size,
                 ghidra_size=gsize, ghidra_type=g[2] if g else None, pointer=is_ptr, decl=decl, issues=[],
                 writable=wr[key] and orig >= ORIG_DATA_START)
        if gsize is not None and gsize != size:
            # A smaller Ghidra item followed by unlabeled bytes is fine when
            # the next labeled item starts at or after orig+size.
            i = bisect.bisect_right(gaddrs, orig)
            nxt = gaddrs[i] if i < len(gaddrs) else None
            if size < gsize or (nxt is not None and nxt < orig + size):
                e["issues"].append("size")
                issues["size"].append("%s:%d %s @0x%x rebuild %d bytes, Ghidra %s %d bytes"
                                      % (file, line, name, orig, size, g[2], gsize))
        ob, rb = pe_read(orig, size), init.get(key)
        rb = rb[:size] if rb is not None else None
        if not is_ptr and ob is not None and rb is not None and ob != rb and not e["issues"]:
            diff = next(i for i in range(size) if ob[i] != rb[i])
            e["issues"].append("init")
            issues["init"].append("%s:%d %s @0x%x first diff at +0x%x: original %s, rebuild %s"
                                  % (file, line, name, orig, diff, ob[diff:diff + 8].hex(), rb[diff:diff + 8].hex()))
        entries.append(e)

    entries.sort(key=lambda e: e["orig"])
    for a, b in zip(entries, entries[1:]):
        if (a["orig"], a["size"]) == (b["orig"], b["size"]):
            continue  # two rebuild names for one original item (alias)
        if b["orig"] < a["orig"] + a["size"]:
            for e in (a, b):
                if "overlap" not in e["issues"]:
                    e["issues"].append("overlap")
            issues["overlap"].append("%s @0x%x+%d overlaps %s @0x%x" % (a["name"], a["orig"], a["size"], b["name"], b["orig"]))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump(entries, f, indent=1)
    clean = sum(1 for e in entries if not e["issues"])
    print("annotated definitions: %d, mapped: %d, clean: %d -> %s" % (len(defs), len(entries), clean, OUT))
    for kind, lst in issues.items():
        print("%-8s %d" % (kind, len(lst)))
        for s in lst if verbose else lst[:5]:
            print("    " + s)


if __name__ == "__main__":
    main()
