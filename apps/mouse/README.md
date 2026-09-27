# MOUSE.COM — the mouse driver

    MOUSE          install the driver and stay resident
    MOUSE OFF      remove it again

A Microsoft-compatible mouse driver: it provides INT 33h for programs,
getting the PS/2 mouse through the BIOS pointing-device services (INT 15h
AX=C200h enable, C201h reset, C205h initialise, C207h handler - the BIOS
calls the driver's handler with the status byte and the motion for every
packet). Written for ARM-DOS; no Microsoft source (the Microsoft mouse driver
was never part of MS-DOS 4.00). Banner:

    ARM-DOS Mouse Driver Version 1.00
    Copyright (C) Europa Micro Systems 1988.  All rights reserved.
    Mouse driver installed

or `Mouse driver already installed`, `Mouse not found` / `Driver not
installed`, `Mouse driver removed`.

Resident size: 5.2 KB (the PSP, the self-relocating .COM prologue, crt0's
entry and the resident part; the installer and the C library are dropped,
the environment is freed). The resident part is in `.text.unlikely.*`
sections, which the SDK link script places right after crt0; `mend.S` marks
its end, INT 21h AH=31h keeps the memory up to it.

## INT 33h

Registers as ARCH.md maps them (AX=r0 ... DI=r5, ES:DX = a flat pointer in DX).

| AX | function |
|---|---|
| 0000h, 0021h | reset: AX=FFFFh, BX=2 buttons; cursor hidden, centred, full-screen range, default cursors, ratio 8/16, no handler |
| 0001h / 0002h | show / hide the cursor (show/hide counter, as Microsoft's) |
| 0003h | BX = buttons (bit 0 left, 1 right, 2 middle), CX, DX = position |
| 0004h | set the position |
| 0005h / 0006h | BX = button: press / release count since the last call and the position of the last one; AX = buttons |
| 0007h / 0008h | horizontal / vertical range CX..DX |
| 0009h | graphics cursor: BX, CX hot spot, ES:DX 16 screen-mask words + 16 cursor-mask words |
| 000Ah | text cursor: BX=0 software, CX screen mask, DX cursor mask (BX=1, the hardware cursor, is drawn as the software one) |
| 000Bh | CX, DX = motion counters (mickeys) since the last call |
| 000Ch | event handler: CX = event mask, ES:DX = the handler |
| 000Fh | mickeys per 8 pixels (default 8 horizontal, 16 vertical) |
| 0010h | conditional off (hides the cursor) |
| 0013h | double-speed threshold (stored) |
| 0014h | swap event handlers |
| 0015h-0017h | state size / save / restore |
| 001Ah / 001Bh | sensitivity (default 50 = x1) |
| 001Dh / 001Eh | display page (stored) |
| 001Fh / 0020h | disable / enable the driver |
| 0024h | version: BX=0100h (1.00), CH=4 (PS/2), CL=0 |
| 0026h | maximum virtual coordinates |

The event handler is called as an ARM "far call" entry (ARCH.md 15): AX
(r0) = the events that occurred (bit 0 moved, 1/2 left pressed/released, 3/4
right, 5/6 middle) and are in its mask, BX = buttons, CX, DX = position, SI,
DI = the mickey counts; it runs in the mouse interrupt (IRQs off), may
clobber anything but sp and r7-r11 and returns with `bx lr`. A C function
`void handler(unsigned ax, unsigned bx, unsigned cx, unsigned dx)` works.

Coordinates are Microsoft's virtual screen: 640 x 200 (640 x lines*8 in text
modes with more lines); text positions are multiples of 8 (16 in 40-column
modes), 320-pixel modes report even x. In text modes the cursor is the
software cursor (the default masks invert the character's colours); in modes
4, 5, 6 and 13h a 16x16 graphics cursor (the default is the arrow). What the
cursor covers is saved and restored - only if the program has not written
over the cursor meanwhile, so a program that forgets to hide the cursor does
not get stale characters back. A video mode set (INT 10h AH=00h, which the
driver hooks) forgets the saved background.

## Deviations

* No double-speed acceleration (AX=0013h is accepted and stored), no light pen
  emulation, no hardware text cursor, no mouse-cursor clipping (AX=0010h just
  hides), one display page.

## Tests

`make mouse-test` (`tests/run.mjs`, with `tests/mtest.c`): the resident part
is self-contained; install banner; reset, version and position; the text
cursor inverts the cell under it and moves with the mouse (moved through the
emulated 8042), the old cell comes back; a left click is seen by AX=0005h and
the event handler, positions and mickeys are right; in mode 13h the arrow is
drawn with its tip at the position and the background restored after
moving; a right click; a second MOUSE says "already installed"; MOUSE OFF
removes the driver and gives all the memory back (resident size checked).
