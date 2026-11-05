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
  DSP56001, checked bit for bit against a model that meets ISO 11172-4's full accuracy, and the
  DSP plays what it decodes itself (its SSI port to the DAC or the FDI): the 68030 only hands it
  the file's frames, when the DSP asks.
- **Through the FDI:** with a SoundPool FDI on the DSP port, EPlay sets it to the file's rate
  (44.1 or 48 kHz, as Cubase Audio does) and the DSP plays into it on its clock, bit for bit.
- **At its own pitch:** an MP3 at a rate the clock hasn't (44.1 kHz on a stock Falcon, which has
  49,170 Hz) is resampled on the DSP, and so is a WAV or AIFF at any rate below the DSP's
  output: the sound DMA carries its samples to the DSP through the matrix, so a stock Falcon's
  68030 only fills a ring (a 24-bit CD-rate AIFF takes about a quarter of it). Only a file faster
  than 49,170 Hz (96 kHz) plays at the nearest rate the Falcon has, unless an FDI gives its rate.
- **Shares the DSP:** EPlay holds the DSP only while it plays through it: an MP3, or a WAV or AIFF
  it resamples. When another program has it, that WAV plays at the nearest rate instead.

*Phase 1 (releases 0.1 to 0.3) was the first try on a real Falcon: the sound core and a minimal
window. Phase 2 (0.4 to 0.17) plays MP3 on the Falcon's DSP or through the FDI as Cubase Audio
does (the FDI set to the file's rate), chosen in the settings, with resampling on the DSP. (MP3 on
the 68030 was dropped: no 68030 keeps up with it.) Phase 3 (0.18 to 0.24) is the player window;
now phase 4, playlists and folders; then 5, the small player, keys, MiNT and MagiC; 6, the TT and
STE.*

## Getting started

Download the latest `eplay-x.y.zip` from **[Releases](../../releases/latest)** and unpack it. It
contains (every name in lowercase, as the family's are, so that MiNT finds them on an ext2 drive):

| File | What it is |
|---|---|
| `eplay.prg` | the program, for the Falcon |
| `eplay.hyp`, `eplay.ref` | the guide, for EHypView or ST-Guide: **Help** opens it (copy them to `C:\GUIDES\` or beside the program) |
| `icons/` | its desktop icon (`eplay.rsc`, or `eplay.icn` and `eplaymk.icn` for desktops that take them) |
| `notices.txt` | what EPlay includes under other licences |

1. Copy `eplay.prg` anywhere, and start it; or drop `.MP3`, `.WAV` or `.AIF` files, a folder
   (its subfolders too, in path order) or an `.M3U` playlist on it: they go into the list, and
   play one after another.
2. **File > Open…** (Control+O) opens **Add files**: choose the drive and the folder (or one
   of the last ten from **Folders**), narrow the list by type and by a name mask (`*` and `?`),
   sort it, and select files and folders with a click, Shift-click or a drag (**All** for every
   file). **Add** puts them at the list's end: a folder's music with its subfolders', a playlist's
   files in its order. Files dragged onto the window from an AV desktop (Thing, Jinnee,
   Teradesk) or under MiNT and MagiC are added too. A double click or **Return** plays a row,
   **Up** and **Down** choose one, **Delete** takes it out; a right click (or **Insert**) on a
   row opens its menu; **Edit > Clear the list** keeps only the file playing. Previous and Next
   go through the list. **File > Save playlist…** (Control+S) writes it as an `.M3U` (paths
   relative to the playlist's folder where they are in it), which other players read too.
   **Play > Shuffle** plays the list in a random order, each file once a round, the list left as
   it is; **Play > Repeat** goes from Off to All (the list again from its start) to One (the file
   again). EPLAY.INF keeps both. The window can be made taller for more rows. The list needs memory left
   over after NVDI and the accessories (a Falcon with TT RAM has plenty; EPlay always leaves
   40 KB of ST RAM for the AES): where there is none, the info line says "The list is full: no
   memory for more files." The bar shows the file's
   album art (unpacked on the DSP, then kept in `cache\` next to the program), its title
   and artist (from its tags), its bit rate and rate, and where it plays (FDI, DSP or DMA). The
   round button, or **Space**, plays and pauses (Play/Pause and Stop are in the Play menu too); a
   click or a drag on the position bar goes to that place in the file. The **volume** at the
   right is set the same way, or with **+** and **−** (3 dB a press); EPLAY.INF keeps it.
3. **View > Mini player** makes the window the bar alone, and back; EPLAY.INF keeps it.
4. **Options > Settings…** (Control+,) chooses the **Output**: Auto (the FDI if its driver
   answers, else the DSP), FDI or DSP. A change takes effect at the next Play. `EPLAY.INF`, next
   to the program, keeps it. Without the FDI an MP3 plays at 49,170 Hz; one the DSP can't keep
   up with there goes on by itself at 32,780 Hz, up to about 15 kHz (the info line shows the
   rate). Through the FDI the music is always whole.

**EPLAY.LOG**, next to the program, says what EPlay measured and did: the external clock it
found and how it set the FDI, each file's format and length, how playing ended, and how the DSP
and the 68030 kept up (the DSP's time to spare a frame, the share of the 68030 that playing
took). While EPlay is young, please send it with any report.

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
make            # binary/eplay.prg, with the DSP's program (a56) built in
make test       # the MP3 model against minimp3, WAV and AIFF against sox
make dsptest    # the DSP's program against the model, bit for bit, on Hatari's DSP core
make help HCP=/path/to/hcp   # binary/eplay.hyp from docs/GUIDE.md (hcp: common/tools/build-hcp.sh DIR)
make dist       # dist/eplay-x.y.zip, as on the Releases page (with the guide: HCP as for help)
make rig        # the Hatari rigs (below)
```

`binary/` and `dist/` are what make builds; they are never committed, and a build changes no tracked
file. A release: `tools/bump.py`, commit and push, then **Actions > Release > Run workflow** on `main`
with what is new. The workflow tests, builds, tags the commit `vX.Y` and publishes the
files and the zip on the Releases page. Builds in between are never handed out: to try a change, release it.

## Source

```
src/        WAV and AIFF (pcm.c), tags, playlists (m3u.c), the play order (order.c); the MP3 frame
            parser and the decoder's C model (src/mp3)
dsp/        the MP3 decoder for the DSP56001 (mp3.a56)
atari/      the Falcon's sound: the ring and the matrix (snd.c), the tick (feed.c), the DSP (dspmp3.c)
ui/         GEM: the window, the menus, the player (play.c)
common/     the AERA library (from AERA-Atari, by git subtree)
data/       the standard's tables (Huffman codes, the synthesis window)
tools/      the table generators, the version bump
tests/      the model's and the parsers' tests, the DSP's, and the Hatari rigs (tests/hatari)
docs/       the user guide (GUIDE.md, the source of eplay.hyp), the design (DESIGN.md), the icons
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
  the file selector and Settings held open while it plays; the list, a playlist, a folder tree,
  Repeat, Shuffle and Save playlist; and (not by default) 1,000 files as a folder, a tree and a
  playlist.
- `tests/hatari/pcm_test.py`: the sound's 68030 loops (16-bit WAV, 24-bit AIFF and WAV) against
  their C, bit for bit, on Hatari's 68030 (`make` in `tests/hatari` first).
- `tests/hatari/speed.py`: the family's speed rig, the longest stretch between event calls of
  each action, on a stock Falcon (`make prof` first), and each picture's time from when it is due
  to its last piece ("EPLAY picture"). The figures are in `docs/DESIGN.md`.

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
