# EDLIN.COM - the MS-DOS 4.00 line editor, on ARM

`C:\DOS\EDLIN.COM` (`build/EDLIN.COM`, a self-relocating ARM `.COM`, picked up
by `disk/hd.json`'s `build/*.COM` rule).

    EDLIN [d:][path]filename [/B]

Everything a 1989 user remembers: the `*` prompt, `New file` / `End of input
file`, line numbers right-aligned in 8 columns with `*` on the current line,
editing a line with the old text as the DOS template (F1, F3, Esc ...),
`A C D E I L M P Q R S T W`, `;` between commands, `.` `#` `+n` `-n`, `?R` /
`?S` with `O.K.?`, `^V` to type control characters, `^Z` (F6) to end insert
and to separate the two strings of `R`, `^C` in insert mode, `Continue (Y/N)?`
when a listing fills the screen, `Abort edit (Y/N)?`, the `.$$$` work file and
the `.BAK` on `E`, the `^Z` written at the end of the file, the 3/4-full load
of files larger than memory with `W`/`A`, and every message of the 4.00
`EDLIN.SKL` (as resolved against `USA-MS.MSG`).

## Source and licence

`edlin.c` is a routine-by-routine C port of Microsoft's MS-DOS 4.00 EDLIN
(`CMD/EDLIN/EDLIN.ASM`, `EDLCMD1.ASM`, `EDLCMD2.ASM`, `EDLMES.ASM`,
`EDLPARSE.ASM` in the MIT-licensed MS-DOS 4.0 source release): the same buffer
layout (`start`, `pointer`, `endtxt`, `last`), the same routines (FINDLIN,
SCANLN, APPEND, SCANEOF, MOVEFILE, BLKMOVE, FNDFIRST/FNDNEXT, ...) and the
same quirks. Portions (C) Microsoft Corp., MIT License. Messages follow the
ARM-DOS branding rule (EDLIN prints no product name anyway).

## How it maps to ARM-DOS

* All I/O is INT 21h through `svc #0x21`: AH=0Ah for command and text lines
  (so the kernel's template editing is EDLIN's line editor, as on a PC), AH=01h
  for the Y/N answers (the message retriever's `DOS_KEYB_INP`), AH=40h to
  handles 1 and 2, AX=6523h for Y/N, AH=6Ch extended open, IOCTL 440Ch for the
  screen size (25x80 when the console does not answer). So `EDLIN F < SCRIPT >
  LOG` works exactly like the original.
* `^C`: the original's INT 23h handlers reset the stack and jump into the
  command loop. Here INT 23h (a C handler called by the kernel with the
  interrupted call's registers) records the ^C and re-issues a harmless call;
  when that returns, the program `longjmp`s to the same places (ABORTCOM,
  ABORTINS, ABORTMERGE).
* The text buffer is up to 64 KB of DOS memory (INT 21h AH=48h), as the
  original's one segment.

## Tests (`make edlin-test`, `apps/edlin/tests/run.mjs`)

107 checks, all passing:

1. **22 sessions against the genuine EDLIN.** `tests/scenarios.mjs` has 18
   scripted sessions (listing and paging, line editing, insert/delete,
   copy/move with counts, entry errors, search and replace with `?`, quit
   answers, `^Z` on load, `/B`, missing final CR LF, empty file, control
   characters and `^V`, transfer, write/append, long lines, names without an
   extension) and 4 command-line errors. `tests/mkref.mjs` ran them with the real
   MS-DOS 4.00 `EDLIN.COM` in DOSBox-X (`EDLIN args < S.SCR > S.OUT`) and
   stored the captured output and every file left behind in `tests/ref/`.
   On ARM-DOS the same sessions must produce identical bytes: output, `.TXT`,
   `.BAK`, and no stray `.$$$`.
2. **Typed at the keyboard:** the verified session of UTILITIES.md 2.14 (screen
   line for line), F1/F3/Esc template editing, F6 ending insert, F3 recalling
   the last command, ^C in insert and at the prompt, `Abort edit (Y/N)?`
   re-asking, `Q` `Y` leaving the file alone, `Continue (Y/N)?` after 24 lines,
   `?S` with `O.K.?`.
3. **Command line:** no name, `.BAK`, bad drive, bad path, too many
   parameters, bad switch, `/B /B`, a read-only file.
4. **Files larger than the buffer** (91,500 bytes): `E` round trip, delete,
   `W`/`A` sequences; the `.BAK` is the original.

`tests/redir.c` (EDREDIR.EXE, test only) runs a program with both STDIN and
STDOUT redirected.

## Deviations and 4.00 quirks

Kept on purpose (verified with the genuine EDLIN):

* **`/B` is accepted but does nothing** in 4.00 (EDLPARSE's `val_sw` compares
  the parser's synonym pointer with the wrong address, so `parse_switch_b`
  never gets set): the file is still cut at the first `^Z`. The port does the
  same; the one-line change to make `/B` work is marked in `parse_command()`.
* `E` ignores anything after it (`e3` saves and exits); `E` on a file that did
  not fit prints `End of input file` while it reads the rest.
* An empty file loads as one empty line (check_end supplies the CR LF).
* `R` without `^Z` reuses the previous replacement text; `S`/`R` without a
  string reuse the previous one.

Differences:

* **Six or more parameters** (`1,2,3,4,5,6m`): the original stores them past
  its 4-word parameter table, over `CURRENT` and `POINTER`, and then usually
  hangs. The port ignores the extra ones (the command then fails with
  `Entry error` or runs as with the first four).
* Memory: the original's buffer is its 64 KB segment minus its own code
  (about 50 KB); the port's is a separate 64 KB block, so the point where a
  big file stops loading differs by some 14 KB.
* The byte before the buffer, which `check_end` looks at for an empty file, is
  always 0 here. In the original it lies outside the `.COM` file; with a LF
  left there by an earlier program, an empty file loads as no line and `E`
  then loops forever.
* The extended open leaves out bit 8 of DX ("don't validate the code page").
  No visible difference.
* Size: 28 KB (the 4.00 EDLIN.COM is 14 KB); about 15 KB of it is the SDK's C
  runtime and libgcc.
* The DBCS (Kanji) code of the original is not ported (it is assembled out of
  the US version too).
