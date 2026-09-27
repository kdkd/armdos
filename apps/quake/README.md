# Quake for ARM-DOS

The shareware Quake (episode 1, "Dimension of the Doomed") as a **DOS program
on an ARM PC**: `C:\GAMES\QUAKE\QUAKE.EXE`. It is id Software's WinQuake
software renderer (the code QUAKE.EXE 1.08/1.09 was built from) with an
ARM-DOS platform layer written after id's own DOS drivers (`sys_dos.c`,
`vid_dos.c`/`vid_vga.c`, `in_dos.c`, `snd_dos.c`) - every instruction is ARM,
the x86 assembly is not used (`id386` is 0: the C span/edge/polygon code).

```
C:\>CD \GAMES\QUAKE
C:\GAMES\QUAKE>QUAKE                       (C:\>GAMES\QUAKE\QUAKE works too)
C:\GAMES\QUAKE>QUAKE +timedemo demo1       benchmark: frames, seconds, fps
C:\GAMES\QUAKE>QUAKE -mem 8 -nosound -nomouse -sspeed 22050
```

Keys are Quake's defaults: arrows move/turn, **Ctrl** fires, **Space** jumps,
Alt strafes, Shift runs, 1-8 weapons, **~** (the key left of 1) opens the
console, Esc the menu. With `MOUSE` (MOUSE.COM) loaded the mouse turns
(`+mlook` in the console for mouse look). Quit: Esc -> Quit -> Y, then the
END1.BIN "sell screen" in text mode, as in 1996.

## How it does DOS things

| | how | where |
|---|---|---|
| video | INT 10h AX=0013h -> VGA **mode 13h, 320x200** (QUAKE.EXE's mode 0; the ARM-PC has no VESA BIOS, so the only mode). Quake renders into a buffer in extended memory; `VID_Update` copies the dirty rectangles to the linear frame buffer at 0xA0000. Palette (and the damage/pickup/underwater shifts) to the DAC through ports 3C8h/3C9h. The loading "disc" is drawn straight into video memory and put back (`D_Begin/EndDirectRect`). | `vid_armdos.c` |
| keyboard | own INT 09h handler (INT 21h AH=35h/25h): set-1 scan codes from port 60h into a ring buffer, EOI to 20h; `Sys_SendKeyEvents` decodes them with id's `scantokey` table, E0 (grey keys, the fake shifts) and E1 (Pause). The BIOS never sees the keys; the old vector is restored and the BDA shift flags cleared on exit. | `sys_armdos.c` |
| timer | PIT channel 0 in **mode 2 at ~1 kHz** (divisor 1193) with an INT 08h handler; `Sys_FloatTime` = ticks x divisor + the latched count (sys_dos.c latched the counter of the 18.2 Hz tick the same way). The BIOS handler is chained every 65536 PIT counts, so 0x46C and the DOS clock stay right (the test measures 18.3 Hz). Mode 3 / divisor 0 and the old vector come back on exit. | `sys_armdos.c` |
| sound | **Sound Blaster 16** via the SDK's `sb.h` (BLASTER=A220 **I7** D1 H5 T6): 11025 Hz (`-sspeed N`), 16-bit stereo on DSP 4.xx, 8-bit stereo on an SB Pro, 8-bit mono otherwise - QUAKE.EXE's choices. id's `snd_dos.c` let Quake mix straight into the auto-init DMA buffer and read the play position from the 8237; here Quake mixes into an 8 KB ring and the SB IRQ handler copies the next 512-byte block into the DMA half that just finished and advances the position `SNDDMA_GetDMAPos` reports (~12 ms granularity). No card or `-nosound`: silent. No CD audio (cd_null). | `snd_armdos.c` |
| mouse | INT 33h (MOUSE.COM): AX=0 reset/detect, AX=3 buttons (-> K_MOUSE1-3), AX=0Bh motion counters - `in_dos.c`'s code. No `-control` external driver. | `in_armdos.c` |
| joystick | The game port at 201h (emu/dev/gameport.mjs), `in_dos.c`'s code unchanged in substance: at start-up `IN_ReadJoystick` fires the one-shots and counts polls of 201h until X and Y drop (10000 polls = "joystick not found"; each poll is a 1 us ISA cycle, so the counts are ~25-1100 as on a PC); then "joystick found" and the CENTER / UPPER LEFT / LOWER RIGHT prompts, each answered with button 1 (Esc skips; `-nojoy` skips it all). Cvars `joystick` (0 = off) and `joybuttons` (4); buttons 1-4 are K_JOY1-4 for `bind`; the stick turns/walks (`+strafe` strafes, `+mlook` looks). Deviation: IN_Init runs before the video mode is set, so the prompts also go to the DOS text screen (`JoyPrintf`), where the start-up messages are. id's `joyyl = (centerx + joysticky)/2` slip is kept. | `in_armdos.c` (block marked "ARM-DOS joystick") |
| memory | QUAKE.EXE ran under CWSDPMI with everything in extended memory. Here the program image (368 KB), its bss (133 KB) and a 16 KB start-up stack are in conventional memory (**529,688 bytes needed**; with HIMEM.SYS and MOUSE.COM loaded 573,760 are free). The hunk (default: as much as there is, up to 16 MB, like `dos_getmaxlockedmem`; `-mem N` MB), the video/z/surface-cache buffers, a **1 MB C stack** (Quake keeps ~200 KB of edges, surfaces and the warp buffer on it; `main` switches `sp` to it) and the ten biggest static arrays (350 KB: `cl_entities`, `mod_known`, ...; `sys_armdos.h` turns each `T x[N]` into `T (*x_p)[N]` + `#define x (*x_p)`) come from the C runtime's heap, which is in **XMS** (HIMEM.SYS); without HIMEM.SYS, raw extended memory (`_armdos_raw_extmem`). `-mem` too large: "Not enough memory for -mem N". | `sys_armdos.c`, `sys_armdos.h` |
| FPU | built for the ARM926's **VFP9-S** (`$(ARMDOS_VFP)`: `-mfpu=vfp -mfloat-abi=softfp`, newlib's `arm/v5te/softfp` libm). As QUAKE.EXE checked for an x87, QUAKE.EXE checks FPEXC.EN and says "Error: Quake requires a floating-point processor" without one. | `sys_armdos.c`, `app.mk` |
| idle | while Quake is ahead of its 72 fps frame clock the main loop sits in **WFI** until the next timer tick. (At 100 MHz Quake rarely gets there: it renders as fast as it can, as it did on a Pentium.) | `sys_armdos.c` |
| files | pak/cfg/savegames through `open/read/lseek` (binary) -> INT 21h; reads and writes are looped (DOS moves < 64 KB per call). The game directory is the current directory, as QUAKE.EXE had it, or - if there is no `ID1` there - QUAKE.EXE's own directory. `ID1\CONFIG.CFG` is written on quit. | `sys_armdos.c` |
| network | loopback only (the single-player game talks to its own server); no IPX/serial/TCP drivers. | `net_armdos.c` |
| exit | `Sys_Quit` loads END1.BIN, writes the version into it, shuts down, INT 10h back to mode 3, END1.BIN to B800:0000, cursor to row 22 (QUAKE.EXE's exact sequence). `Sys_Error` restores text mode first and prints `Error: ...` (also to debug port E9h, with all console output). | `sys_armdos.c`, `vid_armdos.c` |

## Performance

`node apps/quake/tests/run.mjs timedemo` (`QUAKE -nosound +timedemo demo1`,
node 22, Ryzen 9 9955HX; the emulated machine runs 1 instruction per cycle at
its nominal clock):

| | result |
|---|---|
| `timedemo demo1` at the ARM-PC's **100 MHz** | **29.7 fps** (969 frames in 32.6 s; 3.3 G instructions) - about a Pentium 90's figure for 320x200 |
| emulator speed during the timedemo | **~420 host MIPS** (JIT, VFP fast paths) = ~4.2x real time at 100 MHz |
| playing E1M1 | CPU 100% busy (below the 72 fps cap) |

Profile (`node apps/quake/tests/profile.mjs`, PC sampling during the
timedemo): D_DrawSpans8 27%, R_DrawSurfaceBlock8_mip0/1 18%, D_DrawZSpans 7%,
D_PolysetDrawSpans8 5%, 32-bit division (`__divsi3`: the span steppers'
partial segments; the ARM926 has no divide instruction) 4%, R_LeadingEdge 3%,
... - the classic software Quake profile: integer span filling and surface
caching dominate, the float setup (edges, clipping, transforms) is a few
percent thanks to the VFP. The renderer, mixer, maths and physics files are
built `-O2`, the rest `-Os`.

## Building and testing

```sh
make QUAKE                 # build/QUAKE.EXE (apps/quake/app.mk)
make images                # build/hd.img gets C:\GAMES\QUAKE\ (apps/quake/hd.json)
make quake-test            # part of "make test"
node apps/quake/tests/run.mjs play|timedemo|fromroot|nomem [--mhz N] [--no-jit] [--out DIR]
```

`tests/run.mjs` builds its own C: (IO.SYS, ARMDOS.SYS, COMMAND.COM,
CONFIG.SYS `DEVICE=C:\DOS\HIMEM.SYS`, AUTOEXEC.BAT with `SET BLASTER=...` and
`MOUSE`), boots it, types at the real DOS prompt and presses real keys
through the 8042. `play`: MEM (QUAKE.EXE fits in the free conventional
memory), QUAKE, mode 13h, INT 08h/09h hooked, BIOS tick at 18.2 Hz, the demo
loop on screen with SB16 sound, main menu, Single Player -> New Game (the
start map), console `map e1m1` ("the Slipgate Complex"), walk, turn, fire
(ammo 25 -> 24, the shot audible), jump, mouse turn, console `version`,
Quit -> Y -> END1.BIN in text mode, vectors restored, `ID1\CONFIG.CFG`
written. `timedemo`: fps. `fromroot`: `C:\>GAMES\QUAKE\QUAKE`. `nomem`:
`QUAKE -mem 20` fails cleanly. Screenshots in `build/quake-test/`
(`demo1.png`, `menu.png`, `menu-sp.png`, `start.png`, `e1m1.png`,
`e1m1-walk.png`, `e1m1-fire.png`, `e1m1-mouse.png`, `console.png`,
`quit-prompt.png`, `end1.png`, `timedemo-end.png`).

`tests/joystick.mjs` (`make quake-joystick-test`, part of `make test`): no stick ->
"joystick not found"; `-nojoy` -> no probe; a stick plugged in (`m.joy`) -> "joystick
found", the three prompts (on the text screen) answered with the stick and button 1,
`+map e1m1`: centred the player stays put, stick forward walks him (console `edict 1`
origin), stick left turns him, button 2 fires a `bind joy2` (K_JOY2). Screenshots in
`build/quake-test/joystick/`.

## Deviations from QUAKE.EXE (DOS, 1996)

* 320x200 only (no VESA modes: the ARM-PC's VGA has none; the Video Options
  menu says so). No 360x240 Mode X modes either.
* The banner says `Quake v1.09` (the GPL'd WinQuake code) rather than 1.06;
  the data is the 1.06 shareware pak.
* No network play (IPX/serial/modem), no CD audio; the sound
  position granularity is one SB block (512 bytes) instead of the 8237 count.
* The memory messages ("Locked N Mb data") are one line, "Allocated N.N Mb data".
* The big static arrays live in extended memory (see above) - invisible to
  the player; the stack is in extended memory too (as under CWSDPMI).

## Files

* `src/sys_armdos.c`, `vid_armdos.c`, `in_armdos.c`, `snd_armdos.c`,
  `net_armdos.c`, `sys_armdos.h` - the ARM-DOS platform layer.
* `src/*` - the rest is WinQuake from `https://github.com/id-Software/Quake`
  commit `bf4ac424ce754894ac8f1dae6a3981954bc9852d` (2012-01-31), only the
  files the software renderer needs. Changes, marked `ARMDOS`: `quakedef.h`
  (includes `sys_armdos.h`), `model.c` and `r_part.c` (loops that walked a
  2-D array with one flat index - undefined behaviour GCC 14 miscompiles),
  `sv_user.c` (a flag mask assigned to a `qboolean`). Built with
  `-fsigned-char` (Quake assumes x86's signed `char`), `-fno-short-enums`
  (arm-none-eabi's short enums would make `qboolean` a byte) and `-fcommon`.
* `3rdparty/quake/`: `ID1/PAK0.PAK`, `SLICNSE.TXT`, `LICINFO.TXT`,
  `QUAKE106.ZIP` (fetched by tools/fetch-3rdparty.sh, not in the repository) - the shareware data, its licence and id's complete
  shareware release (see below).
* `hd.json` - disk fragment: `C:\GAMES\QUAKE\QUAKE.EXE`, `SLICNSE.TXT`,
  `LICINFO.TXT`, `QUAKE106.ZIP`, `C:\GAMES\QUAKE\ID1\PAK0.PAK` (18.7 MB; the 128 MB C: has
  room), QUAKE.EXE kept out of `C:\DOS`.
* `tests/run.mjs`, `tests/profile.mjs`.

## Licences and provenance

**Code:** GPL-2 (or later): Quake source (c) 1996-1997 id Software, released
under the GPL on 1999-12-21; the licence text is `src/COPYING` (id's
`gnu.txt`). The ARM-DOS platform files are under the same licence.
Distributing QUAKE.EXE means offering this source.

**Data:** `3rdparty/quake/ID1/PAK0.PAK` is the **unmodified shareware Quake v1.06
pak0.pak**:

| | |
|---|---|
| size | 18,689,235 bytes |
| SHA-1 | `36b42dc7b6313fd9cabc0be8b9e9864840929735` |
| MD5 | `5906e5998fc3d896ddaf5e6a62e03abb` |
| SHA-256 | `35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af` |
| downloaded from | `https://ftp.gamers.org/pub/idgames/idstuff/quake/quake106.zip` (id's shareware release; zip SHA-1 `f8a1a509b094ccdbed3c54b96f7d9b351c0898f5`, 9,094,045 bytes), 2026-09-24; `PAK0.PAK` extracted from its LHA archive `resource.1` (entry `ID1/PAK0.PAK`, dated 1996-10-01 14:25:58) |
| checked against | Debian game-data-packager `data/quake.yaml` (salsa.debian.org/games-team/game-data-packager): `id1/pak0.pak?106`, size 18689235, MD5 `5906e599...`, SHA-1 `36b42dc7...` - both match; its `slicnse.txt` (10036 bytes) and `licinfo.txt?shareware` (9311 bytes) entries match the two licence files shipped here |

Terms: the shareware licence (`SLICNSE.TXT`, "SHAREWARE VERSION: QUAKE
LIMITED USE SOFTWARE LICENSE AGREEMENT", June 21, 1996), section 6: "ID grants
to you, the end-user, the limited right to distribute, **free of charge
only**, the Software as a whole", "so long as this Agreement accompanies the
Software at all times"; section 7: "you may make copies of the Software to
give to other persons. You may not charge or receive any consideration";
section 2 forbids any commercial use. `LICINFO.TXT` summarises: play and
enjoy the single player game; no commercial exploitation; the shareware
version must not run user-developed levels. So ARM-DOS may ship `PAK0.PAK`
**unmodified, free of charge, with SLICNSE.TXT beside it**. Taking the
stricter reading of "the Software as a whole", `C:\GAMES\QUAKE\` also
carries id's **complete, unmodified shareware release `QUAKE106.ZIP`**
(byte-identical to `quake106.zip` above, SHA-1
`f8a1a509b094ccdbed3c54b96f7d9b351c0898f5`; its `resource.1` holds id's
QUAKE.EXE, README, HELP, ORDER, TECHINFO, ...), as `apps/wolf3d` ships
1WOLF14.ZIP. `SLICNSE.TXT` and `LICINFO.TXT` are id's files byte for byte
(already CR LF, so the disk tool leaves them alone); `.ZIP`/`.PAK` are not on
its text list, so no `"binary": true` is needed. The registered pak1.pak must never be
included. "Quake" is a trademark of id Software / ZeniMax; this port is not
affiliated with them. The data (18.7 MB pak + 9.1 MB zip) is committed
to git in `3rdparty/quake/`.
