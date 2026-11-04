# slowdisk: the rig's disk at the speed of Erez's cards

Hatari's GEMDOS drive (`--harddrive`) answers every file call in no
emulated time, so a speed-rig stretch never contains a disk wait. The
tables here are what `common/tests/calib` (CALIB.PRG, release rigtools-1)
measured on the real machines, microseconds a call:

- `falcon-bluescsi.txt`: Erez's Falcon (TOS 4.04, GEMDOS 0.30), BlueSCSI SD card, 4 Nov 2026.
- `falcon-idecf.txt`: the same Falcon with an IDE CF card, when it arrives.
- `st-*.txt`: his ST, with the card it uses, when it arrives.

The shim (an AUTO-folder program hooking trap #1) charges these times in
emulated time. Read the table as: sequential reads are cheap, everything
that changes the directory or the FAT is not (create, rename, delete about
0.48 s, make folder 2 s, the close after a write 1.3 to 2.9 s, a random
seek about 0.19 s).
