#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""play060.py WORK [CASE...]: play_test.py's cases on a Falcon with a 68060
(a CT60's), the DSP kept: Hatari's Falcon at CPU level 6, its FPU, 32 MHz,
14 MB ST RAM and 64 MB TT RAM, VGA, under EmuTOS (TOS 4.04 knows no 68060).
Erez, 12 Nov 2026: the VGM player on the 68060 too. The machine is added
here, at run time, as "Falcon-060" (common's machines.py has none yet: the
hub's)."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "common", "tools"))
sys.path.insert(0, HERE)
import machines  # noqa: E402

machines.MACHINES["Falcon-060"] = (
    "a Falcon with a 68060 at 32 MHz (Hatari's most; a CT60 runs 66 to 100), its FPU, 14 MB ST RAM, "
    "64 MB TT RAM, the DSP, VGA; EmuTOS",
    ["--machine", "falcon", "--cpulevel", "6", "--cpuclock", "32", "--fpu", "internal", "--memsize", "14",
     "--ttram", "64", "--addr24", "off", "--dsp", "emu", "--monitor", "vga"],
    ["256", "16", "65536"], "no-tos-for-a-68060.img")

# the same with no DSP (a Falcon whose DSP another program holds, or a 68060
# machine without one, with the DMA's sound): MP3 by MP3FPU.OVL on the CPU
machines.MACHINES["Falcon-060nodsp"] = (
    machines.MACHINES["Falcon-060"][0].replace("the DSP", "no DSP"),
    [a if a != "emu" else "none" for a in machines.MACHINES["Falcon-060"][1]],
    machines.MACHINES["Falcon-060"][2], machines.MACHINES["Falcon-060"][3])

# and with no FPU (a 68LC060, Erez 16 Nov 2026): MP3 by MP3INT.OVL
machines.MACHINES["Falcon-060lc"] = (
    machines.MACHINES["Falcon-060nodsp"][0].replace("its FPU", "no FPU (a 68LC060)"),
    [a if a != "internal" else "none" for a in machines.MACHINES["Falcon-060nodsp"][1]],
    machines.MACHINES["Falcon-060"][2], machines.MACHINES["Falcon-060"][3])

# a 68040 (an Afterburner040's), the DSP there: _CPU 40, so cmp3.c asks
# whether it is a 68080 (atari/cpu080.S), which a 68040 refuses; with
# MP3CPU=1 its MP3 stays on the DSP all the same
machines.MACHINES["Falcon-040"] = (
    machines.MACHINES["Falcon-060"][0].replace("68060", "68040"),
    [a if a != "6" else "4" for a in machines.MACHINES["Falcon-060"][1]],
    machines.MACHINES["Falcon-060"][2], machines.MACHINES["Falcon-060"][3])

import play_test  # noqa: E402

if __name__ == "__main__":
    work = sys.argv[1] if len(sys.argv) > 1 else "/tmp/eplay-060"
    # (PLAY060_MACHINE=Falcon-060nodsp: MP3 on the CPU; Falcon-060lc: no
    # FPU either, MP3INT.OVL)
    spec = os.environ.get("PLAY060_MACHINE", "Falcon-060") + ":256"
    sys.argv = [sys.argv[0], work] + (sys.argv[2:] or ["vgm", "vgz", "vgmsn", "vgmopn", "vgmmd", "vgmopl"]) + [spec]
    play_test.main()
