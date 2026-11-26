#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""midi_test.py WORK MACHINE [CASE...]: a MIDI file played out of the MIDI
port (MIDI.OVL, decided 25 Nov 2026), Hatari's MIDI out written to a file
(--midi-out) and its bytes checked. The cases:

  play   a short type 1 file to its end: the GM reset, the master volume,
         then the file's messages in their order, then All Notes Off and
         Reset All Controllers on all 16 channels; the end in its time
  pause  Pause in the middle and Play: the notes off at Pause; at Play the
         reset again, the file's program and controller there (the chase)
         and the rest of its messages, none lost nor sent twice
  quiet  nothing at all on the port before a MIDI file plays, nor after

Screenshots in WORK/CASE/shots. Exits 1 when a case failed."""
import os
import shutil
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, "..", "..")
sys.path.insert(0, HERE)
import speed  # noqa: E402  (EPlay's Rig)

CASES = ("play", "pause", "quiet")
GM_ON = bytes([0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7])
ALL_OFF = b"".join(bytes([0xb0 | c, 123, 0, 121, 0]) for c in range(16))


def vlq(n):
    b = [n & 0x7f]
    n >>= 7
    while n:
        b.insert(0, 0x80 | (n & 0x7f))
        n >>= 7
    return bytes(b)


def track(evs):
    body = b"".join(vlq(d) + e for d, e in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(body)) + body


def song(seconds):
    """a type 1 file: the tempo and name in track 0, a program, a
    controller and a note each quarter in track 1 (120 a minute, 480 a
    quarter): its messages as they should go out, in order"""
    t0 = track([(0, b"\xff\x03\x09The tests"), (0, b"\xff\x51\x03\x07\xa1\x20")])
    evs, want = [(0, b"\xc0\x18"), (0, b"\xb0\x07\x64")], [b"\xc0\x18", b"\xb0\x07\x64"]
    for k in range(seconds * 2):
        n = 0x3c + k % 12
        evs += [(0 if k == 0 else 240, bytes([0x90, n, 0x50])), (240, bytes([0x80, n, 0x40]))]
        want += [bytes([0x90, n, 0x50]), bytes([0x80, n, 0x40])]
    data = b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480) + t0 + track(evs)
    return data, want


def master(att):
    v = 16383
    for _ in range(att):
        v = v * 8414 // 10000
    return bytes([0xf0, 0x7f, 0x7f, 0x04, 0x01, v & 0x7f, v >> 7, 0xf7])


def wait(r, text, secs):
    t_end = time.time() + secs
    while time.time() < t_end:
        if text in r.console():
            return True
        time.sleep(0.5)
    return False


def take(b, want, what, fails):
    """want at b's start: the rest of b"""
    if not b.startswith(want):
        fails.append("%s: %s, wanted %s" % (what, b[:len(want) + 4].hex(" "), want.hex(" ")))
        return None
    return b[len(want):]


def session(work, spec, case):
    out = os.path.join(work, case + ".midiout")
    os.makedirs(work, exist_ok=True)
    inp = os.path.join(work, case + ".midiin")
    open(out, "wb").close()
    open(inp, "wb").close()
    r = speed.Rig(os.path.join(work, case), spec, display=os.environ.get("EPLAY_SPEED_DISPLAY", ":91"),
                  extra=["--midi-in", inp, "--midi-out", out])
    fails = []
    try:
        st = r.m.name.startswith("STE")
        prg = os.path.join(r.work, "eplay.prg")
        shutil.copy(os.environ.get("EPLAY_MIDI_PRG") or os.path.join(TOP, "binary", "eplayst.prg" if st else "eplay.prg"), prg)
        r.put(prg)
        r.put(os.path.join(TOP, "binary", "midi.ovl"))
        r.put(os.path.join(HERE, "build", "LAUNCH.PRG"))
        r.write("eplay.inf", "")
        data, msgs = song(25 if case == "pause" else 3)
        with open(os.path.join(r.hd, "SONG.MID"), "wb") as f:
            f.write(data)
        if case == "quiet":
            # a WAV first: nothing on the MIDI port while it plays
            import subprocess
            subprocess.run(["sox", "-n", "-r", "44100", "-c", "2", "-b", "16", os.path.join(r.hd, "A.WAV"), "synth",
                            "3", "sine", "1000", "gain", "-6"], check=True)
            r.write("TAIL.TXT", "C:\\A.WAV\n")
        else:
            r.write("TAIL.TXT", "C:\\SONG.MID\n")
        r.start("C:\\LAUNCH.PRG", ready="EPLAY playing")
        t_play = time.time()
        if case == "pause":
            time.sleep(4)
            r.key("space")
            if not wait(r, "EPLAY paused at ", 10):
                fails.append("pause: never paused")
            time.sleep(2)
            mid = os.path.getsize(out)
            r.key("space")
            if not wait(r, "EPLAY resumed at ", 10):
                fails.append("pause: never resumed")
        if not wait(r, "EPLAY ended", 60):
            fails.append("%s: never ended" % case)
        took = time.time() - t_play
        time.sleep(2)
        r.shot("1-ended")
        got = open(out, "rb").read()
        con = r.console()
        if case == "quiet":
            if got:
                fails.append("quiet: %d bytes on the MIDI port with a WAV playing: %s" % (len(got), got[:16].hex(" ")))
        elif case == "play":
            rest = take(got, GM_ON + master(0), "play: the start", fails)
            if rest is not None:
                rest = take(rest, b"".join(msgs), "play: the file's messages", fails)
            if rest is not None and rest != ALL_OFF:
                fails.append("play: the end: %s, wanted the notes off on all 16 channels" % rest[:20].hex(" "))
            if took > 20:
                fails.append("play: ended %.1f s after the start (3 s of music)" % took)
        else:
            # before Pause: the start, then a prefix of the messages, then the notes off
            rest = take(got, GM_ON + master(0), "pause: the start", fails)
            if rest is not None:
                all_ = b"".join(msgs)
                k = rest.find(ALL_OFF)
                if k < 0:
                    fails.append("pause: no notes off at Pause")
                else:
                    before, after = rest[:k], rest[k + len(ALL_OFF):]
                    if not all_.startswith(before):
                        fails.append("pause: before Pause %s isn't the file's start" % before[-12:].hex(" "))
                    # at Play: the reset, the volume, the chase (program, then the controller), the rest
                    after = take(after, GM_ON + master(0) + b"\xc0\x18" + b"\xb0\x07\x64", "pause: at Play", fails)
                    if after is not None:
                        left = all_[len(before):]
                        if not after.startswith(left) and not left.startswith(after[:len(left)]):
                            # (a note on before Pause whose off came after: the chase
                            # sends no note, so the rest is from Play's place on)
                            tail = after[:-len(ALL_OFF)] if after.endswith(ALL_OFF) else after
                            if not all_.endswith(tail):
                                fails.append("pause: after Play %s isn't the rest of the file" % tail[:12].hex(" "))
                        if not after.endswith(ALL_OFF):
                            fails.append("pause: no notes off at the end")
            if mid and got[mid - len(ALL_OFF):mid] != ALL_OFF:
                fails.append("pause: the port's last bytes at Pause aren't the notes off")
        if "MIDI.OVL" not in con and case != "quiet":
            fails.append("%s: MIDI.OVL never loaded" % case)
        if case == "quiet" and "MIDI.OVL" in con:
            fails.append("quiet: MIDI.OVL loaded with no MIDI file")
        for d, _, fs in os.walk(r.hd):
            if any(f.upper() == "CRASH.LOG" for f in fs):
                fails.append("%s: CRASH.LOG in %s" % (case, d))
        print("%-6s on %s: %s" % (case, r.describe(), "ok" if not fails else "; ".join(fails)), flush=True)
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
