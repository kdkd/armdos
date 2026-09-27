# MEM - MS-DOS 4.0 memory report, compiled for ARM

`MEM.EXE` in C:\DOS is Microsoft's MS-DOS 4.0 MEM (`CMD/MEM/MEM.C`) built for
the ARM-PC. It shows ARM-DOS's real memory map: IO.SYS at 0700h, the
ARMDOS.SYS kernel, the DOS 4 memory control blocks with owner names, the
CONFIG.SYS sub-blocks SYSINIT builds (FILES=, FCBS=, BUFFERS=, LASTDRIVE=,
DEVICE= with the driver's name), programs and environments, and extended
memory (INT 15h AH=88h / what HIMEM.SYS left).

    MEM [/PROGRAM | /DEBUG]

## Source

`src/mem.c`, `msgdef.h`, `parse.h`, `MEM.SKL` are Microsoft's (LF line
endings). Every change is in `#ifdef ARMDOS`:

* the 69 `FP_SEG(p) = ...; FP_OFF(p) = ...;` assignments rewritten as flat
  pointers (`SET_FP(p, seg, off)` = seg * 16 + off, ARCH.md 5), pointers from
  INT 21h in BX/SI taken as they are;
* the DOS structures (list of lists, device header, MCB, sublist) with 16-bit
  fields, flat far pointers and the ARM-DOS list-of-lists layout (ARCH.md 16);
* the device chain ends at -1; MEM's own `sublist[4]` overrun given room;
* errorlevel 102 at the end, as the real MEM.EXE leaves it (checked in DOS
  4.00: MEM, MEM /PROGRAM and MEM /DEBUG all give 102).

Branding: the kernel's blocks are named `ARMDOS` where DOS 4 says `MSDOS`
(the file is ARMDOS.SYS); `IO` stays.

## Tests

`make mem-test` (`tests/run.mjs`): MEM, MEM /PROGRAM, MEM /DEBUG on ARM-DOS,
with plain CONFIG.SYS and with HIMEM.SYS/FCBS/LASTDRIVE=Z - the fixed text
byte for byte against the real MEM output (`tests/expected/`), the IO.SYS
device list against the real one (the same twelve drivers), every map line's
format, the map against the machine (contiguous MCB chain, sub-blocks filling
IO System Data, free memory to 640 KB), the summary numbers, the parse errors
on the screen against the real ones (`tests/errors.bat` in DOS 4.00), exit
codes. 57 checks.

As in the real MEM, `/DEBUG` lists no drivers inside a DEVICE= sub-block (the
original's loop compares the low byte of two paragraph addresses; checked in
DOS 4.00 with ANSI.SYS).

## Licence

Portions (C) Microsoft Corp., MIT License (`src/LICENSE`).
