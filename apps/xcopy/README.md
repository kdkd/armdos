# XCOPY.EXE - MS-DOS 4.00 XCOPY for ARM-DOS

    XCOPY source [destination] [/A] [/D:date] [/E] [/M] [/P] [/S] [/V] [/W]

A C re-creation of the MS-DOS 4.00 XCOPY, goes to `C:\DOS` (the default for
`build/*.EXE`).

## Source and licence

Written from the behaviour of `CMD/XCOPY/XCOPY.ASM`, `XCPYINIT.ASM` and
`XCOPYPAR.ASM` of the MS-DOS 4.0 source (Portions (C) Microsoft Corp., MIT
License) and checked against the genuine XCOPY.EXE 4.00 running in DOSBox-X
(apps/mslib/tools/dos400run.sh). No Microsoft code is included; the command
line parser, messages and start-up are the shared helpers in `apps/dosutil`.

## What it does, as 4.00 does it

* Parameters through the DOS 4 parser: `Invalid switch - /Q`, a repeated
  switch is invalid too (`/S /S`), `Invalid number of parameters - X` for a
  third name, `Invalid parameter - /D:abc`, `Invalid date`, `Path too long`
  (over 63 characters), `Invalid drive specification`, `Invalid path`; every
  error still ends with `        0 File(s) copied`.
* The source is tried as a directory first, then as directory + file name.
  A missing target directory is created at once, element by element; if its
  last element could be a file, the question
  `Does X specify a file name` / `or directory name on the target` /
  `(F = file, D = directory)?` is asked (any other key asks again). A
  wild-card target name (`*.BAK`, `X??.*`) is a template.
* Memory for a buffer is taken from DOS (INT 21h AH=48h, all but 5 KB of the
  largest block). Files (in 0FFD0h-byte parts) and directory entries are read
  into it until it is full, then written; `Reading source file(s)...` starts
  each reading pass and every file name is printed as it is written
  (`\S\A.TXT`, `\S\D1\B.TXT`). Space is counted in paragraphs with a
  3-paragraph header per entry exactly as the original, so the passes break
  at the same places for the same free memory.
* `/S` walks subdirectories (only entries whose attribute is exactly
  "directory"), `/E` also creates empty ones; without `/E` an empty
  directory is dropped from the buffer (or removed again if it was already
  created). `/A` copies files with the archive bit, `/M` too and then clears
  it on the source, `/D:date` files dated on or after the date, `/P` asks
  `\S\A.TXT (Y/N)?` per file (and copies one at a time), `/W` first says
  `Press any key to begin copying file(s)`, `/V` turns VERIFY on for the run.
* Copies keep the source's date and time; the archive bit is set on them.
* `File cannot be copied onto itself`, `Cannot perform a cyclic copy` (with
  /S or /E into the source's own subtree), `Cannot XCOPY from a reserved
  device`, `Cannot XCOPY to a reserved device`, `File creation error`,
  `Insufficient disk space`, `Unable to create directory`.
* `File not found - NAME` when nothing matched (the name as DOS parses it,
  `????????.???` for a whole directory), then the count. If the target
  directories it created stayed unused, XCOPY removes them at the end the way
  4.00 does - relative to the current directory of the target drive, which
  on a single-drive copy is the *source* directory. So, faithfully,
  `XCOPY \S\E1 \X12\` with an empty `\S\E1` removes `\S\E1` and leaves
  `\X12`, just as the real 4.00 does.
* The current directories of the drives involved and the default drive are
  restored at the end; exit code 0, or 4 after an error.

## Tests

`make xcopy-test` (`tests/run.mjs`, 70 checks) boots ARM-DOS headless with the
kernel's test shell and runs the same command sequences that were run with
the real XCOPY 4.00: redirected output byte for byte (`tests/expected/X*.TXT`,
`P*.TXT`, `Y*.TXT`), the resulting directory trees (`tree_*.txt`, from
`mdir` on the DOSBox-X disk), the screens with the error messages and
prompts (`screen_*.txt`), archive bits after `/M`, dates of the copies, and a
1,000,000-byte file plus 4 x 200,000 bytes (two reading passes) copied
intact.

## Deviations

* Ctrl-C ends XCOPY (after restoring the directories) with DOS's Ctrl-C
  termination instead of the original's exit code 2.
* No INT 24h handler of its own: a critical error goes to the shell's
  handler (4.00 retried on Ignore and ended with the file count on Abort).
* Extended attributes (INT 21h 5702h/5704h) are not copied - ARM-DOS's FAT
  has none - and APPEND /X is not switched off (there is no APPEND).
* The source tree is read with full path names instead of changing the
  current directory into every source subdirectory (the final current
  directory, which the directory clean-up above depends on, is the same).
