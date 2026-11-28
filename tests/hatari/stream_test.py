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
  stall     (14 Nov 2026) the DSP taken as stalled at 4 s (eplay.inf's
            STALLAT=, the rigs'): EPlay connects again and the music goes on
  startfail (17 Nov 2026: a DFB1X under load, through the Gateway:
            "the DSP stopped answering loading word 0 of 0") the DSP taken
            as not answering as the stream starts (eplay.inf's STALLAT=-1,
            the rigs'): EPlay starts it again, connects again, and the
            music plays
  pls       a .PLS station list behind a redirect (302): its first station
            plays
  rough     a station as real ones are on a bad day (17 Nov 2026:
            "when playing a station both in FDI and DSP, the audio stutters
            and stops"): its address answers with a 302, the station's head
            comes 1.5 s late, then Icecast's 2 s burst, the data in
            uneven pieces (pauses of up to 0.6 s, made up after), and a
            1.5 s stall 8 s in: 16 s of the tone with no gap (the tone 40 s
            long, so no loop of the rig's MP3 falls inside)
  chunked   the station answers HTTP/1.1 with Transfer-Encoding: chunked (as
            Swiss Radio's CDN does): the chunks taken out, the tone and its
            titles, no gap
  cover     the station names a picture with its titles (StreamUrl, as
            Radio Paradise's covers; 500 x 500, a photo's grain: some 70 KB): EPlay fetches it on a Direct
            connection and the 68030 unpacks it: the art square shows it
            (red on its left, blue on its right), the music without a gap
  gwcover   as cover, Through the Gateway (radio/radiogate.py here): the
            Gateway passes the StreamUrl on, EPlay asks its /art, which
            gives a 64 x 64 JPEG
  logo      a Direct station that names no picture, a Gateway given in
            Settings (EPLAY.INF: FALCON=1, GATE, GATEPORT): EPlay asks its
            /logo 8 s in, which finds the station in radio-browser.info (a
            stand-in here): the logo (green) in the art square, nothing said
  aac       a station sending AAC: refused, in words that name the Gateway
  gateway   Through the Gateway (EPLAY.INF: FALCON=0) to radio/radiogate.py
            on this computer, an AAC station converted to MP3: the tone,
            no gap, its titles
  https     the address is http, and the station sends EPlay to its https
            one (a 302, as many do): a Direct connection through TLS.OVL,
            the rig's own root certificate as CACERT.PEM beside EPlay; the
            tone, no gap, its titles
  urledit   as url, then inside the typed address: Left four times, "Z",
            Right twice, "Y" (common's field.c, an arrow one step: hub
            d5786b7): EPLAY.INF's URLTYPED ends ".../ZliYve"
  nginx     laut.fm's way (the owner's station, 8 Nov 2026): an nginx at https
            (AES-128-GCM, P-256 only) answers with a 302 to the station, and
            hangs up on a client that hasn't asked within 5 s
            (client_header_timeout): the handshake's curves on the DSP keep
            EPlay inside it. SKIPPED where there is no nginx
  url       File > Open URL... (Shift+Control+O) with nothing given: the
            address typed and Return: it plays, the row and the bar show
            the station, EPLAY.INF keeps it as the first of the last
            stations, the Options menu ticks Direct connection
  spotify   a Spotify playlist line (spotify:playlist:...) on a Direct
            connection, a Gateway given in Settings: EPlay asks the
            Gateway's Spotify service (radio/spotgate.py here, its player a
            stand-in of radio/test_spotgate.py's: two 8 s tracks of the rig's
            tone)
            for it: the first track's title, length and place shown when it
            is heard (EPLAY track), then the second's, the tone with no gap,
            and the line's end at the second's end, no connection again
            (decided 27 Nov 2026)
  findrefused  File > Find station... through a Gateway whose search refuses
            (400, its words): EPlay's note gives its words, "The Gateway
            refused the search: ...", not "doesn't answer" (a fault
            reported 27 Nov 2026)

RADIOGATE=DIR takes radiogate.py, spotgate.py, test_spotgate.py's stand-in
player (and aeragate.py) from DIR in place of radio/: the Gateway's own
copy, as its release will carry it.
"""
import math
import os
import shutil
import socket
import ssl
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
from natfeats import natfeats  # noqa: E402
from play_test import cmd, wait_line, tone, REC_RATE  # noqa: E402

CASES = ["live", "drop", "stall", "startfail", "pls", "rough", "chunked", "cover", "gwcover", "logo", "aac", "gateway", "https", "nginx", "url", "urledit",
         "find", "findrefused", "m3u", "spotify"]
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
        self.cover = b""
        self.logo = b""
        self.log = []

        self.tls = None                 # (the https case: a second port, TLS)
        # (what is sent at a time: 1 KB; laut.fm's burst comes in whole 16 KB
        # TLS records, HTTPS_PIECE=65536)
        self.piece = int(os.environ.get("HTTPS_PIECE", "1024"))

    def run(self):
        if self.tls:
            threading.Thread(target=self.accept, args=(self.tls[0], self.tls[1]), daemon=True).start()
        self.accept(self.sock, None)

    def accept(self, sock, ctx):
        while True:
            try:
                c, _ = sock.accept()
            except OSError:
                return
            if ctx:
                try:
                    c.settimeout(60)
                    c = ctx.wrap_socket(c, server_side=True)
                    c.settimeout(None)
                    self.log.append("TLS " + c.version() + " " + c.cipher()[0])
                except (OSError, ssl.SSLError) as e:
                    self.log.append("TLS failed: %s" % e)
                    c.close()
                    continue
            self.conns += 1
            threading.Thread(target=self.serve, args=(c, self.conns, ctx is not None), daemon=True).start()

    def serve(self, c, n, secure=False):
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
            if self.case in ("https", "nginx") and not secure:
                c.sendall(("HTTP/1.1 302 Found\r\nLocation: https://radio.test:%d/live\r\n"
                           "Content-Length: 0\r\n\r\n" % self.tls[0].getsockname()[1]).encode())
                return
            if path.startswith("/json/stations/"):
                # (radio-browser.info's stand-in: Find station's search, this station
                # and one more; else this station by its address)
                if path.startswith("/json/stations/search?") and "hidebroken" in path:
                    self.log.append("search asked: " + path)
                    body = ('[{"name":"The rig\'s station","url_resolved":"http://127.0.0.1:%d/live",'
                            '"countrycode":"DE","codec":"MP3","bitrate":128},'
                            '{"name":"Another station","url_resolved":"http://127.0.0.1:%d/live",'
                            '"countrycode":"NL","codec":"AAC","bitrate":64}]' % (self.port, self.port))
                    c.sendall(("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n" + body).encode())
                    return
                body = ('[{"name": "The rig\'s station", "favicon": "http://127.0.0.1:%d/logo.png"}]' % self.port
                        if "byurl" in path else "[]").encode()
                c.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n" + body)
                return
            if path == "/logo.png":
                self.log.append("logo fetched")
                c.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: image/png\r\n\r\n" + self.logo)
                return
            if path == "/cover.jpg":
                self.log.append("cover fetched")
                c.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n"
                          % len(self.cover) + self.cover)
                return
            if self.case == "rough" and path == "/live":
                c.sendall(b"HTTP/1.1 302 Found\r\nLocation: /onair\r\nContent-Length: 0\r\n\r\n")
                return
            if self.case == "rough":
                time.sleep(1.5)         # (the station's head, late)
            if path == "/go":
                c.sendall(b"HTTP/1.1 302 Found\r\nLocation: /list.pls\r\nContent-Length: 0\r\n\r\n")
                return
            if path == "/list.pls":
                body = ("[playlist]\r\nNumberOfEntries=1\r\nFile1=http://127.0.0.1:%d/live\r\n"
                        "Title1=The rig's station\r\nVersion=2\r\n" % self.port).encode()
                c.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: audio/x-scpls\r\n\r\n" + body)
                return
            if self.case == "gateway":
                want_meta = b"icy-metadata: 1" in req.lower()
                c.sendall(("ICY 200 OK\r\nicy-name: The rig's AAC station\r\nContent-Type: audio/aacp\r\n%s\r\n" %
                           ("icy-metaint: %d\r\n" % METAINT if want_meta else "")).encode())
                self.audio(c, n, want_meta)
                return
            if self.case == "aac":
                c.sendall(b"ICY 200 OK\r\nicy-name: An AAC station\r\nContent-Type: audio/aacp\r\n\r\n" +
                          b"\xff\xf1" * 512)
                time.sleep(1)
                return
            want_meta = b"icy-metadata: 1" in req.lower()
            c.sendall(("%s\r\nicy-name: The rig's station\r\nicy-br: %d\r\n"
                       "Content-Type: audio/mpeg\r\n%s%s\r\n" % (
                           "HTTP/1.1 200 OK" if self.case == "chunked" else "ICY 200 OK", KBPS,
                           "icy-metaint: %d\r\n" % METAINT if want_meta else "",
                           "Transfer-Encoding: chunked\r\n" if self.case == "chunked" else "")).encode())
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
        burst = 15 if self.piece > 1024 else 2      # (laut.fm's: 15 s at once)
        pos = 0
        rnd, hold_until = 12345, 0.0
        while True:
            if self.case == "drop" and n == 1 and time.time() - t0 > 6:
                return                  # (the first connection ends)
            if self.case == "rough":
                # (uneven: now and then nothing for up to 0.6 s, made up
                # after; 8 s in, 1.5 s of nothing)
                now = time.time() - t0
                if now < hold_until or 8 <= now < 9.5:
                    time.sleep(0.02)
                    continue
                rnd = (rnd * 1103515245 + 12345) & 0x7fffffff
                if rnd % 40 == 0:
                    hold_until = now + 0.2 + (rnd >> 8) % 400 / 1000.0
            due = rate * burst + int((time.time() - t0) * rate)
            if sent >= due:
                time.sleep(0.02)
                continue
            k = min(self.piece, due - sent)
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
                        if self.cover:
                            t += ("StreamUrl='http://127.0.0.1:%d/cover.jpg';" % self.port).encode()
                        t += b"\0" * (-len(t) % 16)
                        out += bytes([len(t) // 16]) + t
                        title_at = secs + 4
                    else:
                        out += b"\0"
                    upto = METAINT
                elif upto == 0:
                    upto = METAINT
            if self.case == "chunked":
                out = b"%x;rig=1\r\n" % len(out) + out + b"\r\n"
            c.sendall(out)

    def stop(self):
        self.sock.close()
        if self.tls:
            self.tls[0].close()


class Dns(threading.Thread):
    """the rig's name server (fakesting's FAKESTING_DNS): every A query is
    127.0.0.1, so the https station has a name for its certificate"""

    def __init__(self):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.port = self.sock.getsockname()[1]

    def run(self):
        while True:
            try:
                q, a = self.sock.recvfrom(512)
            except OSError:
                return
            end = 12
            while end < len(q) and q[end]:
                end += 1 + q[end]
            question = q[12:end + 5]
            self.sock.sendto(q[:2] + b"\x81\x80" + q[4:6] + b"\x00\x01\x00\x00\x00\x00" + question +
                             b"\xc0\x0c\x00\x01\x00\x01\x00\x00\x00\x3c\x00\x04" +
                             socket.inet_aton("127.0.0.1"), a)


def nginx(w, cert, key, to):
    """an nginx of the rig's own on 127.0.0.1, TLS 1.2, answering every
    path with a 302 to `to`: (the process, its port)"""
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    d = os.path.join(w, "nginx")
    os.makedirs(os.path.join(d, "logs"), exist_ok=True)
    conf = os.path.join(d, "nginx.conf")
    with open(conf, "w") as fh:
        fh.write("""daemon off; master_process off; pid %(d)s/nginx.pid; error_log %(d)s/logs/error.log info;
events {}
http { access_log %(d)s/logs/access.log;
  client_body_temp_path %(d)s; proxy_temp_path %(d)s; fastcgi_temp_path %(d)s;
  uwsgi_temp_path %(d)s; scgi_temp_path %(d)s;
  server { listen 127.0.0.1:%(port)d ssl; server_name radio.test;
    ssl_certificate %(cert)s; ssl_certificate_key %(key)s;
    ssl_protocols TLSv1.2 TLSv1.3;%(extra)s
    location / { return 302 %(to)s; } } }
""" % dict(d=d, port=port, cert=cert, key=key, to=to, extra=os.environ.get("HTTPS_NGINX_TLS", "")))
    p = subprocess.Popen(["nginx", "-c", conf, "-p", d], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    time.sleep(0.5)
    return p, port


def make_certs(w):
    """a root certificate of the rig's own, and radio.test's from it (RSA)"""
    def ossl(*a):
        subprocess.run(["openssl", *a], check=True, capture_output=True)
    ca, key = os.path.join(w, "ca.pem"), os.path.join(w, "ca.key")
    ossl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", ca, "-days", "30",
         "-subj", "/CN=EPlay rig root")
    srv, skey, csr = os.path.join(w, "srv.pem"), os.path.join(w, "srv.key"), os.path.join(w, "srv.csr")
    ext = os.path.join(w, "srv.ext")
    with open(ext, "w") as fh:
        fh.write("subjectAltName=DNS:radio.test\nbasicConstraints=CA:FALSE\n")
    ossl("req", "-newkey", "rsa:2048", "-nodes", "-keyout", skey, "-out", csr, "-subj", "/CN=radio.test")
    ossl("x509", "-req", "-in", csr, "-CA", ca, "-CAkey", key, "-CAcreateserial", "-out", srv, "-days", "30",
         "-extfile", ext)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(srv, skey)
    if os.environ.get("HTTPS_CIPHERS"):
        ctx.maximum_version = ssl.TLSVersion.TLSv1_2
        ctx.set_ciphers(os.environ["HTTPS_CIPHERS"])
    if os.environ.get("HTTPS_CURVE"):
        ctx.set_ecdh_curve(os.environ["HTTPS_CURVE"])
    return ca, ctx


# (a check given up, with why: counted and printed as play_test.py's)
SKIPS = []


def hatari():
    return subprocess.run(["hatari", "--version"], capture_output=True, text=True).stdout.split()[1].lstrip("v")


def resumed(rec):
    """the music's return after it stopped (0.2 s without sound, past its
    start): the first 0.6 s of sound running, its tone 0.3 s in (None: none
    recorded)"""
    with wave.open(rec) as w:
        rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        raw = w.readframes(n)
    s = struct.unpack("<%dh" % (len(raw) // 2), raw)
    left, right = s[0::ch], s[1::ch] if ch > 1 else s[0::ch]
    blk = rate // 50
    rms = [math.sqrt(sum(v * v for v in left[i:i + blk]) / blk) for i in range(0, len(left) - blk, blk)]
    loud = [r > 1000 for r in rms]
    i = next((k for k in range(len(rms)) if all(loud[k:k + 10])), None)
    if i is None:
        return None
    stop = next((k for k in range(i, len(rms)) if not any(loud[k:k + 10])), None)
    if stop is None:
        return None
    back = next((k for k in range(stop, len(rms) - 30) if all(loud[k:k + 30])), None)
    if back is None:
        return None
    c = (back + 15) * blk
    fl, ql = tone([v / 32768 for v in left[c - 2400:c + 2400]], rate)
    fr, qr = tone([v / 32768 for v in right[c - 2400:c + 2400]], rate)
    return (abs(fl - 1000) < 8 and abs(fr - 1500) < 8 and ql > 0.9 and qr > 0.9,
            "back after %.1f s: left %d Hz (%.0f %%), right %d Hz (%.0f %%)" %
            ((back - stop) / 50, fl, 100 * ql, fr, 100 * qr))


def hatari_keeps_parity():
    return tuple(int(x) for x in hatari().split(".")[:2]) <= (2, 4)


def analyse(rec, skip_s, secs, at=None):
    """the tone, and the 20 ms blocks without sound once it began; the
    tone measured in the middle, or at s from the music's start"""
    with wave.open(rec) as w:
        rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        raw = w.readframes(n)
    s = struct.unpack("<%dh" % (len(raw) // 2), raw)
    left, right = s[0::ch], s[1::ch] if ch > 1 else s[0::ch]
    blk = rate // 50
    rms = [math.sqrt(sum(v * v for v in left[i:i + blk]) / blk) for i in range(0, len(left) - blk, blk)]
    loud = [i for i, r in enumerate(rms) if r > 1000]
    if not loud:
        # (MP3 on the CPU, EPLAY_RIG_INF's MP3CPU=1: its samples through
        # the DMA's four tracks, the music in the first. Hatari 2.4 keeps
        # the DMA's word count from its last stop (EPlay's clock
        # measurement), so its DAC may hear another track: the left
        # silent, the tone on the right. play_test.py excuses it the same
        # way; skipped, counted, never "passed")
        rr = [math.sqrt(sum(v * v for v in right[i:i + blk]) / blk) for i in range(0, len(right) - blk, blk)]
        if (("MP3CPU=1" in os.environ.get("EPLAY_RIG_INF", "") or os.environ.get("EPLAY_RIG_CPUMP3") == "1") and
                hatari_keeps_parity() and
                any(r > 1000 for r in rr)):
            SKIPS.append("the sound itself (Hatari %s's DMA started mid-frame, its DAC on another track)" % hatari())
            return True, "SKIPPED: the sound itself (Hatari %s's DMA started mid-frame: the left silent, " \
                "the right a tone)" % hatari(), 0
        return False, "no sound recorded", 0
    # (the music's start: 0.2 s of sound running, not a key's click)
    a = next((i for i in loud if all(r > 1000 for r in rms[i:i + 10])), loud[0])
    b = min(len(rms) - 1, a + int(secs * 50))
    gaps = [i for i in range(a, b) if rms[i] < 1000]
    mid = (a + b) // 2 * blk if at is None else (a + int(at * 50)) * blk
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
    # (EPLAY_RIG_BIN: another build's binary/, a release's, to bisect a
    # station's fault across releases with this rig's cases)
    bin_dir = os.environ.get("EPLAY_RIG_BIN") or os.path.join(TOP, "binary")
    shutil.copy(os.path.join(bin_dir, "eplay.prg"), os.path.join(hd, "eplay.prg"))
    # (MP3 on the CPU, a 68060's: the overlays beside it, as a release has them)
    for ovl in ("mp3fpu.ovl", "mp3int.ovl"):
        if os.path.exists(os.path.join(bin_dir, ovl)):
            shutil.copy(os.path.join(bin_dir, ovl), hd)
    shutil.copy(os.path.join(HERE, "build", "LAUNCH.PRG"), hd)
    wav, mp3 = os.path.join(w, "tone.wav"), os.path.join(w, "tone.mp3")
    # (40 s: the MP3 is sent again and again, and LAME's delay and padding
    # are some 50 ms of near silence at each loop: a gap of the station's
    # own, inside the rough case's 16 s when the tone was 8 s long)
    subprocess.run(["sox", "-n", "-r", "44100", "-c", "2", "-b", "16", wav, "synth", "40", "sine", "1000",
                    "sine", "1500", "remix", "1", "2", "gain", "-6"], check=True)
    subprocess.run(["lame", "--quiet", "--noreplaygain", "-b", str(KBPS), "--cbr", wav, mp3], check=True)
    if case == "gateway":
        aac = os.path.join(w, "tone.aac")
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", wav, "-c:a", "aac", "-b:a",
                        "%dk" % KBPS, "-f", "adts", aac], check=True)
        mp3 = aac                       # (the station sends AAC at the same pace)
    if os.environ.get("STATION_FILE"):
        mp3 = os.environ["STATION_FILE"]    # (a real station's MP3, captured: its times only)
    st = Station(open(mp3, "rb").read(), case)
    if case in ("cover", "gwcover"):
        jpg = os.path.join(w, "cover.jpg")
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                        "color=red:s=250x500[a];color=blue:s=250x500[b];[a][b]hstack,noise=alls=30:allf=u",
                        "-frames:v", "1", "-q:v", "3", jpg], check=True)
        st.cover = open(os.environ.get("COVER_FILE") or jpg, "rb").read()   # (COVER_FILE: a real one, times only)
    if case == "nginx" and not shutil.which("nginx"):
        return True, "-", "SKIPPED (no nginx here)"
    if case in ("https", "nginx"):
        ca, ctx = make_certs(w)
        shutil.copy(ca, os.path.join(hd, "CACERT.PEM"))
        shutil.copy(os.path.join(TOP, "common", "tlsovl", "tls.ovl"), os.path.join(hd, "TLS.OVL"))
        ts = socket.socket()
        ts.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        ts.bind(("127.0.0.1", 0))
        ts.listen(4)
        st.tls = (ts, ctx)
        dns = Dns()
        dns.start()
        os.environ["FAKESTING_DNS"] = "127.0.0.1:%d" % dns.port
    gate = None
    if case == "logo":
        png = os.path.join(w, "logo.png")
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "color=0x00c000:s=128x128",
                        "-frames:v", "1", png], check=True)
        st.logo = open(png, "rb").read()
    if case in ("gateway", "gwcover", "logo", "find", "findrefused"):
        # the Gateway's radio service, EPlay's own (radio/), on this computer
        sys.path.insert(0, os.environ.get("RADIOGATE") or os.path.join(TOP, "radio"))
        import radiogate
        radiogate.Config.allow_local = True     # (the station is on 127.0.0.1)
        gate = radiogate.ThreadingHTTPServer(("127.0.0.1", 0), radiogate.Handler)
        gate.daemon_threads = True
        threading.Thread(target=gate.serve_forever, daemon=True).start()
        with open(os.path.join(hd, "eplay.inf"), "w", newline="") as fh:
            fh.write("FALCON=%d\r\nGATE=127.0.0.1\r\nGATEPORT=%d\r\n\r\n" % (case == "logo", gate.server_address[1]))
        if case in ("logo", "find", "findrefused"):
            radiogate.Config.rb_api = ["http://127.0.0.1:%d" % st.port]
        # (findrefused: a Gateway there that refuses the search, as one that
        # wanted a station's name did: a fault reported 27 Nov 2026. EPlay
        # says what it answered, not that it doesn't answer; the other cases
        # the real search)
        if not hasattr(radiogate, "search0"):
            radiogate.search0 = radiogate.Handler.search
        radiogate.Handler.search = (lambda self, q: self.reply(400, "a station's name wanted")) \
            if case == "findrefused" else radiogate.search0
    spot = None
    if case == "spotify":
        # the Gateway's Spotify service, EPlay's own (radio/), its player a
        # stand-in: two 8 s tracks
        sys.path.insert(0, os.environ.get("RADIOGATE") or os.path.join(TOP, "radio"))
        import test_spotgate
        import spotgate
        test_spotgate.TRACK_S = 8.0
        test_spotgate.TONES = (1000, 1500)     # (the rig's tone, analyse's)
        spot = test_spotgate.Player()
        spotgate.Config.api = "http://127.0.0.1:%d" % spot.port
        spotgate.Config.fifo = spot.fifo
        spotgate.Config.poll = 0.2
        spotgate.Config.start_s = 1
        gate = spotgate.ThreadingHTTPServer(("127.0.0.1", 0), spotgate.Handler)
        gate.daemon_threads = True
        threading.Thread(target=gate.serve_forever, daemon=True).start()
        with open(os.path.join(hd, "eplay.inf"), "w", newline="") as fh:
            fh.write("FALCON=1\r\nGATE=127.0.0.1\r\nSPOTPORT=%d\r\n\r\n" % gate.server_address[1])
    st.start()
    url = "http://127.0.0.1:%d/%s" % (st.port, "go" if case == "pls" else "live")
    if case == "spotify":
        url = test_spotgate.PL
    ngx = None
    if case == "nginx" or (case == "https" and os.environ.get("HTTPS_NGINX")):
        if case == "nginx":
            os.environ["HTTPS_NGINX_TLS"] = (" ssl_ciphers ECDHE-RSA-AES128-GCM-SHA256;"
                                             " ssl_ecdh_curve prime256v1; client_header_timeout 5s;")
        # (laut.fm's way: an nginx at https sends EPlay on with a 302)
        ngx, nport = nginx(w, os.path.join(w, "srv.pem"), os.path.join(w, "srv.key"),
                           "https://radio.test:%d/live" % st.tls[0].getsockname()[1])
        url = "https://radio.test:%d/musickoeln" % nport
    if case == "m3u":
        # (9 Nov 2026: Open... of an .M3U holding a station added
        # nothing) the playlist given, the station in it, named by #EXTINF
        with open(os.path.join(hd, "RADIO.M3U"), "w", newline="") as fh:
            fh.write("#EXTM3U\r\n#EXTINF:-1,The rig radio\r\n%s\r\n" % url)
    # (the window at 1.15's first place: the picture cases look for the
    # art there, and 1.16 opens a first start at the desk's height)
    inf = os.path.join(hd, "eplay.inf")
    if case in ("stall", "startfail"):
        with open(inf, "w", newline="") as fh:
            fh.write("STALLAT=%s\r\n" % ("4000" if case == "stall" else "-1"))
    old_inf = open(inf, newline="").read() if os.path.exists(inf) else ""
    # (EPLAY_RIG_INF: lines added, as play_test.py's: MP3CPU=1, a station
    # decoded on the CPU, task 83)
    for extra in os.environ.get("EPLAY_RIG_INF", "").split(";"):
        if extra.strip():
            old_inf += extra.strip() + "\r\n"
    if "WINDOW=" not in old_inf:
        with open(inf, "w", newline="") as fh:
            fh.write(old_inf + "WINDOW=87,89,472,251\r\n\r\n")
    with open(os.path.join(hd, "TAIL.TXT"), "w") as fh:
        fh.write(("" if case in ("url", "urledit", "find", "findrefused") else "C:\\RADIO.M3U" if case == "m3u" else url) + "\r\n")
    natfeats(hd)		# (EPlay's console lines: NATFEATS=1, the rigs' alone)
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
    if case in ("url", "urledit", "find", "findrefused"):
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
        if case in ("find", "findrefused"):
            # File > Find station... by the mouse (no key), Return searches, the
            # first station (selected) played with Play
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
            fx, fy = [int(v) for v in os.environ.get("FIND_AT", "96,8,96,59,0,0").split(",")[:4]], None
            # (GEM's menus drop on the pointer over the title, not on a click)
            xdo("mousemove", "--window", win[0], str(fx[0]), str(fx[1]))
            time.sleep(1.5)
            cmd(fifo, "hatari-shortcut", "screenshot")
            xdo("mousemove", "--window", win[0], str(fx[0]), str(fx[3]))
            time.sleep(0.5)
            xdo("mousemove", "--window", win[0], str(fx[2]), str(fx[3]))
            time.sleep(0.5)
            xdo("click", "1")
            time.sleep(2)
            cmd(fifo, "hatari-shortcut", "screenshot")
            xdo("key", "Return")
            wait_line(log, lambda l: "Find station:" in l, 60)
            time.sleep(2)
            cmd(fifo, "hatari-shortcut", "screenshot")
            if case == "findrefused":
                time.sleep(1)
                text = open(log, errors="replace").read()
                lines = [l[4:] for l in text.split("\n") if "Find station:" in l]
                ok = any("answered 400: a station's name wanted" in l for l in lines) and \
                    any("said: The Gateway refused the search" in l for l in lines) and \
                    not any("doesn't answer" in l for l in lines)
                return ok, m.describe(), " | ".join(lines) or "no search logged"
            px, py = [int(v) for v in os.environ.get("FIND_PLAY", "512,408").split(",")]
            if os.environ.get("FIND_DBL"):
                # (FIND_DBL=70: a double click, 70 ms apart, on the first station plays it, the
                # hub's check of 11 Nov 2026: 70 ms between the clicks)
                px, py = [int(v) for v in os.environ.get("FIND_ROW", "200,225").split(",")]
            xdo("mousemove", "--window", win[0], str(px), str(py))
            time.sleep(0.5)
            if os.environ.get("FIND_DBL"):
                xdo("click", "--repeat", "2", "--delay", os.environ["FIND_DBL"], "1")
                time.sleep(3)
                cmd(fifo, "hatari-shortcut", "screenshot")
            else:
                xdo("click", "1")
        if case in ("url", "urledit"):
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
            if case == "urledit":
                for k in ["Left"] * 4 + ["Z"] + ["Right"] * 2 + ["Y"]:
                    xdo("key", "--delay", "150", k)
                    time.sleep(0.3)
                time.sleep(1)
            cmd(fifo, "hatari-shortcut", "screenshot")
            time.sleep(1)
            xdo("key", "Return")
        cmd(fifo, "hatari-shortcut", "recsound")
        played = wait_line(log, said, 90)
        if not played or not played.startswith("EPLAY playing"):
            return False, m.describe(), played or "nothing played"
        # (stall: 3 s more, the music's return after the restart recorded too)
        time.sleep(17 if case == "stall" else 14 if case in ("drop", "startfail") else 18 if case == "rough" else 10)
        if case == "logo":
            wait_line(log, lambda l: "album art:" in l or ("logo" in l and "after" in l), 120)
            time.sleep(1)
        if case in ("cover", "gwcover"):
            wait_line(log, lambda l: "album art:" in l or ("picture http" in l and "after" in l), 150)
            time.sleep(1)
        cmd(fifo, "hatari-shortcut", "screenshot")
        cmd(fifo, "hatari-shortcut", "recsound")
        time.sleep(1)
        text = open(log, errors="replace").read()
        titles = sorted(set(l[len("EPLAY title: "):] for l in text.split("\n") if l.startswith("EPLAY title: ")))
        # (stall: the tone 2 s in, before the stall at 4 s; until 1.52 it was
        # measured 4 s in, where the stall begins, and failed in about one
        # run of three, as the 100 ms window caught the stall's first ms)
        ok, what, gaps = analyse(rec, 0, 16 if case == "rough" else 8, 2.0 if case == "stall" else None)
        note = "%s; titles %s; %d connections" % (what, titles, st.conns)
        if case == "live":
            ok = ok and gaps == 0 and len(titles) >= 2
        elif case == "drop":
            ok = ok and st.conns >= 2 and gaps <= 25   # (a reconnect may cost up to 0.5 s)
        elif case == "startfail":
            again = [l[4:] for l in text.split("\n") if l.startswith("LOG the DSP stalled: started again")]
            note += "; " + (again[0] if again else "never started again")
            ok = ok and bool(again) and st.conns >= 2 and "DSP stopped answering." not in text
        elif case == "stall":
            again = [l[4:] for l in text.split("\n") if l.startswith("LOG the DSP stalled: started again")]
            note += "; " + (again[0] if again else "never started again")
            back = resumed(rec)
            note += "; " + (back[1] if back else "no sound after the stall")
            ok = ok and bool(again) and st.conns >= 2 and "DSP stopped answering." not in text and \
                bool(back) and back[0]
        elif case == "find":
            found = [l for l in text.split("\n") if "Find station:" in l]
            note += "; " + " | ".join(l[4:] for l in found)
            ok = ok and gaps == 0 and any("2 stations" in l for l in found) and \
                any(l.startswith("search asked") for l in st.log)
        elif case in ("gateway", "https", "nginx"):
            ok = ok and gaps == 0 and len(titles) >= (1 if case == "nginx" else 2)
            if case in ("https", "nginx"):
                note += "; " + ", ".join(l for l in st.log if l.startswith("TLS"))
                ok = ok and any(l.startswith("TLS ") for l in st.log)
        elif case in ("cover", "gwcover"):
            from PIL import Image
            shot = sorted(os.listdir(shots))[-1]
            im = Image.open(os.path.join(shots, shot)).convert("RGB")
            # (the art square: the bar's left; red pixels on its left half, blue on its right)
            reds = sum(1 for x in range(90, 130) for y in range(140, 190)
                       if im.getpixel((x, y))[0] > 150 and im.getpixel((x, y))[2] < 100)
            blues = sum(1 for x in range(130, 175) for y in range(140, 190)
                        if im.getpixel((x, y))[2] > 150 and im.getpixel((x, y))[0] < 100)
            pic = [l for l in text.split("\n") if "picture" in l or "album art" in l]
            note += "; %d red, %d blue; %s" % (reds, blues, " | ".join(l[4:] for l in pic))
            ok = ok and gaps == 0 and reds > 300 and blues > 300 and "cover fetched" in st.log
            if case == "gwcover":
                ok = ok and any("through the Gateway" in l for l in pic)
        elif case == "logo":
            from PIL import Image
            shot = sorted(os.listdir(shots))[-1]
            im = Image.open(os.path.join(shots, shot)).convert("RGB")
            greens = sum(1 for x in range(92, 170) for y in range(140, 190)
                         if im.getpixel((x, y))[1] > 120 and im.getpixel((x, y))[0] < 90 and im.getpixel((x, y))[2] < 90)
            pic = [l[4:] for l in text.split("\n") if "logo" in l or "album art:" in l]
            spoke = [l for l in text.split("\n") if l.startswith("EPLAY status") and "logo" in l.lower()]
            note += "; %d green; %s" % (greens, " | ".join(pic))
            ok = ok and gaps == 0 and greens > 1500 and "logo fetched" in st.log and not spoke
        elif case == "chunked":
            ok = ok and gaps == 0 and len(titles) >= 2
        elif case == "rough":
            late = [l[4:] for l in text.split("\n") if l.startswith("LOG ") and
                    ("late" in l or "lead" in l or "connect" in l or "Normal" in l or "miss" in l)]
            note += "; " + " | ".join(late[-6:])
            ok = ok and gaps == 0 and st.conns <= 2
        elif case == "pls":
            ok = ok and gaps == 0 and any("/live" in l for l in st.log)
        elif case == "spotify":
            got = wait_line(log, lambda l: l.startswith("EPLAY track: " + test_spotgate.T2), 60)
            end = wait_line(log, lambda l: "the line's end" in l, 60)
            text = open(log, errors="replace").read()
            tracks = [l[len("EPLAY track: "):] for l in text.split("\n") if l.startswith("EPLAY track: ")]
            note += "; tracks %s; %s; the player asked %s" % (tracks, end[4:] if end else "no end", spot.plays)
            ok = ok and gaps == 0 and bool(got) and bool(end) and len(tracks) == 2 and \
                tracks[0] == "%s 8000 0 A Band - One" % test_spotgate.T1 and \
                tracks[1].startswith(test_spotgate.T2 + " 8000 0 A Band, A Guest - Two") and \
                spot.plays == [{"uri": test_spotgate.PL}] and "connecting again" not in text
        elif case == "m3u":
            ok = ok and gaps == 0 and "playlist C:\\RADIO.M3U: 1 files" in open(log, errors="replace").read()
        elif case in ("url", "urledit"):
            xdo("key", "--delay", "120", "ctrl+q")     # (EPLAY.INF is written as EPlay quits)
            time.sleep(4)
            inf = open(os.path.join(hd, "eplay.inf"), errors="replace").read() if os.path.exists(
                os.path.join(hd, "eplay.inf")) else ""
            want = url if case == "url" else url[:-4] + "ZliYve"
            kept = ("URL1=" + want) in inf and (case == "url" or ("URLTYPED=" + want) in inf)
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
        if gate:
            gate.shutdown()
        if spot:
            spot.stop()
        if ngx:
            ngx.terminate()
        if x:
            x.terminate()


def main():
    work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "/tmp/eplay-stream")
    cases = [a for a in sys.argv[2:] if a in CASES] or CASES
    # (the reference configuration by default, the hub's 10 Nov 2026: Falcon-4:256
    # with NVDI; EmuTOS without NVDI where the container has neither, a
    # stand-in, as describe() says)
    specs = [a for a in sys.argv[2:] if a not in CASES] or [machines.ERZ_MACHINE]
    if any(sp.startswith(("Falcon-0", "Falcon-v4")) for sp in specs):
        import play060  # noqa: F401  (its 68060 and 68040 Falcons)
    nvdi_src = os.environ.get("AERA_NVDI", os.path.join(os.path.expanduser("~"), ".aera", "nvdi", "x"))
    if specs == [machines.ERZ_MACHINE] and os.path.exists(os.path.join(nvdi_src, "auto", "nvdi.prg")):
        os.environ.update(machines.erz_env())	# (no NVDI here: the same machine without it, a stand-in)
    failed = 0
    for spec in specs:
        for case in cases:
            ok, desc, note = run(work, case, spec)
            print("%-4s %s on %s: %s" % ("ok" if ok else "FAIL", case, desc, note))
            sys.stdout.flush()
            failed += not ok
    for k in SKIPS:
        print("SKIPPED:", k)
    print(("all passed" if not SKIPS else "passed, %d skipped" % len(SKIPS)) if not failed else "%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
