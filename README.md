# EPlay

<img src="docs/icon16.png" width="96" align="right" alt="EPlay icon: a loudspeaker sending out a play triangle">

An audio player for the Atari Falcon: MP3 decoded on the Falcon's DSP, WAV and AIFF, in a GEM
window. Written in C and DSP56001 assembly for GEM.

**Highlights**

- **Steady sound:** the music is fed by an interrupt from a queue of seconds to minutes held in
  memory, so menus, dialogs, the file selector and other programs don't break it up. Every word to
  and from the DSP waits for the host port's flag, so an accelerated Falcon (a DFB1 at 50 MHz)
  plays exactly as a stock one.
- **MP3 on the DSP:** the whole decoder (MPEG-1, 2 and 2.5 layer III) runs on the Falcon's
  DSP56001, checked bit for bit against a model that meets ISO 11172-4's full accuracy.
- **Through an external clock:** with a clock on the DSP port (a SoundPool FDI: 44.1 or 48 kHz to
  S/PDIF), files at that rate play at their own rate, bit for bit.
- **Shares the DSP:** EPlay holds the DSP only while an MP3 plays.

*Phase 1 (releases 0.1 to 0.3) is the first try on a real Falcon: the sound core and a minimal
window. The playlist, the small player, the guide and resampling (files at 44.1 kHz with no
external clock) come in the next phases, named by what they bring: phase 2, every Falcon with
output and volume settings; 3, the player window; 4, playlists and folders; 5, the small player,
keys, MiNT and MagiC; 6, the TT and STE. Release numbers count releases, not phases.*

## Getting started

Download the latest `EPLAY-x.y.zip` from **[Releases](../../releases/latest)** and unpack it. It
contains:

| File | What it is |
|---|---|
| `EPLAY.PRG` | the program, for the Falcon |
| `icons/` | its desktop icon (`EPLAY.RSC`, or `EPLAY.ICN` and `EPLAYMK.ICN` for desktops that take them) |
| `NOTICES.TXT` | what EPlay includes under other licences |

1. Copy `EPLAY.PRG` anywhere, and start it; or drop a `.MP3`, `.WAV` or `.AIF` file on it.
2. **File > Open…** (Control+O) plays another file; **Play** and **Stop** are in the window and in
   the Play menu.

**EPLAY.LOG**, next to the program, says what EPlay measured and did: the external clock it
found, each file's format and length, how playing ended. While EPlay is young, please send it
with any report.

**Needs:** a Falcon030 (MP3 needs its DSP; WAV and AIFF its sound DMA) with TOS 4.0x or EmuTOS
(MagiC and MiNT not tried yet), 640×400 or larger.

**What it plays:**
- WAV (PCM, 8 to 32 bits, mono or stereo) and AIFF/AIFC (uncompressed);
- MP3: MPEG-1, 2 and 2.5 layer III, every bit rate, constant or variable;
- at the file's own rate where the Falcon has it: 44.1 or 48 kHz through an external clock, or
  the Falcon's own 49170, 32780, 24585 Hz and lower. A file at another rate plays at the nearest
  of those, so a little fast or slow, until phase 2 resamples it.

## How it reaches the servers

It doesn't: EPlay plays files, and needs no network.

## Building

```sh
sudo apt install gcc-m68k-linux-gnu a56 python3 sox lame   # Debian/Ubuntu; no MiNTLib needed
make            # binary/EPLAY.PRG, with the DSP's program (a56) built in
make test       # the MP3 model against minimp3, WAV and AIFF against sox
make dsptest    # the DSP's program against the model, bit for bit, on Hatari's DSP core
make dist       # dist/EPLAY-x.y.zip, as on the Releases page
make rig        # the Hatari rigs (below)
```

`binary/` and `dist/` are what make builds; they are never committed, and a build changes no tracked
file. A release: `tools/bump.py`, commit and push, then **Actions > Release > Run workflow** on `main`
with what is new. The workflow tests, builds, tags the commit `vX.Y` and publishes the
files and the zip on the Releases page. Builds in between are never handed out: to try a change, release it.

## Source

```
src/        WAV and AIFF (pcm.c); the MP3 frame parser and the decoder's C model (src/mp3)
dsp/        the MP3 decoder for the DSP56001 (mp3.a56)
atari/      the Falcon's sound: the ring and the matrix (snd.c), the tick (feed.c), the DSP (dspmp3.c)
ui/         GEM: the window, the menus, the player (play.c)
common/     the AERA library (from AERA-Atari, by git subtree)
data/       the standard's tables (Huffman codes, the synthesis window)
tools/      the table generators, the version bump
tests/      the model's and the parsers' tests, the DSP's, and the Hatari rigs (tests/hatari)
docs/       the design (DESIGN.md) and the icons
```

## Tests

- `make test`: the decoder's C model against minimp3 on 20 files made with sox and lame (MPEG-1,
  2 and 2.5; tones, sweeps, noise, clicks, short blocks, joint stereo), within ISO 11172-4's
  limits for full accuracy; WAV and AIFF converted against sox, sample for sample.
- `make dsptest`: `dsp/mp3.a56` on Hatari's DSP core against the model, bit for bit, with the
  DSP's share of each file.
- `tests/hatari/play_test.py`: EPlay in Hatari on `Falcon` (as Erez's, with the DFB1) and
  `Falcon-4` (a stock Falcon), with Hatari's sound recorded and checked: the tone at the right
  pitch in both channels, no gap, the length, silence after the end. A WAV, an AIFF, two MP3s,
  and the file selector held open while an MP3 plays.
- `tests/hatari/speed.py`: the family's speed rig, the longest stretch between event calls of
  each action, on a stock Falcon (`make prof` first). The figures are in `docs/DESIGN.md`.

## Licence

EPlay is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version. It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [COPYING](COPYING).

Copyright (C) 2026 AERA | Erez Yaary.

It includes, under their own licences (compatible with the GPL; see `NOTICES.TXT`):

- the MP3 standard's Huffman codes and synthesis window as FFmpeg transcribes them
  (`data/`, GNU LGPL 2.1 or later);
- for the tests only, not in `EPLAY.PRG`: minimp3 by Lion (lieff), CC0 1.0.

---

Part of **AERA** for Atari: [ECal](https://github.com/whomper/ECal) · [EContact](https://github.com/whomper/EContact) · [EMail](https://github.com/whomper/EMail) · [EFTP](https://github.com/whomper/EFTP) · [EHypView](https://github.com/whomper/EHypView) · [ClaudeST](https://github.com/whomper/ClaudeST) · [EWeb](https://github.com/whomper/EWeb) · [EUpdate](https://github.com/whomper/EUpdate) · [EPDF](https://github.com/whomper/EPDF) · 8-bit: [EDOS](https://github.com/whomper/EDOS) · [EFTP_XL](https://github.com/whomper/EFTP_XL) · Pi: [AERA-Gateway](https://github.com/whomper/AERA-Gateway)
