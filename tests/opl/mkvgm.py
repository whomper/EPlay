#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""mkvgm.py - YM3812 (OPL2) test files (phase 10, step 7), each a known patch,
so EPlay's model (and the DSP) can be held against the reference (ref.c,
Nuked-OPL3 running them as an OPL2):

  sine    channel 0, its carrier alone (the modulator silent), additive: a
          sine at A4 (F-number 580, block 4: 440.0 Hz at 3,579,545 Hz), 1 s on
  fm      channel 0 in FM, the modulator at TL 20, feedback 5
  env     the carrier alone: a slow attack, a decay to a sustain, a release
  wave    the four waveforms in turn (register 1's bit 5 on), half a second each
  lfo     tremolo and vibrato on the carrier, the deep settings (register 0xBD)
  rhythm  the rhythm mode: bass drum, snare, tom, cymbal and hi-hat, in turn
  chord   nine channels at once, a chord, additive sines
  keys    one channel, a note on and off eight times, rising (key-on edges)

and the YMF262's (OPL3, step 7b: commands 0x5E and 0x5F, its clock at 0x5C):

  o3pan     NEW set, channel 0's sine to the left alone
  o3bank    the second bank's channel 3 (12), a sine to both
  o3wave    waveforms 4 to 7 in turn
  o3four0-3 a four-operator pair (channels 0 and 3), each algorithm
  o3chord   18 channels, a chord, additive sines (both banks)
  o3compat  NEW clear, waveform 1 without register 1's WSE (the OPL3 takes it)

    mkvgm.py OUTDIR
"""
import os
import struct
import sys

CLOCK = 3579545
CLOCK3 = 14318180
RATE = 44100
OP1 = [0, 1, 2, 8, 9, 10, 16, 17, 18]     # a channel's first operator's slot


class Vgm:
    def __init__(self, opl3=False):
        self.d = bytearray()
        self.samples = 0
        self.opl3 = opl3

    def w(self, reg, val):
        if self.opl3:
            self.d += bytes([0x5F if reg & 0x100 else 0x5E, reg & 255, val & 255])
        else:
            self.d += bytes([0x5A, reg & 255, val & 255])

    def wait(self, n):
        self.samples += n
        while n > 0:
            k = min(n, 65535)
            self.d += bytes([0x61, k & 255, k >> 8])
            n -= k

    def save(self, path):
        head = bytearray(0x100)
        head[0:4] = b"Vgm "
        body = bytes(self.d) + b"\x66"
        struct.pack_into("<I", head, 0x04, 0x100 + len(body) - 4)
        struct.pack_into("<I", head, 0x08, 0x151)
        struct.pack_into("<I", head, 0x18, self.samples)
        if self.opl3:
            struct.pack_into("<I", head, 0x5C, CLOCK3)
        else:
            struct.pack_into("<I", head, 0x50, CLOCK)
        struct.pack_into("<I", head, 0x34, 0x100 - 0x34)
        with open(path, "wb") as f:
            f.write(bytes(head) + body)

    # an operator: o 0 the modulator, 1 the carrier of channel ch (an
    # OPL3's 9-17 its second bank's)
    def op(self, ch, o, am=0, vib=0, egt=1, ksr=0, mult=1, ksl=0, tl=0, ar=15, dr=0, sl=0, rr=15, ws=0):
        s = OP1[ch % 9] + 3 * o + (0x100 if ch >= 9 else 0)
        self.w(0x20 + s, am << 7 | vib << 6 | egt << 5 | ksr << 4 | mult)
        self.w(0x40 + s, ksl << 6 | tl)
        self.w(0x60 + s, ar << 4 | dr)
        self.w(0x80 + s, sl << 4 | rr)
        self.w(0xE0 + s, ws)

    def chan(self, ch, fb=0, cnt=1, out=None):
        if out is None:
            out = 0x30 if self.opl3 else 0     # (an OPL3's both outputs; an OPL2's file as it was)
        self.w(0xC0 + ch % 9 + (0x100 if ch >= 9 else 0), out | fb << 1 | cnt)

    def key(self, ch, fnum, block, on):
        b = 0x100 if ch >= 9 else 0
        self.w(0xA0 + ch % 9 + b, fnum & 255)
        self.w(0xB0 + ch % 9 + b, (0x20 if on else 0) | block << 2 | fnum >> 8)


def init(v):
    v.w(0x01, 0x20)              # waveform select on
    v.w(0x08, 0x00)
    v.w(0xBD, 0x00)


def sine(v):
    init(v)
    v.op(0, 0, tl=63)
    v.op(0, 1)
    v.chan(0, 0, 1)
    v.key(0, 580, 4, 1)
    v.wait(RATE)
    v.key(0, 580, 4, 0)
    v.wait(RATE // 4)


def fm(v):
    init(v)
    v.op(0, 0, tl=20, mult=1)
    v.op(0, 1, mult=1)
    v.chan(0, 5, 0)
    v.key(0, 580, 4, 1)
    v.wait(RATE)
    v.key(0, 580, 4, 0)
    v.wait(RATE // 4)


def env(v):
    init(v)
    v.op(0, 0, tl=63)
    v.op(0, 1, ar=4, dr=5, sl=5, rr=4)
    v.chan(0, 0, 1)
    v.key(0, 580, 4, 1)
    v.wait(RATE * 3 // 2)
    v.key(0, 580, 4, 0)
    v.wait(RATE)


def wave(v):
    init(v)
    v.op(0, 0, tl=63)
    v.chan(0, 0, 1)
    for ws in range(4):
        v.op(0, 1, ws=ws)
        v.key(0, 580, 4, 1)
        v.wait(RATE // 2)
        v.key(0, 580, 4, 0)
        v.wait(RATE // 20)


def lfo(v):
    init(v)
    v.w(0xBD, 0xC0)              # deep tremolo and vibrato
    v.op(0, 0, tl=63)
    v.op(0, 1, am=1, vib=1)
    v.chan(0, 0, 1)
    v.key(0, 580, 4, 1)
    v.wait(RATE * 2)
    v.key(0, 580, 4, 0)
    v.wait(RATE // 4)


def rhythm(v):
    init(v)
    # the rhythm channels 6, 7 and 8, their usual patches
    v.op(6, 0, egt=0, mult=0, tl=11, ar=13, dr=8, sl=0, rr=8)
    v.op(6, 1, egt=0, mult=0, tl=0, ar=13, dr=6, sl=4, rr=8)
    v.chan(6, 7, 0)
    v.op(7, 0, egt=0, mult=1, tl=0, ar=15, dr=8, sl=5, rr=8)      # hi-hat
    v.op(7, 1, egt=0, mult=1, tl=0, ar=15, dr=7, sl=5, rr=8)      # snare
    v.chan(7, 0, 1)
    v.op(8, 0, egt=0, mult=5, tl=0, ar=15, dr=7, sl=5, rr=8)      # tom
    v.op(8, 1, egt=0, mult=1, tl=0, ar=15, dr=8, sl=5, rr=8)      # cymbal
    v.chan(8, 0, 1)
    v.key(6, 0x157, 2, 0)
    v.key(7, 0x200, 2, 0)
    v.key(8, 0x150, 1, 0)
    for bit in (0x10, 0x08, 0x04, 0x02, 0x01):   # BD, SD, TOM, CYM, HH
        v.w(0xBD, 0x20 | bit)
        v.wait(RATE // 3)
        v.w(0xBD, 0x20)
        v.wait(RATE // 10)


def chord(v):
    init(v)
    notes = [(580, 3), (731, 3), (869, 3), (580, 4), (731, 4), (869, 4), (580, 5), (731, 5), (869, 5)]
    for ch, (fn, bl) in enumerate(notes):
        v.op(ch, 0, tl=63)
        v.op(ch, 1, tl=12)
        v.chan(ch, 0, 1)
    for ch, (fn, bl) in enumerate(notes):
        v.key(ch, fn, bl, 1)
    v.wait(RATE)
    for ch, (fn, bl) in enumerate(notes):
        v.key(ch, fn, bl, 0)
    v.wait(RATE // 4)


def keys(v):
    init(v)
    v.op(0, 0, tl=30, mult=2)
    v.op(0, 1, ar=12, dr=3, sl=3, rr=10)
    v.chan(0, 3, 0)
    for k in range(8):
        v.key(0, 400 + 50 * k, 4, 1)
        v.wait(RATE // 8)
        v.key(0, 400 + 50 * k, 4, 0)
        v.wait(RATE // 16)


def init3(v, new=1):
    v.w(0x105, new)              # NEW
    v.w(0x104, 0x00)
    v.w(0x08, 0x00)
    v.w(0xBD, 0x00)


def o3pan(v):
    init3(v)
    v.op(0, 0, tl=63)
    v.op(0, 1)
    v.chan(0, 0, 1, out=0x10)    # the left (A) alone
    v.key(0, 580, 4, 1)
    v.wait(RATE)
    v.key(0, 580, 4, 0)
    v.wait(RATE // 4)


def o3bank(v):
    init3(v)
    v.op(12, 0, tl=63)
    v.op(12, 1)
    v.chan(12, 0, 1)
    v.key(12, 580, 4, 1)
    v.wait(RATE)
    v.key(12, 580, 4, 0)
    v.wait(RATE // 4)


def o3wave(v):
    init3(v)
    v.op(0, 0, tl=63)
    v.chan(0, 0, 1)
    for ws in range(4, 8):
        v.op(0, 1, ws=ws)
        v.key(0, 580, 4, 1)
        v.wait(RATE // 2)
        v.key(0, 580, 4, 0)
        v.wait(RATE // 20)


def o3four(alg):
    def f(v):
        init3(v)
        v.w(0x104, 0x01)         # channels 0 and 3 a pair
        # (a chain of three modulations at deep levels is too sensitive to
        # compare: Nuked against itself with one operator 0.75 dB lower is 9
        # to 21 dB apart in timbre; the chains here moderate, as music's)
        v.op(0, 0, tl=32 if alg == 0 else 24, mult=1)       # A (its feedback channel 0's)
        v.op(0, 1, tl=(28, 20, 28, 8)[alg], mult=2)         # B
        v.op(3, 0, tl=(28, 20, 28, 8)[alg], mult=1)         # C
        v.op(3, 1, tl=0, mult=1)         # D
        v.chan(0, 3, alg >> 1)           # (the pair's connection: 0's and 3's)
        v.chan(3, 0, alg & 1)            # (its outputs 3's: both)
        v.key(0, 580, 4, 1)
        v.wait(RATE)
        v.key(0, 580, 4, 0)
        v.wait(RATE // 4)
    return f


def o3chord(v):
    init3(v)
    notes = [(580, 2), (731, 2), (869, 2), (580, 3), (731, 3), (869, 3), (580, 4), (731, 4), (869, 4),
             (580, 5), (731, 5), (869, 5), (580, 6), (731, 6), (869, 6), (436, 3), (548, 4), (651, 5)]
    for ch, (fn, bl) in enumerate(notes):
        v.op(ch, 0, tl=63)
        v.op(ch, 1, tl=16)
        v.chan(ch, 0, 1)
    for ch, (fn, bl) in enumerate(notes):
        v.key(ch, fn, bl, 1)
    v.wait(RATE)
    for ch, (fn, bl) in enumerate(notes):
        v.key(ch, fn, bl, 0)
    v.wait(RATE // 4)


def o3compat(v):
    init3(v, new=0)              # (register 1's WSE not set: the OPL3 has no such bit)
    v.op(0, 0, tl=63)
    v.op(0, 1, ws=1)
    v.chan(0, 0, 1)
    v.key(0, 580, 4, 1)
    v.wait(RATE)
    v.key(0, 580, 4, 0)
    v.wait(RATE // 4)


OPL2_FILES = (("SINE", sine), ("FM", fm), ("ENV", env), ("WAVE", wave), ("LFO", lfo), ("RHYTHM", rhythm),
              ("CHORD", chord), ("KEYS", keys))
OPL3_FILES = (("O3PAN", o3pan), ("O3BANK", o3bank), ("O3WAVE", o3wave), ("O3FOUR0", o3four(0)),
              ("O3FOUR1", o3four(1)), ("O3FOUR2", o3four(2)), ("O3FOUR3", o3four(3)), ("O3CHORD", o3chord),
              ("O3COMPAT", o3compat))


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for files, opl3 in ((OPL2_FILES, False), (OPL3_FILES, True)):
        for name, f in files:
            v = Vgm(opl3)
            f(v)
            v.save(os.path.join(out, name + ".VGM"))


if __name__ == "__main__":
    main()
