#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""mkgmset.py SRC OUT [--bits 8|16]: Sonivox EAS's General MIDI sound set
(third_party/sonivox/wt_22khz.c, Apache License 2.0) made into EPlay's
data file for its own synth (DESIGN.md, "The synth"), read by the Atari
side with no C parser: a separate file, so the set stays a separate work.

The file, every number big-endian, each section at a multiple of 4:

  0   "EPGM"
  4   u16 version (1), u16 flags (bit 0: 16-bit samples, else 8-bit)
  8   u32 sample rate (22050), u32 the library's own format word
  16  u16 banks, programs, regions, articulations, waves, 0
  28  u32 sample points (the pool: every wave's points)
  32  banks: u16 locale (MSB << 8 | LSB), 128 x u16 region index
      (bit 15: an FM region, bit 14: a DLS region, both absent here)
      programs: u32 locale (MSB << 16 | LSB << 8 | program), u16 region,
      u16 0
      regions, 20 bytes: u16 key group and flags (bit 0 looped, bit 15
      the program's last region; key group in bits 8-11), u8 lowest key,
      u8 highest key, i16 tuning (cents), i16 gain (32767 = 1), u32 loop
      start, u32 loop end (sample points from the wave's start), u16 wave,
      u16 articulation
      articulations, 32 bytes: EG1 and EG2 (i16 attack, decay, sustain,
      release, as Sonivox's tables), i16 LFO to pitch, LFO delay, LFO
      frequency, EG2 to pitch, EG2 to cutoff, cutoff; i8 LFO to gain,
      u8 filter Q, i8 pan, u8 0
      waves: u32 length x waves, then u32 offset x waves (sample points)
      samples: the pool, 1 or 2 bytes a point (the points Sonivox's array
      leaves out, zeros, as C makes them)
"""
import re
import struct
import sys

MAGIC = b"EPGM"


def body(src, name, which=0):
    """the numbers of the C array NAME's initialiser (the which-th one)"""
    found = [m for m in re.finditer(r"\b%s\s*\[[^\]]*\]\s*=\s*\{" % re.escape(name), src)]
    if len(found) <= which:
        sys.exit("mkgmset: no array %s (%d)" % (name, which))
    i = found[which].end()
    depth = 1
    j = i
    while depth:
        c = src[j]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        j += 1
    text = re.sub(r"/\*.*?\*/", " ", src[i:j - 1], flags=re.S)
    return [int(t, 0) for t in re.findall(r"-?(?:0x[0-9a-fA-F]+|\d+)", text)]


def lib_numbers(src):
    m = re.search(r"const S_EAS easSoundLib\s*=\s*\{(.*?)\};", src, re.S)
    t = re.sub(r"/\*.*?\*/", " ", m.group(1), flags=re.S)
    t = re.sub(r"#if defined \(_8_BIT_SAMPLES\)\s*(\S+),\s*#else[^\n]*\n\s*(\S+),\s*#endif",
               lambda k: k.group(1) + ",", t)
    return [int(x, 0) for x in re.findall(r"\b(?:0x[0-9a-fA-F]+|\d+)\b", t)]


def pad4(b):
    return b + b"\0" * (-len(b) % 4)


def make(src, bits):
    lib = lib_numbers(src)
    fmt8, nbank, nprog, nreg, nart, nwave = lib[1], lib[-6], lib[-5], lib[-4], lib[-3], lib[-2]
    rate = fmt8 & 0x3ffff
    banks = body(src, "eas_banks")
    progs = body(src, "eas_programs")
    regs = body(src, "eas_regions")
    arts = body(src, "eas_articulations")
    lens = body(src, "eas_sampleLengths")
    offs = body(src, "eas_sampleOffsets")
    samples = body(src, "eas_samples", 0 if bits == 8 else 1)
    pool = offs[-1] + lens[-1]
    # (the C array is declared with the pool's size and C fills the points
    # its initialiser leaves out with zeros: 127 of them, in the last waves)
    assert pool - 127 <= len(samples) <= pool, (len(samples), pool)
    samples = samples + [0] * (pool - len(samples))
    assert len(banks) == nbank * 129, len(banks)
    assert len(progs) == nprog * 2
    assert len(regs) == nreg * 9
    assert len(arts) == nart * 17
    assert len(lens) == nwave and len(offs) == nwave
    head = MAGIC + struct.pack(">HHII6HI", 1, 1 if bits == 16 else 0, rate, fmt8 if bits == 8 else
                               (fmt8 & ~0x00100000) | 0x00200000, nbank, nprog, nreg, nart, nwave, 0, pool)
    out = [pad4(head)]
    out.append(pad4(b"".join(struct.pack(">H128H", *banks[k * 129:k * 129 + 129]) for k in range(nbank))))
    out.append(b"".join(struct.pack(">IHH", progs[k * 2], progs[k * 2 + 1], 0) for k in range(nprog)))
    out.append(b"".join(struct.pack(">HBBhhIIHH", *regs[k * 9:k * 9 + 9]) for k in range(nreg)))
    out.append(b"".join(struct.pack(">4h4h6hbBbB", *(arts[k * 17:k * 17 + 17] + [0])) for k in range(nart)))
    out.append(struct.pack(">%dI" % nwave, *lens) + struct.pack(">%dI" % nwave, *offs))
    out.append(pad4(struct.pack(">%d%s" % (len(samples), "b" if bits == 8 else "h"), *samples)))
    return b"".join(out)


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    bits = 16 if "--bits" in sys.argv and sys.argv[sys.argv.index("--bits") + 1] == "16" else 8
    a = [x for x in a if x not in ("8", "16")]
    if len(a) != 2:
        sys.exit(__doc__)
    src = open(a[0], encoding="latin-1").read()
    data = make(src, bits)
    with open(a[1], "wb") as f:
        f.write(data)
    print("%s: %d bytes (%d-bit samples)" % (a[1], len(data), bits))


if __name__ == "__main__":
    main()
