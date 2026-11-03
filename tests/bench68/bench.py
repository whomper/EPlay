#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""bench.c in Hatari: what a 68030 can do of MP3's synthesis window.

    make && python3 bench.py WORK [MACHINE...]    (Falcon-4, Falcon)
"""
import os, sys, shutil, subprocess, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "common", "tools"))
import machines
work = sys.argv[1]
for spec in sys.argv[2:] or ["Falcon-4", "Falcon"]:
    w = os.path.join(work, spec.replace(":", "-")); shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd"); os.makedirs(hd)
    shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "BENCH.PRG"), hd)
    m = machines.Machine(spec, hd)
    log = os.path.join(w, "log")
    p = subprocess.Popen(["hatari"] + m.args() + ["--fast-boot", "yes", "--natfeats", "yes", "--log-level", "warn",
                          "--statusbar", "no", "--sound", "off", "--auto", "C:\\BENCH.PRG"], env=m.env(),
                         stdout=open(log, "w"), stderr=subprocess.STDOUT)
    t = time.time()
    while time.time() - t < 120:
        if "BENCH" in open(log, errors="replace").read():
            break
        time.sleep(1)
    time.sleep(0.5); p.kill(); p.wait()
    print(spec, m.describe(), [l.strip() for l in open(log, errors="replace") if "BENCH" in l])
