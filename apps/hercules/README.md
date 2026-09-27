# HERCULES.EXE - the Hercules Graphics Card demo (C:\DEMO)

For the machine's Hercules card + monochrome monitor option (the page's "Monitor"
switch: Hercules Mono Green / Amber / Paper White, then a power cycle; in node
`boot({ video: 'hercules', monitor: 'green' })`).

Programs the card directly, the way every Hercules program did (there is no BIOS
graphics mode): configuration switch 3BFh = 3 ("full"), the 6845 at 3B4h/3B5h with the
720x348 table, mode control 3B8h. Scene 1: a wireframe ARM chip (48-pin flat pack,
"ARM" on the lid) spinning over a scrolling perspective grid, drawn on the card's two
32 KB pages and flipped in the vertical retrace (3BAh bit 7 = 0). Scene 2 (Space/Enter):
a Lotus 1-2-3 style bar chart with hatched bars. Esc (or any key on the chart) goes
back to DOS: 3BFh = 0 and INT 10h AX=0007h.

Without a Hercules card it prints `This program requires a Hercules Graphics Card.`

Tests: `make hercules-test` (apps/hercules/tests/run.mjs): the whole card option -
POST, DIR in mode 7, MODE, DOOM's exit, the demo in graphics (`--shots DIR` saves PNGs),
and the unchanged VGA machine. The page's side: web/tests/test_monitor.py.

## What else was changed for the card (minimal edits, each marked "hercules")

* emu: dev/hercules.mjs, dev/mdafont.mjs, render-hgc.mjs; machine.mjs (`video`,
  `monitor`, `setVideo`), render.mjs, spin.mjs. Font: emu/fonts/mda9x14.bin (VileR,
  CC BY-SA 4.0, see emu/fonts/README.md).
* BIOS: bios/README.md ("Display cards").
* SDK: `ARMDOS_TEXT_VRAM` follows the mode (B0000h in mode 7), `armdos_vga_present()`
  (sdk/README.md).
* MODE: finds the adapters like MODE 4.00 (INT 10h AH=1Bh, EGA info, buffer probes);
  `MODE MONO` works on the card, colour modes give `Function not supported on this
  computer - CO80`.
* Text at B0000h in mode 7: DOSSHELL (plus a colour-to-mono attribute mapping and the
  0B0Ch cursor), ARM Commander (the same mapping), BASIC (SCREEN 1/2/13 and WIDTH 40 are
  "Illegal function call" as on an MDA; 0B0Ch cursor), POPUP, TERM, CRASH, ARMINFO,
  Keen's text output. EDIT, Zork, DEBUG, COMMAND.COM needed nothing more (EDIT picks
  Turbo Vision's mono palette itself).
* Games that need a VGA stop at once: DOOM (`DOOM requires a VGA.`), Quake and Hexen II
  (`Error: ... requires a VGA`), Duke3D (`Duke Nukem 3D requires a VGA.`), Wolf3D (its
  original `Improper video card! ...-HIDDENCARD...`), Keen Dreams (its own VW_VideoID
  now reports an MDA, so the original `Improper video card!` message), DEMO.EXE (had
  its check already).
* web: the Monitor selector (index.html, main.js, armdos.css), display.js (phosphor
  glass tint, no shadow mask, more bloom and a halo, afterglow), build-site copies
  mda9x14.bin.

## Deviations

* Graphics are always shown as 720x348 and text as 80x25 of 9x14 whatever the CRTC
  timing registers say (programs that set other geometries get the standard one).
* No dual-monitor setup (VGA + Hercules together).
* The HGC's light pen and printer port (3BCh) are not there.
