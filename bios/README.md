# bios/ - the ARM-PC ROM BIOS

`build/rom.bin` (bios.mk): POST, SETUP, and the PC/AT BIOS services (INT 10h-1Ah)
for the ARM926 machine. The machine-level contract
(memory map, ports, the SWI/interrupt convention) is ARCH.md; this file lists the
parts that go beyond a plain AT BIOS.

| file | contents |
|---|---|
| start.S, rom.ld | reset vector, exception vectors, the ROM layout |
| post.c | POST: memory sizing, device detection, the banner and configuration box; INT 11h, 12h, 14h, 17h, 18h, 19h |
| setup.c | the SETUP screen |
| video.c, font.S | INT 10h, display detection |
| disk.c | INT 13h (diskette, IDE hard disk) |
| kbd.c | INT 09h (IRQ 1), INT 16h |
| timer.c | IRQ 0, INT 1Ah, INT 15h, CMOS, speaker |
| lib.c, bios.h | helpers, shared definitions |

## Display cards

* **Detection at POST** (`video_detect`): a 6845 that reads back its cursor register at
  3D4h is the VGA; one at 3B4h is a monochrome card, a Hercules if status bit 7 (3BAh)
  toggles within 3 timer ticks. The equipment word gets bits 4-5 = 11 on a mono card.
* **Mono card**: every mode set is mode 7 (as the IBM BIOS does when the equipment word
  says mono), text at B0000h (`MDAMEM`), CRTC at 3B4h. AH=05h, 0Bh, 10h (except blink),
  11h (except AL=30h), 12h, 1Ah and 1Bh are absent, as on a PC whose only card is an
  MDA/HGC. Hercules graphics have no BIOS mode (programs program the card). POST screens
  and SETUP use normal/bright/reverse video there.
* **VGA planar modes** 0Dh, 0Eh, 10h and 12h, with pixel (AH=0Ch/0Dh), text and scrolling
  support. Writing a mode number to the ARM-PC mode register 3E0h makes the emulated card
  load the IBM register set for that mode, which programs may then modify (e.g. mode 13h
  -> Mode X).
* **Mode 62h**: 640x480, 256 colours, linear frame buffer at A0000h-EAFFFh (pixel (x, y)
  at `0xA0000 + 640*y + x`), 80x30 text in 8x16 cells; used by GEM (`GEM /V`). Any other
  mode set gives the adapter hole back.

## Other devices

* **Memory sizing**: installed RAM (the SIMMs, 1-16 MB) is probed in 64 KB steps above the
  HMA; the result (`ram_end`) drives the memory test, the configuration box, INT 15h
  AH=88h and AX=E801h, and CMOS 30h/31h. INT 12h stays 640 KB.
* **Device detection**: IDE secondary master (the CD-ROM drive, IDENTIFY PACKET DEVICE),
  COM1/COM2 by their scratch registers (the BDA COM table and the equipment word list
  only the ports that answer), the sound card and MPU-401 (for the configuration box only),
  the game port (equipment bit 12 when 201h reads anything but FFh).
* **INT 14h** as the PC/AT BIOS does it, for any port in the BDA (DX = 0 COM1, 1 COM2):
  init (110-9600 baud), send, receive, status, with the timeouts at BDA 40:7C.
* **INT 15h AH=84h** (joystick): DX=0 buttons, DX=1 the four resistive inputs in the AT's
  units, measured with the AT BIOS's TEST_CORD method; CF set, AH=86h without a game port.
* **INT 15h AH=C2h** (PS/2 mouse) fails with AH=03h when POST found no pointing device.
* **INT 13h AH=16h** reports a diskette change (the FDC's change bit is latched when read).

Tests: emu/tests/machine/ (hardware.mjs, vga-planar.mjs, vga62.mjs), apps/joytest,
apps/gem, apps/hercules.
