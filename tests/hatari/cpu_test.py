#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""cpu_test.py WORK [CASE ...]: each of EPlay's programs on a machine it isn't
for says so, in its own words, and goes (the guidelines 7.4, Erez 22 Nov
2026: two packages, the same names in both; each build names the other
package on the wrong machine). The programs start as a desktop starts them
(LAUNCH.PRG, shel_write); Hatari's AES trace gives the alert's text, word
for word; Return chooses Quit, and the program's appl_exit must follow.

  fbstd   the standard package's EPLAY.PRG (the 68030's) on the FireBee
          stand-in (a 68060 Falcon with FireTOS's cookies, _CF_ among them)
  fbst    the standard package's EPLAYST.PRG (the 68000's) there
  falfb   the FireBee package's EPLAY.PRG (ColdFire code, binary/eplayfb.prg)
          on a Falcon: its first instructions (common's cpucheck_cf.S) are
          the 68000's too, and find no _CF_
  stefb   the same on an STE (TOS 1.62: no _CF_ in its jar)
  stestd  the standard package's EPLAY.PRG on an STE: its 68000 check
          (common's cpucheck.c), as before 7.4

The FireBee's own EPLAY.PRG on a FireBee isn't here: no emulator runs a
ColdFire's GEM (tests/fb runs its code under qemu-m68k -cpu cfv4e)."""
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(TOP, "common", "tools"))
sys.path.insert(0, HERE)
import machines  # noqa: E402
import play_test  # noqa: E402


def words(name):
    """an alert's text as ui/main.c has it (the source, so a change there
    is a change here)"""
    src = open(os.path.join(TOP, "ui", "main.c")).read()
    part = {"fb": src.split("#if defined(EPLAY_FB)")[1].split("#elif")[0],
            "st": src.split("#elif defined(EPLAY_ST)")[1].split("#else")[0],
            "std": src.split("#elif defined(EPLAY_ST)")[1].split("#else")[1].split("#endif")[0]}
    kind, var = name.split(":")
    return re.search(r'\b%s\[\] = "([^"]*)"' % var, part[kind]).group(1)


# case: (the program, its name on the disk, the machine, the alert's words)
CASES = {
    "fbstd": ("eplay.prg", "eplay.prg", "FireBee:16", "std:cpu_cf_alert"),
    "fbst": ("eplayst.prg", "eplayst.prg", "FireBee:16", "st:cpu_cf_alert"),
    "falfb": ("eplayfb.prg", "eplay.prg", "Falcon-4:16", "fb:cpu_alert"),
    "stefb": ("eplayfb.prg", "eplay.prg", "STE", "fb:cpu_alert"),
    "stestd": ("eplay.prg", "eplay.prg", "STE", "std:cpu_alert"),
}


def run(work, case):
    prg, name, spec, which = CASES[case]
    want = words(which)
    w = os.path.join(work, case)
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(TOP, "binary", prg), os.path.join(hd, name))
    shutil.copy(os.path.join(HERE, "build", "LAUNCH.PRG"), hd)
    with open(os.path.join(hd, "TAIL.TXT"), "w") as fh:
        fh.write("\r\n")
    m = machines.Machine(spec, hd)
    fifo, shots = os.path.join(w, "cmd.fifo"), os.path.join(w, "shots")
    os.makedirs(shots)
    log = os.path.join(w, "hatari.log")
    args = ["hatari"] + m.args() + ["--fast-boot", "yes", "--natfeats", "yes", "--cmd-fifo", fifo,
                                    "--screenshot-dir", shots, "--log-level", "warn", "--statusbar", "no",
                                    "--trace", "aes", "--auto", "C:\\LAUNCH.PRG"]
    p = subprocess.Popen(args, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    try:
        if not play_test.wait_line(log, lambda l: l.startswith("LAUNCH shel_write ok"), 120):
            return False, m.describe(), "the launcher never ran"
        said = play_test.wait_line(log, lambda l: "form_alert(" in l and "LAUNCH" not in l, 60)
        if not said:
            return False, m.describe(), "no alert (the program ran on, or nothing came)"
        got = re.search(r'addrin: "([^"]*)"', said)
        got = got.group(1) if got else said
        if got != want:
            return False, m.describe(), "the alert: %r, wanted %r" % (got, want)
        time.sleep(1)
        play_test.cmd(fifo, "hatari-shortcut", "screenshot")
        time.sleep(0.5)
        play_test.cmd(fifo, "hatari-event", "keypress", "0x1c")	# Return: Quit
        text = ""
        end = time.time() + 20
        while time.time() < end:
            text = open(log, errors="replace").read()
            if "appl_exit" in text.split(said)[-1]:
                break
            time.sleep(0.3)
        if "appl_exit" not in text.split(said)[-1]:
            return False, m.describe(), "the alert, but no appl_exit after Quit"
        return True, m.describe(), "%s as %s: %s; Quit, and it went" % (prg, name.upper(), got)
    finally:
        p.kill()
        p.wait()


def main():
    work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "/tmp/eplay-cpu")
    cases = [a for a in sys.argv[2:] if a in CASES] or list(CASES)
    fails = 0
    for case in cases:
        ok, where, what = run(work, case)
        print("%s %s on %s: %s" % ("ok  " if ok else "FAIL", case, where, what), flush=True)
        fails += not ok
    print("all passed" if not fails else "%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
