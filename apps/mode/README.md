# MODE.COM — set and show device modes

A re-creation in C of MS-DOS 4.00's MODE (`CMD/MODE`, Microsoft, MIT
licence) with 4.00's command forms, messages and output formats, checked
against the genuine MODE 4.00 running under DOSBox-X. Portions (C) Microsoft
Corp., MIT License.

    MODE                                  status of LPT1-LPT3 and CON
    MODE device /STATUS                   status of one device (CON, COMn, LPTn)
    MODE [display][,R|L[,T]]              display: 40 80 BW40 BW80 CO40 CO80 MONO
    MODE [display],lines                  lines: 25 43 50 (needs ANSI.SYS)
    MODE CON[:] [COLS=c] [LINES=n]        c: 40 80, n: 25 43 50 (LINES needs ANSI.SYS)
    MODE CON[:] RATE=r DELAY=d            typematic rate 1-32, delay 1-4 (x 1/4 s)
    MODE COMn[:] baud[,parity[,data[,stop[,retry]]]]
    MODE COMn BAUD=b [PARITY=p] [DATA=d] [STOP=s] [RETRY=r]
    MODE LPTn[:] [cols][,[lines][,P]]     cols: 80 132, lines per inch: 6 8
    MODE LPTn [COLS=c] [LINES=l] [RETRY=r]
    MODE LPTn[:]=COMm[:]                  send printer output to a serial port
    MODE CON CP PREPARE=((cp,...) file)   code pages on the screen, with DISPLAY.SYS
    MODE CON CP SELECT=cp | REFRESH | /STATUS

Examples of what it prints (as MODE 4.00 does):

    C:\>mode com1:96
    COM1: 9600,e,7,1,-
    C:\>mode con
    Status for device CON:
    ----------------------
    COLUMNS=80
    LINES=25
    Code page operation not supported on this device

`COLUMNS=` and `LINES=` appear only when ANSI.SYS is installed (MODE asks it
with INT 2Fh AX=1A00h and gets the values through IOCTL 440Ch).
Baud rates 110 150 300 600 1200 2400 4800 9600 (or 11 15 30 60 12 24 48 96);
parity N E O; data bits 7 8; stop bits 1 2 (default 1, 2 at 110 baud);
retry E B R (or P) - the PS/2-only settings (19200 baud, mark/space parity,
5/6 data bits, 1.5 stop bits) give `Function not supported on this computer`,
as on an AT. Errors: `Invalid parameter - X`, `Invalid switch - /X`,
`Parameter format not correct - COLS=`, `Invalid number of parameters`,
`Illegal device name - COM2`, `Baud rate required`, `RATE and DELAY must be
specified together`, `ANSI.SYS must be installed to perform requested
function`, `Unable to shift screen right`.

**Resident portion.** Retry settings and printer redirection need code that
stays in memory, as in DOS 4 (`Resident portion of MODE loaded`, once): an
INT 17h hook sends LPTn output to COMm and retries a timed-out printer, an
INT 14h hook handles the serial retry options. It is 1 KB (PSP included);
MODE finds it again with INT 17h AX=DD00h. The code is in `.text.unlikely.*`
sections right after crt0 (see mode.h), MODE keeps memory up to
`mode_res_end` with INT 21h AH=31h.

## Deviations

* The ARM-PC has one serial port (COM1) and one printer port (LPT1): `MODE
  COM2...` says `Illegal device name - COM2`. The ports are found through the
  equipment word too, because the ARM-PC BIOS leaves the BIOS data area port
  table empty.
* Display adapters are found the way MODE 4.00's GET_VIDEO_INFO does it: INT 10h
  AH=1Bh (the VGA), else the EGA info call, else probing the B000h/B800h buffers.
  On the VGA machine `MONO` gives `Function not supported on this computer -
  MONO` (what MODE says on a colour-only display). With the Hercules card
  (the page's "Monitor" switch, `video: 'hercules'`) `MONO` sets the equipment
  word to 80x25 mono, mode 7 and cursor 0B0Ch, and the colour modes (`CO80`,
  `BW40`, `40`, `80`...) are refused with the same message.
* After a display mode change MODE 4.00 sets the CGA cursor shape 0607h, which
  VGA BIOSes scale to the character cell; the ARM-PC BIOS does not scale, and
  its mode set already gives the right cursor, so MODE leaves it.
* The printer: MODE 4.00 takes any of the bits A9h of the INT 17h status after
  sending a control character as an error, including "not busy" - a real
  printer is busy just after a character, the ARM-PC printer never is, so
  MODE here checks A9h without that bit (29h): `LPT1: set for 132` instead of
  `Printer error`.
* `Function not supported on this computer - %1` after a failed LINES=/COLS=
  shows the parameter (MODE 4.00 shows whatever its pointer holds, often
  garbage).
* Code pages (MODECP.ASM, added in the international round, apps/display): `MODE CON CP
  PREPARE=((cp[,cp...]) file)`, `SELECT=cp`, `REFRESH`, `/STATUS`, and the code page part of
  `MODE CON` / `MODE`, through CON's generic IOCTL (4Ch/4Dh with the font file written by IOCTL
  write, 4Ah, 6Ah, 6Bh) - with DISPLAY.SYS, MODE 4.00's output and messages; without it,
  `Code page operation not supported on this device` for status/prepare/refresh and `Device
  error during select` for SELECT (re-checked against MODE 4.00; the earlier "not supported"
  for SELECT was wrong). A missing font file is `Failure to access code page font file`
  whether or not DISPLAY.SYS is there, as MODE 4.00 opens it first. PREPARE/SELECT/REFRESH on
  a printer: `Device error during ...`, as MODE 4.00 does without PRINTER.SYS.
  apps/display/tests has the DISPLAY.SYS cases.

## Tests

`make mode-test` (`tests/run.mjs`): about 90 command lines in three boots
(plain, with the resident portion, with ANSI.SYS), standard output
redirected to files which are read back from the disk image and compared
byte for byte with the genuine MODE 4.00's output for the same command
lines; the redirection LPT1:=COM1 is checked for real (text written to PRN
comes out of COM1); the resident portion is checked to be self-contained and
loaded only once. The test shell strips the blank that COMMAND.COM leaves
before `>`, so the expected error messages have no trailing blank (MODE 4.00
shows it when it is in the command tail, and so does this MODE).
