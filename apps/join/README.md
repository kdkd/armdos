# JOIN - MS-DOS 4.0 join drive to directory, compiled for ARM

`JOIN.EXE` in C:\DOS is Microsoft's MS-DOS 4.0 JOIN (`CMD/JOIN/JOIN.C` with
`INC/CDS.C DPB.C ERRTST.C SYSVAR.C`) built for the ARM-PC. It marks a drive's
CDS as spliced into an empty (or new) directory and counts the joins in the
list of lists, as DOS 4; the ARM-DOS kernel honours it since round 3.

    JOIN [d: [d:]path]    JOIN d: /D    JOIN

## Source

As SUBST's (the same INC files and the same `#ifdef ARMDOS` changes, see
apps/subst/README.md); `src/joinpars.h` is JOIN's parser header.

## Tests

`make join-test` (`tests/run.mjs`): the commands of `tests/run.bat` compared
with the real DOS 4.00 JOIN.EXE (listings, 9 error messages, errorlevels),
plus: while A: (a data diskette) is joined to C:\JN, `TYPE C:\JN\FLOPPY.TXT`
shows the diskette's file and `DIR A:` says `Invalid drive specification`.
13 checks.

## Licence

Portions (C) Microsoft Corp., MIT License (`src/LICENSE`).
