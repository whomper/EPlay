#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# src/tags.c on files tagged by other programs: lame's ID3v2.3 with a
# picture, ffmpeg's ID3v2.4 with a picture and its ID3v2.3, a WAV's LIST
# INFO from ffmpeg (where it is installed). TEST is test_tags, D a folder
# for the files.
set -e
mkdir -p "$D"
sox -n -r 44100 -c 2 "$D/t.wav" synth 1 sine 440 gain -6
# a picture's bytes as lame takes them (it looks at the JPEG's first bytes;
# the test at its start and end markers): no image library needed
printf '\377\330\377\340\000\020JFIF\000\001\001\000\000\001\000\001\000\000cover bytes\377\331' > "$D/cover.jpg"
lame -t --quiet --noreplaygain -b 128 --tt "Clair de lune" --ta "Claude Debussy" \
	--tl "Suite bergamasque" --ti "$D/cover.jpg" "$D/t.wav" "$D/lame.mp3"
$TEST "$D/lame.mp3" "Clair de lune" "Claude Debussy" "Suite bergamasque" 1
if ! command -v ffmpeg >/dev/null 2>&1; then
	echo "skip ffmpeg's tags: no ffmpeg here"
	exit 0
fi
F="ffmpeg -v error -y"
$F -f lavfi -i color=c=blue:s=64x64 -frames:v 1 "$D/cover2.jpg"
$F -i "$D/t.wav" -i "$D/cover2.jpg" -map 0 -map 1 -c:a libmp3lame -b:a 128k -c:v copy \
	-id3v2_version 4 -metadata title="Gymnopedie No. 1" -metadata artist="Erik Satie" \
	-metadata album="Trois Gymnopedies" -metadata:s:v comment="Cover (front)" "$D/ff24.mp3"
$TEST "$D/ff24.mp3" "Gymnopedie No. 1" "Erik Satie" "Trois Gymnopedies" 1
$F -i "$D/t.wav" -c:a libmp3lame -b:a 128k -id3v2_version 3 -write_id3v1 1 \
	-metadata title="Reverie" -metadata artist="Claude Debussy" -metadata album="Piano" "$D/ff23.mp3"
$TEST "$D/ff23.mp3" "Reverie" "Claude Debussy" "Piano" 0
$F -i "$D/t.wav" -metadata title="Air" -metadata artist="J. S. Bach" -metadata album="Suite No. 3" \
	"$D/info.wav"
$TEST "$D/info.wav" "Air" "J. S. Bach" "Suite No. 3" 0
