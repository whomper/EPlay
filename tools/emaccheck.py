#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""emaccheck.py ELF: fails when a ColdFire build holds an EMAC instruction.

On a ColdFire, 0xa000 (Line-A on a 68000) is the EMAC's macw: it takes the
next instruction's first word and leaves a0 garbage, and FreeMiNT killed
EWEBFB.PRG at its first scroll on a tester's FireBee (the hub, AERA-Atari
13a4971, 19 Nov 2026). EPlay uses no EMAC, so any mac, msac or movclr, or
any move to or from an accumulator, in the FireBee's code is a Line-A call
left in: the build fails and names its function.
"""
import re
import subprocess
import sys

OBJDUMP = "m68k-linux-gnu-objdump"
EMAC = re.compile(r"^(mac|msac|movclr)|%acc|%macsr|%mask")


def main():
    elf = sys.argv[1]
    out = subprocess.run([OBJDUMP, "-d", "-m", "m68k:cfv4e", elf], capture_output=True, text=True,
                         check=True).stdout
    fn, bad = "?", []
    for line in out.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if m:
            fn = m.group(1)
            continue
        parts = line.split("\t")
        if len(parts) >= 3 and EMAC.search(parts[2].strip()):
            bad.append("%s: %s (%s)" % (fn, parts[2].strip(), parts[0].strip()))
    if bad:
        print("emaccheck: %s has EMAC instructions (Line-A left in for a ColdFire?):" % elf)
        for b in bad:
            print("  " + b)
        sys.exit(1)
    print("emaccheck: %s: no EMAC instruction" % elf)


if __name__ == "__main__":
    main()
