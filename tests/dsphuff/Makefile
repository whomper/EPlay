# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# dsp/huff.a56 on Hatari's DSP core against the model, bit for bit, and
# its cycles: make (after the top-level make test has made the files;
# fetches the core with common's tests/ec/fetch-hatari.sh the first time;
# needs a56)
H := ../../common/tests/ec/build/hatari
B := build
TOP := ../..
FILES ?= $(wildcard $(TOP)/build/mp3files/*.mp3)

run: $(B)/sim
	./$(B)/sim $(FILES)
$(B)/huff_prog.h: $(TOP)/dsp/huff.a56 | $(B)
	cd $(B) && a56 ../$(TOP)/dsp/huff.a56 >/dev/null && python3 ../$(TOP)/common/dsp/mkh.py a56.out huff_prog.h dsp_huff_prog huff.a56
$(B)/mp3tab.h: $(TOP)/tools/mkmp3tab.py | $(B)
	python3 $(TOP)/tools/mkmp3tab.py $(TOP)/data $@
$(B)/sim: sim.c $(B)/huff_prog.h $(B)/mp3tab.h $(TOP)/src/mp3/mp3dsp.c $(TOP)/src/mp3/mp3frame.c $(H)/dsp_core.c
	cc -O2 -w -DMP3DSP_HUFF_HOOK -I$(B) -I$(TOP)/src/mp3 -I$(H) -I$(H)/stub -o $@ sim.c \
		$(TOP)/src/mp3/mp3dsp.c $(TOP)/src/mp3/mp3frame.c $(H)/dsp_core.c $(H)/dsp_cpu.c
$(H)/dsp_core.c:
	sh $(TOP)/common/tests/ec/fetch-hatari.sh
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: run clean
