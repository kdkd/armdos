# ARM-DOS 4.00 — architecture contract

ARM-DOS is an **IBM-PC-compatible personal computer that never existed: a PC/AT
whose CPU is an ARM926EJ-S (ARMv5TE)**, running a from-scratch re-creation of
MS-DOS 4.00 (whose source Microsoft released under the MIT license) that has been
"ported" to ARM. Everything the user sees is produced by real ARM machine code
executing in an emulator in the browser: the BIOS POST, the DOS kernel,
COMMAND.COM, and every program on the disk.

The design rule is **"what would IBM and Microsoft have done in 1988 if the PC had
shipped with an ARM?"** Keep every PC/DOS interface that makes sense (I/O port
numbers, the interrupt-vector table at address 0, the BIOS data area at 0x400,
the text buffer at 0xB8000, INT 21h function numbers, PSPs, MCBs, FAT, the EXE
"MZ" signature) and change only what the CPU forces to change (registers, the
instruction that raises an interrupt, segment:offset becoming flat pointers).

This file is the contract between components. If you need to change it, change
it here first and say why.

---

## 1. Directory layout

```
ARCH.md            this file
build.sh           checks the tools, fetches 3rdparty/, runs make, stages public_html/
Makefile           builds everything (ROM, kernel, COMMAND, apps, disk images)
emu/               the machine, in JavaScript (runs in browser AND node)
  cpu.mjs          ARMv5TE core (ARM + Thumb), fast interpreter
  machine.mjs      bus, memory map, device wiring, run loop, timing
  dev/*.mjs        devices (pic, pit, kbc, vga, fdc, ata, cmos, uart, sys, dma, sb16 + opl3, audio)
  headless.mjs     node runner: boot, type keys, dump screen text / PNG
bios/              ROM BIOS (C + ARM asm) -> build/rom.bin
sdk/               crt0, libc glue (newlib), dos.h/conio.h/etc, linker scripts, elf2exe
kernel/            IO.SYS + ARMDOS.SYS (C + asm)
command/           COMMAND.COM (C)
apps/<name>/       one directory per program on the disk
disk/              disk image builder + the files that go on the disks
web/               the web page (index.html, js, css, fonts, assets) and the manual (web/docs)
docs/              design notes (MODEM.md)
tools/             build.sh helpers: fetch-3rdparty.sh (third-party data), smoke.py (site check)
3rdparty/          third-party data fetched by tools/fetch-3rdparty.sh (only manifest.json is tracked)
build/             build outputs (gitignored)
public_html/       the staged web site made by ./build.sh (gitignored)
```

## 2. The CPU

* **ARMv5TE, ARM926EJ-S flavoured**: full ARM instruction set incl. v5TE (CLZ, BLX,
  LDRD/STRD, PLD (nop), QADD/QSUB/QDADD/QDSUB, SMULxy/SMLAxy/SMLAWy/SMULWy/SMLALxy),
  long multiplies, halfword/signed loads, SWP/SWPB, MRS/MSR, LDM/STM with all
  forms incl. user-bank (`^`) and exception return, and **Thumb** (v5T: BLX, BX).
  Jazelle is not implemented (BXJ behaves as BX).
* **VFP9-S floating-point coprocessor (VFPv2)**, as ARM926EJ-S systems could be fitted: FPSID 0x41011090; disabled at reset (FPEXC.EN=0, every VFP instruction undefined) and enabled by the BIOS at reset. The system software (BIOS, kernel, drivers, TSRs) is soft-float and never touches VFP state, so interrupt handlers don't save it. Programs opt in with `-mfpu=vfp -mfloat-abi=softfp` (newlib multilib arm/v5te/softfp); everything else stays `-mfloat-abi=soft`.
* **No MMU.** CP15 exists minimally: c0 ID register reads `0x41069265`
  (ARM926EJ-S r0p5), c1 control register (bit 13 = V, high vectors; reset value
  has V=1), `mcr p15,0,rX,c7,c0,4` = **wait for interrupt** (the CPU sleeps until
  an IRQ is pending — DOS uses this in its idle loops so the browser tab doesn't
  burn CPU), cache ops are accepted and ignored.
* Unaligned LDR rotates (ARMv5 semantics). Access to unmapped memory -> data
  abort (the BIOS reports it on screen like a "General Protection" style crash
  screen, with registers).
* **Reset**: SVC mode, IRQ+FIQ disabled, PC = 0xFFFF0000 (high vectors).

## 3. Memory map (physical, flat, little-endian)

| range | what |
|---|---|
| `0x00000000-0x000003FF` | **Interrupt vector table**: 256 x 32-bit handler addresses, vector *n* at `n*4` (exactly where a PC keeps it) |
| `0x00000400-0x000004FF` | **BIOS data area** (PC offsets: 0x10 equipment word, 0x13 memory KB, 0x17 shift flags, 0x1A/0x1C/0x1E kbd buffer, 0x49 video mode, 0x4A columns, 0x4C page size, 0x50 cursor pos page 0, 0x60 cursor shape, 0x62 page, 0x63 CRTC port, 0x6C tick count (u32), 0x70 midnight flag, 0x84 rows-1, 0x85 char height) |
| `0x00000500-0x0009FFFF` | **conventional memory** (640 KB). DOS kernel, buffers, MCB arena, programs. `0x7C00` is where the BIOS loads a boot sector. |
| `0x000A0000-0x000BFFFF` | **video RAM**: 0xA0000 VGA mode 13h, 0xB8000 text / CGA (mode 62h: 0xA0000-0xEAFFF, see §6) |
| `0x000C0000-0x000FFFFF` | reserved "adapter/ROM" hole (reads 0xFF, writes ignored); in video mode 62h the VGA decodes `0xC0000-0xEAFFF` as the rest of its frame buffer |
| `0x00100000-0x0010FFFF` | the first 64 KB of extended memory ("HMA"): **owned by the BIOS** (its .data, .bss and stacks, bios/rom.ld). Never handed out; INT 15h AH=88h does not count it |
| `0x00110000-0x00FFFFFF` | **extended memory** (15,296 KB with the factory 16 MB of SIMMs). Handed out by HIMEM.SYS (XMS), which hooks INT 15h AH=88h to report 0 KB once installed. Installed RAM is configurable (1/2/4/8/16 MB, "Open the box"): RAM ends at min(16 MB, installed + 384K), because the 384K behind the adapter hole is relocated above 1 MB as AT chipsets did; above that the bus is empty (reads FFh, writes ignored, DMA included) |
| `0x10000000-0x1000FFFF` | **ISA I/O space**: x86 port *p* is at `0x10000000 + p`. Byte ports are byte-accessed; the ATA data port 0x1F0 also supports 16-bit access. |
| `0x11000000-0x11001FFF` | **VGA character generator RAM**: 256 glyphs x 32 bytes (row bytes, only the first `char height` used). Loaded by the BIOS from its ROM font on every text mode set; programs may overwrite (INT 10h AX=1100h). DISPLAY.SYS keeps the selected code page's font there (apps/display, §4.6). |
| `0xFFF00000-0xFFFFFFFF` | **BIOS ROM** (1 MB, read-only). The ARM exception vectors are at `0xFFFF0000` (high vectors). |

## 4. I/O ports (at `0x10000000 + port`)

Only these exist; everything else reads `0xFF`, writes ignored.

| port | device | behaviour |
|---|---|---|
| `0x20,0x21` | **PIC** (8259-ish, 16 lines, one controller) | write `0x20` to port 0x20 = EOI (clears the highest in-service IRQ). Port 0x21 = mask for IRQ0-7 (bit set = masked), port **0xA1** = mask for IRQ8-15 (0xA0 accepts EOI too). **Read port 0x20** = the number of the highest-priority *pending unmasked* IRQ (0-15), or 0xFF if none, and **marks it in-service** (acknowledge). IRQ0 highest priority. IRQ lines: 0 PIT, 1 keyboard, 4 COM1, 6 floppy, 7 Sound Blaster 16, 8 RTC, 9 MPU-401, 12 PS/2 mouse, 14 ATA. The PIC drives the CPU's IRQ input while any unmasked, not-in-service IRQ of higher priority than the current in-service one is pending. |
| `0x40-0x43` | **PIT 8253/8254**, 1.193182 MHz input | channel 0 -> IRQ0 (modes 2 and 3 at least, lobyte/hibyte and latch commands, divisor 0 = 65536, default 18.2 Hz), channel 2 -> PC speaker. Counter reads work (programs use them for fine timing). |
| `0x60,0x64` | **8042 keyboard controller** | 0x60 read = next byte (scan code **set 1**, XT codes incl. 0xE0 prefixes, break = make\|0x80). 0x64 read = status (bit0 output buffer full, bit5 = byte is from aux/mouse). Keyboard bytes raise IRQ1, mouse bytes IRQ12. Command 0xD4 to 0x64 then byte to 0x60 = send to mouse (0xF4 enable -> ack 0xFA). Mouse sends standard 3-byte PS/2 packets. |
| `0x61` | **system control port B** | bit0 = PIT ch2 gate, bit1 = speaker data enable. Speaker sounds when both set, at ch2's frequency. |
| `0x70,0x71` | **CMOS/RTC** (MC146818) | registers 0x00-0x09 time/date in **BCD** from the host clock (register 0x0B bit 2 = 0 -> BCD, bit1 = 1 -> 24h), 0x0A bit 7 update-in-progress (always 0), 0x32 century (BCD). Writes to time registers set an offset from host time (so DATE/TIME can set the clock). 0x0E-0x7F = 114 bytes of battery RAM (persisted by the page in localStorage) used by BIOS SETUP. |
| `0x1F0-0x1F7`, `0x3F6` | **ATA primary master** = hard disk (drive C:); optional **primary slave** = second hard disk (drive D:, the page's keep-forever disk) | 0x1F6 bit 4 selects the slave (both drives latch the task file; the selected one answers; with no slave its status reads 0). A real enough ATA PIO device: LBA28, 0x1F2 sector count, 0x1F3-0x1F6 LBA (0x1F6 bit 6 = LBA, bits 0-3 = LBA 24-27), 0x1F7 command: `0x20` READ SECTORS, `0x30` WRITE SECTORS, `0xEC` IDENTIFY, `0xE7` flush. Status bits BSY 0x80, DRDY 0x40, DRQ 0x08, ERR 0x01. Data at 0x1F0 (16-bit reads/writes, or 8-bit which transfers one byte). No IRQ needed (BIOS polls); IRQ14 raised if nIEN (0x3F6 bit1) is clear. |
| `0x300-0x307` | **floppy controller** (drive A:, ARM-PC simplified FDC with DMA, on the old IBM "prototype card" port range) | NOT an NEC 765 — see §4.1 |
| `0x3F8-0x3FF` | **COM1** 16550-ish UART | THR/RBR at 0x3F8, LSR at 0x3FD (bit5 THR empty always 1, bit0 data ready). In the browser the serial line is shown in a "serial console" panel; in node it goes to stdout. |
| `0xE9` | **debug console** (the Bochs "port E9 hack") | a byte written here is logged to the host console immediately. |
| `0x3C0-0x3DF` | **VGA** | see §6 |
| `0x00-0x0F`, `0x81-0x8F`, `0xC0-0xDF`, `0x481-0x48B` | **8237 DMA controllers** (channels 0-3 8-bit, 5-7 16-bit), page registers, EISA-style high page registers | see §4.2 |
| `0x220-0x22F` | **Sound Blaster 16** (DSP 4.05, CT1745 mixer, OPL3 at 0x220-0x223 and 0x228-0x229), IRQ 7, DMA 1 and 5 | see §4.3 |
| `0x388-0x38B` | **OPL3 FM synthesizer** (YMF262; AdLib-compatible at 0x388/0x389) | see §4.3 |
| `0x330-0x331` | **MPU-401** MIDI interface (UART mode) with a General MIDI synthesizer behind it, IRQ 9 | see §4.3.1 |
| `0x201` | **game port** (IBM game control adapter: on the SB16 and on the multi-I/O card) | see §4.5 |
| `0xF0-0xFF` | **ARM-PC system board** | `0xF0` (read) = board ID 'A'; `0xF1` = CPU nominal clock in MHz (read, u8); `0xF2` = turbo state (read/write: bit 0 = turbo, bit 1 = **clock limiter off**, TURBO MAX: the real-time driver then raises the clock frame by frame while the host has time to spare, turbo clock to 999 MHz, while emulated time keeps following the wall clock; a reset or the TURBO button clears bit 1; `apps/turbo`); `0xF4` write = **exit emulator** with that code (headless tests); `0xF5` = **alt-CPU LED**: write 1 while x86 code runs under ELBOW, 0 when back in ARM code (read returns the state; the page shows the duty cycle on an amber X86 LED); `0xF8-0xFB` read = instructions executed / 1M (u32, snapshot on reading 0xF8); `0xF6` = **keyboard layout mailbox** and `0xF7` = **screen code page mailbox** (read/write latches for the web page, §4.6); `0xFC-0xFF` = **ELBOW descriptor address** (u32 latch, read/write, for the page's ELBOW view, §4.7). |

### 4.1 Floppy controller (simplified, DMA)

| port | R/W | meaning |
|---|---|---|
| `0x300-0x303` | W | DMA address (byte 0 = bits 0-7 ... byte 3 = bits 24-31) |
| `0x304` | W | sector count (1-255) |
| `0x305` | W | LBA bits 0-7 |
| `0x306` | W | LBA bits 8-15 |
| `0x307` | W | command: `0x01` read, `0x02` write, `0x03` reset/recalibrate, `0x04` get media (fills status) |
| `0x307` | R | status: bit7 busy, bit6 disk present, bit5 disk changed since last command (cleared by read), bit4 write protected, bit0 error |
| `0x302` | R | media type: 0 none, 1 = 360K, 2 = 1.2M, 3 = 720K, 4 = 1.44M |

Transfers complete instantly as far as the CPU sees (busy is never observed
set); the page animates the drive LED and plays seek/step sounds proportionally
to the head movement (cylinder = LBA / 36 for 1.44 MB). IRQ6 is raised at
completion (the BIOS polls and ignores it; it is masked by default).

### 4.2 DMA controllers (8237A pair, as on the AT)

DMA1 = channels 0-3 (byte transfers, ports 0x00-0x0F), DMA2 = channels 4-7 (word
transfers, ports 0xC0-0xDE, even ports; odd ports alias), channel 4 = cascade.
Per channel: base/current address and count (lo/hi through the byte flip-flop,
0x0C / 0xD8 clears it; reads return the *current* values), mode register (0x0B /
0xD6: single/demand/block treated alike - the device paces the transfer -, bit 4
auto-initialise, bit 5 address decrement, bits 2-3 direction), single mask (0x0A /
0xD4), mask-all (0x0F / 0xDE), clear-masks (0x0E / 0xDC), master clear (0x0D /
0xDA), status (0x08 / 0xD0: bits 0-3 terminal count, cleared on read). Page
registers: 0x87 ch0, 0x83 ch1, 0x81 ch2, 0x82 ch3, 0x8B ch5, 0x89 ch6, 0x8A ch7
(0x80 stays the POST-code latch; the other 0x8x ports are scratch bytes).

* 8-bit channel: physical address = `page << 16 | address` (wraps inside its 64 KB
  page); 16-bit channel: `(page & 0xFE) << 16 | address << 1` (128 KB page, count in
  words). All ARM-PC RAM is below 16 MB, so **ISA DMA reaches every RAM address** -
  buffers in extended memory (the XMS heap) work.
* **ARM-PC extension (EISA-compatible): high page registers** 0x487/0x483/0x481/
  0x482/0x48B/0x489/0x48A (= low page port + 0x400) hold address bits 24-31. Writing
  a low page register clears the high page, so ISA software that never touches them
  gets plain 24-bit addresses. (Nothing but RAM is DMA-able on this machine; the
  registers exist so 32-bit-clean drivers can be written.) Non-RAM addresses read
  0xFF and ignore writes.
* A single-cycle channel that reaches terminal count masks itself; auto-init
  reloads base address and count.
* The Sound Blaster is the only 8237 user; the floppy controller (§4.1) is a bus
  master with its own address registers.

### 4.3 Sound: Sound Blaster 16 + OPL3

**The ARM-PC's standard sound card is a Sound Blaster 16 at 220h, IRQ 7, 8-bit DMA
1, 16-bit DMA 5**, and the default C:\AUTOEXEC.BAT says so with
`SET BLASTER=A220 I7 D1 H5 T6`, followed by `SBMIX /INIT /Q` (the card's setup
step: master, voice and FM to 0 dB - the CT1745 powers up at -14 dB, as a real
SB16 does, and DIAGNOSE/MIXERSET used to fix that at boot).

**Why IRQ 7, not the usual 5:** hardware IRQs n = 0-7 arrive as INT 08h+n (§5), and on
the ARM-PC **INT 0Dh is also the data-abort exception** (and INT 0Eh the prefetch
abort; the floppy's IRQ 6 shares it - harmless, it stays masked). An SB on IRQ 5
would have its handler called for data aborts too. IRQ 7 = INT 0Fh collides with
nothing (the ARM-PC's LPT1 does not use IRQ 7). Programs must take the IRQ from
BLASTER, as DOS programs always should; IRQ 5/10 still work if selected through
mixer register 80h, at the programmer's risk.

* **DSP** (base+6 reset: write 1 then 0, 0AAh is readable 20 us later; +0Ah read data;
  +0Ch command/data write, read bit 7 = busy (never busy); +0Eh bit 7 = data available,
  reading it acknowledges the 8-bit IRQ; +0Fh acknowledges the 16-bit IRQ). Version
  **4.05** (E1h). Commands: 10h direct DAC; 14h/1Ch 8-bit single/auto-init output,
  90h/91h high-speed (left only by a reset); 24h/2Ch/98h/99h input (records
  silence); 40h time constant; 41h/42h sample rate; 48h block size; 80h silence;
  **Bxh/Cxh** SB16 16-bit/8-bit transfers (bit 3 input, bit 2 auto-init, mode byte
  bit 4 signed, bit 5 stereo, count = samples - 1 with both stereo channels
  counted); D0h/D4h, D5h/D6h pause/continue; D1h/D3h/D8h speaker; D9h/DAh exit
  auto-init after the current block; 45h/47h continue auto-init; E0h invert; E3h
  copyright string; E4h/E8h test register; F2h/F3h raise the 8/16-bit IRQ; the ASP
  commands 04h/05h/0Eh/0Fh are accepted. ADPCM commands play their data as 8-bit PCM.
  8-bit data is unsigned unless the mode byte says signed; 16-bit likewise.
* **Timing**: a transfer consumes one frame per 1/rate s of *emulated* time through the
  8237 and raises its IRQ at the exact emulated time the block ends (so the guest's
  double buffering behaves as on hardware: the half that just played is free when the
  IRQ comes). A masked or finished DMA channel yields silence; the DSP keeps its time.
* **Mixer** (base+4 index, base+5 data), CT1745: 00h reset; 30h-35h master/voice/FM
  left/right (bits 7-3, 2 dB steps, 31 = 0 dB, power-on 24); 36h-3Bh CD/line/mic/PC
  speaker, 3Ch-47h switches/gains/tone (stored); SB Pro aliases 04h/22h/26h/28h/2Eh/0Ah;
  0Eh bit 1 = SB Pro stereo for the 8-bit legacy commands; **80h IRQ select**
  (bit 2 = IRQ 7), **81h DMA select** (bits 1 and 5 = DMA 1 and 5), **82h IRQ status**
  (bit 0 8-bit, bit 1 16-bit). Master x voice apply to PCM, master x FM to the OPL3.
* **OPL3** (YMF262, emulated by a port of Nuked OPL3, LGPL-2.1): 388h/389h = address/
  data of register array 0, 38Ah/38Bh array 1 (also at base+0..3, and base+8/9 = array
  0). Reading 388h (base+0, base+8) returns the status: bit 7 IRQ, bit 6 timer 1, bit 5
  timer 2 overflowed; bits 1-2 are 0 (an OPL2 has 06h there: that is how programs tell
  an OPL3). Timers 1 (80 us) and 2 (320 us) run in emulated time (registers 02h, 03h,
  04h), so the classic AdLib detection (start timer 1 at FFh, wait 80 us, expect
  status & E0h = C0h) works - wait by time (e.g. port 61h's 15 us refresh toggle),
  not by counting port reads: an ISA I/O access costs one instruction here, not 1 us.
  The OPL's timer IRQ is not wired (as on the SB16). The chip runs at 49716 Hz.
* No CD interface on the card. Its game port is §4.5.

### 4.3.1 MPU-401 and the General MIDI synthesizer

A **Roland MPU-401 at 330h (data 330h, status/command 331h), IRQ 9 (INT 71h)** with a
General MIDI / GS synthesizer on it - the ARM-PC's "Sound Canvas" (a Wave Blaster-style
daughterboard of the SB16). With it the BLASTER line gets its P field:
`SET BLASTER=A220 I7 D1 H5 P330 T6`. (Why IRQ 9: free, and IRQ 8-15 arrive as INT 70h+n
without any exception vector sharing, §5; programs normally poll the MPU anyway.)

* **Status (331h read)**: bit 7 = 0 when a byte waits at 330h, bit 6 = 0 when ready for a
  command/data byte (always ready), other bits 1 (so BFh when idle).
* **Commands (331h write)**, intelligent mode (power-on, reset): FFh reset -> ACK FEh at
  330h; 3Fh UART mode -> FEh; ACh -> FEh 15h (version), ADh -> FEh 01h; any other -> FEh
  (the sequencer/timer of the intelligent mode is not emulated). In UART mode only FFh is
  a command (back to intelligent mode, no ACK - as a real MPU-401); 330h writes are MIDI.
  IRQ 9 is raised while an ACK waits to be read.
* **The synthesizer** (emu/dev/gmsynth.mjs): a SoundFont 2 player with the ARM-PC GS sound
  set (derived from GeneralUser GS, apps/midi/sf/): 128 GM programs, the GS variation
  banks and 13 drum kits, channel 10 drums, 64 voices, reverb and chorus, GM/GS/XG reset
  SysEx. MIDI bytes are rendered at the emulated time they were written, mixed with the SB
  through the CT1745's master and FM ("MIDI") volumes.
* The web page lets the visitor switch the synthesizer off; the MPU-401 is then absent
  (both ports read FFh) and programs fall back to FM. SDK: sdk/include/midi.h.

### 4.4 Audio output

The emulator renders the machine's sound in emulated time (emu/dev/audio.mjs): the
DAC's sample stream and the OPL3 (register writes applied at the emulated time they
were made), resampled to the host rate, plus optionally the PC speaker. The web page
plays it through an AudioWorklet with a ~60 ms queue that absorbs drift; headless
runs can record it (`--wav`). The MPU-401's MIDI bytes are rendered by the GM
synthesizer in the same emulated time (in node inside emu/dev/audio.mjs; on the
page inside the AudioWorklet, the bytes travelling with the audio chunks).

### 4.5 Game port (joysticks)

The IBM game control adapter at **201h**: four NE558 one-shots timing four potentiometers (two
joysticks x two axes, 0-100 kOhm) and four buttons. It is on the Sound Blaster 16 and on the
multi-I/O card (jumper J2; "open the box"); the machine option `joystick` (default on) says
whether 201h answers at all (emu/dev/gameport.mjs, `m.joy`).

* **Write** (any value): fires the four one-shots. Each axis bit goes to 1 for **24.2 us +
  0.011 us per ohm** (IBM's figure: 24.2-1124.2 us); a one-shot still timing ignores the fire
  (the 558 is not retriggerable). An axis with nothing plugged in never ends its pulse.
* **Read**: bits 0-3 = one-shots still timing (A-X, A-Y, B-X, B-Y), bits 4-7 = buttons A1, A2,
  B1, B2 (**0 = pressed**).
* **Every access to 201h takes 1 us of emulated time** (an 8-bit ISA I/O cycle; the only port
  with a modelled wait state). Counting loops therefore measure the pulse in bus cycles, as on
  a PC: "fire, then IN/TEST/count until the bit drops" gives about 20-1100 per axis at any CPU
  clock, the range DOS games were written for. Measure with emulated time, not a fixed loop
  count from a faster machine.
* BIOS: equipment word **bit 12** (game adapter) when 201h answers at POST; **INT 15h AH=84h**:
  DX=0 -> AL bits 4-7 = the buttons (as read, 0 = pressed); DX=1 -> AX, BX, CX, DX = A(x),
  A(y), B(x), B(y) in the AT BIOS's units (PIT clocks / 8, 6.704 us: ~3-168, centre ~86; 0 =
  nothing plugged in). CF set, AH=86h without a game adapter or for other DX.

### 4.6 Keyboard layout and code page mailbox (ports F6h/F7h)

Two byte latches on the system board that the machine exposes to its front end (the web
page's on-screen keyboard, emu/machine.mjs `m.nls = { keyb, cp }`); DOS programs only write
them. Both read back what was written; a machine reset clears them, and IO.SYS writes 0 to
both at the start of CONFIG.SYS processing (a warm boot forgets the layout too).

* **F6h — the keyboard layout KEYB.COM has active** (apps/keyb). Written by KEYB when it
  installs or changes layout, when DISPLAY.SYS switches code page (INT 2Fh AD81h), and on
  Ctrl+Alt+F1 / Ctrl+Alt+F2. Bits 0-4 = layout, bits 5-7 = the code page of KEYB's active
  tables (the index below). **0 = US**: no KEYB, KEYB US, or KEYB switched to US with
  Ctrl+Alt+F1. Layouts: 1 GR, 2 SP, 3 PO, 4 FR (ID 189), 5 DK, 6 SG, 7 IT (ID 141), 8 UK
  (ID 166), 9 SF, 10 BE, 11 NL, 12 NO, 13 CF, 14 SV, 15 SU, 16 LA, 17 DV (Dvorak), 18 DL
  (Dvorak, left hand), 19 DR (Dvorak, right hand), 20 FR /ID:120, 21 IT /ID:142, 22 UK /ID:168.
* **F7h — the code page of the font on the screen**, written by DISPLAY.SYS (apps/display)
  whenever it loads a font into the character generator: 0 = 437, 1 = 850, 2 = 860, 3 = 863,
  4 = 865 (7 = other). Without DISPLAY.SYS it stays 0: the ROM font is code page 437.

The page draws the key caps of the layout from web/js/kbdlayouts.js (generated from
KEYBOARD.SYS, apps/keyb/tools/mklegends.mjs) and, with "Keyboard: follow my computer's
layout", types characters the visitor's own layout makes as Alt+keypad codes in the F7h code
page.

### 4.7 ELBOW descriptor (ports FCh-FFh)

A 32-bit latch on the system board through which ELBOW (apps/x86) tells the page's
inspector where its state is, so the **ELBOW view** (web/js/elbow-panel.js) can show the
x86 block running and the ARM code ELBOW generated for it by reading guest RAM. Byte n of
the address is written to port FCh+n; the write to **FFh commits** it (write FCh, FDh,
FEh, FFh in that order). Reading FCh-FFh returns the committed value; a reset clears it
(emu/machine.mjs `m.elbowDesc`). ELBOW writes the address after installing its IRQ hooks
and writes back the previous value (0 normally; an outer ELBOW's when started from an x86
program's ARM child) in its cleanup, which runs however it ends (t22.S). Nothing else is
done per block or per instruction: the page samples the ARM PC between emulator slices,
only while the view is open.

The descriptor: 30 little-endian u32 words, all addresses absolute ARM addresses.

| word | contents |
|---|---|
| 0 | magic `0x57424C45` ("ELBW"); 0 once ELBOW has ended |
| 1, 2 | version (1), number of words (30) |
| 3 | `&cpu`, the x86 CPU state (apps/x86/x86.h `X86`); offsets of `eip` and `sreg[]` in words 20, 21 |
| 4 | `&mem` (a pointer to linear 0 of the x86 memory) |
| 5 | `rpt`, the read page table: host address of x86 linear L = `rpt[L >> 8] + L` |
| 6, 7 | `&blks` (a pointer to the block table), `&nblk` (blocks in use) |
| 8 | size of a block record (`struct jblk`, apps/x86/jit.c) |
| 9-15 | offsets in it of `lin`, `cs` (u32), `seg` (u32 [MAXSEG][2], linear [start, end) of the x86 bytes), `code`, `end` (the block's ARM code and its end, literal pool included), `nseg`, `dead` (u8) |
| 16, 17, 18 | the code cache: blocks in [w16, w17), exit stubs in [w17, w18); 0 with `/NOJIT` |
| 19 | `&cp` (the cache's fill pointer) |
| 22, 23 | `wpt`, `&irq_pending` (with `&cpu` and `rpt`: r8-r11 inside translated code, jitasm.S) |
| 24, 25 | ELBOW's image, code then data: [w24, w25) |
| 26, 27, 28 | `&jit_enabled`, `&` blocks translated so far, `&` cache refills |
| 29 | MAXSEG |

Blocks are allocated at increasing code addresses between cache refills, so the block
holding an address is found by binary search on `code`.

## 5. The interrupt ABI — "INT n" on an ARM

**`SVC #n` raises software interrupt *n*** (0-255). In ARM state it is the low 8
bits of the 24-bit comment field; in Thumb state the 8-bit immediate. The BIOS's
SVC exception handler builds a **register frame** on the SVC stack, reads the
handler address from the IVT at `n*4` and calls it as a C function
`void handler(struct armregs *f)`. When the handler returns, the (possibly
modified) frame is restored into the caller's registers and flags. A zero vector
returns immediately with the carry flag set.

Hardware IRQs are delivered the same way: the IRQ exception stub acknowledges the
PIC (reads port 0x20), then dispatches **INT 08h-0Fh for IRQ 0-7** and **INT
70h-77h for IRQ 8-15** through the same IVT with the same frame, in SVC mode,
**with IRQs disabled** (as on a PC). (Because INT 0Dh/0Eh are also the data/prefetch-abort
exceptions, IRQ 5 and IRQ 6 share those vectors: the Sound Blaster therefore sits on IRQ 7,
§4.3.) Handlers send EOI themselves (`outb(0x20,0x20)`,
plus 0xA0 for IRQ 8-15) — just like PC code. Programs can hook INT 08h / 09h / 1Ch
etc. with INT 21h AH=25h and chain to the old handler by calling it with the same
frame pointer.

```c
struct armregs {        /* layout is ABI: asm stubs and C both use it */
    uint32_t r0;  /* AX */     uint32_t r1;  /* BX */
    uint32_t r2;  /* CX */     uint32_t r3;  /* DX */
    uint32_t r4;  /* SI */     uint32_t r5;  /* DI */
    uint32_t r6;  /* BP */     uint32_t r7, r8, r9, r10, r11, r12;
    uint32_t sp;             /* caller's r13 */
    uint32_t lr;             /* caller's r14 */
    uint32_t pc;             /* where the caller resumes (after the SVC) */
    uint32_t cpsr;           /* caller's CPSR: bit29 C = CF, bit30 Z = ZF */
    uint32_t intno;          /* which INT this is */
};
```

**Register mapping** (the one thing every port relies on):

| x86 | ARM | notes |
|---|---|---|
| AX / AH / AL | r0 / r0 bits 15:8 / r0 bits 7:0 | a service returning a 16-bit AX clears bits 31:16 |
| BX, CX, DX | r1, r2, r3 | |
| SI, DI, BP | r4, r5, r6 | |
| DS:DX, ES:BX, DS:SI, ES:DI … | the **offset register alone holds a flat 32-bit pointer** | segment registers do not exist; a far pointer is a plain pointer |
| CF | CPSR C bit | `svc #0x21` then `bcs error` |
| ZF | CPSR Z bit | (INT 16h AH=01h) |
| 32-bit results in DX:AX | DX = high 16, AX = low 16 | kept for fidelity (seek, file size) |

**Segments.** Where DOS hands out or accepts a *segment* (memory blocks, PSP,
environment), ARM-DOS uses a **paragraph number** = address >> 4, exactly as DOS.
All DOS-owned memory is conventional memory so every segment still fits in 16
bits. `MK_FP(seg, off)` = `(void *)((seg << 4) + off)`.

### 5.1 Segment values: DS = r7, ES = r8

For pointers, the offset register alone holds the flat pointer (DS:DX -> r3,
ES:BX -> r1, DS:SI -> r4, ES:DI -> r5); the segment is ignored. For the few
calls where a segment register carries a segment **value** rather than half of
a pointer, **DS is r7 and ES is r8**:

* AH=49h (free) and AH=4Ah (resize) take the block segment in ES = **r8**;
  AH=48h returns the new segment in AX = r0 (as on x86).
* AH=35h (get vector) returns the flat handler address in BX = r1, and ES = r8 = 0.
* AH=4Bh (EXEC) takes the parameter block in ES:BX -> r1 is a flat pointer to it.
  Structures that hold far pointers keep their DOS layout and field offsets;
  each 4-byte far-pointer slot holds a flat 32-bit pointer (so the AL=00h EXEC
  block is: +0 u16 environment segment, +2 command tail, +6 FCB1, +10 FCB2,
  unaligned, 14 bytes).
* Likewise the XMS entry point from INT 2Fh AX=4310h is the flat pointer in BX = r1.

Mode: DOS and all programs run in **SYS mode** (privileged, no protection, just
like real mode). The kernel uses SVC mode's stack only inside handlers. A program
may disable interrupts (`_disable()` = set CPSR I) like `CLI`.

## 6. Video

The VGA device renders from guest memory — it is not a terminal.

* **Mode 03h** 80x25 text, 9x16 cells (720x400), 16 colours, attribute byte =
  (bg<<4)|fg, bit 7 blink (blink enabled by default: blinking text and the
  blinking underline cursor are real). Mode 01h/00h 40x25, 02h 80x25.
* **Mode 04h/05h** CGA 320x200 4-colour (interleaved: row&1 at +0x2000), **06h**
  640x200 2-colour, at 0xB8000.
* **Mode 13h** 320x200 256-colour linear at 0xA0000.
* **Mode 62h** (ARM-PC extension): 640x480, 256 colours (the DAC), one byte per
  pixel, **linear** from `0xA0000`: pixel (x, y) at `0xA0000 + 640*y + x`, up to
  `0xEAFFF`. In this mode the card also decodes that part of the adapter hole
  (`0xC0000-0xEAFFF` is RAM on the card, cleared when the mode is entered);
  leaving the mode (or a reset) makes the hole empty again. INT 10h AH=00h sets
  it (default 256-colour DAC, 80x30 teletype cells of 8x16, BDA rows 29, char
  height 16); AH=0Ch/0Dh pixels, AH=0Eh/09h/0Ah text and AH=06h/07h scrolling
  work in it; AH=1Bh reports 256 colours and 480 lines. The page size word in
  the BDA is 0 (300 KB does not fit). GEM's colour screen driver uses it
  (apps/gem, `GEM /V`); see bios/README.md.
* **The IBM VGA register model and planar memory**: the card
  has 256 KB of video memory as four 64 KB planes, the sequencer (map mask, memory
  mode: chain-4, odd/even), the graphics controller (set/reset, enable set/reset,
  colour compare/don't care, data rotate + AND/OR/XOR, read map select, read modes
  0/1, write modes 0-3, bit mask, latches loaded by every read), the CRTC (start
  address - latched at the start of vertical retrace -, offset, byte/word/doubleword
  addressing, maximum scan line / double scan, line compare split screen, vertical
  display end, byte panning; registers 0-7 write-protected by 11h bit 7) and the
  attribute controller (palette, mode control incl. 256-colour and the split-screen
  pan reset, overscan, colour plane enable, horizontal pixel panning, colour select).
  **Modes 0Dh (320x200x16), 0Eh (640x200x16), 10h (640x350x16), 12h (640x480x16)**
  are planar EGA/VGA modes; **Mode X/Y** (mode 13h with chain-4 off and the CRTC
  in byte mode, 320x200/240/400, 360-wide) and any other register combination
  are displayed from the registers.
* **Memory decoding**: in text, CGA and chain-4 (mode 13h) states A0000h/B8000h are
  plain RAM, as before (chain-4 plane p offset o = RAM A0000h + (o & ~3) + p). When
  chain-4 is off in a graphics state (or the planar modes), **A0000h-AFFFFh is an
  MMIO window** onto the planes: every CPU access goes through the graphics
  controller (a 16/32-bit access is 2/4 byte accesses in address order, each
  loading the latches). The picture moves between RAM and the planes when the
  window opens/closes. Text mode with odd/even off and A0000h mapped (the usual
  font-loading sequence) opens the window too; plane 2's first 8 KB is the character
  generator RAM (0x11000000). Mode 62h is unaffected.
* **Raster timing**: 3DAh bit 3 is high for the first 1.4 ms of each 1/70 s frame;
  the displayed scan lines follow evenly over the rest. Writes to the DAC,
  attribute, CRTC, sequencer clocking and pel mask registers are placed on the scan
  line they happen on: palette/panning changes per scan line are displayed
  (copper effects), the start address takes effect from the next frame.
* Ports: `0x3D4/0x3D5` CRTC index/data — regs 0x0A/0x0B cursor start/end (bit5
  of 0x0A = cursor off), 0x0C/0x0D display start address (words, text) , 0x0E/0x0F
  cursor location (cell index), and the rest of the CRTC as above; `0x3C4/0x3C5`
  sequencer, `0x3CE/0x3CF` graphics controller, `0x3C2`/`0x3CC` misc output. `0x3C8` DAC write index, `0x3C7` read index,
  `0x3C9` data (6-bit R,G,B triplets, auto-increment) — 256 entries, default VGA
  palette. `0x3DA` read = input status 1: bit3 vertical retrace (true for the first
  ~1.4 ms of each 70 Hz frame of *emulated* time), bit0 display disabled/hblank;
  reading it also resets the attribute flip-flop. `0x3C0` attribute controller (for
  the 16-entry palette -> DAC mapping in text mode, and mode control bit 3 = blink
  enable). **`0x3D9`** colour select for CGA modes (palette/background).
* **`0x3E0`** (ARM-PC extension, write) = **mode register**: the BIOS writes the
  mode number (0x00-0x06, 0x0D, 0x0E, 0x10, 0x12, 0x13, 0x62) here; the card then loads
  that mode's standard IBM register set (misc, sequencer, CRTC except the cursor
  registers 0Ah/0Bh/0Eh/0Fh, graphics controller, attribute 10h-14h - not the
  palette), which is what makes the display switch; programs may then change any
  register (Mode X). Reading returns the current mode. Text (graphics bit of GC 06h
  clear) and CGA modes 4-6 mapped at B8000h are displayed by the mode number as before.
* The display is 70 Hz; the page redraws from memory each animation frame.
* **Option: Hercules Graphics Card + mono monitor** instead of the VGA (`video: 'hercules'`,
  emu/README.md): mode 7, 80x25 MDA text at `0xB0000`, CRTC `0x3B4/0x3B5`, mode control
  `0x3B8`, status `0x3BA` (bit 7 = 0 in vertical retrace), config `0x3BF`; Hercules graphics
  720x348 (offset `0x2000*(y&3) + 90*(y>>2) + x/8`, page 1 at `0xB8000`) programmed directly.
  No VGA ports and no A0000h then; the BIOS detects the card at POST (equipment bits 4-5 =
  11, INT 10h AH=1Ah/1Bh absent). Programs writing the text screen directly use
  `ARMDOS_TEXT_VRAM` (follows the mode). See bios/README.md.

## 7. BIOS services (the ROM provides these through the IVT)

`INT 10h` video (AH=00,01,02,03,05(page0 only),06,07,08,09,0A,0C,0D,0E,0F,10(AL=00/02/10/12/15/17 palette),11(AL=00/04/14/30 font),12(BL=10 info),13,1A), `INT 11h` equipment (bit 12 = game adapter), `INT 12h` memory size (640), `INT 13h` disk (AH=00,01,02,03,04,08,15,16; DL=00 floppy A:, 80h hard disk; CHS translated to LBA with the geometry AH=08 reports; plus AH=41h/42h/43h extended LBA with a disk address packet whose buffer is a flat pointer), `INT 14h` serial (minimal), `INT 15h` (AH=84h joystick, §4.5, AH=86h wait, AH=88h extended memory size in KB, AH=C0h config), `INT 16h` keyboard (AH=00,01,02,10,11,12,05 stuff), `INT 17h` printer (returns "not ready"), `INT 18h` ROM BASIC -> prints the classic "No ROM BASIC" style message and halts, `INT 19h` bootstrap, `INT 1Ah` time (AH=00,01,02,04), `INT 08h` timer tick (increments 0x46C, calls INT 1Ch, handles midnight), `INT 09h` keyboard (set 1 -> BIOS scan/ASCII in the BDA ring buffer, Ctrl/Alt/Shift/locks, Ctrl-Alt-Del warm reboot, Ctrl-Break -> INT 1Bh, Pause), `INT 74h` mouse IRQ (packet assembly for the INT 33h driver via INT 15h AX=C2xx-style hook, simplified).

POST: memory count-up, CPU/ROM identification, drive detection, "Press DEL to enter
SETUP", beep, boot from A: if a disk is in the drive, else C:. Boot = read LBA 0 to
0x7C00, check 0x55AA at 0x7DFE, jump to 0x7C00 in ARM state with r3 (DL) = boot
drive. The boot sector is ARM code.

## 8. Executable format: "MZ" with an ARM image

Every `.EXE` (and any `.COM` that begins with `MZ` — DOS decides by the signature,
not the extension, and so does ARM-DOS) is:

1. A **genuine DOS MZ header plus a tiny 8086 stub program** that prints
   `This program requires an ARM processor.` and exits — so on a real x86 PC the
   file runs and says so, like Windows' "cannot be run in DOS mode".
2. At MZ offset `0x3C` (`e_lfanew`) the file offset of the **ARM header**:

```c
struct armexe {              /* all little-endian */
    char     sig[4];         /* "AR1\0" */
    uint16_t hdrsize;        /* sizeof(struct armexe) = 64 */
    uint16_t flags;          /* bit0: entry is Thumb; bit1: wants extended-memory heap */
    uint32_t image_off;      /* file offset of the load image */
    uint32_t image_size;     /* bytes of text+data */
    uint32_t bss_size;       /* zeroed after the image */
    uint32_t stack_size;     /* reserved after bss */
    uint32_t entry;          /* entry point, offset from load base */
    uint32_t reloc_off;      /* file offset of relocation table */
    uint32_t reloc_count;    /* u32 entries: image offsets of 32-bit words to add the load base to */
    uint32_t min_extra;      /* minimum extra bytes (heap) beyond stack; like MZ minalloc */
    uint32_t max_extra;      /* max extra bytes; 0xFFFFFFFF = as much as possible (MZ maxalloc FFFF) */
    uint32_t reserved[5];
};
```

Images are linked at address 0 and are relocated by adding the load base to every
word the table lists (produced from the ELF's `R_ARM_ABS32` relocations by
`sdk/elf2exe.mjs`). **The image is loaded at PSP + 0x100** (load base), the same
place a .COM goes.

**Raw `.COM`** (no MZ): a flat position-independent ARM image loaded at PSP+0x100
and entered there in ARM state.

**Entry state**: SYS mode, IRQs enabled, `r0` = PSP address (flat pointer), `r1` =
load base, `r2` = end of the memory block (flat pointer, exclusive), `sp` = top of
the stack area (8-byte aligned), `lr` = PSP address **| 1** (so `bx lr` from a
.COM lands in Thumb state on the `svc #0x20` at PSP:0000 and terminates, as `RET`
to PSP:0000 does on a PC; see §16). Exit with INT 21h AH=4Ch.

## 9. PSP (256 bytes, at the start of every program's memory block)

| off | size | contents |
|---|---|---|
| 0x00 | 2 | Thumb `svc #0x20` (0xDF20) — "INT 20h", as a PSP always begins |
| 0x02 | 2 | memory top as a segment (paragraph number of first byte beyond the block) |
| 0x05 | 5 | reserved (x86 far call to DOS in real DOS; zero) |
| 0x0A | 4 | INT 22h terminate address (flat) |
| 0x0E | 4 | INT 23h Ctrl-C address |
| 0x12 | 4 | INT 24h critical error address |
| 0x16 | 2 | parent PSP segment |
| 0x18 | 20 | job file table (handle -> SFT index, 0xFF = closed) |
| 0x2C | 2 | environment segment |
| 0x2E | 4 | saved SP on last INT 21h |
| 0x32 | 2 | JFT size (20) |
| 0x34 | 4 | JFT pointer (flat; normally PSP+0x18) |
| 0x38 | 4 | previous PSP (SHARE) |
| 0x40 | 4 | reserved (the SETVER word is a DOS 5 feature; ARM-DOS writes 0x0004 here but nothing reads it) |
| 0x44 | 4 | ARM-DOS kernel private: the parent's saved EXEC frame (§16) |
| 0x50 | 8 | ARM `svc #0x21` ; `bx lr` — "call PSP:0050h" |
| 0x5C | 16 | FCB 1 (parsed from the first argument) |
| 0x6C | 20 | FCB 2 |
| 0x80 | 128 | command tail: length byte, text, terminated by 0x0D. Default DTA. |

## 10. DOS memory: MCBs

The arena is a chain of 16-byte **memory control blocks** in conventional memory,
each immediately before its block, exactly DOS 4's layout: `+0` 'M' or 'Z' (last),
`+1` owner PSP segment (u16, 0 = free, 8 = DOS system), `+3` size in paragraphs
(u16, not counting the MCB), `+8` program name (8 chars, no NUL if 8 long). INT
21h AH=52h returns in BX (flat) the "list of lists", whose word at `-2` is the
first MCB segment, as in DOS.

Extended memory (1 MB + 64 KB to 16 MB; the first 64 KB is the BIOS's) belongs to **HIMEM.SYS** (loaded from CONFIG.SYS),
which provides XMS through INT 2Fh AX=4300h/4310h; the XMS entry point returned in
BX is a flat function pointer called with `blx` with AH = XMS function (as
documented for XMS 2.0). "Lock block" returns the 32-bit linear address in DX:BX,
which on this machine is directly usable. The SDK's C runtime uses it to grow the
heap beyond 640 KB, so big programs just work — ARM-DOS's C runtime is its own DOS
extender.

## 11. Disks

* **A:** 1.44 MB floppy images, FAT12, standard DOS 4 BPB (the boot sector's first
  4 bytes are an ARM branch instead of `EB xx 90`: `b` over the BPB, which is
  how IBM would have done it: the BPB must stay at offset 0x0B).
* **C:** hard disk image, MBR partition table (FDISK-compatible), one primary FAT16
  partition (type 0x06), 128 MB (streamed to the page in 256 KB chunks as it is read).
* **D:** (the page's second hard disk, primary slave; `disk/d.json`) the same layout, empty but
  for `README.TXT`. It is the user's to keep: the page makes it once, in a browser database of
  its own, and no later release, C: image or D: build changes it (web/js/keepdisk.js). DOS
  letters: each disk's first DOS partition in turn (C:, D:), then the logical drives; the
  CD-ROM (ARMCDEX, first free letter) is E: on the page, D: in single-disk test machines.
* Images are built by `disk/mkimage.mjs` from a directory tree + a manifest.

## 12. Build

`make` at the top level builds `build/rom.bin`, `build/*.EXE/.COM/.SYS`, and
`build/hd.img` / `build/floppy-*.img`. Compiler: `arm-none-eabi-gcc -marm
-march=armv5te -mfloat-abi=soft -Os`, newlib (default multilib = ARM state v4T,
compatible). Programs use `sdk/` (crt0 + syscall glue + `dos.h` etc.). The ROM,
IO.SYS and ARMDOS.SYS are freestanding (`-ffreestanding -nostdlib`, libgcc only).

## 13. Testing

`node emu/headless.mjs --rom build/rom.bin --hd build/hd.img [--fd a.img]
--keys "DIR\r" --until-idle --screen` prints the 80x25 text screen; `--png out.png`
saves a screenshot (any mode); `--serial` echoes COM1/E9 output. Tests live in
`tests/` and are run by `make test`.

## 14. Boot, IO.SYS, ARMDOS.SYS — the kernel's structure

The same division as MS-DOS 4.00, for the same reasons:

1. **Boot sector** (ARM asm, `kernel/boot/`): the ROM enters it at `0x7C00` in SYS mode
   with r3 = boot drive, sp = 0x7C00. It copies itself to `0x9F000` (just under the video
   hole) and continues there, reads the root directory, checks that the first two entries
   are `IO.SYS` and `ARMDOS.SYS`, and loads **the whole of IO.SYS** (contiguous clusters
   from the first data cluster, as DOS 4 requires) to **`0x00700`** — `0070:0000`, where
   DOS has always loaded it — and jumps to 0x700 with r3 = boot drive and r4 = pointer to
   the BPB in the relocated boot sector. Messages on failure: "Non-System disk or disk
   error" / "Replace and press any key when ready" (DOS 4's own).
2. **IO.SYS** (`kernel/io/`, C + asm, linked at 0x700, freestanding): the resident
   **device drivers** — `CON` (INT 16h/10h), `AUX` (INT 14h), `PRN` (INT 17h), `CLOCK$`
   (INT 1Ah + CMOS), and the block driver for A: and C: (INT 13h, reads the BPB / MBR) —
   followed by **SYSINIT**, which loads ARMDOS.SYS, initialises it, processes
   `CONFIG.SYS`, loads `DEVICE=` drivers, and EXECs the shell.
3. **ARMDOS.SYS** (`kernel/dos/`, C): the DOS kernel proper — INT 20h-2Fh, the FAT file
   system, the SFT/JFT, processes, memory. It is itself an **AR1 relocatable image**
   (the EXE format of §8 without needing the MZ stub to run); SYSINIT places it right
   after IO.SYS's resident part and applies the relocations, as SYSINIT once moved the
   DOS kernel to its final address.
4. **Device drivers** follow DOS's model with ARM-sized fields: the header is
   `{ u32 next; u16 attr; u16 pad; u32 strategy; u32 interrupt; char name[8]; }`
   (`strategy`/`interrupt` are C function pointers: `void strategy(struct reqhdr *)`,
   `void interrupt(void)`), and request packets keep DOS's layout (`len, unit, cmd,
   status, ...`) with every far pointer widened to a flat u32. A `.SYS` file loaded with
   `DEVICE=` is an AR1 image whose image begins with its device header.
5. **Processes**: INT 21h runs on the SVC stack. EXEC (4Bh) saves the parent's frame in a
   kernel save area, rewrites the frame to enter the child, and returns; TERMINATE (4Ch,
   20h, 00h, 31h, INT 23h/24h abort) restores the parent's saved frame into the current
   one, with AX/CF set as DOS sets them, and returns. So the SVC stack never holds more
   than one INT 21h at a time except for the legitimate nesting DOS allows (INT 24h/23h
   handlers calling console functions; TSRs from INT 28h).

## 15. Clarifications

* Boot code in a FAT boot sector starts at **0x40** (0x3E is not word aligned); bytes
  0-3 are `b 0x40`.
* **Disk geometry contract**: hard disks use 16 heads x 63 sectors/track everywhere (BIOS
  INT 13h AH=08h, MBR CHS fields, BPB). The default HD's size is set in disk/hd.json
  (128 MB, FAT16).
* **XMS entry** (and any driver "far call" entry): called with `blx`, arguments in r0-r6 as
  mapped in §5, results returned in r0-r6, may clobber anything except sp/r7-r11, returns
  with `bx lr`. Lock returns the 32-bit linear address as DX (high 16) : BX (low 16).
* **INT 23h (Ctrl-C) handler**: returns with **CF set in the frame = abort the program**,
  CF clear = continue (the frame-model equivalent of RETF vs IRET).
* Load bases (PSP+0x100) are **8-byte aligned** (LDRD/STRD are used): PSPs are paragraph
  aligned and MCB blocks start 16 bytes after an MCB, so PSPs must be allocated at
  8-aligned addresses — guaranteed since paragraphs are 16 bytes.
* Raw .COM entry: sp = top of the program's block (like SP=FFFEh), 8-byte aligned.
* The kernel must not include `sdk/include/dos.h` (it defines `interrupt` away).

## 16. Kernel decisions

Recorded by the kernel author; kernel/README.md has the full picture.

* **LoL (INT 21h AH=52h)**: BX = flat pointer, word aligned. DOS 4's layout up
  to +21h; then **+22h two padding bytes and the ARM-DOS 24-byte NUL device header at
  +24h** (it must be word aligned: drivers are called through it), so every later field
  is 8 bytes further than in DOS 4 (+3Ch joins, +47h BUFFERS, +4Bh boot drive, +4Dh
  extended memory KB). Several LoL pointers are unaligned (+12h, +16h, +1Ah): read
  them with byte loads / memcpy, not `LDR`. The word at LoL-2 is the first MCB segment.
* **Device drivers** (§14.4): the kernel calls `strategy(req)` then `interrupt()` as C
  functions, in SVC mode during INT 21h (SYS mode during CONFIG.SYS), IRQs enabled.
  `next` = -1 ends the chain. Request packets keep DOS's byte layout with 4-byte far
  pointer slots holding flat pointers. INIT: +0Eh in = the memory limit, out = break
  address; +12h in = the command line after `DEVICE=` (upper-cased, path first, ends
  CR LF), out (block) = BPB pointer array; +16h first drive (0 = A:); +17h config-error
  word. READ/WRITE use 32-bit sectors (+14h = FFFFh, +1Ah = sector) when the block
  driver's attribute bit 1 is set. Installed character devices go in front (after NUL),
  so a DEVICE= "CON" replaces the built-in one.
* **Frame-based EXEC** (§14.5): AX=4B00h saves the parent's frame (r0-r12, sp, lr, pc,
  cpsr: 72 bytes with a tag) just below the parent's sp and records it in the child's
  PSP+44h; terminate restores it (CF clear) - unless the child's INT 22h (PSP+0Ah,
  which EXEC sets to the parent's return address) was changed, in which case execution
  continues at the new address with the parent's registers. EXEC from SVC mode (a TSR
  inside an interrupt) uses a small kernel pool instead of the parent's stack.
  AX=4B01h returns the child's initial sp at +0Eh and entry address at +12h (bit 0 =
  Thumb) of the parameter block and makes the child the current PSP; when such a child
  terminates, execution continues at its INT 22h address with sp = the parent's PSP
  +2Eh (the sp of the parent's last INT 21h), registers zeroed.
* **The root process**: the first shell's PSP parent is itself. Terminating it frees
  nothing, restores INT 22h-24h from its PSP and continues at its INT 22h (PSP+0Ah) if
  that is non-zero; otherwise the kernel starts the shell again.
* **INT 23h**: the handler gets a copy of the caller's INT 21h registers; CF set on
  return = abort (exit type 1), CF clear = the INT 21h call is re-issued with the
  registers the handler left (as DOS re-dispatches).
* **INT 24h**: AH/AL/DI as DOS (AH bits: 7 char device, 1-2 area, 0 write, 3-5 fail/
  retry/ignore allowed; AL drive; DI low byte = error code, >0Ch mapped to 0Ch);
  **SI (r4) = flat pointer to the device header** (BP (r6) = its paragraph); the other
  registers are the caller's. The handler returns AL = 0 ignore, 1 retry, 2 abort,
  3 fail. Before a shell installs one, the kernel's default fails the call.
* **INT 27h**: DX = flat pointer to the first byte after the resident part (a value
  below the PSP address is taken as an offset from the PSP, as CS:DX with CS = PSP).
* **INT 25h/26h**: AL drive, CX sectors, DX start, BX flat buffer; CX = FFFFh: BX -> the
  DOS 4 packet {u32 start, u16 count, u32 buffer}. Nothing is left on the stack.
  A partition over 65535 sectors requires the packet form (AX=0207h otherwise).
* **Country information (38h)**: the case-map routine at +12h is a C function
  `uint32_t map(uint32_t c)` (r0 in, r0 out).
* **National language support** (kernel/README.md): COUNTRY= reads
  COUNTRY.SYS (DOS 4.00's format; default \COUNTRY.SYS, then \DOS\COUNTRY.SYS on the boot
  drive); INT 21h AH=38h/65h/66h serve the kernel's `struct nls_state` (kabi.h) and call
  NLSFUNC through **INT 2Fh AH=14h with DI (r5) = that structure** for other countries /
  code pages (AL=01h set code page BX, 02h extended info type BP of country DX / code page
  BX into SI, 03h set country DX, 04h country info of DX into SI; AL returns 0 or the error).
* **Divide by zero**: libgcc's `__aeabi_idiv0/ldiv0` raise INT 00h (`svc #0`); the
  kernel prints "Divide overflow" and ends the program (exit type 1). Undefined
  instruction / data abort / prefetch abort in a program (SYS mode, outside the
  kernel) print "Exception 06h|0Dh|0Eh: ... at <pc> in <PROGRAM>" and a register dump
  and end the program the same way; faults in the kernel or in SVC-mode handlers go to
  the BIOS crash screen.
