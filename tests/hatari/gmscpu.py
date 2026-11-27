#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""gmscpu.py WORK [CPULEVEL [MHZ [TTRAM_MB [PRG]]]]: GMSCPU.PRG (make gmscpu) on
Hatari's Falcon, under EmuTOS, no DSP: the internal synth's engine timed
with 1 to 16 voices (DESIGN.md, "The synth"). CPULEVEL 3 at 16 MHz with no
TT RAM is a stock Falcon; 3 at 50 with TT RAM a DFB1's; 6 a 68060 (not
cycle-exact in Hatari: a guide). A measurement, not a test."""
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(TOP, "common", "tools"))
import machines  # noqa: E402


def main():
    work = os.path.abspath(sys.argv[1])
    level = sys.argv[2] if len(sys.argv) > 2 else "3"
    mhz = sys.argv[3] if len(sys.argv) > 3 else "16"
    tt = sys.argv[4] if len(sys.argv) > 4 else "0"
    prg = sys.argv[5] if len(sys.argv) > 5 else "GMSCPU.PRG"	# (or RINGCPY.PRG: the DSP synth's copying)
    w = os.path.join(work, "gmscpu-%s-%s-%s" % (level, mhz, tt))
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(HERE, "build", prg), os.path.join(hd, "GMSCPU.PRG"))
    shutil.copy(os.path.join(TOP, "binary", "synth.dat"), os.path.join(hd, "SYNTH.DAT"))
    log = os.path.join(w, "hatari.log")
    args = ["hatari", "--tos", machines.emutos(), "--machine", "falcon", "--cpulevel", level,
            "--cpuclock", mhz, "--fpu", "internal" if level == "6" else "68882",
            "--memsize", "14", "--ttram", tt, "--addr24", "off" if tt != "0" or level == "6" else "on",
            "--dsp", "none", "--monitor", "vga", "--harddrive", hd, "--gemdos-drive", "C", "--fast-boot", "yes",
            "--natfeats", "yes", "--log-level", "warn", "--statusbar", "no", "--sound", "off",
            "--fast-forward", "no", "--auto", "C:\\GMSCPU.PRG"]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    p = subprocess.Popen(args, env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)
    got = None
    try:
        end = time.time() + 900
        while time.time() < end and not got and p.poll() is None:
            time.sleep(2)
            for line in open(log, errors="replace"):
                if line.startswith(("GMSCPU", "RINGCPY")):
                    got = line.strip()
    finally:
        p.kill()
    print(got or "GMSCPU: no answer (%s)" % log)
    return 0 if got else 1


if __name__ == "__main__":
    sys.exit(main())
