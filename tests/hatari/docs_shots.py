#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""docs_shots.py - the screenshots in docs/ and the README, taken in Hatari
on the Falcon (256 colours, VGA): EPlay playing a small album of its own
(tones made here, with tags and a cover drawn here: nobody's real music
or art), its Settings, and the mini player.

  python3 tests/hatari/docs_shots.py WORK [--update]

WORK holds the runs; --update copies the pictures to docs/ (player.png,
settings.png, mini.png). make in tests/hatari first (LAUNCH.PRG), and make.
"""
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(TOP, "common", "tools"))
import machines  # noqa: E402

SPEC = "Falcon:256"
ALBUM = "Signals"
ARTIST = "Test Card"
TRACKS = [("Carrier Wave", 440, 554), ("Night Shift", 392, 494), ("Long Wave", 330, 415),
          ("Harmonics", 523, 659), ("Last Light", 294, 370)]


def cover(path):
    """a 300 x 300 cover drawn here: bands of colour and three rings"""
    from PIL import Image, ImageDraw
    im = Image.new("RGB", (300, 300))
    d = ImageDraw.Draw(im)
    for y in range(300):
        t = y / 299
        d.line([(0, y), (299, y)], fill=(int(20 + 40 * t), int(30 + 90 * t), int(90 + 120 * t)))
    for r, c in ((120, (240, 170, 60)), (80, (230, 90, 70)), (40, (250, 230, 200))):
        d.ellipse([150 - r, 160 - r, 150 + r, 160 + r], outline=c, width=10)
    im.save(path, "JPEG", quality=85)


def make_album(w, hd):
    jpg = os.path.join(w, "cover.jpg")
    cover(jpg)
    names = []
    for i, (title, f1, f2) in enumerate(TRACKS):
        wav = os.path.join(w, "t%d.wav" % i)
        subprocess.run(["sox", "-n", "-r", "44100", "-c", "2", "-b", "16", wav, "synth", "40", "sine", str(f1),
                        "sine", str(f2), "remix", "1", "2", "gain", "-9"], check=True)
        name = "%02d.MP3" % (i + 1)
        subprocess.run(["lame", "--quiet", "-b", "192", "--tt", title, "--ta", ARTIST, "--tl", ALBUM,
                        "--tn", str(i + 1), "--ti", jpg, wav, os.path.join(hd, name)], check=True)
        names.append("C:\\" + name)
    return " ".join(names)


def wait_line(log, pred, secs):
    end = time.time() + secs
    while time.time() < end:
        for line in open(log, errors="replace"):
            if pred(line):
                return line.strip()
        time.sleep(0.2)
    return None


def cmd(fifo, *a):
    for _ in range(300):
        if os.path.exists(fifo):
            break
        time.sleep(0.1)
    with open(fifo, "w") as fh:
        fh.write(" ".join(a) + "\n")
    time.sleep(0.05)


def shot(fifo, shots, n_before):
    cmd(fifo, "hatari-shortcut", "screenshot")
    for _ in range(50):
        names = sorted(os.listdir(shots))
        if len(names) > n_before:
            time.sleep(0.5)
            return os.path.join(shots, names[-1])
        time.sleep(0.1)
    raise RuntimeError("no screenshot")


def crop(path, out):
    """the picture without the desk around it: the desk is a green and
    white dither, so a pixel is the desk's when it is green, or white with
    a green one beside it; what is left in pieces of more than 2,000
    pixels (not the mouse pointer) gives the box, below the menu bar"""
    from PIL import Image
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()
    green, white = (0, 255, 0), (255, 255, 255)
    top = 20 * h // 480

    def desk(x, y):
        c = px[x, y]
        if c == green:
            return True
        return c == white and ((x > 0 and px[x - 1, y] == green) or (x + 1 < w and px[x + 1, y] == green))
    mask = [[y >= top and not desk(x, y) for x in range(w)] for y in range(h)]
    seen = [[False] * w for _ in range(h)]
    box = None
    for y0 in range(top, h):
        for x0 in range(w):
            if not mask[y0][x0] or seen[y0][x0]:
                continue
            stack, part = [(x0, y0)], []
            seen[y0][x0] = True
            while stack:
                x, y = stack.pop()
                part.append((x, y))
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if 0 <= nx < w and top <= ny < h and mask[ny][nx] and not seen[ny][nx]:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            if len(part) > 2000:
                xs = [p_[0] for p_ in part]
                ys = [p_[1] for p_ in part]
                b = (min(xs), min(ys), max(xs) + 1, max(ys) + 1)
                box = b if not box else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    im.crop(box).save(out)


def run(work, mini):
    w = os.path.join(work, "mini" if mini else "player")
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(TOP, "binary", "eplay.prg"), hd)
    shutil.copy(os.path.join(HERE, "build", "LAUNCH.PRG"), hd)
    with open(os.path.join(hd, "TAIL.TXT"), "w") as fh:
        fh.write(make_album(w, hd) + "\r\n")
    with open(os.path.join(hd, "eplay.inf"), "w", newline="") as fh:
        fh.write("VOLUME=12\r\n\r\n" + ("MINI=1\r\n\r\n" if mini else "") +
                 "GATE=192.168.1.20\r\n\r\nGATEPORT=8580\r\n\r\n")
    m = machines.Machine(SPEC, hd)
    fifo, shots, log = os.path.join(w, "cmd.fifo"), os.path.join(w, "shots"), os.path.join(w, "hatari.log")
    os.makedirs(shots)
    args = ["hatari"] + m.args() + ["--sound", "48000", "--fast-boot", "yes", "--natfeats", "yes", "--cmd-fifo", fifo,
                                    "--screenshot-dir", shots, "--log-level", "warn", "--statusbar", "no",
                                    "--auto", "C:\\LAUNCH.PRG"]
    p = subprocess.Popen(args, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    out = {}
    try:
        if not wait_line(log, lambda l: l.startswith("EPLAY playing"), 120):
            raise RuntimeError("nothing played: see " + log)
        wait_line(log, lambda l: "album art" in l and "shown" in l, 60)
        time.sleep(8)
        if mini:
            out["mini.png"] = shot(fifo, shots, 0)
        else:
            out["player.png"] = shot(fifo, shots, 0)
            cmd(fifo, "hatari-event", "keydown", "0x1d")    # ^, : Settings
            cmd(fifo, "hatari-event", "keypress", "0x33")
            cmd(fifo, "hatari-event", "keyup", "0x1d")
            time.sleep(3)
            out["settings.png"] = shot(fifo, shots, 1)
            cmd(fifo, "hatari-event", "keypress", "0x61")   # Undo: Cancel
            time.sleep(1)
    finally:
        p.kill()
        p.wait()
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    work = os.path.abspath(sys.argv[1])
    got = {}
    got.update(run(work, False))
    got.update(run(work, True))
    for name, path in got.items():
        dst = os.path.join(work, name)
        crop(path, dst)
        print("%s: %s" % (name, dst))
        if "--update" in sys.argv:
            shutil.copy(dst, os.path.join(TOP, "docs", name))


if __name__ == "__main__":
    main()
