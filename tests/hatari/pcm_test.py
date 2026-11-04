#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""pcm_test.py WORK [MACHINE]: src/pcm.c's 68030 loops (the sound's tick:
16-bit WAV, 24-bit AIFF and WAV, stereo) against its C, bit for bit, on
Hatari's 68030 (build/PCMTEST.PRG, make). The play rig can't hear the
DMA's cases on Hatari 2.4 (its DAC on a silent track), so this is their
check. MACHINE: Falcon-4 by default."""
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
    work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "/tmp/eplay-pcm")
    spec = sys.argv[2] if len(sys.argv) > 2 else "Falcon-4"
    w = os.path.join(work, spec.replace(":", "-"))
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(HERE, "build", "PCMTEST.PRG"), hd)
    m = machines.Machine(spec, hd)
    log = os.path.join(w, "hatari.log")
    args = ["hatari"] + m.args() + ["--fast-boot", "yes", "--natfeats", "yes", "--log-level", "warn",
                                    "--statusbar", "no", "--sound", "off", "--auto", "C:\\PCMTEST.PRG"]
    p = subprocess.Popen(args, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    got = None
    try:
        end = time.time() + 120
        while time.time() < end and not got:
            for line in open(log, errors="replace"):
                if line.startswith("PCMTEST ok") or (line.startswith("PCMTEST FAIL") and " of " in line):
                    got = line.strip()
            time.sleep(0.5)
    finally:
        p.kill()
        p.wait()
    fails = [l.strip() for l in open(log, errors="replace") if l.startswith("PCMTEST FAIL") and " of " not in l]
    for f in fails:
        print(f)
    ok = bool(got) and got.startswith("PCMTEST ok")
    print("%s pcm on %s: %s" % ("ok  " if ok else "FAIL", m.describe(), got or "no answer"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
