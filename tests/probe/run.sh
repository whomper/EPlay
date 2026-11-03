#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# src/probe.c: the lengths of files made by sox and lame (WAV, AIFF, an MP3
# with a Xing frame, a VBR one, one without (its bit rate), one behind an
# ID3v2 tag with a picture). TEST is test_probe, D a folder for the files.
set -e
mkdir -p "$D"
fail=0
sox -n -r 44100 -c 2 -b 16 "$D/a.wav" synth 3 sine 440 gain -6
sox -n -r 48000 -c 2 -b 24 "$D/b.aif" synth 2.5 sine 440 gain -6
sox -n -r 22050 -c 1 -b 8 -e unsigned-integer "$D/c.wav" synth 1.25 sine 440 gain -6
$TEST "$D/a.wav" 3000 1 || fail=1
$TEST "$D/b.aif" 2500 1 || fail=1
$TEST "$D/c.wav" 1250 1 || fail=1
sox -n -r 44100 -c 2 "$D/src.wav" synth 4 sine 440 sine 660 gain -6
lame --quiet --noreplaygain -b 192 "$D/src.wav" "$D/xing.mp3"
$TEST "$D/xing.mp3" 4000 60 || fail=1
lame --quiet --noreplaygain -V 2 "$D/src.wav" "$D/vbr.mp3"
$TEST "$D/vbr.mp3" 4000 60 || fail=1
lame -t --quiet --noreplaygain -b 128 "$D/src.wav" "$D/cbr.mp3"
$TEST "$D/cbr.mp3" 4000 60 || fail=1
printf '\377\330\377\340\000\020JFIF\000\001\001\000\000\001\000\001\000\000cover bytes\377\331' > "$D/cover.jpg"
head -c 20000 /dev/zero >> "$D/cover.jpg"
lame --quiet --noreplaygain -b 160 --tt "Long tag" --ti "$D/cover.jpg" "$D/src.wav" "$D/tagged.mp3"
$TEST "$D/tagged.mp3" 4000 60 || fail=1
exit $fail
