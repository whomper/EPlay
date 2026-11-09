# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# dsp/mp3.a56, the whole DSP decoder, on Hatari's DSP core against the
# model, bit for bit, and its cycles: make (after the top-level make test
# has made the files). Then again resampled (mp3rs): to the Falcon's clock,
# and 48 kHz to 44.1. Then STREAM as EPlay plays (stream.c): the SSI
# clocked, the packets by the host receive's interrupt, none broken
H := ../../common/tests/ec/build/hatari
B := build
TOP := ../..
FILES ?= $(wildcard $(TOP)/build/mp3files/*.mp3)

run: $(B)/sim $(B)/stream
	./$(B)/sim $(FILES)
	@echo "resampled to the Falcon's clock (SIMRS=1):"
	SIMRS=1 ./$(B)/sim $(FILES)
	@echo "48 kHz resampled to 44.1 (SIMRS=2):"
	SIMRS=2 ./$(B)/sim $(FILES)
	@echo "Quality Normal: to 32,780 Hz, the top subbands left out (SIMRS=3):"
	SIMRS=3 ./$(B)/sim $(FILES)
	@echo "PCM through the DSP (SIMPCM=1), as is and to the Falcon's clock:"
	SIMPCM=1 ./$(B)/sim $(FILES)
	SIMPCM=1 SIMRS=1 ./$(B)/sim $(FILES)
	@echo "STREAM (stream.c): as the file's rate, and resampled to the Falcon's clock:"
	for f in mix320 mono lsf22 mix48 mixvbr; do ./$(B)/stream $(TOP)/build/mp3files/$$f.mp3 120 && \
		SIMRS=1 ./$(B)/stream $(TOP)/build/mp3files/$$f.mp3 120 || exit 1; done
	@echo "STREAM, gapless (SIMDROP: PK_DROP, the join's padding and delay left out), as the model, bit for bit:"
	for f in tones mono lsf22 mixvbr; do for d in 30:745:1512 30:216:1512 30:29:1500 30:0:1105 30:1151:600; do \
		SIMDROP=$$d ./$(B)/stream $(TOP)/build/mp3files/$$f.mp3 80 && \
		SIMDROP=$$d SIMRS=1 ./$(B)/stream $(TOP)/build/mp3files/$$f.mp3 80 || exit 1; done; done
	@echo "STREAM, gapless, the heavier files with a DSP's time to spare (SIMLOAD=75):"
	for f in mix320 infocbr mix48; do for d in 30:745:1512 30:936:1512; do \
		SIMLOAD=75 SIMDROP=$$d ./$(B)/stream $(TOP)/build/mp3files/$$f.mp3 80 && \
		SIMLOAD=75 SIMDROP=$$d SIMRS=1 ./$(B)/stream $(TOP)/build/mp3files/$$f.mp3 80 || exit 1; done; done
	@echo "STREAM, ReplayGain (SIMGAIN: global_gain up to +12 dB, held at full; and down), as the model, bit for bit:"
	for f in tones mix320 lsf22 mixvbr; do for g in 8 4 -6; do \
		SIMGAIN=$$g ./$(B)/stream $(TOP)/build/mp3files/$$f.mp3 80 && \
		SIMGAIN=$$g SIMRS=1 ./$(B)/stream $(TOP)/build/mp3files/$$f.mp3 80 || exit 1; done; done
	@echo "STREAM, a slow 68030 (SIMLAT=300: a word each 9 us) and a DSP 3 % slower (SIMLOAD=103):"
	for f in mix320 mixvbr lsf22; do SIMRS=1 SIMLAT=300 SIMLOAD=103 ./$(B)/stream $(TOP)/build/mp3files/$$f.mp3 120 || exit 1; done
$(B)/mp3tab.h: $(TOP)/tools/mkmp3tab.py | $(B)
	python3 $(TOP)/tools/mkmp3tab.py $(TOP)/data $@
$(B)/mp3_prog.h: $(TOP)/dsp/mp3.a56 $(B)/mp3tab.h
	cd $(B) && a56 ../$(TOP)/dsp/mp3.a56 > a56.lst && ! grep -q 'warning:\|error:' a56.lst && \
		python3 ../$(TOP)/common/dsp/mkh.py a56.out mp3_prog.h dsp_mp3_prog mp3.a56
$(B)/sim: sim.c ../dspsim.h $(B)/mp3_prog.h $(B)/mp3tab.h $(TOP)/src/mp3/mp3dsp.c $(TOP)/src/mp3/mp3frame.c $(H)/dsp_core.c
	cc -O2 -w -DMP3DSP_DEBUG -I$(B) -I$(TOP)/src/mp3 -I$(H) -I$(H)/stub -o $@ sim.c \
		$(TOP)/src/mp3/mp3dsp.c $(TOP)/src/mp3/mp3frame.c $(H)/dsp_core.c $(H)/dsp_cpu.c
$(B)/stream: stream.c ../dspsim.h $(B)/mp3_prog.h $(B)/mp3tab.h $(TOP)/src/mp3/mp3dsp.c $(TOP)/src/mp3/mp3frame.c $(H)/dsp_core.c
	cc -O2 -w -I$(B) -I$(TOP)/src/mp3 -I$(H) -I$(H)/stub -o $@ stream.c \
		$(TOP)/src/mp3/mp3dsp.c $(TOP)/src/mp3/mp3frame.c $(H)/dsp_core.c $(H)/dsp_cpu.c
$(H)/dsp_core.c:
	sh $(TOP)/common/tests/ec/fetch-hatari.sh
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: run clean
