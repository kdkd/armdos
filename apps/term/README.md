# TERM.EXE - communications program for ARM-DOS

`C:\TERM\TERM.EXE` (with `C:\DOS\TERM.BAT`, so `TERM` works from anywhere; its
files live next to it: `TERM.DIR` dialing directory, `TERM.CFG` setup, `TERM.CAP`
capture, downloads in `C:\TERM\DOWNLOAD\`). Also offered for download on The ARM
Pit BBS (apps/bbs).

An original program in the style of the late-1980s DOS terminal programs
(Procomm Plus, Qmodem, Telix) - nothing is copied from them; the look is an
homage: an ANSI-BBS terminal screen with a status line at the bottom

    Alt-Z for Help | ANSI-BBS | 2400 N81 | FDX | COM2 | LOG OFF | Online 00:03:12

| key | what |
|---|---|
| Alt-D | dialing directory (Enter dials, 1-9 dials by number, R revise, M manual dial, E erase) |
| Alt-H | hang up (drops DTR; `+++` + `ATH0` if the modem ignores DTR) |
| Alt-S | setup: port COM1/COM2, baud, init string, dial/hang-up commands, download directory, redial attempts and pause, ZMODEM auto-download, crash recovery, local echo |
| Alt-L | capture incoming text to a file (raw, appended) |
| Alt-B | scrollback (400 lines; arrows, PgUp/PgDn, Home/End) |
| Alt-C / Alt-E | clear screen / local echo (HDX/FDX) |
| PgUp / PgDn | upload / download: ZMODEM, YMODEM (batch), YMODEM-G, XMODEM (CRC), XMODEM-1K |
| Alt-Z | help screen |
| Alt-X | exit to DOS (asks whether to hang up if online) |

The dialing directory comes with **The ARM Pit BBS 555-1989**, **ARM-DOS Host
Link 555-0100**, and two jokes: Jenny 867-5309 (always BUSY) and WOPR 399-2364
(nobody answers). Dialling shows the Procomm-style box: the modem reset and init
string (`ATZ`, `AT&C1&D2`), `ATDT5551989`, the modem's answers, attempt number,
elapsed time, last result; BUSY / NO ANSWER / NO CARRIER redial after the pause
(Space redials at once, Esc cancels). `CONNECT` beeps, starts the online timer and
records the call date/count in the directory.

## Scripts (`TERM /S:FILE.SCR`)

A small script language in the spirit of Procomm Plus ASPECT and Telix SALT
(script.c), for unattended calls. `TERM /S:GETKEEN.SCR` (also `/S=`; the file
is looked up as given, then in TERM's directory; `.SCR` is the default
extension) runs the script while the normal terminal screen stays up - the
dialing box, the BBS's ANSI screens and the transfer window all show, as when
you watched a script run in 1990. The status line says `Script GETKEEN`
instead of `Alt-Z for Help`. **Esc** asks "Stop the script (Y/N)?"; after
that (or after a script error) the terminal is interactive and Alt-X exits
with errorlevel 1.

One command per line, case-insensitive; `;` starts a comment; labels are
`name:` or `:name`; strings are in double quotes (`""` is a quote) with the
control escapes `^M` (CR) `^J` `^[` (Esc) `^^` (a caret). Every command sets
the success flag that `IF` tests.

| command | what |
|---|---|
| `MESSAGE "text"` / `TYPE "text"` | a line on the local screen (yellow), not sent |
| `CLEAR` | clear the terminal screen |
| `SET BAUDRATE n` | the UART (DTE) rate, e.g. 19200 (must be at least the line rate) |
| `SET INIT "string"` | modem init string sent after ATZ when dialling (e.g. `AT&C1&D2&B14400` asks the modem to train at 14400) |
| `SET DOWNLOAD "dir"` | download directory; `"."` or `""` = the current directory |
| `SET AUTOZMODEM ON\|OFF`, `SET ECHO ON\|OFF`, `SET DIALATTEMPTS n` | as in Alt-S |
| `DIAL "number" ["name"]` | the dialing box with redial; success = CONNECT |
| `WAITFOR "text" [seconds]` | wait (default 30 s) for text from the line, ANSI sequences ignored; the screen keeps running. Looks at what arrived since the last TRANSMIT/DIAL or the previous match |
| `TRANSMIT "text^M"` | send (paced one character per clock tick, like a typist) |
| `PAUSE n` | wait n seconds (the screen keeps running) |
| `DOWNLOAD ZMODEM` / `GETFILE ZMODEM` | receive into the download directory. If an auto-download already started during WAITFOR/PAUSE, its result is used |
| `UPLOAD ZMODEM "file"` / `SENDFILE ZMODEM "file"` | send a file |
| `HANGUP`, `BEEP`, `ALARM` | |
| `GOTO label`, `IF [NOT] SUCCESS\|FAILURE command` | |
| `EXIT [n]` | leave TERM (hanging up if online) with DOS errorlevel n (default 0) - for batch files |
| `END` (or the end of the file) | stop the script; the terminal stays |

Errors (unknown command, missing label) show `Script error line N: ...` and
stop the script. The example is apps/keen/data/GETKEEN.SCR (C:\GAMES\KEEN),
which GETKEEN.BAT uses to fetch Keen Dreams from The ARM Pit BBS:

```
        SET BAUDRATE 19200
        SET INIT "AT&C1&D2&B14400"
        DIAL "555-1989" "The ARM Pit BBS"
        IF FAILURE GOTO NOCALL
        WAITFOR "FIRST name?" 60
        TRANSMIT "GUEST^M"
        ...
        DOWNLOAD ZMODEM
        IF FAILURE GOTO NOFILE
```

## How it talks to the modem

Like the real comm programs of 1989 it programs the 16550 on COM2 itself
(lib/comm.c), not through INT 14h: divisor latch, 8N1, FIFO (trigger 8), hooks
INT 0Bh (IRQ3), unmasks IRQ3 at the PIC, sets DTR+RTS+OUT2, enables RX / THRE /
line status / modem status interrupts; the ISR moves bytes between the UART and
an 8 KB receive ring and a 4 KB transmit ring and sends EOI. Carrier = MSR DCD.
The main loop sleeps in WFI (`mcr p15 ... c7,c0,4`) when there is nothing to do.

## Terminal emulation (lib/vt.c)

ANSI-BBS / VT100 subset: CSI A B C D E F G H f d J K X L M @ P S T m s u n
(5n, 6n cursor report - BBSes use it to detect ANSI) c r h/l (?6 ?7), ESC 7 8
D E M c, SGR 0 1 2 4 5 7 8 22-28 30-37 39 40-47 49 90-97 100-107, BEL BS HT LF
VT FF CR. Deferred ("VT100 last column") wrap, so an 80-column line followed by
CR LF is one line. Form feed and `ESC[2J` clear and home. ANSI music is off
(CSI M is Delete Line). CP437 characters display as-is.

## File transfer (lib/zmodem.c, lib/xmodem.c)

Written from scratch for ARM-DOS from Chuck Forsberg's protocol descriptions
(ZMODEM 1988, YMODEM 1985-88) and Ward Christensen's XMODEM. **No lrzsz code is
used** (lrzsz is GPL-2; it only serves as the reference peer in the host test).

* ZMODEM: hex / binary CRC-16 / binary CRC-32 headers, ZDLE escaping (ESCCTL on
  request), streaming with ZCRCG, ZRPOS error recovery with shrinking subpackets
  on a noisy line, batch, file size and time, ZSKIP, ZCRC, ZCHALLENGE, ZFIN/OO,
  CAN abort both ways, auto-download (the `**`ZDLE`B00` ZRQINIT starts the
  receiver; the characters are held back so they don't litter the screen).
* Crash recovery: an aborted download keeps its partial file; the next download
  of the same file continues with `ZRPOS` at the partial length ("Resuming at
  ..."); as a sender TERM can ask for ZCRECOV.
* Transfer window: file name, size, bytes (%), CPS, time left, errors, last
  message, a progress bar; Esc aborts (sends the CAN sequence).
* YMODEM batch (block 0 with name/size/time, 1K blocks, NAK-first-EOT,
  truncation to the real size), YMODEM-G, XMODEM checksum/CRC, 128/1K.

## Tests

* `make term-zmhost-test`: lib/zmodem.c + lib/xmodem.c compiled for the build
  host and run against **lrzsz** (`sz`/`rz`/`sx`/`rx`/`sb`/`rb`) through a relay
  that can flip and drop bytes: both directions, clean and noisy lines, CRC-16
  and CRC-32, 8K subpackets, crash recovery both ways (ours->ours, ours->rz with
  ZCRECOV, `sz -r`->ours), XMODEM-1K/XMODEM/YMODEM both ways, YMODEM-G. Uses
  `sz`/`rz` from PATH or `LRZSZ=dir-of-an-lrzsz-build`; skipped if neither.
  (lrzsz 0.12.20 `sz` crashes on an empty file in CRC-16 mode; that case is left
  out.)
* `make term-test` (tests/run.mjs): TERM on an ARM-PC with the real COM2 modem
  and phone exchange (emu/dev/modem.mjs, emu/phone.mjs): help, setup (saved to
  TERM.CFG), BUSY + redial, Esc cancel, manual dial of a scripted ANSI endpoint
  (colours, cursor positioning, save/restore, erase, deferred wrap, `ESC[6n`
  answered), scrollback, capture, Alt-H; the **Host Link** (emu/hostlink.mjs, the
  page's JS ZMODEM): ZMODEM download by auto-start, Esc abort, **resume** of the
  partial file (checked byte for byte), ZMODEM upload to the Host Link, goodbye ->
  NO CARRIER, directory bookkeeping, Alt-X. Screenshots `build/term-test/term-*.png`.
* The two-machine test with the BBS is in apps/bbs (`make bbs-test`).
* `make term-script-test` (tests/script.mjs): two machines, real modem pacing:
  `TERM /S:BUSY.SCR` (dials 867-5309 once, `IF FAILURE EXIT 2` -> errorlevel 2),
  then `TERM /S:GETKEEN.SCR` from C:\GAMES\KEEN: status line, MESSAGE lines,
  dialing box with `AT&C1&D2&B14400`, `CONNECT 14400`, guest logon, Games area,
  ZMODEM window, logoff, errorlevel 0; KEENDRMS.ZIP (358,438 bytes) arrives
  byte-identical. Emulated time: **4.7 min** for the whole call at 14400
  (transfer ~250 s, ~1,380 CPS); `node apps/term/tests/script.mjs 2400` makes
  the same call at 2400: **26.6 min** (transfer ~1,540 s, ~235 CPS).
  Screenshots `build/term-test/script-*.png`.

tests/lib.mjs is shared with apps/bbs/tests (private disk images, lock-step
running of several machines, tapping what COM2 receives).

## Deviations / notes

* Not a clone of any one program: key assignments follow Procomm Plus where it
  had one (Alt-D, Alt-H, Alt-S, Alt-L, Alt-B, Alt-Z, Alt-X, PgUp/PgDn).
* No Kermit, no printer logging, no chat mode; one port at a time.
* Capture is raw (escape sequences included), which replays nicely with
  `TYPE TERM.CAP` under ANSI.SYS.

## Fast lines (docs/MODEM.md)

The port is locked at **115200** by default: the modem can train at up to 56K and the port must not be the bottleneck. The setup menu cycles 300 ... 115200, and a dialling-directory entry's baud never lowers the port below the configured speed (old TERM.DIR files say 2400). The status line shows "115200 N81". GETKEEN.SCR (`SET BAUDRATE 19200`, `&B14400`) is unaffected.

## Speech and scripted numbers (docs/MODEM.md)

* **Speech:** `ESC P speak[:wopr];text ESC \` from the other end is spoken through the Sound
  Blaster (`speech.c`, DRARM's TTS linked in; voices default and `wopr`, a monotone). Alt-V
  turns it off/on. TERM.EXE's running size is 320 KB.
* **Menus run commands:** the Alt-Z menu runs an Alt-command you press, and so does the
  dialling directory (except Alt-D).
* **Directory:** the dialling directory adds the talking clock, the 386 Fortress, the
  Floating Point and KREMVAX (TERM.DIR and the built-in default).
* **Tests:** `tests/wopr.mjs` (`make term-wopr-test`) covers the menus, WOPR with its speech
  captured and checked, the war room, Ctrl-Alt-Del mid-call, and the talking clock.
