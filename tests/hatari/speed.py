#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The speed rig (the guidelines' "Speed: the stock machines set it"):
build/prof/eplay.prg (make prof: -DAES_PROFILE=20) on a stock Falcon,
the longest stretch between event calls of each thing the user does,
from the AESGAP lines on the emulator's console (common/tests/speed).
Fails when one is over its time. A button's press (drawn pressed) is
timed apart from its release (the action), as the guidelines' 5.8 asks.

    make prof && make -C tests/hatari && speed.py WORK [MACHINE [mp3|wav]]
    (MACHINE: Falcon-4:16; both kinds by default; AERA_WITH_NVDI=1: under NVDI)

EPlay starts with a file (C:\\LAUNCH.PRG gives it, as the desktop does):
an MP3 at 320 kbit/s, the heaviest there is (the DSP's program and
tables sent as it starts), or a CD-quality WAV; both at 49170 Hz, as
with no external clock. While it plays, the screen changes each second
(the time), so an action is taken as settled after a few seconds rather
than on a still screen.
"""
import importlib.util
import os
import re
import shutil
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
# the bar's round Play/Pause button (design D)
PLAY = PAUSE = (365, 150)
POS_MID = (364, 175)		# the position bar's middle
SECS = 120	# (long enough not to end before the last action, when the emulator runs fast)


class Rig(speed.Rig):
    quick = False

    def still(self, most=30):
        """while it plays the time changes each second: a few seconds are
        taken as settled (the stretches are on the console meanwhile); at
        once for the start of an MP3 whose cover is unpacked first (its wait
        is a row of its own)"""
        time.sleep(0.3 if self.quick else 3)
        return True

    def wait_line(self, text, secs=30):
        end = time.time() + secs
        while time.time() < end and text not in self.console():
            time.sleep(0.5)

    def press(self, x, y):
        """the button down over x, y (held: the guidelines' 5.8 times the
        press, the button drawn pressed, apart from the release's action)"""
        g = dict(l.split("=") for l in self.xdo("getwindowgeometry", "--shell", self.win()).split())
        self.xdo("mousemove", str(int(g["X"]) + x), str(int(g["Y"]) + y))
        time.sleep(0.4)
        self.xdo("mousedown", "1")

    def let_go(self):
        self.xdo("mouseup", "1")

    def held_shown(self, x, y, what):
        """a press held still a second, its button seen pressed on the
        screen before the release (guidelines 5.8, AERA-Atari 60d1ea5: TOS's
        AES holds BEG_UPDATE back while the button is held, unseen by
        AESGAP): the round button's disc no longer white (the accent), on a
        ring between its symbol and its edge"""
        from PIL import Image
        time.sleep(1)
        img = Image.open(self.shot("held-" + what)).convert("L")
        ring = [(9, 0), (-9, 0), (0, 9), (0, -9), (7, 7), (-7, 7), (7, -7), (-7, -7)]
        box = [img.getpixel((x + dx, y + dy)) for dx, dy in ring]
        pressed = sum(1 for v in box if v < 200) / len(box)
        ok = pressed > 0.6
        print("%s  %s held 1 s: %d %% of the button in its pressed colour (drawn pressed before the release)"
              % ("ok  " if ok else "FAIL", what, pressed * 100))
        self.held_ok = getattr(self, "held_ok", True) and ok


def session(work, spec, kind):
    """EPlay started with a file of kind (mp3: 320 kbit/s at 44.1 kHz;
    wav: 16-bit stereo at 44.1 kHz, both played at 49170 Hz with no
    external clock: the most a stock Falcon is asked), then the user's
    actions while it plays"""
    # (AERA_WITH_NVDI=1: under Erez's NVDI, as the hub runs it)
    r = Rig(os.path.join(work, kind), spec, nvdi=os.environ.get("AERA_WITH_NVDI") == "1")
    try:
        r.put(os.path.join(TOP, "build", "prof", "eplay.prg"))
        r.put(os.path.join(HERE, "build", "LAUNCH.PRG"))
        src = os.path.join(r.work, "src.wav")
        subprocess.run(["sox", "-n", "-r", "44100", "-c", "2", "-b", "16", src, "synth", str(SECS),
                        "sine", "1000", "sine", "1500", "gain", "-6"], check=True)
        if kind == "mp3":
            # with album art as a shop's is: a 500 x 500 JPEG of a photo's
            # detail (some 100 KB), its steps timed with the rest
            from PIL import Image, ImageFilter
            import random
            random.seed(1)
            img = Image.new("RGB", (500, 500))
            img.putdata([(x * 255 // 499, y * 255 // 499, random.randrange(256))
                         for y in range(500) for x in range(500)])
            cover = os.path.join(r.work, "cover.jpg")
            img.filter(ImageFilter.GaussianBlur(1)).save(cover, quality=90)
            name, what = "LONG.MP3", "an MP3 (320 kbit/s)"
            subprocess.run(["lame", "-t", "--quiet", "--noreplaygain", "-b", "320", "-m", "s",
                            "--tt", "Long", "--ta", "The rig", "--ti", cover, src,
                            os.path.join(r.hd, name)], check=True)
        else:
            name, what = "LONG.WAV", "a WAV (CD quality)"
            subprocess.run(["sox", src, os.path.join(r.hd, name)], check=True)
        r.write("TAIL.TXT", "C:\\%s\n" % name)
        shutil.rmtree(os.path.join(r.hd, "cache"), ignore_errors=True)
        if kind == "mp3":
            # the start ends when the window is up; an MP3 with a cover not
            # in the cache waits for the DSP to unpack it, a row of its own
            # (the hub, 3 Nov 2026: 3000 ms, and its turns a job's)
            r.quick = True
            r.start("C:\\LAUNCH.PRG", ready="EPLAY status: Getting the album art")
            r.quick = False
            # (the start: AESSTART and the stretches to the window's first
            # draw; the rig's own wait after it would add the cover's
            # unpacking turns, which are the next row's)
            con = r.console()
            at = con.find("EPLAY status: Getting the album art")
            drawn = con.find("WM_REDRAW", at)
            if at >= 0 and drawn >= 0:
                r.start_end = con.find("\n", drawn) + 1
            r.results[-1] = ("started, the window drawn", "AESSTART", speed.START)
            r.act("an MP3 with an uncached cover: playing starts", 3000,
                  lambda: r.wait_line("EPLAY playing"), job=True)
        else:
            r.start("C:\\LAUNCH.PRG", ready="EPLAY playing")
            r.results[-1] = ("started, %s playing" % what, "AESSTART", speed.START)
        r.act("%s playing, 5 s" % what, speed.KEY, lambda: time.sleep(5))
        r.act("About box opened (playing)", speed.DIALOG, lambda: (r.click(*DESK), time.sleep(1),
                                                                   r.click(*ABOUT)))
        r.act("About box closed", speed.WINDOW, lambda: r.key("Return"))
        # Settings (^,), as the guidelines' rig opens it, and Esc
        r.act("Settings opened (playing)", speed.DIALOG, lambda: r.key("ctrl+comma"))
        r.shot("settings")
        r.act("Settings closed (Esc)", speed.WINDOW, lambda: r.key("Escape"))
        r.act("Pause: pressed (drawn pressed)", speed.KEY, lambda: r.press(*PAUSE))
        r.held_shown(PAUSE[0], PAUSE[1], "Pause")
        r.act("Pause: let go (pauses)", speed.KEY, r.let_go)
        r.act("About box opened (paused)", speed.DIALOG, lambda: (r.click(*DESK), time.sleep(1),
                                                                   r.click(*ABOUT)))
        r.act("About box closed", speed.WINDOW, lambda: r.key("Return"))
        r.act("Play: pressed (drawn pressed)", speed.KEY, lambda: r.press(*PLAY))
        r.held_shown(PLAY[0], PLAY[1], "Play")
        r.act("Play: let go (goes on)", speed.KEY, r.let_go)
        r.act("%s playing, 5 s more" % what, speed.KEY, lambda: time.sleep(5))
        # the position bar: the time follows the press, the music goes there
        # on the release (play_seek: the file opened again at its place)
        r.act("Position bar: pressed at its middle", speed.KEY, lambda: r.press(*POS_MID))
        r.act("Position bar: let go (goes there)", speed.KEY, r.let_go)
        r.act("%s playing from there, 3 s" % what, speed.KEY, lambda: time.sleep(3))
        r.act("Pause again", speed.KEY, lambda: r.click(*PAUSE))
        rc = r.report()
        if not getattr(r, "held_ok", True):
            rc = 1
        went = [l for l in r.console().split("\n") if l.startswith("EPLAY went to ")]
        ms = int(went[-1].split()[3]) if went else -1
        if abs(ms - SECS * 500) > SECS * 50:
            print("FAIL  the position bar's middle went to %d ms, not about %d" % (ms, SECS * 500))
            rc = 1
        else:
            print("ok    the position bar's middle went to %d ms (of %d)" % (ms, SECS * 1000))
        lines = r.console().split("\n")
        if kind == "mp3":
            log = ""
            for n in ("eplay.log", "EPLAY.LOG"):
                if os.path.exists(os.path.join(r.hd, n)):
                    log = open(os.path.join(r.hd, n), errors="replace").read()
            art = [l for l in log.split("\n") if l.startswith("album art")]
            # the job rule while a cover is unpacked (the hub, 3 Nov 2026):
            # no turn over 50 ms; EPlay logs its longest between the
            # decoder's polls (the rig's start row can't see them all)
            gap = re.search(r"the longest between polls (\d+)", log)
            if gap and int(gap.group(1)) > speed.JOB_030:
                print("FAIL  the cover's unpacking: %s ms between turns (a job's are %d at most)"
                      % (gap.group(1), speed.JOB_030))
                rc = 1
            # (made where the 68030 is fast enough, else left: art.c)
            if not art or not (art[0].startswith("album art: JPEG") or "left for a faster 68030" in art[0]):
                print("FAIL  the album art: %s" % (art or "not made"))
                rc = 1
            else:
                print("ok    %s" % art[0])
        paused = [int(l.split()[3]) for l in lines if l.startswith("EPLAY paused at ")]
        resumed = [int(l.split()[3]) for l in lines if l.startswith("EPLAY resumed at ")]
        if not paused or not resumed or abs(resumed[0] - paused[0]) > 100:
            print("FAIL  paused at %s, went on at %s (Play takes it up where Pause was)" % (paused, resumed))
            rc = 1
        else:
            print("ok    paused at %d ms, went on at %d ms" % (paused[0], resumed[0]))
        n = sum(1 for l in lines if l.startswith("EPLAY playing"))
        if n != 1:
            print("FAIL  the file opened %d times, not once (Pause and Play go on in it)" % n)
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
