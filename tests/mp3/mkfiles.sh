#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The test files, made with sox and lame into DIR: tones, sweeps, noise
# and clicks (which make lame use short blocks), at the rates, bit rates
# and stereo modes an MP3 player meets.
set -e
d=$1
mkdir -p "$d/src"
s="$d/src"
sox -n -r 44100 -c 2 "$s/tones.wav" synth 6 sine 440 sine 1000 remix 1,2 gain -1 \
	synth 6 sine amod 3 fade 0.1 6 0.1
sox -n -r 44100 -c 2 "$s/sweep.wav" synth 6 sine 20:20000 sine 20000:20 gain -3
sox -n -r 44100 -c 2 "$s/noise.wav" synth 6 pinknoise pinknoise gain -6
sox -n -r 44100 -c 2 "$s/clicks.wav" synth 6 square 2 square 3 gain -12 \
	synth 6 sine amod 1000 lowpass 9000
sox "$s/tones.wav" "$s/noise.wav" "$s/clicks.wav" -m "$s/mix.wav" gain -n -1
sox "$s/mix.wav" -r 48000 "$s/mix48.wav"
sox "$s/mix.wav" -r 32000 "$s/mix32.wav"
sox "$s/mix.wav" -c 1 "$s/mono.wav"
sox "$s/mix.wav" "$s/loud.wav" gain -n -0.1
L="lame -t --quiet --noreplaygain"
$L -b 128 "$s/mix.wav" "$d/mix128.mp3"
$L -b 320 "$s/mix.wav" "$d/mix320.mp3"
sox -n -r 44100 -c 2 "$s/square.wav" synth 6 square 100 square 1500 gain -0.1
$L -b 320 "$s/square.wav" "$d/square.mp3"
$L -V 2 "$s/mix.wav" "$d/mixvbr.mp3"
$L -b 192 -m s "$s/mix.wav" "$d/mixstereo.mp3"
$L -b 192 -m j "$s/noise.wav" "$d/noisejs.mp3"
$L -b 128 "$s/sweep.wav" "$d/sweep.mp3"
$L -b 160 "$s/clicks.wav" "$d/clicks.mp3"
$L -b 192 "$s/tones.wav" "$d/tones.mp3"
$L -b 192 "$s/mix48.wav" "$d/mix48.mp3"
$L -b 128 "$s/mix32.wav" "$d/mix32.mp3"
$L -b 96 "$s/mono.wav" "$d/mono.mp3"
$L -b 320 "$s/loud.wav" "$d/loud.mp3"
# MPEG-2 (22.05, 24, 16 kHz) and MPEG-2.5 (11.025, 12, 8 kHz)
$L -b 64 --resample 22.05 "$s/mix.wav" "$d/lsf22.mp3"
$L -b 64 --resample 24 "$s/clicks.wav" "$d/lsf24.mp3"
$L -b 32 --resample 16 "$s/mix.wav" "$d/lsf16.mp3"
$L -V 5 --resample 22.05 "$s/noise.wav" "$d/lsfvbr.mp3"
$L -b 24 --resample 11.025 "$s/mix.wav" "$d/lsf11.mp3"
$L -b 32 --resample 12 -m m "$s/mix.wav" "$d/lsf12mono.mp3"
$L -b 16 --resample 8 "$s/mix.wav" "$d/lsf8.mp3"
# as players meet them: LAME's own header frame (Xing for VBR, Info for
# CBR: the frame count, the length), and CRC-protected frames
I="lame --quiet --noreplaygain"
$I -V 0 "$s/mix.wav" "$d/xingvbr.mp3"
$I -b 192 "$s/mix.wav" "$d/infocbr.mp3"
$I -p -V 2 "$s/clicks.wav" "$d/crcvbr.mp3"
rm -rf "$s"
