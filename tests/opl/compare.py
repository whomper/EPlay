#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""compare.py - EPlay's YM3812 (OPL2) against the reference (phase 10, step 7): for
each test file, the reference's WAV (Nuked-OPL3 as an OPL2, at the chip's
49,716 Hz) and the DSP's arithmetic's (fast.c: src/opld.c and src/oplfast.c)
at the rate given: the pitch, the level (dB, the
middle), the loudness curve in 20 ms steps (its largest difference in
dB where the sound is above -40 dB), the first ten harmonics' levels (the
timbre: the largest difference, dB, of those within 30 dB of the loudest),
left against right (the panning). Fails past the limits.

    compare.py DIR RATE [NAME...]

The rhythm mode's drums (noise in three of them) have no pitch nor
harmonics: their timbre is the levels of their spectrum's octaves.
"""
import sys
import wave

import numpy as np

LIMITS = {"pitch": 0.5, "level": 0.5, "curve": 3.0, "timbre": 3.0}


def load(path):
    w = wave.open(path)
    r = w.getframerate()
    x = np.frombuffer(w.readframes(w.getnframes()), "<i2").reshape(-1, 2).astype(float)
    return r, x


def db(v):
    return 20 * np.log10(max(v, 1e-9))


def curve(x, r):
    blk = r // 50
    m = (x[:, 0] + x[:, 1]) / 2
    return np.array([np.sqrt((m[i:i + blk] ** 2).mean()) for i in range(0, len(m) - blk, blk)])


def harmonics(s, r, f0):
    sp = np.abs(np.fft.rfft(s * np.hanning(len(s))))
    hz = np.fft.rfftfreq(len(s), 1 / r)
    out = []
    for k in range(1, 11):
        f = f0 * k
        if f > r / 2 - 200:
            break
        band = (hz > f - 20) & (hz < f + 20)
        out.append(db(sp[band].max()) if band.any() else -200)
    return np.array(out)


def octaves(x, r):
    """the whole file's spectrum in octaves from 62.5 Hz to 8 kHz, dB"""
    s = (x[:, 0] + x[:, 1]) / 2
    sp = np.abs(np.fft.rfft(s)) ** 2 / len(s) ** 2   # (each band's mean square, at any rate)
    hz = np.fft.rfftfreq(len(s), 1 / r)
    return np.array([10 * np.log10(sp[(hz >= f) & (hz < 2 * f)].sum() + 1e-9) for f in 62.5 * 2 ** np.arange(8)])


def main():
    d = sys.argv[1]
    rate = sys.argv[2]
    bad = 0
    names = sys.argv[3:] or ["SINE", "FM", "ENV", "WAVE", "LFO", "RHYTHM", "CHORD", "KEYS"]
    for name in names:
        rr, ref = load("%s/%s.ref.wav" % (d, name))
        mr, mod = load("%s/%s.f%s.wav" % (d, name, rate[:2]))
        a, b = int(0.25 * rr), int(0.75 * rr)
        am, bm = int(0.25 * mr), int(0.75 * mr)
        s_ref = (ref[a:b, 0] + ref[a:b, 1]) / 2
        s_mod = (mod[am:bm, 0] + mod[am:bm, 1]) / 2

        def peak(s, r):
            sp = np.abs(np.fft.rfft(s * np.hanning(len(s))))
            k = int(sp[1:].argmax()) + 1
            # (the peak between its bins: a parabola's top)
            y0, y1, y2 = np.log(sp[k - 1:k + 2] + 1e-9)
            return (k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)) * r / len(s)
        f_ref, f_mod = peak(s_ref, rr), peak(s_mod, mr)
        lv_ref, lv_mod = db(np.sqrt((s_ref ** 2).mean())), db(np.sqrt((s_mod ** 2).mean()))
        c_ref, c_mod = curve(ref, rr), curve(mod, mr)
        n = min(len(c_ref), len(c_mod))
        top = db(c_ref[:n].max())
        # (not where the reference itself jumps: a note's start or end falls
        # in a 20 ms step at different places at different rates)
        live = [i for i in range(1, n - 1) if db(c_ref[i]) > top - 40 and
                abs(db(c_ref[i]) - db(c_ref[i - 1])) < 3 and abs(db(c_ref[i]) - db(c_ref[i + 1])) < 3]
        cd = max(abs(db(c_ref[i]) - db(c_mod[i])) for i in live) if live else 0
        if name == "RHYTHM":
            h_ref, h_mod = octaves(ref, rr), octaves(mod, mr)
            f_mod = f_ref
        else:
            h_ref, h_mod = harmonics(s_ref, rr, f_ref), harmonics(s_mod, mr, f_ref)
        k = min(len(h_ref), len(h_mod))
        hmax = h_ref[:k].max()
        sel = [i for i in range(k) if h_ref[i] > hmax - 30]
        # (each harmonic against the fundamental's: the shape, not the level)
        td = max(abs((h_ref[i] - h_ref[0]) - (h_mod[i] - h_mod[0])) for i in sel) if sel else 0
        lr_ref = db(np.sqrt((ref[a:b, 0] ** 2).mean())) - db(np.sqrt((ref[a:b, 1] ** 2).mean()))
        lr_mod = db(np.sqrt((mod[am:bm, 0] ** 2).mean())) - db(np.sqrt((mod[am:bm, 1] ** 2).mean()))
        pitch_ok = abs(f_ref - f_mod) < LIMITS["pitch"]
        if not pitch_ok:
            # (several notes as loud: the model's loudest may be another of
            # them; then the reference's must be within 1 dB of it there)
            sp = np.abs(np.fft.rfft(s_mod * np.hanning(len(s_mod))))
            hz = np.fft.rfftfreq(len(s_mod), 1 / mr)
            at = sp[(hz > f_ref - 3) & (hz < f_ref + 3)].max()
            pitch_ok = db(sp.max()) - db(at) < 1.0
        ok = (pitch_ok and abs(lv_ref - lv_mod) < LIMITS["level"] and
              cd < LIMITS["curve"] and td < LIMITS["timbre"] and abs(lr_ref - lr_mod) < 1)
        bad += not ok
        print("%s %-5s pitch %.1f/%.1f Hz, level %+.1f dB, curve %.1f dB, timbre %.1f dB, L-R %+.1f/%+.1f" %
              ("ok  " if ok else "FAIL", name, f_ref, f_mod, lv_mod - lv_ref, cd, td, lr_ref, lr_mod))
    print("%d failed" % bad if bad else "all within the limits")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
