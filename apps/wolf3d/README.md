# Wolfenstein 3D for ARM-DOS

The shareware **Wolfenstein 3D v1.4** (episode 1, "Escape from Wolfenstein")
as a **DOS program on an ARM PC**: `C:\GAMES\WOLF3D\WOLF3D.EXE`. The code is
[Wolf4SDL](https://github.com/11001011101001011/Wolf4SDL), a portable C port of
id Software's 1992 source, with its SDL layer replaced by the bare
ARM-PC hardware, driven the way id's WOLF3D.EXE drove a 1992 PC. Every
instruction is ARM.

```
C:\>CD \GAMES\WOLF3D
C:\GAMES\WOLF3D>WOLF3D                  (or C:\GAMES\WOLF3D\WOLF3D from anywhere)
```

Keys are WOLF3D.EXE's: arrows move, **Ctrl** fires, **Alt** strafes,
**Space** opens doors and pushes walls, **Right Shift** runs (the left Shift works
too, see "Deviations"), **1-4** choose weapons, **Esc** menu, **F1** help, **F2/F3**
save/load, **F4** sound, **F5** view size, **F6** controls, **F7** end game,
**F8/F9** quick save/load, **F10** quit, **Pause** pauses. Holding **M**
at start-up opens the jukebox.

Parameters as in WOLF3D.EXE (case does not matter, a leading `-` or `/` is
ignored, unknown words are ignored): `BABY EASY NORMAL HARD`, `TEDLEVEL n`
(start on map n with `NOWAIT`), `NOWAIT` (skip the intro screens), `GOOBERS`
(debug keys), `NOAL` (no AdLib detection), `NOSB` (no Sound Blaster), `NOMOUSE`,
`NOJOYS` (no joystick detection).
One ARM-DOS extra: `FPS` writes the frame rate to debug port E9h every second.

## How it does DOS things

| | how | where |
|---|---|---|
| video | INT 10h AX=0013h, VGA mode 13h (320x200x256, linear at 0xA0000). The game draws into a 64,000-byte buffer and each finished frame is `memcpy`'d to 0xA0000; the palette (fades, red/white damage and bonus flashes) goes to the DAC through ports 3C8h/3C9h, 6 bits per gun; `VL_WaitVBL` waits for the vertical retrace on port 3DAh bit 3 **sleeping in WFI between polls** (the 140/700 Hz timer wakes it; since a 1.4 ms retrace can fall between two polls, 1/70 s passing counts as a retrace too), so palette fades cost almost nothing (each fade step is one 768-byte DAC upload, no BIOS calls) and the SB/AdLib interrupts run on undisturbed. The fizzle-fade writes pixel by pixel straight into video memory, as the original did. INT 10h AX=0003h on exit. | `id_vl.c`, `id_vh.c` |
| keyboard | own INT 09h handler (INT 21h AH=35h/25h), a port of the original `INL_KeyService`: set-1 scan code from port 60h into `Keyboard[]`/`LastScan`, E0 prefixes dropped (so the grey arrows and right Ctrl/Alt are the keypad/left keys, exactly as WOLF3D.EXE saw them), E1 = Pause, ASCII from the original's tables for typing savegame names, EOI to port 20h. The BIOS never sees the keys; the old vector is restored and the BDA shift flags cleared on exit. Scan codes are the PC's own: `SDL_SCANCODE_*` in the shim are set-1 values. | `id_in.c`, `armdos/SDL.h` |
| timer | INT 08h hooked and PIT channel 0 reprogrammed exactly like `ID_SD.C`/`ID_SD_A.ASM`: **140 Hz** (the "slow" service: one PC speaker / AdLib effect step per tick) or **700 Hz** while AdLib music plays (the "fast" service: one IMF step per tick, effects every 5th). The handler chains the BIOS tick every 65,536 PIT counts (the BIOS then sends the EOI), otherwise it sends the EOI itself, so 0x46C and the DOS clock keep 18.2 Hz. Game time (70 Hz tics, `SDL_GetTicks`) is derived from the PIT counts. Divisor 0 and the old vector come back on exit. | `id_sd.c` |
| PC speaker | the PC sounds of AUDIOT.WL1: one byte per 1/140 s, 0 = silence, else PIT channel 2 divisor = byte x 60 (mode 3), speaker and gate through port 61h - the original's DOFX macro in C. | `id_sd.c` |
| AdLib | OPL2 register writes to ports 388h/389h with the original's status-read delays (`alOut`); detected with the classic timer-1 test (`SDL_DetectAdLib`). AdLib sound effects (channel 0) and **IMF music** from AUDIOT.WL1: register/value/delay triples played at 700 Hz from the timer interrupt. | `id_sd.c` |
| Sound Blaster | detected through `BLASTER` (SDK `sb.h`: DSP reset + version); the digitized sounds of VSWAP.WL1 (doors, guns, guards) play at 7042 Hz 8-bit through the DSP with 8237 DMA and the card's IRQ (`sb_play_pcm`), one at a time like the original. As WOLF3D.EXE did, the SB Pro mixer is used for stereo position (voice volume register 04h) and to boost FM (26h) to match the digitized sound; both are restored on exit. | `id_sd.c` |
| mouse | INT 33h (AX=0 reset/detect, AX=3 buttons, AX=0Bh motion counters) when a driver is loaded. | `id_in.c` |
| joystick | the game port at 201h, with the original ID_IN.C code (marked `ARMDOS joystick`): `IN_GetJoyAbs` fires the one-shots and counts polls of 201h with interrupts off until the axis bits drop (timeout 5000), `INL_StartJoy` detects both sticks at start-up assuming they are centred (`IN_SetupJoy(0, x*2, 0, y*2)`), `INL_GetJoyDelta` scales to +/-127 with the original's thresholds and 16-bit arithmetic, buttons from bits 4-7 (all four: a Gravis-style pad's buttons 3/4 are stick B's). Control -> "Joystick Enabled" runs the original **Calibrate Joystick** (upper left + button 0, lower right + button 1). `joystickenabled` and the port (Wolf4SDL's unused port slot) are saved in CONFIG.WL1; the calibration is not, as in the original. | `id_in.c`, `wl_menu.c`, `wl_main.c` |
| idle | while the game waits (for its next 70 Hz tic, a key, a fade step), `SDL_Delay`/`IN_WaitAndProcessEvents` sit in **WFI** until the next interrupt. | `id_sd.c`, `id_in.c` |
| files | the WL1 files, CONFIG.WL1 and SAVEGAM?.WL1 through newlib stdio -> INT 21h, in the current directory; if VSWAP.WL1 is not there, WOLF3D changes to the drive and directory it was loaded from (argv[0]). | `wl_main.c` |
| memory | WOLF3D.EXE: 266 KB file (248 KB image + relocations), 72 KB bss, 32 KB stack. Wolf4SDL keeps all of VSWAP.WL1 (726 KB) and VGAGRAPH in memory: the C runtime's heap continues in extended memory through XMS (HIMEM.SYS), or raw INT 15h AH=88h memory without it (`_armdos_raw_extmem`). | `armdos/sdl_armdos.c` |
| exit | back to text mode, then - as WOLF3D.EXE's `Quit` - the **ORDERSCREEN** (the shareware "Thanks for playing ... To order, call" page, an 80x25 text screen in VGAGRAPH) is copied to 0xB8000 with the cursor on row 24; an error shows the top of the ERRORSCREEN with the message in it (and on port E9h), exit code 1. | `wl_main.c` |

The CPU has no FPU: `-mfloat-abi=soft`. Wolf3D is fixed-point; the few
floats (the trig tables at start-up, projectile angles) are not hot.

## Performance

`node apps/wolf3d/tests/run.mjs fps` (ARM-PC at its nominal 100 MHz, 1
instruction per cycle; node 22, Ryzen 9 9955HX):

| | result |
|---|---|
| E1M1, turning (full-width view, `FPS`) | **70 fps** - the original's cap: the game never draws two frames in one 70 Hz tic |
| CPU busy while doing so | **45%** of emulated time, the rest in WFI (so ~150 fps uncapped) |
| emulator | ~800-875 host MIPS = ~17x real time |

Profile (`node apps/wolf3d/tests/profile.mjs`, PC sampling, after the fps
test): ScalePost (the wall column loop) ~60% of the busy time, WallRefresh
(ray casting) ~12%, the 64 KB frame copy ~6%. Two changes to Wolf4SDL's
`ScalePost` (under `#ifdef ARMDOS`, same pixels): the rows below the view are
skipped in one step instead of one at a time, and the loop keeps `vbuf` and the
pitch in registers.

## Building and testing

```sh
make WOLF3D                # build/WOLF3D.EXE  (apps/wolf3d/app.mk, SDK armdos_exe)
make images                # build/hd.img gets C:\GAMES\WOLF3D\ (apps/wolf3d/hd.json)
make wolf3d-test           # (also part of "make test") boots ARM-DOS headless and plays
node apps/wolf3d/tests/run.mjs play|speaker|fps|fades|fromroot|nodata [--mhz N] [--no-jit] [--out DIR]
make wolf3d-joy-test       # (also in "make test") tests/joystick.mjs: the joystick
```

`tests/run.mjs` builds its own C: image (IO.SYS, ARMDOS.SYS, a CONFIG.SYS whose
`SHELL=` is the kernel's test shell, C:\GAMES\WOLF3D\ as `hd.json` has it),
boots it with `emu/testkit.mjs` and presses real keys through the 8042:

* **play**: signon ("Press a key") -> PG-13 -> title (AdLib music: OPL writes and
  audio level) -> main menu (on "Read This!", as the shareware version starts
  without a config) -> New Game, episode 1, "Bring 'em on" -> E1M1: walk, open
  the door (Space; its sound on the SB), fire (Ctrl; the digitized pistol via
  DMA), knife (1), pistol (2), turn, strafe (Alt), run (Shift) -> F2, save in slot
  1 as "arm dos" -> F10, Y -> ORDERSCREEN. Checks mode 13h, INT 08h/09h hooked and
  restored, the BIOS tick at 18.2 Hz while the PIT runs at 700 Hz, the PIT back
  at 65536, speaker silent, text mode, SAVEGAM0.WL1 and CONFIG.WL1 on C: (read
  back from the disk image). Then WOLF3D a second time: Load Game lists "arm dos"
  and resumes it. The audio is captured to `play.wav`.
* **speaker**: `WOLF3D NOAL NOSB NOWAIT TEDLEVEL 0`: PC speaker effects (PIT
  channel 2 tones) when firing and opening a door; silent after exit.
* **fps**: see Performance.
* **fades** (twice: with AdLib, and NOAL = Sound Blaster only): the CPU load
  during the fade to the menu and back into the game (it sleeps: ~3% / 12-16%
  busy; before the fix, with the spinning 3DAh wait, 48% / 91% and 7-12x the
  host time), AdLib music keeps going through the fade (no pause > 300 ms in the
  OPL writes), and the 1.4 s digitized door sound plays unbroken. (Esc cuts a
  digitized sound on purpose: `ClearMemory` calls `SD_StopDigitized`, as in the
  original.)
* **fromroot**: `C:\GAMES\WOLF3D\WOLF3D.EXE` from `C:\` finds its data.
* **nodata**: without the WL1 files: "NO WOLFENSTEIN 3-D DATA FILES to be
  found!" in text mode, exit code 1.

Screenshots go to `build/wolf3d-test/` (signon, pg13, title, menu,
menu-newgame, sound-menu, episode, skill, e1m1-start/walk/door/fire/knife/move,
save-menu, save-name, saved, quit-prompt, orderscreen, menu2, load-menu,
loaded, speaker-e1m1, fps-e1m1, fps-turned, fromroot).

## Deviations from WOLF3D.EXE

* **Mode 13h instead of mode X.** WOLF3D.EXE used the unchained ("mode X")
  variant of the 320x200 VGA mode to flip between pages in video memory. Here
  the game draws into system memory and copies each finished frame to the
  linear mode 13h frame buffer. The resolution, palette and look are the same;
  there is no tearing either, only the mechanism differs.
* Wolf4SDL's renderer and game code, not the original's asm scalers: pixel
  results are Wolf4SDL's (which follows the original closely, incl. the
  original demos' behaviour, `PLAYDEMOLIKEORIGINAL`). Wolf4SDL's `ADDEDFIX`
  bug fixes are kept.
* **Digitized sounds**: one voice, like the original (the newest sound cuts off
  the previous one), not Wolf4SDL's 8-channel mixing. No Disney Sound Source
  (the ARM-PC has none); the menu item is greyed out as on a PC without one.
* **Run**: the default "run" key is WOLF3D.EXE's **right Shift** (the controls
  menu shows RShft); as a convenience the other Shift key runs too while "run"
  is bound to a Shift key.
* **Savegames and CONFIG.WL1** are Wolf4SDL's format (not interchangeable
  with the original's 16-bit files).
* Joystick: the original's "Use joystick port 2" and "Gravis GamePad" menu items are not in
  Wolf4SDL's control menu; port 2 is used when only a stick B is found at start-up, and all
  four buttons are always read. Plug the stick in (on the page: press a button on the
  gamepad) before starting WOLF3D, which detects it once, as the original did.
* The signon screen is the one Wolf4SDL embeds (signon.c, from id's release).
* `FPS` is an ARM-DOS addition.

## Files

* `src/` - Wolf4SDL, vendored from `https://github.com/11001011101001011/Wolf4SDL`
  commit `dc8b250af35fb0ace68db5eb879490b50068c20e` (2024-05-20; the project
  continues at bitbucket.org/ks-presto/wolf4sdl). Only the files the build
  needs; no MAME/DOSBox OPL emulator (the ARM-PC has a real OPL).
  * rewritten for ARM-DOS: `id_vl.c` (VGA), `id_in.c` (INT 09h, INT 33h),
    `id_sd.c` (PIT/INT 08h, PC speaker, AdLib, Sound Blaster; ported from the
    original ID_SD.C/ID_SD_A.ASM logic);
  * changed under `#ifdef ARMDOS`: `wl_main.c` (no SDL init, WOLF3D.EXE's
    parameters, the game-directory search, `Quit` with ORDERSCREEN/ERRORSCREEN),
    `wl_menu.c` (the original's scan code names, no $HOME config dir),
    `wl_draw.c` (ScalePost, FPS counter), `wl_play.c` (original key defaults),
    `id_vh.c` (frame copy, fizzle fade into video memory), `wl_def.h`
    (`#pragma pack(1)` replaced by packing only the file-format structures - ARM
    has no unaligned word access; newlib's `itoa`), `id_ca.c/h`, `id_sd.h`,
    `id_in.h`, `id_vl.h`, `wl_utils.c`, `wl_debug.c` (BMP screenshot),
    `wl_game.c`; `version.h` selects "Wolf3d Shareware v1.4" (`CARMACIZED` +
    `UPLOAD`).
  * `src/armdos/SDL.h`, `src/armdos/sdl_armdos.c` - not libSDL: the few SDL
    types and calls the game code still uses (8-bit surfaces, blit/fill,
    colours, scan code names), see the header.
* `3rdparty/wolf3d/` - the shareware release (see below; fetched, not in the repository).
* `hd.json` - disk fragment: `C:\GAMES\WOLF3D\` with WOLF3D.EXE (kept out of
  `C:\DOS`), the eight WL1 files, VENDOR.DOC, ORDER.FRM, FILE_ID.DIZ and the
  complete original archive 1WOLF14.ZIP, dated 01-01-93 1:40p as released.
* `tests/run.mjs` (headless end-to-end test), `tests/joystick.mjs` (a stick in the game
  port: detection, Calibrate Joystick, walking - the same view as walking with the Up arrow -,
  turning and firing with the stick, CONFIG.WL1), `tests/profile.mjs`.

## Licences and provenance

**Code:** GPL-2 (`src/license-gpl.txt`). id Software released the Wolfenstein
3D source in 1995 under a limited-use licence (`src/license-id.txt`, also in
github.com/id-Software/wolf3d, 2012) and later under the GNU GPL v2 with the
source of *Wolfenstein 3D Classic* for iOS (2009); Wolf4SDL is offered under
either (its README.TXT: "The original source code of Wolfenstein 3D: At your
choice: license-id.txt or license-gpl.txt"), and ARM-DOS uses it under the
**GPL-2**, as the ARM-DOS files here. Wolf4SDL (c) Moritz "Ripper" Kroll and
contributors; Wolfenstein 3D (c) 1992 id Software. The MAME OPL emulator
(non-commercial licence) is *not* included. Distributing WOLF3D.EXE means
offering this source.

**Data:** `3rdparty/wolf3d/` holds the **unmodified shareware Wolfenstein 3D v1.4**, from
Apogee/3D Realms' own distribution archive:

| | |
|---|---|
| archive | `1WOLF14.ZIP`, 856,401 bytes, SHA-1 `a4553d7ec4216061b9486c40be90b066862ea8e0`, MD5 `a29432cd4a5184d552d8e5da8f80a531`, SHA-256 `cb2a2ef7ecef14152c65ff93cc3b84fbd3e8eb0c5c1de41a6fc8cdef559451a8`; contains INSTALL.EXE, W3DSW14.SHR, FILE_ID.DIZ |
| downloaded from | `ftp.3drealms.com/share/1wolf14.zip` (3D Realms = Apogee's shareware site) via the Internet Archive: `http://web.archive.org/web/2010id_/http://ftp.3drealms.com/share/1wolf14.zip`, 2026-09-24 |
| extracted | W3DSW14.SHR (PKWARE DCL "implode") unpacked by tools/fetch-3rdparty.mjs; the results are the same files Debian's game-data-packager gets with `id-shr-extract` (package `dynamite`) |
| checked against | Debian game-data-packager `data/wolf3d.yaml` (salsa.debian.org/games-team/game-data-packager): the archive's size and MD5 (`1wolf14.zip?3drealms`) and every file below match. The same eight WL1 files are also in archive.org's `wolf3dsw.zip` (identical hashes). |

| file | bytes | MD5 | SHA-256 |
|---|---|---|---|
| AUDIOHED.WL1 | 1156 | 58aa1b9892d5adfa725fab343d9446f8 | 39351624ae6f8eef4b873e060c1a6f3e5ee7e81c4939c485275a89b145336338 |
| AUDIOT.WL1 | 132613 | 4b6109e957b584e4ad7f376961f3887e | 1e2c9ae30398a14c61a4ddd39aabaa0dbcc984cc4924a3e51df75574c259cfb9 |
| GAMEMAPS.WL1 | 27425 | 30fecd7cce6bc70402651ec922d2da3d | a6a6654b342f2c027bcb22bfce0a41f9fc0063b775e9e4da0c771970e53e11aa |
| MAPHEAD.WL1 | 402 | 7b6dd4e55c33c33a41d1600be5df3228 | 3458f661c9b875bca99ea22a7267771fad8f2a33c699ea732cf7c3322909bf8c |
| VGADICT.WL1 | 1024 | 76a6128f3c0dd9b77939ce8313992746 | 59878fec65f033b00dbb1240317d1793f5858213dc8013b4d68f9ac8b45b0c80 |
| VGAGRAPH.WL1 | 326568 | 74decb641b1a4faed173e10ab744bff0 | d5176f843c53415132db199c19f38591eaf3cedd35a4f7eb2698c1865d83030d |
| VGAHEAD.WL1 | 471 | 61bf1616e78367853c91f2c04e2c1cb7 | f4cc800dc8444373092d4eaa5d6ab59d63d23a510a9d38b73ca9b3dbb700d18b |
| VSWAP.WL1 | 742912 | 6efa079414b817c97db779cecfb081c9 | 698f217257e2cbb951a4d110ba09140291f38d0121b3784d1d6be59c03a6b47b |
| VENDOR.DOC | 7641 | eccc7fc421f3d1f00e6eabd6848637f6 | fc1f4c23122702198eaa0df09a12af135801bdc835317638f2b895e948a33710 |
| ORDER.FRM | 5714 | 063d3bfda9c014b6395c1aa952ad2f8b | e0596c5390896a27001f6cf263bc7a6b5f087a7499c5df18f1530de6c959bcd4 |
| FILE_ID.DIZ | 350 | 01b4e263c6a41a9159aa4332e1cb8592 | 840242ab486718872c865f4feed86b7bd874bf0d7e339977956e347dac97dfca |

**Terms** - the shareware licence itself, `VENDOR.DOC` ([V.07.15.94], Apogee
Software, in the archive): "Everyone can -- and is encouraged! -- to copy,
upload and generally pass around this Program without charging for it", on the
conditions that all of the program's files are included **without
modification** (the list includes WOLF3D.EXE and W3DHELP.EXE; that is why the
complete original archive 1WOLF14.ZIP is shipped next to the extracted data,
byte-identical), no copyright notice is removed, and no changed versions,
add-on levels or game-altering tools are distributed; distribution by BBS /
on-line service needs no written permission; only the shareware episode 1 may
be distributed, never the registered episodes (2-6, Spear of Destiny).

**Distribution:** VENDOR.DOC section [3][C] asks for Apogee's prior written permission for
distribution "on CD-ROM, in a retail location, or as part of a hardware or software bundle".
That clause was aimed at commercial CD-ROM compilations; a free web-emulated PC whose hard disk
carries the unmodified shareware release (with its VENDOR.DOC) is the free "pass around /
on-line service" copy the licence encourages - the same archive has been offered for free by
Apogee/3D Realms itself and is widely mirrored (archive.org, PCjs, DOS game archives). Nothing
is charged and the files are unmodified. "Wolfenstein 3D" is a trademark of id Software /
ZeniMax; "Apogee" of Apogee Software; this port is not affiliated with either. The data is not
in the repository: tools/fetch-3rdparty.sh downloads 1WOLF14.ZIP and unpacks it into
`3rdparty/wolf3d/` (3rdparty/manifest.json); the data is preinstalled on C:.
The original 1WOLF14.ZIP ships byte-identical alongside the extracted files.
