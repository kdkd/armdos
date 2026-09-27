# DISPLAY.SYS and EGA.CPI - code page switching for the screen

    DEVICE=C:\DOS\DISPLAY.SYS CON=(EGA,437,1)
    MODE CON CP PREPARE=((850) C:\DOS\EGA.CPI)
    MODE CON CP SELECT=850

A re-creation in C of MS-DOS 4.00's DISPLAY.SYS (`DEV/DISPLAY`, Microsoft, MIT licence;
Portions (C) Microsoft Corp., MIT License), checked against the genuine DISPLAY.SYS and MODE
4.00 under DOSBox-X, and an EGA.CPI in DOS 4.00's "FONT" file format with fonts made from
VileR's font pack.

## DISPLAY.SYS

`CON[:]=(type[,[hwcp][,n]])` or `...,(n,m))`: type **EGA** (the ARM-PC's VGA, as DOS 4 calls
it), `hwcp` the code page of the display's ROM font (437), `n` how many code pages MODE may
prepare (0-12), `m` fonts per code page (accepted). Messages as DISPLAY 4.00: `Invalid syntax
on DISPLAY.SYS code page driver` (also for no parameters), `CON code page driver cannot be
initialized` (another type; any type on the Hercules option, whose font is a ROM on the card),
`Insufficient memory` (n > 12 or no room), each with a beep; MONO and CGA quietly install
nothing. Memory: 4.3 KB, plus 6 KB for each code page it can prepare (its 8x16 and 8x8 fonts).

It is a CON device that passes all console I/O to the CON driver it was loaded over (so it
works above or below ANSI.SYS) and adds generic IOCTL category 3, as DISPLAY 4.00:

| minor | | |
|---|---|---|
| 4Ch | prepare | the code pages for the slots (-1 keeps a slot); the font file follows by IOCTL write (MODE sends EGA.CPI as it is) and is parsed as it streams in; none = refresh |
| 4Dh | end prepare | a code page that was not in the file leaves its slot empty; if the selected one was replaced, none is selected |
| 4Ah | select | loads the font, then tells KEYB (INT 2Fh AD81h); a code page KEYB has no tables for still switches the screen and fails with "keyboard" (error 8) |
| 6Ah / 6Bh | query | the selected code page (error 7 before the first select); the hardware and prepared lists |

and the INT 2Fh interface KEYB uses: AD00h installed, AD01h select BX, AD02h the selected
code page (CF and AX=1 if none), AD03h the lists, AD10h. The screen follows the selected
code page after every mode set and ROM font load (INT 10h AH=00h, AX=1101h/1102h/1104h/
1111h/1112h/1114h are followed by reloading the character generator: 8x16 for 16-line text,
rows 1-14 of the same glyphs for 14-line text - as the ARM-PC BIOS makes its own 14-line
font -, 8x8 for 43/50 lines), and INT 10h AX=1130h hands out the selected code page's fonts
instead of the ROM's, so ANSI.SYS's 43/50-line mode (built from that pointer) shows them too.
The code page on the screen is published on system board port F7h (ARCH.md 4.6) for the page.

## MODE CON CP (apps/mode)

`MODE CON CP PREPARE=((cp[,cp...]) [d:][path]file)`, `SELECT=cp`, `REFRESH`, `/STATUS`
(and `MODE CON CP`, `MODE CON`): MODE 4.00's output, e.g.

    Active code page for device CON is 850
    Hardware code pages:
      code page 437
    Prepared code pages:
      code page 850

    MODE status code page function completed

with its messages (`No code page has been selected`, `Code page not prepared`, `Current keyboard
does not support this code page`, `Device error during prepare`, `Failure to access code page
font file`, `Font file contents invalid`, ...).

## EGA.CPI

Code pages **437, 850, 860, 863, 865** (DOS 4.00's set), each with 8x16, 8x14 and 8x8 fonts, in
the layout of DOS 4.00's EGA.CPI (FFh "FONT", one information pointer, a 1Ch-byte entry per code
page with device type 1 "EGA     ", the fonts after a 6-byte code page information header).
`tools/mkcpi.mjs` packs it from `fonts/CPnnn-h.BIN`, which `tools/mkfonts.py` made once from
VileR's "Ultimate Oldschool PC Font Pack" (see fonts/README.md for the source and the CC BY-SA
4.0 licence of the font data): every character code page 437 also has keeps the IBM glyph of
the 437 font (the ROM's, emu/fonts), every other character is the "Plus" variant's glyph of the
same IBM font for that Unicode character. DISPLAY.SYS uses the 8x16 and 8x8 fonts.

## Tests

`make display-test` (tests/run.mjs): the CPI structure and fonts; the four CONFIG.SYS messages;
26 MODE CON CP command lines against MODE 4.00 with DISPLAY 4.00; and the glyphs - after SELECT=850
the character generator holds 850's 8x16 font, still after a mode set, 850's 8x8 font in 50-line
mode with ANSI.SYS, the ROM font after SELECT=437, 850 again after NLSFUNC + CHCP 850 and a
mode set - with screenshots in build/display-test/.
KEYB's and CHCP's interplay with DISPLAY.SYS is tested in apps/keyb and apps/nlsfunc.

## Deviations

* Text drawn by the BIOS in graphics modes (INT 10h AH=09h/0Eh in CGA/VGA graphics) uses the
  ROM's code page 437 font: the ARM-PC BIOS draws those from its ROM, not from the INT 43h
  vector DOS 4's DISPLAY.SYS pointed at its font.
* The 14-line font is rows 1-14 of the 8x16 glyphs (the ARM-PC BIOS's own way); EGA.CPI carries
  real 8x14 fonts all the same.
* The LCD, MONO and CGA types of DOS 4 do not exist on the ARM-PC; `CON=(EGA,,n)` (no hardware
  code page) works, where DISPLAY 4.00 could then prepare nothing.
