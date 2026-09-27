# GEM for ARM-DOS

Digital Research's **GEM/3** (the 1988 PC release: AES 3.0, VDI, the GEM/3
Desktop and the Calculator/Clock/Print Spooler desk accessory) compiled natively for
the ARM-PC, with the GEM Programmer's Toolkit sample application **DEMO** (a drawing
program). It is built from DRI's own C sources, which Caldera released under the GNU
GPL in 1999. The 8086 assembler parts and the screen driver were rewritten in C and ARM
assembler.

```
C:\>GEM            the Desktop in 640x200 on the CGA mode (or 720x348 with a Hercules card)
C:\>GEM /V         in colour: 640x480, 16 colours (the ARM-PC's VGA mode 62h)
C:\>GEM /C         force the CGA mode          C:\>GEM /H   force the Hercules card
```

`GEM.BAT` (in C:\DOS, on the PATH) loads MOUSE.COM if it is not resident yet (GEM/3's
screen drivers had their own mouse code; this VDI uses INT 33h). It then changes to
`C:\GEMAPPS\GEMSYS` and starts `GEMVDI.EXE`. GEMVDI installs the INT EFh entry and starts `GEM.EXE` (the AES). The AES
loads the desk accessories (`*.ACC`) and then runs `DESKTOP.APP`. The layout is GEM's
own (GEMAPPS layout):

```
C:\GEMAPPS\GEMSYS\  GEMVDI.EXE  GEM.EXE  GEM.RSC  DESKTOP.APP  DESKTOP.RSC
                    DESKLO.ICN  DESKHI.ICN  DESKTOP.INF  CALCLOCK.ACC
C:\GEMAPPS\         DEMO.APP  DEMO.RSC
C:\DOS\GEM.BAT
```

## What works

Every item below is checked by `make gem-test`, which boots the machine headless and
drives it with the emulated mouse.

* **The Desktop**:
  * Drive icons (FLOPPY DISK, HARD DISK) and folder and file windows, as icons or as text
    (name, size, date, time).
  * The DESKTOP, File, Options and Arrange menus, including Desktop info, Set preferences
    and Show as text/icons.
  * Double-clicking a drive or a folder opens it.
  * Dragging an icon onto a folder copies the file, with the COPY FOLDERS / ITEMS dialog.
  * Exit to DOS returns to the DOS prompt.
* **Formatting a floppy.** Select FLOPPY DISK and choose File > Format.... The Desktop
  runs FORMAT (found on the PATH) behind the graphics screen, as GEM/3 did with
  DESKOSIF's hooks (`deskarm.c`):
  * Its output is swallowed (INT 10h and the console functions of INT 21h).
  * Its questions are answered: Enter for "press ENTER" and the volume label, N for
    "Format another (Y/N)?". ARM-DOS's FORMAT asks for a label, so the answer depends on
    the question, where DOS 3's FORMAT got a fixed CR, N, CR.
  * A 1.44 MB disk takes about 28 s of emulated time.
* **Launching DOS programs.** Double-click an .EXE/.COM/.BAT, answer the Open Application
  dialog, and GEM steps aside. The screen goes to text mode and the program runs; the
  test uses EDIT. When the program ends, GEM comes back with its windows as they were.
* **GEM applications.** DEMO.APP, DRI's Toolkit sample, is a drawing program with a
  window, sliders, menus, dialogs, a file selector and pictures saved as `.DOO` files.
  * Double-clicking it in `C:\GEMAPPS` makes the Desktop hand over. The AES runs DEMO
    in graphics mode, and brings the Desktop back when DEMO quits.
  * The test drags the mouse to draw, and opens the Pen / Eraser dialog. That dialog has
    PROGDEF objects, which the AES calls back into DEMO to draw, and a scrolling colour
    selector; the test picks the broad pen and a colour.
  * It then saves the picture with Save As, quits, starts DEMO again and loads the
    picture back through the AES's ITEM SELECTOR.
  * In colour (`GEM /V`) the pen colours are real colours.
* **Desk accessories.** CALCLOCK.ACC adds the Calculator, the Clock and the Print Spooler
  to the DESKTOP menu. The Calculator does decimal arithmetic (12x3 = 36, 1/8 = .125,
  7-10 = -3, ...). The Clock shows the time and date.
* **Mouse** through INT 33h (MOUSE.COM), with the pointer drawn by the VDI. The
  keyboard mouse of the DRI drivers is also there: the cursor keys move the pointer,
  Home clicks, End holds the button, and Ctrl+Right Shift switches it on and off.
* **Critical errors.** Opening A: with no disk shows GEM's alert "Drive A: is not
  responding...". See the deviations below for how this differs from the PC version.
* **Three screens**:
  * CGA 640x200 mono (mode 6).
  * Hercules 720x348, with the 8x14 system font.
  * VGA 640x480 in 16 colours, where the desktop pattern and colours appear.
  * Fonts: the GEM/3 system fonts from the screen-driver release (6x6, 8x8, 8x14,
    8x16).
  * The text effects GEM/3's drivers had: bold, light, italic, underline, and scaling.
* **Idle costs nothing.** When no GEM process can run, the dispatcher waits for an
  interrupt (CP15 WFI) instead of spinning. An idle desktop uses about 0.05% of the
  emulated CPU.

Screenshots from the last test run are in `build/gem-test/*.png`.

### Speed (emulated ARM at 100 MHz; `make gem-test` prints these)

| redraw | CGA 640x200 | VGA 640x480x16 |
|---|---|---|
| Arrange > Show as text (two windows redrawn) | 26 ms, 2.6M instructions | 92 ms, 9.3M instructions |
| Arrange > Show as icons | 21 ms, 2.1M instructions | |
| Desktop info box drawn and closed | 8 ms, 0.8M instructions | |
| `GEM` typed at the prompt until the menu bar is up | 1.2 s (typing included) | |

The times are *busy* emulated time: total emulated time minus the time spent halted.
The host runs the emulator at roughly 8x real time, so the host's own time is about an
eighth of these.

## What was ported, what was rewritten

| part | from | how |
|---|---|---|
| AES: `aes/gem*.c`, `optimize.c` (29 files, ~14,000 lines) | GEM/3 AES source (Caldera) | **Ported.** Converted from K&R to ANSI C by `tools/knr2ansi.py` and `tools/dropdecl.py`, then fixed by hand for 32-bit flat pointers (see below). |
| AES: `gemarm.c`, `gemasm.S` | GEMSTART/GEMGMAIN/GEMASM/GEMDOSIF/GSX2/LARGE/OPTIMOPT `.A86` | **Rewritten.** Covers: the dispatcher (a coroutine switch of r4-r11/lr), the INT EFh entry, INT 24h, the timer and mouse hooks, the DOS interface, EXEC, the accessory loader (AR1 images), and the idle WFI. |
| Desktop: `desk/*.c` (~11,000 lines) | GEM/3 Desktop source (Caldera) | **Ported** the same way. `deskarm.c` replaces DESKSTAR/DESKOSIF/LONGASM/GSX2 `.A86`. |
| Accessory: `acc/ccsmain.c calc.c clok.c spol.c calcif.c` | CalClock, in the GEM/3 Desktop source (Caldera) | **Ported.** `accarm.c` replaces ACCSTART/CALCASM `.A86`. `fld.c` is **new**: the decimal arithmetic that was the binary-only CBARITH.OBJ. |
| VDI: `vdi/monobj.c monout.c opttext.c jmptbl.c isin.c dummy.c` | GEM/3 screen-driver C sources (Caldera) | **Ported**: attributes, text layout, fill and line setup, the jump table. |
| VDI: `vdi/draw.c raster.c text.c mouse.c dev.c` | IBMBLMPC, HERCSPPC, IBMMDVSP, RASTOP, MONMMRE1 `.A86` and DEVDATA | **Rewritten in C.** Covers the spans, lines, fills, blits, text blitting with the effects, the mouse cursor, the modes and the escapes. They work for 1 bpp (CGA, Hercules) and 8 bpp (VGA 62h) screens. |
| VDI: `vdi/gdos.c`, `tramp.S` | GEM/3 GDOS (ENTRY/DRIVER.A86) | **New code** in GEM's calling convention. The driver is linked in, not loaded from an SDxxx.SYS file. |
| `vdi/fonts.c`, `vdi/patdata.c` | the `.FUL` system fonts and the pattern tables of the screen-driver release | **Generated** by `tools/fnt2c.mjs` and `tools/pat2c.mjs`. |
| `common/gemrt.c`, `include/` | LONGASM/OPTIMOPT and friends | **New**: the shared runtime and the DOS pseudo-registers. |
| resources, icons | `GEM.RSC`, `DESKTOP.RSC`, `DESKLO/DESKHI.ICN` from the Caldera sources | used as they are |
| `dist/DESKTOP.INF`, `dist/GEM.BAT` | the GEM 3.13 distribution (OpenGEM SDK) | copied; `GEM.BAT` adapted: upper case, `MOUSE >NUL` first, no Concurrent DOS lines |
| DEMO: `demo/demo.c`, `vdibind.c`, `obdefs.h`, `treeaddr.h`, `demo.h`, `demo.rsc` | the GEM Programmer's Toolkit 3.x samples (DEMO.C 2.1, VDIBIND.C, in the OpenGEM SDK) | **Ported** with the same tools. The fixes: a LONG passed to `wind_set` as two words, a packed `obdefs.h`, the C library's string functions, and the VDI's 32-bit CONTRL addresses stored as two words. |
| DEMO: `demo/gembind.c` | the GEM/3 Desktop's GEMBIND.C (already ported), plus `graf_growbox`/`graf_shrinkbox` | reused |
| DEMO: `demo/demoarm.c` | PROSTART, GEMASM, VDIASM, FARDRAW, LONGASM, DOSASM, DOSBIND | **Rewritten**. `dos_read` and `dos_write` take LONG counts: a 640x480 picture is 150 KB. |

`orig/` keeps pristine copies of every source file used, with LF line ends, so that
`diff orig/aes/gemwmlib.c aes/gemwmlib.c` shows the port.

### The calling convention

* **VDI**: `svc #0xEF` with r2 (CX) = 0473h and r3 (DX) pointing to the parameter
  block `{contrl, intin, ptsin, intout, ptsout}` (five flat pointers).
* **AES**: `svc #0xEF` with r2 = 200 and r1 (BX) pointing to the AES parameter block.
  These are the 8086 registers of ARCH.md §5.
* **Returning.** The INT EFh handler pushes the caller's return address on the caller's
  stack and continues at a trampoline in the caller's mode. So a GEM call runs like a
  function call, on the caller's own stack for the VDI, and on the process's supervisor
  stack for the AES, as it did on the PC.
* **VDI vectors are C function pointers**, exchanged through CONTRL[7..8] (the old one
  comes back in CONTRL[9..10]): `butv(WORD)`, `motv(WORD*, WORD*)`, `curv(WORD, WORD)`,
  `timv(void)`.

### The 8086 habits that needed fixing

* **`LONG` values in GEM structures are only 2-byte aligned.**
  * `LLGET`/`LLSET` use two halfword accesses.
  * `OBJECT`, `TEDINFO`, `ICONBLK`, `BITBLK`, `USERBLK`, `PARMBLK`, `FCB`, `FNODE` and the
    Desktop's `SFCB` are packed to 2-byte alignment (`GEM_PACKED`).
  * `FNODE` gained a pad byte so that its `WORD`s stay even.
* **A `LONG` passed as two `WORD` arguments** (and the reverse): `forkq`, `wind_set`,
  `evnt_timer`, `evnt_multi`, `w_strchg`. The fork routines now get their two words
  split.
* **The address of a scalar parameter used as an array** (the 8086 stack layout made
  that work): `w_adjust`, `gsx_cline`, `gr_stilldn`, `gr_xor`. Each now builds a real
  array.
* **`sizeof(BYTE *)` taken as 2** in the icon-file code.
* **`LLOWD(tree)` used as a pointer** in the accessory.
* **Undefined behaviour** that 8086 compilers happened to get right, e.g. `a[i++] = b[i]`.
* **Plain `char` is unsigned.** `-fsigned-char` breaks the loops in `sh_rdinf`.
* **GEM's string routines take the source first**; they are renamed (`strcpy` becomes
  `gem_strcpy`, ...) so that newlib's do not get in the way.

### Memory

| resident under a DOS program | size |
|---|---|
| GEMVDI.EXE | 116 KB |
| GEM.EXE | 132 KB |
| the accessory | 44 KB |

A DOS program started from the Desktop gets about **260 KB** of conventional memory.
The same machine without GEM leaves 571 KB. All of the extended memory stays free:
GEM's programs never move their heap into XMS (`_armdos_xms_kb = ~0`), so EDIT and
friends can use it.

## The colour mode: VGA mode 62h (emulator + BIOS addition)

The ARM-PC's VGA had no mode for 16 colours at 640x480 (there are no planar modes), so
there is now one. Everything is documented in ARCH.md §6, emu/README.md and
bios/README.md.

* **Mode 62h** is 640x480 in 256 colours, one byte per pixel, **linear from A0000h to
  EAFFFh**.
* When the mode register (3E0h) says 62h, the card also decodes that part of the adapter
  hole (C0000h-EAFFFh becomes RAM on the card). Any other mode, or a reset, gives the
  hole back.
  * `emu/machine.mjs setLinearFb` clears the pages' write protection.
  * `emu/render.mjs` shows the frame through the DAC.
  * Tests are in `emu/tests/machine/vga62.mjs`, which is in `run-all`.
* **BIOS**: INT 10h AH=00h AL=62h sets the mode.
  * AH=0Ch/0Dh (pixels), 0Eh/09h/0Ah (8x16 text in 80x30 cells) and 06h/07h (scrolling)
    work in it.
  * AH=1Bh reports 256 colours and 480 lines.
  * `tests/modetest.c` checks all of this.
* **The VDI** (`GEM /V`) programs the first 16 DAC entries with GEM's colours, in the
  pixel order of DRI's EGA/VGA drivers: white = 0, black = 15, red = 1, ... With that
  order, XOR swaps ink and paper as it does on the mono screens.
  * Like the DRI 16-colour drivers, it reports **4 planes**, so the AES and the
    applications see 16 colours.
  * The screen itself is one byte per pixel.
  * A 4-plane memory form in *device* format holds packed pixels, two to a byte (high
    nibble left). That is the same size as the standard format, so `vr_trnfm` may work
    in place.

## Deviations from GEM/3 on a PC

* **Critical errors (INT 24h).**
  * On the PC, the AES put up its alert from inside the INT 24h handler and ran the
    dispatcher while DOS was in the middle of the call.
  * On ARM-DOS the handler runs in SVC mode inside the kernel's INT 21h, where switching
    GEM processes is not safe. So the call fails at once (AL = 3), and the same alert
    ("Drive A: is not responding...", with Cancel/Retry) appears at the program's next
    AES call.
  * **Retry cannot retry** any more: both buttons leave the operation failed.
  * The program's own reaction to the failure (the Desktop would say "the Directory name
    ... exceeds the maximum ...") is answered without being shown. The PC's pseudo error
    codes kept programs quiet in the same way.
  * After Cancel, the Desktop opens an empty A:\ window.
* **No GDOS.** There are no printer or plotter drivers and no disk fonts, only the screen
  and its system fonts.
* **The outline text effect** is ignored, as GEM/3's CGA driver did (it only honours
  bits 0-3).
* **GEM Paint and GEM Write are not included.** Their sources were never released: the
  OpenGEM SDK has only the 8086 binaries, which cannot run here.
  * The GEM applications ported instead are the Toolkit's DEMO (a drawing program) and
    the Calculator/Clock accessory.
  * Any 8086 GEM application (.APP) needs porting in the same way. The bindings to
    start from are `demo/gembind.c`, `demo/vdibind.c` and `demo/demoarm.c`.
* **16 colours in mode 62h.** The 256-colour DAC is there, but GEM's palette is its 16
  pens. A program that allocates a device-format buffer as `w*h*planes/8` bytes gets the
  right size (see packed pixels above).

## Tests

`make gem-test` (part of `make test`) runs `tests/run.mjs`: 81 checks, about 15 s of host time. Scenarios (`--only`):

| scenario | what it checks |
|---|---|
| `vdi` | VDITEST under GEMVDI: frame, filled rectangle, the 24 patterns and 12 hatches, text in the 8x8 and 6x6 fonts with effects, back to text mode. |
| `boot` | GEM from the prompt: mode 6, menu bar, the two windows, the drive icons. |
| `open` | Double-click HARD DISK, then the DOS folder. |
| `drag` | Drag GEM.BAT onto GEMSYS; confirm; the file is on the disk. |
| `menu` | File and DESKTOP menus; Desktop info. |
| `launch` | EDIT from the Desktop: text mode, back to GEM. XMSTEST under GEM: the XMS is free. |
| `perf` | Redraw times: Show as text / icons, Desktop info. |
| `acc` | Calculator sums, the Clock. |
| `exit` | Exit to DOS. |
| `gemapp` | DEMO.APP from the Desktop: its window and menus; the pen dialog with the application-drawn brushes; a broad line drawn with the mouse; Save As; Quit back to the Desktop; start again and Load through the ITEM SELECTOR. Then the same in colour, where the stroke is red. |
| `floppy` | Booted without MOUSE in AUTOEXEC (GEM.BAT loads it). Open A: with no disk: the alert once. Options menu, Set preferences. |
| `format` | File > Format... on a blank 1.44 MB floppy: FORMAT runs unseen and answered, the disk opens. |
| `vga` | MODETEST: the BIOS mode 62h. VDITEST `/V`: colours and DAC. `GEM /V`: the desktop in colour, the mouse in the lower half, a redraw time, exit (hole closed). |
| `hgc` | GEM on the Hercules card. |

`tests/lib.mjs` builds a hard disk image from `hd.json` and has these helpers:

* `findText` reads text off the graphics screen by matching the glyphs of GEM's own
  `.FUL` fonts.
* `mouse` moves the PS/2 mouse in screen pixels.

Emulator side: `node emu/tests/machine/vga62.mjs`.

## Building and debugging

`app.mk` builds `GEMVDI.EXE`, `GEM.EXE`, `DESKTOP.EXE` (installed as DESKTOP.APP),
`CALCLOCK.EXE` (installed as CALCLOCK.ACC) and `GEMDEMO.EXE` (installed as
C:\GEMAPPS\DEMO.APP; the name keeps it apart from apps/demo's DEMO.EXE). It also builds the test programs in
`build/gem-test/`.

`make GEM_DEBUG=1` (after touching the sources) builds with `-DGEMDEBUG -DGEMTRACE`,
which writes a trace of AES opcodes, DOS calls and mouse events to the debug port E9h
(`pc.debug` in the testkit).

Tools (`tools/`):

* `knr2ansi.py` / `dropdecl.py`: the K&R conversion.
* `paramaddr.py`: finds `&param` uses.
* `rscdump.mjs`: lists a resource file.
* `fnt2c.mjs` / `pat2c.mjs`: generate `fonts.c` and `patdata.c`.

## Licences and provenance

* **GEM/3 AES, VDI, screen drivers, Desktop and CalClock sources**: Copyright
  1985-1988 Digital Research Inc.; released by Caldera Thin Clients, Inc. in 1999 under
  the **GNU General Public License v2**. The licence text is in `aes/license.txt` and
  `desk/license.txt`.
  * Obtained from the OpenGEM repository (github.com/shanecoughlan/OpenGEM, commit
    ac06b1a, 2017), "OpenGEM SDK Release 3": "GEM 3 AES source code.zip", "GEM 3 VDI
    source code.zip", "GEM 3 Screen Drivers source code.zip", "GEM 3 Desktop source
    code.zip" (with CalClock).
* `dist/DESKTOP.INF` and `dist/GEM.BAT` come from "GEM complete distribution 3.13 (with
  source code).zip" in the same SDK (GPL, as above).
* **DEMO** (DEMO.C 2.1 by Tom Rolander and Tim Oren, DEMO.RSC, VDIBIND.C) comes from
  the GEM Programmer's Toolkit.
  * The copy used is the SAMPLE directory of John Elliott's "GEM bindings for DJGPP"
    (OpenGEM SDK, `PROGRAMMING BINDINGS AND COMPILERS/CONTRIB/GEM`).
  * DRI's notice in the file reads: "a non-copyrighted work which can be freely used ...
    you are requested to acknowledge Digital Research, Inc. as the originator of this
    code". Acknowledged here, and in DEMO's own About box.
  * The pristine files are in `orig/demo/`.
* John Elliott's FreeGEM / Pacific C versions were used
  only as a reference.
* **The new code** (gemarm.c, gemasm.S, deskarm.c, accarm.c, fld.c, gdos.c, the rewritten
  VDI files, the tests and tools) is Copyright 2026 Europa Micro Systems. It is part of
  this GPL v2 program.
* GEM is a trademark of its owners. This port shows DRI's own copyright notices in the
  Desktop info box, as the GPL release does.
