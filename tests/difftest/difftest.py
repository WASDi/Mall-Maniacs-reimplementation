#!/usr/bin/env python3
"""Differential tests: original maniac.exe (emulated) vs the 32-bit rebuild.

    tests/difftest/build.sh && tests/difftest/.venv/bin/python tests/difftest/gen_globals.py
    tests/difftest/.venv/bin/python tests/difftest/difftest.py [-n N] [--seed S] [func ...]

See harness.py for how both sides are lined up, cases_*.py for the cases.
"""
import argparse
import random
import sys

import cases_math
import cases_world
from harness import EmuError, GlobalMap, Original, Rebuild, run_case

TESTS = {}
for mod in (cases_math, cases_world):
    TESTS.update(mod.TESTS)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("funcs", nargs="*", help="subset of: " + ", ".join(TESTS))
    ap.add_argument("-n", type=int, default=500, help="cases per function")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--ulps", type=int, default=4, help="float tolerance counted as noise")
    ap.add_argument("--show", type=int, default=3, help="examples printed per verdict")
    opt = ap.parse_args()

    orig = Original()
    gmap = GlobalMap(orig.read_image)
    reb = Rebuild()
    failed = False
    for name in opt.funcs or TESTS:
        addr, conv, sym, gen = TESTS[name]
        rng = random.Random("%s/%d" % (name, opt.seed))
        tally = {"exact": 0, "noise": 0, "MISMATCH": 0, "error": 0}
        shown = {k: 0 for k in tally}
        unsynced_r, unsynced_w = set(), set()
        for _ in range(opt.n):
            case = gen(rng)
            try:
                v, o = run_case(orig, reb, gmap, (addr, conv, sym), case, opt.ulps)
                verdict, notes = v.level, v.notes
                unsynced_r |= o["unsynced_r"]
                unsynced_w |= o["unsynced_w"]
            except EmuError as e:
                verdict, notes = "error", [str(e)]
                if "driver died" in str(e):
                    reb = Rebuild()
            tally[verdict] += 1
            if verdict != "exact" and shown[verdict] < opt.show:
                shown[verdict] += 1
                print("  [%s] %s: %s" % (verdict, name, case["inputs"]))
                for n in notes[:8]:
                    print("      " + n)
        failed |= tally["MISMATCH"] > 0 or tally["error"] > 0
        print("%-24s @0x%06x  %s" % (name, addr, "  ".join("%s=%d" % kv for kv in tally.items())))
        if unsynced_w:
            print("    original WROTE unsynced globals: " + ", ".join(sorted(unsynced_w)[:12]))
        if unsynced_r:
            print("    original read unsynced globals: " + ", ".join(sorted(unsynced_r)[:12]))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
