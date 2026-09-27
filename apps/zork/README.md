# ZORK I, II, III for ARM-DOS

`C:\GAMES\ZORK\ZORK1.EXE`, `ZORK2.EXE`, `ZORK3.EXE` (also `A:\ZORK\` on the
Games floppy). Type `ZORK1` in `C:\GAMES\ZORK` to play Zork I.

## What it is

* **Interpreter:** [MojoZork](https://github.com/icculus/mojozork) by Ryan C.
  Gordon, a Z-machine version 3 interpreter in one C file, **zlib licence**
  (`src/LICENSE-MOJOZORK.TXT`, copied to the disk as `MOJOZORK.TXT`).
  Vendored from `https://github.com/icculus/mojozork` at commit
  `ff7e00742a00acec8e175ddefb97520fb270df2d` as `src/mojozork.c`, **altered**
  (zlib clause 2: the changes are marked `ARM-DOS` / `#ifdef ARMDOS`).
* **Story files:** the compiled `.z3` files from Microsoft's MIT-licensed
  release of the Zork source (2025), `https://github.com/historicalsource/zork1`
  (`zork2`, `zork3`)
  (zork1 at `97b7b3d6`, 2025-11-21):

  | disk file | repo file | bytes | release / serial |
  |---|---|---|---|
  | `ZORK1.DAT` | `zork1/COMPILED/zork1.z3` | 86,838 | 119 / 880429 |
  | `ZORK2.DAT` | `zork2/COMPILED/zork2.z3` | 92,524 | 63 / 860811 |
  | `ZORK3.DAT` | `zork3/COMPILED/zork3.z3` | 87,984 | 25 / 860811 |

  Byte-identical copies, renamed the way Infocom's DOS releases named them.
  Licence: MIT, `Copyright (c) 2025 Microsoft` (`data/LICENSE.TXT`, identical in
  the three repos, copied to the disk as `LICENSE.TXT`). The licence does not
  grant trademark rights; "Zork" is used here only to say what the files are.
  (MojoZork's own `zork1.dat`, Activision's old release 88, is *not* used.)

## The DOS front end (`zorkdos.c`)

`zorkdos.c` `#include`s `src/mojozork.c` and supplies what 1988's Infocom
MS-DOS interpreter looked like:

* the screen is cleared; **row 0 is an inverse-video status line**, room name
  on the left, `Score: n        Moves: n` on the right (a "time" game would show
  the time), written straight into text memory at B800:0000;
* the story scrolls in rows 1-24 only: at the bottom line a newline scrolls the
  window with INT 10h AH=06h (rows 1-24) instead of letting the BIOS teletype
  scroll the whole screen; the status line is redrawn after each input line
  (DOS's echo of Enter may scroll the full screen);
* word wrap at the screen width (79 columns, so the cursor never auto-wraps),
  and a `[MORE]` prompt when a screenful goes by without input;
* the text itself goes through DOS (stdout), and input is read with DOS's
  buffered line input (so the DOS editing keys work). With stdout redirected
  there is no status line and no `[MORE]`: `ZORK1 <MOVES.TXT >LOG.TXT` gives a
  clean transcript (tested);
* `SAVE`/`RESTORE` ask for a file name like Infocom's interpreter:
  `Enter a file name.` / `(Default is "ZORK1.SAV"):`; Enter takes the default,
  a name without an extension gets `.SAV`; the last name becomes the default.
  A missing or foreign save file makes `RESTORE` say `Failed.` instead of
  killing the game (the file is read and checked - release and serial - before
  anything is overwritten);
* `SCRIPT` copies the transcript to `ZORKn.SCR` (Infocom printed it);
* `QUIT` returns to DOS; the story file is found next to the program (from
  `argv[0]`), else in the current directory; `ZORK1 OTHER.DAT` plays any other
  version-3 story file.

Changes inside `mojozork.c` (all `#ifdef ARMDOS`): line input hook (end of
input = quit), save/restore file names and the checked restore, the status-line
hook and header flag ("status line available"), `output_stream`/`input_stream`
accepted as no-ops (v3), no upstream `main`. Two upstream bugs fixed for ARMv5:
`opcode_call` and `opcode_loadw` did 16-bit loads through odd pointers
(`*(uint16 *) routine`), which an ARMv5 `LDRH` cannot do (it ignores bit 0 of the
address): routine locals got wrong initial values - e.g. Zork I's `VERSION`
printed no serial number. Both now use `memcpy`.

One source, three programs: `app.mk` builds it three times with
`-DSTORYNAME='"ZORK1"'` etc. Each `.EXE` is 51,576 bytes (the story is loaded
into the heap at run time).

## Tests (headless emulator, `emu/testkit.mjs`)

* Booted with `SHELL=` a ZORK1 test image, then the full `hd.img` (merged
  manifests) with COMMAND.COM: `CD \GAMES\ZORK`, `ZORK1`: banner "Release 119 /
  Serial number 880429", `open mailbox`, `read leaflet`, `save` (default name,
  `ZORK1.SAV` 15,398 bytes appears in `DIR`), moves, `restore`, `look` back at
  West of House; into the house, `take lamp`, `move rug`, `open trap door`,
  `down` (score 35, status line updated); `quit` returns to `C>`.
* `ZORK2`, `ZORK3` start (status line "Inside the Barrow" / "Endless Stair");
  `restore` of a missing file: `File not found.` / `Failed.`
* `\GAMES\ZORK\ZORK1 <\M.TXT >\LOG.TXT` then `TYPE \LOG.TXT`: full transcript.
* Screenshots checked: status line in inverse video, story scrolling beneath.
