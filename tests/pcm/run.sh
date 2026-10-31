#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# tests/pcm: WAV and AIFF files of every kind EPlay plays, made by sox,
# against sox's own 16-bit stereo of each (make test). TEST=the program.
set -e
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
sox -n -r 44100 -c 2 "$d/src.wav" synth 0.5 sine 440 sine 1000 gain -2
fail=0
for spec in "wav 44100 2 16" "wav 44100 1 16" "wav 48000 2 24" "wav 44100 2 32" \
	    "aiff 44100 2 16" "aiff 44100 1 16" "aiff 32000 2 24" "aiff 11025 1 8" "aifc 44100 2 16"; do
	set -- $spec
	f="$d/t_$1_$2_$3_$4.$1"
	sox -D "$d/src.wav" -r "$2" -c "$3" -b "$4" -e signed-integer "$f" 2>/dev/null || \
		sox -D "$d/src.wav" -r "$2" -c "$3" -b "$4" "$f"
	sox -D "$f" -t raw -e signed-integer -b 16 -c 2 -L "$f.raw" 2>/dev/null
	"$TEST" "$f" "$f.raw" "$2" "$3" "$4" || fail=1
done
# 8-bit WAV is unsigned
for c in 1 2; do
	sox -D "$d/src.wav" -r 22050 -c $c -b 8 -e unsigned-integer "$d/u8_$c.wav"
	sox -D "$d/u8_$c.wav" -t raw -e signed-integer -b 16 -c 2 -L "$d/u8_$c.raw"
	"$TEST" "$d/u8_$c.wav" "$d/u8_$c.raw" 22050 $c 8 || fail=1
done
exit $fail
