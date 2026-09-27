# Hexen II for ARM-DOS (engine only)

Raven Software's Hexen II (1997) as a **DOS program on an ARM PC**:
`C:\GAMES\HEXEN2\H2.EXE`, the uHexen2 / Hammer of Thyrion 1.5.10 engine
(GPL-2), software renderer, with an ARM-DOS platform layer written after
uHexen2's own DJGPP DOS port (`sys_dos.c`, `vid_dos.c`/`vid_vga.c`,
`in_dos.c`, `snd_sb.c`) and the ARM-DOS Quake (`apps/quake`).

**ARM-DOS ships no Hexen II data.** The demo's `pak0.pak` may not be
redistributed (see *Why engine-only* below), so the player brings it:
`C:\GAMES\HEXEN2\README.TXT` explains where the official demo is, how to
open it on a modern computer (7-Zip / `unzip`: it is a WinZip SFX; the file
is `Install/Hexen2/data1/pak0.pak`) and two ways into the ARM PC: the web
page's Files panel (machine off, `C:` tab, `GAMES\HEXEN2\DATA1`, *Copy files
in...*) or the Host Link by modem (`GETPAK.BAT`, ~6 hours at 14,400 bps).

```
C:\>CD \GAMES\HEXEN2
C:\GAMES\HEXEN2>INSTALL          checks DATA1\PAK0.PAK (size, pak directory, CRC-32)
C:\GAMES\HEXEN2>HEXEN2           or H2; C:\DOS\HEXEN2.BAT works from anywhere
C:\GAMES\HEXEN2>GETPAK           TERM + Host Link: receive pak0.pak by ZMODEM
C:\GAMES\HEXEN2>H2 -nosound -nomouse -mem 10 -sndspeed 22050
```

## What is on C: (apps/hexen2/hd.json)

| file | what |
|---|---|
| `GAMES\HEXEN2\H2.EXE` | the engine: extender stub + game image (below), 603 KB |
| `GAMES\HEXEN2\H2CHECK.EXE` | `check/h2check.c`: size, `PACK` directory (file count + the engine's CRC-16 of the directory, as `quakefs.c` checks it) and a CRC-32 of the whole file; errorlevel 0 good, 1 missing, 2 not a known pak, 3 damaged. Knows the demo 1.11 pak0 (27,750,257 bytes, 797 files, CRC 22780, CRC-32 `BB755DBC`) and the retail 1.11 pak0 (needs pak1) |
| `GAMES\HEXEN2\INSTALL.BAT` | runs H2CHECK, or shows `HOWTO.TXT` (through MORE) when PAK0.PAK is missing |
| `GAMES\HEXEN2\HEXEN2.BAT`, `DOS\HEXEN2.BAT` | start H2.EXE if DATA1\PAK0.PAK exists, else INSTALL. (`H2.BAT` would be shadowed by H2.EXE in its own directory, so the launcher is HEXEN2.BAT.) |
| `GAMES\HEXEN2\GETPAK.BAT`, `GETPAK.SCR` | TERM script (`TERM /S:`): 19200 bps UART, `AT&C1&D2&B14400`, dial 555-0100, `D`, wait up to 10 minutes for the visitor to pick the file, ZMODEM download into DATA1 (auto-start, crash recovery), `G`, hang up; errorlevels 0/1/2/3 like apps/keen's GETKEEN |
| `GAMES\HEXEN2\README.TXT`, `HOWTO.TXT` | the player's instructions (CR LF, 80 columns) |
| `GAMES\HEXEN2\DATA1\PROGS.DAT` | the Hexen II game code, **compiled here from the GPL HexenC source** (`gamecode/h2/`, uHexen2 1.29c) with uHexen2's `hcc -os` - `tools/build-progs.sh` (optional; needs uHexen2's `hcc`, given as its argument or `HCC=`) rebuilds it byte-identically (SHA-1 `468cfbed28e857206babcb9d8f450b980e9cc052`). uHexen2's own demo packages use their progs.dat the same way; the demo's pak0.pak has no progs.dat inside (Raven's lay loose next to it) |
| `GAMES\HEXEN2\DATA1\HEXEN.RC` | uHexen2's `gamecode/res/h2/hexen.rc` (GPL), also not inside the pak |

So the player needs exactly one file, `PAK0.PAK`. `excludeFromDos` keeps
H2.EXE and H2CHECK.EXE out of `C:\DOS`.

## How it does DOS things

| | how | where |
|---|---|---|
| memory | H2DOS.EXE ran under CWSDPMI. H2.EXE is a **bound extender program** like DUKE3D.EXE (`loader/h2load.c`, after `apps/duke3d/loader/d3dload.c`): a 15 KB stub allocates one XMS block (HIMEM.SYS), loads and relocates the game image (531 KB code+data, 1.36 MB bss) there, leaves the handle in PSP 58h-5Bh ("DX"+handle, freed at exit) and enters it; the game's C runtime then takes its heap from XMS. The hunk is as much as is left (**12.8 MB** of the 15.3 MB XMS; `-mem N`). Conventional memory, otherwise idle, holds the **main zone** (384 KB, `zone.c` marked ARMDOS; `-zone N`) and, if 192 KB are left, the C stack (else 256 KB of XMS - the engine goes 150 KB deep; painted, `sys_stack` in the console reports). Without HIMEM.SYS: "This program requires an extended memory manager (HIMEM.SYS)." | `loader/h2load.c`, `src/sys_armdos.c` |
| video | INT 10h AX=0013h, **320x200 mode 13h** (H2DOS.EXE's mode 0; the ARM PC has no VESA/Mode X). z-buffer, surface cache (768 KB) and frame buffer from the high hunk as in `vid_vga.c`; `VID_Update` copies dirty rectangles to A000:0000; palette to 3C8h/3C9h; the loading icon drawn straight into video memory. The Options menu has no "Video Modes" entry (the uHexen2 behaviour for a driver without a mode menu). | `src/vid_armdos.c` |
| keyboard | own INT 09h handler, set-1 scan codes into a ring, `sys_dos.c`'s `scantokey` table, E0/E1 handling, BDA shift flags cleared on exit | `src/sys_armdos.c` |
| timer | PIT channel 0 mode 2 at ~1 kHz, INT 08h handler, BIOS tick chained every 65536 counts; WFI while ahead of the 72 fps frame clock | `src/sys_armdos.c` |
| sound | Sound Blaster 16 via `sb.h` (BLASTER=A220 I7 D1 H5 T6) as uHexen2's `snddrv_blaster`: **11025 Hz** by default (Hexen II's sounds are 11 kHz; `-sndspeed 22050`), 16-bit stereo on DSP 4.xx, 8-bit on older; the engine mixes into an 8 KB ring, the IRQ copies 512-byte blocks. No music (CD audio and MIDI are compiled out). | `src/snd_armdos.c`, `snd_sys.h` (ARMDOS) |
| mouse | INT 33h (MOUSE.COM): reset, buttons, motion counters (in_dos.c). No joystick. | `src/in_armdos.c` |
| FPU | `$(ARMDOS_VFP)`; FPEXC.EN check, "Hexen II requires a floating-point processor" | `src/sys_armdos.c` |
| files | newlib stdio -> INT 21h. DATA1 is looked for in the current directory, else next to H2.EXE (so `C:\>GAMES\HEXEN2\H2` works). Saves (`DATA1\S0..`, `QUICK`), CONFIG.CFG, demos go to DATA1. Console text goes to port E9h always and to stdout only while in text mode. | `src/sys_armdos.c` |
| network | loopback only | `src/net_armdos.c` |

## Performance (100 MHz ARM PC, node, JIT)

| | |
|---|---|
| `timedemo` of a walk through Blackmarsh (recorded by the test, `-nosound`) | **28.8 fps** (468 frames in 16.3 s; 2.1 G instructions) |
| `timerefresh`, Atrium of Immolation (sound on) | 17.0 fps |
| playing | CPU 100% busy (below the 72 fps cap) |
| emulator | ~650-730 host MIPS during the timedemo |

## Tests (`make hexen2-test`, part of `make test`)

`tests/run.mjs` builds its own C: in memory (never on disk) from
`hd.json` + HIMEM, MOUSE, MORE, AUTOEXEC with `SET BLASTER`.

* `nodata` (always): `HEXEN2` without data -> INSTALL's explanation (with MORE),
  `H2` -> "FATAL ERROR: Unable to find a proper Hexen II installation" in text
  mode, INSTALL/H2CHECK reject a fake PAK0.PAK, no HIMEM.SYS -> the stub's message.
* With the demo pak (`--pak FILE` / `$HEXEN2_PAK`, a local copy of the demo's
  `data1/pak0.pak`, **never in the project**; skipped without it): `play` - INSTALL (CRC-32 good), HEXEN2, main menu, Single
  Player -> New Game -> Crusader -> difficulty, Blackmarsh, walk, turn, attack
  (SB16 audio RMS), jump, mouse turn, F6/F9 quick save/load, `map demo2/demo3/ravdm1`
  (Barbican, The Mill with a fight against its archers, Atrium of Immolation),
  timerefresh, quit -> text mode, INT 08h/09h restored, CONFIG.CFG written;
  `timedemo` - record + play back; `files` - `web/js/fat.js` (the Files
  panel's writer) copies pak0.pak onto the factory C:, then INSTALL and a start.
* `tests/hostlink.mjs` (by hand, ~1 min; `--real` ~2 min): GETPAK.BAT -> TERM
  script -> the JS Host Link (`emu/hostlink.mjs`) offers pak0.pak -> ZMODEM ->
  DATA1\PAK0.PAK byte-identical -> INSTALL good. Results: the whole 27.7 MB pak
  at 115200 bps with the line dropped at 10 MB and GETPAK run again (ZMODEM
  resume): intact, 2,804 s emulated / 64 s host; at the visitor's real
  settings (`--real`: AT&B14400, UART 19200): intact, **21,895 s = 6.1 hours
  emulated** (1,267 bytes/s including the call), 117 s host.

Screenshots: `build/hexen2-test/` (they show demo content: local only).

## Deviations from H2DOS.EXE

* 320x200 only; no music (no CD audio / MIDI); no network play; no joystick.
* `MAX_MOD_KNOWN` 512 instead of 2048 (unreferenced brush models are recycled
  when it fills), the zone and stack in conventional memory - to fit the 16 MB
  machine. The demo's four maps load with an estimated 0.4 MB of hunk to spare at the peak
  (Barbican); the retail game is recognised by H2CHECK but untested and its
  bigger maps may not fit.
* Banner "Hexen II for ARM-DOS - Hammer of Thyrion 1.5.10"; the menu shows
  "Hexen2 1.29 (ARM-DOS)" (`PLATFORM_STRING`, arch_def.h ARMDOS).
* Out-of-hunk errors print the hunk's low/high use (zone.c ARMDOS).

## Source and changes

`src/hexen2/`, `src/h2shared/`, `src/common/`: uHexen2
`https://git.code.sf.net/p/uhexen2/uhexen2` commit
`475c048b1c8b0806b8585f5cd2715f3ad60bf393` (2026-09-04), only the files the
software renderer needs (+ `hexen2/dos/dos_sock.h`). Changes, marked `ARMDOS`:
`quakeinc.h` (includes `src/sys_armdos.h`), `snd_sys.h` (only the SB driver),
`arch_def.h` (PLATFORM_STRING), `model.c` (MAX_MOD_KNOWN), `zone.c` (zone
block, error text). Built with `-D__MSDOS__ -DARMDOS -DNO_PCI_AUDIO
-D_NO_CDAUDIO -D_NO_MIDIDRV -fsigned-char -fno-short-enums -fcommon`; hot
renderer files `-O2`, the rest `-Os`. `gamecode/h2/`: uHexen2's HexenC game
code (same commit). Licence: GPL-2 (`src/COPYING`), ARM-DOS files likewise.

## Why engine-only

| file | source | SHA-1 |
|---|---|---|
| `H2Demo.exe` (Aug 1997 demo v0.42, WinZip SFX, 12,244,480 bytes) | `https://www.gamers.org/pub/idgames/idstuff/hexen2/H2Demo.exe` | `1b02a98ad2ab344d7527d3dda7e8893f528229d7` |
| `h2demo.exe` (Nov 1997 demo v1.11, WinZip SFX, 15,334,912 bytes) | `https://www.gamers.org/pub/games/hexen2/official/demo/h2demo.exe` | `dcb5ab7c92352a8af4975151bd089354723d9417` |
| `Install/Hexen2/data1/pak0.pak` from the Nov 1997 demo (27,750,257 bytes, CRC-32 `BB755DBC`, MD5 `8e598d82bf53436ed7a0e133aa4b9f09`) | inside the above | `a8cfd8790819c47998141adb97c14d1680cf8738` |
| `HEXEN II SUBLICENSE.doc` (30,208 bytes, 1997-08-27; in both demos) | inside both | `1aadeaf00dc72726e03bcfeb2fa0af93107852a7` |

The demo's only licence is Activision's "SOFTWARE SUBLICENSE AGREEMENT": "the
non-exclusive, non-transferable, limited right and sublicense to install and
use one copy of this Program solely and exclusively for your personal use",
and "YOU SHALL NOT: ... Make copies of this Program or any part thereof ...
Sell, rent, lease, license, **distribute** or otherwise transfer this Program
... without the express prior written consent of Activision." The Nov 1997
installer also warns against "Unauthorized reproduction or distribution".
Unlike id's Quake shareware licence there is no grant to pass it on; the GPL
source release did not cover the pak data; other ports (e.g. DCH2) say the
same; Hexen II is still sold. The v0.42 demo is refused by the engine (old
demo support is off, `ENABLE_OLD_DEMO`). The data was used here for local
testing only (outside the repository); a scan of
build/*.img and build/site (samples of the pak's contents and directory,
gunzipped) found none of it.
