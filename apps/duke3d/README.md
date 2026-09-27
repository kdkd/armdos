# Duke Nukem 3D for ARM-DOS

The shareware **Duke Nukem 3D v1.3D** (episode 1, "L.A. Meltdown") as a **DOS
program on an ARM PC**: `C:\GAMES\DUKE3D\DUKE3D.EXE`. It is 3D Realms' GPL'd
Duke Nukem 3D source (the 2003 release, through the Chocolate Duke3D port)
on Ken Silverman's Build engine, with Jim Dose's Apogee Sound System (MultiVoc
+ the AdLib MIDI driver) as DUKE3D.EXE used them, and an ARM-DOS platform
layer in `src/armdos/`. Every instruction is ARM; the x86 assembly (A.ASM,
MV_MIX*.ASM, the Watcom pragmas) is replaced by C.

```
C:\>CD \GAMES\DUKE3D
C:\GAMES\DUKE3D>DUKE3D                    (C:\>GAMES\DUKE3D\DUKE3D works too)
C:\GAMES\DUKE3D>DUKE3D /nm /ns            no music / no sound (the game's own switches)
```

Keys are the original defaults (SETUP.EXE's): arrows move/turn, **Ctrl**
fires, **Space** opens, **A** jumps, **Z** crouches, Alt strafes, Shift runs,
Caps Lock auto-run, 1-0 weapons, Enter uses the inventory item, `[` `]`
select it, PgUp/PgDn look, Home/End aim, Tab the map, F1-F10 the menu keys,
Esc the menu. With `MOUSE` (MOUSE.COM) loaded the mouse turns and fires.
Quit: Esc -> Quit -> Y, then the two shareware ordering screens (a key
each) and the DUKESW.BIN text screen, as in 1996.

## How it does DOS things

| | how | where |
|---|---|---|
| memory | DUKE3D.EXE was a bound **DOS/4GW** program: the game (1.1 MB of code, 3 MB of data) ran in extended memory. Here too: `DUKE3D.EXE` = a 23 KB **extender stub** (`loader/d3dload.c`) + the game image appended to it. The stub allocates an XMS block (HIMEM.SYS) for image + bss + a 256 KB stack, reads and relocates the image there exactly as the DOS loader does (ARCH.md 8), and enters it with the EXE entry registers and its own PSP. The game's C runtime then takes the rest of XMS as its heap (the Build cache: the whole 9 MB of shareware art fits). Conventional memory used while playing: the stub's **27,408 bytes**. No HIMEM.SYS: "This program requires an extended memory manager (HIMEM.SYS)." The XMS handle of the image block is passed in PSP bytes 58h-5Bh and freed at exit. | `loader/d3dload.c`, `src/armdos/game_armdos.c` |
| video | INT 10h AX=0013h, **VGA mode 13h 320x200** - DUKE3D.EXE's "VGA compatible" (chained, screen buffer) mode. The engine renders into a 64,000-byte buffer and `nextpage()` copies it to 0xA0000; the palette (and the damage/pickup/underwater tints, gamma) goes to the DAC through 3C8h/3C9h. The ARM-PC VGA has no VESA BIOS and no Mode X: 320x200 is the only screen mode. | `src/armdos/display_armdos.c` |
| keyboard | own INT 09h handler: set-1 scan codes from port 60h go straight to the game's `keyhandler()` inside the interrupt, as the MACT keyboard ISR did (E0 grey keys; E1 = Pause). The BIOS never sees the keys; old vector restored and BDA shift flags cleared on exit. | `display_armdos.c` |
| timer | the Apogee Sound System's **task manager** on the PIT: channel 0 runs at the rate of the fastest task (the MIDI sequencer, one interrupt per MIDI tick, a few hundred Hz), each task advances by PIT counts: the game clock (`totalclock`, **120 Hz**, as GAME.C's `TS_ScheduleTask(timerhandler)`), the MIDI player, the music fade. The BIOS INT 08h is chained every 65,536 PIT counts (the test measures 18.17 Hz). | `src/armdos/task_man_armdos.c` |
| sound | **Sound Blaster 16** with Jim Dose's own drivers: `BLASTER.C` (the BLASTER variable, DSP 4.xx, auto-init DMA through `DMA.C` on DMA 5, IRQ 7 via INT 21h AH=25h, the CT1745 mixer for the voice/FM volumes) and `MULTIVOC.C` (Duke's VOC effects mixed into a 4 x 256-sample ring, 16-bit stereo 22,050 Hz, 8 voices, from the SB interrupt). The mixer loops are Jonathon Fowler's C versions of MV_MIX.ASM / MV_MIX16.ASM / MVREVERB.ASM. | `src/audiolib/` |
| music | Duke's MIDI music (GRABBAG.MID, STALKER.MID, ...) through `MIDI.C` + `AL_MIDI.C` on the **OPL3** with the game's own timbre bank **D3DTIMBR.TMB** from DUKE3D.GRP (`MUSIC_RegisterTimbreBank`), OPL3 stereo, FM volume on the mixer - DUKE3D.EXE's "Sound Blaster" music device. The AdLib detection's 80 us wait uses port 61h's refresh toggle. | `src/audiolib/` |
| GM music | **With an MPU-401** at BLASTER's P port (330h) CONFIG.C's defaults pick SETUP's **"General MIDI"** music device: MIDI.C's events go through Jim Dose's own **MPU401.C** (from the AudioLib release, unchanged) in UART mode to the ARM-PC's GM synthesizer. A DUKE3D.CFG `MusicDevice` still decides; if it names a MIDI device and no MPU answers (the page's MIDI switch off), SOUNDS.C falls back to FM instead of quitting. Tested against GRABBAG.MID (`tests/gm.mjs`, `make duke3d-gm-test` in apps/midi). | `src/audiolib/mpu401.c`, `music.c`, `game/config.c`, `game/sounds.c` |
| mouse | INT 33h (MOUSE.COM): AX=0 detect, AX=0Bh mickeys, AX=3 buttons. | `display_armdos.c` |
| joystick | the **game port at 201h** (ARCH.md; `emu/dev/gameport.mjs`), read the MACT library's way: write 201h, count polls until each axis bit drops (with interrupts off while counting), buttons from bits 4-7. Enabled as in 1996 by DUKE3D.CFG `ControllerType = 2` (SETUP's "Keyboard and Joystick"; 5/6 flight stick/Thrustmaster, 7 joystick and mouse). At start-up, in text mode, `CONTROL_CenterJoystick` takes GAME.C's calibration: "Center the joystick and press a button", "Move joystick to upper-left corner ...", "... lower-right corner ..." (+ throttle/rudder centres for flight sticks, `EnableRudder`). Without a stick in the port the game quietly stays on the keyboard. Defaults = what SETUP wrote (DUKE3D.CFG entries override): X `analog_turning`, Y `analog_moving`, B-X `analog_strafing`, buttons 1-4 Fire / Strafe / Run / Open, `JoystickAnalogScaleN` 65536 (= 1.0, integers read as MACT 16.16), dead zone 2048 of 32767. Changes marked `ARMDOS JOYSTICK` in `display_armdos.c`, `game/control.c`, `game/config.c`, `game/player.c` (Chocolate's "remove y axis" is kept for the mouse only), `game/joystick.h`. Test: `tests/joystick.mjs` (`make duke3d-joystick-test`). | `display_armdos.c`, `game/control.c` |
| idle | WFI in the game's key-wait loops (logo, ordering screens, bonus screens). The game itself renders as fast as it can, as it did. | `game.c` |
| files | DUKE3D.GRP and the CON files through `open/read/lseek` (binary; `read()` is looped: ARM-DOS returns at most 32 KB per call, as DOS). The game directory is the current directory, or DUKE3D.EXE's own directory when there is no DUKE3D.GRP there (restored at exit). DUKE3D.CFG is written on quit. | `src/armdos/game_armdos.c`, `src/engine/doscmpat.h` |
| setup | SETUP.EXE (x86) cannot run; the defaults are what it would have written for the ARM-PC: FX and music device Sound Blaster, BLASTER from the environment, 8 voices, 16-bit stereo, 22,050 Hz, SETUP's key layout. Everything else is set in the game's own Options menus. | `src/game/config.c` |
| start/exit | GAME.C's start-up: mode 3, the red title bars (`Duke Nukem 3D Unregistered Shareware v1.3D`, `Copyright (c) 1996 3D Realms Entertainment`), the licence notice, CON compilation, "You have run Duke Nukem 3D N times", "Checking music/sound inits." ... On quit: the two ordering screens, mode 3, **DUKESW.BIN** to B800:0000 and the cursor below it. Error(): sound, timer and keyboard restored and text mode before the message. While mode 13h is set, Chocolate Duke's progress printf()s go to the debug port E9h instead of being drawn over the picture. | `game.c`, `display_armdos.c`, `support_armdos.c` |

## Performance

`node apps/duke3d/tests/run.mjs fps` (E1L1, walking and turning for 21 s,
the engine's frame counter and 120 Hz clock read from memory; node 22, Ryzen 9
9955HX; the emulated machine runs 1 instruction per cycle):

| | result |
|---|---|
| E1L1 at the ARM-PC's **100 MHz**, 320x200, full status bar | **61 fps** (1301 frames in 21.2 s) - a Pentium-class figure (a 486DX2-66 did ~20-25) |
| emulator speed | ~450-600 host MIPS (JIT) = 4.5-6x real time |

Profile (PC sampling while turning in E1L1): vlineasm4 22%, mvlineasm4 19%
(the wall / masked-wall column drawers), hlineasm4 10% (floors), wallscan 7%,
`__udivmoddi4` 5% (the 64/32-bit divides of Build's divscale - the ARM926 has
no divide instruction), drawalls 4%, the MultiVoc mixer 3%. The column
drawers were rewritten with their steppers in registers (Chocolate's C
versions kept them in global arrays, reloaded after every byte store, plus a
per-pixel debug counter): 38 -> 61 fps. The engine, cache, mixer and driver
files are built `-O2`, the rest `-Os`.

## Building and testing

```sh
make DUKE3D                # build/DUKE3D.EXE (= build/duke3d/D3DLOAD.EXE + D3DGAME.EXE)
make images                # build/hd.img gets C:\GAMES\DUKE3D\ (apps/duke3d/hd.json)
make duke3d-test           # part of "make test"
node apps/duke3d/tests/run.mjs play|fps|fromroot|nomem [--mhz N] [--no-jit] [--out DIR]
```

`tests/run.mjs` builds its own C: (IO.SYS, ARMDOS.SYS, COMMAND.COM, CONFIG.SYS
`DEVICE=C:\DOS\HIMEM.SYS`, AUTOEXEC.BAT with `SET BLASTER=A220 I7 D1 H5 T6`,
`SBMIX /INIT /Q`, `MOUSE`), boots, types at the DOS prompt and presses real
keys through the 8042. **play**: MEM (the stub fits), DUKE3D, the start-up
text, mode 13h, INT 08h/09h/0Fh hooked, BIOS tick 18.2 Hz, the 3D Realms logo
with the title music on the OPL3 (key-ons counted, RMS measured with only the
OPL playing), the title with its explosions on the SB16, menu -> New Game ->
L.A. Meltdown -> Let's Rock -> E1L1 "Hollywood Holocaust" (the level name is
logged to port E9h), walk, turn, fire (pistol ammo 48 -> 47 read from
`ps[0]`, offset from the ELF's DWARF via `arm-none-eabi-readelf`; the shot audible), jump,
mouse turn, F12 -> DUKE0000.PCX, quit -> ordering screens -> DUKESW.BIN in text mode, vectors
restored, DUKE3D.CFG written; the whole audio goes to
`build/duke3d-test/duke3d.wav`. **fps**: see above. **fromroot**:
`C:\>GAMES\DUKE3D\DUKE3D`. **nomem**: no HIMEM.SYS. Screenshots in
`build/duke3d-test/` (logo, title, e1l1*, quit-prompt, order1/2, exit, fps-end).

## Deviations from DUKE3D.EXE 1.3D (DOS, 1996)

* **320x200 only** (no VESA modes / Mode X on the ARM-PC VGA); the Video Setup
  menu offers only 320x200.
* The game code is **v1.5** (the released source, through Chocolate Duke3D)
  running the 1.3D shareware data with its own 1.3 CON files; Chocolate's
  1.3/1.5 compatibility (FIX_00022 ...) decides the differences. Chocolate's
  **Options menus** are kept: Game Options / Setup Keyboard / Setup Mouse /
  Setup Sound / Setup Video (in 1996 keys and sound were set up with the x86
  SETUP.EXE, which cannot run here). Its console and its extra keys (hide
  weapon, auto aim, console) are unbound.
* The **attract-mode demos** (DEMO1-3.DMO) are not played: their version byte
  is 0 and Chocolate's player accepts 1.3D-era versions 27-29 only; the title
  goes to the main menu instead.
* No network/modem/serial play (single player; `network.c`/`dummy_multi.c`).
  No GUS/PAS/SoundScape/AWE32 (not on the ARM-PC; the calls are
  stubs, `src/armdos/nocards/`); the MPU-401 is real (General MIDI music).
* The group file is identified by its size (a known release size) instead of
  a CRC-32 of all 11 MB (seconds at 100 MHz); an unknown GRP is still summed.
* None for screenshots: F12 writes DUKE0000.PCX, DUKE0001.PCX ... (320x200
  PCX, 8.3 names, as in 1996; Chocolate's long .bmp names are not used).
* Ports that needed code changes, all marked `ARMDOS`: unaligned 16/32-bit
  loads from VOC and MIDI data (`src/armdos/unaligned.h`: an ARMv5 LDR
  rotates), Ken's 16-byte rounding of cache objects (Chocolate had dropped
  it), `MV_GetVoice` returning NULL (else MV_AllocVoice loops forever), the
  sample `lock` byte volatile (the SB interrupt clears it while `newgame()`
  spins on it), `char` signed for game/engine (as Chocolate on x86 GCC) but
  unsigned for the sound library (as Watcom: its pan table depends on it),
  `int32_t` = `int` (as on the x86 hosts Chocolate was written for).

## Files

* `loader/d3dload.c` - the extender stub.
* `src/armdos/` - the ARM-DOS platform: `display_armdos.c` (video, keyboard,
  mouse, timer glue, PCX capture), `task_man_armdos.c` (TASK_MAN.C's
  interface on the PIT), `game_armdos.c` (game directory, XMS block, title
  bars), `support_armdos.c` (interrupt flags, DPMI stubs, `Z_AvailHeap`,
  music loader, stdout in mode 13h, full `read()`), `unaligned.h`,
  `nocards/` (the absent sound cards).
* `src/engine/` - Build engine as in Chocolate Duke3D (`engine.c`, `draw.c` =
  A.ASM in C, `cache.c`, `filesystem.c`, `tiles.c`, ...), plus `doscmpat.h`
  (PLATFORM_DOS). `src/game/` - the game (GAME.C, ACTORS.C, PLAYER.C, ...),
  Chocolate Duke3D. `src/audiolib/` - the Apogee Sound System: `MULTIVOC.C`,
  `FX_MAN.C`, `BLASTER.C`, `DMA.C`, `MIDI.C`, `AL_MIDI.C`, `MUSIC.C`,
  `GMTIMBRE.C`, ... **from the original 3D Realms release** (AudioLib.zip),
  `mv_mix.c` from jfaudiolib.
* `3rdparty/duke3d/` - the shareware release (below; fetched by tools/fetch-3rdparty.sh, not in the repository). `hd.json` - `C:\GAMES\DUKE3D\`:
  DUKE3D.EXE (kept out of C:\DOS), BUILDLIC.TXT, DUKE3D.GRP, DUKE.RTS, the
  CON files, DEMO1-3.DMO, README.DOC, LICENSE.TXT, ULTRAMID.INI, MODEM.PCK,
  FILE_ID.DIZ and the complete original archive 3DDUKE13.ZIP.
* `tests/run.mjs`, `tests/lib.mjs`.

## Licences and provenance

**Game code:** GPL-2 (or later), `src/COPYING` (3D Realms' GNU.TXT). Duke
Nukem 3D (c) 1996-2003 3D Realms, source released 2003-04-01
(`duke3dsource.zip`, 4,017,201 bytes, SHA-1
`f77cfba84a367a314ad2ed6149c48002881ff240`, from
`https://web.archive.org/web/20160403223736/http://www.classicdosgames.com/files/source/duke3dsource.zip`,
2026-09-24). Game and engine port: Chocolate Duke3D,
`https://github.com/fabiensanglard/chocolate_duke3D` commit
`1cfa1909aa1f5a5ab159301044ca499673bc2283` (2019-03-08), GPL-2 (game).
**Sound library:** Apogee Sound System (c) 1994-1995 James R. Dose, GPL-2,
from the same archive's AudioLib.zip (SHA-1
`f6d277a2fcc37197a29b7949f1d212b6db96ee7d`); `mv_mix.c` from jfaudiolib
(`https://github.com/jonof/jfaudiolib` commit `68be97f6`), (c) 2009 Jonathon
Fowler, GPL-2. The ARM-DOS files are GPL-2 or later.

**Build engine:** (c) 1993-1997 Ken Silverman, under his **BUILD SOURCE CODE
LICENSE** (`src/BUILDLIC.TXT`, 06/20/2000, copied from jfbuild; also on the
disk next to DUKE3D.EXE, as the licence requires): derivative works may be
distributed only over the Internet, completely free of charge, with the
notice and BUILDLIC.TXT; modified files say so (all engine files do). Note
that this licence is not GPL-compatible in the strict sense;
the combination "GPL game + BUILDLIC engine" is the one every Duke3D source
port (Chocolate Duke3D, JFDuke3D, EDuke32) distributes. ARM-DOS's free web
emulator satisfies its conditions (Internet, free, notice included). Offering
DUKE3D.EXE means offering this source.

**Data:** `3rdparty/duke3d/` holds the **unmodified shareware Duke Nukem 3D v1.3D**:

| | |
|---|---|
| archive | `3DDUKE13.ZIP`, 5,924,374 bytes, SHA-1 `72b832734d72c829cecaffd8d8ae0eb38995aeb3`, MD5 `04e4ca70b8a2d59ed56c451c5c1d5d39`, SHA-256 `c67efd179022bc6d9bde54f404c707cbcbdc15423c20be72e277bc2bdddf3d0e`; contains LICENSE.TXT, INSTALL.EXE, DN3DSW13.SHR (a zip, SHA-1 `4307b7f33c8df15a2338e06d8ac26651b3cf523a`), FILE_ID.DIZ |
| downloaded from | `https://archive.org/download/3dduke13/3dduke13.zip` (3D Realms' shareware release, `ftp.3drealms.com/share/3dduke13.zip`), 2026-09-24 |
| extracted | DN3DSW13.SHR unpacked with `unzip` (Info-ZIP); the x86 programs (DUKE3D.EXE, SETUP.EXE, SETMAIN.EXE, COMMIT.EXE, DN3DHELP.EXE, INSTALL.EXE) stay inside the archive - they cannot run on an ARM PC |
| checked against | Debian game-data-packager `data/duke3d.yaml`: `3dduke13.zip`, `dn3dsw13.shr`, `duke3d.grp?demo` (11,035,779 bytes, MD5 `c03558e3a78d1c5356dc69b6134c5b55`, SHA-1 `a58bdbfaf28416528a0d9a4452f896f46774a806`), `duke.rts` and `license.txt?demo` (MD5 `583bf2a6...`, SHA-1 `efb92cf5...`) all match |

| file | bytes | SHA-1 |
|---|---|---|
| DUKE3D.GRP | 11,035,779 | a58bdbfaf28416528a0d9a4452f896f46774a806 |
| DUKE.RTS | 188,954 | 738c7f5fd0c8b57ee2e87ae7a97bf8e21a821d07 |
| GAME.CON | 99,639 | 1c38973690e88bc93b8ab9bbbe26c1c1e0a4f65c |
| DEFS.CON | 28,893 | 72a34f03047898d27a7807bec4317027624c956e |
| USER.CON | 36,960 | 8129e3f44060aea790f92f4aaebb235a7898e31d |
| DEMO1.DMO | 6,226 | 1986c363b82fc9e50d26cc98e856807cb0645b63 |
| DEMO2.DMO | 9,701 | 677aae480f445113e703ccf18dcae6748ef0ca08 |
| DEMO3.DMO | 3,759 | f1c67c23d18f43b3e995d2637511911be8872d51 |
| LICENSE.TXT | 9,108 | efb92cf5a397ec80f1752f0a75590529a5b30fc0 |
| README.DOC | 2,760 | d1f737dee42e8476d3fbbcc1008b79d8711f6a9d |
| ULTRAMID.INI | 6,871 | 54a404652aecfddf73aea0c11326f9f95fdd1e25 |
| MODEM.PCK | 4,125 | c0353741d28ded860d708f0915a27028fb47b9f3 |
| FILE_ID.DIZ | 496 | 7b2089b4c248da2740da1d8a89463918d26291ba |

**Terms** - the shareware licence, `LICENSE.TXT` (the "LICENSE.DOC" it
names; [V.7.16.98], 3D Realms Entertainment): "[3][A] INDIVIDUALS are
encouraged to share and give copies of the Game to friends, family,
coworkers ... but only without charge. [B] ONLINE SERVICES (including BBSs,
and WWW and FTP sites) that are free (except for any subscription fees or
incidental Internet access charges) ... may make the Game available for
downloading. [C] These grants are subject to the conditions that no
copyright information or trademark will be added or removed, and all of the
Game's files as released by 3D Realms will be included without modification"
(the list: COMMIT.EXE ... USER.CON - that is why the complete original
archive 3DDUKE13.ZIP ships byte-identical next to the extracted files).
"[D] *ALL* other distribution, including ... CD-ROM, catalog, and retail rack
REQUIRES WRITTEN PERMISSION". [4]: New Levels must not work with this Episode
One (none are shipped). The FormGen exclusivity (until March 1, 1997) is
long over. So ARM-DOS, a free web site, may make the unmodified shareware
episode available on its free emulated disk with LICENSE.TXT beside it - the
same reading as for Wolfenstein 3D (apps/wolf3d).
The 2003 source README's "You cannot redistribute our data" concerns the
full (registered) game's data; the registered DUKE3D.GRP must never be
included. "Duke Nukem" is a trademark of Gearbox Software (formerly Apogee /
3D Realms); this port is not affiliated with them. The data (11 MB GRP +
5.9 MB zip) is in `3rdparty/duke3d/`.
