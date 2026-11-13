#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""compare.py - the FireBee's MP3 decoding (the ColdFire's, under QEMU)
against the host's minimp3, the same source: as many samples, and each
within a few steps (the V4e's FPU rounds in double precision, the host's
SSE in single: minimp3's float sums can differ in their last bits), the
error's power far under the music's.

    compare.py CF.RAW HOST.RAW NAME
"""
import sys

import numpy as np

cf = np.fromfile(sys.argv[1], ">i2").astype(float)
ho = np.fromfile(sys.argv[2], ">i2").astype(float)
name = sys.argv[3]
if len(cf) != len(ho) or not len(cf):
    print("FAIL %s: %d samples on the ColdFire, %d on the host" % (name, len(cf), len(ho)))
    sys.exit(1)
d = np.abs(cf - ho)
snr = 10 * np.log10((ho ** 2).mean() / max((d ** 2).mean(), 1e-12)) if (ho ** 2).mean() > 0 else 999
ok = d.max() <= 4 and snr > 80
print("%s %s: %d samples, the largest difference %d, %.0f dB under the music" %
      ("ok  " if ok else "FAIL", name, len(cf), d.max(), snr))
sys.exit(0 if ok else 1)
