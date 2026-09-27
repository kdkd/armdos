# Commander Keen in Keen Dreams for ARM-DOS - KEEN.EXE, GETKEEN.BAT

`C:\GAMES\KEEN\` holds the **Keen Dreams engine without its data**: `KEEN.EXE`,
`GETKEEN.BAT`, `GETKEEN.SCR` and `README.TXT`. The game itself (graphics, levels,
sounds) is shareware and is not on the visitor's disks. **`GETKEEN`** fetches it the
way a 1992 PC owner would have: TERM dials **The ARM Pit BBS** (555-1989, apps/bbs),
logs on as a guest and downloads the unmodified shareware **KEENDRMS.ZIP** by ZMODEM;
UNZIP (apps/unzip) unpacks it into `C:\GAMES\KEEN`; the game starts. The visitor
watches the whole session on screen, ANSI logo and transfer window included.

```
C:\>CD \GAMES\KEEN
C:\GAMES\KEEN>GETKEEN          (first time: call the BBS, unzip, play; later: just play)
C:\GAMES\KEEN>KEEN             (once the data is there)
```

Keys are KDREAMS.EXE's: arrows move, **Ctrl** jumps, **Alt** throws flower power,
**Space** status window, **F1** help, **Esc** control panel (Enter on EXIT quits),
Pause pauses. The shareware texts mention START.EXE (the Gamer's Edge shell): an
Intel program; on the ARM PC type KEEN. `KEEN /?` prints a short help.

## The GETKEEN story

1. `GETKEEN.BAT` checks for the six data files. If they are missing it explains:
   "Keen Dreams is shareware. This will call The ARM Pit BBS and download it ..."
   (and where the licence terms are), then `PAUSE`.
2. `C:\TERM\TERM.EXE /S:GETKEEN.SCR` - TERM's script language (apps/term, Procomm
   ASPECT / Telix SALT flavoured): *"Connecting at 14400 to save you time..."*,
   `SET BAUDRATE 19200`, `SET INIT "AT&C1&D2&B14400"`, the dialing box, CONNECT 14400,
   the BBS's ANSI welcome, `GUEST` / password `GUEST`, no bulletins, Files, area 2
   (Games), `D` `KEENDRMS.ZIP` `Z`, `DOWNLOAD ZMODEM` (the transfer window: bytes, CPS,
   time left), log off, NO CARRIER, `EXIT 0`. Errorlevels: 2 no connection, 3 logon or
   download failed, 1 stopped with Esc.
3. `UNZIP -t` (if a KEENDRMS.ZIP from an interrupted call is incomplete, GETKEEN calls
   once more and TERM's ZMODEM crash recovery continues the file - not covered by the
   test), then `UNZIP -o KEENDRMS.ZIP` - "Inflating:
   KDREAMS.EXE" ... The zip stays in `C:\GAMES\KEEN` with its LICENSE.DOC and VENDOR.DOC.
4. `KEEN`.

**Emulated time** (tests/run.mjs, real modem pacing, the BBS on a second ARM PC):

| line rate | GETKEEN, key press to unpacked files | of which the ZMODEM transfer |
|---|---|---|
| 14400 (what GETKEEN.SCR asks for) | **283 s = 4.7 min** | 252 s (~1,380 CPS) |
| 2400 (the modem's factory setting) | **1,596 s = 26.6 min** | 1,537 s (~235 CPS) |

At 2400 bps the 358,438-byte zip is authentic but too slow for a web visitor, so the
script asks the modem card for 14400 (`AT&B14400`; the BBS's modem is set up the same
way, both ends train at the slower of the two) and says so on screen. The serial ports
run at 19200 on both machines so the DTE rate never limits the line. UNZIP takes ~2 s.

## Licences and provenance

### The code (on the visitor's disk): GPL

* Keen Dreams source: **GNU GPL v2 or later**, (c) 2014 Javier M. Chavez, released after a
  crowdfunding campaign (github.com/keendreams/keen). Its README: "The release of the
  source code does not affect the licensing of the game data files, which you must still
  legally acquire."
* KEEN.EXE is built from **ReflectionHLE** (formerly Reflection Keen, NY00123,
  github.com/NY00123/refkeen, commit `320d6fb0b780648b5fea31b04f796966ed8969b2`,
  2026-07-17), whose `src/kdreams` is that source ported to portable C - the assembly
  (ID_VW_AE, ID_RF_A, ID_US_A ...) rewritten in C, the EGA memory accesses behind
  `BE_ST_EGA*` calls - and which reproduces the *shareware v1.13* behaviour (GPL-2+).
  ReflectionHLE's backend is 3-clause BSD (be_cross.c, be_cross_mem.c,
  be_cross_doszeroseg.c, be_filesystem_file.c, be_st_egavga_lookup_tables.h, and the
  EGA routines in src/armdos/be_egaemu.c); its unlzexe and CRC-32 are public domain.
  Licence texts: `src/LICENSES/` (gpl-2.0.txt, bsd-3-clause-template.txt), notices at
  the top of each file, ReflectionHLE's README and AUTHORS in `src/REFKEEN-*.md`.
  Distributing KEEN.EXE means offering this source (apps/keen/src).
* Why ReflectionHLE and not the keendreams/keen repository directly: the GPL repository is
  the 1.93 (registered, 1993) source. The only freely distributable data is the
  **shareware v1.13**, whose tables (MAPHEAD/MAPDICT, GAMETEXT, the Gamer's Edge menus,
  the LOADSCN exit screen) differ; checked: of the repository's `static/` tables only
  EGAHEAD/EGADICT, AUDIOHHD/AUDIODCT, CONTEXT and STORY match what is inside the v1.13
  KDREAMS.EXE, MAPHEAD and MAPDICT do not. ReflectionHLE supports 1.13 exactly and, like
  KEEN.EXE, takes those tables from the user's own KDREAMS.EXE (unpacked LZEXE 0.91 image,
  each table CRC-checked) - so **no Softdisk data is compiled into KEEN.EXE**.

### The data (on The ARM Pit BBS only): Softdisk's shareware terms

`KEENDRMS.ZIP`, the shareware **Keen Dreams v1.13** of 1992-09-10:

| | |
|---|---|
| file | `3rdparty/keen/KEENDRMS.ZIP`, 358,438 bytes, 24 files (PKZIP 2.0, deflate/stored) |
| SHA-1 | `9b1bed87bc1e0b091f90703e9463377725e8d61e` |
| MD5 / SHA-256 | `930f743207cf9250c24346ae91cf9e7b` / `4f013bfd94f893a7303900d33a4d4d8c7084026c634e9a126bde393fc458931b` |
| from | the DOS Games Archive bundle `https://image.dosgamesarchive.com/games/kdreams.zip` (363,022 bytes), which holds `keendrms.zip` unchanged next to its own info card ("Distribution model: Shareware", "License file(s): LICENSE.DOC") |
| inside | KDREAMS.EXE `6841cb967090e4c84b8cfcab72ff0b4ac5c8a446` (81,619, LZEXE 0.91), KDREAMS.EGA `904bc033455a1eaaa7c3e5fb53cc901359574b2b`, KDREAMS.MAP `632575a0dfadd06102295c4b995914617b88e9df`, KDREAMS.AUD `c8001b57de02f90c3bec17ab397c86c4a6843b0e`, KDREAMS.CMP `ef455ba1681bef9b462dd90f1dfd31e6ff895ea1`, LAST.SHL `d6ecfcc479fa8696fbc2268e1b7ee618292a4847`, LICENSE.DOC `96d19749eb1b1e130c5815af8f62662daca28033`, VENDOR.DOC `e77dc2e928f5e754b7d6702b0e0638ea8ecbdd9d`, START.EXE, LOADSCN.EXE, the *.SHL shell screens, FILE_ID.DIZ ... - the exact file set (sizes, CRC-32s) ReflectionHLE lists for "Keen Dreams EGA v1.13" |

Its `LICENSE.DOC` ("Unregistered Shareware Version", August 1992) and `VENDOR.DOC`
(September 1992): **"BBS SysOps: This program may be freely distributed over BBS systems as
long as there are no files added to or removed from the original compressed file
package. Any and all file modifications are strictly prohibited."** and "It is NOT
necessary for BBS SysOps to register before distributing this software." Vendors,
collections and CD-ROMs need Softdisk's prior permission, and "Softdisk Publishing
prohibits the distribution of outdated versions".

**Decision:** host the **unmodified** zip on The ARM Pit BBS -
exactly the distribution the licence grants to BBS SysOps - and let the visitor's own
machine download it; ARM-DOS's disk images carry no Keen Dreams data. So:

* the zip is byte-identical to the release (checked by the test on both machines), in the
  BBS's Games area with our own FILE_ID.DIZ-style description in FILES.BBS (apps/bbs);
* nothing is added to or removed from it; it is not unpacked, repackaged or "installed"
  by us - the visitor's UNZIP does that on the visitor's machine, and GETKEEN keeps the
  zip there, as the licence asks users to pass it on whole;
* KEEN.EXE contains no game data; it reads the user's files and refuses modified ones.

For the record: the rights passed from
Softdisk to Flat Rock Software and in 2014 to Javier M. Chavez, who sells Keen Dreams
commercially (Steam 2015, Nintendo Switch 2019/2020); no statement from him makes the
data freely redistributable, and a 1992 shareware release is "outdated" in the sense of
VENDOR.DOC's twelve-month clause. The BBS SysOp permission above is the grant this
distribution relies on.

## How KEEN.EXE does DOS things (src/armdos/)

| | how |
|---|---|
| video | Keen Dreams draws into **EGA mode 0Dh**: four bit planes, write modes, latches for tile copies, hardware scrolling through the CRTC start address, pel panning and a line width wider than the screen. The ARM PC's VGA has no planar modes, so the EGA memory is emulated (be_egaemu.c: ReflectionHLE's routines; one 64-bit word per EGA address = 8 pixels of 4 bits, 512 KB) and the displayed part - from the start address, with the pel panning and line width - is copied into **VGA mode 13h** (320x200x256), whose DAC entries 0-15 hold the 16 EGA colours (RGBI, brown included; palette changes and fades set the DAC). A frame is copied only when something changed and the game waits (VBL, a tic, a key). |
| text | the "Did you know?" loading screen, error messages and the exit screens are written to 0xB8000 in mode 03h, with the BIOS cursor; on exit the cursor is left below them. |
| timer | INT 08h hooked; PIT channel 0 reprogrammed to the sound manager's rate (140 Hz: TickBase 70 x 2); game time (the original's TimeCount) counts these interrupts, as ReflectionHLE's `BE_ST_TimerInt*` define it; the BIOS tick is chained every 65,536 PIT counts (DOS clock keeps 18.2 Hz); divisor and vector restored on exit. |
| keyboard | INT 09h hooked; the set-1 byte from port 60h goes to the game's `INL_KeyService` (E0/E1 prefixes and all), EOI; BIOS shift flags cleared on exit. |
| sound | AdLib sound effects: the OPL at 388h/389h (the ARM PC's OPL3), with the original's status-read delays; PC speaker sounds: PIT channel 2 + port 61h. KDREAMS.CFG as shipped selects AdLib. |
| mouse | INT 33h when a driver answers (the original's check). |
| joystick | the game port at 201h, read as KDREAMS.EXE's ID_IN.C did (`BE_ST_GetEmuJoyAxes` / `BE_ST_GetEmuJoyButtons` in be_armdos.c, marked "ARM-DOS joystick"): with interrupts off, read 201h and write it back to fire the one-shots, then count the stick's X and Y bits per poll until both drop or 5000 polls (MaxJoyValue) pass, shifted as the original's SHR; buttons = bits 4-5 (6-7 for joystick 2) inverted. A 201h read is a 1 us ISA cycle, so the counts are the 386-era ones (about 22 upper-left, 560 centre, 1020 lower-right) whatever the clock. Everything else is the game's own: IN_Startup's detection (the stick must be plugged in before KEEN starts), the control panel's "Use / Configure Joystick 1/2" and "Configure Joystick" (upper-left + button, lower-right + button), the joystick moving the panel's cursor. |
| idle | WFI whenever the game waits: ~93% of emulated time asleep while playing. |
| files | KDREAMS.* in the current directory, or KEEN.EXE's own; KDREAMS.CFG and SAVEGAM?.KDR written there. Missing data: "the engine is here, but not the game data ... run GETKEEN", exit 1. A file that is not v1.13's (size or CRC-32): refused, exit 1. |
| memory | KEEN.EXE 203 KB (188 KB image, 121 KB bss); ReflectionHLE's emulated 16-bit memory (683 KB: the unpacked KDREAMS.EXE image + the game's near/far heaps) and the EGA memory (512 KB) come from the C heap, which continues in XMS (HIMEM.SYS) or raw extended memory. |
| exit | as v1.13: `Quit` runs LOADSCN's code (ReflectionHLE's loadscn2.c) on `LAST.SHL` -> the "Thanks for playing KEEN DREAMS" ordering screen, then the DOS prompt. |

Changes to the vendored ReflectionHLE files (all marked `REFKEEN_PLATFORM_ARMDOS`):
`refkeen_config.h` (new: one game, one version), `be_gamever.h` (`refkeen_current_gamever`
is the constant `BE_GAMEVER_KDREAMSE113`, so other versions' code folds away),
`kdreams/id_heads.h` + `id_ca.c` (`GRMODE`, `current_gamever_int` constants again),
`be_cross.h`/`be_cross_mem*.c` (the emulated memory is allocated, not 683 KB of .bss),
`be_st.h`/`refkeen.h` (no launcher settings, no EMS/XMS emulation), `kdreams/id_in.c`
(mouse only if a driver answers). `kdreams/altcontroller.c` (game pad / touch mappings) is
replaced by empty mappings (src/armdos/altcontrol.c). New: src/armdos/be_armdos.c (start-up
and the whole BE_ST_* machine interface), src/armdos/be_egaemu.c.

## Deviations

* EGA emulated in memory and shown in mode 13h (the ARM PC has no planar VGA modes);
  pixels, palette, scrolling are the original's; no split screen (Keen Dreams uses none).
* No START.EXE / Gamer's Edge shell (x86 programs): KEEN starts the game directly, as
  ReflectionHLE does (its `/DETOUR` inversion); the texts that mention START are the data's.
* Mode 13h has no overscan border colour.
* Mouse only through INT 33h.
* KEEN.EXE is not KDREAMS.EXE: the original stays in the directory (it is part of the
  zip, and its tables are read from it).

## Files

* `src/kdreams/`, `src/*.c|h`, `src/unlzexe`, `src/crc32` - ReflectionHLE (see above).
* `src/armdos/be_armdos.c`, `be_egaemu.c`, `altcontrol.c` - the ARM-DOS backend.
* `data/GETKEEN.BAT`, `data/GETKEEN.SCR`, `data/README.TXT` - to `C:\GAMES\KEEN\` (hd.json).
* `3rdparty/keen/KEENDRMS.ZIP` - the unmodified shareware release (fetched by
  tools/fetch-3rdparty.sh, not in the repository); goes on the BBS disk only
  (apps/bbs/bbs.json, `BBS\FILES\GAMES\`).
* `tests/run.mjs` - the end-to-end test; `tests/joystick.mjs` - the joystick test;
  `tests/jit-repro.mjs` - a small repro for a past emulator JIT regression (a program
  loaded where TERM.EXE ran executed stale compiled code); `nojit` runs it on the interpreter.

## Tests

`make keen-test` (tests/run.mjs): two ARM PCs on one PhoneExchange with the real modems
and their real pacing: the visitor's C: (C:\GAMES\KEEN from hd.json, TERM from
apps/term/hd.json, UNZIP, HIMEM, SBMIX) and build/bbs.img. It checks: KEEN without data
-> the message; GETKEEN's explanation; TERM running the script ("Connecting at 14400",
dialing box, `CONNECT 14400`, the BBS's ANSI logon, guest logon, ZMODEM window); the
download and logoff; UNZIP's listing; KEENDRMS.ZIP byte-identical on the visitor's disk
and all 24 files unpacked unmodified (compared with the host's inflate); the BBS back to
"Waiting for call"; KEEN: loading screen, mode 13h, title, control panel, world map, a
level, jumping; AdLib register writes while playing; WFI idle share; Esc + Enter -> the
LAST.SHL ending screen and the prompt; text mode, BIOS clock at 18.2 Hz, keyboard back
to DOS; GETKEEN again -> straight to the game. `node apps/keen/tests/run.mjs 2400`
does the same call at 2400 bps (measures the 26.6 minutes). Screenshots:
`build/keen-test/` (keen-nodata, getkeen-intro, getkeen-dialing, getkeen-bbs-logon,
getkeen-zmodem, getkeen-unzip, keen-loading, keen-title, keen-controlpanel,
keen-worldmap, keen-level, keen-jump, keen-quit-menu, keen-ending).

`make keen-joy-test` (tests/joystick.mjs): a private C: with the shareware files unpacked on the
host; a stick plugged into the game port (`machine.joy`) before KEEN starts. Checks: IN_Startup
found joystick 1 and not 2; the control panel (C, Joystick 1, Configure Joystick) calibrated with
the stick upper-left + button 1, lower-right + button 1: JoyDefs[0] min ~22, max ~1022, the
centre inside the dead zone; a new game with Controls[0] = ctrl_Joystick1; on the world map Keen
stands still with the stick centred and walks left/up when it is pushed; button 1 enters the
level; the stick runs Keen right, button 1 jumps and he lands; Esc + Enter quits. Keen's
position is read from the game's `player` object (address from build/obj/KEEN/KEEN.elf's
symbols plus the load base found in the MCB chain). Screenshots: `build/keen-joy-test/`.

Also checked by hand (screenshots looked at): the title screen from KDREAMS.CMP, the high
score table, horizontal scrolling while walking, saving a game from the control panel
(slot shows "LOAD" afterwards), the "One moment / Did you know?" text screen.

`node apps/keen/tests/run.mjs nojit` runs the same story with the visitor's machine on the
interpreter (the emulator's JIT off).
