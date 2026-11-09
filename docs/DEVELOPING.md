# Developing EPlay

What the README leaves out: the source in full, the tests, and how a release is made. The design
and the measurements are in [DESIGN.md](DESIGN.md); the user guide is [GUIDE.md](GUIDE.md).

## Building

```sh
sudo apt install gcc-m68k-linux-gnu a56 python3 sox lame   # Debian/Ubuntu; no MiNTLib needed
make            # binary/eplay.prg, with the DSP's program (a56) built in
make st         # binary/eplayst.prg, the STE's edition
make test       # the host tests
make dsptest    # the DSP's program against the model, bit for bit, on Hatari's DSP core
make help HCP=/path/to/hcp   # binary/eplay.hyp from docs/GUIDE.md (hcp: common/tools/build-hcp.sh DIR)
make dist       # dist/eplay-x.y.zip, as on the Releases page
make prof       # build/prof/eplay.prg, for the speed rig
```

`binary/` and `dist/` are what make builds; they are never committed, and a build changes no
tracked file. A release: `tools/bump.py`, commit and push, then **Actions > Release > Run workflow**
on `main` with what is new. The workflow tests, builds, tags the commit `vX.Y` and publishes the
files and the zip on the Releases page. Builds in between are never handed out.

## Source

```
src/        WAV and AIFF (pcm.c), tags and ReplayGain, playlists (m3u.c), the play order
            (order.c), web radio's parsing (stream.c); the MP3 frame parser and the decoder's
            C model (src/mp3)
dsp/        the MP3 decoder for the DSP56001 (mp3.a56)
atari/      the sound: the ring and the matrix (snd.c), the tick (feed.c), the DSP (dspmp3.c),
            the log (elog.c)
ui/         GEM: the window and the menus (main.c, bar.c), the player (play.c), Settings
            (prefs.c), web radio (netsrc.c, urldlg.c, sart.c)
radio/      radiogate, the Gateway's radio service (handed to AERA-Gateway)
common/     the AERA library (from AERA-Atari, by git subtree)
data/       the standard's tables (Huffman codes, the synthesis window)
tools/      the table generators, the version bump
tests/      the host tests, the DSP's, and the Hatari rigs (tests/hatari)
docs/       the guide, the design, this file, the pictures
```

## Tests

- `make test`: the decoder's C model against minimp3 on 20 files made with sox and lame (MPEG-1,
  2 and 2.5), within ISO 11172-4's limits for full accuracy; WAV and AIFF against sox, sample for
  sample; tags and ReplayGain; web radio's parsing under AddressSanitizer, its fuzz corpus
  replayed (`make fuzz` runs the fuzzer).
- `make dsptest`: `dsp/mp3.a56` on Hatari's DSP core against the model, bit for bit, gapless joins
  and ReplayGain included.
- `tests/hatari/play_test.py`: EPlay in Hatari on a stock Falcon and others, its sound recorded and
  checked (pitch, gaps, length, level): files, playlists, folders, Shuffle, Repeat, gapless,
  ReplayGain, the dialogs and the mini player.
- `tests/hatari/stream_test.py`: web radio in Hatari from a station of its own: titles,
  reconnecting, redirects, chunked answers, https, pictures, and the Gateway.
- `tests/hatari/pcm_test.py`: the sound's 68030 loops against their C, bit for bit.
- `tests/hatari/speed.py`: the family's speed rig, each action's longest stretch between event
  calls (`make prof` first); the figures are in DESIGN.md.
- `tests/hatari/docs_shots.py`: the README's pictures.
- `radio/test_radiogate.py`: the Gateway's radio service against stations of its own.
