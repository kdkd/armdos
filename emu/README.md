# emu/ — the ARM-PC machine

The emulator for ARM-DOS: an ARMv5TE (ARM926EJ-S flavoured) CPU plus the PC/AT-style
devices of ARCH.md §2-§6, as pure ES modules that run unmodified in node 22 and in
browsers. Only `headless.mjs`, `testkit.mjs` (lazily) and `tests/` touch node APIs.

```
cpu.mjs        ARMv5TE core: ARM + Thumb interpreter, banked modes, exceptions, CP15
vfp.mjs        VFP9-S floating-point coprocessor (VFPv2, CP10/CP11): exact IEEE semantics
jit.mjs        basic-block JIT (JS source via new Function) on top of the interpreter
jitvfp.mjs     JIT code generation for VFP instructions (native JS float fast paths)
machine.mjs    bus + memory map, I/O port dispatch, scheduler/timing, input, RealtimeDriver
dev/*.mjs      pic, pit, kbc (+ keymap), cmos, ata, fdc, uart, lpt, vga, dma (8237), sb16 (+ opl3: Nuked OPL3
               port, LGPL-2.1, OPL3-LICENSE.txt), audio (emulated-time audio renderer),
               mpu401 (MPU-401 at 330h) + gmsynth (General MIDI SoundFont synth) + sf2 (SoundFont reader),
               gameport (the joysticks at 201h)
render.mjs     display -> RGBA (text 720x400, CGA 320x200 / 640x200, mode 13h, mode 62h 640x480, and every
               other VGA graphics state from the registers: planar 16-colour, Mode X/Y, split screen...), screen text
render-hgc.mjs the Hercules card's display (MDA text 720x350, graphics 720x348), phosphor-coloured
disasm.mjs     ARM/Thumb/VFPv2 disassembler, objdump syntax
memmap.mjs     memory activity counters (execute/read/write per 256-byte cell) + region table (MCBs, XMS) for the inspector's map
testkit.mjs    programmatic API for tests (boot, type, wait for text, screenshots)
headless.mjs   node CLI (ARCH.md §13)
demo.html      minimal browser front end (canvas, keyboard, mouse, speaker, LEDs, serial)
fonts/         CP437 VGA 8x16 and CGA 8x8 bitmaps (CC BY-SA 4.0, see fonts/README.md)
tests/         CPU differential tests vs QEMU, unit tests, device tests, test ROM, browser tests
```

## Quick start

```sh
node emu/headless.mjs --rom build/rom.bin --hd build/hd.img --keys "DIR\r" --until-idle --screen
emu/tests/rom/build.sh                                  # device test ROM -> build/emu-tests/rom/testrom.bin
node emu/headless.mjs --rom build/emu-tests/rom/testrom.bin --wait-text "ESC continues" --keys "{ESC}" --until-idle --png out.png
python3 -m http.server -d emu 8000   # then open http://localhost:8000/demo.html[?rom=..&hd=..&fd=..]
node emu/tests/run-all.mjs [--quick] [--browser]        # all test suites
```

## Headless CLI

`node emu/headless.mjs --rom rom.bin [options]`

| option | meaning |
|---|---|
| `--hd img`, `--fd img`, `--fd-wp` | disk images (hard disk C:, floppy A:, floppy write-protected) |
| `--keys "text"` | typed after `--wait-text` appears, or when the machine first goes idle. `\r` = Enter; escapes `{ESC} {ENTER} {TAB} {BS} {F1}..{F12} {UP} {DOWN} {LEFT} {RIGHT} {HOME} {END} {PGUP} {PGDN} {INS} {DEL} {PAUSE} {PRTSC}`, combos `{CTRL+C} {ALT+F} {CTRL+ALT+DEL} {SHIFT+TAB}`, raw codes `{KeyA} {Numpad5}`, pauses `{WAIT:500}` (ms) |
| `--wait-text "C:\>"` | wait until the text is on the screen |
| `--until-idle` | stop when typing is consumed (8042 queue and BIOS buffer at BDA 0x41A/0x41C empty) and the CPU spent >= 90% of the last 300 ms in WFI |
| `--max-seconds N` | limit of emulated time (default 60) |
| `--screen` | print the text screen as UTF-8 (CP437 mapping) |
| `--png file` | screenshot of the current mode (native size) |
| `--serial` | echo COM1 and port E9 to stdout |
| `--exit-on-port` | run until port 0xF4 is written; the process exits with that code |
| `--mips` | print emulated time, instructions, host MIPS, idle percentage |
| `--trace N` | keep the last N instructions (disables the JIT); printed with disassembly and registers on the first undefined/abort faults and on timeouts |
| `--mhz N`, `--no-jit`, `--rtc 2026-01-01T12:00` | clock, interpreter only, RTC start time (default: host clock) |
| `--save-hd f`, `--save-fd f` | write the (modified) disk images at the end |
| `--preload-font` | copy fonts/vga8x16.bin into the font RAM at power-on (BIOS bring-up aid) |
| `--no-joystick`, `--stick` | no game port at 201h / a joystick plugged into it, centred (`m.joy`) |
| `--wav file`, `--wav-rate hz` | record the machine's audio (SB16 DAC, OPL3, PC speaker) from power-on to a 16-bit stereo WAV (default 44100 Hz) |

Exit status: the 0xF4 code with `--exit-on-port`, 3 on a timeout while waiting, else 0.

## Test kit (for other components' tests)

```js
import { boot } from '../emu/testkit.mjs';
const pc = await boot({ rom: 'build/rom.bin', hd: 'build/hd.img' });   // paths or Uint8Arrays
pc.waitText('C:\\>');            // runs emulated time until the text appears -> true/false
pc.type('DIR\r');                // same escapes as --keys
pc.waitIdle();                   // see --until-idle
console.log(pc.screen());        // 25 lines, trailing blanks trimmed; pc.lines() untrimmed
await pc.png('shot.png');
pc.serial; pc.debug;             // captured COM1 / port E9 output
pc.waitSerial('ok'); pc.waitExit(); pc.exitCode; pc.faults;   // [{kind, pc, addr, timeMs}]
pc.run(500);                     // run 500 ms of emulated time
pc.machine; pc.cpu;              // full access
```

`boot()` options: `rom hd fd fdWriteProtected mhz jit trace preloadFont rtcBaseMs` (default RTC
2026-01-01 12:00 local, so runs are deterministic) and any `Machine` option. Everything is driven
by emulated time; there is no wall clock in tests.

## Machine API

```js
import { Machine, RealtimeDriver } from './machine.mjs';
import { renderScreen, screenText } from './render.mjs';

const m = new Machine({
  rom, hd, fd,                 // Uint8Array; rom <= 1 MB, byte 0 at 0xFFF00000 (padded with 0xFF)
  fdWriteProtected: false,
  mhz: 100, slowMhz: 12, turbo: true, jit: true,
  rtcBaseMs: Date.now(),       // RTC = rtcBaseMs + emulated ms + offset set by the guest
  cmos: savedRam,              // Uint8Array(128) battery RAM (else IBM-style defaults)
  onSerial(byte), onDebug(byte), onExit(code),
  onSpeaker(on, freqHz),       // PC speaker for WebAudio
  onDiskActivity(drive, lba, count, isWrite, cyl, prevCyl),   // drive 0x00 = A:, 0x80 = C:; cyl/prevCyl for floppy seek sounds
  onDiskWrite(drive, lba, count),   // image modified (persist it)
  onCmosWrite(ram, index),     // persist battery RAM
  onLeds(bits), onModeChange(mode), onReset(),
  onPrint(byte),               // LPT1 (0x378-0x37A): one byte per STROBE
  audio: { rate, onAudio(left, right), speaker },   // start the audio output at once (see below)
  sb: { base: 0x220, irq: 7, dma8: 1, dma16: 5 },   // Sound Blaster 16 resources
  mpu: { synth, remote, present, onFirstUse, onMidi }, // MPU-401 + GM synth (dev/mpu401.mjs; see below)
});
m.runFor(ms); m.runUntil(targetNs, hostDeadlineMs); m.timeMs(); m.timeNs();
m.keyDown(code); m.keyUp(code);          // KeyboardEvent.code -> scan code set 1 (E0/E1 sequences)
m.typeText('dir\r{F3}');                  // paced like a typist
m.mouseMove(dx, dy); m.mouseButtons(bits); // bit0 left, bit1 right, bit2 middle
m.joy.plug(stick, on); m.joy.setAxis(n, pos); m.joy.setButton(n, on);   // the game port (dev/gameport.mjs)
m.insertFloppy(img, wp); m.ejectFloppy(); m.serialInput('text');
m.setTurbo(on); m.reset(); m.powerCycle(); m.loadFont(bytes, height);
m.stopped; m.exitCode; m.haltedNs; m.postCode;   // (port 0x80 writes are latched for debugging)
m.audio.start(rate, onAudio, { speaker: false }); m.audio.stop();   // Float32Array left/right chunks, emulated time
m.sb, m.sb.opl.chip, m.dma;                      // the SB16 (DSP, mixer, pcm queue), the OPL3 core, the 8237s
renderScreen(m, img?, timeMs?) -> { width, height, data: Uint8ClampedArray RGBA }
screenText(m), screenLines(m)

const drv = new RealtimeDriver(m, { onFrame(stats) {}, maxSliceMs: 12, maxLagMs: 200 });
drv.start(); drv.stop();   // stats: { mips (host speed while running), effMips (delivered), hostLoad, emuMs, lagMs }
```

`RealtimeDriver` runs slices from `requestAnimationFrame` (or `setTimeout` in node / hidden
tabs), never lets emulated time run ahead of the wall clock, spends at most `maxSliceMs` of host
time per slice, and drops debt beyond `maxLagMs` when the host cannot keep up (the guest then
simply runs slower). While the guest sleeps in WFI no host time is burnt.

## Design

### CPU (cpu.mjs)
* State: `r` Int32Array (current mode's view; banks swapped on mode change), flags as fields
  (`nv`/`zv` hold the last result: N = nv < 0, Z = zv === 0; `c`, `v`, `q` 0/1), `t i f mode`.
* Memory: one ArrayBuffer = 16 MB RAM + 1 MB ROM with Uint8/Uint16/Int32 views. Aligned RAM
  accesses are inline (`(a & 0xFF000003) === 0 ? m32[a >>> 2]`); a flag byte per 128-byte line
  (`pflags`: bit 0 write-ignored, bit 1 compiled code, bit 2 armed by the memory map) sends
  stores to the 0xC0000 hole, to lines holding compiled code and to armed lines to the slow
  path. Everything else goes through `machine.read/write` (I/O, font RAM) or data-aborts.
* ARM decode: a 4096-entry table indexed by bits 27:20 and 7:4; Thumb: switch on bits 15:11.
* Timing: 1 instruction = 1 cycle at the nominal clock; `icount` plus the in-flight count
  (`_n`) gives exact emulated time for I/O reads in the middle of a slice.

### JIT (jit.mjs)
* Entry addresses are counted by the interpreter; at 16 executions the block from there (up
  to 64 instructions, continuing past conditional branches as side exits, never crossing a
  4 KB page) is translated to a JS function: guest registers and flags live in JS locals,
  constants are folded (PC reads, immediates, shifts, **PC-relative literal pool loads**),
  memory accesses are inline with the RAM fast paths, a branch back to the entry is a native
  `for(;;)` loop (bounded by the slice budget, checking `brk`). Function returns
  (`POP {..,pc}`, `LDM ..pc`, `BX`/`BLX reg`) and LDRD/STRD are inlined; rare instructions
  call the interpreter's handlers.
* **libgcc division** (`libgcc.mjs`): an entry whose code is word for word libgcc's ARMv5
  `__udivsi3`/`__divsi3`/`.divsi3_skip_div0_test` (the CLZ version of the `v5te` multilibs,
  e.g. `-mfloat-abi=softfp` builds) runs natively and *exactly*: the same final r0-r3, r12,
  N/Z/C/V and the same instruction count as the routine (checked by `tests/cpu/divtest.mjs`),
  so emulated timing is unchanged. Division by zero, or too little budget, runs the code.
* Memory slow paths only record the retired count (`cpu._n`, for I/O timing); if one throws a
  data abort, the region's `catch` writes its registers back and the dispatcher then enters
  the abort (`cpu.takeDataAbort()`). One shared exit tail writes registers back at exits.
* The dispatcher keeps a 2-way successor cache per compiled function.
* Invalidation: compiled code *and folded literals* are tracked per 128-byte RAM line; a store
  into such a line (self-modifying code, program loading, DMA via `hostWrite`) drops exactly
  the functions overlapping it (they are listed on every page they cover) and raises
  `cpu.brk`; compiled code checks `brk` after every slow-path store, so it never continues
  into code it has just overwritten (test: `tests/cpu/reload.mjs`, program B loaded where
  program A ran, by the host and by guest LDM/STM copies; `tests/cpu/unit.mjs` self-modifying
  code and patched literals).
* Tried and measured slower in V8 (kept as options/plumbing, off): several entries per JS
  function (`switch` between them: whole-page regions exceed TurboFan's size limit, and even
  small multi-entry functions optimise worse), and traces that continue through `B`/`BL`
  (`new JIT(cpu, { follow: true })`, ~30% slower on Quake: more, duplicated code).
* If `new Function` is forbidden (CSP without `unsafe-eval`), the JIT silently stays off.
* Limit: the instruction count after a data abort inside an in-block loop can be low by the
  completed iterations.

### Spin-loop idle skip (spin.mjs)
Busy-wait loops (retrace waits on 3DAh, the port 61h refresh bit, the BIOS tick at 046Ch, ...)
are fast-forwarded without changing any result. Trigger: the same port read returns the same
value 12 times with no I/O write in between, or a long slice ended. Verification: one loop
iteration is single-stepped from an instruction boundary; it must return to the same PC with
identical registers, flags, mode and VFP state, write no I/O port and take no exception - then
every further iteration performs the same (idempotent) memory stores and reads. Skip: whole
iterations are charged (`cpu.icount += k*P`) up to the earliest of the next scheduled device
event (timer IRQs, keyboard, SB/DMA progress, ...), the end of the run slice, and the moment a
polled port can change (device hints: VGA retrace/blank edges, 61h refresh toggle and OUT2, RTC
second, the game port's one-shot ends; ports without a hint are never skipped). Failures back off exponentially. Exact and
deterministic: `tests/machine/spin.mjs` runs retrace/tick/refresh polling loops and a counting
loop with and without the skip (and the JIT) and requires identical registers, memory,
instruction counts and time; the polling loops execute 0.5-2.4% of their instructions.
`new Machine({ spinSkip: false })` disables it; `m.spin.stats` counts skips.

### Memory activity map (memmap.mjs)
The inspector's MEMORY MAP (web/js/memmap-panel.js, web/README.md) shows, per 256-byte cell of
RAM, ROM, I/O ports and font RAM, the instructions executed and the reads and writes, decaying
over ~0.5 s. The counters are a `MemActivity`, attached with `act.attach(machine)` (sets
`cpu.act`) and read with `act.drain((x, r, w, wp) => ...)`. Nothing is counted, and no code path
changes, while none is attached: the only additions to the closed machine are one
`cpu.act !== null` test per `jit.run()` call and per bus (I/O) access, and a `pflags & 4` test
on the store slow path.

* **Execute and reads: sampled in time.** While attached, `jit.run()` hands over to
  `runAct()`, which alternates plain dispatch (`runPlain`, run()'s loop, budget cut at the
  next phase boundary) with **sampling windows**: 256 instructions out of every 524288 (1/2048)
  run in the interpreter with `act.hook` as its per-instruction trace callback (`cpu.trace` +
  `cpu.traceHook`, called before each instruction executes). The hook adds the instruction to
  its cell and decodes the loads/stores it is about to do from the current registers (ARM
  LDR/STR/B/H/SB/SH/D, LDM/STM, SWP, VFP FLD/FST/FLDM/FSTM; Thumb loads/stores, PUSH/POP,
  LDMIA/STMIA; condition codes honoured), each weighted 2048: unbiased estimates of all
  activity, compiled code or not. Because run phases end exactly at window boundaries, windows
  fall uniformly in instruction time (a long compiled loop cannot hide from them).
  `new MemActivity({ exact: true })` also counts every compiled region call exactly (cpu.jx to
  the cell of its entry, `runCounted`), `{ period, window }` change the sampling.
* **Writes: exact in space, per frame in time.** `attach()` and every `drain()` set bit 2 in
  `cpu.pflags` for every RAM line; the first store to an armed line takes the store slow path
  (compiled code and interpreter alike test `pflags === 0`), where `cpu.lineWritten()` disarms
  it and counts it in `wp`. A time sample misses most of a sweep through a buffer (DOOM's
  64000-byte framebuffer copy is ~0.1% of its instructions: 1/2048 of it is a few cells); the
  armed lines see all of it, at one slow-path store per written line per frame (~1600 lines per
  66 ms in DOOM).
* **I/O ports, font RAM** (machine.read/write) and **ISA DMA** (8237 reads, `dmaToMemory`):
  counted exactly.
* **Regions**: `memoryRegions(machine)` reads guest memory: IVT, BDA, DOS kernel (0x500 to the
  first MCB; the arena is found by walking candidate chains to the top of conventional memory),
  every MCB block with its owner's name, video RAM, the adapter hole, the HMA, HIMEM's handles
  (its device header "XMSXXXX0", then the static pointer to its handle table, validated), the
  empty bus above `ramEnd`, the ROM. `regionAt`, `cellOf`, `portName` help the UI.
* **A first design compiled a second, instrumented copy of every JIT region** (each inline
  access also bumping a counter) and ran it during the windows. It was exact and needed no
  decoder, but V8 runs rarely called functions unoptimised for a long time: the copies ran at
  27-170 MIPS for many seconds and the map cost 15-26% at 1/16 sampling and still ~14% at
  1/256. The interpreter is always warm (~150 MIPS; ~70 with the hook), so it samples instead.
* Side effect: the windows run in the interpreter, which takes an interrupt raised by an I/O
  access right after that instruction, where compiled code takes it at its next exit. So with
  the map open, IRQs can land a few instructions differently: DOOM's timedemo took 65072 instead
  of 65065 ms emulated (same 5026 gametics). With the windows off (`{ window: 0 }`) the
  timedemo result is identical; the unit test checks identical results on IRQ-free code.

### VFP (vfp.mjs, jitvfp.mjs)
The ARM926EJ-S's optional **VFP9-S** coprocessor, VFPv2: 32 single / 16 double registers
(aliased: Dn = S2n+1:S2n), FPSID = **0x41011090** (ARM, VFPv2, part 0x10, variant 9 — the same
value QEMU's arm926 reports), FPSCR, FPEXC (only EN writable, like QEMU), FPINST/FPINST2 (stored).
All VFPv2 instructions: FMAC/FNMAC/FMSC/FNMSC (not fused: product rounded, then sum),
FMUL/FNMUL/FADD/FSUB/FDIV, FCPY/FABS/FNEG/FSQRT, FCMP/FCMPE/FCMPZ/FCMPEZ, FMSTAT, FSITO/FUITO,
FTOSI(Z)/FTOUI(Z), FCVTDS/FCVTSD, FLDS/FLDD/FSTS/FSTD, FLDM/FSTM (IA, IA!, DB!, and the FLDMX/FSTMX
odd-count forms whose extra word is skipped), FMSR/FMRS, FMDLR/FMDHR/FMRDL/FMRDH, FMDRR/FMRRD,
FMSRR/FMRRS, FMXR/FMRX. ARM state only (Thumb-1 has no coprocessor instructions).

* **Enabling:** FPEXC.EN is clear at reset and then every VFP instruction is UNDEFINED except
  FMRX/FMXR of FPSID/FPEXC/FPINST* (privileged; FPSID is readable from USR mode). The ROM BIOS sets
  EN in its reset path (`mov r0, #0x40000000; fmxr fpexc, r0` in bios/start.S), so DOS programs
  find the VFP enabled. Programs use it with `-mfpu=vfp -mfloat-abi=softfp` (ARM state).
* **Arithmetic** is exact IEEE 754: default NaN 0x7FC00000/0x7FF8000000000000 (FPSCR.DN),
  NaN propagation (first signalling NaN, else first quiet NaN, quietened), all four rounding modes,
  flush-to-zero (FZ: denormal inputs -> signed zero + IDC, even when the other operand is a NaN;
  tiny results -> signed zero + UFC), tininess before rounding, IOC/DZC/OFC, saturating float->int
  conversions with IOC, FPSCR NZCV from compares.
* **IXC (inexact) and non-FZ UFC** are only tracked in *exact-flags mode*
  (`cpu.setVfpExactFlags(true)`), which sends every operation through the exact path (it computes
  the sign of the rounding error with BigInt). By default they are never set: computing them costs
  an error-free transform per operation, and C code practically never reads them
  (`fetestexcept(FE_INEXACT)` is the only way to notice).
* **Short vectors** (FPSCR LEN/STRIDE) are implemented (in the interpreter; the JIT defers to it
  while LEN != 0): destinations in bank 0 are scalar, Fm in bank 0 is a scalar operand, registers
  wrap within their bank of 8 singles / 4 doubles, STRIDE 0b00 = 1, 0b11 = 2. Two QEMU differences
  (QEMU is the one that deviates from the ARM ARM): QEMU steps single-precision vectors by 4 for
  STRIDE=0b11, and stores the later elements of the unary vector ops (FCPY/FABS/FNEG/FSQRT) to the
  advanced *source* register. The differential test therefore avoids those cases.
* **Trapped exceptions:** the enable bits (FPSCR 15, 12:8) are RAZ/WI (no support code), as QEMU.
* **Speed:** single precision is computed in double and rounded with `Math.fround` (correctly
  rounded for + - * / sqrt because 53 >= 2*24+2). The JIT emits native JS float code for data
  processing, compares, conversions, loads/stores and core<->VFP transfers while the block-entry
  mode check says "enabled, round-to-nearest, no FZ, LEN=0, no exact flags"; any non-finite result
  (NaN, infinity, overflow, divide by zero, invalid) re-executes the instruction on the exact path.
  Bit-exact moves (FCPY/FABS/FNEG, loads/stores, transfers) use the Int32 view.
* **Inspector:** `cpu.vfpState()` -> `{ enabled, fpsid, fpscr, fpexc, s[32], d[16], sBits[32] }`;
  raw state is `cpu.F32 / cpu.F64 / cpu.FW / cpu.fpscr / cpu.fpexc`. `formatRegs()` (testkit, and
  `--trace` output) prints FPSCR/FPEXC and D0-D15 when the VFP is enabled.

### Machine (machine.mjs)
The scheduler asks the devices for their next event time (PIT channel 0 edges, 8042 byte
delivery, RTC periodic/update interrupts, the typist), runs the CPU exactly up to it
(`budget = ceil(Δt / ns-per-instruction)`, at most 200k instructions), then services due
events. A device that changes its schedule (PIT reprogrammed, key pressed) makes the CPU
return early. When the CPU is halted by WFI and no IRQ is pending, time jumps straight to the
next event (or to the real-time target).

## Spec decisions (where ARCH.md is silent or ambiguous)

**Memory / CPU**
* rom.bin byte 0 maps to 0xFFF00000; images shorter than 1 MB are padded with 0xFF; ROM writes are ignored (no abort).
* Unaligned: LDR/SWP rotate (ARMv5); LDRH/STRH/LDRSH ignore bit 0; STR, LDM/STM, LDRD/STRD ignore bits 1:0. No alignment faults (CP15 A bit is not implemented).
* STR/STM of PC store PC+8 (QEMU-compatible; real ARM926 would store +12).
* STM with the base in the list stores the original base; LDM with writeback and the base in the list: the loaded value wins; Thumb LDMIA likewise.
* Undefined instruction exception for: coprocessors other than CP15, CP15 from USR mode, LDC/STC/CDP/MCRR/MRRC, the v6 media space, cond=1111 except BLX(imm)/PLD, LDRD/STRD with odd Rd, Thumb 0xDExx / unallocated misc. BKPT is a prefetch abort (v5). BXJ behaves as BX.
* CP15: c0 = 0x41069265 (op2=1: cache type 0x1D152152), c1 reset 0x00052078 (V=1; SBO bits kept on write), `mcr p15,0,rX,c7,c0,4` = WFI (wakes on a pending IRQ/FIQ even when masked, like hardware), other c7/c8 ops ignored, `mrc p15,0,r15,c7,c10,3` (test-and-clean) sets Z, c2/c3/c5/c6/... read back what was written; a data abort stores FSR (c5) = 0x8 and FAR (c6).
* MSR never changes T; writing an invalid mode keeps the current mode; USR mode can only write the flags.
* 16/32-bit accesses to byte-wide I/O ports access consecutive ports (like x86 `in ax,dx`); the ATA data port 0x1F0 does 16-bit transfers (32-bit = two). Font RAM accepts any size.

**PIC** — PC priority order 0, 1, 8-15, 3-7. Read 0x20 returns the highest pending unmasked IRQ that is deliverable given the in-service levels (falling back to any pending unmasked one), marks it in service; 0xFF if none; 0xA0 reads 0xFF. EOI (0x20 non-specific, 0x60+n specific) to port 0x20 clears the highest in-service *master level* (IRQ 8-15 count as level 2); to 0xA0 the highest in-service of 8-15. Leniencies: an IRQ 8-15 handler that only sends EOI to 0x20 still works; mask bit 2 of 0x21 does not mask IRQ 8-15. ICW1-4 sequences are accepted and ignored. Edge-triggered.

**PIT** — Counters derive from emulated time. Modes 0, 2, 3 exact (1/5 like 0, 4 like 0 with a pulse), BCD, lobyte/hibyte/lo-hi, counter latch, 8254 read-back (count and status). The first IRQ0 comes one full period after the count is written; reloading restarts the period at once. Mode 2/3 count of 1 behaves as 2/65536. Channel 2 gate = port 0x61 bit 0; speaker callback when gate and bit 1 are set and ch2 runs mode 2/3. Port 0x61 reads bit 4 = refresh toggle every 15.085 µs, bit 5 = OUT2.

**8042** — default command byte 0x47 (IRQ1 and IRQ12 enabled; the mouse still sends nothing until 0xF4). Bytes are released 20 µs after the previous one was read; controller command replies go ahead of queued keys. Supported: 0x20/0x60 command byte, A7/A8/A9/AA/AB/AD/AE, C0/D0/D1/D2/D3/D4, E0, FE/F0-FF pulse (= machine reset, RAM kept); keyboard ED (LEDs -> onLeds), EE, F0 (reports set 1), F2 (AB 41), F3, F4/F5/F6, FF (FA AA); mouse FF (FA AA 00), F6, F5, F4, F3, E8, E6, E7, EA, F0, F2 (00), E9, EB. Mouse packets are coalesced at the sample rate (default 100/s), deltas clamped to ±255 per packet (larger moves become several packets), browser +y (down) becomes PS/2 −y. No fake-shift bytes around E0 keys.

**CMOS/RTC** — local time; UIP always 0; register B default 0x02; 12-hour mode supported (bit 7 = PM); the day-of-week register is derived and writes to it are ignored; writes to the other time/date/century registers adjust an offset. PIE (rate from register A) and UIE raise IRQ8, register C read clears flags. Without saved RAM the battery RAM gets IBM AT defaults: 0x10 = 0x40 (1.44 MB A:), 0x12 = 0xF0/0x19 = 47, 0x14 = 0x41, 0x15/16 = 640 KB, 0x17/18 and 0x30/31 = 15360 KB, checksum at 0x2E/2F.

**ATA** — geometry 16 heads x 63 sectors x floor(sectors/1008) cylinders (≤ 16383); CHS addressing supported (bit 6 of 0x1F6 clear); IDENTIFY: model "ARM-PC FIXED DISK", serial "ARMPC0000000000001", firmware "1.00", LBA supported, words 60-61 total sectors; slave absent (status 0). READ/WRITE (MULTIPLE) interrupt per sector when nIEN is clear; reading 0x1F7 lowers IRQ14; VERIFY, FLUSH (E7/EA), INITIALIZE DEVICE PARAMETERS, SET FEATURES, SET MULTIPLE, power commands and recalibrate complete OK; unknown commands -> ERR/ABRT (error 0x04); out of range -> ERR/IDNF (0x10); SRST via 0x3F6 bit 2.

**Floppy** — DMA target must lie in RAM below 16 MB, else error; sector count 0 = error; status bit 5 (changed) is set by insert/eject and cleared when status is read; bit 0 reflects the last command. Media type by image size (360K/1.2M/720K/1.44M; other sizes guessed). IRQ6 raised after every command. `onDiskActivity` gets the floppy cylinder (LBA/36) and the previous one for seek sounds.

**COM1** — THR always empty (bytes go to `onSerial` immediately), LSR bit 5/6 set, loopback (MCR bit 4) supported, IRQ4 (IER bits 0/1) gated by MCR OUT2 as on a PC, DLAB divisor latches stored.

**LPT1** (`dev/lpt.mjs`, added for the web page's printer) — 0x378 data latch (reads back), 0x379 status always 0xDF (not busy, no ACK pulse, paper present, selected, no error), 0x37A control (reads back | 0xE0); the latched byte goes to `onPrint` on the rising edge of control bit 0 (STROBE as software writes it). No IRQ7, no bidirectional mode. The BIOS's INT 17h uses exactly this.

**VGA** — the DAC powers up with the standard 256-colour VGA palette. **Deviation from IBM:** the attribute palette powers up as the identity 0-15 (not 00-05,14,07,38-3F), so text and CGA colours are right with the 256-colour DAC and no BIOS setup; a BIOS that programs the IBM attribute values must also load the matching DAC entries. Text colour = DAC[attr[i] (| colour select bits)] per the attribute controller (0x10 bit 7 P54S honoured). Text: 9-pixel cells, the 9th column repeats column 8 for 0xC0-0xDF when attribute 0x10 bit 2 (line graphics, default on) is set; rows = 400 / character height (CRTC reg 9 + 1, default 16 → 25 rows); 40-column modes are pixel-doubled to 720 wide; display start (0x0C/0x0D) in words from 0xB8000, wrapping in 32 KB; cursor from 0x0A/0x0B/0x0E/0x0F blinks every 16 frames, blink attribute every 32 frames at 70 Hz (emulated time; the demo uses wall time). CGA: 0x3D9 bits 0-3 background (mode 4/5) or foreground (mode 6), bit 4 intensity, bit 5 palette; mode 5 uses the cyan/red/white palette; power-on value 0x30 — a BIOS must write 0x3F for mode 6 as the IBM BIOS does. Mode register 0x3E0: modes 00-03 text, 04/05/06 CGA, 13h, and the ARM-PC's **62h**: 640x480, 256 colours through the DAC, one byte per pixel linear from A0000h to EAFFFh — writing 62h makes the card decode the adapter hole C0000h-EAFFFh as video RAM (cleared; `Machine.setLinearFb`), any other mode or a reset gives the hole back (reads FFh, writes vanish); tests in tests/machine/vga62.mjs. Other values render as text. 0x3DA: bit 3 in the first 1.4 ms of every 1/70 s, bit 0 also during horizontal blanking (last ~20% of each 31.78 µs line); reading resets the attribute flip-flop. 

**VGA register model and planes** (dev/vga.mjs): 256 KB of video memory as four planes, stored interleaved (`vga.vram[offset*4 + plane]`, `vram32[offset]` = the four planes = the latch). Writing the mode register 3E0h loads the IBM register set of that mode (`STD_MODES`: 00h-06h, 0Dh, 0Eh, 10h, 12h, 13h; 62h uses 13h's) except the cursor registers and the attribute palette. `displayKind()`: `'lfb'` (62h), `'text'` (GC 06h graphics bit clear), `'cga'` (modes 4-6 mapped at B8000h), else `'vga'`. **Window**: chain-4 off in a graphics state with A0000h mapped (GC 06h map 0/1), or text with odd/even off and A0000h mapped (plane-2 font access), makes A0000h-AFFFFh an MMIO window: `machine.setVgaWindow(on)` sets `pflags` bit 3 on those lines (stores go to `machine.write`) and `cpu.mmioSeg = 0xA` (loads from that 64 KB segment go to `machine.read`: the interpreter tests it inline, the JIT compiles the extra `(a >>> 16) !== 10` test into its load fast paths only while the window is open - every switch flushes compiled code and raises `cpu.brk`); `winRead(o)`/`winWrite(o, v)` implement read modes 0/1 and write modes 0-3 with rotate, set/reset, ALU and bit mask on 32-bit lanes, map mask; bus accesses of 2/4 bytes are bytes in order. Opening the window moves the chain-4 picture from RAM into the planes, closing it into a chain-4 state moves it back; plane-2 writes at offsets < 8 KB in text mode also update the font RAM. Window accesses count as I/O writes for the spin-skip (never skipped). **Frame timeline**: every display-relevant register write (DAC, attribute, CRTC, sequencer 1, pel mask) is logged with its scan line (from emulated time: the first 1.4 ms of a frame = retrace = line -1, then the displayed lines evenly); at each frame boundary the log and a snapshot of the registers at the frame's start become `vga.doneStart/doneEv` (the last complete frame). **Renderer** (`renderVga`): mode 13h with standard registers and no mid-frame writes takes the old fast path (byte-identical); everything else is drawn per scan line from the frame's start state plus its timeline (palette/panning/pel changes per line; the start address latched per frame), with doubleword/word/byte addressing, offset, max scan line / double scan (a double-scanned picture is output at half the scan lines: mode 13h and Mode X 320x200, 0Dh 320x200; Mode Y 320x400, 12h 640x480), line compare split (address counter reset, pan reset when attribute 10h bit 5), horizontal pixel panning (256 colours: value/2) and byte panning, colour plane enable, attribute palette + colour select + P54S (16 colours), screen off (sequencer 1 bit 5). Speed (node, one frame): 320x400 unchained 0.15 ms, 640x480x16 0.6 ms, mode 13h 0.1 ms. Tests: `tests/machine/vga-planar.mjs` (68 checks).

**DMA (8237)** — `dev/dma.mjs`, ARCH.md §4.2. Register-level 8237 pair: current/base address and count with the byte flip-flop, mode (auto-init, decrement, direction; single/demand/block all paced by the device), masks, master clear, status TC bits (cleared on read), page registers and the EISA-style high page registers (0x48x, cleared by a low page write). Devices pull transfers with `read8or16(ch, units, buf)` / `write8or16`; any port access first lets devices catch up (`onAccess`), so address/count reads are exact in emulated time. Channel 4 never transfers. Non-RAM addresses read 0xFF.

**Sound Blaster 16** — `dev/sb16.mjs`, ARCH.md §4.3. DSP 4.05 with the command set listed there; a transfer is `{frames, frameNs, startNs}`: frames are fetched through the 8237 when due (lazily: at the block-end event, on DSP/DMA port access, and when audio is rendered - never later than the guest could notice), the block-end IRQ is a scheduler event at the exact emulated time. Reset → 0AAh after 20 µs. Direct DAC (10h) writes are timestamped samples. Masked/finished DMA gives silence without stalling the DSP clock (deviation: a real DSP waits for DREQ). ADPCM is played as 8-bit PCM (not decoded). Input records silence. Speaker on/off (D1h/D3h) does not mute (as on an SB16). Mixer: CT1745 registers, power-on master/voice/FM at 24 (-14 dB); master×voice scale the DAC, master×FM the OPL. The OPL port (`OplPort`) keeps the address latches, timers 1/2 (lazy, emulated time, status C0h/A0h/E0h) and a timestamped register queue for the renderer; with no audio consumer, writes go straight into the chip.

**OPL3** — `dev/opl3.mjs`: Nuked OPL3 1.8 ported by hand (from the Nuked-OPL3-fast copy in Chocolate Doom, which is bit-exact with upstream). Bit-exact with the C original: `tests/machine/sb16.mjs` checks a 60000-sample random register stream (rhythm, 4-op, OPL3 mode, vibrato/tremolo, all waveforms) against a checksum of the C output; during development 6 x 150000 samples were compared sample by sample with zero differences. ~23 ms of host time per emulated second with 9 voices sounding; an all-silent chip is skipped. The OPL's own timer IRQ is not wired.

**MPU-401 + General MIDI** — `dev/mpu401.mjs` (`m.mpu`), ARCH.md §4.3.1: status/command 331h, data 330h, intelligent-mode reset/ACK (FFh -> FEh, 3Fh UART -> FEh, ACh/ADh version/revision, others ACK), UART mode (only FFh leaves it, without ACK), IRQ 9 while an ACK waits, `present: false` = no card (ports FFh). MIDI bytes are timestamped in emulated time; `render()` (called by dev/audio.mjs per chunk) either mixes them through a **local** synth (`mpu: { synth }`: node tests, headless `--wav`, which loads 3rdparty/midi/ARMGS.SFA or `--sf2 file`) or, **remote** (`mpu: { remote: true }`, the web page), returns `{ midi: Int32Array(offset << 9 | byte; 256 = reset), gl, gr }`, which `onAudio(left, right, midi)` receives as a third argument for a synth elsewhere (web/js/audio-gm*.js). Gain = SB mixer master x FM. `onFirstUse` fires on the first command or MIDI byte (the page fetches the sound set then); `onMidi(tNs, byte)` taps the stream (tests).
`dev/gmsynth.mjs` (`GmSynth`): SoundFont 2.04 synthesis as FluidSynth does it (generators, envelopes, LFOs, resonant low-pass, default + bank modulators with the override rules, EMU 0.4 initial attenuation; CC 7/11 on the GM 40 log10 curve), GM/GS MIDI (bank select with capital-tone fallback, drums on channel 10 and GS rhythm parts, RPN 0/1/2, sustain, SysEx GM/GS/XG reset, master volume), 64 voices, Freeverb-style reverb, chorus; ~50 ms of host time per second with 64 voices. `dev/sf2.mjs`: the SF2 reader, plus the 4.5-bit block ADPCM sample chunk `smpB` of the ARM-PC sound set (apps/midi/tools/mksf.mjs).

**ATAPI CD-ROM** — `dev/atapi.mjs` (`m.cdrom`, always present): the secondary IDE channel's master (0x170-0x177, 0x376), IRQ 15 (INT 77h) when nIEN is clear; ATA DEVICE RESET/IDENTIFY PACKET DEVICE ("ARM-PC CD-ROM DRIVE", firmware 1.00, PIO only; IDENTIFY DEVICE aborts with the 14h/EBh signature) and the SFF-8020i/MMC packets listed at the top of the file (TEST UNIT READY, REQUEST SENSE, INQUIRY, START STOP UNIT, PREVENT/ALLOW, READ CAPACITY, READ(10/12), SEEK, READ SUB-CHANNEL 1-3, READ TOC 0/1, PLAY AUDIO (10/12/MSF), GET EVENT STATUS, PAUSE/RESUME, STOP, MODE SENSE/SELECT (6/10) pages 01h/0Dh/0Eh/2Ah/3Fh, SET CD SPEED, READ CD). A disc is a `CdDisc(manifest, { data, audio })` (apps/cdrom's disc.json: an ISO data track + audio tracks with pregaps; the data a Uint8Array or a streamed source whose missing sectors keep BSY; the audio decoded lazily through `request(track)`). Machine options `cdrom` (a disc in the drive at power-on), `onCdrom(state)` (tray/disc/play changes: `m.cdrom.state()`), `onCdActivity(lba, count)`; host calls `insert(disc)`, `eject()`, `trayButton()`, `setDiscInTray(disc)`/`closeTray()`, `headphones`/`knob` (the front jack: CD audio bypasses the SB16). After the tray closes the disc spins up for 1.2 s (NOT READY 04/01, then UNIT ATTENTION 28h once). CD audio plays in emulated time: `render()` (called by dev/audio.mjs) mixes it at 44.1 kHz into the SB16's CD input (mixer 36h/37h x master; power-on 0 = silent until SBMIX /INIT sets 24) with the MODE page 0Eh port volumes; audio that is not decoded yet holds the position ("seeking") instead of skipping. `tests/machine/atapi.mjs` (83 checks).

**Audio output** — `dev/audio.mjs` (`m.audio`). `start(rate, onAudio, {speaker})` renders from then on: the SB DAC stream (linear interpolation between its timestamped samples), the OPL3 (native 49716 Hz, register writes applied at their emulated time, linearly resampled), and with `speaker: true` the PC speaker (square wave at PIT ch2's frequency, lightly low-passed; the web page synthesises its speaker itself). `pump()` runs after every `runUntil` and every 20 ms of emulated time within one, emitting `Float32Array` chunks (left, right); nothing waits for the consumer, which handles drift (web/js/audio-sb.js). Output is calibrated: a full-scale 16-bit sample at 0 dB master/voice = 1.0; an OPL voice at full level ≈ 0.125.

**System board** — 0xF1 returns the current clock (100 turbo / 12 slow), 0xF2 read = turbo; writing 0xF2 switches turbo (extension). 0xF8 read snapshots `instructions/1e6` for 0xF8-0xFB. Port 0x80 writes are latched in `machine.postCode` (otherwise ignored, as the spec requires).

## Performance

Ryzen 9 9955HX, node 22.22, Chromium (Playwright headless). "bench" = emu/tests/cpu/c/bench.c
(sieve, crc32, qsort with callbacks, matrix multiply, struct/string work, list merge sort,
FixedMul/FixedDiv + memcpy, RLE), gcc 14.

| workload | JIT | interpreter |
|---|---|---|
| bench -O2 ARM, bare CPU, 30 rounds (568M insns), node | ~890 MIPS | ~175 MIPS |
| bench -O2 Thumb, bare CPU, node | ~870 MIPS | ~200 MIPS |
| bench -Os Thumb, bare CPU, node | ~640 MIPS | ~220 MIPS |
| bench -O2 ARM inside the full Machine (PIT at 1 kHz), node (`tests/machine/perf.mjs`) | ~670-770 MIPS | ~158 MIPS |
| bench -O2 ARM, bare CPU, Chromium (`tests/browser/bench.html`) | ~880 MIPS | ~155 MIPS |
| call-heavy integer torture (arith.c, 22-70M insns, includes warm-up) | 170-280 MIPS | 130-160 MIPS |

At the nominal 100 MHz the machine therefore runs 6-7x faster than real time on this host in
CPU-bound code; the real-time driver idles the rest.

**Games** (DOOM / Quake `-timedemo demo1`, whole demo incl. JIT warm-up, host idle; before -> after
the JIT's literal folding, inlined returns, native libgcc division,
lean slow paths): DOOM 702-708 -> 845-869 host MIPS (77.21 fps at 100 MHz either way), Quake
423-454 -> 469-534 (steady-state window ~530 -> ~650-680; 29.7 fps at 100 MHz either way). The
emulated frame rate at a fixed clock depends only on the guest's instruction count (timing is
exact); host speed is headroom for real time and higher clocks.

**Memory activity map** (`tests/machine/memmap-perf.mjs`, DOOM `-timedemo demo1`, node;
the host had other jobs on it, runs vary +-5%):

| | host MIPS |
|---|---|
| map never opened, before this feature (emu/ copy without the hooks) / with it: timedemo, 3 runs each | 896 / 924 (no change) |
| same, `tests/machine/perf.mjs` bench, 6 runs each | 856 / 873 (no change) |
| map open from the start of the demo vs closed (whole demo, 3 runs each) | 886 vs 912: **2.8% slower** |
| steady state: one demo, the map toggled every 125M instructions, each open slot vs its closed neighbours (16 pairs, 3 runs) | **0.7-1.9% slower** (+-1.6-2.0% s.e.) |
| ... with 1/1024 sampling | 2.9-4.8% slower |
| ... with the windows off (written lines + I/O only) | 0% (-0.2, +0.3) |
| ... with 1/1024 sampling and exact execute counts (`exact: true`) | 3.9% slower |
| first design, instrumented JIT copies, 1/16 / 1/256 sampling | 20-26% / 14% slower |

The estimates add up: over the whole demo the map counted ~6353M executed instructions of the
6397M run (sampled, 1/2048), 99.9% of them in DOOM's MCB block, and ~83M writes to the VGA
window A0000-AFFFF - the 5026 frames x 16000 words of DOOM's framebuffer copy.

The first ~0.3 s after the first opening are slower (V8 re-optimises the interpreter loop once
its trace branch is taken). In the browser (`web/tests/test_memmap.py`, headless Chromium with
a software canvas) the panel's drawing takes ~2.8-3 ms per 66 ms frame on the main thread; the
page's host-MIPS readings with the map open vs closed are within their noise (DOOM runs in
real time there).

**Floating point** (`tests/cpu/fpbench.mjs`: tests/cpu/c/fbench.c = 5-body simulation for 20000
steps and a 160x100 mandelbrot in double, a 3000-vertex float 3D transform x 20; gcc -O2 ARM,
bare CPU, node, JIT; measured while other jobs were loading the host, so absolute MIPS are ~15%
low):

| build | instructions per round | host time per round | MIPS | emulated time at 100 MHz |
|---|---|---|---|---|
| `-mfpu=vfp -mfloat-abi=softfp` | 23.3M | 50-60 ms | 400-700 | 0.23 s |
| `-mfloat-abi=soft` (libgcc soft-float) | 2075M | 5.2-5.7 s | 360-580 | 20.8 s |

VFP: 89x fewer instructions, ~95x less host time. The interpreter alone runs VFP code at ~30-40
MIPS (it is only used for cold code).

## Tests

* `tests/cpu/vfpdiff.mjs` — VFP differential vs QEMU (arm926 = VFP9-S): 11 classes (single and
  double data processing, FCPY/FABS/FNEG/FSQRT, conversions, compares + FMSTAT, loads/stores incl.
  pc-relative, multiples incl. X forms and writeback, all register transfers, short vectors,
  encodings that must be UNDEFINED), random FPSCR (rounding modes, FZ, DN, flags) and operands
  drawn from specials (signed zeros, infinities, quiet/signalling NaNs with payloads, denormals,
  extremes, near-overflow). Registers, CPSR, FPSCR, all of S0-S31, a memory checksum and the
  number of undefined-instruction traps are compared. `--jit` (threshold 1) and `--exact`
  (exact-flags mode, IXC/UFC compared too). Over 150k cases passed across seeds and modes.
* `tests/cpu/ctest.mjs` also builds `fp.c` (float/double arithmetic, conversions, compares on
  random and special operands) and `fbench.c` with `-mfpu=vfp -mfloat-abi=softfp` at -O0/-O2/-O3
  (plus -Os Thumb and soft-float -O2, which exercise libgcc) and compares with QEMU.

`node emu/tests/run-all.mjs` (add `--quick` to skip the QEMU/toolchain differentials,
`--browser` for the Playwright checks).

* `tests/cpu/difftest.mjs` — random single-instruction differential vs `qemu-system-arm -M
  versatilepb -cpu arm926`: 22 classes (all data-processing forms incl. register shifts by
  0/32/>32 with carry, multiplies, long multiplies, v5TE DSP ops, LDR/STR/LDRB/STRB/T forms,
  halfword/signed/doubleword, LDM/STM incl. base in list, SWP, MRS/MSR, and every Thumb format)
  with random operands, flags and conditions; each case's registers, CPSR and a checksum of a
  scratch buffer are compared. Unaligned addresses are excluded (QEMU aborts; ARM-PC rotates —
  covered by unit tests). Run with `--jit` to put every case through the JIT (compile
  threshold 1). During development over 500k random cases across seeds passed on the two tiers
  with zero mismatches; `run-all` runs 66k per tier.
* `tests/cpu/ctest.mjs` — C programs (arith.c, bench.c, excpt.c) built with -O0/-O2/-O3/-Os,
  ARM and Thumb, run on QEMU and on our CPU, output compared line by line. excpt.c covers
  SVC (ARM/Thumb), undefined instructions (ARM, Thumb, coprocessors), BKPT, banked registers in
  all modes, LDM/STM `^`, exception return with CPSR/T restore, interworking via LDR pc/LDM pc/
  BLX/POP pc.
* `tests/cpu/unit.mjs` — reset state, CP15, unaligned rotation, data/prefetch aborts (LR, SPSR,
  base restore, FAR), IRQ/FIQ entry and return, banked FIQ registers, WFI semantics, LDM `^`,
  user-mode MSR, self-modifying code and code reloading under the JIT.
* `tests/machine/devices.mjs` — port-level tests of every device.
* `apps/midi/tests/synth.mjs` (`make midi-test`) — MPU-401 registers (reset/ACK, UART, IRQ 9, absent card), the GM synth through the machine (note onset in emulated time, pitch of 12 program/key pairs, envelopes, velocity/CC 7/10/11, pitch bend with RPN 0, sustain, drums and exclusive class, GS rhythm part, bank fallback, 64-voice stealing and speed, mixer MIDI volume, remote mode).
* `tests/machine/sb16.mjs` — SB16/8237/OPL3: DSP reset/ID/commands, the mixer, 8237 registers (readback, pages, EISA high page, masks), 8-bit single-cycle pacing (DMA progress half way, IRQ at 200.0 ms), auto-init IRQ times, pause/continue/exit auto-init, 16-bit stereo on DMA 5 rendered to audio (1 kHz sine: pitch, level, polarity, no gaps), OPL timers (AdLib detection), Nuked OPL3 bit-exactness and note pitch, FM rendered through the machine on time (onset at 100 ms ± 1 ms).
* `tests/machine/memmap.mjs` — memory activity map: exact mode (every instruction sampled)
  counts ARM word stores/loads and Thumb byte stores/halfword loads in exactly the right cells,
  every access form (LDM/STM, LDRD/STRD, STRH/LDRSB, SWP, register offsets, PUSH/POP, failed
  conditions), port E9h writes, execute counts = instructions run; sampled mode estimates within
  20% with no stray cells and identical registers/instruction count; written lines (all 128 lines
  of a filled buffer per drain, re-armed); detach leaves no armed line and counts nothing; a booted
  ARM-DOS's regions (IVT, BDA, DOS kernel, COMMAND in the MCB chain, video, HMA, ROM, XMS, no
  overlaps) and DIR writing the text buffer. `tests/machine/memmap-perf.mjs` measures the cost
  (DOOM timedemo; `--steady` for the paired toggling measurement).
* `tests/machine/rom-test.mjs` — boots `tests/rom` (a small C+asm ROM that loads the font,
  takes PIT interrupts, reads CMOS, IDENTIFY/READ/WRITE/CHS on the ATA disk, floppy DMA, mouse,
  keyboard, retrace, speaker, mode 13h/4/6 drawing) with and without JIT and checks screen text,
  pixels, callbacks and the exit port.
* `tests/disasm/test-disasm.mjs` — disassembler vs objdump (100% on 24k random + 3k per class).
* `tests/browser/run.py` — Chromium: MIPS benchmark page and the demo page driven with real
  key presses.

## For the web front end

* `RealtimeDriver`: when the host falls more than `maxLagMs` behind (a stalled tab, a slow first frame), the debt is dropped: the wall-clock origin moves back by it (`emu0 -= lag - maxLag`), so emulated time does not run ahead afterwards.
* `dev/lpt.mjs` (LPT1, `onPrint`), see the LPT1 paragraph above.
* `dev/ata.mjs`: optional streaming **sector source** for the web page's chunked hard disk: `machine.ata.source = { ready(lba, n), fetch(lba, n) -> Promise<bool>, wrote(lba) }`. A READ touching sectors the source lacks keeps BSY (DRQ clear) until `fetch` resolves, then completes normally (`onDiskActivity` fires then, not while waiting); `false` = ERR with error register 0x40 (UNC). Writes land in the image at once and are reported via `wrote`. A new command, SRST or reset cancels a wait. Without a source nothing changes. Tests: `tests/machine/ata-stream.mjs`.

## Known gaps

* No MMU, caches, alignment faults, FIQ sources or Jazelle (per ARCH.md). VFP: IXC/UFC only in
  exact-flags mode, no trapped floating-point exceptions (see "VFP").
* VGA: CRTC horizontal/vertical timing registers (totals, sync, blanking) are stored but the
  frame is always 70 Hz (also for 480-line modes); preset row scan, the word-mode address
  rotation (MA13/MA15), odd/even in graphics (EGA 64 KB modes), 9-dot planar text and the
  overscan border are not modelled; VFP loads (VLDR/VLDM) compiled by the JIT do not see the
  planar window (plain RAM behind it).
* The floppy controller and ATA disk complete instantly (no rotational/seek timing).
* Sound: no MPU-401 intelligent mode (sequencer, timer), no SB CD port, no OPL timer IRQ; ADPCM not decoded; the DSP does not stall on a masked DMA channel.
* Instruction timing is one cycle per instruction.

## COM2: 16550A + internal Hayes modem + phone line (docs/MODEM.md)

* `dev/uart16550.mjs` — NS16550A at 0x2F8, IRQ 3: DLAB/divisor, FIFOs with trigger levels, character-timeout, THRE/LSR/MSR interrupts, OUT2 gating, loopback; characters take real (emulated) time at the programmed baud rate. `com2.baudOverride` (tests) forces a wire rate. IRQ leniency: re-raises on each new event while the line is high.
* `dev/modem.mjs` — Hayes command set (E V Q X H A O D Z I S M L &C &D &K &F &V &B, S0-S12, S37 line rate, +++ with guard time, A/), result codes verbal/numeric, dial/ring/answer/training timeline, 2400 bps pacing (up to 14400 via AT&B/S37), sound events for the page (`onModemSound`), front-panel state `modem.panel()`. Machine options: `phone` (PhoneExchange), `phoneNumber`, `onModemSound`, `onModemChange`.
* `phone.mjs` — PhoneExchange (numbers → endpoints, legs, BUSY for 867-5309; `lineRate` override for fast tests) and PortEndpoint/PortExchange to bridge a Web Worker's machine.
* `zmodem.mjs` — ZMODEM sender/receiver (CRC-16/32, ZRPOS recovery, windowing), `hostlink.mjs` — the 555-0100 Host Link endpoint.
* Tests: `tests/machine/modem.mjs` (registers, AT set, two machines calling each other), `tests/machine/modem-dos.mjs` (ARM-DOS `ECHO ATDT...>COM2`, `COPY COM2 CON` via INT 14h), `tests/zmodem/lrzsz.mjs` (vs lrzsz sz/rz, with line noise), `tests/zmodem/hostlink.mjs` (a machine dials the Host Link, lrzsz on the terminal side, both directions).

* The card's **speed switch** (`Machine({ modemRate })`, `modem.setSwitch()`: 2400 / 14400 / 33600 / 56000; `S37=0` trains at it), V.22bis / V.32bis / V.34 / V.90 per rate, V.90 asymmetric 53333 down / 31200 up (paced per direction), `ATW1` extended result codes (`CONNECT 53333/V90`), `AT+MS=`. `modemsound.mjs` generates each modulation's training (the 56K one tuned against a real call, docs/MODEM.md), `tests/modemsound/` renders WAVs and checks the V.90 structure.

## Hercules Graphics Card + mono monitor

`new Machine({ video: 'hercules', monitor: 'green' | 'amber' | 'white' })` (testkit `boot()`
passes these through; `m.setVideo(video, monitor)` + `powerCycle()` swaps the card later).
One card at a time: with the Hercules the VGA's ports (3C0h-3E0h) read FFh and A0000h is an
empty bus (reads FFh, writes ignored); the `m.vga` object stays for helpers, unplugged.

* `dev/hercules.mjs`: CRTC 3B4h/3B5h (mirrors 3B0-3B7; R14-R17 read back, the others 0),
  mode control 3B8h (bit 1 graphics, 3 video, 5 blink, 7 page 1; bits 1/7 need the config
  switch), status 3BAh (bit 0 hsync, bit 3 dots, bit 7 vertical retrace **0 during the
  retrace**, Hercules-style, which is how programs detect a Hercules; bits 4-6 = 0 HGC),
  config 3BFh (bit 0 graphics allowed, bit 1 page 1 at B8000h: while 0, B8000h-BFFFFh is an
  empty bus and the page's contents are kept aside). 50 Hz: 370 lines of 54.25 us.
* `render-hgc.mjs`: MDA text (80x25, 9x14 cells, the card's own ROM font `dev/mdafont.mjs`;
  columns 9 of C0h-DFh repeat column 8; attributes: 00/08/80/88 nothing, x0h with background
  7 reverse, foreground 1 underlined at row 12, bit 3 bright, bit 7 blink / bright reverse;
  other backgrounds black; cursor per R10/R11, R10 bits 5-6 = 01 hides it) and graphics
  720x348 (offset 2000h*(y&3) + 90*(y>>2) + x/8, page per 3B8h bit 7, start address honoured).
  Colours come from `PHOSPHORS` (background glow, normal, bright). Video off = dark screen.
  Always 80x25 / 720x348 regardless of the CRTC timing registers.
* `screenLines()` reads B0000h (page 1 if selected); in graphics it returns blank lines.
* `onModeChange` gets 07h / 107h when the card switches text/graphics.


## Open the box: the cards, SIMMs and clock jumper

`new Machine({ ram, mhz, sound, mouse, modem, cdrom, video, monitor })`, or later
`m.setHardware({...})` + `powerCycle()` (what is not given keeps its value; like `setVideo`,
do it with the power off). The web page's "Open case" drives it (web/js/openbox.js). The
defaults are the machine as it always was; `emu/tests/machine/hardware.mjs` covers the bus and
ARM-DOS on each configuration (bios/README.md).

| option | values | effect |
|---|---|---|
| `ram` | 1, 2, 4, 8, **16** (MB of SIMMs) | RAM ends at `min(16 MB, ram MB + 384 KB)` (`m.ramEnd`; the 384 KB behind the adapter hole is relocated to the top, as AT chipsets did; 1 MB = 0x160000). Above it the bus is empty: reads FFh, writes vanish (the hole's mechanism: FFh fill + `pflags` bit 0), also for DMA (`dmaToMemory`). The BIOS sizes it at POST. |
| `mhz` | any, the page offers 12/25/33/50/**100**/133 | the turbo clock (port F1h); turbo off stays `slowMhz` (12) |
| `sound` | **'sb16'**, 'adlib', 'none' | 'adlib': 220h-22Fh and the MPU-401 float, the OPL3 answers at 388h-38Bh only, and the CT1745 mixer (inaudible, still the renderer's gain stage) is set to 0 dB master/FM at every reset, since an AdLib has no mixer. 'none': 388h floats too. The MPU-401 is the SB16's daughterboard: without an SB16 its ports float whatever `mpu.present` says. |
| `mouse` | **true**, false | nothing on the PS/2 port: every byte sent with D4h is answered FEh on the aux channel (the 8042's time-out), `mouseMove`/`mouseButtons` do nothing (`kbc.mousePresent`) |
| `modem` | **true**, false | the COM2 card is out: 2F8h-2FFh float |
| `cdrom` | a CdDisc / nothing (**drive present**), false | false: no drive on the secondary IDE channel (170h-177h, 376h float) |
| `joystick` | **true**, false | the game port at 201h (on the SB16 / the multi-I/O card's J2; the page decides); false: 201h floats |

The port map is rebuilt from the cards (`buildPortmap`); with the factory set it is the shared
`PORTMAP` as before. Headless: `--ram MB --sound sb16|adlib|none --video vga|hercules
--no-mouse --no-modem --no-cdrom --no-midi`.


## The game port: joysticks at 201h (dev/gameport.mjs)

ARCH.md 4.5. `m.joy` (a `GamePort`), present unless `joystick: false`.

* Four NE558 one-shots: a write to 201h starts each idle one at the current emulated time; bit n
  reads 1 while `now < start + 24.2 us + 0.011 us/ohm x R`. The pulse length follows the
  potentiometer *now*, so a stick plugged in or moved while a pulse runs ends it by its new
  resistance; with nothing plugged in (`ohms[n] === null`) a fired pulse never ends, and an axis
  never fired reads 0 (so POST finds the card: 201h reads F0h). Not retriggerable.
* Buttons are bits 4-7, 0 = pressed. Host side: `plug(stick, on)` (both axes centred at 50 kOhm
  / open, the stick's buttons released), `setAxis(n, pos)` (-1..1 -> 0..100 kOhm, null = open),
  `setOhms(n, r)`, `setButton(n, on)`, `state()`, `onChange`.
* **The ISA cycle**: `Machine.isaWait(ns)` adds bus time without instructions (`nowNs +=`); each
  201h read or write costs 1 us, so software polling loops count ~0.9 poll per us plus their own
  instructions at any clock. If a wait carries time past the end of the CPU slice
  (`m.sliceEndNs`) the slice is stopped, so interrupts stay on time. `m.isaWaitNs` totals it.
* **Spin skip**: 201h's hint is the next one-shot end (buttons only change between `runUntil`
  calls, which bound a skip), and a skipped iteration is charged its instructions *and* its
  wait states. A pure wait (for a pulse, for a button) is skipped exactly: identical registers,
  memory, instruction count and time with and without the skip. Under the JIT the counts and
  results are identical too, but the JIT leaves compiled code only at a loop's back edge, not
  right after the 201h read that crossed the slice end, so its slices (and the interrupts
  between them) can come one polling iteration (~1 us) later than under the interpreter.
* `tests/machine/gameport.mjs`: port bits and timing (0 Ohm = 24.2 us, 100 kOhm = 1124.2 us,
  not retriggerable, open circuit, plugged in while charging), the 1 us cycle, the option;
  guest loops (Quake's counting loop, a pulse wait, a button wait) at 100 and 12 MHz with and
  without JIT and skip: counts as predicted (x 279 / y 947 at 100 MHz) and identical everywhere.
* `phonelines.mjs` - the scripted numbers (WOPR 399-2364, support 555-0142, POPCORN 767-2676, fax 555-3299, 386 Fortress 555-0386, Floating Point 555-7734, wrong number 555-0123, KREMVAX 011-7-095-231-1984) with `registerLines(exchange)`; legs can pick up by voice (`leg.voice()`) and send line audio (`leg.lineSound()`); international numbers ring the Soviet/European way. `tests/machine/phonelines.mjs` calls them all through a real modem.
