# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# EPlay.
#   make            binary/eplay.prg (68030 code), with a stock m68k ELF cross
#                   compiler (apt install gcc-m68k-linux-gnu) and a56 for its
#                   DSP program: freestanding on common/'s runtime, the
#                   header written by common/tools/elf2tos.py
#   make test       the MP3 decoder's model against minimp3 on files made
#                   with sox and lame; WAV and AIFF against sox
#   make dsptest    dsp/mp3.a56 against the model, bit for bit, on Hatari's
#                   DSP core, with its cycles
#   make prof       build/prof/eplay.prg for the speed rig (tests/hatari)
#   make rig        the Hatari rigs: playing (tests/hatari/play_test.py) and
#                   the speed rig (tests/hatari/speed.py)
#   make dist       dist/eplay-x.y.zip and its files, as on the Releases page
# Every file is built with -ffunction-sections and linked with
# --gc-sections, so of common/ only what EPlay calls goes in.
CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
B       := build
OBJDIR  := build/atari
INC     := -I$(B) -Isrc -Isrc/mp3 -Iatari -Icommon/src -Icommon/atari -Iui -Icommon/ui
CFLAGS  := -m68030 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables \
           -ffunction-sections -fdata-sections -Wall -Wno-pointer-sign \
           -nostdinc -isystem $(GCCINC) -Icommon/atari/include $(INC)
OWNWARN := -Wextra -Wno-unused-parameter -Werror
LDFLAGS := -m68030 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,common/atari/link.ld \
           -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

OWN     := main draw about play settings snd feed hostirq dspmp3 elog pcm mp3frame inf
CSRC    := bidi
CATARI  := libc tos gem crash
CUI     := mbar aboutbox aboutline alert dialog av cpucheck text offdraw click
OBJS    := $(OBJDIR)/crt0.o $(OBJDIR)/nf.o $(OBJDIR)/crash_s.o $(OBJDIR)/feed_s.o $(OBJDIR)/hostirq_s.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(OWN) $(CSRC) $(CATARI) $(CUI)))
HDRS    := $(wildcard src/*.h src/mp3/*.h atari/*.h ui/*.h common/src/*.h common/atari/*.h \
           common/atari/include/*.h common/ui/*.h) $(B)/mp3tab.h $(B)/mp3_prog.h

all: binary/eplay.prg

binary/eplay.prg: $(OBJDIR)/eplay.elf common/tools/elf2tos.py | binary
	PRGFLAGS=7 python3 common/tools/elf2tos.py $< $@

VERSION_STR := $(shell cat VERSION)
$(OBJDIR)/version.c: FORCE | $(OBJDIR)
	@printf '/* written by the Makefile: do not edit */\nconst char app_version[] = "%s";\n' "$(VERSION_STR)" > $@.new
	@if cmp -s $@.new $@; then rm $@.new; else mv $@.new $@; fi
FORCE:
.PHONY: FORCE
$(OBJDIR)/version.o: $(OBJDIR)/version.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/eplay.elf: $(OBJS) $(OBJDIR)/version.o common/atari/link.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(OBJDIR)/version.o -lgcc

# the tables and the DSP program (as C arrays), made here, never committed
$(B)/mp3tab.h: tools/mkmp3tab.py data/mp3huff.txt data/mp3window.txt | $(B)
	python3 tools/mkmp3tab.py data $@
$(B)/mp3_prog.h: dsp/mp3.a56 $(B)/mp3tab.h common/dsp/mkh.py
	cd $(B) && a56 ../dsp/mp3.a56 > mp3.lst && ! grep -q 'warning:\|error:' mp3.lst && \
		python3 ../common/dsp/mkh.py a56.out mp3_prog.h dsp_mp3_prog mp3.a56

$(OBJDIR)/%.o: ui/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) $(OWNWARN) -c -o $@ $<
$(OBJDIR)/%.o: atari/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) $(OWNWARN) -c -o $@ $<
$(OBJDIR)/%.o: src/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) $(OWNWARN) -c -o $@ $<
$(OBJDIR)/%.o: src/mp3/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) $(OWNWARN) -c -o $@ $<
# the first code, built for the 68000: on an ST it says EPlay needs a
# Falcon, before anything of the 68030's runs (common's cpucheck.c)
$(OBJDIR)/cpucheck.o: common/atari/cpucheck.c | $(OBJDIR)
	$(CC) $(subst -m68030,-m68000,$(CFLAGS)) -c -o $@ $<
$(OBJDIR)/%.o: common/src/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: common/atari/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: common/ui/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/crt0.o: common/atari/crt0.S Makefile | $(OBJDIR)
	$(CC) -m68030 -DSTACK_SIZE=16384 -c -o $@ $<
$(OBJDIR)/crash_s.o: common/atari/crash.S | $(OBJDIR)
	$(CC) -m68030 -c -o $@ $<
$(OBJDIR)/nf.o: common/atari/nf.S | $(OBJDIR)
	$(CC) -m68030 -c -o $@ $<
$(OBJDIR)/feed_s.o: atari/feed.S | $(OBJDIR)
	$(CC) -m68030 -c -o $@ $<
$(OBJDIR)/hostirq_s.o: atari/hostirq.S | $(OBJDIR)
	$(CC) -m68030 -c -o $@ $<

binary:
	mkdir -p $@

# the speed rig's build (tests/hatari/speed.py): each stretch between
# event calls of 20 ms or more on the emulator's console (-DAES_PROFILE,
# common/atari/gem.c), in build/prof/eplay.prg
ifdef PROF
CFLAGS += -DAES_PROFILE=$(PROF)
endif
prof: $(B)/mp3tab.h $(B)/mp3_prog.h
	$(MAKE) OBJDIR=build/prof PROF=20 build/prof/eplay.prg
build/prof/eplay.prg: build/prof/eplay.elf common/tools/elf2tos.py
	PRGFLAGS=7 python3 common/tools/elf2tos.py $< $@

HOSTCC  := cc
HCFLAGS := -O2 -Wall -Wextra -DDSP_COUNT_LIMIT -I$(B) -Isrc/mp3 -Itests/mp3
MP3SRC  := src/mp3/mp3frame.c src/mp3/mp3dsp.c
MP3HDRS := $(wildcard src/mp3/*.h) $(B)/mp3tab.h

$(B)/test_mp3: tests/mp3/test_mp3.c $(MP3SRC) $(MP3HDRS)
	$(HOSTCC) $(HCFLAGS) -o $@ tests/mp3/test_mp3.c $(MP3SRC) -lm
$(B)/mp3files/.made: tests/mp3/mkfiles.sh
	rm -rf $(B)/mp3files
	sh tests/mp3/mkfiles.sh $(B)/mp3files
	touch $@
$(B)/test_info: tests/mp3/test_info.c src/mp3/mp3frame.c src/mp3/mp3frame.h
	$(HOSTCC) $(HCFLAGS) -o $@ tests/mp3/test_info.c src/mp3/mp3frame.c
$(B)/test_inf: tests/inf/test_inf.c src/inf.c src/inf.h
	$(HOSTCC) $(HCFLAGS) -fsanitize=address,undefined -Isrc -o $@ tests/inf/test_inf.c src/inf.c
$(B)/test_pcm: tests/pcm/test_pcm.c src/pcm.c src/pcm.h
	$(HOSTCC) $(HCFLAGS) -fsanitize=address,undefined -Isrc -o $@ tests/pcm/test_pcm.c src/pcm.c
test: $(B)/test_mp3 $(B)/mp3files/.made $(B)/test_pcm $(B)/test_info $(B)/test_inf
	./$(B)/test_inf
	./$(B)/test_mp3 $(B)/mp3files/*.mp3
	./$(B)/test_info $(B)/mp3files/*.mp3
	TEST=$(B)/test_pcm sh tests/pcm/run.sh

$(B) $(OBJDIR):
	mkdir -p $@
clean:
	rm -rf $(B) binary dist
dsptest: $(B)/mp3files/.made
	$(MAKE) -C tests/dspmp3

# a release's files: each one in dist/, and all of them in
# dist/eplay-x.y.zip (the icons in icons/; every name in lowercase, the
# guidelines 6.6), for the release workflow. 0.1
# has no guide yet (EPLAY.HYP comes with the GEM program's phase); the
# workflow's HCP is taken and not used until then
HCP     ?= hcp
DIST    := binary/eplay.prg
ICONS   := icons/eplay.rsc icons/eplaynv.rsc icons/eplay.icn icons/eplaymk.icn
dist: all
	rm -rf dist
	mkdir -p dist/zip/icons
	cp $(DIST) $(ICONS) dist/
	cp NOTICES.TXT dist/notices.txt
	cp $(DIST) dist/zip/
	cp NOTICES.TXT dist/zip/notices.txt
	cp $(ICONS) dist/zip/icons/
	cd dist/zip && zip -q -9 -r ../eplay-$(VERSION_STR).zip .
	rm -rf dist/zip

rig: all prof
	$(MAKE) -C tests/hatari
	python3 tests/hatari/play_test.py $(B)/rig/play
	python3 tests/hatari/speed.py $(B)/rig/speed

.PHONY: all test dsptest prof rig dist clean icons

# the desktop icon and its headers, from common/icons/EPLAY.txt (committed)
icons:
	python3 common/tools/appicon.py EPLAY --rsc icons/eplay.rsc --rscnv icons/eplaynv.rsc \
		--icn icons/eplay.icn --mask icons/eplaymk.icn --headers ui --docs docs
