#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""emaccheck.py ELF: fails when a ColdFire build holds an EMAC instruction.

On a ColdFire, 0xa000 (Line-A on a 68000) is the EMAC's macw: it takes the
next instruction's first word and leaves a0 garbage, and FreeMiNT killed
EWEBFB.PRG at its first scroll on a tester's FireBee (the hub, AERA-Atari
13a4971, 19 Nov 2026). EPlay uses no EMAC, so any mac, msac or movclr, or
any move to or from an accumulator, in the FireBee's code is a Line-A call
left in: the build fails and names its function. The one exception is
EPlay's own MP3 decoder, whose synthesis filter (and from 1.53 its IMDCT's sums and
alias butterflies) is on the EMAC on purpose
(src/mp3/mp3dsp.c, dspmath.h's em_*: 21 Nov 2026): its functions alone
(ALLOWED) may hold them.
"""
import re
import subprocess
import sys

OBJDUMP = "m68k-linux-gnu-objdump"
EMAC = re.compile(r"^(mac|msac|movclr)|%acc|%macsr|%mask")
# (src/mp3/mp3dsp.c's, GCC's copies too: synth.constprop.0; where they are inlined, frame_:
# the synthesis filter, from 1.53 the IMDCT's sums and the alias butterflies, from 1.54 the
# resampler a sample at a time)
ALLOWED = {"synth", "frame_", "em_probe", "mp3dsp_frame", "alias", "dct9", "rows36", "imdct",
           "mp3rss_run"}


# jsr, jmp, pea and lea (any register) to an absolute long address: their
# address's low word may read as an EMAC instruction where objdump has lost
# step after a jump table's words (.short) in a function (ui/main.c's can(),
# 1.54: jsr $0004ae02, its "ae02" taken for mac.l)
ABS_L = {0x4eb9, 0x4ef9, 0x4879} | {0x41f9 | (r << 9) for r in range(8)}


def main():
    elf = sys.argv[1]
    out = subprocess.run([OBJDUMP, "-d", "-m", "m68k:cfv4e", elf], capture_output=True, text=True,
                         check=True).stdout
    words = {}
    for line in out.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\t((?:[0-9a-f]{4} ?)+)", line)
        if m:
            a = int(m.group(1), 16)
            for i, w in enumerate(m.group(2).split()):
                words[a + 2 * i] = int(w, 16)
    fn, bad = "?", []
    for line in out.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if m:
            fn = m.group(1)
            continue
        parts = line.split("\t")
        if len(parts) >= 3 and EMAC.search(parts[2].strip()) and fn.split(".")[0] not in ALLOWED:
            a = int(parts[0].strip().rstrip(":"), 16)
            if words.get(a - 4) in ABS_L:
                continue	# (an address's low word, read out of step: not an instruction)
            bad.append("%s: %s (%s)" % (fn, parts[2].strip(), parts[0].strip()))
    if bad:
        print("emaccheck: %s has EMAC instructions (Line-A left in for a ColdFire?):" % elf)
        for b in bad:
            print("  " + b)
        sys.exit(1)
    print("emaccheck: %s: no EMAC instruction outside EPlay's MP3 decoder" % elf)


if __name__ == "__main__":
    main()
