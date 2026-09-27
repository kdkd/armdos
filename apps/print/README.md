# PRINT.COM - the MS-DOS 4.00 background print spooler, for ARM-DOS

    PRINT [/D:device] [/B:size] [/U:ticks] [/M:ticks] [/S:ticks] [/Q:qsize]
          [/T] [[d:][path]filename[ ...]] [/C] [/P]

A C re-creation of DOS 4.00's PRINT, written from `CMD/PRINT/PRINT_T.ASM`
(transient) and `PRINT_R.ASM` (resident) of Microsoft's MS-DOS 4.0 source
(MIT licence, Portions (C) Microsoft Corp.) and checked against the genuine
PRINT.COM in DOSBox-X (apps/mslib/tools/dos400run.sh), including the bytes it
sends to the printer (DOSBox-X `parallel1=file`).

The first run asks `Name of list device [PRN]: ` (unless `/D:` names it),
prints `Resident part of PRINT installed` and stays resident. From then on
files print in the background while you keep working: the resident part
hooks INT 1Ch (every /S ticks, when DOS is not busy, it reads a /B-byte block
of the current file or sends the buffered characters to the list device) and
INT 28h (the same while COMMAND.COM waits at the prompt), and it answers the
PRINT multiplex interface on INT 2Fh AH=01h (install check, submit, cancel,
cancel all, queue status) that later runs of PRINT use. It calls the list
device's driver directly (PRN = LPT1 = INT 17h: the web page's dot-matrix
printer). Tabs are expanded to 8 columns, ^Z ends a file, a form feed follows
every file; `/C` prints `File X canceled by operator`, `/T` prints `All files
canceled by operator` on the printer (then CR, form feed, bell).

All messages, their order, STDOUT/STDERR and the quirks are 4.0's: the queue
listing after every run (`  X is currently being printed`, `  X is in queue`,
or `PRINT queue is empty`); DOS errors for files that cannot be opened (`File
not found - C:\X.TXT` on STDERR, the rest of the command line is still
processed); `PRINT queue is full` once; a `/C` or `/P` right after a file
name applies to it; wildcards submit every match, and a wildcard `/C` cancels
every match; install switches (/D /B /Q /S /U /M) are `Invalid switch` once
PRINT is resident (and after their first use in a run); the answer to the
device prompt overwrites "PRN" without blanking it, so `LP` becomes `LPN` and
`List output is not assigned to a device`; the print column is only reset by
a CR, so a file's first tab continues from where the previous file ended.

## Resident size

The resident part (`res.c`, `resasm.S`) lives in `.text.unlikely.pr*`
sections, which the SDK's link script puts right after `_start`, ahead of
the transient code; `resend.c` marks its end. When the transient part is
done, `r_keep()` moves the queue and the buffer down behind the resident
code (what PRINT_R.ASM's MoveTrans does the other way round), disables
interrupts during the move, and goes resident with INT 21h AH=31h. With the
defaults (/B:512, /Q:10) PRINT keeps about 9.5 KB (PSP, the 160-byte
self-relocation prologue of the .COM, 6 KB of resident code and data, queue,
buffer, a 2 KB worker stack); the 8086 original keeps about 5.5 KB. The test
checks with `nm` that the resident code calls nothing outside itself.

## Deviations

* From INT 28h (DOS idle) the original prints one character per call - on a
  PC the idle loop calls INT 28h thousands of times a second. ARM-DOS's
  idle loop sleeps (WFI) between interrupts, so PRINT sends the rest of the
  buffer per call instead (a few KB/s); nothing else changes.
* The INT 1Ch path runs before the BIOS's EOI, so no further ticks arrive
  while it works: PRINT does not send its own EOI (the ARM-PC PIC has no ISR
  register to check for nested interrupts); /M therefore does not limit an
  activation (the buffer does), and a busy printer is abandoned after a
  bounded number of polls as well as after /U ticks.
* Not hooked: INT 13h (disk busy flag), INT 15h AH=20h, INT 5 (PrtSc), INT
  19h (reboot unhook) - they have no counterpart worth having on ARM-DOS.
  INT 17h and INT 14h are hooked as in 4.0 (other programs' BIOS printer
  calls time out while PRINT prints on that port).
* The "server DOS" calls (INT 2Ah, AX=5D00h), the code-page / PRINTER.SYS
  handshake and saving the interrupted program's extended error (AX=5D0Ah is
  not in the ARM-DOS kernel) are left out.
* A run that fails after having installed the resident part (e.g. `PRINT
  A.TXT /X` as the first PRINT) restores the interrupt vectors before it
  exits; 4.0 leaves them pointing into freed memory (verified: the real
  PRINT hangs the machine at the next PRINT).
* INT 21h AX=6C00h is called with DX=0001h instead of 0101h (kernel request).

## Files

`print.c` transient part, `res.c`/`res.h`/`resasm.S`/`resend.c` resident
part, `u4lib.c`/`u4int.S` the shared DOS 4 utility helpers (`apps/dosutil`),
`tests/run.mjs` (`make print-test`), `tests/spin.c` (SPIN.EXE, a test helper
that burns timer ticks without calling DOS, to show the INT 1Ch path),
`tests/expected/*.PRN` printer bytes of the real PRINT.
