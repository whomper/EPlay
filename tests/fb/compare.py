#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""compare.py - the FireBee's MP3 decoding (the ColdFire's, under QEMU)
against the host's minimp3, the same source: as many samples, and each
within a few steps (the V4e's FPU rounds in double precision, the host's
SSE in single: minimp3's float sums can differ in their last bits), the
error's power far under the music's.

    compare.py CF.RAW HOST.RAW NAME
    compare.py --exact CF.RAW HOST.RAW NAME   (integer models: bit for bit)
    compare.py --near CF.RAW HOST.RAW NAME    (another decoder: EPlay's own
        in integers against minimp3, both full accuracy: within 1/512 of
        full scale, the difference 70 dB under the music)
"""
import sys

import numpy as np

exact = sys.argv[1] == "--exact"
near = sys.argv[1] == "--near"
args = sys.argv[2:] if exact or near else sys.argv[1:]
cf = np.fromfile(args[0], ">i2").astype(float)
ho = np.fromfile(args[1], ">i2").astype(float)
name = args[2]
if len(cf) != len(ho) or not len(cf):
    print("FAIL %s: %d samples on the ColdFire, %d on the host" % (name, len(cf), len(ho)))
    sys.exit(1)
d = np.abs(cf - ho)
snr = 10 * np.log10((ho ** 2).mean() / max((d ** 2).mean(), 1e-12)) if (ho ** 2).mean() > 0 else 999
if exact:
    ok = d.max() == 0 and (ho ** 2).mean() > 0
elif near:
    ok = d.max() <= 64 and snr > 70
else:
    ok = d.max() <= 4 and snr > 80
print("%s %s: %d samples, the largest difference %d, %.0f dB under the music" %
      ("ok  " if ok else "FAIL", name, len(cf), d.max(), snr))
sys.exit(0 if ok else 1)
