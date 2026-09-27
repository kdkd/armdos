# FC - MS-DOS 4.0 file compare, compiled for ARM

`FC.EXE` in C:\DOS is Microsoft's own MS-DOS 4.0 FC (`CMD/FC`, the old MS
"tools" file compare: lower-case messages, no "Comparing files" banner), built
from the MIT-licensed source for the ARM-PC.

    fc [/a] [/b] [/c] [/l] [/lbNN] [/w] [/t] [/n] [/NNNN] file1 file2

## Source

`src/` holds Microsoft's files (line endings converted to LF, otherwise
unchanged except where marked `#ifdef ARMDOS`): `fc.c error.c fgetl.c ntoi.c
update.c fc.h tools.h ttypes.h internat.h` and `kstring.c` (`INC/KSTRING.C`).
The seven 8086 helpers (`orig/*.ASM`: messages, Move/Fill, strbscan/strbskip,
max/min, IToupper/get_lbtbl/test_ecs, the XTAB tables) are `src/fcasm.c`.
ARM-DOS changes: the country-info buffer spelled with 16-bit fields, `errno`
from newlib, `"..., )"` prototypes, a cast lvalue, and FC returning
errorlevel 1 after differences in line mode (MS C left AX = 1 there; checked).

## Tests

`make fc-test` (`tests/run.mjs`): the commands of `tests/run.bat` - text and
binary compares, `/A /B /C /L /LB2 /N /T /W /3`, tabs, resync failure, binary
extensions, missing files, a directory, usage and switch errors - on ARM-DOS,
compared byte for byte with what the real MS-DOS 4.00 FC.EXE wrote
(`tests/expected/`, DOSBox-X, `apps/mslib/tools/dos400run.sh`), and the
errorlevels. 39 checks.

Real quirks kept: the resync message ends in a literal `\n` (MASM strings have
no C escapes), `/T` keeps CR (lines end CR CR LF), "Permission denied" for a
directory.

## Licence

Portions (C) Microsoft Corp., MIT License (`src/LICENSE`).
