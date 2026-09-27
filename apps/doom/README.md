# DOOM for ARM-DOS

The shareware DOOM (v1.9 data) as a **DOS program on an ARM PC**: `C:\GAMES\DOOM\DOOM.EXE`.
It is [doomgeneric](https://github.com/ozkl/doomgeneric) (a Chocolate Doom
derivative) with an ARM-DOS platform layer that talks to the hardware the way
id's DOOM.EXE did on a 1993 PC - except that every instruction is ARM.

```
C:\>CD \GAMES\DOOM
C:\GAMES\DOOM>DOOM                      (or C:\GAMES\DOOM\DOOM.EXE from anywhere)
C:\GAMES\DOOM>DOOM -timedemo demo1      benchmark: prints fps on exit
```

Keys are vanilla DOOM's: arrows move, **Ctrl** fires, **Space** opens doors,
Alt strafes, Shift runs, 1-7 weapons, Tab automap, Esc menu, F10 quits
(then the ENDOOM screen).

## How it does DOS things

| | how | where |
|---|---|---|
| video | INT 10h AX=0013h -> VGA mode 13h; each frame is DOOM's own 8-bit screen `memcpy`'d to the linear frame buffer at 0xA0000 (doomgeneric's `CMAP256` path; no 32-bit conversion). PLAYPAL palettes (damage/pickup flashes too) go to the DAC through ports 0x3C8/0x3C9, 6 bits per gun. | `doomgeneric_armdos.c`, `i_video.c` |
| keyboard | own INT 09h handler (INT 21h AH=35h/25h): reads set-1 scan codes from port 0x60 into a ring buffer, EOI to port 0x20; `DG_GetKey` decodes E0 (grey arrows, right Ctrl/Alt, keypad Enter) and E1 (Pause) sequences. The BIOS never sees the keys, as with DOOM; the old vector is restored on exit and the BDA shift flags cleared. | `doomgeneric_armdos.c` |
| timer | PIT channel 0 reprogrammed to **140 Hz** (divisor 8523, mode 3 - the DMX sound library's rate) with an INT 08h handler that counts ticks and **chains the BIOS handler every 65536 PIT counts** (the BIOS then sends the EOI), so 0x46C keeps running at 18.2 Hz and the DOS clock stays right. DOOM's 35 Hz tic = 4 ticks. Divisor 0 and the old vector come back on exit. | `doomgeneric_armdos.c` |
| sound | **With `BLASTER` set** (AUTOEXEC.BAT: `A220 I7 D1 H5 T6`) and the card answering, the **Sound Blaster 16**, as DMX would: the WAD's 8-bit **DS\*** effects (16 padding bytes skipped at each end, as DMX) mixed in software, 16 channels with DOOM's volume/separation law, into a **16-bit stereo 11025 Hz auto-init DMA stream** (DMA 5, 256-frame halves = 23 ms) filled from the SB's IRQ 7 handler (sdk `sb_start`). **Otherwise** the WAD's **DP\*** PC-speaker lumps, one tone per 140 Hz tick from the timer interrupt: PIT channel 2 (mode 3, DMX's divisor table) gated by port 0x61 bits 0-1; one sound at a time, the newest wins, as DMX did. `I_InitSound` prints which. | `i_sound.c`, `i_sb_armdos.c`, `i_pcsound_armdos.c` |
| music | **OPL music** on the SB's OPL3 (AdLib at 388h): the **MUS** lumps through the WAD's **GENMIDI** bank with DMX's logic, taken function by function from Chocolate Doom's `i_oplmusic.c` (voice allocation and stealing, double-voice instruments with fine tuning, DMX's frequency and volume tables, the OPL register init) but interpreting MUS directly, **one MUS tick per 140 Hz timer interrupt** - DMX's own clock. OPL2 mode (9 voices) as DOOM 1.9; `DMXOPTION=-opl3` gives 18 voices and stereo, `-reverse` swaps DMX's reversed channels. Checked note for note against Chocolate Doom (below). No music with the PC speaker. | `i_oplmus_armdos.c` |
| GM music | **With an MPU-401** answering (BLASTER's P, else 330h; ARCH.md 4.3.1) DOOM plays **General MIDI** instead - DMX's "General MIDI" music device: the MUS score interpreted on the same 140 Hz tick, each event sent to the MPU in UART mode (channels 0-8 -> 1-9, 9-14 -> 11-16, percussion 15 -> 10; mus2mid's controller table; CC 7 = MUS volume x music volume), and the title plays D_INTRO (the GM version) instead of D_INTROA. `DMXOPTION=-fm` keeps the OPL3. Tested note for note against the lump (`tests/gm.mjs`, `make doom-gm-test` in apps/midi). | `i_mpumus_armdos.c` |
| idle | when DOOM is ahead of its tic clock, `DG_SleepMs` sits in **WFI** (CP15 wait-for-interrupt) until the next timer tick: an idle browser tab burns nothing. | `doomgeneric_armdos.c` |
| joystick | **Vanilla DOS DOOM's**: off unless DEFAULT.CFG has `use_joystick 1` (the vanilla default is 0; SETUP.EXE used to set it). Then `I_StartupJoystick` reads the stick through the BIOS (**INT 15h AH=84h**, DX=1 axes, DX=0 buttons) and asks, each answered with button 1 (Esc skips): `CENTER the JOYSTICK and press button 1:`, `Push the JOYSTICK to the UPPER LEFT corner ...`, `... LOWER RIGHT corner ...`; thresholds half way between the centre and each corner, as in i_ibm.c (whose `joyyl` used centerx - fixed here). Per tic `I_JoystickEvents` posts `ev_joystick` (data2/data3 = -1/0/+1: turn, walk; data1 = buttons) and g_game.c does the rest with the vanilla `joyb_fire 0`, `joyb_strafe 1`, `joyb_use 3`, `joyb_speed 2`. No stick: `joystick not found`; `-nojoy` skips it. Marked `ARMDOS` in `i_joystick.c` (the SDL code stays under ORIGCODE), the per-tic call in `i_video.c` `I_StartTic`; `m_config.c` now reads DEFAULT.CFG/EXTENDED.CFG (loading was compiled out; saving still is). | `i_joystick.c`, `i_video.c`, `m_config.c` |
| mouse | **Vanilla DOS DOOM's**, through the **INT 33h driver** (load `MOUSE.COM` first): `I_StartupMouse` resets it with AX=0000h (`Mouse: detected` / `Mouse: not present`; skipped with `use_mouse 0` in DEFAULT.CFG or `-nomouse`; the default is 1, as vanilla). Each tic `I_ReadMouse` takes the buttons (AX=0003h) and the mickeys moved since the last tic (AX=000Bh) and posts one `ev_mouse` (x = turn, -y = walk); g_game.c scales them by `mouse_sensitivity` (Options menu) and maps buttons 1/2/3 to `mouseb_fire 0`, `mouseb_strafe 1`, `mouseb_forward 2`. In the browser the visitor must capture the mouse (the page's Mouse button, or fullscreen) for the PS/2 mouse to move. | `doomgeneric_armdos.c`, `i_video.c` |
| files | DOOM1.WAD, DEFAULT.CFG, EXTENDED.CFG and savegames through newlib stdio -> INT 21h. The IWAD is looked for in the current directory and then in DOOM.EXE's own directory (argv[0]). | `d_iwad.c`, `m_config.c`, `d_main.c` |
| memory | DOOM.EXE (402 KB file: 372 KB image + relocations; +16 KB for the Sound Blaster/OPL code) needs 501 KB (512,768 bytes) of conventional memory with its 97 KB bss and 32 KB stack (with COMMAND.COM and HIMEM.SYS loaded it runs; the kernel leaves 582 KB for the shell); the big renderer arrays (visplanes, openings, drawsegs, vissprites: 145 KB) were moved from bss into the zone for that; the 8 MB zone comes from the C runtime's heap, which continues in **extended memory via XMS**. Until HIMEM.SYS exists, the runtime's opt-in fallback takes the raw extended memory INT 15h AH=88h reports (pre-XMS DOS-extender style). | `i_system.c`, `r_plane.c`, `sdk/libdos/startup.c` |
| exit | back to mode 3 via INT 10h, **ENDOOM** copied to 0xB8000, cursor on row 23 and a newline - exactly DOOM.EXE's `I_Quit`. `I_Error` restores text mode first so the message is readable (and also writes it to debug port E9h). | `i_endoom.c`, `i_system.c` |

The CPU has no FPU: everything is `-mfloat-abi=soft`; DOOM is fixed-point
(the few floats - mouse acceleration, the timedemo report - are not hot).

## Performance

`make doom-test` (node 22, Ryzen 9 9955HX; the emulated machine runs 1
instruction per cycle at its nominal clock):

| | result |
|---|---|
| `-timedemo demo1` at the ARM-PC's **100 MHz** | **77.2 fps** (5026 tics in 65.1 s emulated, 6.4 G instructions) |
| same, machine at 33 MHz (`--mhz 33`) | 25.8 fps |
| emulator speed during the timedemo | **715-746 host MIPS** (JIT) = 7.3-7.6x real time at 100 MHz |
| playing E1M1 (walking, capped at DOOM's 35 fps) | CPU busy ~62%, the rest in WFI |

So DOOM runs at its full 35 fps with room to spare, and in real time (the
browser's `RealtimeDriver`) the host is loaded at roughly 62% / 7.4 = ~8% of
a core while playing.

Profile of the timedemo (`node apps/doom/tests/profile.mjs`, PC sampling):
R_DrawColumn 36%, R_DrawSpan 20%, R_RenderSegLoop 7%, 32-bit division 6%,
memcpy (incl. the 64 KB blit) 4%, R_MakeSpans 3% - the classic DOOM profile.
The renderer, fixed-point, blockmap/sight code and the zone allocator are
built `-O2`, the rest `-Os` (the image must fit in conventional memory).

## Building and testing

```sh
make DOOM                  # build/DOOM.EXE  (apps/doom/app.mk, SDK armdos_exe)
make images                # build/hd.img gets C:\GAMES\DOOM\ (apps/doom/hd.json)
make doom-test             # boots ARM-DOS headless and plays (needs the kernel + build/ktest/TSHELL.EXE)
make doom-mouse-test       # the mouse via MOUSE.COM: detected, turn, fire, forward, sensitivity, no driver, use_mouse 0 (tests/mouse.mjs)
make doom-joy-test         # the joystick: calibration prompts, walk/turn/fire with the emulated stick (tests/joystick.mjs)
node apps/doom/tests/run.mjs play|timedemo|wadcheck|fromroot [--mhz N] [--no-jit] [--out DIR]
```

`tests/run.mjs` builds its own C: image (IO.SYS, ARMDOS.SYS, a CONFIG.SYS whose
`SHELL=` is the kernel's test shell running `mem`, DOOM, `mem`, `halt`), boots it
with `emu/testkit.mjs` and presses real keys through the 8042. It checks:
mode 13h set; INT 08h/09h hooked while running and restored after; the BIOS
tick at 18.2 Hz while DOOM owns the PIT and after; the PC speaker playing DP*
tones and silent after exit; back in mode 3 with ENDOOM on screen; the
timedemo result. Screenshots go to `build/doom-test/`: `title.png`,
`menu.png`, `game1.png` (E1M1 start), `game2.png`, `game3-fire.png`,
`game4-automap.png`, `game5.png`, `quit-prompt.png`, `endoom.png`,
`timedemo-end.png`. `wadcheck` (tests/wadcheck.c) compares every lump read via
fseek/fread with a sequential read (and the runner compares that with the host
file). When COMMAND.COM exists, `DOOM` can simply be typed at `C:\>`.

`tests/joystick.mjs` (`make doom-joy-test`, also under `make test`): DEFAULT.CFG with `use_joystick 1`, a stick plugged into the emulated game port (`m.joy`), the three calibration prompts answered by moving it and pressing button 1; in E1M1 (`-warp 1 1`) the view stays put with the stick centred, changes when it is pushed forward (walk) and right (turn), and button 1 fires (the ammo digits change). Also: `use_joystick 1` with nothing plugged in says `joystick not found` and plays on; the vanilla default (no DEFAULT.CFG) prints nothing about joysticks. Screenshots in `build/doom-test/joy/`.

## Files

* `src/doomgeneric_armdos.c` - the platform layer (DG_Init, DG_DrawFrame,
  DG_SleepMs, DG_GetTicksMs, DG_GetKey, DG_SetWindowTitle, main, the INT 08h/09h
  handlers, mode 13h, palette, ENDOOM).
* `src/i_pcsound_armdos.c` - PC speaker sound module (+ a null music module).
* `src/i_sb_armdos.c` - Sound Blaster sound effects module (IRQ-driven mixer).
* `src/i_oplmus_armdos.c` - OPL music module (MUS on the OPL3, DMX-style), from Chocolate Doom's `i_oplmusic.c`.
* `tests/sound.mjs`, `tests/oplref/` - the Sound Blaster test and its Chocolate Doom reference.
* `src/i_armdos.h` - hooks between the two and the doomgeneric sources.
* `src/*` - doomgeneric, vendored from `https://github.com/ozkl/doomgeneric`
  commit `dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284` (2026-04-12). Changes, all
  under `#ifdef ARMDOS`: `i_video.c` (8-bit frame straight to the platform
  layer, mode set in I_InitGraphics, palette index lookup), `doomgeneric.c` (no
  32-bit buffer), `i_endoom.c`, `i_system.c` (zone 8 MB / min 4 MB, I_Error
  restores text mode first, no zenity), `i_sound.c` (SB or PC speaker chosen from
  BLASTER, the OPL music module), `i_sound.h` (the two modules), `m_controls.c` (vanilla Ctrl/Space bindings), `m_config.c` (config in
  the current directory), `d_main.c` (8.3 config name), `d_iwad.c` (IWAD next
  to DOOM.EXE), `g_game.c` (integer timedemo report in ms), `r_plane.c`, `r_bsp.c/h`,
  `r_things.c/h` (visplanes, openings, drawsegs, vissprites - 145 KB -
  allocated from the zone in R_InitPlanes instead of living in bss).
  The Chocolate Doom PC-speaker tone table and lump handling in
  `i_pcsound_armdos.c` follow `chocolate-doom/src/i_pcsound.c` (GPL-2).
* `3rdparty/doom/DOOM1.WAD` - the shareware IWAD (see below) (not in the repository: tools/fetch-3rdparty.sh downloads it, see 3rdparty/manifest.json).
* `hd.json` - disk fragment: `C:\GAMES\DOOM\DOOM.EXE` + `DOOM1.WAD`, DOOM.EXE kept out of `C:\DOS`.
* `tests/` - `run.mjs` (headless end-to-end test), `wadcheck.c`, `profile.mjs`.

## Licences and provenance

`make doom-sound-test` (`tests/sound.mjs`) boots ARM-DOS with COMMAND.COM and the real
AUTOEXEC.BAT (SET BLASTER, SBMIX), runs DOOM from the prompt with the machine's audio
recorded to `build/doom-test/sound/doom-sb.wav`, and checks: IRQ 7 (INT 0Fh) hooked and
unmasked; title music (D_INTROA) and E1M1's audible; E1M1's bass on E2 and the riff's
fifth on B3 in the spectrum; **E1M1 note for note against Chocolate Doom's OPL player**
(`tests/oplref/`: its `i_oplmusic.c`, `midifile.c`, `mus2mid.c`, `memio.c` vendored
unchanged from chocolate-doom 895f581c with a logging OPL driver, built with the host gcc
and run on the same lump: 142/142 key-ons in 12 s with identical F-numbers and at most
0.5 ms apart); the pistol heard on top of the music; the 16-bit stereo stream on DMA 5;
after quitting, IRQ 7 masked again and the SB and OPL silent; and without BLASTER the PC
speaker and no OPL. The timedemo is unchanged (77.2 fps): mixing 43 blocks a second
costs little.

**Code:** GPL-2 (or later): DOOM source (c) 1993-1996 id Software, released
under the GPL on 1999-10-03; Chocolate Doom (c) 2005-2014 Simon Howard et al.;
doomgeneric by ozkl. The licence text is `src/LICENSE` (doomgeneric's). The
ARM-DOS platform files are under the same licence. Distributing DOOM.EXE means
offering this source.

**Data:** `3rdparty/doom/DOOM1.WAD` is the **unmodified shareware DOOM v1.9 IWAD**:

| | |
|---|---|
| size | 4,196,020 bytes |
| SHA-1 | `5b2e249b9c5133ec987b3ea77596381dc0d6bc1d` |
| MD5 | `f0cefca49926d00903cf57551d901abe` |
| SHA-256 | `1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771` |
| downloaded from | `https://www.jbserver.com/downloads/games/doom/misc/shareware/doom1.wad.zip` (zip SHA-1 `4c5d94be6b6371736831e215912ea8094068d629`, file dated 1995-02-01), 2026-09-24 |
| checked against | Debian game-data-packager `data/doom.yaml` (salsa.debian.org/games-team/game-data-packager: MD5 `f0cefca4...` and SHA-1 `5b2e249b...` for `doom1.wad?shareware`, size 4196020) and the Doom Wiki's DOOM1.WAD page (both hashes) - both match |

Terms: id Software's shareware licence allows anyone to copy and give away the
shareware version but **not to charge** for it ("You may not charge or receive
any consideration...") and not to distribute it modified; John Carmack, 23 Oct
1999 (recorded in Debian's doom-wad-shareware package): "The DOOM shareware
wad is freely distributable." So ARM-DOS may ship it **unmodified and free of
charge**; the registered/Ultimate DOOM and DOOM II WADs must never be
included. "DOOM" is a trademark of id Software / ZeniMax; this port is not
affiliated with them. The WAD is not in the repository: tools/fetch-3rdparty.sh
downloads it (3rdparty/manifest.json) to `3rdparty/doom/DOOM1.WAD`.

**AdLib only**: with BLASTER set but no Sound Blaster DSP answering
(the page's "open the box" can leave just an AdLib-compatible card), the sound effects go to the
PC speaker and the music to the OPL at 388h, as DMX did (`I_InitMusic: AdLib (FM) at 388h`).
