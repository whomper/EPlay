#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""mp3cpu.py WORK FILE.MP3 [CPULEVEL [PRG]]: MP3CPU.PRG (make mp3cpu) on Hatari's
Falcon with a 68060 (CPULEVEL 6, or 3 for a 68030 to compare) at 32 MHz,
under EmuTOS (TOS 4.04 knows no 68060), no DSP: the C model of EPlay's
DSP decoder timed on the CPU. A measurement (DESIGN.md, "MP3 on the CPU"):
Hatari's 68060 isn't cycle-exact, so the figure is a guide, scaled to a
real 68060's clock by its 32 MHz."""
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
    mp3 = sys.argv[2]
    level = sys.argv[3] if len(sys.argv) > 3 else "6"
    prg = sys.argv[4] if len(sys.argv) > 4 else "MP3CPU.PRG"	# (or MP3MINI.PRG: minimp3's)
    w = os.path.join(work, "%s-%s" % (prg.split(".")[0].lower(), level))
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(HERE, "build", prg), os.path.join(hd, "MP3CPU.PRG"))
    shutil.copy(mp3, os.path.join(hd, "TEST.MP3"))
    log = os.path.join(w, "hatari.log")
    args = ["hatari", "--tos", machines.emutos(), "--machine", "falcon", "--cpulevel", level,
            "--cpuclock", "32", "--fpu", "internal" if level == "6" else "68882",
            "--memsize", "14", "--ttram", "64", "--addr24", "off", "--dsp", "none",
            "--monitor", "vga", "--harddrive", hd, "--gemdos-drive", "C", "--fast-boot", "yes",
            "--natfeats", "yes", "--log-level", "warn", "--statusbar", "no", "--sound", "off",
            "--fast-forward", "no", "--auto", "C:\\MP3CPU.PRG"]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    p = subprocess.Popen(args, env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)
    got = None
    try:
        end = time.time() + 900
        while time.time() < end and not got and p.poll() is None:
            time.sleep(2)
            for line in open(log, errors="replace"):
                if line.startswith("MP3CPU"):
                    got = line.strip()
    finally:
        p.kill()
    print(got or "MP3CPU: no answer (see %s)" % log)
    return 0 if got and "FAIL" not in got else 1


if __name__ == "__main__":
    sys.exit(main())
