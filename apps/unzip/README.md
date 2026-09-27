# UNZIP.EXE - ZIP extract utility for ARM-DOS

`C:\DOS\UNZIP.EXE` (38 KB). Extracts, tests and lists ZIP archives in the
style of the PKUNZIP of 1990-93, under ARM-DOS's own name. GETKEEN.BAT
(apps/keen) uses it to unpack the Keen Dreams shareware zip downloaded from
The ARM Pit BBS.

```
UNZIP [options] zipfile[.ZIP] [d:\outdir\] [file...]

  -o  overwrite existing files without asking
  -n  extract only files newer than the ones on disk
  -d  restore the directory structure stored in the zip
  -v  view: list the files in the zip
  -t  test the integrity of the zip
  -c  extract to the console (screen)
```

* Options start with `-` or `/`, any case, and combine (`-od`).
* `.ZIP` is added to the zip name if the name as given does not exist.
* The argument after the zip name is the output directory if it ends in `\`,
  `/` or `:`, or names an existing directory; it is created if needed.
* File specs select members by name, case-insensitively, with `*` and `?`
  (`*.*` = all). A spec without a path matches the member's base name.
* Without `-d` the member's path is dropped (as PKUNZIP does); with it the
  directories are created. Names are made valid DOS 8.3 names (upper case,
  8+3 characters, invalid characters become `_`).
* The file date and time and the read-only/hidden/system attributes are set
  from the zip.
* An existing file without `-o`/`-n` asks
  `WARNING: FILE.EXT already exists.  Overwrite (y/n/a/r)?`:
  y yes, n no, a yes to all, r asks for a new name.

Output (through DOS handles, so `>` and pipes work; errors on stderr):

```
UNZIP  Extract Utility  Version 1.0  ARM-DOS
(C) 1989 Europa Micro Systems.  All Rights Reserved.

Searching: KEENDRMS.ZIP
   Inflating: START.EXE
  Extracting: HELP.BAT
```

`-v` prints PKUNZIP's columns (Length, Method `Stored`/`DeflatN|X|F|S`/...,
Size, Ratio, Date, Time, CRC-32, Attr, Name) and a totals line; `-t` prints
`Testing: FILE.EXT  OK` per member.

## Errorlevels (as PKUNZIP)

| | |
|---|---|
| 0 | no error |
| 1 | warning: a member was skipped (unsupported method, encrypted, can't create) |
| 3 | bad compressed data or CRC error in a member (the others are still extracted) |
| 9 | zip file not found |
| 10 | invalid option |
| 11 | no matching files |
| 50 | disk full |
| 51 | unexpected end of the zip file (truncated download) |

## Implementation

`unzip.c`, original code for ARM-DOS: a streaming inflater written from RFC
1951 (Huffman decoding with a 9-bit first-level lookup table and a canonical
slow path, 32 KB window which is also the output buffer), CRC-32 on every
member, the central directory (found from the end-of-central-directory
record; when there is none, the local headers are walked). Methods: 0
stored and 8 deflated. The older methods (shrink, reduce, implode) and
encrypted members are reported and skipped with errorlevel 1.
Extracting KEENDRMS.ZIP (356 KB compressed, 444 KB, 24 files) takes about
2 s of emulated time.

Licence: original work for ARM-DOS, same terms as the rest of the project;
no Info-ZIP, zlib or PKWARE code is used.

## Tests

`make unzip-test` (`tests/run.mjs`): boots ARM-DOS headless with a private
C: holding UNZIP.EXE, the real Keen Dreams shareware zip, zips built by the
host's `zip` (subdirectories, an empty file, stored and deflated members), a
copy with one corrupted byte and a truncated one. Checks: usage/banner, `-v`
against the host's `unzip -v` (lengths, sizes, CRCs, totals), `-t`, `-o`
extraction byte-identical to the host's `unzip` for all 24 files, file dates,
the overwrite prompt (n, a), wildcards, `-n`, `-d` with an output directory,
path stripping, `-c`, and errorlevels 3, 9, 10, 11 and 51 through a batch
file's `IF ERRORLEVEL`. Screenshots `build/term-test/unzip-*.png`.

`node apps/unzip/tests/run.mjs nojit` runs the same with the emulator's JIT off (useful to
tell a JIT problem from an UNZIP one).

## Deviations from PKUNZIP

* No shrink/reduce/implode (PKZIP 1.x methods), no passwords, no `-e`
  sort orders, `-x` exclusions, `-$` volume labels, `-s` passwords, or
  `-p` printer output; no "PKUNZIP-style" progress percentage.
* Banner and messages are ARM-DOS's own wording (no PKWARE text).
