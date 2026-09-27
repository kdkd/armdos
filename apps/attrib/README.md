# ATTRIB - MS-DOS 4.0 file attributes, compiled for ARM

`ATTRIB.EXE` in C:\DOS is Microsoft's MS-DOS 4.0 ATTRIB (`CMD/ATTRIB`) built
for the ARM-PC.

    ATTRIB [+R|-R] [+A|-A] [d:][path]filename [/S]
    ATTRIB [d:][path]filename FILESIZE|DATE|TIME|*|name

## Source

`src/attrib.c`, `attrib.h`, `msgret.h`, `parse.h`, `ATTRIB.SKL` are
Microsoft's (LF line endings; changes in `#ifdef ARMDOS`). `ATTRIBA.ASM`
(the start-up that replaces MS C's, PSP access, the INT 24h hook; kept in
`orig/`) is `src/attrib_arm.c`. Main changes:

* `WORD` is 32 bits in this program: MS C's WORD held near pointers
  (`(WORD)&pos1_buff`), 16-bit fields of DOS structures are `DOSWORD`;
* the eight message sublists that MS C laid out one after another (their
  sizes 72, 60, ... are strides to the `%2` sublist) are one array;
* the parser's value list is packed and its string pointers filled at start-up
  (the loader cannot relocate unaligned words);
* the extended-attribute calls take flat pointers; a DOS without them (error
  1) has no extended attributes, as DOS 4.00 on FAT;
* `strcpy(fix_es_reg, NUL)` (an MS C trick to restore ES) made harmless;
* errorlevel 7 after a normal run, as the real ATTRIB.EXE (checked).

## Tests

`make attrib-test` (`tests/run.mjs`): the commands of `tests/run.bat` - show,
set and clear R/A, `/S`, relative paths, `.` and `..`, other drive syntax,
FILESIZE/DATE/TIME (with the real one's NUL padding and 2-digit year cap),
`*`, unknown names, 16 error cases on the screen - compared with the real
DOS 4.00 ATTRIB.EXE's output (`tests/expected/`), plus errorlevels and the
attribute bits left on the disk. 32 checks.

Deviations: Ctrl-C ends ATTRIB through DOS's abort (the original exited with
errorlevel 3 from its INT 23h handler).

## Licence

Portions (C) Microsoft Corp., MIT License (`src/LICENSE`).
