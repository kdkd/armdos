# BASIC for ARM-DOS (Bywater BASIC 3.40, GW-BASIC dialect)

`C:\DOS\BASIC.EXE` (on the PATH), demo programs in `C:\BASIC\`.

```
C:\>BASIC
Bywater BASIC 3.40 for ARM-DOS  (GW-BASIC dialect)
Copyright (c) 1993 Ted A. Campbell, 1995-1997 Jon B. Volkoff,
2014-2017 Howard Wulf, 2019 Ken Martin, and others.  GNU GPL v2.
158856 Bytes free
Ok
10 PRINT "HELLO"
20 GOTO 10
RUN
...                      (Ctrl-C or Ctrl-Break: "Break in 20")
SAVE "FOO"               (writes FOO.BAS)
LOAD "FOO"   LIST   RUN "\BASIC\PATTERN"   FILES   SYSTEM
```

`BASIC PROG` loads and runs `PROG.BAS` and stays in BASIC (as GW-BASIC
does); end the program with `SYSTEM` to return to DOS.

## Source and licence

* **Bywater BASIC 3.40** (2025-10-23), `https://sourceforge.net/projects/bwbasic/`
  (`bwbasic-3.40.zip`). **GPL version 2** (`src/COPYING`, `src/README`).
  Copyright Ted A. Campbell, Jon B. Volkoff, Paul Edwards, Howard Wulf,
  Ken Martin, Lee Wittenberg.
* `src/` holds the upstream interpreter files, **modified** for ARM-DOS; every
  change is inside `#ifdef ARMDOS` (or is the mechanical `M80 | G86` bitmask
  edit below), so `diff` against the zip shows exactly what changed.
* `bwx_dos.c`, `bwx_dos.h`, `dosvid.c`, `dosvid.h` are new (GPL-2 as well):
  `bwx_dos.c` replaces upstream's `bwx_tty.c` and holds the new statements;
  `dosvid.c` is the PC hardware side (screen, graphics, keyboard, speaker,
  ports), kept apart so the DOS headers never meet bwBASIC's names.
* The GPL requires offering the source with the binary: this directory is it.

## The GW-BASIC dialect

bwBASIC has no GW-BASIC mode as such; its closest dialect is `MBASIC`
(Microsoft BASIC-80). A new dialect `GW-BASIC` (bit `G86`) was added to
`bwb_tbl.c` as a copy of MBASIC, every command/function/operator table entry
that had `M80` also got `G86` (a scripted edit of `bwd_cmd.c`, `bwd_fun.c`,
`bwb_exp.c`), plus `ON TIMER`/`TIMER ON|OFF|STOP`; it is the default dialect
(`OPTION VERSION` still switches to any other). On top of that, to behave like
GW-BASIC:

* `Ok` prompt, shown after direct commands but not after typing a program line
  (and always at the start of a line); banner + "Bytes free".
* Messages as GW-BASIC prints them: `Syntax error in 20`, `Break in 30`,
  `Syntax error` for a bad direct statement (a bad program line is kept and
  reported when run); no stack trace.
* `LIST` and `SAVE` show each line **as it was typed** (`20 FOR I=1 TO 3:
  PRINT I: NEXT`). bwBASIC splits lines into statements and rewrites some of
  them internally (`CALL CLS`, `IF ... THEN: ... :END IF`); the typed text is
  kept per line (`LineType.OrigText`) for listing. No indentation, no blank
  line after `LIST`.
* `SAVE "NAME"` adds `.BAS`; `,A` / `,P` accepted (files are always ASCII);
  `LOAD`/`RUN "NAME"` find `NAME.BAS`.
* Direct-mode loops and IFs: `FOR I=1 TO 10: PRINT I: NEXT` and
  `IF X THEN PRINT 1: PRINT 2 ELSE PRINT 3` work on the command line (upstream
  says "Illegal direct"): the statements of a direct line are linked the way
  `bwb_scan` links a program.
* `NEXT I, J` (rewritten to `NEXT I: NEXT J` on entry); `10PRINT` without a
  space; integer division truncates (`7\2` = 3; upstream rounded to 4);
  `RENUM [new][,[old][,inc]]` is built in (renumbers `GOTO GOSUB THEN ELSE
  RESTORE RESUME RUN` and `ON ... GOTO` lists; upstream ran an external
  `renum` program on the saved file).
* `RUN "FILE"` of a program with a structure error no longer re-runs itself
  forever (upstream bug); `BASIC PROG` stays in BASIC afterwards, and a missing
  file says `File not found`.
* `FILES [spec]` lists the directory GW-BASIC style (native, no `DIR /W`).
* `TIMER` has 1/18.2 s resolution (BIOS tick count); `INKEY$` never waits and
  returns `CHR$(0)+scan code` for extended keys; `INPUT$(n)` reads keystrokes
  without echo; `PEEK`/`POKE` use `DEF SEG` (address = segment*16 + offset,
  real memory: `DEF SEG=&HB800: POKE 0,65`), `INP`/`OUT` are real I/O ports;
  `WIDTH 40`/`WIDTH 80` switch the text mode.

## Screen, graphics and sound

When standard output is the console, `PRINT` goes to the screen through our
own writer (text memory / INT 10h) so that `COLOR` applies, as GW-BASIC drew
its own screen; when stdout is redirected (`BASIC PROG >OUT.TXT`) output goes
through DOS unchanged. Input lines always come from DOS (buffered input with
the DOS editing keys). Statements added:

| statement | notes |
|---|---|
| `CLS`, `LOCATE row[,col[,cursor]]`, `CSRLIN`, `POS(0)` | text memory / INT 10h |
| `COLOR fg[,bg[,border]]` | text: fg 0-31 (16+ blinks), bg 0-7; SCREEN 1: `COLOR bg,palette`; SCREEN 13: `COLOR fg` |
| `SCREEN 0/1/2/13` | 0 text, 1 = CGA 320x200x4, 2 = CGA 640x200x2, 13 = VGA 320x200x256 (QBasic's number) |
| `PSET`/`PRESET [STEP](x,y)[,c]`, `POINT(x,y)` | direct video memory |
| `LINE [[STEP](x1,y1)]-[STEP](x2,y2)[,[c][,B[F]]]` | |
| `CIRCLE [STEP](x,y),r[,c[,start,end[,aspect]]]` | arcs and pie slices (negative angles), default aspect 5/6 (5/12 in SCREEN 2) |
| `PAINT [STEP](x,y)[,paint[,border]]` | scan-line flood fill |
| `DRAW a$` | U D L R E F G H, M x,y / M +-x,+-y, B, N, C, S, A, TA, P |
| `PALETTE attr,colour` | text/CGA: attribute controller; SCREEN 13: DAC entry, colour = &Hbbggrr (0-63 each) |
| `BEEP`, `SOUND freq,ticks` | PC speaker: PIT channel 2 + port 61h; 18.2 ticks per second |
| `PLAY a$` | music macro language: A-G #+- n ., O n, < >, L n, T n, P/R n, N n, MN ML MS (MF/MB accepted) |
| `DEF SEG [=seg]`, `KEY ON/OFF/LIST` (accepted, no key line) | |
| `SHELL ["command"]` | runs the command through COMMAND.COM; plain `SHELL` gives a DOS prompt, `EXIT` returns |

Ctrl-C and Ctrl-Break stop a running program ("Break in *n*"): INT 1Bh and
INT 23h are hooked while BASIC runs and the keyboard is polled for Ctrl-C every
64 statements; sounds stop too. On exit (`SYSTEM`) the vectors are restored,
the speaker is silenced and a graphics mode goes back to text mode.

Not provided: `EDIT n` (upstream runs an external editor), `VIEW`/`WINDOW`,
`GET`/`PUT` graphics, `ON KEY`/`ON PLAY`/`STRIG`, background music (`PLAY` and
`SOUND` wait), GW-BASIC's full-screen editor (lines are typed on the DOS input
line; F1/F3 recall the last line).

## Numbers

Double precision soft-float (`-mfloat-abi=soft`), printed with newlib's float
printf (`$(ARMDOS_PRINTF_FLOAT)`), 6 significant digits (`PRINT 1/3` gives
` .333333`). Speed: `FOR I=1 TO 10000: A=A+I*2: NEXT` takes 1.3 s of
emulated time (a 4.77 MHz PC took about 10 s), 1,000 `SIN`+`SQR` 0.27 s.
BASIC.EXE is about 386 KB (64 KB stack); the heap continues in XMS when
conventional memory is used up.

## Demo programs (`disk/BASIC/` -> `C:\BASIC\`, written for ARM-DOS)

| file | what |
|---|---|
| `SINE.BAS` | sine wave plotted with `TAB` in text mode, then SIN/COS/SIN(2X) in SCREEN 13 |
| `GUESS.BAS` | guess the number (RND, INPUT; a fanfare with PLAY if you win in 7) |
| `MUSIC.BAS` | Ode to Joy and Frère Jacques with PLAY (public-domain tunes), a siren with SOUND, scales |
| `PATTERN.BAS` | SCREEN 13: circles, lines, the 256-colour palette, DRAW and PAINT |
| `COLORS.BAS` | the 16 text colours on the 8 backgrounds, blinking text |

## Tests (headless emulator)

Typed at the `Ok` prompt: a program (`10 PRINT "HELLO"`, a FOR loop), `LIST`,
`RUN`, `SAVE "FOO.BAS"`, `NEW`, `LOAD "FOO.BAS"`, `LIST` (round trip), `FILES`,
`RENUM` (twice, with references), `DELETE`, `AUTO`, `TRON`, `20 GOTO 10` +
`RUN` + Ctrl-C (`Break in 30`), `PRINT` of floats/strings/`HEX$`/`TIMER`,
direct-mode FOR/NEXT, WHILE/WEND and multi-statement IF/ELSE, `COLOR 14,1`,
`LOCATE`, `PEEK` of text memory, `BEEP`/`SOUND`/`PLAY` (speaker callback:
262/294/330 Hz notes of 0.48 s at T120), `SCREEN 13/1/2` with LINE/CIRCLE/
PAINT/DRAW/PSET (screenshots checked), every demo program, `SYSTEM`. With
COMMAND.COM: `BASIC GUESS` from `C:\BASIC`, Ctrl-C at INPUT -> `Break in 70`
and `Ok`, `BASIC NOSUCH` -> `File not found`, `BASIC \P >\OUT.TXT` then
`TYPE \OUT.TXT` (` 1  4  9 DONE`), `SHELL "DIR /W"`, `SHELL` + `VER` + `EXIT`.

## Fixes (from QB.EXE's copy of the interpreter)

* `LOCATE row, col, cursor` and `COLOR fg, bg, border` gave "Illegal function
  call": bwBASIC's function dispatcher had the third number argument compiled
  out (`bwb_fnc.c`, under `#ifdef ARMDOS`).
* String comparison stopped at the first CHR$(0), so `INKEY$ = CHR$(0) + "H"`
  compared equal to `""` and to `CHR$(0) + "P"`; strings now compare by
  length (`=`, `<>`, `<`, `>`, ..., `SELECT CASE`; `bwx_bufcmp`).
* `VAL("")` gave "Illegal function call"; it is 0, as in GW-BASIC.

`make basic-test` (`tests/run.mjs`, hooked to `make test`) checks these.
