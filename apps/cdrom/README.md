# apps/cdrom - the Multimedia PC upgrade

The ARM/AT gets a double-speed ATAPI CD-ROM drive on the secondary IDE channel, and ARM-DOS the
1993 software for it:

| file | what |
|---|---|
| `ARMCD.SYS` | the CD-ROM device driver (`drv/`), the "MSCDEX device driver" request set |
| `ARMCDEX.EXE` | the CD-ROM extensions (`cdex/`): INT 2Fh AX=15xxh and the network-redirector hook that makes the disc drive D: |
| `CDPLAY.EXE` | the ARM-DOS CD Player (`cdplay/`): full screen, or resident with `/R` (Ctrl+Alt+C) |
| `CDPLAY.INI` | CDPLAY's disc database (C:\DOS, made with the disc) |
| the disc | "ARM-DOS Multimedia Sampler '93" (`disc/`, `tools/`), below |

All three programs are original work for this project (no Microsoft code); the drive itself is
`emu/dev/atapi.mjs` (see emu/README.md), the page's drive and disc box are `web/js/cdrom.js`.

## Setting it up

```
CONFIG.SYS     DEVICE=C:\DOS\ARMCD.SYS /D:ARMCD001
AUTOEXEC.BAT   SBMIX /INIT /Q                  (already there: the SB16's CD input at -14 dB)
               C:\DOS\ARMCDEX /D:ARMCD001
               C:\DOS\CDPLAY /R                (optional: the pop-up player, 26 KB)
```

`LASTDRIVE` defaults to E, so D: is free on the standard machine.

## ARMCD.SYS

`DEVICE=[d:][path]ARMCD.SYS [/D:devname] [/Q]` - `/D:` the device name ARMCDEX is told about
(default ARMCD001), `/Q` no banner. INIT resets the drive (ATA DEVICE RESET: the ATAPI
signature 14h/EBh), reads IDENTIFY PACKET DEVICE and says

```
ARM-PC CD-ROM Device Driver  Version 1.10
Copyright (C) Europa Micro Systems 1993.  All rights reserved.
  Unit 0: ARM-PC CD-ROM DRIVE  1.00  (secondary IDE, master)
  Device name: ARMCD001
```

or "No CD-ROM drive found on the secondary IDE channel. ARMCD.SYS not installed." The driver is a
character device (attribute C800h) with the CD-ROM header extension (drive letter, units) and
implements INIT; IOCTL input 0 (device header), 1 (head location, HSG/Red Book), 4 (audio
channel info), 5, 6 (device status: door, lock, raw, audio, Red Book, no disc), 7 (sector
size), 8 (volume size), 9 (media changed), 10 (audio disk info), 11 (audio track info), 12
(Q channel), 14 (UPC/EAN = the disc's MCN), 15 (audio status); input/output flush; IOCTL
output 0 (eject), 1 (lock/unlock), 2 (reset), 3 (audio channel control = MODE SELECT page 0Eh),
5 (close tray); open/close; READ LONG (cooked 2048 and raw 2352 = READ CD), READ LONG
PREFETCH, SEEK, PLAY AUDIO (HSG or Red Book), STOP AUDIO (the first pauses, a second stops),
RESUME AUDIO; WRITE LONG answers write-protect. The request status has BUSY set while audio
plays. Packets: TEST UNIT READY, REQUEST SENSE, INQUIRY, START STOP UNIT, PREVENT/ALLOW, READ
CAPACITY, READ(10), READ CD, SEEK, READ TOC, READ SUB-CHANNEL, GET EVENT STATUS, PLAY AUDIO(12),
PAUSE/RESUME, STOP PLAY, MODE SENSE/SELECT(10). Unit attention (media change, reset) is
retried and remembered for IOCTL 9; "becoming ready" (spin-up) is waited for up to 10 s.
Polled with nIEN set, like most 1993 vendor drivers (so INT 77h/IRQ 15 needs no handler);
long waits sleep in WFI between timer ticks. 6.5 KB resident.

## ARMCDEX.EXE

`ARMCDEX /D:devname [/D:devname ...] [/L:letter] [/M:buffers] [/V] [/E] [/S] [/K]`

```
ARM CD-ROM Extensions Version 2.21
Copyright (C) Europa Micro Systems 1986-1993. All rights reserved.
        Drive D: = Driver ARMCD001 unit 0
```

It opens each named driver, asks it for its header (IOCTL input 0 through INT 21h AX=4402h),
takes the next free drive letter per unit (or from `/L`), marks that CDS "network + valid"
(C000h) and stays resident (INT 21h AH=31h; the environment is freed; 23 KB with the default
4 sector buffers, `/M:n` = 1-32 buffers of 2 KB, `/V` shows the sizes; `/E /S /K` are
accepted and ignored). Errors: `Device driver not found: 'X'.`, `No valid CDROM device drivers
selected`, `ARM CD-ROM Extensions already installed`, `Not enough drive letters available`.

* **INT 2Fh AX=15xxh**: 1500h installation check (BX drives, CX first letter), 1501h drive
  device list, 1502h-1504h copyright/abstract/bibliographic file names (the PVD fields as
  recorded, `COPYRGHT.TXT;1`), 1505h read volume descriptor, 1508h absolute read, 1509h write
  (error 5), 150Bh drive check (BX=ADADh, AX<>0 for a CD drive), 150Ch version (BX=0215h =
  2.21), 150Dh drive letters, 150Eh volume descriptor preference, 150Fh directory entry,
  1510h send device request. Pointers are flat (ARCH.md: ES:BX -> BX, SI:DI buffer -> DI).
* **The redirector** (INT 2Fh AH=11h, below): open/read/close/seek, find
  first/next (DOS attribute rules; the volume label from the PVD), get attributes, CHDIR,
  disk space (0 bytes free), process termination; RMDIR/MKDIR/DELETE/RENAME/CREATE/WRITE/
  SET ATTRIBUTES answer "access denied". ISO 9660 and High Sierra, 8.3 names from the
  identifiers (";1" and a trailing dot dropped), directories and files read-only, the
  "existence" bit as hidden, dates from the directory records. So DIR, TYPE, COPY, XCOPY, CD,
  running programs and batch files from the CD all work, relative paths included.
* **Media**: before every file-system call the driver's "media changed" is asked; a changed
  disc drops the sector cache and re-reads the volume descriptors. A driver error goes to DOS's
  critical-error handling through INT 2Fh AX=1206h (below): `Not ready
  reading drive D` / `Abort, Retry, Fail?`, with Retry really retrying (put a disc in, press R).

### The kernel's redirector interface

ARMCDEX is a network redirector, as the 1990s CD-ROM extensions were; the kernel side is
kernel/dos/redir.c. A drive whose CDS flags (+43h) have **bit 15 (CDS_NET) and bit 14
(CDS_VALID)** set is redirected: the TSR sets that up itself through the LoL (AH=52h, +16h):
`flags = C000h`, `path = "D:\"`, `bsoffset (+4Fh) = 2`, `dpb (+45h) = 0`. For such a drive
the kernel never looks for a DPB, never does media checks or buffer I/O, and sends every
file-system operation to INT 2Fh AH=11h. Local drives take exactly the old code paths.

**Calling convention.** DOS 4 passes most inputs in SDA fields; ARM-DOS has no DOS-layout
SDA, so the pointers travel in registers. The kernel calls the INT 2Fh vector from inside
INT 21h with a fresh frame:

| register | meaning |
|---|---|
| r0 = AX | 11xxh, the function |
| r4 (SI) | the canonical path as `canon_path()` makes it (`"D:\DIR\FILE.EXT"`, upper case; wildcards only in the last component) |
| r3 (DX) | the canonical new name (1111h) or the buffer (1108h/1109h) |
| r5 (DI) | the SFT for handle functions |
| r6 (BP) | the drive's CDS |
| r1 (BX), r2 (CX) | per function |

Return: **CF clear = success; CF set = failure with AX = a DOS error code**, which the
program sees as for a local drive. If nobody answers (the chain ends, or a handler returns
CF set with AX still 11xxh) the kernel returns error 32h.

| AX | function | inputs | outputs |
|---|---|---|---|
| 1101h / 1103h | RMDIR / MKDIR | r4, r6 | |
| 1105h | CHDIR | r4, r6 | CF clear if it is a directory; the kernel updates the CDS text |
| 1106h / 1107h | CLOSE / COMMIT | r5 | (CLOSE when the last reference goes) |
| 1108h / 1109h | READ / WRITE | r5, r2 = count, r3 = buffer | r2 = bytes done; the redirector advances the position (+15h) |
| 110Ch | DISK SPACE | r6 | r0-r3 as AH=36h returns them |
| 110Eh / 110Fh | SET / GET ATTRIBUTES | r4 (r2 = attributes) | get: r0 = attributes, r1 = size, r2 = time, r3 = date |
| 1111h | RENAME | r4 = old, r3 = new | |
| 1113h | DELETE | r4 (wildcards) | |
| 1116h / 1117h | OPEN / CREATE | r4, r5 = SFT, r2 = mode / attributes | the redirector fills the SFT (attr, flags with **8000h SF_REMOTE** + drive, time, date, size, position 0, FCB-form name; private fields allowed); the kernel sets ref_count, mode and owner |
| 111Bh / 111Ch | FIND FIRST / NEXT | r4 = pattern, r2 = attributes, r1 = 43-byte find DTA | +00h = drive \| 80h, +01h-14h private state, then attr, time, date, size, name; error 12h at the end |
| 1122h | process termination | r1 = PSP | (optional) |

The kernel maps AH=3Ch/3Dh/5Ah/5Bh/6Ch and the EXEC loader to 1116h/1117h; handle I/O on an
SF_REMOTE SFT to 1106h-1109h (seeks are local, SEEK_END from the size); 4Eh/4Fh and the FCB
searches 11h/12h (COMMAND's DIR and VOL; the search state is kept in the FCB's reserved
bytes) to 111Bh/111Ch; 39h/3Ah/3Bh/41h/56h/43h to the directory and attribute functions;
36h/1Bh/1Ch to 110Ch. IOCTL 4409h answers DX = 1000h (remote); 4408h, 440Dh and 69h fail
with error 1; other FCB I/O on a remote drive fails with error 5.

**INT 2Fh AX=1206h, "invoke critical error"**, called by a redirector from inside a callout:
BH = the INT 24h AH flags, BL = drive (0 = A:), DI low byte = the driver error, SI = the
device header (0: COMMAND.COM names the drive). Runs the kernel's normal critical-error
path and returns AL = 0 ignore, 1 retry, 3 fail (abort ends the program).

The ATAPI drive raises IRQ 15 only with nIEN clear; ARMCD.SYS sets nIEN and polls, so the
BIOS and kernel need no IRQ 15 support.

## CDPLAY.EXE

`CDPLAY` (full screen), `CDPLAY /R` (install the pop-up), `CDPLAY /U` (remove it), `CDPLAY /?`.

The full-screen player: a grey front panel with a black LCD (7-segment track number and
MM:SS digits drawn with half-block characters, the unlit segments faintly visible; PLAY /
PAUSE / STOP / SHUFFLE / REPEAT lamps; the track title and elapsed / length; the time on the
disc), the disc title and total time, 3-D buttons (Play, Pause, Stop, Prev, Next, Eject,
Shuffle, Repeat, the mouse works if MOUSE is loaded) and a scrolling track list with titles and
lengths. Keys: `1`-`9` play that track, Up/Down/Home/End select and Enter plays it, Space or
`P` play/pause, `A` pause, `S` stop, Left/Right (or `V`/`N`, `,`/`.`) previous/next (previous
within the first 3 s, else back to the start of the track), `E` eject / load, `H` shuffle,
`R` repeat, Esc/`Q`/F10 leave - the music keeps playing, as a CD drive plays by itself.
Normal play runs from the chosen track to the end of the audio; shuffle plays single tracks
in a random order; repeat starts again at the end. Paused, the digits blink. A disc change
(from the page, or `E`) is noticed and the TOC re-read.

Track titles come from **CDPLAY.INI** next to CDPLAY.EXE, a disc database in the style of the
Windows 3.1 CD Player's `CDPLAYER.INI`: `[ID]` sections with `title=`, `artist=`,
`numtracks=`, and `0=`, `1=`, ... the titles of tracks 1, 2, ...; the ID is 8 hex digits,
`ntracks << 24` plus the Red Book frame address (LBA + 150) of every track and of the
lead-out (`disc_id()` in cdplay.c; `tools/build-disc.mjs` writes the Sampler's entry).
Without an entry the tracks are "Track n".

**The pop-up** (`CDPLAY /R`, 26 KB resident: the whole program and a 3 KB stack):
Ctrl+Alt+C opens a 54x14 window with the small LCD, the track title and the keys above over
any text-mode program; Esc puts the screen (and the cursor) back exactly. In graphics modes
(and 40 columns) it refuses with an 880 Hz beep. Like apps/popup it hooks INT 15h AH=4Fh
(the hot key), 09h/08h/28h (when to pop up), 10h/13h (BIOS busy), 2Fh (AH=C7h installation
check; and "inside the CD-ROM extensions" for AH=11h/15h) and 21h. It pops up only when no
critical error, BIOS video/disk call or CD-ROM extensions call is in progress, and DOS is
either not busy (InDOS = 0), idle at INT 28h, or waiting for the keyboard in AH=3Fh from
handle 0 - the way ZORK reads its commands, where DOS 4 issues no INT 28h (checked against the
DOS 4 source: IdleInt is cleared for functions above 0Ch). In that last case the pop-up makes
no DOS calls at all (it talks only to the extensions' device-request function, which does not
enter DOS), so titles of a disc inserted meanwhile are read at the next safe moment. With
shuffle or repeat on, the resident player checks every 2 s (at those safe moments) whether the
track ended and starts the next one. `/U` restores the eight vectors (if still its own) and
frees the memory.

## Deviations

* The critical-error message names the drive (`Not ready reading drive D`); ARMCDEX passes no
  device header to 1206h (else COMMAND.COM names the device), and there is no `CDR101:`-style
  prefix.
* CDPLAY's track lengths come from the TOC, so they include the 2-second pregap of the next
  track (3:53 for Cipher, whose audio is 3:51) - as the CD players of the time showed them.
* ARMCDEX cannot be removed from memory (neither could its model); the redirector ignores
  1121h/1123h/111Dh/111Eh/1120h/112Eh (not used by the kernel).
* BIOS SETUP's summary screen does not list the CD-ROM (bios/ is not ours).

## Tests

`make cdrom-test` (all of these are also in `make test`):

| test | what |
|---|---|
| `emu/tests/machine/atapi.mjs` | the drive at port level (83 checks; in `emu/tests/run-all.mjs`) |
| `tests/iso9660.mjs`, `tests/disc.mjs` | the ISO builder (checked with xorriso) and the built disc (`make cdrom-disc-test`) |
| `tests/redir-mock.mjs` | the kernel's redirector callouts with a mock redirector (`tests/mockred.c`) |
| `tests/cdex.mjs` | ARMCD.SYS + ARMCDEX with COMMAND.COM on the Sampler: banners and errors, VOL/DIR/TYPE/COPY (byte-compared with the ISO)/CD/DEL on D:, MENU.BAT and ZORK run from the CD, the 15xxh API and IOCTLs through `tests/cdapi.c`, the position advancing in emulated time, the resident size, "Not ready" with the tray open, Retry after a new disc (media change), Fail |
| `tests/cdplay.mjs` | CDPLAY: screen, titles, every key, mouse clicks (with MOUSE.COM), pause/resume/next/prev/stop/eject, shuffle and repeat at track ends on a short test disc, the audio the machine renders (silent/sounding at the right times, cross-correlated with the track's WAV at the position the drive reports; `build/cdrom-test/cdplay.wav`), the pop-up at the prompt and over ZORK (screens restored exactly), refused in mode 13h, repeat while closed, `/U` |
| `web/tests/test_cdrom.py` | the page (Playwright): insert the disc, DIR D:, CDPLAY plays track 2 and the audio path receives samples |

Screenshots go to `build/cdrom-test/`.

## The disc: ARM-DOS Multimedia Sampler '93

A mixed-mode CD: track 1 is an ISO 9660 data track, tracks 2-5 are Red Book audio.
`make cdrom-disc` builds it into `build/cdrom/sampler93/` (`make cdrom-disc-test` checks it):

| file | what |
|---|---|
| `disc.json` | the manifest the emulator's ATAPI drive reads (below) |
| `data.iso` | track 1, ISO 9660 level 1, volume `SAMPLER93`, 583 sectors (1.1 MB), 46 files |
| `tNN.wav` | audio track NN, 44.1 kHz 16-bit stereo, padded with silence to whole 588-frame sectors (node tests) |
| `tNN.opus` | the same as Ogg Opus, 96 kbps VBR (the web page's download, 1.9-3.2 MB per track) |
| `tNN.mp3` | the same as MP3 128 kbps (fallback for browsers without Opus in `decodeAudioData`) |

**Manifest** (`disc.json`): `{ id, title, volumeId, mcn, tracks: [...] }`; track 1 is
`{ number: 1, type: "data", file: "data.iso", sectors }`, each audio track
`{ number, type: "audio", pregap: 150, sectors, frames, files: { wav, opus, mp3 }, title, artist,
licence, licenceUrl, source, seconds }` with `sectors = frames / 588` (frames = 44.1 kHz stereo
sample frames). Layout: track 1 at LBA 0, each later track starts at the previous start +
previous sectors + its pregap (2 s of silence). Lead-out at LBA 60652 (13:30 on the disc) with
the Linux (GCC 14) build; the data track's size follows the programs on it, so another compiler
moves the audio tracks and the lead-out by a few sectors (the tests take them from `disc.json`).

### Audio tracks

| # | title | artist | length | licence | source |
|---|---|---|---|---|---|
| 2 | Cipher | Kevin MacLeod | 3:51 | CC BY 4.0 | https://incompetech.com/music/royalty-free/index.html?isrc=USUAN1100844 |
| 3 | Local Forecast - Elevator | Kevin MacLeod | 3:09 | CC BY 4.0 | https://incompetech.com/music/royalty-free/index.html?isrc=USUAN1300012 |
| 4 | Funkorama | Kevin MacLeod | 3:21 | CC BY 4.0 | https://incompetech.com/music/royalty-free/index.html?isrc=USUAN1100474 |
| 5 | Eighties Action | Kevin MacLeod | 2:51 | CC BY 4.0 | https://incompetech.com/music/royalty-free/index.html?isrc=USUAN1100243 |

Attribution (as the licence asks, and as CREDITS.TXT on the disc and the web page give it):
*"&lt;title&gt;" Kevin MacLeod (incompetech.com), Licensed under Creative Commons: By
Attribution 4.0 License, http://creativecommons.org/licenses/by/4.0/*. The tracks are
loudness-normalised (the only change): a derivative that CC BY permits.

Provenance: downloaded 2026-09-24 from `https://incompetech.com/music/royalty-free/mp3-royaltyfree/`
(the originals are fetched unmodified by tools/fetch-3rdparty.sh into `3rdparty/cdrom-music/`,
renamed to lower case; original names,
ISRCs and sha256 sums are in `disc/music/tracks.json`, which the build and the test check).
The licence was read on incompetech.com's track pages ("Licensed under Creative Commons: By
Attribution 4.0 License") and its licences page. FreePD.com (the CC0 source first planned)
closed in 2025, so CC BY 4.0 was used instead. 32 MB of originals (320 kbps MP3).

Build: `tools/build-disc.mjs` runs ffmpeg: two-pass EBU R128 `loudnorm` (-16 LUFS, -1.5 dBTP,
linear), soxr resampling to 44.1 kHz, then libopus 96 kbps and libmp3lame 128 kbps from the
padded WAV. Audio is only re-made when an original or the script changes (~35 s); the ISO is
rebuilt in a second.

### The data track

1993 style, no AUTORUN: `README.TXT`, `MENU.BAT` (a CP437 box menu; items are `1.BAT`-`8.BAT`,
since DOS 4 has no CHOICE), `CREDITS.TXT`, `COPYRGHT.TXT` / `ABSTRACT.TXT` / `BIBLIO.TXT` (named in
the primary volume descriptor's copyright/abstract/bibliographic fields; `COPYRGHT` because level
1 names are 8.3), and:

| directory | contents | licence |
|---|---|---|
| `DEMO\` | build/DEMO.EXE, DEMO.NFO | original ARM-DOS project work |
| `ZORK\` | build/ZORK1-3.EXE, ZORK1-3.DAT, LICENSE.TXT, MOJOZORK.TXT | story files MIT (Microsoft's 2025 Zork release), MojoZork zlib |
| `ADVENT\` | build/ADVENT.EXE, LICENSE.TXT | Open Adventure, BSD-2-Clause |
| `ANSI\` | ANSI.ART (apps/ansi), README.TXT | original |
| `BASIC\` | the five demo .BAS programs of apps/basic, LICENSE.TXT (bwBASIC's GPL v2) | as apps/basic |
| `UTILS\` | build/ARMINFO.EXE | original |
| `PICTURES\` | four NASA photographs as GIF89a, 320x200, 256 colours | public domain (NASA) |
| `TEXTS\` | The Time Machine, The Raven, the US Constitution | public domain |
| `MUSIC\` | TRACKS.TXT (generated: titles, lengths, attribution) | |

Pictures (converted with ffmpeg: 4:3 crop or black pad, lanczos to 320x200, 256-colour palette,
Bayer dither), from `https://images-assets.nasa.gov/image/<id>/<id>~medium.jpg`, downloaded
2026-09-24: `EARTHRSE.GIF` = AS08-14-2383 (Earthrise, Apollo 8, 1968), `BLUEMARB.GIF` =
AS17-148-22727 (Blue Marble, Apollo 17, 1972), `ALDRIN.GIF` = AS11-40-5903 (Apollo 11, 1969),
`MOON.GIF` = PIA00405 (the Moon from Galileo, 7 December 1992, NASA/JPL/USGS). NASA imagery is
not copyrighted (NASA media usage guidelines). There is no GIF viewer on ARM-DOS yet; the README
says to use "your favourite GIF viewer".

Texts: public-domain works whose transcriptions came from Project Gutenberg (eBooks #35, #1065,
#5, downloaded 2026-09-24); the Project Gutenberg header, licence and footer (and the #5
transcribers' preface) were removed (the Project Gutenberg licence allows the plain public-domain text to be
redistributed that way, without their trademark), and the text converted to CP437 (curly quotes and dashes to
ASCII) with CRLF.

Authored text lives in `disc/text/` as UTF-8 and is converted to CP437 + CRLF by the build
(unknown characters are an error); `disc/files/` is copied byte for byte. Programs not built yet
are left off with a note. All dates on the disc are 1993-06-17 12:00.

### Tools

* `tools/iso9660.mjs`: ISO 9660 level 1 builder (`buildIso`) and reader (`IsoReader`) in plain
  JS: system area, PVD (both-endian fields, identifiers, 17-byte dates), terminator, L and M path
  tables (breadth-first, parent-number order), directories in ECMA-119 blank-padded name order
  with records never crossing a sector, contiguous files, 8.3 + `;1`, depth <= 8. Deterministic.
* `tests/iso9660.mjs`: a synthetic tree (150-entry multi-sector directory, 8-deep path, empty
  file/directory, exact-sector and 300 KB files, sort-order corner cases) read back with the
  reader and independently with **xorriso** (`-find` without warnings, `-extract` byte
  comparison, `-pvd_info`).
* `tests/disc.mjs`: the built disc: ISO files byte-identical to their sources, CRLF/CP437 text,
  the menu box, CREDITS naming every track, xorriso listing, manifest vs files (sectors, frames,
  LBAs), WAV format, peak/RMS (not silent, not clipped, no silent 10-s stretch), Opus/MP3
  durations within 0.1 s of the WAV (ffprobe), licence fields, originals' sha256.
