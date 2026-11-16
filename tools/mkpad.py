#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""mkpad.py BASE.map NEW.map [BASE-NAME] > atari/pad.S

The 68030's build keeps each function at the place a release had it,
modulo the 68030's 256-byte instruction cache: a pad after each code
section whose size changed, so that every section both link maps hold sits
at BASE.map's address mod 256 in NEW.map once the pads are in.

Why (16 Nov 2026, EPlay 1.42): the XBIOS path's code, never run on a
Falcon, shrank by 700 bytes, every function sorting after it moved, and a
Falcon's start under NVDI came out 15 to 30 ms slower in every pair against
1.41 with no work added. The tick's own code is pinned (src/tickcode.h);
the pads hold the rest. The hub's rule (AERA-Atari 098183d): code-placement
rows get a pad round before they go to Erez.

Each pad is a section named after the one it follows with "!" added: the
link sorts the sections by name (--sort-section=name), and "!" sorts
straight after a name, before any longer one ("run_fn" would come before
"run~"). Two functions of one name in two files share a section name, and
no pad goes between them: name them apart.

Recheck at every change: link NEW.map without pads (make PADS=no), then
make this file again against the same BASE.map, and see each section at
its place (the map's shift, mod 256, is 0).
"""
import re
import sys


def load(path):
    """the map's code sections: (name, address, size, object)"""
    out, pend = [], None
    for line in open(path):
        m = re.match(r"^ (\.text\S*)\s*$", line)
        if m:
            pend = m.group(1)
            continue
        m = re.match(r"^ (\.text\S*)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", line)
        if m:
            out.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4).split("/")[-1]))
            pend = None
            continue
        m = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", line)
        if m and pend:
            out.append((pend, int(m.group(1), 16), int(m.group(2), 16), m.group(3).split("/")[-1]))
        pend = None
    return [o for o in out if o[2] > 0 and o[3] != "pad_s.o"]


def pads(base, new):
    """(the section a pad follows, its bytes), in the link's order"""
    old = {(s, o): ad for s, ad, sz, o in base}
    out, moved, prev = [], 0, None
    for s, ad, sz, o in new:
        if ad == 0:
            continue
        if (s, o) in old and prev:
            d = (ad + moved - old[(s, o)]) % 256
            if d:
                x = (-d) % 256
                out.append((prev, x))
                moved += x
        prev = s
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    base, new = load(sys.argv[1]), load(sys.argv[2])
    name = sys.argv[3] if len(sys.argv) > 3 else sys.argv[1]
    p = pads(base, new)
    print("| SPDX-License-Identifier: GPL-2.0-or-later")
    print("| Copyright (C) 2026 AERA | Erez Yaary")
    print("| pad.S - written by tools/mkpad.py against %s: do not edit." % name)
    print("| %d pads, %d bytes, each section at its place there, mod the 68030's" % (len(p), sum(x for _, x in p)))
    print("| 256-byte cache (the 68030's build alone; the Makefile keeps each one)")
    for i, (after, x) in enumerate(p):
        print('\t.section "%s!","ax"' % after)
        print("\t.globl ep_pad_%d" % i)
        print("ep_pad_%d:" % i)
        print("\t.space %d" % x)


if __name__ == "__main__":
    main()
