#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""stream_test.py - EPlay playing a station (phase 7) in Hatari, through
common's fakesting (FAKESTNG.PRG and serial_bridge.py: the Atari's TCP
connections become this computer's). A station of its own here, on
127.0.0.1: Icecast's way (ICY 200 OK, icy-metaint, StreamTitle), an MP3
tone sent at its own pace. EPlay alone, nothing else of the family
(guidelines 1.2: the stand-in is this repo's).

  python3 tests/hatari/stream_test.py WORK [CASE...] [SPEC]

Cases:
  live      a 128 kbit/s station: the tone heard, no gap once it plays, the
            titles the station sends shown (EPLAY title)
  drop      the station closes the connection after 6 s: EPlay connects
            again and the music goes on
  pls       a .PLS station list behind a redirect (302): its first station
            plays
  aac       a station sending AAC: refused, in words that name the Gateway
  url       File > Open URL... (Shift+Control+O) with nothing given: the
            address typed and Return: it plays, the row and the bar show
            the station, EPLAY.INF keeps it as the first of the last
            stations, the Options menu ticks Direct connection
"""
import math
import os
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(TOP, "common", "tools"))
sys.path.insert(0, HERE)
import machines  # noqa: E402
from play_test import cmd, wait_line, tone, REC_RATE  # noqa: E402

CASES = ["live", "drop", "pls", "aac", "url"]
METAINT = 8192
KBPS = 128


class Station(threading.Thread):
    """the station: one answer per connection, as the case wants"""

    def __init__(self, mp3, case):
        super().__init__(daemon=True)
        self.mp3, self.case = mp3, case
        self.sock = socket.socket()
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(4)
        self.port = self.sock.getsockname()[1]
        self.conns = 0
        self.log = []

    def run(self):
        while True:
            try:
                c, _ = self.sock.accept()
            except OSError:
                return
            self.conns += 1
            threading.Thread(target=self.serve, args=(c, self.conns), daemon=True).start()

    def serve(self, c, n):
        try:
            req = b""
            while b"\r\n\r\n" not in req and len(req) < 4096:
                d = c.recv(1024)
                if not d:
                    return
                req += d
            line = req.split(b"\r\n")[0].decode(errors="replace")
            self.log.append(line)
            path = line.split()[1] if len(line.split()) > 1 else "/"
            if path == "/go":
                c.sendall(b"HTTP/1.1 302 Found\r\nLocation: /list.pls\r\nContent-Length: 0\r\n\r\n")
                return
            if path == "/list.pls":
                body = ("[playlist]\r\nNumberOfEntries=1\r\nFile1=http://127.0.0.1:%d/live\r\n"
                        "Title1=The rig's station\r\nVersion=2\r\n" % self.port).encode()
                c.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: audio/x-scpls\r\n\r\n" + body)
                return
            if self.case == "aac":
                c.sendall(b"ICY 200 OK\r\nicy-name: An AAC station\r\nContent-Type: audio/aacp\r\n\r\n" +
                          b"\xff\xf1" * 512)
                time.sleep(1)
                return
            want_meta = b"icy-metadata: 1" in req.lower()
            c.sendall(("ICY 200 OK\r\nicy-name: The rig's station\r\nicy-br: %d\r\n"
                       "Content-Type: audio/mpeg\r\n%s\r\n" % (KBPS, "icy-metaint: %d\r\n" % METAINT
                                                                 if want_meta else "")).encode())
            self.audio(c, n, want_meta)
        except OSError:
            pass
        finally:
            c.close()

    def audio(self, c, n, meta):
        """the MP3 again and again at its pace (a burst of 2 s first, as
        Icecast sends), a title every 4 s, the metadata every METAINT"""
        rate = KBPS * 1000 // 8
        sent, t0, upto, title_at = 0, time.time(), METAINT, 0
        pos = 0
        while True:
            if self.case == "drop" and n == 1 and time.time() - t0 > 6:
                return                  # (the first connection ends)
            due = rate * 2 + int((time.time() - t0) * rate)
            if sent >= due:
                time.sleep(0.02)
                continue
            k = min(1024, due - sent)
            out = b""
            while k > 0:
                take = min(k, upto, len(self.mp3) - pos)
                out += self.mp3[pos:pos + take]
                pos = (pos + take) % len(self.mp3)
                k -= take
                sent += take
                upto -= take
                if upto == 0 and meta:
                    secs = int(time.time() - t0)
                    if secs >= title_at:
                        t = ("StreamTitle='The rig - Tone %d';" % (secs // 4 + 1)).encode()
                        t += b"\0" * (-len(t) % 16)
                        out += bytes([len(t) // 16]) + t
                        title_at = secs + 4
                    else:
                        out += b"\0"
                    upto = METAINT
                elif upto == 0:
                    upto = METAINT
            c.sendall(out)

    def stop(self):
        self.sock.close()


def analyse(rec, skip_s, secs):
    """the tone, and the 20 ms blocks without sound once it began"""
    with wave.open(rec) as w:
        rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        raw = w.readframes(n)
    s = struct.unpack("<%dh" % (len(raw) // 2), raw)
    left, right = s[0::ch], s[1::ch] if ch > 1 else s[0::ch]
    blk = rate // 50
    rms = [math.sqrt(sum(v * v for v in left[i:i + blk]) / blk) for i in range(0, len(left) - blk, blk)]
    loud = [i for i, r in enumerate(rms) if r > 1000]
    if not loud:
        return False, "no sound recorded", 0
    # (the music's start: 0.2 s of sound running, not a key's click)
    a = next((i for i in loud if all(r > 1000 for r in rms[i:i + 10])), loud[0])
    b = min(len(rms) - 1, a + int(secs * 50))
    gaps = [i for i in range(a, b) if rms[i] < 1000]
    mid = (a + b) // 2 * blk
    seg = slice(mid - 2400, mid + 2400)
    fl, ql = tone([v / 32768 for v in left[seg]], rate)
    fr, qr = tone([v / 32768 for v in right[seg]], rate)
    ok = abs(fl - 1000) < 8 and abs(fr - 1500) < 8 and ql > 0.9 and qr > 0.9
    return ok, "%.1f s of sound, %d gaps; left %d Hz (%.0f %%), right %d Hz (%.0f %%)" % (
        (b - a) / 50, len(gaps), fl, 100 * ql, fr, 100 * qr), len(gaps)


def said(l):
    """a line that answers: playing, or a status that isn't the start's
    own notes (the external clock)"""
    return l.startswith(("EPLAY playing", "EPLAY refused")) or (l.startswith("EPLAY status") and
                                                              "external clock" not in l and "Connecting" not in l)


def run(work, case, spec):
    w = os.path.join(work, spec.replace(":", "-") + "-" + case)
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(TOP, "binary", "eplay.prg"), os.path.join(hd, "eplay.prg"))
    shutil.copy(os.path.join(HERE, "build", "LAUNCH.PRG"), hd)
    wav, mp3 = os.path.join(w, "tone.wav"), os.path.join(w, "tone.mp3")
    subprocess.run(["sox", "-n", "-r", "44100", "-c", "2", "-b", "16", wav, "synth", "8", "sine", "1000",
                    "sine", "1500", "remix", "1", "2", "gain", "-6"], check=True)
    subprocess.run(["lame", "--quiet", "--noreplaygain", "-b", str(KBPS), "--cbr", wav, mp3], check=True)
    st = Station(open(mp3, "rb").read(), case)
    st.start()
    url = "http://127.0.0.1:%d/%s" % (st.port, "go" if case == "pls" else "live")
    with open(os.path.join(hd, "TAIL.TXT"), "w") as fh:
        fh.write(("" if case == "url" else url) + "\r\n")
    m = machines.Machine(spec, hd)
    net = m.network(40000)
    margs = m.args()                    # (makes Hatari's home and its config folder)
    rec = os.path.join(w, "rec.wav")
    with open(os.path.join(m.home, ".config", "hatari", "hatari.cfg"), "w") as fh:
        fh.write("[Sound]\nszYMCaptureFileName = %s\n" % rec)
    fifo, log = os.path.join(w, "cmd.fifo"), os.path.join(w, "hatari.log")
    shots = os.path.join(w, "shots")
    os.makedirs(shots)
    args = ["hatari"] + margs + net + ["--sound", str(REC_RATE), "--fast-boot", "yes", "--natfeats", "yes",
                                          "--cmd-fifo", fifo, "--screenshot-dir", shots, "--log-level", "warn",
                                          "--statusbar", "no", "--auto", "C:\\LAUNCH.PRG"]
    env = m.env()
    x = None
    if case == "url":
        # (a screen to type into: xdotool, as common's speed rig)
        disp = ":%d" % (60 + os.getpid() % 30)
        x = machines.xvfb(disp)
        env = m.env(dict(os.environ, DISPLAY=disp, SDL_VIDEODRIVER="x11"))
    p = subprocess.Popen(args, env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)
    try:
        if not wait_line(log, lambda l: l.startswith("LAUNCH shel_write"), 120):
            return False, m.describe(), "the launcher never ran"
        if case == "aac":
            got = wait_line(log, said, 90)
            return bool(got) and "AAC" in got and "Gateway" in got, m.describe(), got or "nothing said"
        if case == "url":
            def xdo(*a):
                return subprocess.run(["xdotool", *a], env=env, capture_output=True, text=True).stdout
            wait_line(log, lambda l: l.startswith("EPLAY window"), 60)
            time.sleep(4)
            win = ""
            for _ in range(50):
                win = (xdo("search", "--name", "Hatari") or xdo("search", "--class", "hatari")).split()
                if win:
                    break
                time.sleep(0.2)
            xdo("windowfocus", win[0])
            xdo("keydown", "shift")
            xdo("keydown", "ctrl")
            time.sleep(0.3)
            xdo("key", "--delay", "120", "o")
            time.sleep(0.3)
            xdo("keyup", "ctrl")
            xdo("keyup", "shift")
            time.sleep(2)
            cmd(fifo, "hatari-shortcut", "screenshot")
            time.sleep(1)
            xdo("type", "--delay", "150", url)
            time.sleep(1)
            cmd(fifo, "hatari-shortcut", "screenshot")
            time.sleep(1)
            xdo("key", "Return")
        cmd(fifo, "hatari-shortcut", "recsound")
        played = wait_line(log, said, 90)
        if not played or not played.startswith("EPLAY playing"):
            return False, m.describe(), played or "nothing played"
        time.sleep(14 if case == "drop" else 10)
        cmd(fifo, "hatari-shortcut", "screenshot")
        cmd(fifo, "hatari-shortcut", "recsound")
        time.sleep(1)
        text = open(log, errors="replace").read()
        titles = sorted(set(l[len("EPLAY title: "):] for l in text.split("\n") if l.startswith("EPLAY title: ")))
        ok, what, gaps = analyse(rec, 0, 8)
        note = "%s; titles %s; %d connections" % (what, titles, st.conns)
        if case == "live":
            ok = ok and gaps == 0 and len(titles) >= 2
        elif case == "drop":
            ok = ok and st.conns >= 2 and gaps <= 25   # (a reconnect may cost up to 0.5 s)
        elif case == "pls":
            ok = ok and gaps == 0 and any("/live" in l for l in st.log)
        elif case == "url":
            xdo("key", "--delay", "120", "ctrl+q")     # (EPLAY.INF is written as EPlay quits)
            time.sleep(4)
            inf = open(os.path.join(hd, "eplay.inf"), errors="replace").read() if os.path.exists(
                os.path.join(hd, "eplay.inf")) else ""
            kept = ("URL1=" + url) in inf
            note += "; EPLAY.INF %s it" % ("keeps" if kept else "doesn't keep")
            ok = ok and gaps == 0 and kept
        return ok, m.describe(), note
    finally:
        cmd(fifo, "hatari-shortcut", "quit")
        try:
            p.wait(10)
        except subprocess.TimeoutExpired:
            p.kill()
        m.stop()
        st.stop()
        if x:
            x.terminate()


def main():
    work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "/tmp/eplay-stream")
    cases = [a for a in sys.argv[2:] if a in CASES] or CASES
    specs = [a for a in sys.argv[2:] if a not in CASES] or ["Falcon-4"]
    failed = 0
    for spec in specs:
        for case in cases:
            ok, desc, note = run(work, case, spec)
            print("%-4s %s on %s: %s" % ("ok" if ok else "FAIL", case, desc, note))
            sys.stdout.flush()
            failed += not ok
    print("all passed" if not failed else "%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
