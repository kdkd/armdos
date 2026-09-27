# SUBST - MS-DOS 4.0 substitute drive, compiled for ARM

`SUBST.EXE` in C:\DOS is Microsoft's MS-DOS 4.0 SUBST (`CMD/SUBST/SUBST.C`
with `INC/CDS.C DPB.C ERRTST.C SYSVAR.C`) built for the ARM-PC. It edits the
kernel's current directory structures (the CDS array of INT 21h AH=52h) the
way DOS 4 does; the ARM-DOS kernel honours the SUBST flag since round 3.

    SUBST [d: [d:]path]    SUBST d: /D    SUBST

## Source

`src/` holds Microsoft's files (LF line endings; changes in `#ifdef ARMDOS`):
the CDS, list-of-lists, DPB and DTA structures with 16-bit fields and flat
pointers (the ARM-DOS layouts, ARCH.md 16), the list of lists taken from BX,
`(unsigned short)&rslt1` pointer compares widened, the 11-byte sublist packed,
`strcat(source, argv[argc])` and `strcpy(fix_es_reg, NULL)` (MS C read "" at
address 0) made harmless, the DBCS table scan ending at 0,0 (the original
compared with the address of "00"). `com_substr` (binary-only COMSUBS.LIB)
is in apps/mslib. Built with `-fcommon` (MS C's tentative definitions).

## Tests

`make subst-test` (`tests/run.mjs`): the commands of `tests/run.bat` compared
with the real DOS 4.00 SUBST.EXE (listings byte for byte, 13 error messages on
the screen, errorlevels), and the safety case: with E: substituted for
C:\SUB, `DEL E:*.TXT` (COMMAND.COM) deletes C:\SUB\A.TXT and B.TXT and
leaves C:\A.TXT, C:\B.TXT and C:\SUB\C.DAT - as on the real DOS. 12 checks.

## Licence

Portions (C) Microsoft Corp., MIT License (`src/LICENSE`).
