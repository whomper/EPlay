# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# EPlay.
#   make            binary/eplay.prg (68030 code: the Falcon, the TT) and
#                   binary/eplayst.prg (68000: the STE), with a stock m68k ELF cross
#                   compiler (apt install gcc-m68k-linux-gnu) and a56 for its
#                   DSP program: freestanding on common/'s runtime, the
#                   header written by common/tools/elf2tos.py
#   make test       the MP3 decoder's model against minimp3 on files made
#                   with sox and lame; WAV and AIFF against sox
#   make dsptest    dsp/mp3.a56 against the model, bit for bit, on Hatari's
#                   DSP core, with its cycles; dsp/sn.a56; dsp/opn.a56 (the
#                   YM2612) against src/opnfast.c, bit for bit, its worst case's
#                   cycles
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
INC     := -I$(B) -Isrc -Isrc/mp3 -Iatari -Icommon/src -Icommon/atari -Iui -Icommon/ui \
           -Icommon/third_party/tjpgd -Icommon/third_party/nsgif/include -Iatari/include -DPIC_NO_GIF \
           -DPIC_TJPGD_DSP -DDLG_CLICK
# CPU: 68030 (EPLAY.PRG), or 68000 for the STE's EPLAYST.PRG (make st, with
# the family's 68000 rules: no bswap or store merging, oddaccess.py after)
# 68020-60 for EPLAY.PRG: code that runs on every CPU from the 68020 to the
# 68060, with none of the 64-bit mulu.l/divu.l the 68060 lacks (a CT60
# trapped on them: a tester, 9 Nov 2026); atari/div64.c gives libgcc's
# 64-bit helpers without them
CPU     ?= 68020-60
ifeq ($(CPU),68000)
STFLAGS := -fdisable-tree-bswap -fno-store-merging -DEPLAY_ST
endif
CFLAGS  := -m$(CPU) $(STFLAGS) -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables \
           -ffunction-sections -fdata-sections -Wall -Wno-pointer-sign \
           -nostdinc -isystem $(GCCINC) -Icommon/atari/include $(INC)
OWNWARN := -Wextra -Wno-unused-parameter -Werror
LDFLAGS := -m$(CPU) -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,common/atari/link.ld \
           -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

OWN     := main bar art sart draw about play prefs snd feed hostirq term div64 stsearch finddlg vgm vgmfile vgmplay ym dspsn sncmd dspopn opnd dspmp3 elog pcm mp3frame inf tags rgain probe list order m3u netsrc urldlg \
           stream plat_tos
# (util, conn, net_tos, sting: phase 7's streams; ovl and tls_ovl: https on a
# Direct connection through TLS.OVL, beside the program with CACERT.PEM)
CSRC    := bidi charset pic png inflate jpegdc dirlist util futil conn ovl
CTP     := tjpgd
CATARI  := libc tos gem crash dspjpeg net_tos sting tls_ovl
CUI     := mbar iconify fsel aboutbox aboutline alert dialog av cpucheck text offdraw click dd popup help filepick badge busy field clip settings dlgbuild tooltip
OBJS    := $(OBJDIR)/crt0.o $(OBJDIR)/nf.o $(OBJDIR)/crash_s.o $(OBJDIR)/feed_s.o $(OBJDIR)/hostirq_s.o \
	   $(OBJDIR)/inflate_fast_s.o \
           $(OBJDIR)/sting_s.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(OWN) $(CSRC) $(CTP) $(CATARI) $(CUI)))
HDRS    := $(wildcard src/*.h src/mp3/*.h atari/*.h ui/*.h common/src/*.h common/atari/*.h \
           common/atari/include/*.h common/ui/*.h) $(B)/mp3tab.h $(B)/mp3_prog.h $(B)/sn_prog.h \
           $(B)/opn_prog.h $(B)/opntab.h

all: binary/eplay.prg binary/eplayst.prg

# the STE's: the same sources for the 68000, in build/st
st: binary/eplayst.prg
binary/eplayst.prg: FORCE $(B)/mp3tab.h $(B)/mp3_prog.h $(B)/opn_prog.h $(B)/opntab.h | binary
	$(MAKE) --no-print-directory CPU=68000 OBJDIR=build/st build/st/eplay.elf
	python3 common/tools/oddaccess.py build/st/eplay.elf
	PRGFLAGS=0x10 python3 common/tools/elf2tos.py build/st/eplay.elf $@
.PHONY: st

# FBPROBE.PRG, for the FireBee's tester (the hub, 10 Nov 2026): plain
# 68000 code that only reads (tools/fbprobe), built with the STE's objects
fbprobe: binary/fbprobe.prg
binary/fbprobe.prg: FORCE | binary
	$(MAKE) --no-print-directory CPU=68000 OBJDIR=build/st build/st/fbprobe.elf
	PRGFLAGS=0 python3 common/tools/elf2tos.py build/st/fbprobe.elf $@
build/st/fbprobe.o: tools/fbprobe/fbprobe.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(OWNWARN) -c -o $@ $<
build/st/fbpeek.o: tools/fbprobe/peek.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
build/st/fbprobe.elf: build/st/crt0.o build/st/fbprobe.o build/st/fbpeek.o build/st/libc.o build/st/tos.o common/atari/link.ld
	$(CC) $(LDFLAGS) -o $@ build/st/crt0.o build/st/fbprobe.o build/st/fbpeek.o build/st/libc.o build/st/tos.o -lgcc
.PHONY: fbprobe

# PRGFLAGS 0x17: fast load, the program and its memory in TT RAM, and
# under MiNT's memory protection Global: the sound's interrupts (the tick,
# the host port's) run while other programs' are current, and reach
# EPlay's code, data and buffers then (a tester on MiNT, 9 Nov 2026: with
# 7, private, the Falcon halted as the music began)
binary/eplay.prg: $(OBJDIR)/eplay.elf common/tools/elf2tos.py | binary
	PRGFLAGS=0x17 python3 common/tools/elf2tos.py $< $@

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
$(B)/sn_prog.h: dsp/sn.a56 common/dsp/mkh.py | $(B)
	mkdir -p $(B)/sn && cd $(B)/sn && a56 ../../dsp/sn.a56 > sn.lst && ! grep -q 'warning:\|error:' sn.lst && \
		python3 ../../common/dsp/mkh.py a56.out ../sn_prog.h dsp_sn_prog sn.a56
$(B)/opn_prog.h: dsp/opn.a56 common/dsp/mkh.py | $(B)
	mkdir -p $(B)/opn && cd $(B)/opn && a56 ../../dsp/opn.a56 > opn.lst && ! grep -q 'warning:\|error:' opn.lst && \
		python3 ../../common/dsp/mkh.py a56.out ../opn_prog.h dsp_opn_prog opn.a56
# (the YM2612's tables: the model's, made on the build machine)
$(B)/opntab.h: tools/mkopntab.c src/opnfast.c src/opnfast.h src/opnd.h | $(B)
	cc -O2 -Wall -Wextra -Werror -Isrc -o $(B)/mkopntab tools/mkopntab.c src/opnfast.c -lm
	$(B)/mkopntab $@
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
	$(CC) $(subst -m$(CPU),-m68000,$(CFLAGS)) -c -o $@ $<
$(OBJDIR)/%.o: common/src/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
# the album art's JPEGs (common's pic.c; GIF left out: an album's art
# never is one)
$(OBJDIR)/tjpgd.o: common/third_party/tjpgd/tjpgd.c $(wildcard common/third_party/tjpgd/*.h) | $(OBJDIR)
	$(CC) $(CFLAGS) -Icommon/third_party/tjpgd -c -o $@ $<
$(OBJDIR)/%.o: common/atari/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: common/ui/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/crt0.o: common/atari/crt0.S Makefile | $(OBJDIR)
	$(CC) -m$(CPU) -DSTACK_SIZE=16384 -c -o $@ $<
$(OBJDIR)/crash_s.o: common/atari/crash.S | $(OBJDIR)
	$(CC) -m$(CPU) -c -o $@ $<
$(OBJDIR)/nf.o: common/atari/nf.S | $(OBJDIR)
	$(CC) -m$(CPU) -c -o $@ $<
$(OBJDIR)/feed_s.o: atari/feed.S | $(OBJDIR)
	$(CC) -m$(CPU) -c -o $@ $<
$(OBJDIR)/hostirq_s.o: atari/hostirq.S | $(OBJDIR)
	$(CC) -m$(CPU) -c -o $@ $<
# common's inflate_piece with its hot loop in 68000 assembly (a VGZ:
# inflate_read unpacked 200 KB/s on a 32 MHz Falcon, Erez's 33 MB VGZ took
# two minutes)
$(OBJDIR)/inflate_fast_s.o: common/atari/inflate_fast.S | $(OBJDIR)
	$(CC) -m$(CPU) -c -o $@ $<
$(OBJDIR)/inflate.o: CFLAGS += -DINFLATE_ASM
$(OBJDIR)/sting_s.o: common/atari/sting.S | $(OBJDIR)
	$(CC) -m$(CPU) -c -o $@ $<

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
# the STE's, for the speed rig on an STE (speed.py takes it there)
profst: $(B)/mp3tab.h $(B)/mp3_prog.h
	$(MAKE) --no-print-directory CPU=68000 OBJDIR=build/profst PROF=20 build/profst/eplay.elf
	PRGFLAGS=0x10 python3 common/tools/elf2tos.py build/profst/eplay.elf build/profst/eplay.prg
.PHONY: profst
build/prof/eplay.prg: build/prof/eplay.elf common/tools/elf2tos.py
	PRGFLAGS=0x17 python3 common/tools/elf2tos.py $< $@

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
$(B)/test_tags: tests/tags/test_tags.c src/tags.c src/tags.h src/rgain.c src/rgain.h common/src/charset.c common/src/bidi.c
	$(HOSTCC) $(HCFLAGS) -fsanitize=address,undefined -Isrc -Icommon/src -o $@ tests/tags/test_tags.c src/tags.c src/rgain.c \
		common/src/charset.c common/src/bidi.c
$(B)/test_gain: tests/pcm/test_gain.c src/pcm.c src/pcm.h
	$(HOSTCC) $(HCFLAGS) -fsanitize=address,undefined -Isrc -o $@ tests/pcm/test_gain.c src/pcm.c
$(B)/test_probe: tests/probe/test_probe.c src/probe.c src/probe.h src/tags.c src/rgain.c src/pcm.c src/mp3/mp3frame.c \
		common/src/charset.c common/src/bidi.c
	$(HOSTCC) $(HCFLAGS) -fsanitize=address,undefined -Isrc -Isrc/mp3 -Icommon/src -o $@ tests/probe/test_probe.c \
		src/probe.c src/tags.c src/rgain.c src/pcm.c src/mp3/mp3frame.c common/src/charset.c common/src/bidi.c
$(B)/test_m3u: tests/m3u/test_m3u.c src/m3u.c src/m3u.h
	$(HOSTCC) $(HCFLAGS) -fsanitize=address,undefined -Isrc -o $@ tests/m3u/test_m3u.c src/m3u.c
$(B)/test_order: tests/m3u/test_order.c src/order.c src/order.h
	$(HOSTCC) $(HCFLAGS) -fsanitize=address,undefined -Isrc -o $@ tests/m3u/test_order.c src/order.c
$(B)/test_pcm: tests/pcm/test_pcm.c src/pcm.c src/pcm.h
	$(HOSTCC) $(HCFLAGS) -fsanitize=address,undefined -Isrc -o $@ tests/pcm/test_pcm.c src/pcm.c
$(B)/test_stsearch: tests/stsearch/test_stsearch.c src/stsearch.c src/stsearch.h common/src/charset.c common/src/bidi.c
	$(HOSTCC) $(HCFLAGS) -fsanitize=address,undefined -Isrc -Icommon/src -o $@ tests/stsearch/test_stsearch.c \
		src/stsearch.c common/src/charset.c common/src/bidi.c
$(B)/test_vgm: tests/vgm/test_vgm.c src/vgm.c src/vgm.h src/vgmfile.c src/vgmfile.h src/tags.c src/rgain.c \
		common/src/charset.c common/src/bidi.c common/src/inflate.c common/src/util.c
	$(HOSTCC) $(HCFLAGS) -fsanitize=address,undefined -Isrc -Icommon/src -o $@ tests/vgm/test_vgm.c \
		src/vgm.c src/vgmfile.c src/tags.c src/rgain.c common/src/charset.c common/src/bidi.c \
		common/src/inflate.c common/src/util.c
$(B)/test_stream: tests/stream/test_stream.c src/stream.c src/stream.h
	$(HOSTCC) $(HCFLAGS) -fsanitize=address,undefined -Isrc -o $@ tests/stream/test_stream.c src/stream.c
# the stream parser's fuzz target (the guidelines, 7.1): make fuzz runs
# libFuzzer for FUZZ_S seconds; make test replays tests/fuzz/found and the
# seeds with the build machine's compiler
FUZZ_S ?= 60
$(B)/fuzz_replay: tests/fuzz/fuzz_stream.c tests/fuzz/replay.c src/stream.c src/stream.h
	$(HOSTCC) $(HCFLAGS) -fsanitize=address,undefined -Isrc -o $@ tests/fuzz/fuzz_stream.c tests/fuzz/replay.c src/stream.c
fuzz: tests/fuzz/fuzz_stream.c src/stream.c src/stream.h | $(B)
	clang -O1 -g -fsanitize=fuzzer,address,undefined -Isrc -o $(B)/fuzz_stream tests/fuzz/fuzz_stream.c src/stream.c
	mkdir -p $(B)/fuzzcorpus
	./$(B)/fuzz_stream -max_total_time=$(FUZZ_S) -artifact_prefix=tests/fuzz/found/ $(B)/fuzzcorpus tests/fuzz/seeds
test: $(B)/test_mp3 $(B)/mp3files/.made $(B)/test_pcm $(B)/test_info $(B)/test_inf $(B)/test_tags $(B)/test_gain \
		$(B)/test_probe $(B)/test_m3u $(B)/test_order $(B)/test_stream $(B)/test_stsearch $(B)/test_vgm $(B)/fuzz_replay
	./$(B)/test_inf
	mkdir -p $(B)/vgmfiles && ./$(B)/test_vgm $(B)/vgmfiles
	./$(B)/test_stream
	./$(B)/test_stsearch
	./$(B)/fuzz_replay tests/fuzz/seeds/* tests/fuzz/found/*
	./$(B)/test_gain
	./$(B)/test_m3u
	./$(B)/test_order
	./$(B)/test_tags
	TEST=$(B)/test_tags D=$(B)/tagfiles sh tests/tags/run.sh
	./$(B)/test_mp3 $(B)/mp3files/*.mp3
	./$(B)/test_info $(B)/mp3files/*.mp3
	TEST=$(B)/test_pcm sh tests/pcm/run.sh
	TEST=$(B)/test_probe D=$(B)/probefiles sh tests/probe/run.sh

$(B) $(OBJDIR):
	mkdir -p $@
clean:
	rm -rf $(B) binary dist
dsptest: $(B)/mp3files/.made
	$(MAKE) -C tests/dspmp3
	$(MAKE) -C tests/dspsn
	$(MAKE) -C tests/dspopn

# eplay.hyp from docs/GUIDE.md with the family's front page (docs/front.json,
# common/tools/helpfront.py), and eplay.ref, its pages' names for ST-Guide's
# index (the guidelines, 4.3)
HCP     ?= hcp
help: binary/eplay.hyp
binary/eplay.hyp: docs/GUIDE.md common/tools/mkhelp.py VERSION docs/front.json docs/help/WINDOW.IMG \
                  common/help/EPLAY.IMG common/tools/helpfront.py common/tools/gemimg.py | binary
	mkdir -p $(B)/help
	python3 common/tools/mkhelp.py docs/GUIDE.md VERSION $(B)/help/EPLAY.STG --database "EPlay - music on the Falcon" --subject Sound
	python3 common/tools/helpfront.py page docs/front.json $(B)/help/EPLAY.STG $(B)/help/EPLAY.STG
	$(HCP) -q -o $@ $(B)/help/EPLAY.STG >/dev/null
	test -s binary/eplay.ref

# a release's files: each one in dist/, and all of them in
# dist/eplay-x.y.zip (the icons in icons/; every name in lowercase, the
# guidelines 6.6), for the release workflow
DIST    := binary/eplay.prg binary/eplayst.prg binary/fbprobe.prg binary/eplay.hyp binary/eplay.ref \
           common/tlsovl/tls.ovl
ICONS   := icons/eplay.rsc icons/eplaynv.rsc icons/eplay.icn icons/eplaymk.icn
dist: all fbprobe help
	rm -rf dist
	mkdir -p dist/zip/icons
	cp $(DIST) $(ICONS) dist/
	cp NOTICES.TXT dist/notices.txt
	cp common/certs/CACERT.PEM dist/cacert.pem
	cp $(DIST) dist/zip/
	cp NOTICES.TXT dist/zip/notices.txt
	cp common/certs/CACERT.PEM dist/zip/cacert.pem
	cp $(ICONS) dist/zip/icons/
	cd dist/zip && zip -q -9 -r ../eplay-$(VERSION_STR).zip .
	rm -rf dist/zip

rig: all prof
	$(MAKE) -C tests/hatari
	python3 tests/hatari/play_test.py $(B)/rig/play
	python3 tests/hatari/speed.py $(B)/rig/speed

.PHONY: all test dsptest prof rig dist clean icons help

# the desktop icon and its headers, from common/icons/EPLAY.txt (committed)
icons:
	python3 common/tools/appicon.py EPLAY --rsc icons/eplay.rsc --rscnv icons/eplaynv.rsc \
		--icn icons/eplay.icn --mask icons/eplaymk.icn --headers ui --docs docs
