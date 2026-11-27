#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""menu_test.py WORK MACHINE [CASE...]: a menu of the menu bar held open
while the music plays (a fault reported 27 Nov 2026). The AES's screen
manager holds the mouse while a menu is down, and EPlay's turns don't
come: what the sound needs must not wait on them, and once the menu is
closed the music goes on as before. The cases:

  wav    a WAV (16-bit stereo, 44.1 kHz)
  mp3    an MP3 (320 kbit/s, 44.1 kHz stereo) on the DSP
  aiff24 a 24-bit AIFF (44.1 kHz stereo: 258 KB/s), the heaviest file to read
  mid    a MIDI file on the internal synth (MIDI=SYNTH); a machine that
         sends it to the MIDI port (a stock Falcon: too slow for the
         synth) is said so, and the case passes over it
  vgm    a VGM's YM2612 (play_test's) made on the CPU: a 68060 with no
         DSP (play060.py's Falcon-060nodsp); elsewhere on the DSP, said so

The 68060 machines (Falcon-060, Falcon-060nodsp) are play060.py's. mid
on Falcon-060 is no evidence: Hatari's 68060 (32 MHz; a CT60 runs 66 to
100) doesn't keep up with the synth at 49,170 Hz even with no menu
(MENU_HOLD=0, the control: 415 underruns, the synth 82 % of the time).

Each plays 30 s; 4 s in, the View menu is opened and held 8 s, then
closed. Passes when EPlay's own count (the console's "EPLAY ended"
line) has no underrun: the sound went on all the while the menu was
down, and after. MENU_DISKKBS=481 makes each read of the file take a
real disk's time (EPLAY.INF's DISKKBS=; Hatari's disk answers at once): GEM reads it, so
the queue may run dry once while the menu is held (silence, not the
ring's old sound round again, since 27 Nov 2026), and the music goes on
a second after it is closed; one underrun then, no more.
Screenshots in WORK/CASE/shots. Exits 1 when a case failed."""
import os
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, "..", "..")
sys.path.insert(0, HERE)
if any(a.startswith(("Falcon-0", "Falcon-v4")) for a in sys.argv):
    import play060  # noqa: E402,F401  (its machines)
import speed  # noqa: E402  (EPlay's Rig)
import midi_test  # noqa: E402  (its MIDI file writer)
import play_test  # noqa: E402  (its VGM file writer)

CASES = ("wav", "aiff24", "mp3", "mid", "vgm")
VIEW_X = 190            # the View menu's title (speed.py's VIEW)
LONG = 30               # seconds of music
DISK = int(os.environ.get("MENU_DISKKBS", "0"))  # KB/s: the file read at a real disk's pace (EPLAY.INF's DISKKBS=)
HOLD = int(os.environ.get("MENU_HOLD", "8"))    # seconds the menu is held (0: none, the control)
# (MENU_INF="SNDXBIOS=1;...": lines added to EPLAY.INF, the XBIOS's sound on the 68060 stand-ins)


def chords(seconds):
    """a type 1 file at 120 a minute: a held chord each half note on a
    piano and strings, so that the synth has voices to make"""
    t0 = midi_test.track([(0, b"\xff\x51\x03\x07\xa1\x20")])
    evs = [(0, b"\xc0\x00"), (0, b"\xc1\x30")]
    roots = (0x30, 0x35, 0x37, 0x30)
    for k in range(seconds):
        r = roots[k % 4]
        notes = (r, r + 4, r + 7, r + 12)
        first = True
        for ch in (0, 1):
            for n in notes:
                evs.append((0, bytes([0x90 | ch, n, 0x60])))
        for ch in (0, 1):
            for n in notes:
                evs.append((960 if first else 0, bytes([0x80 | ch, n, 0x40])))
                first = False
    return b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480) + t0 + midi_test.track(evs)


def wait(r, text, secs):
    t_end = time.time() + secs
    while time.time() < t_end:
        if text in r.console():
            return True
        time.sleep(0.5)
    return False


def session(work, spec, case):
    r = speed.Rig(os.path.join(work, case), spec, nvdi=os.environ.get("AERA_WITH_NVDI") == "1",
                  display=os.environ.get("EPLAY_SPEED_DISPLAY", ":91"))
    fails = []
    try:
        st = r.m.name.startswith("STE")
        prg = os.path.join(r.work, "eplay.prg")
        shutil.copy(os.environ.get("EPLAY_MENU_PRG") or os.path.join(TOP, "binary", "eplayst.prg" if st else "eplay.prg"),
                    prg)
        r.put(prg)
        for f in ("synth.ovl", "synth.dat", "midi.ovl", "mp3fpu.ovl", "mp3int.ovl"):
            r.put(os.path.join(os.path.dirname(os.environ.get("EPLAY_MENU_PRG") or os.path.join(TOP, "binary", "x")), f))
        r.put(os.path.join(HERE, "build", "LAUNCH.PRG"))
        # (an INF as the other rigs have one: none is a first run)
        r.write("eplay.inf", "MIDI=SYNTH\r\nLOG=1\r\n" + ("DISKKBS=%d\r\n" % DISK if DISK else "") +
                "".join(l + "\r\n" for l in os.environ.get("MENU_INF", "").split(";") if l))
        if case == "mid":
            with open(os.path.join(r.hd, "SONG.MID"), "wb") as f:
                f.write(chords(LONG))
            r.write("TAIL.TXT", "C:\\SONG.MID\n")
        elif case == "vgm":
            play_test.make_vgm(os.path.join(r.hd, "MD.VGM"), LONG, False, md=True)
            r.write("TAIL.TXT", "C:\\MD.VGM\n")
        elif case == "aiff24":
            subprocess.run(["sox", "-n", "-r", "44100", "-c", "2", "-b", "24", os.path.join(r.hd, "A.AIF"), "synth",
                            str(LONG), "sine", "440", "gain", "-6"], check=True)
            r.write("TAIL.TXT", "C:\\A.AIF\n")
        elif case == "mp3":
            src = os.path.join(r.work, "a.wav")
            subprocess.run(["sox", "-n", "-r", "44100", "-c", "2", "-b", "16", src, "synth", str(LONG), "sine", "440",
                            "synth", str(LONG), "pinknoise", "mix", "gain", "-9"], check=True)
            subprocess.run(["lame", "--quiet", "-t", "--noreplaygain", "-b", "320", src, os.path.join(r.hd, "A.MP3")],
                           check=True)
            r.write("TAIL.TXT", "C:\\A.MP3\n")
        else:
            subprocess.run(["sox", "-n", "-r", "44100", "-c", "2", "-b", "16", os.path.join(r.hd, "A.WAV"), "synth",
                            str(LONG), "sine", "440", "gain", "-6"], check=True)
            r.write("TAIL.TXT", "C:\\A.WAV\n")
        r.start("C:\\LAUNCH.PRG", ready="EPLAY playing")
        t_play = time.time()
        time.sleep(4)
        if HOLD:
            r.click(VIEW_X, 8)      # the View menu down, and held
            time.sleep(1)
            r.shot("1-menu")
            time.sleep(HOLD - 1)
            r.click(600, 300)       # closed: a click outside it
        t_closed = time.time()
        time.sleep(1)
        r.shot("2-closed")
        if not wait(r, "EPLAY ended", LONG + 60):
            fails.append("%s: never ended" % case)
        took = time.time() - t_play
        con = r.console()
        end = [l for l in con.splitlines() if l.startswith("EPLAY ended")]
        playing = ([l for l in con.splitlines() if l.startswith("EPLAY playing")] or [""])[-1]
        if case == "mid" and playing.endswith("MIDI port"):
            print("%-4s on %s: not tried: the file went to the MIDI port (%s)" % (case, r.describe(), playing),
                  flush=True)
            return []
        if case == "vgm" and "nodsp" not in spec:
            print("%-4s on %s: not tried: the chips on the DSP there (%s)" % (case, r.describe(), playing), flush=True)
            return []
        under = int(end[-1].split()[2]) if end and end[-1].split()[2].isdigit() else -1
        # (a real disk's pace: the queue may run dry once while the menu is
        # held, as GEM reads the file; never again once it is closed)
        # (aiff24: the point is that the reads keep the lead, so none even
        # at a disk's pace: the code before 64b897d ran dry there, a 4 s
        # menu at DISKKBS=481, its least lead 61 ms)
        if under < 0 or under > (1 if DISK and case != "aiff24" else 0):
            fails.append("%s: %s (the menu held %d s%s)" % (case, end[-1] if end else "no end", HOLD,
                                                            ", the disk at %d KB/s" % DISK if DISK else ""))
        if took > LONG + 15:
            fails.append("%s: ended %.1f s after the start (%d s of music)" % (case, took, LONG))
        for d, _, fs in os.walk(r.hd):
            if any(f.upper() == "CRASH.LOG" for f in fs):
                fails.append("%s: CRASH.LOG in %s" % (case, d))
        log = os.path.join(r.hd, "EPLAY.LOG")
        if os.path.exists(log):
            shutil.copy(log, os.path.join(r.work, "eplay-log.txt"))
        print("%-4s on %s: %s (%s; the menu closed %.1f s in)" % (case, r.describe(), "ok" if not fails else "; ".join(fails),
                                                                end[-1] if end else "no end", t_closed - t_play), flush=True)
    finally:
        r.stop()
    return fails


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        print(__doc__)
        return 2
    work, spec = os.path.abspath(a[0]), a[1]
    fails = []
    for c in a[2:] or list(CASES):
        fails += session(work, spec, c)
    for f in fails:
        print("FAIL  " + f)
    print("all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
