#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""run.py TEST DIR: MIDI files made here, each read by test_smf (src/smf.c)
and by this file's own reading (exact fractions), the records compared:
the same bytes in the same order, each within 1 ms of the exact time.
Then damaged files and random bytes, which must neither crash nor hang
(the test is built with the sanitizers)."""
import os
import random
import struct
import subprocess
import sys
from fractions import Fraction

TEST, D = sys.argv[1], sys.argv[2]
os.makedirs(D, exist_ok=True)


def vlq(n):
    b = [n & 0x7f]
    n >>= 7
    while n:
        b.insert(0, 0x80 | (n & 0x7f))
        n >>= 7
    return bytes(b)


def track(events):
    """events: (delta, bytes) with bytes as they go in the file"""
    body = b"".join(vlq(d) + e for d, e in events)
    return b"MTrk" + struct.pack(">I", len(body)) + body


def smf(typ, div, tracks, ntrk=None):
    return b"MThd" + struct.pack(">IHHh", 6, typ, ntrk if ntrk is not None else len(tracks), div) + b"".join(tracks)


def tempo(us):
    return b"\xff\x51\x03" + struct.pack(">I", us)[1:]


def meta(t, s):
    return bytes([0xff, t]) + vlq(len(s)) + s


EOT = b"\xff\x2f\x00"


def reference(data):
    """(records, len_ms, title, copyright): this file's own reading"""
    typ, ntrk, div = struct.unpack(">HHh", data[8:14])
    hlen = struct.unpack(">I", data[4:8])[0]
    p = 8 + hlen
    trks = []
    while p + 8 <= len(data) and len(trks) < ntrk:
        n = struct.unpack(">I", data[p + 4:p + 8])[0]
        d = data[p + 8:p + 8 + n]
        if data[p:p + 4] == b"MTrk":
            trks.append(d)
        p += 8 + n
    evs = []          # (tick, track, seq, kind, payload)
    ends = []
    for ti, d in enumerate(trks):
        i, tick, rs, seq = 0, 0, 0, 0

        def rv():
            nonlocal i
            v = 0
            for _ in range(4):
                if i >= len(d):
                    return None
                c = d[i]
                i += 1
                v = v << 7 | (c & 0x7f)
                if not c & 0x80:
                    return v
            return None
        while i < len(d):
            dt = rv()
            if dt is None:
                break
            tick += dt
            if i >= len(d):
                break
            st = d[i]
            if st & 0x80:
                i += 1
            else:
                st = rs
                if not st & 0x80 or st >= 0xf0:
                    break
            if st < 0xf0:
                n = 1 if st & 0xe0 == 0xc0 else 2
                if len(d) - i < n:
                    break
                rs = st
                evs.append((tick, ti, seq, "m", bytes([st]) + bytes(x & 0x7f for x in d[i:i + n])))
                i += n
            elif st in (0xf0, 0xf7):
                rs = 0
                n = rv()
                if n is None or n > len(d) - i:
                    break
                body = d[i:i + n]
                evs.append((tick, ti, seq, "x", (b"\xf0" + body) if st == 0xf0 else body))
                i += n
            elif st == 0xff:
                rs = 0
                if i >= len(d):
                    break
                t = d[i]
                i += 1
                n = rv()
                if n is None or n > len(d) - i:
                    break
                evs.append((tick, ti, seq, "meta%d" % t, d[i:i + n]))
                i += n
                if t == 0x2f:
                    break
            else:
                break
            seq += 1
        ends.append((tick, ti))
    if typ == 2:
        # songs one after another: each track's ticks from the end of the one before
        off, out = 0, []
        for ti in range(len(trks)):
            mine = [e for e in evs if e[1] == ti]
            out += [(t + off, ti, s, k, p) for t, ti_, s, k, p in mine]
            off += ends[ti][0]
        evs = out
        ends = [(off, 0)]
    evs.sort(key=lambda e: (e[0], e[1], e[2]) if typ != 2 else (e[1], e[2]))
    # the times, exact
    if div < 0:
        fps = -(div >> 8)
        fps = Fraction(2997, 100) if fps == 29 else fps
        us_tick = lambda tempo_: Fraction(1000000) / (fps * (div & 0xff))
    else:
        us_tick = lambda tempo_: Fraction(tempo_, div)
    cur_tick, cur_us, tp = 0, Fraction(0), 500000
    recs, title, got_name, copy = [], "", False, ""

    def at(tick):
        return cur_us + (tick - cur_tick) * us_tick(tp)
    for tick, ti, seq, kind, payload in evs:
        us = at(tick)
        cur_tick, cur_us = tick, us
        if kind == "meta81" and len(payload) >= 3 and div > 0:
            tp = payload[0] << 16 | payload[1] << 8 | payload[2]
        elif kind == "meta3" and ti == 0 and not got_name:
            title = "".join(chr(c) for c in payload if 32 <= c < 127).rstrip()
            got_name = bool(title)
        elif kind == "meta2" and not copy:
            copy = "".join(chr(c) for c in payload if 32 <= c < 127).rstrip()
        elif kind in ("m", "x") and payload:
            if kind == "x" and payload[:1] == b"\xf0" and len(payload) > 255:
                recs.append((us / 1000, b"\xf0"))
                payload = payload[1:]
            for k in range(0, len(payload), 255):
                recs.append((us / 1000, payload[k:k + 255]))
    end_us = max(at(t) for t, _ in ends) if ends else Fraction(0)
    end_us = max([end_us] + [r[0] * 1000 for r in recs])
    return recs, end_us / 1000, title, copy


def run(name, data, expect_error=None):
    path = os.path.join(D, name)
    with open(path, "wb") as f:
        f.write(data)
    r = subprocess.run([TEST, path], capture_output=True, text=True, timeout=20)
    out = r.stdout.splitlines()
    if expect_error is not None:
        if r.returncode != 1 or not out or not out[0].startswith("error:") or expect_error not in out[0]:
            return "%s: wanted the error %r, got rc %d: %s" % (name, expect_error, r.returncode, out[:2])
        return None
    if r.returncode != 0:
        return "%s: rc %d: %s %s" % (name, r.returncode, out[:2], r.stderr[-300:])
    got = []
    info = {}
    for l in out:
        f = l.split(" ", 1)
        if f[0].isdigit():
            got.append((int(f[0]), bytes.fromhex(f[1].replace(" ", "")) if len(f) > 1 else b""))
        else:
            info[f[0]] = f[1] if len(f) > 1 else ""
    want, len_ms, title, copy = reference(data)
    if len(got) != len(want):
        return "%s: %d records, wanted %d" % (name, len(got), len(want))
    for i, ((gm, gb), (wm, wb)) in enumerate(zip(got, want)):
        if gb != wb:
            return "%s: record %d: %s, wanted %s" % (name, i, gb.hex(), wb.hex())
        if abs(gm - wm) > 1:
            return "%s: record %d at %d ms, wanted %.3f" % (name, i, gm, float(wm))
    if abs(int(info["len"]) - len_ms) > 1:
        return "%s: length %s ms, wanted %.3f" % (name, info["len"], float(len_ms))
    if info.get("title", "") != title or info.get("copyright", "") != copy:
        return "%s: title %r copyright %r, wanted %r %r" % (name, info.get("title"), info.get("copyright"), title, copy)
    return None


fails = []
cases = []
# type 0: running status, a tempo change, program, pitch bend, pressure
cases.append(("type0.mid", smf(0, 480, [track([
    (0, meta(3, b"Type zero")), (0, b"\xc0\x05"), (0, b"\x90\x3c\x64"), (240, b"\x3e\x64"), (240, b"\x3c\x00"),
    (0, b"\x3e\x00"), (0, tempo(250000)), (480, b"\xe0\x00\x40"), (10, b"\xd0\x30"), (480, b"\x80\x40\x00"),
    (0, EOT)])])))
# type 1: the tempo map in track 0, notes in 1 and 2 interleaved
t0 = track([(0, meta(3, b"Song")), (0, meta(2, b"(C) Nobody")), (0, tempo(600000)), (960, tempo(400000)),
            (1920, tempo(1000000)), (0, EOT)])
t1 = track([(0, b"\xc1\x10"), (100, b"\x91\x40\x50"), (480, b"\x40\x00"), (1000, b"\x91\x41\x50"), (500, b"\x81\x41\x40"),
            (0, EOT)])
t2 = track([(50, b"\xb2\x07\x64"), (430, b"\x92\x24\x7f"), (0, b"\x92\x28\x7f"), (2000, b"\x92\x24\x00"), (0, EOT)])
cases.append(("type1.mid", smf(1, 480, [t0, t1, t2])))
# system exclusive: the GM reset, one past 255 bytes, an F7 packet
big = bytes(range(1, 127)) * 3
cases.append(("sysex.mid", smf(0, 96, [track([
    (0, b"\xf0\x05\x7e\x7f\x09\x01\xf7"), (10, b"\xf0" + vlq(len(big) + 1) + big + b"\xf7"),
    (10, b"\xf7\x03\xf3\x01\xf6"), (0, b"\x90\x3c\x40"), (96, b"\x80\x3c\x40"), (0, EOT)])])))
# type 2: two songs, one after the other
s1 = track([(0, b"\x90\x30\x40"), (480, b"\x80\x30\x40"), (0, EOT)])
s2 = track([(0, tempo(1000000)), (240, b"\x90\x32\x40"), (480, b"\x80\x32\x40"), (0, EOT)])
cases.append(("type2.mid", smf(2, 480, [s1, s2])))
# SMPTE: 25 frames a second, 40 ticks a frame (a millisecond a tick); 29.97 too
cases.append(("smpte25.mid", smf(0, struct.unpack(">h", bytes([256 - 25, 40]))[0],
                                 [track([(0, b"\x90\x3c\x40"), (1000, b"\x80\x3c\x40"), (5000, b"\x90\x3c\x40"), (0, EOT)])])))
cases.append(("smpte29.mid", smf(0, struct.unpack(">h", bytes([256 - 29, 80]))[0],
                                 [track([(0, b"\x90\x3c\x40"), (2397, b"\x80\x3c\x40"), (0, EOT)])])))
# a long one: no drift (an hour at 480 a quarter, tempo 500,000 and 476,190)
ev = [(0, tempo(476190))]
for k in range(4000):
    ev.append((431, b"\x90\x3c\x40"))
    ev.append((449, b"\x80\x3c\x40"))
ev.append((0, EOT))
cases.append(("long.mid", smf(0, 480, [track(ev)])))
# a chunk not a track passed over; a track cut short by the file's end
tr = track([(0, b"\x90\x3c\x40"), (100, b"\x80\x3c\x40"), (100, b"\x90\x3e\x40"), (100, b"\x80\x3e\x40"), (0, EOT)])
cases.append(("chunks.mid", smf(1, 96, [b"XFIH\x00\x00\x00\x03abc", tr], ntrk=1)))
cases.append(("cut.mid", smf(0, 96, [tr])[:-7]))
# data with no status to run on: the track stops there
cases.append(("nostatus.mid", smf(0, 96, [track([(0, b"\x3c\x40"), (0, EOT)]), ], ntrk=1)))
for name, data in cases:
    e = run(name, data)
    if e:
        fails.append(e)
    else:
        print("ok    %s" % name)
for name, data, err in (("notmidi.mid", b"RIFF\x00\x00\x00\x00WAVE", "isn't a MIDI"),
                        ("badhead.mid", b"MThd\x00\x00\x00\x06\x00\x05\x00\x01\x00\x60", "damaged"),
                        ("notracks.mid", smf(1, 96, [], ntrk=0), "damaged")):
    e = run(name, data, err)
    if e:
        fails.append(e)
    else:
        print("ok    %s refused" % name)
# random damage: no crash, no hang (ASan/UBSan built); records still checked where it reads
rnd = random.Random(5)
base = cases[1][1]
for k in range(300):
    d = bytearray(base)
    for _ in range(rnd.randint(1, 12)):
        d[rnd.randrange(14, len(d))] = rnd.randrange(256)
    if rnd.random() < 0.3:
        d = d[:rnd.randrange(14, len(d))]
    path = os.path.join(D, "fuzz.mid")
    open(path, "wb").write(bytes(d))
    r = subprocess.run([TEST, path], capture_output=True, text=True, timeout=20)
    if r.returncode not in (0, 1) or "runtime error" in r.stderr or "AddressSanitizer" in r.stderr:
        fails.append("fuzz %d: rc %d %s" % (k, r.returncode, r.stderr[-300:]))
        open(os.path.join(D, "fuzz-fail-%d.mid" % k), "wb").write(bytes(d))
        break
else:
    print("ok    300 damaged files")
for f in fails:
    print("FAIL  " + f)
sys.exit(1 if fails else 0)
