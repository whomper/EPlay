#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""save_test.py WORK MACHINE [CASE...]: File > Save playlist... through the
family's file dialog in its save mode (common's filepick_save, guidelines
5.12, decided 25 Nov 2026), with the music playing. The cases:

  playing  ^S, Return: PLAYLIST.M3U saved with the list, the music never
           paused and no underrun while the dialog was open
  replace  PLAYLIST.M3U there already: ^S, Return asks "Replace it?",
           Return there is Cancel (the file as it was, the dialog still
           open), Return again and Replace clicked saves over it (the
           family's alert: Esc and Undo are Cancel too)
  system   SYSFSEL=1 (Settings' "Use the system file selector"): the AES's
           selector, the music paused while it is open and on from where it
           was after, the file saved

Screenshots of each step in WORK/CASE/shots. Exits 1 when a case failed."""
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, "..", "..")
sys.path.insert(0, HERE)
import speed  # noqa: E402  (EPlay's Rig: its window's places, its INF)

CASES = ("playing", "replace", "system")
OLD = "#EXTM3U\r\nOLD.WAV\r\n"


def wait(r, text, secs):
    t_end = time.time() + secs
    while time.time() < t_end:
        if text in r.console():
            return True
        time.sleep(0.5)
    return False


def lines(r, start):
    return [l for l in r.console().splitlines() if l.startswith(start)]


def playlist(r):
    p = os.path.join(r.hd, "PLAYLIST.M3U")
    return open(p, newline="").read() if os.path.exists(p) else None


def saved_list(text):
    return [l for l in (text or "").split("\r\n") if l and not l.startswith("#")]


def session(work, spec, case):
    r = speed.Rig(os.path.join(work, case), spec, nvdi=os.environ.get("AERA_WITH_NVDI") == "1",
                  display=os.environ.get("EPLAY_SPEED_DISPLAY", ":91"))
    fails = []
    try:
        # (an STE: the 68000's edition, under the name LAUNCH.PRG starts)
        prg = os.path.join(r.work, "eplay.prg")
        shutil.copy(os.path.join(TOP, "binary", "eplayst.prg" if r.m.name.startswith("STE") else "eplay.prg"), prg)
        r.put(prg)
        r.put(os.path.join(HERE, "build", "LAUNCH.PRG"))
        r.write("eplay.inf", "SYSFSEL=1\r\n" if case == "system" else "")
        for n, f in (("A.WAV", 1000), ("B.WAV", 1500)):
            subprocess.run(["sox", "-n", "-r", "44100", "-c", "2", "-b", "16", os.path.join(r.hd, n), "synth",
                            "8", "sine", str(f), "gain", "-6"], check=True)
        if case == "replace":
            with open(os.path.join(r.hd, "PLAYLIST.M3U"), "w", newline="") as fh:
                fh.write(OLD)
        r.write("TAIL.TXT", "C:\\A.WAV C:\\B.WAV\n")
        r.start("C:\\LAUNCH.PRG", ready="EPLAY playing")
        time.sleep(3)
        r.key("ctrl+s")
        time.sleep(3)
        r.shot("1-open")
        if case == "replace":
            save(r, spec)			# Save: "PLAYLIST.M3U is there already. Replace it?"
            time.sleep(2)
            r.shot("2-question")
            r.key("Return")			# Cancel (the default): the dialog stays
            time.sleep(2)
            r.shot("3-cancelled")
            if playlist(r) != OLD:
                fails.append("replace: Cancel changed PLAYLIST.M3U")
            if lines(r, "EPLAY saved "):
                fails.append("replace: saved on Cancel")
            save(r, spec)			# Save again: the question again
            time.sleep(2)
            r.shot("4-question")
            x, y = REPLACE_AT.get(spec, REPLACE_AT["*"])
            speed.speed.Rig.click(r, x, y)	# Replace (the screen's place, not the window's)
        elif case == "system":
            r.key("Return")			# the AES's selector: OK, under the name proposed
        else:
            save(r, spec)			# Save, under the name proposed
        if not wait(r, "EPLAY saved ", 30):
            fails.append("%s: never saved" % case)
        time.sleep(2)
        r.shot("5-saved")
        got = saved_list(playlist(r))
        if got != ["A.WAV", "B.WAV"]:
            fails.append("%s: PLAYLIST.M3U holds %s" % (case, got))
        paused = lines(r, "EPLAY paused at ")
        resumed = lines(r, "EPLAY resumed at ")
        if case == "system":
            at = [int(l.split()[3]) for l in paused[:1] + resumed[:1]]
            if len(at) != 2 or abs(at[1] - at[0]) > 300:
                fails.append("system: paused %s, resumed %s" % (paused, resumed))
        elif paused:
            fails.append("%s: the music paused while EPlay's dialog was open: %s" % (case, paused))
        wait(r, "EPLAY ended", 60)		# (the list played to its end: the underruns counted)
        end = lines(r, "EPLAY ended")
        if not end or " 0 underruns" not in end[-1]:
            fails.append("%s: %s" % (case, end[-1] if end else "never ended"))
        for d, _, fs in os.walk(r.hd):
            if any(f.upper() == "CRASH.LOG" for f in fs):
                fails.append("%s: CRASH.LOG in %s" % (case, d))
        print("%-8s on %s: %s" % (case, r.describe(), "ok" if not fails else "; ".join(fails)))
    finally:
        r.stop()
    return fails


def save(r, spec):
    """Return: the dialog saves under the name in its field (the keyboard
    still on the field, not the list: common 4d083dc)"""
    r.key("Return")


# the "Replace" button of the dialog's question, on the screen (TOS's
# alert centres it; found from 4-question's screenshot)
REPLACE_AT = {"*": (278, 272)}


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        print(__doc__)
        return 2
    work, spec = os.path.abspath(a[0]), a[1]
    cases = a[2:] or list(CASES)
    fails = []
    for c in cases:
        fails += session(work, spec, c)
    for f in fails:
        print("FAIL  " + f)
    print("all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
