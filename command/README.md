# COMMAND.COM — the ARM-DOS 4.00 shell

A C re-creation of MS-DOS 4.00's COMMAND.COM, following the MIT-released
4.0 source (`CMD/COMMAND/*.ASM`) routine by routine, with every text from
`messages.c` (numbered as `COMMAND.SKL`/`USA-MS.MSG`, branding: ARM-DOS).
The behaviour reference is MS-DOS 4.00 itself; the tests compare the
screen with captures of the real 4.00 (see "Tests").

`make command` builds `build/COMMAND.COM`; `make command-test` runs the tests.

## Resident and transient parts

As in DOS 4, COMMAND.COM is split so that programs get almost all memory:

* **resident part** (`res.c`, `resstart.S`, `res.h`; about 9 KB with its
  PSP): start-up, the INT 22h/23h/24h handlers (CONTC, DSKERR with all the
  critical-error texts), EXEC of external programs, and the transient loader.
  All state that must survive a program run (`struct res R`: echo flag,
  batch/FOR/pipe state, SINGLECOM, RETCODE, COMSPEC, the keyboard template
  line, ...) lives here.
* **transient part** (everything else): the command processor. It is an AR1
  image carried at the end of COMMAND.COM (`blob.S`). At start-up the
  resident part copies it into a block at the top of memory (last-fit
  allocation), relocates it and shrinks itself. To run a program it frees
  that block, EXECs, allocates the block again and checks the transient's
  checksum; if the program overwrote it, it is read back from COMSPEC and
  relocated — with DOS 4's `Insert disk with \COMMAND.COM in drive A` /
  `Press any key to continue . . .` when the disk is not there, `Invalid
  COMMAND.COM` for a different file, and `Cannot load COMMAND, system
  halted` when it cannot be found on fixed media.
* The transient reaches the resident state through `R` (the macros in
  `cmd.h` make `echoflag` etc. mean `R->echoflag`). It must not have
  initialised writable data (the build checks), and its `.bss` is zeroed
  each time it is entered.

The first shell is its own parent; its INT 22h (PSP+0Ah) is `lodcom_entry`,
so a ^C or a critical-error Abort during an internal command makes DOS
"terminate" COMMAND and come back to the command loop, as LODCOM does.

## Files

| file | from DOS 4's | what |
|---|---|---|
| `res.c` | COMMAND1/2.ASM, RUCODE.ASM | resident part |
| `main.c` | TCODE.ASM, TMISC1.ASM, INIT.ASM | command loop, internal command table, start-up |
| `parse.c` | TMISC1 PRESCAN, CPARSE.ASM, PARSE2.ASM, SYSPARSE | redirection/pipe scan, tokens, parse blocks |
| `batch.c` | TBATCH.ASM, TBATCH2.ASM, TFOR.ASM | batch files, CALL, GOTO, SHIFT, IF, FOR |
| `exec.c` | PATH1/2.ASM, TMISC2.ASM, TPIPE.ASM | PATH search, EXEC request, redirection, pipes |
| `env.c` | TENV.ASM, TCMD2A.ASM | environment, SET, PATH, PROMPT, the prompt |
| `cmds1.c` | TCMD1A/B.ASM, TUCODE.ASM | DIR, VOL, DEL, REN, TYPE, PAUSE |
| `cmds2.c` | TENV2, TCMD2A/B, TUCODE, TPIPE | CD, MD, RD, VER, CLS, ECHO, BREAK, VERIFY, DATE, TIME, CTTY, CHCP, TRUENAME |
| `copy.c` | COPY.ASM, COPYPR1/2.ASM | COPY |
| `messages.c`, `output.c`, `dos.c` | messages, retriever formats, INT 21h calls |

## Tests

`tests/run.mjs` boots `build/cmdtest/hd.img` (COMMAND.COM, the tree in
`tests/disk`, and the test programs `tests/progs/*.c`: RET, ARGS, UPCASE,
TRASH) headless and runs `tests/cases.txt`; each case starts with CLS and the
screen is compared with `tests/expected/NAME.txt`.

The expected screens are the real MS-DOS 4.00's: `tests/ref/capture.py`
runs the same keystrokes on the PCjs 4.00 disks in DOSBox-X
(`tests/ref/dos4ref.py`, reference tooling only; needs MS-DOS 4.00 disk
images, which are not distributed, in the directory `ARMDOS_REFS` names), on a disk with the same files in the same directory order
and x86 twins of the test programs (`tests/ref/x86/*.asm`), and substitutes
the branding. Dates, times, free space, serial numbers and COMMAND.COM's
size are normalised on both sides (`tests/cases.mjs`).

## Deviations

* Not implemented: INT 2Eh (the "back door"), APPEND's hooks, the code-page
  extended attributes COPY and TYPE carry in 4.0, and "Mel Hallerman" copies
  (an ambiguous concatenation to an ambiguous destination, `COPY *.A+*.B *.C`).
* DOS 4 bug compatibility: a failed GOTO in a CALLed batch file leaks a batch
  segment in NEST, and the next `Terminate batch job` then reads garbage
  memory as a batch segment; we reproduce what the real 4.00 shows (`Batch
  file missing`, and the echo state it ends up with) without reading garbage.

## ARM-DOS additions

* **VER** prints two lines after `ARM-DOS Version 4.00`:
  `Copyright (C) 2026 Kevin Day, with lots of help from Europa.` and `See
  documentation for full copyright notices and acknowledgements.` (messages
  1901/1902). `$V` in PROMPT is
  unchanged.

## Spoilers: hidden commands

Internal commands that DOS 4.00 never had (`eggs.c`, messages 1910-1935).
They are not files, so DIR doesn't show them, and no help lists them. Each
prints its text on stdout (redirectable) and sets ERRORLEVEL 0 (UNAME: 1
after a usage error). None of
them changes anything. Real programs with the same names would be shadowed,
so the names were checked against every app on the disks (none clash).

| command | says / does |
|---|---|
| `MSD` | Microsoft Diagnostics? On an ARM926? Type ARMINFO - it knows this machine better than anyone in Redmond ever will. |
| `WIN` | Windows? This machine has GEM. Type GEM - it was here first, and it doesn't need a 386. |
| `DELTREE` | DELTREE arrives with DOS 6.0, in 1993. Patience. |
| `MEMMAKER` | 640K ought to be enough for anybody. You have 15 MB of extended memory anyway. Relax. |
| `INTEL` | No Intel inside. This is an ARM926EJ-S. (For the other kind of PC, look for the ELBOW box.) |
| `XYZZY` | Nothing happens. |
| `PLUGH` | A hollow voice says "Plugh". |
| `IDDQD` | Degreelessness mode on. (It doesn't do anything here, but it feels good.) |
| `IDKFA` | Very happy ammo added. Now go and find a keyboard with DOOM on it. |
| `HAL` | I'm sorry, Dave. I'm afraid I can't do that. |
| `42` | 42. Now, what was the question? |
| `SUDO` | It's 1988. Nobody has heard of sudo. Besides, this is DOS: you are already root. (Ignores its arguments, so `SUDO DEL *.*` deletes nothing.) |
| `LS` | This is not Unix. Did you mean DIR? Running DIR. - then DIR with the same arguments (`LS /W`). |
| `ELIZA` | ELIZA has retired. Her colleague will see you now. - then runs DOCTOR (found on the PATH) with the same arguments. |

| `UNAME` | GNU-style `uname` with the ARM/AT's answers: no flags = `-s` (`ARM-DOS`); `-s -n -r -v -m -p -i -o` (also combined, `-snrm`, and as `--kernel-name` etc.), `-a`/`--all` = `ARM-DOS ARMAT 4.00 4.00-1988.06.17 armv5tel ARM926EJ-S ARM/AT ARM-DOS`; `--help` (ends "(This is still not Unix.)"), `--version`. GNU's errors on stderr, ERRORLEVEL 1: `uname: invalid option -- 'x'`, `uname: unrecognized option '--foo'`, `uname: extra operand 'bar'`, each followed by `Try 'uname --help' for more information.` Options are case-insensitive (DOS habit). |

Tests: cases `eggs`, `eggs2`, `eggs-uname`, `eggs-uname-help`, `eggs-ls` (ARM-DOS only). The `bad-command`
case now types `XYZZQ` instead of `XYZZY`.
