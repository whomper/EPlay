#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""padcheck.py SHIPPED.map PROF.map [SHIPPED.elf PROF.elf [OBJ ...]]: fails
where a section both maps hold sits at another place mod 256 (the 68030's
caches) in PROF.map; with the ELFs, also where a symbol both hold does
(unique names, the pads' own labels and the strings' .L labels left out,
and the symbols of each OBJ: an object the two builds compile apart).

The speed rig times build/prof (-DAES_PROFILE), not the program that
ships. Its sections are padded (atari/padprof.S) to build/atari's places
mod 256, so that a pair of profiling builds compares two shipped layouts
(the hub, 17 Nov 2026: in 1.45 build/prof was off by 254 from
.rodata.m_desk on, the linker's merged .str1.1 sections coming out at
other sizes there). make prof runs this after its link."""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkpad import load  # noqa: E402


def nm(path):
    """name -> address, and the names defined twice"""
    out = subprocess.run(["m68k-linux-gnu-nm", "-n", "--defined-only", path], capture_output=True, text=True,
                         check=True).stdout
    d, twice = {}, set()
    for ln in out.splitlines():
        p = ln.split()
        if len(p) != 3 or p[1].lower() not in "trdb":
            continue
        if p[2] in d:
            twice.add(p[2])
        d[p[2]] = int(p[0], 16)
    return d, twice


def symbols(ship_elf, prof_elf, objs):
    """the symbols both ELFs hold at other places mod 256 (common's
    tools/profpad.py's rules, 17 Nov 2026: with -ffunction-sections and
    -fdata-sections each symbol is a section of its own here, so this
    checks what a section's pad can't, the merged strings aside)"""
    a, ta = nm(ship_elf)
    b, tb = nm(prof_elf)
    skip = ta | tb
    for o in objs:
        out = subprocess.run(["m68k-linux-gnu-nm", "--defined-only", o], capture_output=True, text=True).stdout
        skip |= {ln.split()[-1] for ln in out.splitlines() if ln.split()}
    both = [n for n in a if n in b and n not in skip and not n.startswith((".L", "ep_pad_"))]
    return len(both), [(n, a[n], b[n]) for n in both if (a[n] - b[n]) % 256]


def main():
    if len(sys.argv) not in (3,) and len(sys.argv) < 5:
        sys.exit(__doc__)
    a, b = load(sys.argv[1]), load(sys.argv[2])
    old = {(s, o): ad for s, ad, sz, o in a}
    off = [(s, o, (ad - old[(s, o)]) % 256) for s, ad, sz, o in b if (s, o) in old and (ad - old[(s, o)]) % 256]
    if off:
        print("padcheck: %d sections of %s not at %s's places mod 256 (remake atari/padprof.S: "
              "make padprof), the first:" % (len(off), sys.argv[2], sys.argv[1]))
        for s, o, d in off[:8]:
            print("  %s (%s) %+d" % (s, o, d))
        return 1
    print("padcheck: every section of %s at %s's place mod 256" % (sys.argv[2], sys.argv[1]))
    if len(sys.argv) >= 5:
        n, offs = symbols(sys.argv[3], sys.argv[4], sys.argv[5:])
        if offs:
            print("padcheck: %d of %d symbols of %s not at %s's places mod 256, the first:" %
                  (len(offs), n, sys.argv[4], sys.argv[3]))
            for s_, x, y in offs[:8]:
                print("  %s shipped at 0x%x, timed at 0x%x" % (s_, x, y))
            return 1
        print("padcheck: and every one of the %d symbols both hold" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
