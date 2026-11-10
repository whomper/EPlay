#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""mkvgm.py - YM2612 test files (phase 10), each a known patch, so the model
(and the DSP) can be held against the reference (ref.c, Nuked-OPN2):

  sine    algorithm 7, operator 4 alone (the others silent): a sine at A4
          (F-number 1084, block 4: 440.6 Hz at the NTSC clock), 1 s on, 0.5 s off
  fm      algorithm 4 (two pairs), operator 1 modulating 2, 3 modulating 4:
          a brass-like tone, its feedback 5
  chain   algorithm 0, the four in a chain (the heaviest), a bell
  env     operator 4 alone, a slow attack, a decay to a sustain, a release:
          the envelope's shape
  dac     the DAC (channel 6) playing a 1 kHz sine from a data block at
          8,000 samples a second
  chord   six channels at once, a chord in algorithm 7, both sides and the
          left, right and centre panned

    mkvgm.py OUTDIR
"""
import math
import os
import struct
import sys

CLOCK = 7670453          # NTSC Mega Drive
RATE = 44100


class Vgm:
    def __init__(self):
        self.d = bytearray()
        self.samples = 0

    def w(self, port, reg, val):
        self.d += bytes([0x52 + port, reg, val])

    def wait(self, n):
        self.samples += n
        while n > 0:
            k = min(n, 65535)
            self.d += bytes([0x61, k & 255, k >> 8])
            n -= k

    def save(self, path, data_block=b""):
        head = bytearray(0x100)
        head[0:4] = b"Vgm "
        body = (bytes([0x67, 0x66, 0x00]) + struct.pack("<I", len(data_block)) + data_block
                if data_block else b"") + bytes(self.d) + b"\x66"
        struct.pack_into("<I", head, 0x04, 0x100 + len(body) - 4)
        struct.pack_into("<I", head, 0x08, 0x171)
        struct.pack_into("<I", head, 0x18, self.samples)
        struct.pack_into("<I", head, 0x2c, CLOCK)
        struct.pack_into("<I", head, 0x34, 0x100 - 0x34)
        with open(path, "wb") as f:
            f.write(bytes(head) + body)


SLOT = (0, 8, 4, 12)     # operators 1-4 at register offsets 0, 8, 4, 12 (the chip's order: 1, 3, 2, 4)


def op(v, ch, o, dt_mul=1, tl=0, ks_ar=31, am_d1r=0, d2r=0, sl_rr=0x0f):
    port, c = divmod(ch, 3)
    base = c + SLOT[o]
    v.w(port, 0x30 + base, dt_mul)
    v.w(port, 0x40 + base, tl)
    v.w(port, 0x50 + base, ks_ar)
    v.w(port, 0x60 + base, am_d1r)
    v.w(port, 0x70 + base, d2r)
    v.w(port, 0x80 + base, sl_rr)
    v.w(port, 0x90 + base, 0)


def chan(v, ch, alg, fb=0, pan=0xc0):
    port, c = divmod(ch, 3)
    v.w(port, 0xb0 + c, fb << 3 | alg)
    v.w(port, 0xb4 + c, pan)


def freq(v, ch, fnum, block):
    port, c = divmod(ch, 3)
    v.w(port, 0xa4 + c, block << 3 | fnum >> 8)
    v.w(port, 0xa0 + c, fnum & 255)


def key(v, ch, on):
    port, c = divmod(ch, 3)
    v.w(0, 0x28, (0xf0 if on else 0) | port << 2 | c)


def init(v):
    v.w(0, 0x22, 0)      # no LFO
    v.w(0, 0x27, 0)      # channel 3 normal
    v.w(0, 0x2b, 0)      # the DAC off
    for ch in range(6):
        key(v, ch, False)


def sine(out):
    v = Vgm()
    init(v)
    chan(v, 0, 7)
    for o in range(4):
        op(v, 0, o, tl=0 if o == 3 else 127)
    freq(v, 0, 1084, 4)
    key(v, 0, True)
    v.wait(RATE)
    key(v, 0, False)
    v.wait(RATE // 2)
    v.save(out)


def fm(out):
    v = Vgm()
    init(v)
    chan(v, 0, 4, fb=5)
    op(v, 0, 0, dt_mul=1, tl=30)
    op(v, 0, 1, dt_mul=1, tl=0, sl_rr=0x27)
    op(v, 0, 2, dt_mul=2, tl=35)
    op(v, 0, 3, dt_mul=1, tl=4, sl_rr=0x27)
    freq(v, 0, 1084, 3)
    key(v, 0, True)
    v.wait(RATE)
    key(v, 0, False)
    v.wait(RATE // 2)
    v.save(out)


def chain(out):
    v = Vgm()
    init(v)
    chan(v, 0, 0, fb=3)
    op(v, 0, 0, dt_mul=7, tl=40, am_d1r=4, sl_rr=0x5f)
    op(v, 0, 1, dt_mul=3, tl=36, am_d1r=3, sl_rr=0x5f)
    op(v, 0, 2, dt_mul=1, tl=30, am_d1r=2, sl_rr=0x5f)
    op(v, 0, 3, dt_mul=1, tl=0, am_d1r=2, sl_rr=0x5f)
    freq(v, 0, 1084, 4)
    key(v, 0, True)
    v.wait(RATE * 3 // 2)
    key(v, 0, False)
    v.wait(RATE // 2)
    v.save(out)


def env(out):
    v = Vgm()
    init(v)
    chan(v, 0, 7)
    for o in range(3):
        op(v, 0, o, tl=127)
    # attack 20 (2 x 20 = 40: a slow one), first decay 10 to level 4 (12 dB),
    # no second decay, release 6
    op(v, 0, 3, tl=0, ks_ar=20, am_d1r=10, d2r=0, sl_rr=0x46)
    freq(v, 0, 1084, 4)
    key(v, 0, True)
    v.wait(RATE * 2)
    key(v, 0, False)
    v.wait(RATE * 2)
    v.save(out)


def dac(out):
    v = Vgm()
    init(v)
    v.w(0, 0x2b, 0x80)
    v.w(1, 0xb6, 0xc0)   # channel 6 both sides
    sr = 8000
    pcm = bytes(int(128 + 100 * math.sin(2 * math.pi * 1000 * i / sr)) for i in range(sr))
    v.d += bytes([0xe0]) + struct.pack("<I", 0)
    # each sample: the next of the block, then a wait (44,100 / 8,000: 5 or 6)
    acc = 0.0
    for i in range(sr):
        acc += RATE / sr
        n = int(acc)
        acc -= n
        while n > 15:
            v.d += bytes([0x8f])
            v.samples += 15
            n -= 15
            # (a sample a 0x8n: a longer wait as 0x8F then waits without one)
            v.d.pop()
            v.wait(15)
        v.d += bytes([0x80 + n])
        v.samples += n
    v.w(0, 0x2b, 0)
    v.wait(RATE // 4)
    v.save(out, pcm)


def chord(out):
    v = Vgm()
    init(v)
    notes = [(1084, 3), (1366, 3), (1625, 3), (1084, 4), (1366, 4), (1625, 4)]
    pans = [0xc0, 0x80, 0x40, 0xc0, 0x80, 0x40]
    for ch in range(6):
        chan(v, ch, 7, pan=pans[ch])
        for o in range(4):
            op(v, ch, o, tl=127 if o < 3 else 16)
        freq(v, ch, *notes[ch])
        key(v, ch, True)
    v.wait(RATE)
    for ch in range(6):
        key(v, ch, False)
    v.wait(RATE // 2)
    v.save(out)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for f in (sine, fm, chain, env, dac, chord):
        f(os.path.join(out, f.__name__.upper() + ".VGM"))


if __name__ == "__main__":
    main()
