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

The cases: wav, wavvol, aiff, aiff24, wav22, mp3, mp3long, vbr, fsel, auto, aiff44, settings, keys, selall, list, m3u, tree, repeat, shuffle, save, mini, addplay, wavrg, wavrgup, mp3rg, vgm, vgz, vgmsn, vgmopn, vgmmd, vgmopl, vgmopl3, vgmopl3late, vgmlow, wavlow (the TT and STE alone), stall, firstrun, quit (and many, manym3u, manytree); all but the many ones by default. The machines: Erez's configuration by default
(machines.ERZ_MACHINE, Falcon-4:256 with NVDI; on EmuTOS without NVDI here, a
stand-in: the hub's, 10 Nov 2026); Falcon (with the DFB1) when named, once a week (the
hub's test matrix, 7 Nov 2026: it never found what Falcon-4 missed). Each case leaves
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
    # Add files (Control+O) held open 5 s while it plays, then closed with
    # Esc: the music must go on (the tick), nothing added
    # VBR, as music is: LAME's header frame (Xing) gives the length (0.1 took
    # the first frame's bit rate: an 8-minute file said 22:10)
    "vbr": ("VBR.MP3", ["-r", "44100", "-c", "2", "-b", "16"], ["XING", "-V", "0"], 49170,
            (1000, 1500), 10),
    "fsel": ("LONG.MP3", ["-r", "44100", "-c", "2", "-b", "16"], ["-b", "320", "-m", "s"], 49170,
             (1000, 1500), 10),
    # (Erez, 13 Nov 2026: File > Open as Open URL) while it plays, Add files
    # (Control+O), ^A and Return add ANOTHER.MP3 (LONG.MP3 is in the list
    # already): the added row plays at once
    "addplay": ("LONG.MP3", ["-r", "44100", "-c", "2", "-b", "16"], ["-b", "320", "-m", "s"], 49170,
                (1000, 1500), 30),
    # Quality by itself (Erez, 4 Nov 2026: no longer a setting): where the
    # DSP falls behind at 49,170 Hz, the file goes on at 32,780 Hz, the
    # subbands above about 15 kHz left out (Normal), to its end. Hatari's
    # DSP keeps up with every file now (square waves and noise at 48 kHz,
    # 320 kbit/s: waits to spare), so eplay.inf's QLATE=0 (the rigs')
    # makes it go to Normal past its first frames
    "auto": ("SQUARE.MP3", ["-r", "48000", "-c", "2", "-b", "16"], ["-b", "320"], 32780, (100, 1500), 8),
}
# Settings (Control+comma) held open 6 s while a CD WAV plays (44.1 kHz,
# resampled on the DSP: the 68030's heaviest WAV, and one Hatari 2.4
# records, where its DMA's four tracks may leave the DAC on a silent one):
# the dialog's wait goes on feeding the queue (common's dlg_idle: the hub,
# 4 Nov 2026, from 0.22's 451 underruns with modal dialogs open), so the
# queue's lead (3 s) never runs out
CASES["settings"] = ("CD.WAV", ["-r", "44100", "-c", "2", "-b", "16"], None, 49170, (1000, 1500), 14)
# phase 9 (Erez, 9 Nov 2026): a VGM and a VGZ on the YM2149, a 1 kHz square
# wave (the chip's), its file's AY at 1.7734 MHz turned to the Atari's 2 MHz
CASES["vgm"] = ("TONE.VGM", ["-r", "44100", "-c", "1", "-b", "16"], None, 44100, (1000, 1000), 3)
CASES["vgz"] = ("TONE.VGZ", ["-r", "44100", "-c", "1", "-b", "16"], None, 44100, (1000, 1000), 3)
CASES["vgmsn"] = ("SN.VGM", ["-r", "44100", "-c", "1", "-b", "16"], None, 44100, (999, 999), 3)
# phase 10 (Erez, 10 Nov 2026): the Mega Drive's YM2612 on the DSP at
# 24,585 Hz, algorithm 7's operator 4 alone, a sine at 999.7 Hz (F-number
# 1230, block 5, at the NTSC clock)
CASES["vgmopn"] = ("OPN.VGM", ["-r", "44100", "-c", "1", "-b", "16"], None, 44100, (1000, 1000), 3)
# a Mega Drive file whose sound is its SN76489 (period 112: 998.8 Hz), the
# YM2612 silent: the PSG in opn.a56, not sn.a56
CASES["vgmmd"] = ("MD.VGM", ["-r", "44100", "-c", "1", "-b", "16"], None, 44100, (999, 999), 3)
# step 7 (Erez, 12 Nov 2026): the YM3812 (OPL2) on the DSP at 24,585 Hz,
# channel 0's carrier at full level (its modulator at TL 63), a sine at
# 999.9 Hz (F-number 659, block 5, at 3,579,545 Hz)
CASES["vgmopl"] = ("OPL.VGM", ["-r", "44100", "-c", "1", "-b", "16"], None, 44100, (1000, 1000), 3)
# step 7b (Erez, 14 Nov 2026): the YMF262 (OPL3) at 16,390 Hz, its second
# bank keyed: the same sine on its channel 9 (port 1's channel 0), NEW set,
# to the left and the right
CASES["vgmopl3"] = ("OPL3.VGM", ["-r", "44100", "-c", "1", "-b", "16"], None, 44100, (1000, 1000), 3)
# the same, its second bank keyed past the start EPlay looks through (64
# KB of other writes first): nine channels at 24,585 Hz, then opened again
# with 18 at 16,390 Hz at the key
CASES["vgmopl3late"] = ("OPL3L.VGM", ["-r", "44100", "-c", "1", "-b", "16"], None, 44100, (1000, 1000), 4)
# where the DSP falls behind, the rest of the track at the next lower
# rate (Erez, 14 Nov 2026): eplay.inf's VGMLOW=2048 (the rigs') takes any
# lead as behind, so the OPL2's sine goes on at 19,668 Hz past its first
# second, then 16,390 past its second, the tone whole
CASES["vgmlow"] = ("LOW.VGM", ["-r", "44100", "-c", "1", "-b", "16"], None, 44100, (1000, 1000), 4)
# the same for a WAV on the TT's or STE's 8-bit DMA (theirs alone):
# eplay.inf's DMA8LOW=0 (the rigs') takes the CPU as behind, so a CD's 44.1
# kHz goes on at 25,033 Hz past its first second, then 12,517
# (Erez, 14 Nov 2026: a station through the FDI stalled twice) an MP3 on
# the DSP that stalls (eplay.inf's STALLAT=2000, the rigs': taken as
# stalled at 2 s) goes on from there, opened again, to its end
CASES["stall"] = ("STALL.MP3", ["-r", "44100", "-c", "2", "-b", "16"], ["-b", "128"], 49170, (1000, 1500), 6)
CASES["wavlow"] = ("LOW.WAV", ["-r", "44100", "-c", "2", "-b", "16"], None, 50066, (1000, 1500), 4)
# a real file, never in the repo (a game's music: EPLAY_RIG_VGM names it, a
# VGM or VGZ kept outside), played for EPLAY_RIG_SECS (20) and quit: does
# its place keep up with the clock, and the DSP's ring (EPlay 1.18 on Erez's
# Falcon: 6.4 s of music in 15, the ring dry)
CASES["vgmreal"] = ("REAL.VGZ", None, None, 0, (0, 0), 0)
SQUARE = {"vgm", "vgz", "vgmsn", "vgmmd"}
# the VGM cases of the DSP's chips (refused on a machine without one)
DSP_VGM = {"vgmsn", "vgmopn", "vgmmd", "vgmopl", "vgmopl3", "vgmopl3late", "vgmlow"}
		# (a square wave: 81 % of its power in the tone)
# the cases' sounds, where not sine tones
SYNTH = {"auto": ["square", "100", "pinknoise"]}
# three files given at the start (as the desktop gives files dropped on the
# program): the list holds them and plays them one after another (Erez,
# 3 Nov 2026: files added in bulk). Each a second, A and C WAVs, B an MP3
LIST = [("A.WAV", None), ("B.MP3", ["-b", "128"]), ("C.WAV", None)]
CASES["list"] = ("A.WAV", ["-r", "24585", "-c", "2", "-b", "16"], None, 24585, (1000, 1500), 1)
# gapless (phase 8a): one tone of 6 s cut into two WAVs of 3 s, given
# together: the second follows on in the queue, the sound 6 s without a
# gap, and EPlay says it followed on
CASES["gapless"] = ("G1.WAV", ["-r", "24585", "-c", "2", "-b", "16"], None, 24585, (1000, 1500), 6)
# the same with MP3s (44.1 kHz, LAME's Info frame each): the DSP's stream
# goes on, not loaded again, and the encoder's delay and padding (LAME's
# tag) are left out on the DSP (PK_DROP): the tone whole through the join
CASES["gaplessmp3"] = ("G1.MP3", ["-r", "44100", "-c", "2", "-b", "16"], ["-b", "128"], 49170,
                       (1000, 1500), 6)
# a folder of 1,000 files given (as dropped on the program): all in the
# list, their tags and lengths read, and the memory it takes (the hub, 4
# Nov 2026: the free ST RAM a menu needs under NVDI). Not by default (slow)
CASES["many"] = ("A.WAV", ["-r", "24585", "-c", "2", "-b", "16"], None, 24585, (1000, 1500), 1)
# phase 4's long lists, as many: a playlist of 1,000 (47 KB, read a piece
# at a time; ten folders of 100) and a tree of 1,000 (the same ten folders, given whole)
CASES["manym3u"] = CASES["many"]
CASES["manytree"] = CASES["many"]
NOT_DEFAULT = {"many", "manym3u", "manytree", "miniwav", "vgmreal"}
# phase 4 (Erez, 4 Nov 2026), each a second's files from A.WAV: (the files
# made, relative to C:\ and lame's arguments or None; what EPlay is given;
# the files played, in turn; whether it ends then). A playlist with a file
# in a folder; a folder with its subfolders (in path order); Repeat all
# (two files: the first again, then stopped by the rig); Shuffle (four
# files each once, in some order, then the end)
ORDERED = {
    "m3u": ([("A.WAV", None), ("SUB\\B.MP3", ["-b", "128"]), ("C.WAV", None),
             ("LIST.M3U", "#EXTM3U\r\n#EXTINF:1,A\r\nA.WAV\r\nSUB/B.MP3\r\n\r\nC:\\C.WAV\r\n")],
            "C:\\LIST.M3U", ["A.WAV", "B.MP3", "C.WAV"], True),
    "tree": ([("TREE\\B\\T2.WAV", None), ("TREE\\A\\T1.WAV", None), ("TREE\\T0.WAV", None),
              ("TREE\\A\\NOTES.TXT", "not music\r\n")],
             "C:\\TREE\\", ["T1.WAV", "T2.WAV", "T0.WAV"], True),   # (path order: A\\, B\\, then T0: 'A' < 'T')
    "repeat": ([("A.WAV", None), ("C.WAV", None)], "C:\\A.WAV C:\\C.WAV", ["A.WAV", "C.WAV", "A.WAV"], False),
    "shuffle": ([("T1.WAV", None), ("T2.WAV", None), ("T3.WAV", None), ("T4.WAV", None)],
                "C:\\T1.WAV C:\\T2.WAV C:\\T3.WAV C:\\T4.WAV", None, True),
}
for _c in ORDERED:
    CASES[_c] = CASES["list"]
# File > Save playlist (^S): two files given, one in a folder; ^S, and the
# file selector's name as EPlay gives it (PLAYLIST.M3U) taken with Return:
# the playlist at C:\ with both, relative to it
CASES["save"] = CASES["list"]
# the music's keys (Erez, 5 Nov 2026): three 30 s WAVs given; Right twice
# (20 s on), Left once (back to about 10 s), Shift+Right (the next file),
# Shift+Left (back to the first), Clr Home (stopped)
CASES["keys"] = ("K1.WAV", ["-r", "24585", "-c", "2", "-b", "16"], None, 24585, (1000, 1500), 30)
# several rows (D21, 1.14): the three files given; Down selects the second
# and Del takes it out (2 left); ^A selects both, Del takes them out, the
# one playing stopped (none left)
CASES["selall"] = CASES["keys"]
# the cases' eplay.inf
INFS = {"stall": "STALLAT=2000\r\n\r\n", "vgmlow": "VGMLOW=2048\r\n\r\n", "wavlow": "DMA8LOW=0\r\n\r\n", "repeat": "REPEAT=ALL\r\n\r\n", "shuffle": "SHUFFLE=1\r\n\r\n", "aiff44": "OUTPUT=DSP\r\n\r\n", "auto": "QLATE=0\r\n\r\n", "wavvol": "VOLUME=8\r\n\r\n", "mini": "MINI=1\r\n\r\n",
        "miniwav": "MINI=1\r\n\r\n"}
# the mini player (eplay.inf's MINI=1): the bar alone, playing as ever
CASES["mini"] = CASES["mp3"]
# and on a machine without a DSP (a TT, an STE), a WAV in it
CASES["miniwav"] = CASES["wav"]
# ReplayGain (phase 8b): the file tagged (ID3v2.3's TXXX; a WAV's in its
# "id3 " chunk) and its level against the untagged file's, in dB: -6.02 dB
# is 4 steps down; +6.00 dB with a peak of 0.502 (sox's -6 dB tone) is held
# by the peak to 3 steps up, +4.5 dB, and never clips
CASES["wavrg"] = CASES["wav"]
CASES["wavrgup"] = CASES["wav"]
CASES["mp3rg"] = CASES["mp3"]
RG_TAG = {"wavrg": ("-6.02 dB", None), "wavrgup": ("+6.00 dB", "0.502"), "mp3rg": ("-6.02 dB", None)}
RG_WANT = {"wavrg": ("wav", -6.0), "wavrgup": ("wav", 4.5), "mp3rg": ("mp3", -6.0)}


def id3_rg(gain, peak):
    """an ID3v2.3 tag with REPLAYGAIN_TRACK_GAIN (and _PEAK) in TXXX frames"""
    body = b""
    for k, v in (("REPLAYGAIN_TRACK_GAIN", gain), ("REPLAYGAIN_TRACK_PEAK", peak)):
        if v is None:
            continue
        d = b"\x00" + k.encode() + b"\x00" + v.encode()
        body += b"TXXX" + len(d).to_bytes(4, "big") + b"\x00\x00" + d
    n = len(body)
    size = bytes([(n >> 21) & 127, (n >> 14) & 127, (n >> 7) & 127, n & 127])
    return b"ID3\x03\x00\x00" + size + body


def add_rg(path, case):
    gain, peak = RG_TAG[case]
    tag = id3_rg(gain, peak)
    data = open(path, "rb").read()
    if data[:4] == b"RIFF":
        if len(tag) & 1:
            tag += b"\x00"
        data += b"id3 " + len(tag).to_bytes(4, "little") + tag
        data = data[:4] + (len(data) - 8).to_bytes(4, "little") + data[8:]
    else:
        data = tag + data
    open(path, "wb").write(data)


# the window's saved place (WINDOW=: D43): it opens there, the height
# taken in whole rows
CASES["place"] = CASES["wav"]
INFS["place"] = "WINDOW=24,96,480,330\r\n\r\n"
# the first start (1.16, D39): no place kept, the window at the desk's
# whole height, centred across (its width design D's)
CASES["firstrun"] = CASES["wav"]
# a quit while it plays (^Q): each step of the way out in EPLAY.LOG, the
# crash catcher off (the hub, 10 Nov 2026: EFTP's and EMail's quit bombs,
# the catcher hooked over), no CRASH.LOG
CASES["quit"] = CASES["wav"]
QUIT_STEPS = ("the sound stopped", "settings saved", "the window closed", "colours, menu bar and VDI given back",
              "crash catcher off", "out of the AES")

# the level of the middle (RMS), each case's, for wavvol's against wav's
LEVEL = {}


def opn_cmds():
    """the YM2612's writes for a sine at 999.7 Hz on channel 1: algorithm
    7, operators 1-3 silent (TL 127), operator 4 at full level, its attack
    at once, held"""
    w = lambda r, v: bytes([0x52, r, v])
    c = w(0x22, 0) + w(0x27, 0) + w(0x2b, 0) + w(0xb0, 0x07) + w(0xb4, 0xc0)
    for slot, tl in ((0x00, 127), (0x04, 127), (0x08, 127), (0x0c, 0)):    # (operators 1, 3, 2, 4)
        c += w(0x30 + slot, 0x01) + w(0x40 + slot, tl) + w(0x50 + slot, 0x1f) + w(0x60 + slot, 0)
        c += w(0x70 + slot, 0) + w(0x80 + slot, 0x0f)
    return c + w(0xa4, (5 << 3) | (1230 >> 8)) + w(0xa0, 1230 & 255) + w(0x28, 0xf0)


def opl_cmds(opl3=False):
    """the YM3812's writes for a sine at 999.9 Hz on channel 0: both
    operators out, the modulator at TL 63 (-47 dB), the carrier at full
    level, their attacks at once, held. opl3: the YMF262's, on its second
    bank's first channel, NEW set, to both outputs"""
    if opl3:
        w = lambda r, v: bytes([0x5f, r, v])
        c = w(0x05, 0x01) + bytes([0x5e, 0xbd, 0]) + w(0xc0, 0x31)
    else:
        w = lambda r, v: bytes([0x5a, r, v])
        c = w(0x01, 0x20) + w(0xbd, 0) + w(0xc0, 0x01)
    for slot, tl in ((0, 63), (3, 0)):
        c += w(0x20 + slot, 0x21) + w(0x40 + slot, tl) + w(0x60 + slot, 0xf0) + w(0x80 + slot, 0x0f)
        c += w(0xe0 + slot, 0)
    return c + w(0xa0, 659 & 255) + w(0xb0, 0x20 | 5 << 2 | 659 >> 8)


def make_vgm(path, secs, gz, sn=False, opn=False, md=False, opl=False, opl3=False, filler=0):
    """a VGM (phase 9): channel A of an AY at the Spectrum's 1,773,400 Hz,
    a 1 kHz tone there (period 111: 998.6 Hz; 125 at the Atari's 2 MHz,
    1000 Hz), secs long, then silent; gzipped as a VGZ. sn: the SN76489
    at the Master System's 3,579,545 Hz instead, tone 0 at period 112
    (998.8 Hz), the others silent (on the DSP)"""
    import gzip
    if opl or opl3:
        cmds = bytes([0x5e, 0x00, 0x00]) * (filler // 3) + opl_cmds(opl3)     # (register 0: nothing)
    elif opn:
        cmds = opn_cmds()
    elif sn or md:
        cmds = bytes([0x50, 0xbf, 0x50, 0xdf, 0x50, 0xff, 0x50, 0x80, 0x50, 0x07, 0x50, 0x90])
    else:
        cmds = bytes([0xa0, 7, 0x3e, 0xa0, 0, 111, 0xa0, 1, 0, 0xa0, 8, 15])
    left = secs * 44100
    while left > 0:
        k = min(left, 44100)
        cmds += bytes([0x61, k & 0xff, k >> 8])
        left -= k
    cmds += bytes([0x5f, 0xb0, 5 << 2 | 659 >> 8, 0x66] if opl3 else [0x5a, 0xb0, 5 << 2 | 659 >> 8, 0x66] if opl else [0x52, 0x28, 0x00, 0x66] if opn else [0x50, 0x9f, 0x66] if sn or md else [0xa0, 8, 0, 0x66])
    head = bytearray(0x80)
    head[0:4] = b"Vgm "
    struct.pack_into("<I", head, 0x04, 0x80 + len(cmds) - 4)
    struct.pack_into("<I", head, 0x08, 0x151)
    struct.pack_into("<I", head, 0x18, secs * 44100)
    struct.pack_into("<I", head, 0x34, 0x80 - 0x34)
    if opl3:
        struct.pack_into("<I", head, 0x5c, 14318180)
    elif opl:
        struct.pack_into("<I", head, 0x50, 3579545)
    elif md:
        struct.pack_into("<I", head, 0x2c, 7670453)
        struct.pack_into("<I", head, 0x0c, 3579545)
        struct.pack_into("<H", head, 0x28, 0x0009)
        head[0x2a] = 16
    elif opn:
        struct.pack_into("<I", head, 0x2c, 7670453)
    elif sn:
        struct.pack_into("<I", head, 0x0c, 3579545)
    else:
        struct.pack_into("<I", head, 0x74, 1773400)
    data = bytes(head) + cmds
    with open(path, "wb") as fh:
        fh.write(gzip.compress(data, mtime=0) if gz else data)


def make_file(hd, case):
    f, fmt, lame, _, _, secs = CASES[case]
    if case == "vgmreal":
        shutil.copy(os.environ["EPLAY_RIG_VGM"], os.path.join(hd, f))
        return f
    if f.endswith((".VGM", ".VGZ")):
        make_vgm(os.path.join(hd, f), secs, f.endswith(".VGZ"), case == "vgmsn", case == "vgmopn", case == "vgmmd",
                 case in ("vgmopl", "vgmlow"), case in ("vgmopl3", "vgmopl3late"),
                 70 * 1024 if case == "vgmopl3late" else 0)
        return f
    src = os.path.join(hd, "..", "src.wav")
    synth = ["synth", str(secs)] + SYNTH.get(case, ["sine", "1000"] + (["sine", "1500"] if fmt[3] == "2" else []))
    out = os.path.join(hd, f)
    if lame:
        subprocess.run(["sox", "-n"] + fmt + [src] + synth + ["gain", "-6"], check=True)
        tag = [] if "XING" in lame else ["-t"]          # (-t: no header frame)
        subprocess.run(["lame"] + tag + ["--quiet", "--noreplaygain"] + [a for a in lame if a != "XING"] +
                       [src, out], check=True)
    else:
        subprocess.run(["sox", "-n"] + fmt + [out] + synth + ["gain", "-6"], check=True)
    if case in RG_TAG:
        add_rg(out, case)
    if case == "addplay":
        shutil.copy(out, os.path.join(hd, "ANOTHER.MP3"))     # (the file Add files adds)
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


RESAMPLED = {"wav22", "aiff44", "settings", "vgmsn"}       # (and every MP3; the SN76489 the DSP's)
# wav's level when its own run recorded nothing (excused: Hatari's DMA on
# a silent track) or didn't run: the tone's own, sox's sine at -6 dB
# (0.501 of full scale, its RMS that over the square root of 2). Hatari
# records the DAC as the samples are: the hub's wavvol at 8 (12 dB down)
# read 2917, -12.0 dB from it (TOS 4.04, 6 Nov 2026). It was 0, so wavvol
# alone, or after an excused wav, read -99 dB
WAV_LEVEL = 32767 * 10 ** (-6 / 20) / math.sqrt(2)


# a check given up, with why (guideline 1.2, AERA-Atari 3bdb497: never
# "passed" over a skipped step): printed as SKIPPED and counted
SKIPS = []


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
        # (MP3CPU=1, play060.py's: an MP3 decoded on the CPU goes through the DMA too)
        cpu_mp3 = "MP3CPU=1" in os.environ.get("EPLAY_RIG_INF", "")
        # (phase 11: a VGM's chips made on the CPU play from the DMA too)
        cpu_mp3 = cpu_mp3 or ("VGMCPU=1" in os.environ.get("EPLAY_RIG_INF", "") and case in DSP_VGM)
        if hatari_keeps_parity() and (cpu_mp3 or (not CASES[case][2] and case not in RESAMPLED)):
            # the sound DMA plays four tracks, the music in the first
            # (Erez's Analog 8: channels 3 to 8 silent). Hatari 2.4 keeps
            # the DMA's word count from its last stop (EPlay's clock
            # measurement), so its DAC hears another track: silence. Later
            # Hatari starts each DMA at the first word ("1st frame is
            # left"), as the Falcon does
            SKIPS.append("%s: the sound itself (Hatari %s's DMA started mid-frame, its DAC on a silent track)" %
                         (case, HATARI))
            return True, "no sound recorded: Hatari %s's DMA (the sound's checks skipped)" % HATARI
        return False, "no sound recorded (%d frames at %d Hz)" % (n, rate)
    a, b = loud[0], loud[-1]
    secs = (b - a + 1) / 50
    gaps = [i for i in range(a, b + 1) if rms[i] < 1000]
    # the tone's window (100 ms) at the middle, moved off a key's click
    # (Hatari records the keyboard's click: Return in fsel's case once fell
    # in it, the tone 78 % of the power): the nearest blocks whose high
    # frequencies are the music's alone
    def hf(i):
        seg = left[i * blk:(i + 1) * blk]
        return math.sqrt(sum((seg[k] - seg[k - 1]) ** 2 for k in range(1, len(seg))) / len(seg))
    c = (a + b) // 2
    base = sorted(hf(i) for i in range(c - 10, c + 11))[10]
    for d in range(0, (b - a) // 2 - 3):
        m = c + (d // 2 + 1 if d % 2 else -(d // 2))
        if all(hf(i) < base * 1.1 + 50 for i in range(m - 3, m + 4)):
            c = m
            break
    mid = c * blk
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
    if case in RG_WANT:
        ref, want_db = RG_WANT[case]
        full = LEVEL.get(ref, WAV_LEVEL if ref == "wav" else 0)
        if full > 0:
            db = 20 * math.log10(level / full) if level > 0 else -99
            vol_ok = abs(db - want_db) < 1
            vol_note = "; ReplayGain: the level %+.1f dB from %s's (%+.1f dB wanted)" % (db, ref, want_db)
        else:
            vol_note = "; ReplayGain: no %s run to measure against (run %s first)" % (ref, ref)
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
        SKIPS.append("%s: which channel is which (Hatari %s's DMA swapped them)" % (case, HATARI))
    if not pitch and hatari_keeps_parity() and not CASES[case][2] and case not in RESAMPLED and \
            ((abs(fl - want[1]) < 8 and ql > 0.9) or (abs(fr - want[0]) < 8 and qr > 0.9)):
        # (four tracks a frame: Hatari 2.4's DAC a word off takes one
        # channel and a silent track's word: one side the other's tone,
        # the other silent)
        pitch, note = True, " (a word off: Hatari %s's DMA, four tracks)" % HATARI
        SKIPS.append("%s: one channel's tone (Hatari %s's DMA a word off)" % (case, HATARI))
        ql = qr = 1.0
    _, fmt, lame, out, _, length = CASES[case]
    if not lame and case not in RESAMPLED:
        length = length * int(fmt[1]) / out  # (the DMA: at the Falcon's rate, if not the file's)
    pure = 0.75 if case in SQUARE else 0.9
    ok = (vol_ok and abs(secs - length) < 0.1 and not gaps and pitch and ql > pure and qr > pure
          and max(rms[b + (4 if case in SQUARE else 2):] or [0]) < 50)
    # (silence after: past the block the end falls in; the YM2149's output
    # in Hatari dies away over a few ms more)
    return ok, ("%.2f s of sound, %d gaps; left %d Hz (%.0f %% of its power), right %d Hz (%.0f %%); "
                "wanted %.2f s, %.0f and %.0f Hz%s%s; level %.0f" % (secs, len(gaps), fl, 100 * ql, fr, 100 * qr,
                                                             length, want[0], want[1], note, vol_note, level))


HATARI = subprocess.run(["hatari", "--version"], capture_output=True, text=True).stdout.split()[1].lstrip("v")


def hatari_keeps_parity():
    return tuple(int(x) for x in HATARI.split(".")[:2]) <= (2, 4)


def no_dsp(spec):
    """a TT or an STE: no DSP, MP3 files passed over (phase 6)"""
    return spec.split(":")[0] in ("TT", "STE", "STE-1")


def run(work, case, spec):
    w = os.path.join(work, spec.replace(":", "-") + "-" + case)
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(hd)
    # (an STE runs the 68000 edition under its own name, as it is shipped)
    prg = "eplayst.prg" if spec.startswith("STE") else "eplay.prg"
    shutil.copy(os.path.join(TOP, "binary", prg), os.path.join(hd, prg))
    # (MP3FPU.OVL beside it, as a release has it: a 68060 with no DSP decodes MP3 with it)
    if os.path.exists(os.path.join(TOP, "binary", "mp3fpu.ovl")):
        shutil.copy(os.path.join(TOP, "binary", "mp3fpu.ovl"), hd)
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
    if case == "gaplessmp3":
        src = os.path.join(w, "src.wav")      # (make_file's tone, whole)
        for k, (a, b) in enumerate((("0", "3"), ("3", None))):
            half = os.path.join(w, "half%d.wav" % k)
            subprocess.run(["sox", src, half, "trim", a] + ([b] if b else []), check=True)
            subprocess.run(["lame", "--quiet", "--noreplaygain", "-b", "128", half,
                            os.path.join(hd, "G%d.MP3" % (k + 1))], check=True)
        tail = "C:\\G1.MP3 C:\\G2.MP3"
    if case == "gapless":
        whole = os.path.join(w, "whole.wav")
        shutil.move(os.path.join(hd, "G1.WAV"), whole)
        subprocess.run(["sox", whole, os.path.join(hd, "G1.WAV"), "trim", "0", "3"], check=True)
        subprocess.run(["sox", whole, os.path.join(hd, "G2.WAV"), "trim", "3"], check=True)
        tail = "C:\\G1.WAV C:\\G2.WAV"
    if case in ("keys", "selall"):
        for n in ("K2.WAV", "K3.WAV"):
            shutil.copy(os.path.join(hd, "K1.WAV"), os.path.join(hd, n))
        tail = "C:\\K1.WAV C:\\K2.WAV C:\\K3.WAV"
    if case == "save":
        os.makedirs(os.path.join(hd, "SUB"))
        shutil.copy(os.path.join(hd, "A.WAV"), os.path.join(hd, "SUB", "B.WAV"))
        tail = "C:\\A.WAV C:\\SUB\\B.WAV"
    if case in ORDERED:
        src = os.path.join(hd, "A.WAV")
        for name, how in ORDERED[case][0]:
            dst = os.path.join(hd, *name.split("\\"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if isinstance(how, str):
                with open(dst, "w", newline="") as fh:
                    fh.write(how)
            elif how:
                subprocess.run(["lame", "--quiet", "--noreplaygain"] + how + [src, dst], check=True)
            elif dst != src:
                shutil.copy(src, dst)
        tail = ORDERED[case][1]
    if case == "many":
        many = os.path.join(hd, "MANY")
        os.makedirs(many)
        for i in range(1000):
            shutil.copy(os.path.join(hd, "A.WAV"), os.path.join(many, "T%04d.WAV" % i))
        tail = "C:\\MANY\\"
    if case in ("manym3u", "manytree"):
        names = ["D%d\\T%04d.WAV" % (i // 100, i) for i in range(1000)]
        for n in names:
            dst = os.path.join(hd, "MANY", *n.split("\\"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy(os.path.join(hd, "A.WAV"), dst)
        if case == "manym3u":
            with open(os.path.join(hd, "MANY", "ALL.M3U"), "w", newline="") as fh:
                # (as EPlay saves one: an #EXTINF line each; half the paths
                # whole, half relative: 47 KB, more than a piece of 16 KB)
                fh.write("#EXTM3U\r\n" + "".join(
                    "#EXTINF:1,The rig - Track %d\r\n%s\r\n" % (i, ("C:\\MANY\\" + n) if i % 2 else n.replace("\\", "/"))
                    for i, n in enumerate(names)))
            tail = "C:\\MANY\\ALL.M3U"
        else:
            tail = "C:\\MANY\\"
    with open(os.path.join(hd, "TAIL.TXT"), "w") as fh:
        fh.write(tail + "\r\n")
    if case in INFS:
        with open(os.path.join(hd, "eplay.inf"), "w", newline="") as fh:
            fh.write(INFS[case])
    if os.environ.get("EPLAY_RIG_INF_FILE"):
        # (inf_test.py's: this file as eplay.inf, its date kept)
        shutil.copy2(os.environ["EPLAY_RIG_INF_FILE"], os.path.join(hd, "eplay.inf"))
    if os.environ.get("EPLAY_RIG_INF"):
        # (lines added to eplay.inf: play060.py's MP3CPU=1)
        with open(os.path.join(hd, "eplay.inf"), "a", newline="") as fh:
            fh.write(os.environ["EPLAY_RIG_INF"].replace(";", "\r\n") + "\r\n")
    m = machines.Machine(spec, hd)
    args = m.args()
    rec = os.path.join(w, "rec.wav")
    with open(os.path.join(m.home, ".config", "hatari", "hatari.cfg"), "w") as fh:
        fh.write("[Sound]\nszYMCaptureFileName = %s\n" % rec)
    fifo, shots = os.path.join(w, "cmd.fifo"), os.path.join(w, "shots")
    os.makedirs(shots)
    log = os.path.join(w, "hatari.log")
    if os.environ.get("EPLAY_RIG_PARSE"):      # (debugger commands at the start: a profile)
        args = args + ["--parse", os.environ["EPLAY_RIG_PARSE"]]
    args = ["hatari"] + args + ["--sound", str(REC_RATE), "--fast-boot", "yes", "--natfeats", "yes",
                                "--cmd-fifo", fifo, "--screenshot-dir", shots, "--log-level", "warn",
                                "--statusbar", "no", "--auto", "C:\\LAUNCH.PRG"]
    p = subprocess.Popen(args, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    try:
        if not wait_line(log, lambda l: l.startswith("LAUNCH shel_write"), 120):
            return False, m.describe(), "the launcher never ran"
        cmd(fifo, "hatari-shortcut", "recsound")
        played = wait_line(log, lambda l: l.startswith(("EPLAY playing", "EPLAY refused", "EPLAY no sound")), 60)
        if no_dsp(spec) and played and CASES[case][0].endswith(".MP3"):
            # (no DSP: an MP3 refused, in Erez's words, 7 Nov 2026)
            return "MP3 needs a Falcon's DSP" in played, m.describe(), "refused without a DSP: " + played
        if no_dsp(spec) and played and case in DSP_VGM:
            # (no DSP: the DSP's chips' music refused, as their own words say)
            return "needs the Falcon's DSP" in played, m.describe(), "refused without a DSP: " + played
        if not played or not played.startswith("EPLAY playing"):
            return False, m.describe(), played or "nothing played"
        time.sleep(1.5)
        cmd(fifo, "hatari-shortcut", "screenshot")
        if case == "firstrun":
            wl = wait_line(log, lambda l: l.startswith("EPLAY window "), 5) or ""
            v = [int(x) for x in wl.split()[2:6]] if wl else [0, 0, 0, 0]
            left, right = v[0], 640 - (v[0] + v[2])
            if not (v[1] < 80 and v[1] + v[3] > 420 and abs(left - right) <= 24):
                return False, m.describe(), "a first start: the work area at %s (wanted the desk's height, centred)" % wl
        if case == "vgmreal":
            secs = float(os.environ.get("EPLAY_RIG_SECS", "20"))
            for _ in range(int(os.environ.get("EPLAY_RIG_SHOTS", "0"))):   # (the title's scroll, a shot a second)
                cmd(fifo, "hatari-shortcut", "screenshot")
                time.sleep(1)
                secs -= 1
            time.sleep(max(secs, 0))
            cmd(fifo, "hatari-event", "keydown", "0x1d")
            cmd(fifo, "hatari-event", "keypress", "0x10")
            cmd(fifo, "hatari-event", "keyup", "0x1d")
            wait_line(log, lambda l: l.startswith("LOG quit: out of the AES"), 30)
            got = [l.strip()[4:] for l in open(log, errors="replace")
                   if l.startswith("LOG ") and ("stopped at" in l or "least lead" in l or "VGM" in l)]
            return True, m.describe(), "after %.0f s of the host's clock: %s" % (secs, " | ".join(got))
        if case == "quit":
            cmd(fifo, "hatari-event", "keydown", "0x1d")
            cmd(fifo, "hatari-event", "keypress", "0x10")
            cmd(fifo, "hatari-event", "keyup", "0x1d")
            out = wait_line(log, lambda l: l.startswith("LOG quit: out of the AES"), 20)
            time.sleep(1)
            steps = [l.strip()[len("LOG quit: "):] for l in open(log, errors="replace") if l.startswith("LOG quit: ")]
            missing = [q for q in QUIT_STEPS if not any(x.startswith(q) for x in steps)]
            crashed = machines.ci_path(hd, "CRASH.LOG")
            # (the catcher left in a chain it couldn't walk: the vector
            # leads into EPlay's memory once it has gone)
            stuck = any("not taken off" in x for x in steps)
            if not out or missing or stuck or os.path.exists(crashed):
                return False, m.describe(), "quit: %s%s" % ("; ".join(steps) or "no quit lines",
                                                            ", CRASH.LOG written" if os.path.exists(crashed) else "")
            return True, m.describe(), "quit: " + "; ".join(steps)
        if case == "place":
            wl = wait_line(log, lambda l: l.startswith("EPLAY window "), 5) or ""
            v = [int(x) for x in wl.split()[2:6]] if wl else [0, 0, 0, 0]
            if not (24 <= v[0] <= 40 and 96 < v[1] <= 140 and 250 <= v[3] <= 330):
                return False, m.describe(), "WINDOW=24,96,480,330: the work area at %s" % wl
        if case == "fsel":
            cmd(fifo, "hatari-event", "keydown", "0x1d")
            cmd(fifo, "hatari-event", "keypress", "0x18")
            cmd(fifo, "hatari-event", "keyup", "0x1d")
            time.sleep(2)
            cmd(fifo, "hatari-shortcut", "screenshot")
            time.sleep(3)
            # Esc empties the filter and leaves it open (D28); Undo cancels:
            # closed, nothing added (Return adds the highlighted file: on
            # TOS 4.04 LONG.MP3 again, so it never ended: the hub, 5 Nov)
            cmd(fifo, "hatari-event", "keypress", "0x01")
            time.sleep(1)
            early = wait_line(log, lambda l: l.startswith("EPLAY list: "), 2)
            if early:
                return False, m.describe(), "Esc closed Add files: %s (it only empties the filter: D28)" % early
            cmd(fifo, "hatari-event", "keypress", "0x61")
            added = wait_line(log, lambda l: l.startswith("EPLAY list: "), 10) or "EPLAY list: ? files"
            if added.split()[2] != "1":
                return False, m.describe(), "Undo in Add files: %s (wanted 1 file, nothing added)" % added
        if case == "addplay":
            time.sleep(1)       # (once it plays; Hatari runs fast: the 10 s go by in some 5)
            def ctrl(scan):
                # (Control held as a hand holds it: the AES gives the shift
                # state when EPlay reads the key)
                cmd(fifo, "hatari-event", "keydown", "0x1d")
                time.sleep(0.3)
                cmd(fifo, "hatari-event", "keypress", scan)
                time.sleep(1)
                cmd(fifo, "hatari-event", "keyup", "0x1d")
            ctrl("0x18")        # ^O: Add files
            time.sleep(1.5)
            ctrl("0x1e")        # ^A: ANOTHER.MP3 and LONG.MP3 (already in the list) selected
            cmd(fifo, "hatari-shortcut", "screenshot")
            cmd(fifo, "hatari-event", "keypress", "0x1c")      # Return: Add
            added = wait_line(log, lambda l: l.startswith("EPLAY list: "), 10) or "EPLAY list: ? files"
            if added.split()[2] != "2":
                return False, m.describe(), "Add files' Return: %s (wanted 2 files: ANOTHER.MP3 added)" % added
            lines = open(log, errors="replace").read().split("\n")
            at_add = next(i for i, l in enumerate(lines) if l.startswith("EPLAY list: 2"))
            if any(l.startswith("LOG played to the end") for l in lines[:at_add]):
                return False, m.describe(), "LONG.MP3 ended before the add: not the case asked (while it plays)"
            again = None
            for _ in range(50):
                n = sum(1 for l in open(log, errors="replace") if l.startswith("EPLAY playing: "))
                if n >= 2:
                    again = n
                    break
                time.sleep(0.2)
            if not again:
                return False, m.describe(), "the file added while one played didn't play (File > Open as Open URL)"
            row = wait_line(log, lambda l: l.startswith("EPLAY status: Playing 2 of 2"), 10)
            row = row if row and "playing: ANOTHER.MP3" in open(log, errors="replace").read() else None
            return bool(row), m.describe(), "addplay: ANOTHER.MP3 added while LONG.MP3 played: %s; %s" % (
                added, row or "not \"Playing 2 of 2\"")
        if case == "settings":
            # (once the music is heard: the DSP's program is sent first, and
            # Control+comma's key click before the music reads as a gap)
            time.sleep(2)
            cmd(fifo, "hatari-event", "keydown", "0x1d")
            cmd(fifo, "hatari-event", "keypress", "0x33")    # Control+comma: Settings
            cmd(fifo, "hatari-event", "keyup", "0x1d")
            time.sleep(2)
            cmd(fifo, "hatari-shortcut", "screenshot")
            time.sleep(4)
            # Undo: Cancel, nothing changed (since the Gateway's address, a
            # field: Esc empties a field, the family's 3.2)
            cmd(fifo, "hatari-event", "keypress", "0x61")
        if case in ("many", "manym3u", "manytree"):
            got = wait_line(log, lambda l: l.startswith("LOG the list: "), 240)
            start = wait_line(log, lambda l: l.startswith("LOG free memory"), 1) or ""
            ok = bool(got) and got.split()[3] == "1000"
            return ok, m.describe(), "%s\n     %s" % (start[4:], got[4:] if got else "the list never read")
        if case == "keys":
            def key(scan, shift=False):
                # (Shift held as a hand holds it: the AES gives the shift
                # state when the event is taken, and a busy 68000 takes it
                # after a Shift let go at once)
                if shift:
                    cmd(fifo, "hatari-event", "keydown", "0x2a")
                    time.sleep(0.3)
                cmd(fifo, "hatari-event", "keypress", scan)
                if shift:
                    time.sleep(0.3)
                    cmd(fifo, "hatari-event", "keyup", "0x2a")
                time.sleep(1.5)
            for k in ("0x4d", "0x4d", "0x4b"):
                key(k)
            key("0x4d", True)
            key("0x4b", True)
            key("0x47")
            time.sleep(1)
            lines = open(log, errors="replace").read().split("\n")
            went = [int(l.split()[3]) for l in lines if l.startswith("EPLAY went to ")]
            names = [l.split()[2] for l in lines if l.startswith("EPLAY playing: ")]
            stopped = any(l.startswith("EPLAY status: Stopped.") for l in lines)
            # (each key 1.5 s after the last: the place moves on meanwhile)
            ok = (len(went) == 3 and 10000 <= went[0] <= 14000 and 20000 <= went[1] <= 25000 and
                  10000 <= went[2] <= 16000 and names == ["K1.WAV", "K2.WAV", "K1.WAV"] and stopped)
            return ok, m.describe(), "went to %s ms; played %s; %s" % (
                went, " ".join(names), "stopped" if stopped else "never stopped")
        if case == "selall":
            cmd(fifo, "hatari-event", "keypress", "0x50")     # Down: the second row
            time.sleep(0.5)
            cmd(fifo, "hatari-event", "keypress", "0x53")     # Del
            one = wait_line(log, lambda l: l.startswith("EPLAY removed "), 10) or "EPLAY removed nothing"
            time.sleep(0.5)
            cmd(fifo, "hatari-event", "keydown", "0x1d")
            cmd(fifo, "hatari-event", "keypress", "0x1e")     # ^A: Select all
            cmd(fifo, "hatari-event", "keyup", "0x1d")
            time.sleep(1)
            cmd(fifo, "hatari-shortcut", "screenshot")
            cmd(fifo, "hatari-event", "keypress", "0x53")     # Del
            time.sleep(2)
            lines = [l.strip() for l in open(log, errors="replace") if l.startswith("EPLAY removed ")]
            stopped = any(l.startswith("EPLAY stopped") or "Stopped." in l for l in open(log, errors="replace"))
            ok = lines == ["EPLAY removed 1, list: 2 files", "EPLAY removed 2, list: 0 files"]
            return ok, m.describe(), "%s; stopped %s" % (" / ".join(lines) or one, stopped)
        if case == "save":
            cmd(fifo, "hatari-event", "keydown", "0x1d")
            cmd(fifo, "hatari-event", "keypress", "0x1f")     # ^S
            cmd(fifo, "hatari-event", "keyup", "0x1d")
            time.sleep(2.5)
            cmd(fifo, "hatari-shortcut", "screenshot")
            cmd(fifo, "hatari-event", "keypress", "0x1c")     # Return: the name as given
            saved = wait_line(log, lambda l: l.startswith("EPLAY saved "), 15)
            pl = os.path.join(hd, "PLAYLIST.M3U")
            text = open(pl, newline="").read() if os.path.exists(pl) else ""
            lines = [l for l in text.split("\r\n") if l and not l.startswith("#")]
            ok = bool(saved) and text.startswith("#EXTM3U\r\n#EXTINF:") and lines == ["A.WAV", "SUB\\B.WAV"]
            return ok, m.describe(), "%s; PLAYLIST.M3U: %s" % (saved or "never saved", " | ".join(lines) or "none")
        if case in ORDERED:
            files, _, want, ends = ORDERED[case]
            if no_dsp(spec):    # (the MP3s greyed and passed over: Erez, 7 Nov 2026)
                want = [f for f in want if not f.endswith(".MP3")] if want else want
                files = [(f, h) for f, h in files if not f.endswith(".MP3")]
            n = len(want) if want else len(files)
            played = lambda: [l.split()[2] for l in open(log, errors="replace") if l.startswith("EPLAY playing: ")]
            t_end = time.time() + 40
            while time.time() < t_end and len(played()) < n:
                time.sleep(0.5)
            end = wait_line(log, lambda l: l.startswith("EPLAY status: Played to the end"), 8) if ends else None
            names = played()
            if want:
                ok = names[:n] == want
            else:               # shuffle: each once
                ok = sorted(names[:n]) == sorted(f for f, _ in files) and len(names) == n
            ok = ok and (end is not None) == ends and (ends or len(names) >= n)
            return ok, m.describe(), "played: %s (wanted %s)%s" % (
                " ".join(names), " ".join(want) if want else "each once", "; ended" if end else "")
        if case == "list":
            # the three in turn, then the end: the playing lines in order
            last = wait_line(log, lambda l: l.startswith("EPLAY playing: " + LIST[-1][0]), 60)
            time.sleep(1.5)
            cmd(fifo, "hatari-shortcut", "screenshot")
            end = wait_line(log, lambda l: l.startswith("EPLAY status: Played to the end"), 30) if last else None
            names = [l.split()[2] for l in open(log, errors="replace") if l.startswith("EPLAY playing: ")]
            wanted = [n for n, _ in LIST if not (no_dsp(spec) and n.endswith(".MP3"))]
            ok = names == wanted and end is not None
            return ok, m.describe(), "played in turn: %s (wanted %s)%s" % (
                " ".join(names), " ".join(wanted), "" if end else "; never ended")
        end = wait_line(log, lambda l: l.startswith("EPLAY status: ") and "Playing" not in l and
                        "clock" not in l and "DMA sound" not in l, 120)
        time.sleep(1.0)
        cmd(fifo, "hatari-shortcut", "recsound")
        time.sleep(0.5)
        if not end:
            return False, m.describe(), played + "; never ended"
        if case == "stall":
            lines = [l[4:] for l in open(log, errors="replace").read().split("\n") if l.startswith("LOG ")]
            again = [l for l in lines if l.startswith("the DSP stalled: started again from")]
            ok = len(again) == 1 and "Played to the end" in end
            return ok, m.describe(), "%s\n     %s\n     %s" % (played[len("EPLAY playing: "):], end,
                                                       again[0] if again else "never started again")
        if case == "wavlow":
            lines = [l[4:] for l in open(log, errors="replace").read().split("\n") if l.startswith("LOG ")]
            why = [l for l in lines if " behind at " in l and "for this track" in l]
            to = [l for l in lines if " to the DMA's " in l]
            ok = (len(why) == 2 and "at 50066 Hz" in why[0] and "at 25033 Hz" in why[1] and
                  len(to) == 3 and "12517 Hz" in to[-1] and "Played to the end" in end)
            return ok, m.describe(), "%s\n     %s\n     %s\n     %s" % (
                played[len("EPLAY playing: "):], end, " / ".join(why) or "never lowered", " / ".join(to))
        if case in ("vgmlow", "vgmopl3late"):
            # (judged by the log, as auto: each change a moment's break)
            lines = [l[4:] for l in open(log, errors="replace").read().split("\n") if l.startswith("LOG VGM: ")]
            ons = [l for l in lines if " on the DSP at " in l]
            if case == "vgmlow":
                why = [l for l in lines if "the DSP behind at" in l]
                ok = len(why) == 3 and len(ons) == 4 and "at 12292 Hz" in ons[-1]
            else:
                why = [l for l in lines if "second bank keyed" in l]
                ok = len(why) == 1 and len(ons) == 2 and "at 24585 Hz" in ons[0] and "at 16390 Hz" in ons[-1]
            ok = ok and "Played to the end" in end
            return ok, m.describe(), "%s\n     %s\n     %s\n     %s" % (
                played[len("EPLAY playing: "):], end, " / ".join(why) or "never lowered",
                " / ".join(o[o.index(" on the DSP at ") + 15:] for o in ons))
        if case == "auto":
            # High first; behind, Normal from there; then to the end
            lines = open(log, errors="replace").read().split("\n")
            behind = [l for l in lines if l.startswith("LOG the DSP behind at 49,170 Hz")]
            outs = [l for l in lines if l.startswith("LOG output: ")]
            ok = (bool(behind) and len(outs) >= 2 and outs[0].startswith("LOG output: 49170 Hz") and
                  outs[-1].startswith("LOG output: 32780 Hz") and "15 kHz left out" in outs[-1] and
                  "Played to the end" in end)
            return ok, m.describe(), "%s\n     %s\n     %s\n     %s" % (
                played[len("EPLAY playing: "):], end, behind[0][4:] if behind else "never behind at High",
                " / ".join(o[4:o.index(";")] for o in outs))
        if not os.path.exists(rec):
            return False, m.describe(), "no recording"
        ok, what = analyse(rec, case)
        if case in ("gapless", "gaplessmp3"):
            followed = [l for l in open(log, errors="replace") if "(followed on)" in l]
            what += "; %s" % ("the second followed on" if followed else "the second didn't follow on")
            ok = ok and bool(followed)
            # the join itself, on the channel that has the sound (Hatari 2.4's
            # DMA may leave one silent): 6 s, no 5 ms block without it
            with wave.open(rec) as wv:
                r, ch, nf = wv.getframerate(), wv.getnchannels(), wv.getnframes()
                smp = struct.unpack("<%dh" % (nf * ch), wv.readframes(nf))
            best = None
            for c in range(ch):
                x = smp[c::ch]
                blk = r // 200
                rms = [math.sqrt(sum(v * v for v in x[i:i + blk]) / blk) for i in range(0, len(x) - blk, blk)]
                loud = [i for i, v in enumerate(rms) if v > 1000]
                if loud and (best is None or len(loud) > best[0]):
                    a, b = loud[0], loud[-1]
                    best = (len(loud), (b - a + 1) * 0.005, sum(1 for i in range(a, b + 1) if rms[i] < 1000))
            if best:
                what += "; %.3f s of sound, %d 5 ms blocks without it" % (best[1], best[2])
                # (MP3s too: the encoder's delay and padding, LAME's, left
                # out on the DSP, so the tone runs on through the join)
                ok = ok and abs(best[1] - 6.0) < 0.05 and best[2] == 0
            else:
                ok = False
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
    # (Erez's configuration by default, the hub's 10 Nov 2026: Falcon-4:256
    # with NVDI; EmuTOS without NVDI where the container has neither, a
    # stand-in, as describe() says)
    specs = [a for a in sys.argv[2:] if a not in CASES] or [machines.ERZ_MACHINE]
    nvdi_src = os.environ.get("AERA_NVDI", os.path.join(os.path.expanduser("~"), ".aera", "nvdi", "x"))
    if specs == [machines.ERZ_MACHINE] and os.path.exists(os.path.join(nvdi_src, "auto", "nvdi.prg")):
        os.environ.update(machines.erz_env())	# (no NVDI here: the same machine without it, a stand-in)
    failed = 0
    for spec in specs:
        for case in cases:
            if case == "mini" and no_dsp(spec):
                case = "miniwav"
            if case == "wavlow" and spec.split(":")[0] not in ("TT", "STE", "STE-1"):
                continue        # (the 8-bit DMA's: a Falcon's WAV keeps up on its DMA)
            n = len(SKIPS)
            ok, what, found = run(work, case, spec)
            print("%s %s on %s: %s" % ("ok  " if ok else "FAIL", case, what, found), flush=True)
            for r in SKIPS[n:]:
                print("SKIPPED: %s on %s" % (r, spec), flush=True)
            failed += not ok
    print("%d failed" % failed if failed else "passed, %d skipped" % len(SKIPS) if SKIPS else "all passed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
