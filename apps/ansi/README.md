# ANSI.SYS — the ANSI console driver

`DEVICE=C:\DOS\ANSI.SYS [/X] [/L] [/K]` in CONFIG.SYS replaces the console
device CON with one that understands ANSI escape sequences, exactly as MS-DOS
4.00's ANSI.SYS does. It is a real installable character device driver
(ARCH.md 14.4): an AR1 image beginning with its device header named `CON`,
attribute C053h (character, IOCTL, generic IOCTL, INT 29h "special",
standard input/output), strategy and interrupt entry points. What it does
not handle itself (status, open/close, output flush, IOCTL read/write, other
generic IOCTLs) goes to the CON driver it replaced.

Source: a re-creation in C of `DEV/ANSI` (ANSI.ASM, ANSIINIT.ASM, IOCTL.ASM,
PARSER.ASM) from Microsoft's MIT-licensed MS-DOS 4.0 source, following the
original's logic step by step. Portions (C) Microsoft Corp., MIT License.

## Escape sequences

`ESC[` followed by parameters (decimal numbers, or quoted strings for key
reassignment, separated by `;`) and a command letter:

| sequence | effect |
|---|---|
| `ESC[nA` `ESC[nB` `ESC[nC` `ESC[nD` | cursor up / down / forward / back n (default 1), stopping at the edge |
| `ESC[r;cH` `ESC[r;cf` | cursor position (1-based; a row beyond the screen is ignored, a column is clipped) |
| `ESC[s` `ESC[u` | save / restore the cursor position |
| `ESC[2J` | clear the screen and home the cursor (like DOS 4, *any* `ESC[..J` does this) |
| `ESC[K` | erase from the cursor to the end of the line |
| `ESC[p1;...;pnm` | attributes: 0 normal, 1 bold (bright), 4 underline (blue on colour screens), 5 blink, 7 reverse, 8 concealed, 30-37 foreground, 40-47 background |
| `ESC[=nh` `ESC[=nl` | n = 0-6, 19 (13h): set that video mode; n = 7: line wrap on (`h`) / off (`l`) |
| `ESC[6n` | device status report: the cursor position is typed as `ESC[rr;ccR` + CR |
| `ESC[code;"string"p` | key reassignment: the key gives the string (see below) |
| `ESC[1q` `ESC[0q` | extended keys distinct on / off (as `/X`) |

Key reassignment as in DOS 4: `ESC[65;66p` makes A type B; `ESC[0;59;"DIR";13p`
makes F1 type `DIR` and Enter (extended keys are 0 followed by the scan
code); `ESC[65p` removes A's definition; an extended key is reset by
assigning it to itself (`ESC[0;59;0;59p`). Initially Ctrl-PrtSc (0;114)
types Ctrl-P, as in DOS 4.

The DOS 4 quirks are kept: parameters are collected in the key-reassignment
buffer (numbers are taken modulo 256), an unknown command letter is displayed,
`ESC` followed by anything but `[` displays that character, the text
attribute is applied through INT 10h AH=09h, the screen scrolls in the
current attribute, and the cursor position report goes through the keyboard.

`/X` makes the grey keys of the enhanced keyboard distinct keys for
reassignment, `/L` keeps the line count set by `MODE CON LINES=` across video
mode changes, `/K` uses the conventional INT 16h functions. Anything else:
`Invalid parameter - /Q` and `Error in CONFIG.SYS line n`, and the driver is
not installed.

## Colour prompt

With ANSI.SYS loaded, `$e` in PROMPT is the escape character:

    PROMPT $e[1;33;44m $p $e[0m$g
    PROMPT $e[s$e[1;1H$e[0;30;47m $d  $t $e[K$e[0m$e[u$p$g

The first gives a bright yellow on blue path, the second a status line with
the date and time at the top of the screen. `TYPE C:\DOS\ANSI.ART` shows the
ARM-DOS logo screen (made by `art/mkart.py`, CP437 block characters and
colour sequences only).

## Interfaces for programs

* INT 2Fh AX=1A00h: installed check (AL=FFh). AX=1A01h: the IOCTL below
  through INT 2Fh (CL=7Fh/5Fh, DS:DX data). AX=1A02h: information (/L state).
* INT 21h AX=440Ch on CON, CX=037Fh / 035Fh: get / set display information
  (level, length 14, flags bit 0 = intensity instead of blink, mode 1 text / 2
  graphics, colours, pixel width/height, columns, rows). Text modes 40 or 80
  columns with 25, 43 or 50 lines; graphics modes 4, 5, 6, 13h. Error codes as
  DOS 4 (1 invalid, 10 not supported). `MODE CON LINES=` and `COLS=` use it.
* INT 29h: fast console output; INT 1Bh: Ctrl-Break makes the next read ^C.

## Deviations (ARM-PC hardware)

* The mode table has only the modes the ARM-PC BIOS provides (0-6, 13h); `ESC[=13h`..
  `=18h` ask the BIOS for modes it does not have (nothing happens).
* 43 and 50 lines: the ARM-PC VGA always shows 400 lines and its BIOS has no
  INT 10h AX=1111h/1112h or AH=12h BL=30h, so the driver loads the BIOS 8x8
  font (AX=1110h) and programs the CRTC maximum scan line register itself:
  50 lines are 8-pixel rows, 43 lines 9-pixel rows (the screen shows an unused
  44th row below them).
* `ESC[nB` stops on the last line (as the real driver behaves; its source
  would allow one line more).
* The resident part is 5.8 KB (DOS 4's ANSI.SYS: about 4 KB).

## Tests

`make ansi-test` (`tests/run.mjs`) boots with `DEVICE=C:\DOS\ANSI.SYS`:

* `resident`: nothing in the resident part refers to the discarded INIT code
  (`tests/rescheck.mjs`, also used by MOUSE and MODE).
* `t1`, `t1raw`: `tests/T1.ANS` (every sequence and quirk: wrap and no-wrap at
  the right margin, clipping, overflowing numbers, unknown commands, stray
  ESC, SGR combinations, save/restore, ED with a parameter, EL) typed through
  INT 29h and through the driver's WRITE command; the screen is compared cell
  by cell (character, foreground, background) with the screen the genuine
  MS-DOS 4.00 ANSI.SYS produced from the same file in DOSBox-X
  (`tests/real/t1-dos400.txt`).
* `func` (`tests/atest.c`): INT 2Fh install check, `ESC[6n` reports, key
  reassignment of a normal and an extended key and its removal, the IOCTL:
  get, 50 lines, 43 lines, invalid 30, 25, 40 and 80 columns.
* `art`: `ANSI.ART` (screenshot `build/ansi-test/art.png`); `prompt`: a
  PROMPT-style colour sequence; `badparm`: `/Q` is refused as DOS 4 does.
