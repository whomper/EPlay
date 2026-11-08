# EPlay - user guide

EPlay plays music on the Atari Falcon: MP3 on the Falcon's DSP (or through a SoundPool FDI, as
Cubase Audio does), and WAV and AIFF. It plays from a list of files, folders and playlists, one
after another, and the music goes on while you work in other dialogs and menus.

## Getting started

| File | What it is |
|---|---|
| `eplay.prg` | the program, for the Falcon |
| `eplay.hyp`, `eplay.ref` | this guide, for EHypView or ST-Guide |
| `tls.ovl`, `CACERT.PEM` | the family's secure connections and the root certificates, for web radio stations at https addresses |
| `notices.txt` | the licences of the parts EPlay is made with |
| `icons/` | the desktop icons: `eplay.rsc` (`eplaynv.rsc` under NVDI with its own palette), or `eplay.icn` and `eplaymk.icn` for desktops that take them |

- **Install:** copy `eplay.prg` into a folder of its own, such as `C:\EPLAY\`. It keeps its
  settings (`EPLAY.INF`), its log (`EPLAY.LOG`), the album art it has unpacked (`cache\`) and any
  `CRASH.LOG` beside itself. Copy `tls.ovl` and `CACERT.PEM` beside it too, for stations at
  https addresses.
- **This guide:** copy `eplay.hyp` and `eplay.ref` into `C:\GUIDES\`, or beside `eplay.prg`.
  **Help** in EPlay opens it in EHypView or ST-Guide.
- **Play something:** start EPlay and choose **File > Open...** (^O), or drop MP3, WAV or AIFF
  files, a folder or an `.M3U` playlist on EPlay's window or on `eplay.prg`. What you give goes
  into the list, and the first file plays.

**Needs:** a Falcon030 with TOS 4.0x or EmuTOS, or a TT, and a screen 640 pixels wide or more.
MP3 needs the Falcon's DSP; WAV and AIFF play on the Falcon's sound DMA, or on the TT's 8-bit DMA
sound (interpolated up to 50 kHz and dithered to 8 bits by the 68030). On a TT MP3 files show dim
in the list and are passed over. On an STE (or a Mega STE), start `eplayst.prg`, the 68000's
edition: WAV and AIFF on its 8-bit DMA sound (a CD's 44.1 kHz at 50 kHz, the nearest sample), its
volume on the STE's own volume control; MP3 files show light and are passed over. While a CD-rate
file plays, the music takes about half of the 68000, so windows and menus answer slowly then
(EPlay is being made faster there); paused, they answer as usual. A Falcon with TT RAM (a DFB1 and the like) has room for very long
lists; a stock Falcon plays as well.

**What it plays:** MP3 (MPEG-1, 2 and 2.5 layer III, every bit rate, constant or variable); WAV
(PCM, 8 to 32 bits, mono or stereo); AIFF and AIFC (uncompressed). A file at a rate the Falcon's
clock hasn't, such as 44.1 kHz without an external clock, is resampled on the DSP and heard at its
own pitch.

## What EPlay does

- **Steady sound.** The music is fed by an interrupt from a queue of seconds to minutes held in
  memory, so menus, dialogs, the file selector and other programs don't break it up. An
  accelerated Falcon (a DFB1 at 50 MHz) plays exactly as a stock one.
- **MP3 on the DSP.** The whole MP3 decoder runs on the Falcon's DSP56001, and the DSP plays what
  it decodes itself: the 68030 only hands it the file, a piece at a time, when the DSP asks.
- **Through the FDI.** With a SoundPool FDI on the DSP port, EPlay sets it to each file's rate
  (44.1 or 48 kHz, as Cubase Audio does) and the music goes out bit for bit.
- **At its own pitch.** A file at a rate the Falcon's clock hasn't, such as 44.1 kHz on a stock
  Falcon (49,170 Hz), is resampled on the DSP; a WAV or AIFF reaches the DSP through the sound
  DMA, so the 68030 has little to do. Only a file faster than 49,170 Hz (96 kHz) plays at the
  nearest rate the Falcon has, unless an FDI gives its rate.
- **Shares the DSP.** EPlay holds the DSP only while it plays through it, and gives it back on
  Pause and Stop: see [Settings](#settings).

## Windows and menus

EPlay has one window. **The bar** across its top shows what plays: the album art (unpacked on the
DSP, then kept in `cache\`), the title and artist from its tags, its bit rate and rate, and where
it plays: **FDI**, **DSP** or **DMA**.

- **Play/Pause**, the symbol in the middle (in the accent colour), plays and pauses; the arrows
  beside it go to the previous and next file.
- **The position bar:** click or drag on it to go to that place in the file.
- **The volume** at the right: click or drag on it, or press + and -.
- **The info line** under the window's title says what EPlay is doing: "Playing", "Paused", the
  output's rate, or why a file can't play.

**The list** under the bar holds the files to play, with their title, artist, album and length (read
in the background, a file at a time). A click selects a row; a double click, or Return, plays it.
A right click, or Insert, opens the row's menu. Make the window taller for more rows.

- **File:** Open... (^O) opens **Add files**; Open URL... (Shift+^O) opens a web radio station;
  Save playlist... (^S) writes the list as an `.M3U`; Quit (^Q).
- **Edit:** Remove from the list (Delete); Clear the list keeps only the file that plays.
- **View:** Mini player: the window becomes the bar alone, and back. `EPLAY.INF` keeps it.
- **Play:** Play/Pause (Space), Stop (Clr Home), Previous (Shift+Left), Next (Shift+Right);
  **Shuffle** plays the list in a random order, each file once a round, the list itself left as
  it is; **Repeat** goes round Off, All (the list again from its start) and One (the file again).
  `EPLAY.INF` keeps both.
- **Options:** Direct connection or Through the Gateway, how a station is reached (see **Web
  radio**); Settings... (^,).
- **Help:** this guide.

**Add files** is EPlay's own file dialog. Choose the drive and the folder (or one of the last ten
from **Folders**), narrow the list by type and by a name mask (`*` and `?`), and sort it. Select
files and folders with a click, Shift-click or a drag; **All** selects every file. **Add** puts them
at the list's end: a folder with its subfolders, in the order of their names; a playlist's files in
its order. Return opens a folder when it is the only one selected, or nothing is. The music goes on
while the dialog is open.

**Dropped files** go into the list too: drag MP3, WAV and AIFF files, a folder or an `.M3U`
playlist onto EPlay's window from an AV desktop (Thing, Jinnee, Teradesk), or under MiNT and
MagiC.

**Playlists** are `.M3U` files, which other players read and write too. A line is a file, its path
whole (`C:\MUSIC\A.MP3`) or relative to the playlist's folder (`LIVE\A.MP3`, `..\B.MP3`). EPlay
saves each file's length and tags with it, and its path relative to the playlist's folder where it
is in it, so the playlist and its music can move together.

## Web radio

**File > Open URL...** (Shift+^O) plays a station on the Falcon: type its address
(`http://radio.example.com:8000/live`, or a `.pls` or `.m3u` station list's), or choose one of the
last ten stations under it. The field keeps what was last typed in it. The station goes into the list, its name on its row, and plays. EPlay
gathers about three seconds of it first, so the network's pauses aren't heard, and connects again
by itself if the line drops, while what it gathered plays on. Esc or Undo stops a connection that
takes too long.

The bar shows the song playing (the station sends its titles) and the station's name under it;
**Live** and the time listened stand where a file's length would. Where the station names a picture
with its titles (some send each song's cover, some their logo), it takes the album art's place: on a
Direct connection EPlay fetches it and the 68030 unpacks it, which takes a few seconds (the DSP is
playing); through the Gateway it comes small and quickly. A station that names no picture shows
its logo when a Gateway's address is in **Settings** (on a Direct connection too): the Gateway looks
it up, and a station it knows none for keeps the disc. A picture seen before comes from `cache\`. **Pause** ends the connection,
and **Play** connects again and plays the station live from then. Station addresses in an `.M3U`
playlist play as its files do.

The **Options** menu chooses how a station is reached:

- **Direct connection** (the default): EPlay connects to the station itself. It plays MP3
  stations, at http and https addresses (https with `tls.ovl` and `CACERT.PEM` beside EPlay;
  the secure connection takes a few seconds more to start; its arithmetic is done on the DSP when
  no music holds it).
- **Through the Gateway:** the AERA Gateway fetches the station and passes it on as MP3, so
  stations that send AAC or Ogg, or HLS, play too. Give the Gateway's address and port
  in **Settings**. The Gateway's radio service comes with AERA-Gateway 1.33 (`aera-gateway install
  radio`); a station's picture through it, with the release after.

Web radio needs a network: STinG or MiNTnet with a network card. With STinG, set the **Delay** in
`STING.CPX` to 10 on a Falcon or TT; it polls the card that often. Web radio needs the Falcon's
DSP: on a TT or an STE a station's row shows dim and is passed over.

## Keyboard

| Key | What it does |
|---|---|
| Space | Play and pause |
| Left, Right | 10 seconds back and forward in the file |
| Shift+Left, Shift+Right | The previous and the next file |
| Clr Home | Stop |
| + and - | The volume, 3 dB a press |
| Up, Down | Select the row above or below |
| Return, Enter | Play the selected row |
| Delete | Take the selected row out of the list |
| Insert | The selected row's menu |
| ^O | Open: Add files |
| Shift+^O | Open URL: a web radio station |
| ^S | Save playlist |
| ^, | Settings |
| ^Q, ^U | Quit |
| Help | This guide |

In a dialog, Esc and Undo cancel (in a text field, Esc empties it and Undo cancels), Return
chooses the default button. The same keys work in the
mini player.

## Settings

**Options > Settings...** (^,) chooses the **Output**, how EPlay plays the music:

- **Auto:** the FDI if its driver answers, else the DSP.
- **FDI:** a SoundPool FDI on the DSP port, set to each file's rate (44.1 or 48 kHz), as Cubase
  Audio does. Through the FDI the music is always whole. Greyed when its driver isn't loaded.
- **DSP:** the Falcon's own output, at 49,170 Hz without an external clock. An MP3 the DSP can't
  keep up with there goes on by itself at 32,780 Hz, up to about 15 kHz; the info line shows the
  rate.

**Through the Gateway: address, port** are the AERA Gateway's, for stations reached through it
(the Options menu).

A change takes effect at the next Play. `EPLAY.INF`, beside the program, keeps it with the volume,
the mini player, Shuffle, Repeat, the Add files dialog's folders, the way stations are reached and
the last ten stations.

EPlay holds the DSP only while it plays through it, and gives it back on Pause and Stop. When
another program has it, an MP3 says so ("The DSP is in use by another program.") and a WAV plays
at the nearest rate instead.

## Troubleshooting

- **"The DSP is in use by another program."** Another program holds the Falcon's DSP: close it,
  or pause what it plays, and press Play again.
- **The music plays a little fast or slow.** A WAV or AIFF at a rate the Falcon hasn't is
  resampled on the DSP; when the DSP is in use by another program it plays at the nearest rate.
- **"The list is full: no memory for more files."** The list needs memory left over after NVDI
  and the accessories. EPlay always leaves 40 KB of ST RAM for the AES. A Falcon with TT RAM has
  room for thousands of files.
- **The FDI is greyed in Settings.** Its driver didn't answer: load the FDI's program at boot.
- **"The station doesn't answer."** Check the address, and that the network is up (STinG or
  MiNTnet). Through the Gateway: **"The Gateway doesn't answer: is it on?"**
- **"The station sends AAC: play it through the Gateway."** (or Ogg, or HLS) EPlay plays MP3
  stations directly; choose **Options > Through the Gateway** for these.
- **"A secure station needs TLS.OVL and CACERT.PEM beside EPlay."** The station's address is
  https (or it sends EPlay there): copy both from EPlay's release beside `eplay.prg`.
- **"There's no network: EPlay needs STinG or MiNTnet."** Web radio needs a TCP/IP stack and a
  network card.
- **A station stops now and then.** The network fell behind for longer than the three seconds
  EPlay gathered. With STinG, check the Delay in `STING.CPX` (10 on a Falcon).
- **`EPLAYNET.LOG`**, beside the program, says how each connection to a station went (the secure
  connection's steps among them): please send it with a report about web radio.
- **`EPLAY.LOG`**, beside the program, says what EPlay found and did: the external clock and how it
  set the FDI, each file's format and length, how playing ended, and how the DSP and the 68030 kept
  up. Please send it, and any `CRASH.LOG`, with a report.

## About

EPlay is by AERA | Erez Yaary, one of the AERA family of Atari programs (ECal, EContact, EMail,
EFTP, EHypView, ClaudeST, EWeb, EUpdate, EPDF and EPlay), which look and behave as one.

Its MP3 decoder runs on the Falcon's DSP56001, written for EPlay and tested bit for bit against a
model of its arithmetic, which is held to the MP3 standard's full accuracy. EPlay is free software
under the GNU General Public License, version 2 or later; `notices.txt` names the licences of the
parts it is made with.

The source, the releases and the design: github.com/whomper/EPlay
