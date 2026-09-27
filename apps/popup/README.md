# POPUP.COM - a SideKick-style pop-up desk accessory

An original TSR for ARM-DOS, installed as `C:\DOS\POPUP.COM`.

    POPUP        install:  "POPUP installed. Press Ctrl+Alt+P to activate."
    POPUP /U     remove it again (only if nobody hooked the same vectors after it)
    POPUP /?     help

Ctrl+Alt+P opens a menu (double-line windows with shadows) over COMMAND.COM or any
80-column text-mode program; Esc puts the screen, cursor position and shape back
exactly. In a graphics or 40-column mode it refuses with an 880 Hz beep.

* **Calculator** - decimal/hexadecimal (Tab), `+ - * / % & | ^ < >` (shifts), `=`/Enter,
  `C` clear, `N` negate; shows the value in decimal (and signed), hex, binary and as an
  ARM data-processing immediate: `MOV r0,#0x0003FC00 = E3A00BFF (FF ROR 22)`, else
  `MVN` if the complement fits, else `no immediate: LDR r0,=...`.
* **Notepad** - 12 x 56, arrows/Home/End/Del/Backspace/Enter; F2 or Esc saves to
  `C:\POPUP.TXT` (loaded again the first time it opens).
* **ASCII table** - 16 x 16 with dec/hex/octal/binary and control-code names.
* **About** - resident size and address, how many million ARM instructions the machine
  executed while POPUP was resident (the ARM-PC instruction counter, ports F8h-FBh)
  and how often its hooks ran.

How it stays resident, as 1980s TSRs did:

| vector | why |
|---|---|
| INT 15h AH=4Fh | keyboard intercept: sees P with Ctrl+Alt, swallows it, sets the hot-key flag |
| INT 09h | after the BIOS handled the key: pop up at once if safe |
| INT 08h | timer: pop up as soon as it becomes safe; ends the beep |
| INT 28h | DOS idle (COMMAND.COM at the prompt, InDOS = 1): pop up from inside DOS |
| INT 10h, 13h | busy counters - never interrupt the BIOS video/disk code |
| INT 2Fh AH=C5h | installation check (second copy refuses) and `/U` |

"Safe" = InDOS flag 0 (INT 21h AH=34h) or the INT 28h path, the critical-error flag
(the byte before InDOS) 0, and neither INT 10h nor INT 13h active. The pop-up runs on
its own 3 KB stack (the install-time stack area, switched in `popasm.S`) with IRQs
enabled, in SVC mode inside the interrupt, and uses INT 16h/INT 10h/INT 21h like any
TSR. It frees its environment and keeps PSP + code + data + stack: **19,760 bytes**
resident. It is freestanding C (no newlib: its own `__armdos_start`), so the SDK's
start-up is not linked.

Tests: `make popup-test` - install, second copy, pop-up at the prompt (INT 28h path),
calculator encodings, notepad save and file content, ASCII table, About, exact screen
restore, over ARMINFO (INT 09h path, InDOS 0), ARMINFO listing POPUP's hooked vectors,
beep and no pop-up in mode 13h (DEMO), `/U` restoring all seven vectors, reinstall.
Screenshots in `build/showcase-test/popup/`.

Deviations from SideKick: much smaller feature set; the hot key is fixed.
