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
# the bar's round Play/Pause button (design D at 56 columns: Erez, 3 Nov 2026)
# (the window 11 pixels to the left of 0.20's: the list's slider at its right)
PLAY = PAUSE = (389, 150)
POS_MID = (389, 185)		# the position bar's middle
VOL_Q = (498, 185)		# a quarter of the volume bar: 4 of 16
NEXT = (421, 150)
ROW = [(300, 229 + 20 * i) for i in range(6)]	# the list's rows
SECS = 120	# (long enough not to end before the last action, when the emulator runs fast)

# A redraw row (Erez's rule, 4 Nov 2026, the hub's ruling the same day): a
# redraw of 300 ms or less is drawn whole, no event call between its pieces,
# so its stretch is held to WINDOW, and the user's wait, from the key or
# click to the whole picture (EPlay's "EPLAY picture NAME N ms, M from the
# input"), may not be later than 0.22's: its figures below (measure/0.22-
# pictures 016d7c78, Falcon-4:16), with a rig step (5 ms) of give.
PICTURE_022 = {
    "EmuTOS": {"a row played (double click)": 175, "Next": 200, "Return (plays the selected)": 155,
               "Delete (one out of the list)": 65, "MUSIC opened (a double click)": 100},
    "TOS": {"a row played (double click)": 160, "Next": 180, "Return (plays the selected)": 140,
            "Delete (one out of the list)": 75, "MUSIC opened (a double click)": 100},
}
STEP = 5


# where EPlay's window's work area opens on the Falcon's 640 x 480 (its
# "EPLAY window" line): the places above are its; on another screen (an
# STE's 640 x 400) they move with the window (Rig.at)
WORK_FALCON = (85, 131)
PLACE = "WINDOW=87,89,472,251\r\n"	# (that work area's window: 1.15's first place)


class Rig(speed.Rig):
    quick = False

    def at(self, x, y):
        """a place in EPlay's window as on the Falcon, where the window is
        now (from its "EPLAY window" line); the menu bar's (y < 60) as it is"""
        if y < 60:
            return x, y
        m = re.findall(r"EPLAY window (-?\d+) (-?\d+) \d+ \d+", self.console())
        if not m:
            return x, y
        return x + int(m[-1][0]) - WORK_FALCON[0], y + int(m[-1][1]) - WORK_FALCON[1]

    def click(self, x, y, button="1"):
        super().click(*self.at(x, y), button)

    def write(self, name, text):
        """eplay.inf with the window's old first place (1.16 opens a first
        start at the desk's height: the figures stay comparable from
        release to release, and the Add files dialog's places, the screen's,
        stay where the rows are; play_test's firstrun checks the new place)"""
        if name == "eplay.inf" and "WINDOW=" not in text:
            text = text + PLACE
        super().write(name, text)

    def still(self, most=30):
        """while it plays the time changes each second: a few seconds are
        taken as settled (the stretches are on the console meanwhile); at
        once for the start of an MP3 whose cover is unpacked first (its wait
        is a row of its own)"""
        time.sleep(0.3 if self.quick else 3)
        return True

    def picture_apart(self, start, picture):
        """a job's turns with the picture's stretch apart: the picture
        drawn whole (Erez's rule, 4 Nov 2026) is the stretch that comes
        with its marker, a redraw held to WINDOW, not a job's turn (the
        start's picture comes in the job's first turns or its later ones,
        as the DSP's program load falls: 25 or 100 ms in "its turns")"""
        with open(self.log, errors="replace") as f:
            f.seek(start)
            new = f.read()
        gaps, pic, after = [], None, False
        for line in new.split("\n"):
            if line.startswith("EPLAY picture %s " % picture):
                after = True
            m = re.match(r"AESGAP (\d+) ms", line)
            if m:
                if after and pic is None:
                    pic, after = int(m.group(1)), False
                else:
                    gaps.append(int(m.group(1)))
        for i, (what, ms, budget) in enumerate(self.results):
            if what.endswith(": its turns"):
                self.results[i] = (what, max(gaps[1:]) if len(gaps) > 1 else 0, budget)
        if pic is not None:
            self.results.append((self.results[-1][0].replace(": its turns", "") + ": its picture, whole", pic,
                                 speed.WINDOW))

    def redraw(self, what, picture, do):
        """a redraw row: its stretch held to WINDOW, and its picture's time
        from the input to 0.22's (PICTURE_022), a second line in the table"""
        start = os.path.getsize(self.log)
        self.act(what, speed.WINDOW, do)
        with open(self.log, errors="replace") as f:
            f.seek(start)
            new = f.read()
        got = re.findall(r"EPLAY picture %s \d+ ms, (\d+) from the input" % picture, new)
        tos = "EmuTOS" if "EmuTOS" in self.describe() else "TOS"
        # (a row 0.22 hadn't, the drag: held to WINDOW, from the release)
        ref = PICTURE_022[tos][what] + STEP if what in PICTURE_022[tos] else speed.WINDOW
        # (no marker: shown as "?", over)
        self.results.append((what + ": the whole picture", int(got[-1]) if got else None, ref))

    def wait_line(self, text, secs=30):
        end = time.time() + secs
        while time.time() < end and text not in self.console():
            time.sleep(0.5)

    def press(self, x, y):
        """the button down over x, y (held: the guidelines' 5.8 times the
        press, the button drawn pressed, apart from the release's action)"""
        x, y = self.at(x, y)
        g = dict(l.split("=") for l in self.xdo("getwindowgeometry", "--shell", self.win()).split())
        self.xdo("mousemove", str(int(g["X"]) + x), str(int(g["Y"]) + y))
        time.sleep(0.4)
        self.xdo("mousedown", "1")

    def dclick(self, x, y):
        x, y = self.at(x, y)
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
        x, y = self.at(x, y)
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
        # (an STE: the 68000's edition, make profst, under the same name)
        r.put(os.path.join(TOP, "build", "profst" if r.m.name.startswith("STE") else "prof", "eplay.prg"))
        r.put(os.path.join(HERE, "build", "LAUNCH.PRG"))
        r.write("eplay.inf", "")		# (the place: Rig.write; a session's own INF replaces it)
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
        elif kind == "add":
            return add_session(r, src)
        elif kind == "mini":
            return mini_session(r, src)
        elif kind == "vgm":
            # a Mega Drive file (the YM2612 on the DSP, its drums from the
            # 68030's tick): EPLAY_SPEED_VGM names one kept outside the repo
            # (a game's music is never committed)
            name, what = "REAL.VGZ", "a Mega Drive VGM (YM2612)"
            shutil.copy(os.environ["EPLAY_SPEED_VGM"], os.path.join(r.hd, name))
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
            start = os.path.getsize(r.log)
            r.act("an MP3 with an uncached cover: playing starts", 3000,
                  lambda: r.wait_line("EPLAY playing"), job=True)
            r.picture_apart(start, "start")
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
        # (Undo: Settings has a field since the Gateway's address, and Esc
        # empties a field: the family's 3.2)
        r.act("Settings closed (Undo)", speed.WINDOW, lambda: r.key("Undo"))
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
        if kind == "vgm":
            print("ok    the position bar's middle went to %d ms (a VGM's own length)" % ms)
        elif abs(ms - SECS * 500) > SECS * 50:
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
            # (each line "hh:mm:ss.cc EPlay -- ...": common's conn.c logger)
            lines_log = [re.sub(r"^\d\d:\d\d:\d\d\.\d\d EPlay -- ", "", l) for l in log.split("\n")]
            art = [l for l in lines_log if l.startswith("album art")]
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
    r.redraw("a row played (double click)", "start", lambda: r.dclick(*ROW[3]))
    r.redraw("Next", "start", lambda: r.click(*NEXT))
    r.act("Down", speed.KEY, lambda: r.key("Down"))
    r.redraw("Return (plays the selected)", "start", lambda: r.key("Return"))
    r.redraw("Delete (one out of the list)", "rows", lambda: (r.key("Up"), r.key("Delete")))

    def drag(y1, y2):
        g = dict(l.split("=") for l in r.xdo("getwindowgeometry", "--shell", r.win()).split())
        x, y = int(g["X"]), int(g["Y"])
        px, y1 = r.at(300, y1)
        _, y2 = r.at(300, y2)
        x += px - 300
        r.xdo("mousemove", str(x + 300), str(y + y1))
        time.sleep(0.4)
        r.xdo("mousedown", "1")
        time.sleep(0.3)
        for k in range(1, 11):	# (in steps, as a hand would)
            r.xdo("mousemove", str(x + 300), str(y + y1 + (y2 - y1) * k // 10))
            time.sleep(0.1)
        time.sleep(0.5)
        r.xdo("mouseup", "1")
    # (Erez, 5 Nov 2026: the list reordered by a drag) the first row to
    # between the fourth and the fifth: the line shown as it moves, the
    # rows between drawn again when it is let go
    r.redraw("A row dragged to another place", "rows", lambda: drag(ROW[0][1], ROW[4][1] - 10))
    rc = r.report()
    lines = r.console().split("\n")
    played = [l.split()[2] for l in lines if l.startswith("EPLAY playing: ")]
    want = ["T1.WAV", "T4.MP3", "T5.WAV", "T6.MP3"]
    moved = [l for l in lines if l.startswith("EPLAY moved ")]
    if moved != ["EPLAY moved 1 to 4"]:
        print("FAIL  the drag: %s (wanted EPLAY moved 1 to 4)" % moved)
        rc = 1
    else:
        print("ok    the drag: the first row moved to the fourth")
    if played[:4] != want:
        print("FAIL  played %s, not %s (the first, a double click on row 4, Next, Return on row 6)" %
              (played, want))
        rc = 1
    else:
        print("ok    played in turn: %s" % " ".join(played[:4]))
    return rc


def add_session(r, src):
    """the Add files dialog (Erez, 4 Nov 2026): ^O opens it at C:\\RIGADD\\
    (eplay.inf's ADDDIR), whose one folder is MUSIC: MUSIC opened by a
    double click on the first row; three files selected (a click, a
    Shift-click, another Shift-click) and added. (Not at the root: a TOS
    image's root has folders of its own, AUTO sorting before MUSIC: the
    hub, 4 Nov 2026. An AUTO folder is made at the root as such a root has.)"""
    os.makedirs(os.path.join(r.hd, "AUTO"), exist_ok=True)
    music = os.path.join(r.hd, "RIGADD", "MUSIC")
    os.makedirs(music)
    r.write("eplay.inf", "ADDDIR=C:\\RIGADD\\\r\n")
    short = os.path.join(r.work, "short.wav")
    subprocess.run(["sox", src, short, "trim", "0", "20"], check=True)
    for i in range(6):
        shutil.copy(short, os.path.join(music, "T%d.WAV" % (i + 1)))
    r.write("TAIL.TXT", "\n")
    r.start("C:\\LAUNCH.PRG", ready="EPLAY status:")
    r.results[-1] = ("started, nothing given", "AESSTART", speed.START)
    r.act("Add files opened (^O)", speed.DIALOG, lambda: r.key("ctrl+o"))
    r.shot("add-start")
    # (Undo: Esc empties the filter and never closes it, D28)
    r.act("Add files closed (Undo)", speed.WINDOW, lambda: r.key("Undo"))
    r.act("Add files opened again", speed.DIALOG, lambda: r.key("ctrl+o"))
    row = lambda i: (300, 206 + 16 * i)     # the dialog's list rows (Falcon-4's 640 x 480)

    def shift_click(x, y):
        r.xdo("keydown", "shift")
        time.sleep(0.2)
        r.click(x, y)
        time.sleep(0.2)
        r.xdo("keyup", "shift")
    r.redraw("MUSIC opened (a double click)", "folder", lambda: r.dclick(*row(0)))
    r.act("T1 selected (a click)", speed.KEY, lambda: r.click(*row(0)))
    r.act("T3 added to the selection (Shift-click)", speed.KEY, lambda: shift_click(*row(2)))
    r.act("T5 added to the selection (Shift-click)", speed.KEY, lambda: shift_click(*row(4)))
    r.shot("add-three")
    r.act("Add: the three into the list", speed.WINDOW, lambda: r.click(455, 401))
    time.sleep(3)
    rc = r.report()
    lines = r.console().split("\n")
    listed = [l for l in lines if l.startswith("EPLAY list: ")]
    played = [l.split()[2] for l in lines if l.startswith("EPLAY playing: ")]
    if not listed or listed[-1].split()[2] != "3" or played[:1] != ["T1.WAV"]:
        print("FAIL  added %s, played %s (wanted 3 files, T1.WAV first)" % (listed, played))
        rc = 1
    else:
        print("ok    three files added (T1, T3, T5), T1 playing")
    return rc


MINI_PLAY = (400, 185)		# the mini player's Play/Pause (the bar alone: no slider, lower down)
VIEW = (190, 28)		# View > Mini player (the title's x, the item's y)


def mini_session(r, src):
    """the mini player (Erez, 3 Nov 2026: the bar alone), from eplay.inf's
    MINI=1: Pause and Play in it, then View > Mini player to the whole
    window and back"""
    short = os.path.join(r.work, "short.wav")
    subprocess.run(["sox", src, short, "trim", "0", "60"], check=True)
    subprocess.run(["lame", "--quiet", "--noreplaygain", "-b", "192", "--tt", "Mini", "--ta", "The rig", short,
                    os.path.join(r.hd, "MINI.MP3")], check=True)
    r.write("eplay.inf", "MINI=1\r\n")
    r.write("TAIL.TXT", "C:\\MINI.MP3\n")
    r.start("C:\\LAUNCH.PRG", ready="EPLAY playing")
    r.results[-1] = ("started in the mini player, playing", "AESSTART", speed.START)
    # (MINI_PLAY is the screen's place: Rig.click would move it as if it
    # were in the full window's work area, and missed: paused 0 times)
    r.act("Pause (mini)", speed.KEY, lambda: speed.Rig.click(r, *MINI_PLAY))
    r.act("Play (mini)", speed.KEY, lambda: speed.Rig.click(r, *MINI_PLAY))
    r.act("View > Mini player: the whole window", speed.WINDOW, lambda: r.menu(VIEW[0] - 20, VIEW[1]))
    r.act("View > Mini player: the bar alone", speed.WINDOW, lambda: r.menu(VIEW[0] - 20, VIEW[1]))
    rc = r.report()
    lines = r.console().split("\n")
    minis = [l.split()[2] for l in lines if l.startswith("EPLAY mini ")]
    paused = [l for l in lines if l.startswith("EPLAY paused at ")]
    if minis != ["0", "1"] or not paused:
        print("FAIL  the mini player: switched %s (wanted 0 then 1), paused %d times" % (minis, len(paused)))
        rc = 1
    else:
        print("ok    the mini player paused and went on; switched to the whole window and back")
    return rc


def main():
    work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "/tmp/eplay-speed")
    spec = sys.argv[2] if len(sys.argv) > 2 else "Falcon-4:16"
    kinds = sys.argv[3:] or ["mp3", "wav", "list", "mini", "add"]	# (and vgm, asked for)
    if spec.split(":")[0] in ("ST", "ST-1"):
        # (EPlay leaves out the plain ST: no DMA sound, DESIGN.md; it says so
        # and stops. Its 8 MHz 68000 is the STE's edition's: STE:mono. A
        # Falcon's eplay.prg on an ST's 68000 never started: 11 Nov 2026)
        print("speed: EPlay leaves out the plain ST (no DMA sound): its 68000 at 8 MHz is STE:mono's")
        return 1
    return max(session(work, spec, k) for k in kinds)


if __name__ == "__main__":
    sys.exit(main())
