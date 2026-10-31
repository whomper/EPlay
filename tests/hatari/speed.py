#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The speed rig (the guidelines' "Speed: the stock machines set it"):
build/prof/EPLAY.PRG (make prof: -DAES_PROFILE=20) on a stock Falcon,
the longest stretch between event calls of each thing the user does,
from the AESGAP lines on the emulator's console (common/tests/speed).
Fails when one is over its time.

    make prof && make -C tests/hatari && speed.py WORK [MACHINE [mp3|wav]]
    (MACHINE: Falcon-4:16; both kinds by default)

EPlay starts with a file (C:\\LAUNCH.PRG gives it, as the desktop does):
an MP3 at 320 kbit/s, the heaviest there is (the DSP's program and
tables sent as it starts), or a CD-quality WAV; both at 49170 Hz, as
with no external clock. While it plays, the screen changes each second
(the time), so an action is taken as settled after a few seconds rather
than on a still screen.
"""
import importlib.util
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.normpath(os.path.join(HERE, "..", ".."))
# common's speed rig, under its own name (this file is speed.py too)
_spec = importlib.util.spec_from_file_location("aera_speed", os.path.join(TOP, "common", "tests", "speed", "speed.py"))
speed = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(speed)

# where things are on the 640x480 screen (EPlay's window where it opens)
DESK = (40, 9)
ABOUT = (60, 27)
OPEN, PLAY, STOP = (170, 258), (288, 258), (400, 258)
SECS = 40


class Rig(speed.Rig):
    def still(self, most=30):
        """while it plays the time changes each second: a few seconds are
        taken as settled (the stretches are on the console meanwhile)"""
        time.sleep(3)
        return True


def session(work, spec, kind):
    """EPlay started with a file of kind (mp3: 320 kbit/s at 44.1 kHz;
    wav: 16-bit stereo at 44.1 kHz, both played at 49170 Hz with no
    external clock: the most a stock Falcon is asked), then the user's
    actions while it plays"""
    r = Rig(os.path.join(work, kind), spec)
    try:
        r.put(os.path.join(TOP, "build", "prof", "EPLAY.PRG"))
        r.put(os.path.join(HERE, "build", "LAUNCH.PRG"))
        src = os.path.join(r.work, "src.wav")
        subprocess.run(["sox", "-n", "-r", "44100", "-c", "2", "-b", "16", src, "synth", str(SECS),
                        "sine", "1000", "sine", "1500", "gain", "-6"], check=True)
        if kind == "mp3":
            name, what = "LONG.MP3", "an MP3 (320 kbit/s)"
            subprocess.run(["lame", "-t", "--quiet", "--noreplaygain", "-b", "320", "-m", "s", src,
                            os.path.join(r.hd, name)], check=True)
        else:
            name, what = "LONG.WAV", "a WAV (CD quality)"
            subprocess.run(["sox", src, os.path.join(r.hd, name)], check=True)
        r.write("TAIL.TXT", "C:\\%s\n" % name)
        r.start("C:\\LAUNCH.PRG", ready="EPLAY playing")
        r.results[-1] = ("started, %s playing" % what, "AESSTART", speed.START)
        r.act("%s playing, 5 s" % what, speed.KEY, lambda: time.sleep(5))
        r.act("About box opened (playing)", speed.DIALOG, lambda: (r.click(*DESK), time.sleep(1),
                                                                   r.click(*ABOUT)))
        r.act("About box closed", speed.WINDOW, lambda: r.key("Return"))
        r.act("Stop", speed.KEY, lambda: r.click(*STOP))
        r.act("About box opened (stopped)", speed.DIALOG, lambda: (r.click(*DESK), time.sleep(1),
                                                                   r.click(*ABOUT)))
        r.act("About box closed", speed.WINDOW, lambda: r.key("Return"))
        r.act("Play: the file again", speed.KEY, lambda: r.click(*PLAY))
        r.act("%s playing, 5 s more" % what, speed.KEY, lambda: time.sleep(5))
        r.act("Stop again", speed.KEY, lambda: r.click(*STOP))
        rc = r.report()
        n = sum(1 for l in r.console().split("\n") if l.startswith("EPLAY playing"))
        if n != 2:
            print("FAIL  played %d times, not 2" % n)
            rc = 1
        return rc
    finally:
        r.stop()


def main():
    work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "/tmp/eplay-speed")
    spec = sys.argv[2] if len(sys.argv) > 2 else "Falcon-4:16"
    kinds = sys.argv[3:] or ["mp3", "wav"]
    return max(session(work, spec, k) for k in kinds)


if __name__ == "__main__":
    sys.exit(main())
