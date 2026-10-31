# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# EPlay. make: the Atari objects so far (the program comes with the sound
# core); make test: the MP3 decoder's model against minimp3 on files made
# with sox and lame; make dsptest: the DSP's programs against the model,
# bit for bit, on Hatari's DSP core, with their cycles (needs a56).
CROSS   := m68k-linux-gnu-
ACC     := $(CROSS)gcc
ACFLAGS := -m68030 -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-store-merging \
           -Ibuild -Isrc/mp3 -Icommon/atari/include
B       := build
HOSTCC  := cc
HCFLAGS := -O2 -Wall -Wextra -DDSP_COUNT_LIMIT -I$(B) -Isrc/mp3 -Itests/mp3
MP3SRC  := src/mp3/mp3frame.c src/mp3/mp3dsp.c
HDRS    := $(wildcard src/mp3/*.h) $(B)/mp3tab.h

all: $(B)/atari/mp3frame.o $(B)/atari/mp3dsp.o

$(B)/mp3tab.h: tools/mkmp3tab.py data/mp3huff.txt data/mp3window.txt | $(B)
	python3 tools/mkmp3tab.py data $@
$(B)/atari/%.o: src/mp3/%.c $(HDRS) | $(B)/atari
	$(ACC) $(ACFLAGS) -c -o $@ $<

$(B)/test_mp3: tests/mp3/test_mp3.c $(MP3SRC) $(HDRS)
	$(HOSTCC) $(HCFLAGS) -o $@ tests/mp3/test_mp3.c $(MP3SRC) -lm
$(B)/mp3files/.made: tests/mp3/mkfiles.sh
	rm -rf $(B)/mp3files
	sh tests/mp3/mkfiles.sh $(B)/mp3files
	touch $@
test: $(B)/test_mp3 $(B)/mp3files/.made
	./$(B)/test_mp3 $(B)/mp3files/*.mp3

$(B) $(B)/atari:
	mkdir -p $@
clean:
	rm -rf $(B) binary dist
dsptest: $(B)/mp3files/.made
	$(MAKE) -C tests/dspbench
	$(MAKE) -C tests/dsphuff
	$(MAKE) -C tests/dspsynth
	$(MAKE) -C tests/dspmp3

.PHONY: all test dsptest clean icons

# the desktop icon and its headers, from common/icons/EPLAY.txt (committed)
icons:
	python3 common/tools/appicon.py EPLAY --rsc icons/EPLAY.RSC --rscnv icons/EPLAYNV.RSC \
		--icn icons/EPLAY.ICN --mask icons/EPLAYMK.ICN --headers ui --docs docs
