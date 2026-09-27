# FIND.EXE — the MS-DOS 4.00 FIND filter for ARM-DOS

    FIND [/V] [/C] [/N] "string" [[d:][path]filename ...]

A C re-creation of `CMD/FIND/FIND.ASM` from Microsoft's MS-DOS 4.0 source
(MIT licence; behaviour taken from the source and checked against the real
FIND.EXE in DOSBox-X). Goes to `C:\DOS\FIND.EXE`.

What it does, as 4.00 does:
* switches anywhere on the line (a first pass collects them — so a quoted
  string containing ` /x` gives `FIND: Invalid switch`, as the original);
  4.0 has only `/V /C /N` (no `/I`);
* each file: CR LF `---------- NAME` (name upper-cased by the parser), then
  the matching lines unchanged, `[n]` before them with `/N`, `: count` after
  the heading with `/C`; standard input (a pipe or `<`) gets no heading;
* case-sensitive match; empty lines never match; `""` inside the quotes is a
  quote; the file is read in 4 KB blocks, ^Z ends it, a last line without LF
  gets CR LF, a longer line is split at 4 KB;
* `File not found - NAME` (stderr) and FIND goes on with the next file;
  `FIND: Required parameter missing`, `FIND: Invalid switch`, `FIND:
  Parameter format not correct` (the `FIND: ` prefix only until a file name
  has been seen); exit code 0, or 2 after a syntax error;
* the counters are cleared by their low byte only between files, as the
  original (`/C` on two 300-line files prints 300 and 556).

Tests: `make find-test` (apps/find/tests/run.mjs, 30 checks against the real
FIND's output bytes and screen messages).

Deviations: none known. No DBCS lead-byte handling and no code-page
extended attributes (4.0's CPSW support), which ARM-DOS does not have.
