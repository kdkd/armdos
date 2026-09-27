# REPLACE.EXE — MS-DOS 4.00 REPLACE for ARM-DOS

`REPLACE [d:][path]filename [d:][path] [/A] [/P] [/R] [/S] [/U] [/W]` — re-created
in C from `CMD/REPLACE/REPLACE.C` of the MS-DOS 4.0 source (MIT licence, Portions
(C) Microsoft Corp.) and checked against the real REPLACE.EXE 4.00 in DOSBox-X.

As 4.00:
* the command line is argv joined with single blanks, parsed by SysParse (one
  required, one optional file spec, each switch at most once): `Source path
  required`, `Invalid switch - /X`, `Too many parameters - X`, `Invalid parameter
  combination` (/A with /S or /U); errorlevel 11
* the source is made fully qualified; no match: `No files found - C:\R1\*.XYZ`
  (errorlevel 2); the target defaults to the current directory, `..` works
* replace: every file of the target directory (with /S its subdirectories too)
  that is among the source files is copied over (with /U only when the source is
  newer); /A: source files missing in the target are copied
* `Replacing C:\R2\A.TXT` / `Adding ...`; /P asks `Replace x? (Y/N)` /
  `Add x? (Y/N)` on STDERR without echo; /W waits after `Press any key to continue . . .`
* a read-only target is only replaced with /R and keeps its attributes (+archive);
  copies get the source's date and time; a 64 KB buffer from DOS (AH=48h)
* `File cannot be copied onto itself - x`, then `n file(s) replaced|added` or
  `No files replaced|added`; errors stop the walk: `Path not found - x`,
  `Access denied  - x` (DOS's text has a trailing blank), `Invalid drive
  specification - Q:\`; errorlevel = the error

Tests: `make replace-test` (tests/run.mjs): 9 redirected-stdout comparisons byte for
byte with the real REPLACE's output and 16 screens (29 checks).

Deviations: no extended attributes, no APPEND /X suspension, no own INT 23h/24h
handlers; at most 256 source files in one list (4.00 loops in batches of 255).
REPLACE.C opens with INT 21h AX=6C00h, DX=0101h; this version passes DX=0001h
(the same meaning: DOS 4 ignores DH).
