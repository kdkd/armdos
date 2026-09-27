# KEYB.COM and KEYBOARD.SYS - international keyboard layouts

    KEYB [xx[,[yyy],[[d:][path]KEYBOARD.SYS]]] [/ID:nnn]

    C:\>keyb gr
    C:\>keyb
    Current keyboard code: GR  code page: 437
    Active code page not available from CON device

A re-creation in C of MS-DOS 4.00's KEYB (`CMD/KEYB`, Microsoft, MIT licence; Portions (C)
Microsoft Corp., MIT License), checked against the genuine KEYB 4.00 under DOSBox-X, and
MS-DOS 4.00's own **KEYBOARD.SYS**, assembled from its MIT-licensed sources.

## Layouts

`xx`: **US GR SP PO FR DK SG IT UK SF BE NL NO CF SV SU LA** (MS-DOS 4.00's set, exactly as
its KEYBOARD.SYS defines them: German, Spanish, Portuguese, French, Danish, Swiss German,
Italian, United Kingdom, Swiss French, Belgian, Dutch, Norwegian, Canadian French, Swedish,
Finnish, Latin American) plus **DV, DL, DR**: Dvorak, and Dvorak for the left and for the
right hand. **MS-DOS 4.00 had no Dvorak layout**; these are ARM-DOS's own (ANSI X4.22-1983,
and the one-handed layouts as Windows' "United States-Dvorak for left/right hand"). `/ID:`
picks a keyboard where DOS 4 had two: FR 120/189, IT 141/142, UK 166/168 (the second of
each is the default); an ID alone (`KEYB 129`) names its layout.

`yyy`: the code page the layout's characters are given in - 437 or 850 for most, 865/850
for DK and NO, 860/850 for PO, 863/850 for CF; the Dvorak layouts (ASCII only) have all five.
Without `yyy` KEYB takes the code page DISPLAY.SYS has selected on CON (INT 2Fh AD02h), or,
without DISPLAY.SYS, the layout's first code page - as KEYB 4.00: `KEYB NO` on a plain
machine loads NO's 850 tables. KEYB loads the tables for every code page prepared on CON
and switches between them when DISPLAY.SYS selects another code page (INT 2Fh AD81h), so
a German keyboard types `ß` as E1h in 437 and in 850, `ü` as 81h, `Ø` only in 850 (9Dh).

What the layouts do is DOS 4's state logic, interpreted by KEYB the way KEYBI9.ASM's
KEYB_STATE_PROCESSOR did: **dead keys** (´ ` ¨ ^ ~ and the cedilla, where the layout has
them) combine with the next letter; dead key + Space = the accent itself; dead key + a
letter it does not combine with = the accent, a beep, then the letter. **AltGr** (right
Alt) gives the third character of a key (`@` on German Q, `{ [ ] }` on 7 8 9 0). Alt and Ctrl combinations of moved letters are what they are on the US key of the
same letter (German Alt+Z sends Alt+Z's scan code whichever key is labelled Z), as KEYB's
ALT_CASE / CTRL_CASE tables say. **Ctrl+Alt+F1** switches to the US layout, **Ctrl+Alt+F2**
back.

## Messages (as KEYB 4.00)

`Current keyboard code: xx  code page: yyy`, `Current keyboard ID: nnn`, `Current CON code
page: yyy`, `KEYB has not been installed`, `Active code page not available from CON device`,
`Invalid keyboard code specified`, `Invalid keyboard ID specified`, `Invalid code page
specified`, `Bad or missing Keyboard Definition File`, `Code page specified has not been
prepared`, `One or more CON code pages invalid for given keyboard code`, `Code page requested
(437) is not valid for given keyboard code`, `Code page specified is inconsistent with the
selected code page`, `Keyboard ID specified is inconsistent with the selected keyboard
layout`, `Unable to create KEYB table in resident memory`, and the parser's `Invalid
parameter -  GRR `, `Too many parameters - X `, `Invalid switch -  /X ` (quoting the
parameter the way 4.00's parser does). Exit codes 1 (parameters), 2 (KEYBOARD.SYS), 3
(memory), 4 (CON), 5 (not prepared), 6 (code page not in the layout). KEYBOARD.SYS is looked
for as KEYB 4.00 does: the path given, else the current directory, KEYB.COM's own directory,
the root.

## How it works on the ARM-PC

* **KEYBOARD.SYS** is built by `tools/kdfasm.mjs`, a small assembler for the MASM subset of
  MS-DOS 4.00's keyboard definition sources (`kdf/KDF*.ASM`, `KEYBMAC.INC`, MIT licence;
  `kdf/LICENSE`): linked as KEYBOARD.LNK did, they give **exactly MS-DOS 4.00's KEYBOARD.SYS**
  (the test compares them when the original is at hand). ARM-DOS's file adds `KDFDV.ASM`
  (written by `tools/mkdvorak.py` in the same macro language) and `KDFARM.ASM` (DOS 4's
  KDFNOW.ASM header with the three Dvorak entries), and corrects the header's "maximum
  section size" words, which in 4.00 are smaller than several of its own sections.
* **The resident part** (kbres.c) hooks **INT 15h AH=4Fh**: the ARM-PC BIOS calls it for
  every byte from the keyboard before translating it. DOS 4's KEYB replaced the whole INT 9
  handler (a copy of the AT BIOS's with calls to its state processor); here the BIOS keeps
  the shift states, Pause, Ctrl-Break, Alt+keypad and the US translation, and KEYB takes the
  make codes its tables translate (returning CF clear), at the same points where KEYBI9C.ASM
  called KEYB_STATE_PROCESSOR. It also answers **INT 2Fh AD80h** (installed: DI = its shared
  data), **AD81h** (code page BX selected on CON) and **AD82h** (BL = 0 US, FFh national).
  Resident size: 2.7 KB of code and data plus the tables (1.5-3 KB, room for every layout
  at the number of code pages prepared when KEYB was first loaded, as in DOS 4); a later
  KEYB replaces the tables in place.
* **The page**: KEYB writes its layout and code page to the ARM-PC system board port F6h
  (ARCH.md 4.6), so the on-screen keyboard relabels its caps (web/js/pckeys.js, legends in
  web/js/kbdlayouts.js made by `tools/mklegends.mjs` from KEYBOARD.SYS through the model of
  the state processor in `tools/kbdsys.mjs`).

## Tests

`make keyb-test` (tests/run.mjs): KEYBOARD.SYS = DOS 4.00's (when available), the legends
file is up to date, the resident part is self-contained; 36 command lines without and 16
with DISPLAY.SYS against the genuine KEYB's output; for 19 layout/code page pairs every key
unshifted, shifted and with AltGr (about 145 keystrokes each, dead keys closed with Space)
must give exactly what the model of KEYB's state processor predicts from the same tables;
by hand: German `y z ü ö ä ß Ü Ä`, AltGr+Q `@`, `´`+e `é`, `` ` ``+e `è`, `´`+x = `'` x;
Ctrl+Alt+F1/F2; Alt and Ctrl on moved keys; Dvorak; port F6h.

## Deviations

* INT 15h AH=4Fh instead of a whole INT 9 handler (see above); the typewriter-style Caps
  Lock of some layouts on non-enhanced keyboards and the PCjr/P12 paths do not apply (the
  ARM-PC keyboard is an enhanced 101/102-key one, KEYB's `G_KB`).
* The "maximum size" words of KEYBOARD.SYS's header are the real maxima (a KEYB 4.00 that
  read ARM-DOS's file would reserve more, not less).
* Dvorak (DV, DL, DR) is new.

## Known issues

* The emulator's JIT: one boot that installs KEYB with some 20 layout/code page changes and
  runs a key reader after each (the typing test, in one boot) crashes on the 20th KEYB start
  (an undefined instruction at 00000014h inside the SDK's start-up code) - with the JIT
  off (`jit: false`) the same run passes, and so do shorter ones; the test runs in three
  boots (`KEYB_ONE_BOOT=1 node apps/keyb/tests/run.mjs` reproduces it; add `KEYB_NOJIT=1` to see
  it pass without the JIT).
