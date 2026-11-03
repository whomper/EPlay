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
# the bar's round Play/Pause button (design D at 56 columns: Erez, 3 Nov 2026)
# (the window 11 pixels to the left of 0.20's: the list's slider at its right)
PLAY = PAUSE = (389, 150)
POS_MID = (389, 185)		# the position bar's middle
VOL_Q = (498, 185)		# a quarter of the volume bar: 4 of 16
NEXT = (421, 150)
ROW = [(300, 229 + 20 * i) for i in range(6)]	# the list's rows
SECS = 120	# (long enough not to end before the last action, when the emulator runs fast)


class Rig(speed.Rig):
    def still(self, most=30):
        """while it plays the time changes each second: a few seconds are
        taken as settled (the stretches are on the console meanwhile)"""
        time.sleep(3)
        return True

    def press(self, x, y):
        """the button down over x, y (held: the guidelines' 5.8 times the
        press, the button drawn pressed, apart from the release's action)"""
        g = dict(l.split("=") for l in self.xdo("getwindowgeometry", "--shell", self.win()).split())
        self.xdo("mousemove", str(int(g["X"]) + x), str(int(g["Y"]) + y))
        time.sleep(0.4)
        self.xdo("mousedown", "1")

    def dclick(self, x, y):
        g = dict(l.split("=") for l in self.xdo("getwindowgeometry", "--shell", self.win()).split())
        self.xdo("mousemove", str(int(g["X"]) + x), str(int(g["Y"]) + y))
        time.sleep(0.4)
        self.xdo("click", "--repeat", "2", "--delay", "120", "1")

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
        elif kind == "list":
            return list_session(r, src)
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
        # the volume: pressed at a quarter, then + (two steps up)
        r.act("Volume: pressed at a quarter", speed.KEY, lambda: r.press(*VOL_Q))
        r.act("Volume: let go", speed.KEY, r.let_go)
        r.act("Volume: + (3 dB up)", speed.KEY, lambda: r.key("plus"))
        r.act("Pause again", speed.KEY, lambda: r.click(*PAUSE))
        rc = r.report()
        vols = [int(l.split()[2]) for l in r.console().split("\n") if l.startswith("EPLAY volume ")]
        # (a quarter is 4 of 16; the pointer lands a pixel or two from where
        # it is sent, and the bar is 38 pixels: 3 to 5)
        if len(vols) < 2 or not 3 <= vols[0] <= 5 or vols[1] != vols[0] + 2:
            print("FAIL  the volume: %s, not about 4 (a quarter of the bar) then two more (+)" % vols)
            rc = 1
        else:
            print("ok    the volume: %d at a quarter of the bar, %d after +" % (vols[0], vols[1]))
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


def list_session(r, src):
    """the list (Erez, 3 Nov 2026: files added in bulk): eight files given
    at the start (as dropped on the program), their tags and lengths read
    in the background a file a turn, then a row selected, one played by a
    double click, Next, the keys"""
    names = ["T%d.%s" % (i + 1, "MP3" if i % 2 else "WAV") for i in range(8)]
    short = os.path.join(r.work, "short.wav")      # (30 s each: the rig's disk)
    subprocess.run(["sox", src, short, "trim", "0", "30"], check=True)
    src = short
    for i, n in enumerate(names):
        if n.endswith("MP3"):
            subprocess.run(["lame", "--quiet", "--noreplaygain", "-b", "192", "--tt", "Track %d" % (i + 1),
                            "--ta", "The rig", "--tl", "The list", src, os.path.join(r.hd, n)], check=True)
        else:
            subprocess.run(["sox", src, os.path.join(r.hd, n)], check=True)
    r.write("TAIL.TXT", " ".join("C:\\" + n for n in names) + "\n")
    r.start("C:\\LAUNCH.PRG", ready="EPLAY playing")
    r.results[-1] = ("started, 8 files given, the first playing", "AESSTART", speed.START)
    r.act("the list's files read (tags, lengths)", speed.KEY, lambda: time.sleep(6), job=True)
    r.act("a row selected (clicked)", speed.KEY, lambda: r.click(*ROW[2]))
    r.act("a row played (double click)", speed.KEY, lambda: r.dclick(*ROW[3]))
    r.act("Next", speed.KEY, lambda: r.click(*NEXT))
    r.act("Down", speed.KEY, lambda: r.key("Down"))
    r.act("Return (plays the selected)", speed.KEY, lambda: r.key("Return"))
    r.act("Delete (one out of the list)", speed.KEY, lambda: (r.key("Up"), r.key("Delete")))
    rc = r.report()
    lines = r.console().split("\n")
    played = [l.split()[2] for l in lines if l.startswith("EPLAY playing: ")]
    want = ["T1.WAV", "T4.MP3", "T5.WAV", "T6.MP3"]
    if played[:4] != want:
        print("FAIL  played %s, not %s (the first, a double click on row 4, Next, Return on row 6)" %
              (played, want))
        rc = 1
    else:
        print("ok    played in turn: %s" % " ".join(played[:4]))
    return rc


def main():
    work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "/tmp/eplay-speed")
    spec = sys.argv[2] if len(sys.argv) > 2 else "Falcon-4:16"
    kinds = sys.argv[3:] or ["mp3", "wav", "list"]
    return max(session(work, spec, k) for k in kinds)


if __name__ == "__main__":
    sys.exit(main())
