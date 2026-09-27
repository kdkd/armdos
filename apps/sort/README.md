# SORT.EXE — the MS-DOS 4.00 SORT filter for ARM-DOS

    SORT [/R] [/+n]          (reads standard input: SORT < FILE, DIR | SORT)

A C port of `CMD/SORT/SORT.ASM` from Microsoft's MS-DOS 4.0 source (MIT
licence), checked against the real SORT.EXE in DOSBox-X. Goes to
`C:\DOS\SORT.EXE`.

The port keeps the original's data layout: it asks DOS for 64 KB (INT 21h
AH=48h, or as much as there is), reads all of standard input into it and
sorts in place with 16-bit offsets (a 256-byte blank record, then records
with length words, insertion by moving memory), so its behaviour is the
original's, quirks included:
* records end at CR LF; a lone CR or LF stays inside the line;
* `/+n` (1-65535, also `/+:n`) starts the key at column n; lines shorter
  than that compare as 256 blanks; equal keys keep their input order, and
  with `/R` (the original patches `JAE` into `JB`) they come out reversed;
* characters compare through the country collating table (INT 21h
  AX=6506h) — case-insensitive for the US;
* ^Z ends the input; the output always ends with CR LF; a last line
  without CR LF picks up the one byte that followed the data in memory
  (the real SORT does the same — its value depends on the memory);
* 64 KB or more of input: `SORT: Insufficient memory` (stderr), nothing
  written; `SORT: Invalid switch`, `SORT: Too many parameters`, `SORT:
  Parameter value not in allowed range`, `SORT: Parameter format not
  correct`, `SORT: Required parameter missing`; exit code 1 on errors.

Tests: `make sort-test` (apps/sort/tests/run.mjs: 25 checks, outputs byte-
compared with the real SORT's for 15 inputs, plus the messages).

Deviations / notes: the collating table is whatever the kernel returns
(INT 21h AX=6506h: DOS 4's built-in US/437 table). Input close to 64 KB
makes the original read a word across the 64 KB segment end (a fault on a
386; DOSBox-X aborts it); the port wraps the offset like an 8086.
