#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EPlay plays in Hatari: a WAV, an AIFF and an MP3 (on the emulated DSP)
on the Falcon (common/tools/machines.py), with Hatari's sound recorded
and checked: the tone the file holds, at the pitch the Falcon's rate
gives, in both channels, without a gap, then silence after the end.

C:\\LAUNCH.PRG (make) starts C:\\eplay.prg with the file in its command
line, as the desktop does with a file dropped on it; EPlay says what it
does on the emulator's console (nf_debug), which the rig follows.

    make && python3 play_test.py WORK [CASE...] [MACHINE...]

The cases: wav, wavvol, aiff, aiff24, wav22, mp3, mp3long, vbr, fsel, normal; all by default. The machines: Falcon (Erez's, with the
DFB1) and Falcon-4 (a stock Falcon) by default. Each case leaves
WORK/MACHINE-CASE/rec.wav (what the Falcon played) and a
screenshot while it plays in WORK/MACHINE-CASE/shots.
"""
import math
import os
import shutil
import stat
import struct
import subprocess
import sys
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(TOP, "common", "tools"))
import machines  # noqa: E402

REC_RATE = 48000

# name: (file, how sox makes its source, lame's arguments or None, the
# rate the Falcon plays it at, the left and right tones heard, seconds).
# What the DSP resamples to the Falcon's clock (an MP3, a WAV at 44.1 kHz)
# is heard at its own pitch
CASES = {
    "wav": ("TONE.WAV", ["-r", "24585", "-c", "2", "-b", "16"], None, 24585, (1000, 1500), 3),
    # the volume at 8 of 16 (eplay.inf's VOLUME): the Falcon's own output 12 dB
    # down (the codec's attenuation, 8 steps of 1.5 dB)
    "wavvol": ("TONE.WAV", ["-r", "24585", "-c", "2", "-b", "16"], None, 24585, (1000, 1500), 3),
    "aiff": ("TONE.AIF", ["-r", "32780", "-c", "1", "-b", "16"], None, 32780, (1000, 1000), 3),
    # 24 bits, stereo, ten seconds at a rate the DMA plays as it is (Erez's
    # 24-bit AIFF at 44.1 kHz on the FDI's clock, 3 Nov 2026: 330 underruns
    # in 6 s, the first at 0.86 s)
    "aiff24": ("T24.AIF", ["-r", "49170", "-c", "2", "-b", "24"], None, 49170, (1000, 1500), 10),
    # Erez's AIFF of 4 Nov 2026: 24 bits, stereo, 44.1 kHz, with Output DSP
    # (no FDI): the Falcon's clock hasn't the rate, so it is resampled to
    # 49170 Hz and heard at its own pitch (0.21 played it on the DMA at
    # 49170 Hz as it was, 11 % fast: crackles, and a crash)
    "aiff44": ("T44.AIF", ["-r", "44100", "-c", "2", "-b", "24"], None, 49170, (1000, 1500), 10),
    # a rate the Falcon's clock hasn't, 22.05 kHz mono: through the DSP,
    # resampled to 24585 Hz (CD's stereo stays on the DMA, at 49170: a
    # host-port word a sample would cost a stock Falcon's 68030 too much)
    "wav22": ("HALF.WAV", ["-r", "22050", "-c", "1", "-b", "16"], None, 24585, (1000, 1000), 3),
    "mp3": ("TONE.MP3", ["-r", "32000", "-c", "2", "-b", "16"], ["-b", "128", "-m", "s"], 32780,
            (1000, 1500), 3),
    # ten seconds at 320 kbit/s: the ring wraps four times, the file
    # buffer (64 KB) is refilled six times across the bit reservoir
    "mp3long": ("LONG.MP3", ["-r", "44100", "-c", "2", "-b", "16"], ["-b", "320", "-m", "s"], 49170,
                (1000, 1500), 10),
    # the file selector (Control+O) held open 5 s while it plays: GEM
    # gives EPlay no event meanwhile, and the music must go on (the tick)
    # VBR, as music is: LAME's header frame (Xing) gives the length (0.1 took
    # the first frame's bit rate: an 8-minute file said 22:10)
    "vbr": ("VBR.MP3", ["-r", "44100", "-c", "2", "-b", "16"], ["XING", "-V", "0"], 49170,
            (1000, 1500), 10),
    "fsel": ("LONG.MP3", ["-r", "44100", "-c", "2", "-b", "16"], ["-b", "320", "-m", "s"], 49170,
             (1000, 1500), 10),
    # Settings' Quality Normal (eplay.inf): no external clock, so the
    # Falcon's 32,780 Hz, the subbands above about 15 kHz left out
    "normal": ("NORMAL.MP3", ["-r", "44100", "-c", "2", "-b", "16"], ["-b", "192", "-m", "s"], 32780,
               (1000, 1500), 6),
}
# three files given at the start (as the desktop gives files dropped on the
# program): the list holds them and plays them one after another (Erez,
# 3 Nov 2026: files added in bulk). Each a second, A and C WAVs, B an MP3
LIST = [("A.WAV", None), ("B.MP3", ["-b", "128"]), ("C.WAV", None)]
CASES["list"] = ("A.WAV", ["-r", "24585", "-c", "2", "-b", "16"], None, 24585, (1000, 1500), 1)
# a folder of 1,000 files given (as dropped on the program): all in the
# list, their tags and lengths read, and the memory it takes (the hub, 4
# Nov 2026: the free ST RAM a menu needs under NVDI). Not by default (slow)
CASES["many"] = ("A.WAV", ["-r", "24585", "-c", "2", "-b", "16"], None, 24585, (1000, 1500), 1)
NOT_DEFAULT = {"many"}
# the cases' eplay.inf
INFS = {"aiff44": "OUTPUT=DSP\r\n\r\n", "normal": "QUALITY=NORMAL\r\n\r\n", "wavvol": "VOLUME=8\r\n\r\n", "mini": "MINI=1\r\n\r\n"}
# the mini player (eplay.inf's MINI=1): the bar alone, playing as ever
CASES["mini"] = CASES["mp3"]
# the level of the middle (RMS), each case's, for wavvol's against wav's
LEVEL = {}


def make_file(hd, case):
    f, fmt, lame, _, _, secs = CASES[case]
    src = os.path.join(hd, "..", "src.wav")
    synth = ["synth", str(secs), "sine", "1000"] + (["sine", "1500"] if fmt[3] == "2" else [])
    out = os.path.join(hd, f)
    if lame:
        subprocess.run(["sox", "-n"] + fmt + [src] + synth + ["gain", "-6"], check=True)
        tag = [] if "XING" in lame else ["-t"]          # (-t: no header frame)
        subprocess.run(["lame"] + tag + ["--quiet", "--noreplaygain"] + [a for a in lame if a != "XING"] +
                       [src, out], check=True)
    else:
        subprocess.run(["sox", "-n"] + fmt + [out] + synth + ["gain", "-6"], check=True)
    return f


def wait_fifo(fifo):
    for _ in range(300):
        if os.path.exists(fifo) and stat.S_ISFIFO(os.stat(fifo).st_mode):
            return
        time.sleep(0.1)
    raise RuntimeError("Hatari's command FIFO never appeared")


def cmd(fifo, *args):
    wait_fifo(fifo)
    with open(fifo, "w") as fh:
        fh.write(" ".join(args) + "\n")
    time.sleep(0.05)


def wait_line(log, pred, secs):
    end = time.time() + secs
    while time.time() < end:
        for line in open(log, errors="replace"):
            if pred(line):
                return line.strip()
        time.sleep(0.2)
    return None


def tone(x, rate):
    """the strongest frequency in x (a Goertzel scan, 1 Hz steps around the
    tones the cases use), and the share of x's power in it"""
    n = len(x)
    total = sum(v * v for v in x) or 1.0
    best, bp = 0, 0.0
    for f in range(900, 1700, 2):
        w = 2 * math.pi * f / rate
        c = 2 * math.cos(w)
        s1 = s2 = 0.0
        for v in x:
            s0 = v + c * s1 - s2
            s2, s1 = s1, s0
        p = s1 * s1 + s2 * s2 - c * s1 * s2
        if p > bp:
            best, bp = f, p
    return best, 2 * bp / n / total


RESAMPLED = {"wav22", "aiff44"}       # (and every MP3)
WAV_LEVEL = 0               # (set below once measured: wav's level on Hatari's Falcon)


def analyse(rec, case):
    """(ok, what was found) for the recording"""
    with wave.open(rec) as w:
        rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        raw = w.readframes(n)
    s = struct.unpack("<%dh" % (len(raw) // 2), raw)
    left, right = s[0::ch], s[1::ch] if ch > 1 else s[0::ch]
    blk = rate // 50                       # 20 ms blocks
    rms = [math.sqrt(sum(v * v for v in left[i:i + blk]) / blk) for i in range(0, len(left) - blk, blk)]
    loud = [i for i, r in enumerate(rms) if r > 1000]
    if not loud:
        if hatari_keeps_parity() and not CASES[case][2] and case not in RESAMPLED:
            # the sound DMA plays four tracks, the music in the first
            # (Erez's Analog 8: channels 3 to 8 silent). Hatari 2.4 keeps
            # the DMA's word count from its last stop (EPlay's clock
            # measurement), so its DAC hears another track: silence. Later
            # Hatari starts each DMA at the first word ("1st frame is
            # left"), as the Falcon does
            return True, "no sound: Hatari %s's DMA starts mid-frame, its DAC on a silent track (excused)" % HATARI
        return False, "no sound recorded (%d frames at %d Hz)" % (n, rate)
    a, b = loud[0], loud[-1]
    secs = (b - a + 1) / 50
    gaps = [i for i in range(a, b + 1) if rms[i] < 1000]
    mid = (a + b) // 2 * blk
    seg = slice(mid - 2400, mid + 2400)
    level = math.sqrt(sum(v * v for v in left[seg]) / len(left[seg]))
    LEVEL[case] = level
    vol_ok, vol_note = True, ""
    if case == "wavvol":
        # 12 dB down from wav's (run first), else from what wav has on
        # Hatari's Falcon at full volume
        full = LEVEL.get("wav", WAV_LEVEL)
        db = 20 * math.log10(level / full) if level > 0 and full > 0 else -99
        vol_ok = abs(db + 12) < 1
        vol_note = "; the level %.1f dB from full volume (12 dB down wanted)" % db
    fl, ql = tone([v / 32768 for v in left[seg]], rate)
    fr, qr = tone([v / 32768 for v in right[seg]], rate)
    want = CASES[case][4]

    def near(a, b):
        return abs(a[0] - b[0]) < 8 and abs(a[1] - b[1]) < 8
    note = ""
    pitch = near((fl, fr), want)
    # (an MP3 is played by the DSP's SSI, not the DMA: no swap excused)
    if not pitch and near((fr, fl), want) and hatari_keeps_parity() and not CASES[case][2] \
            and case not in RESAMPLED:
        # Hatari 2.4 doesn't start a DMA's stereo on the left: it keeps the
        # word count's parity from the DMA's last stop (EPlay's clock
        # measurement), fixed in later Hatari (crossbar.c, "1st frame is
        # left"). The Falcon starts each frame on the left.
        pitch, note = True, " (left and right swapped: Hatari %s's DMA)" % HATARI
    if not pitch and hatari_keeps_parity() and not CASES[case][2] and case not in RESAMPLED and \
            ((abs(fl - want[1]) < 8 and ql > 0.9) or (abs(fr - want[0]) < 8 and qr > 0.9)):
        # (four tracks a frame: Hatari 2.4's DAC a word off takes one
        # channel and a silent track's word: one side the other's tone,
        # the other silent)
        pitch, note = True, " (a word off: Hatari %s's DMA, four tracks)" % HATARI
        ql = qr = 1.0
    _, fmt, lame, out, _, length = CASES[case]
    if not lame and case not in RESAMPLED:
        length = length * int(fmt[1]) / out  # (the DMA: at the Falcon's rate, if not the file's)
    ok = (vol_ok and abs(secs - length) < 0.1 and not gaps and pitch and ql > 0.9 and qr > 0.9
          and max(rms[b + 2:] or [0]) < 50)     # (silence after: past the block the end falls in)
    return ok, ("%.2f s of sound, %d gaps; left %d Hz (%.0f %% of its power), right %d Hz (%.0f %%); "
                "wanted %.2f s, %.0f and %.0f Hz%s%s; level %.0f" % (secs, len(gaps), fl, 100 * ql, fr, 100 * qr,
                                                             length, want[0], want[1], note, vol_note, level))


HATARI = subprocess.run(["hatari", "--version"], capture_output=True, text=True).stdout.split()[1].lstrip("v")


def hatari_keeps_parity():
    return tuple(int(x) for x in HATARI.split(".")[:2]) <= (2, 4)


def run(work, case, spec):
    w = os.path.join(work, spec.replace(":", "-") + "-" + case)
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(TOP, "binary", "eplay.prg"), hd)
    shutil.copy(os.path.join(HERE, "build", "LAUNCH.PRG"), hd)
    f = make_file(hd, case)
    tail = "C:\\%s" % f
    if case == "list":
        src = os.path.join(hd, "A.WAV")
        for name, lame in LIST[1:]:
            if lame:
                subprocess.run(["lame", "--quiet", "--noreplaygain"] + lame + [src, os.path.join(hd, name)],
                               check=True)
            else:
                shutil.copy(src, os.path.join(hd, name))
        tail = " ".join("C:\\" + n for n, _ in LIST)
    if case == "many":
        many = os.path.join(hd, "MANY")
        os.makedirs(many)
        for i in range(1000):
            shutil.copy(os.path.join(hd, "A.WAV"), os.path.join(many, "T%04d.WAV" % i))
        tail = "C:\\MANY\\"
    with open(os.path.join(hd, "TAIL.TXT"), "w") as fh:
        fh.write(tail + "\r\n")
    if case in INFS:
        with open(os.path.join(hd, "eplay.inf"), "w", newline="") as fh:
            fh.write(INFS[case])
    m = machines.Machine(spec, hd)
    args = m.args()
    rec = os.path.join(w, "rec.wav")
    with open(os.path.join(m.home, ".config", "hatari", "hatari.cfg"), "w") as fh:
        fh.write("[Sound]\nszYMCaptureFileName = %s\n" % rec)
    fifo, shots = os.path.join(w, "cmd.fifo"), os.path.join(w, "shots")
    os.makedirs(shots)
    log = os.path.join(w, "hatari.log")
    args = ["hatari"] + args + ["--sound", str(REC_RATE), "--fast-boot", "yes", "--natfeats", "yes",
                                "--cmd-fifo", fifo, "--screenshot-dir", shots, "--log-level", "warn",
                                "--statusbar", "no", "--auto", "C:\\LAUNCH.PRG"]
    p = subprocess.Popen(args, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    try:
        if not wait_line(log, lambda l: l.startswith("LAUNCH shel_write"), 120):
            return False, m.describe(), "the launcher never ran"
        cmd(fifo, "hatari-shortcut", "recsound")
        played = wait_line(log, lambda l: l.startswith(("EPLAY playing", "EPLAY refused", "EPLAY no sound")), 60)
        if not played or not played.startswith("EPLAY playing"):
            return False, m.describe(), played or "nothing played"
        time.sleep(1.5)
        cmd(fifo, "hatari-shortcut", "screenshot")
        if case == "fsel":
            cmd(fifo, "hatari-event", "keydown", "0x1d")
            cmd(fifo, "hatari-event", "keypress", "0x18")
            cmd(fifo, "hatari-event", "keyup", "0x1d")
            time.sleep(2)
            cmd(fifo, "hatari-shortcut", "screenshot")
            time.sleep(3)
            cmd(fifo, "hatari-event", "keypress", "0x1c")    # Return: no file named, so nothing
        if case == "many":
            got = wait_line(log, lambda l: l.startswith("LOG the list: "), 240)
            start = wait_line(log, lambda l: l.startswith("LOG free memory"), 1) or ""
            ok = bool(got) and got.split()[3] == "1000"
            return ok, m.describe(), "%s\n     %s" % (start[4:], got[4:] if got else "the list never read")
        if case == "list":
            # the three in turn, then the end: the playing lines in order
            last = wait_line(log, lambda l: l.startswith("EPLAY playing: " + LIST[-1][0]), 60)
            time.sleep(1.5)
            cmd(fifo, "hatari-shortcut", "screenshot")
            end = wait_line(log, lambda l: l.startswith("EPLAY status: Played to the end"), 30) if last else None
            names = [l.split()[2] for l in open(log, errors="replace") if l.startswith("EPLAY playing: ")]
            ok = names == [n for n, _ in LIST] and end is not None
            return ok, m.describe(), "played in turn: %s (wanted %s)%s" % (
                " ".join(names), " ".join(n for n, _ in LIST), "" if end else "; never ended")
        end = wait_line(log, lambda l: l.startswith("EPLAY status: ") and "Playing" not in l and
                        "clock" not in l, 120)
        time.sleep(1.0)
        cmd(fifo, "hatari-shortcut", "recsound")
        time.sleep(0.5)
        if not end:
            return False, m.describe(), played + "; never ended"
        if not os.path.exists(rec):
            return False, m.describe(), "no recording"
        ok, what = analyse(rec, case)
        under = wait_line(log, lambda l: l.startswith("EPLAY ended"), 5) or "EPLAY ended: ? underruns"
        ok = ok and " 0 underruns" in under
        if "XING" in (CASES[case][2] or []):
            # the length EPlay worked out from the header frame
            got = wait_line(log, lambda l: l.startswith("LOG MP3 at") and "(VBR)" in l, 1) or ""
            ms = int(got.split("; ")[-1].split()[0]) if got else 0
            want = CASES[case][5] * 1000
            ok = ok and abs(ms - want) < want / 100
            under += "\n     the length from the Xing frame: %d ms (wanted %d)" % (ms, want)
        return ok, m.describe(), (played[len("EPLAY playing: "):] + "\n     " + end + "\n     " + what +
                                  "\n     " + under)
    finally:
        p.kill()
        p.wait()


def main():
    work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "/tmp/eplay-play")
    cases = [a for a in sys.argv[2:] if a in CASES] or [c for c in CASES if c not in NOT_DEFAULT]
    specs = [a for a in sys.argv[2:] if a not in CASES] or ["Falcon", "Falcon-4"]
    failed = 0
    for spec in specs:
        for case in cases:
            ok, what, found = run(work, case, spec)
            print("%s %s on %s: %s" % ("ok  " if ok else "FAIL", case, what, found), flush=True)
            failed += not ok
    print("all passed" if not failed else "%d failed" % failed)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
