#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""padcheck.py SHIPPED.map PROF.map: fails where a section both maps hold
sits at another place mod 256 (the 68030's caches) in PROF.map.

The speed rig times build/prof (-DAES_PROFILE), not the program that
ships. Its sections are padded (atari/padprof.S) to build/atari's places
mod 256, so that a pair of profiling builds compares two shipped layouts
(the hub, 17 Nov 2026: in 1.45 build/prof was off by 254 from
.rodata.m_desk on, the linker's merged .str1.1 sections coming out at
other sizes there). make prof runs this after its link."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkpad import load  # noqa: E402


def main():
    if len(sys.argv) != 3:
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
