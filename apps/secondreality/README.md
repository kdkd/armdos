# Second Reality (Future Crew, 1993) on the ARM PC

Future Crew's Assembly '93 winner, the unmodified x86 binaries, run by ELBOW
(apps/x86) on the ARM PC: C:\ELBOW\DEMOS\SECOND (`ELBOW SECOND`, the ELBOW
gallery's `MENU 17`, or `C:\ELBOW\DEMOS\SECOND.BAT`).

## Provenance and licence

* Source: https://github.com/mtuomi/SecondReality, commit
  `071a82eddb7a7390dbaa491a8a473e0e923a55c2` (2022-06-27). Future Crew released the source code and data
  for the demo's 20th anniversary (2013) into the public domain with the
  Unlicense (`UNLICENSE`, sha256 `7e12e5df…94e85c`; shipped as `UNLICENS.TXT`).
  The Unlicense covers the binaries and data too. The 1993 texts inside the
  release (FCINFO10.TXT, README.1ST) predate it and speak of freeware.
* The repository has no packed release `SECOND.EXE` (the 1993 distribution's
  loader with all parts appended and encrypted). What it has is the demo's loader
  `MAIN/U2.EXE` built with `FINAL=1` (sha256
  `9a066292…` is the top-level `U2.EXE`, a slightly older build of the same; we use
  `MAIN/U2.EXE`) and the parts in `MAIN/DATA`. The FINAL loader wants
  `SECOND.EXE` and `REALITY.FC` in the current directory; when `SECOND.EXE` carries
  no appended pack (no `0FC0h` signature at the offset its last dword names) it
  opens every part by name, trying the current directory and then `DATA\`. So
  `3rdparty/secondreality/` (made by tools/fetch-3rdparty.sh from the pinned commit;
  not in this repository) is `MAIN/U2.EXE` renamed `SECOND.EXE` next to every file of
  `MAIN/DATA` - nothing was changed, nothing rebuilt. `SHA256.txt` lists all 82 files.
* `dist/README.TXT` (on the disk as README.TXT) is ours; `dist/SECOND.BAT`
  starts it from anywhere.

## What it took (the "crashes immediately" report)

1. **ELBOW: INT 21h AH=55h ignored SI.** The demo loads each part with its own
   EXE loader (`U2A.ASM` `loadexe`/`runexe`): AH=55h to create the child PSP with
   SI = the end of the part's memory, AH=50h, a far jump. ELBOW copied the parent's
   memory-size word instead, and every part is PKLITE-compressed: the PKLITE stub
   compares that word with what it needs and said "Not enough memory" - every part
   "crashed" at once. Fixed (proc.c).
2. **ELBOW: a lost interrupt.** The run loop set `irq_pending |= PEND_INHIBIT` (after
   STI, MOV SS...) with a plain read-modify-write while the ARM IRQ handler sets
   `PEND_IRQ0` in the same word; an IRQ between the load and the store was lost,
   IRQ0 stayed in service at the PIC for ever and the demo's copper/music timer
   stopped (the intro hung after a few seconds). Fixed with an IRQ-safe `pend_set()`.
3. **The VGA.** Every part programs the card directly: mode 13h through the BIOS,
   then chain-4 off and the CRTC in byte mode (Mode X/Y: 320x200, 320x400, a 640-wide
   virtual screen scrolled by the start address and pixel panning, line compare,
   palette changes timed by the PIT "copper"), and TECHNO uses EGA mode 0Dh. The
   emulator now has the real register model and the four planes (emu/dev/vga.mjs,
   emu/render.mjs, ARCH.md §6, bios/README.md).
4. **Sound Blaster music** needs 1 MB of EMS (Scream Tracker's STMIK keeps the
   samples there) and DMA from x86 memory: ELBOW now has LIM EMS 4.0 (ems.c, 2.5 MB,
   page frame E000h, `EMMXXXX0`) and moves the 8237 page registers by where its x86
   memory lives (the memory is 128 KB aligned, so address and count registers pass
   through unchanged). There is no Gravis UltraSound: choose "SoundBlaster Pro" in
   the setup screen (Left arrow once); the SB16 plays SB Pro stereo.
5. **Speed.** The translator used to refuse every instruction with a 66h prefix, the
   0Fh two-byte opcodes, FS/GS overrides, IN/OUT and far CALL/JMP/RETF, LES/LDS,
   PUSHA/POPA - most of this 386 demo ran in the interpreter (11% of the ARM's time
   in translated code). It now translates them (66% in translated code); STMIK mixes
   at "High" quality in real time at 100 MHz.

No 386 protected mode, unreal mode or XMS is involved.

## Status

The whole demo runs, start to end, with music: the intro texts over the
landscape, the ships (U2A), the PAM explosion, the SECOND REALITY logo, the glenz
vectors, the dot tunnel, TECHNO (EGA mode 0Dh: interference circles, the falling
curtain, the rotating bars through the plane masks), the troll picture, the
forest/mountain scroller, the lens and the rotozoomer, plasma and the plasma cube,
the mini vector balls, the raytraced mirror balls, the 3-D sinus field (voxel
landscape), the JP logo zoom, the vector city (U2E), the end logo, the credits and
the end scroller - each looked at in screenshots (full runs with No sound and with
the Sound Blaster, at 100 and 133 MHz).

Speed. ELBOW translates 85% of the demo's time into ARM code (about 12 ARM
instructions per x86 instruction): at the nominal 100 MHz the ARM PC is roughly a
386DX-33 for this demo, which Future Crew wrote for a 486-33 ("the demo also runs
on slower 386 computers, but some parts will naturally slow down"). Pictures per
second (`tests/fps.mjs`, VGA 70 Hz): glenz vectors and the dot tunnel 67-70, the
intro 20-35, the landscape scroll ~20, heavy 3-D scenes (the ships, the city) 6-15.
The music itself always plays at full speed (STMIK at "High" quality).

The parts are synchronised to the music, and like on a slow 386 some sync points
are reached late - then a part waits for the next mark in the music on its last
picture (a white screen before TECHNO's bars or the plasma, for 10-30 s, with the
Sound Blaster at 100 MHz). With "No sound", or at 133 MHz (the clock jumper in
"Open the box"), the demo keeps up. The reverse also exists in Future Crew's code:
the 3-D sinus field counts the frames it spent waiting for its music mark, and if
it arrives more than 5.7 s early its landscape never rises (a black screen for that
part) - it depends on the timing, seen at 133 MHz. In one No-sound run the mini
vector balls ended with the C runtime's "run-time error R6003 - integer divide by
0" (an IDIV overflow in its projection with the positions of that moment; the demo
carries on with the next part).

## Tests

`make secondreality-test` (tests/run.mjs, ~1 minute): the setup screen, the Sound
Blaster Pro choice, "A Future Crew Production" in the unchained 320x400 mode with the
planar window open, music (SB16 output not silent, from EMS through the DMA page
translation), the landscape, and Ctrl ending the demo cleanly (the end screen from
U2END.BIN, text mode, the window closed, the prompt). ELBOW's own regressions are in
apps/x86/tests (VGAEMS.COM: AH=55h, the IRQ race, EMS, EGA info, mode 12h write
mode 2, Mode X latch copies with REP MOVSB, DMA page registers). Manual tools:
`tests/try.mjs` (run with traces, screenshots, VGA/music/PIC state), `tests/prof.mjs`
(where the ARM's time goes, why translated blocks end), `tests/fps.mjs`.

## Deviations

* The setup screen offers the Gravis UltraSound first (Future Crew's default); it
  is not there - choosing it ends with "Failed to initialize the selected
  soundcard". Sound Blaster Pro or No sound.
* The display is always 70 Hz (the real card's 60 Hz 480-line timing is not
  modelled; Second Reality uses the 70 Hz modes).
* STMIK maps four EMS pages per sample even for shorter samples; like EMM386,
  ELBOW answers the extra ones with error 8Ah and the player ignores it.
