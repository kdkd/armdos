// ARM-DOS machine: bus, memory map (ARCH.md §3), I/O ports (§4), timing, input.
//
//   const m = new Machine({ rom, hd, fd });
//   m.runFor(1000);           // deterministic: advance 1 s of emulated time
//   new RealtimeDriver(m).start();   // browser/node: follow the wall clock
//
// Emulated time is derived from retired instructions at the nominal clock
// (default 100 MHz "turbo", 12 MHz slow). When the CPU executes WFI it idles:
// time jumps to the next scheduled device event.

import { CPU, RAM_SIZE, ROM_BASE, ROM_SIZE } from './cpu.mjs';
import { PIC } from './dev/pic.mjs';
import { PIT } from './dev/pit.mjs';
import { KBC } from './dev/kbc.mjs';
import { CMOS } from './dev/cmos.mjs';
import { ATA } from './dev/ata.mjs';
import { ATAPI } from './dev/atapi.mjs';
import { FDC } from './dev/fdc.mjs';
import { UART } from './dev/uart.mjs';
import { UART16550 } from './dev/uart16550.mjs';
import { Modem } from './dev/modem.mjs';
import { VGA } from './dev/vga.mjs';
import { Hercules } from './dev/hercules.mjs';
import { LPT } from './dev/lpt.mjs';
import { DMA } from './dev/dma.mjs';
import { SB16 } from './dev/sb16.mjs';
import { MPU401 } from './dev/mpu401.mjs';
import { GamePort, ISA_IO_NS } from './dev/gameport.mjs';
import { AudioOut } from './dev/audio.mjs';
import { CHARMAP, KEYNAMES } from './dev/keymap.mjs';
import { JIT } from './jit.mjs';
import { SpinSkip } from './spin.mjs';

export { RAM_SIZE, ROM_BASE, ROM_SIZE };
export const IO_BASE = 0x10000000, FONT_BASE = 0x11000000, FONT_SIZE = 0x2000;
const HOLE_START = 0xC0000, HOLE_END = 0x100000;
// Mode 62h (640x480, 256 colours, one byte per pixel) is linear from A0000h:
// the card then also decodes the adapter hole up to LFB_END (ARCH.md §6)
export const LFB_BASE = 0xA0000, LFB_END = 0xA0000 + 640 * 480;
const SLICE_MAX = 200000;          // max instructions per CPU slice
const UNLOCKED_MAX_MHZ = 999;      // TURBO MAX: the front panel has three digits
const UNLOCKED_BUSY = 0.6;         // of each frame the CPU may use before the clock stops rising

// port -> device id
const D_NONE = 0, D_PIC = 1, D_PIT = 2, D_KBC = 3, D_SYSB = 4, D_CMOS = 5, D_ATA = 6, D_FDC = 7,
  D_UART = 8, D_E9 = 9, D_VGA = 10, D_BOARD = 11, D_LPT = 12, D_COM2 = 13,
  D_DMA = 14, D_SB = 15, D_OPL = 16, D_HGC = 17, D_CD = 18;
const D_MPU = 31;                  // MPU-401 (dev/mpu401.mjs)
const D_JOY = 30;                  // game port (dev/gameport.mjs)
const PORTMAP = new Uint8Array(0x10000);
(() => {
  const set = (a, b, d) => { for (let p = a; p <= b; p++) PORTMAP[p] = d; };
  set(0x20, 0x21, D_PIC); set(0xA0, 0xA1, D_PIC);
  set(0x40, 0x43, D_PIT);
  PORTMAP[0x60] = D_KBC; PORTMAP[0x64] = D_KBC; PORTMAP[0x61] = D_SYSB;
  set(0x70, 0x71, D_CMOS);
  set(0x1F0, 0x1F7, D_ATA); PORTMAP[0x3F6] = D_ATA;
  set(0x170, 0x177, D_CD); PORTMAP[0x376] = D_CD;                                                    // ATAPI CD-ROM (dev/atapi.mjs)
  set(0x300, 0x307, D_FDC);
  set(0x3F8, 0x3FF, D_UART);
  set(0x2F8, 0x2FF, D_COM2);
  set(0x378, 0x37A, D_LPT);
  PORTMAP[0xE9] = D_E9;
  set(0x3C0, 0x3DF, D_VGA); PORTMAP[0x3E0] = D_VGA;
  set(0xF0, 0xFF, D_BOARD);
  set(0x00, 0x0F, D_DMA); set(0x81, 0x8F, D_DMA); set(0xC0, 0xDF, D_DMA); set(0x481, 0x48B, D_DMA);   // 8237 (ARCH.md §4.2)
  set(0x220, 0x22F, D_SB); set(0x388, 0x38B, D_OPL);                                                  // SB16 + OPL3 (§4.3)
  set(0x330, 0x331, D_MPU);                                                                            // MPU-401 + GM synth
  PORTMAP[0x201] = D_JOY;                                                                              // game port (on the I/O card / SB16)
})();

const noop = () => {};
export const RAM_OPTIONS = [1, 2, 4, 8, 16];   // MB of SIMMs (setHardware)
export const HW_DEFAULTS = Object.freeze({ ram: 16, sound: 'sb16', mouse: true, modem: true, cdrom: true, joystick: true });

export class Machine {
  /**
   * @param {object} o
   *  rom: Uint8Array (<= 1 MB, byte 0 at 0xFFF00000), hd/fd: Uint8Array disk images,
   *  fdWriteProtected, mhz (turbo clock, default 100), slowMhz (default 12), turbo (default true),
   *  jit (default true), rtcBaseMs (default Date.now()), cmos (Uint8Array(128) battery RAM),
   *  callbacks: onSerial(byte), onDebug(byte), onExit(code), onSpeaker(on, freqHz),
   *  onDiskActivity(drive, lba, count, isWrite, cyl, prevCyl), onDiskWrite(drive, lba, count),
   *  onCmosWrite(ram, index), onLeds(bits), onModeChange(mode), onReset(), onPrint(byte)
   *  COM2 modem (docs/MODEM.md): phone (a PhoneExchange), phoneNumber (this line's number),
   *  onModemSound(ev), onModemChange(panel), modemRate (the card's speed switch: 2400/14400/33600/56000)
   *  ATAPI CD-ROM (dev/atapi.mjs): cdrom (a CdDisc in the drive at power-on), onCdrom(state), onCdActivity(lba, count)
   *  Sound Blaster 16 (dev/sb16.mjs): audio: { rate, onAudio(left, right), speaker } starts the
   *  audio output at once (else m.audio.start(rate, onAudio) later), sb: { base, irq, dma8, dma16 }
   *  Display card (one at a time, like a real PC): video 'vga' (default) or 'hercules'
   *  (dev/hercules.mjs: MDA text + Hercules graphics at B0000h, ports 3B4h-3BFh; the VGA's
   *  ports and A0000h then are empty), monitor 'green' | 'amber' | 'white' (mono phosphor)
   *  The cards and jumpers (setHardware; the web page's "open the box"): ram (MB of SIMMs:
   *  1, 2, 4, 8 or 16 = default), sound 'sb16' (default) | 'adlib' (OPL3 at 388h only) | 'none',
   *  mouse / modem (default true), cdrom: false = no CD-ROM drive (else a CdDisc or nothing),
   *  joystick (default true): the game port at 201h (dev/gameport.mjs; m.joy, sticks plugged by the host)
   */
  constructor(o = {}) {
    this.o = o;
    this.cb = {
      serial: o.onSerial || noop, debug: o.onDebug || noop, exit: o.onExit || noop, speaker: o.onSpeaker || noop,
      disk: o.onDiskActivity || noop, diskWrite: o.onDiskWrite || noop, leds: o.onLeds || noop,
      mode: o.onModeChange || noop, reset: o.onReset || noop, print: o.onPrint || noop,
    };
    this.cpu = new CPU(this);
    this.font = new Uint8Array(FONT_SIZE);
    this.turboMhz = o.mhz || 100; this.slowMhz = o.slowMhz || 12;
    this.turbo = o.turbo !== false;
    this.mhz = this.turbo ? this.turboMhz : this.slowMhz;
    this.nsPerInsn = 1000 / this.mhz;
    this.nowNs = 0; this.icBase = 0;
    this.haltedNs = 0;
    this.stopped = false; this.exitCode = null;
    this.port61 = 0;
    this.icSnapshot = 0;
    this.postCode = 0;
    this.typeQ = []; this.typeNextNs = 0; this.typeDelayNs = (o.typeDelayMs ?? 15) * 1e6;
    this.pendingReset = false;
    this.pic = new PIC((lvl) => { this.cpu.irqLine = lvl; if (lvl) this.cpu.brk = 1; });
    this.pit = new PIT(this);
    this.kbc = new KBC(this);
    this.cmos = new CMOS(this, { rtcBaseMs: o.rtcBaseMs, ram: o.cmos, onRamWrite: o.onCmosWrite });
    this.ata = new ATA(this, o.hd || null);
    this.cdrom = new ATAPI(this, { onChange: o.onCdrom, onActivity: o.onCdActivity });   // secondary IDE master, IRQ 15
    this.fdc = new FDC(this, o.fd || null, o.fdWriteProtected);
    this.uart = new UART(this);
    this.com2 = new UART16550(this, { base: 0x2F8, irq: 3 });
    this.modem = new Modem(this, this.com2, { exchange: o.phone || null, number: o.phoneNumber || null, onSound: o.onModemSound, onChange: o.onModemChange });
    if (o.modemRate) this.modem.setSwitch(o.modemRate);
    this.vga = new VGA(this);
    this.portmap = PORTMAP;
    this.hgc = null;
    this.hw = { ...HW_DEFAULTS };
    this.setVideo(o.video, o.monitor);
    this.spin = new SpinSkip(this);          // spin-loop idle skip (spin.mjs); spinSkip: false disables
    if (o.spinSkip === false) this.spin.enabled = false;
    this.lpt = new LPT(this);
    this.dma = new DMA(this);
    this.audio = new AudioOut(this);
    this.sb = new SB16(this, o.sb);
    this.mpu = new MPU401(this, o.mpu);      // mpu: { synth, remote, present, onFirstUse, onMidi } (dev/mpu401.mjs)
    this.joy = new GamePort(this);           // game port 201h (dev/gameport.mjs)
    this.isaWaitNs = 0;                      // total ISA bus wait charged (spin.mjs)
    this.sliceEndNs = Infinity;              // end of the current CPU slice (runUntil)
    this.setHardware(o);
    if (o.jit !== false) this.enableJit();
    this.loadRom(o.rom || new Uint8Array(0));
    this.initMemory();
    this.reset();
    if (o.cdrom) this.cdrom.insert(o.cdrom);
    if (o.audio) this.audio.start(o.audio.rate || 44100, o.audio.onAudio, o.audio);
  }

  /**
   * Plug in the display card: 'vga' (default) or 'hercules' (+ monitor 'green' |
   * 'amber' | 'white'). Like swapping cards: do it with the power off, i.e.
   * follow it with powerCycle() (the constructor does this itself).
   */
  setVideo(video = 'vga', monitor) {
    if (video === 'hercules') {            // the VGA object stays (render/spin helpers), unplugged
      if (this.hgc) this.hgc.setMonitor(monitor); else this.hgc = new Hercules(this, { monitor });
    } else {
      if (this.hgc) {                      // the empty-bus windows become RAM again
        const pf = this.cpu.pflags;
        for (let l = 0xA0000 >>> 7; l < 0xC0000 >>> 7; l++) pf[l] &= ~1;
        this.hgc = null;
      }
    }
    this.video = this.hgc ? 'hercules' : 'vga';
    this.buildPortmap();
  }

  /**
   * Open the box: set the SIMMs, the clock jumper and which cards are in the slots
   * (options as the constructor's: ram, mhz, sound, mouse, modem, cdrom, video, monitor;
   * what is not given keeps its value). Like setVideo, do it with the power off and
   * follow it with powerCycle(): the RAM is sized when memory is (re)initialised.
   */
  setHardware(o = {}) {
    const h = this.hw;
    if (o.ram !== undefined) h.ram = RAM_OPTIONS.includes(+o.ram) ? +o.ram : 16;
    if (o.sound !== undefined) h.sound = ['sb16', 'adlib', 'none'].includes(o.sound) ? o.sound : 'sb16';
    if (o.mouse !== undefined) h.mouse = o.mouse !== false;
    if (o.modem !== undefined) h.modem = o.modem !== false;
    if (o.cdrom !== undefined) h.cdrom = o.cdrom !== false;
    if (o.joystick !== undefined) h.joystick = o.joystick !== false;
    // 640K of the SIMMs are conventional memory; the 384K behind the adapter hole is relocated
    // above the rest (as the AT chipsets' "384K relocated"), so extended memory is N MB - 640K,
    // up to the 16 MB the 24-bit bus reaches (16 MB of SIMMs: the top 384K is lost, as on an AT)
    this.ramEnd = Math.min(RAM_SIZE, h.ram * 0x100000 + 0x60000);
    if (o.mhz && o.mhz !== this.turboMhz) { this.turboMhz = +o.mhz; this.setTurbo(this.turbo); }
    if ((o.video !== undefined && o.video !== this.video) || (o.monitor !== undefined && this.hgc && o.monitor !== this.hgc.monitor)) this.setVideo(o.video ?? this.video, o.monitor);
    this.kbc.mousePresent = h.mouse;
    this.buildPortmap();
  }
  /** The port -> device map for the cards that are plugged in. */
  buildPortmap() {
    const h = this.hw || HW_DEFAULTS;
    if (!this.hgc && h.sound === 'sb16' && h.modem && h.cdrom && h.joystick !== false) { this.portmap = PORTMAP; return; }
    const pm = this.portmap = PORTMAP.slice();
    const none = (a, b) => { for (let p = a; p <= b; p++) pm[p] = D_NONE; };
    if (this.hgc) { none(0x3C0, 0x3E0); for (let p = 0x3B0; p <= 0x3BF; p++) pm[p] = D_HGC; }
    if (h.sound !== 'sb16') { none(0x220, 0x22F); none(0x330, 0x331); }    // the MIDI daughterboard sits on the SB16
    if (h.sound === 'none') none(0x388, 0x38B);
    if (!h.modem) none(0x2F8, 0x2FF);
    if (!h.cdrom) { none(0x170, 0x177); pm[0x376] = D_NONE; }
    if (h.joystick === false) pm[0x201] = D_NONE;
  }

  enableJit(on = true) { this.cpu.jit = on ? new JIT(this.cpu, this.o.jitOptions) : null; }

  loadRom(rom) {
    if (rom.length > ROM_SIZE) throw new Error('ROM image larger than 1 MB');
    const m8 = this.cpu.m8;
    m8.fill(0xFF, RAM_SIZE, RAM_SIZE + ROM_SIZE);
    m8.set(rom, RAM_SIZE);
    if (this.cpu.jit) this.cpu.jit.flushAll();
  }
  initMemory() {
    const cpu = this.cpu;
    cpu.m8.fill(0, 0, RAM_SIZE);
    cpu.m8.fill(0xFF, HOLE_START, HOLE_END);
    cpu.pflags.fill(0);
    cpu.mmioSeg = 0x10000;
    if (this.vga) this.vga.window = false;
    for (let l = HOLE_START >>> 7; l < HOLE_END >>> 7; l++) cpu.pflags[l] = 1;
    this.lfb = false;
    this.emptyAboveRam();
    this.font.fill(0);
    if (cpu.jit) cpu.jit.flushAll();
  }

  /** No SIMMs above ramEnd: like the empty ISA bus, reads float high (FFh), writes vanish. */
  emptyAboveRam() {
    const end = this.ramEnd ?? RAM_SIZE;
    if (end >= RAM_SIZE) return;
    this.cpu.m8.fill(0xFF, end, RAM_SIZE);
    for (let l = end >>> 7; l < RAM_SIZE >>> 7; l++) this.cpu.pflags[l] = 1;
  }

  /**
   * Host-side font load (the real machine gets its font from the BIOS): copies
   * an 8xN font (N bytes per glyph, 256 glyphs) into the character generator
   * RAM with the hardware's 32-byte stride. Handy for bring-up and tests.
   */
  loadFont(bytes, height = 16) {
    for (let g = 0; g < 256; g++) for (let y = 0; y < 32; y++) this.font[g * 32 + y] = y < height ? bytes[g * height + y] : 0;
  }

  /** Warm reset: CPU + devices; RAM is kept (like a PC's reset line). */
  /** The alt-CPU LED (port F5h): on while ELBOW runs x86 code. onNs accumulates the
   *  emulated time spent lit, so a page can show a duty cycle. */
  setAltCpu(on) {
    const a = this.altCpu, t = this.timeNs();
    if (on && !a.on) { a.on = 1; a.since = t; }
    else if (!on && a.on) { a.on = 0; a.onNs += t - a.since; }
  }
  altCpuNs() { const a = this.altCpu; return a.onNs + (a.on ? this.timeNs() - a.since : 0); }

  reset() {
    if (this.unlocked) this.setTurbo(this.turbo);   // a reset puts the clock limiter back
    this.altCpu = { on: 0, onNs: 0, since: 0 };
    // system board ports F6h/F7h (ARCH.md 4.6): the keyboard layout KEYB has active
    // and the code page of the screen font, for the page's on-screen keyboard
    this.nls = { keyb: 0, cp: 0 };
    // FCh-FFh: the address of ELBOW's debug descriptor (ARCH.md 4.7), for the page's ELBOW view
    this.elbowDesc = 0; this.elbowLatch = 0;
    this.cpu.reset();
    this.pic.reset(); this.pit.reset(); this.kbc.reset(); this.cmos.reset(); this.ata.reset(); this.cdrom.busReset();
    this.fdc.reset(); this.uart.reset(); this.vga.reset(); this.lpt.reset();
    this.setLinearFb(false);
    if (this.hgc) this.hgc.reset();
    this.modem.reset(); this.com2.reset();
    this.dma.reset(); this.sb.reset(); this.mpu.reset(); this.joy.reset();
    if (this.hw.sound === 'adlib') for (const r of [0x30, 0x31, 0x34, 0x35]) this.sb.mixer[r] = 0xF8;   // an AdLib has no mixer: FM at full level
    this.port61 = 0;
    this.pendingReset = false;
    this.cb.reset();
  }
  /** Cold boot: clear RAM too. */
  powerCycle() { this.initMemory(); this.reset(); }
  resetRequest() { this.pendingReset = true; this.cpu.requestStop(); }

  // ------------------------------------------------------------- time
  timeNs() { const c = this.cpu; return this.nowNs + (c.icount + c._n - this.icBase) * this.nsPerInsn; }
  timeMs() { return this.timeNs() / 1e6; }
  sync() { const c = this.cpu; this.nowNs = this.timeNs(); this.icBase = c.icount + c._n; }
  setTurbo(on) { this.sync(); this.turbo = !!on; this.unlocked = false; this.mhz = on ? this.turboMhz : this.slowMhz; this.nsPerInsn = 1000 / this.mhz; }
  /** Port F2h bit 1 (TURBO MAX): the clock limiter is off. The real-time driver then
   *  sets the clock to what the host can deliver (unlockedClock); emulated time still
   *  follows the wall clock, so timers and music keep their speed. */
  setUnlocked(on) {
    if (on) { if (!this.unlocked) { this.setClock(Math.max(this.mhz, this.turboMhz)); this.unlocked = true; } }
    else if (this.unlocked) this.setTurbo(this.turbo);
  }
  setClock(mhz) { this.sync(); this.mhz = mhz; this.nsPerInsn = 1000 / mhz; }
  /** The real-time driver's clock choice while unlocked (RealtimeDriver._unlockedAfter). */
  unlockedClock(want) {
    if (!this.unlocked || !(want > 0)) return;
    const mhz = Math.round(Math.max(this.turboMhz, Math.min(UNLOCKED_MAX_MHZ, want)));
    if (mhz !== this.mhz) this.setClock(mhz);
  }
  reschedule() { this.cpu.requestStop(); }

  nextEventNs() {
    let t = this.pit.nextEventNs();
    const k = this.kbc.nextEventNs(); if (k < t) t = k;
    const c = this.cmos.nextEventNs(); if (c < t) t = c;
    if (this.typeQ.length) { const n = Math.max(this.typeNextNs, 0); if (n < t) t = n; }
    const u = this.com2.nextEventNs(); if (u < t) t = u;
    const md = this.modem.nextEventNs(); if (md < t) t = md;
    const sb = this.sb.nextEventNs(); if (sb < t) t = sb;
    return t;
  }
  processEvents(now) {
    for (let guard = 0; guard < 16; guard++) {
      let any = false;
      if (this.pit.nextEventNs() <= now) { this.pit.service(); any = true; }
      if (this.kbc.nextEventNs() <= now) { const before = this.kbc.obf; this.kbc.service(); if (this.kbc.obf !== before) any = true; }
      if (this.cmos.nextEventNs() <= now) { this.cmos.service(); any = true; }
      if (this.typeQ.length && this.typeNextNs <= now) { this.typeStep(now); any = true; }
      if (this.com2.nextEventNs() <= now) { this.com2.service(); any = true; }
      if (this.modem.nextEventNs() <= now) { this.modem.service(); any = true; }
      if (this.sb.nextEventNs() <= now) { this.sb.service(); any = true; }
      if (!any) break;
    }
  }

  /**
   * Run until emulated time reaches targetNs (or stop/exit). Optional
   * hostDeadline (performance.now() ms) bounds wall time; maxIdleJumpNs caps
   * how far an idle CPU may jump (used by the real-time driver).
   * Returns the number of instructions executed.
   */
  runUntil(targetNs, hostDeadline = Infinity) {
    const cpu = this.cpu;
    const ic0 = cpu.icount;
    let iter = 0;
    while (!this.stopped) {
      this.sync();
      if (this.pendingReset) this.reset();
      const now = this.nowNs;
      this.processEvents(now);
      if (this.audio.enabled && now - this.audio.pumpedNs > 20e6) this.audio.pump();
      if (now >= targetNs) break;
      let next = this.nextEventNs();
      if (next > targetNs) next = targetNs;
      if (next < now) next = now;
      if (cpu.halted && !cpu.irqLine && !cpu.fiqLine) {
        this.haltedNs += next - now;
        this.nowNs = next;
        continue;
      }
      let budget = Math.ceil((next - now) / this.nsPerInsn);
      if (budget < 1) budget = 1; else if (budget > SLICE_MAX) budget = SLICE_MAX;
      const io0 = this.spin.ioOps;
      this.sliceEndNs = next;
      const ran = cpu.run(this.spin.limit(budget));
      this.spin.afterSlice(ran, io0);
      if (this.spin.pending) this.spin.check(targetNs);
      if ((++iter & 15) === 0 && hostDeadline !== Infinity && performance.now() >= hostDeadline) break;
    }
    this.sync();
    this.audio.pump();
    return cpu.icount - ic0;
  }
  runFor(ms) { return this.runUntil(this.timeNs() + ms * 1e6); }

  // ------------------------------------------------------------- bus
  read(a, size) {
    a >>>= 0;
    if (this.cpu.act !== null) this.cpu.act.io(a, 0);       // memory map open (memmap.mjs)
    if ((a >>> 16) === 0xA && this.vga.window) {             // the VGA's planar window: bytes in order, each loads the latches
      const vga = this.vga, o = a & 0xFFFF;
      this.spin.onWrite();                                   // (not a pollable port: never skipped)
      if (size === 1) return vga.winRead(o);
      if (size === 2) { const lo = vga.winRead(o); return lo | (vga.winRead(o + 1) << 8); }
      const b0 = vga.winRead(o), b1 = vga.winRead(o + 1), b2 = vga.winRead(o + 2);
      return (b0 | (b1 << 8) | (b2 << 16) | (vga.winRead(o + 3) << 24)) >>> 0;
    }
    if ((a >>> 16) === 0x1000) {
      const port = a & 0xFFFF;
      if (size === 1) { const v = this.in8(port); this.spin.onRead(port, v); return v; }
      this.spin.onRead(port, -2);
      if (size === 2) {
        if (port === 0x1F0) return this.ata.readData16();
        if (port === 0x170) return this.cdrom.readData16();
        return this.in8(port) | (this.in8((port + 1) & 0xFFFF) << 8);
      }
      if (port === 0x1F0) return (this.ata.readData16() | (this.ata.readData16() << 16)) >>> 0;
      if (port === 0x170) return (this.cdrom.readData16() | (this.cdrom.readData16() << 16)) >>> 0;
      return (this.in8(port) | (this.in8(port + 1) << 8) | (this.in8(port + 2) << 16) | (this.in8(port + 3) << 24)) >>> 0;
    }
    if (a >= FONT_BASE && a < FONT_BASE + FONT_SIZE) {
      const o = a - FONT_BASE, f = this.font;
      if (size === 1) return f[o];
      if (size === 2) return f[o] | (f[o + 1] << 8);
      return (f[o] | (f[o + 1] << 8) | (f[o + 2] << 16) | (f[o + 3] << 24)) >>> 0;
    }
    return -1;
  }
  write(a, size, v) {
    a >>>= 0;
    if (this.cpu.act !== null) this.cpu.act.io(a, 1);
    if ((a >>> 16) === 0xA && this.vga.window) {
      const vga = this.vga, o = a & 0xFFFF;
      this.spin.onWrite();
      vga.winWrite(o, v & 0xFF);
      if (size >= 2) vga.winWrite(o + 1, (v >>> 8) & 0xFF);
      if (size === 4) { vga.winWrite(o + 2, (v >>> 16) & 0xFF); vga.winWrite(o + 3, (v >>> 24) & 0xFF); }
      return true;
    }
    if ((a >>> 16) === 0x1000) {
      const port = a & 0xFFFF;
      this.spin.onWrite();
      if (size === 1) { this.out8(port, v & 0xFF); return true; }
      if (port === 0x1F0) {
        this.ata.writeData16(v & 0xFFFF);
        if (size === 4) this.ata.writeData16((v >>> 16) & 0xFFFF);
        return true;
      }
      if (port === 0x170) {
        this.cdrom.writeData16(v & 0xFFFF);
        if (size === 4) this.cdrom.writeData16((v >>> 16) & 0xFFFF);
        return true;
      }
      this.out8(port, v & 0xFF); this.out8((port + 1) & 0xFFFF, (v >>> 8) & 0xFF);
      if (size === 4) { this.out8((port + 2) & 0xFFFF, (v >>> 16) & 0xFF); this.out8((port + 3) & 0xFFFF, (v >>> 24) & 0xFF); }
      return true;
    }
    if (a >= FONT_BASE && a < FONT_BASE + FONT_SIZE) {
      const o = a - FONT_BASE, f = this.font;
      f[o] = v; if (size >= 2) f[o + 1] = v >>> 8;
      if (size === 4) { f[o + 2] = v >>> 16; f[o + 3] = v >>> 24; }
      this.vga.dirty = true;
      return true;
    }
    return false;
  }

  in8(port) {
    switch (this.portmap[port]) {
      case D_PIC: return this.pic.read(port);
      case D_PIT: return this.pit.read(port);
      case D_KBC: return this.kbc.read(port);
      case D_SYSB: {
        const t = this.timeNs();
        return (this.port61 & 3) | ((Math.floor(t / 15085) & 1) << 4) | (this.pit.out2() << 5);
      }
      case D_CMOS: return this.cmos.read(port);
      case D_ATA: return this.ata.read(port);
      case D_CD: return this.cdrom.read(port);
      case D_FDC: return this.fdc.read(port);
      case D_UART: return this.uart.read(port);
      case D_COM2: return this.com2.read(port);
      case D_LPT: return this.lpt.read(port);
      case D_DMA: return this.dma.read(port);
      case D_SB: return this.sb.read(port);
      case D_OPL: return this.sb.opl.read(port - 0x388);
      case D_MPU: return this.mpu.read(port);
      case D_JOY: { const v = this.joy.read(); this.isaWait(ISA_IO_NS); return v; }
      case D_E9: return 0xE9;
      case D_VGA: return this.vga.read(port);
      case D_HGC: return this.hgc.read(port);
      case D_BOARD:
        switch (port) {
          case 0xF0: return 0x41;
          case 0xF1: return Math.min(255, this.mhz);
          case 0xF2: return (this.turbo ? 1 : 0) | (this.unlocked ? 2 : 0);
          case 0xF5: return this.altCpu.on;
          case 0xF6: return this.nls.keyb;
          case 0xF7: return this.nls.cp;
          case 0xF8: { this.icSnapshot = Math.floor((this.cpu.icount + this.cpu._n) / 1e6) >>> 0; return this.icSnapshot & 0xFF; }
          case 0xF9: return (this.icSnapshot >>> 8) & 0xFF;
          case 0xFA: return (this.icSnapshot >>> 16) & 0xFF;
          case 0xFB: return (this.icSnapshot >>> 24) & 0xFF;
          case 0xFC: case 0xFD: case 0xFE: case 0xFF: return (this.elbowDesc >>> ((port - 0xFC) * 8)) & 0xFF;
        }
        return 0xFF;
    }
    return 0xFF;
  }
  out8(port, v) {
    switch (this.portmap[port]) {
      case D_PIC: this.pic.write(port, v); return;
      case D_PIT: this.pit.write(port, v); return;
      case D_KBC: this.kbc.write(port, v); return;
      case D_SYSB: this.port61 = v; this.pit.setGate2(v & 1); this.pit.speakerUpdate(); return;
      case D_CMOS: this.cmos.write(port, v); return;
      case D_ATA: this.ata.write(port, v); return;
      case D_CD: this.cdrom.write(port, v); return;
      case D_FDC: this.fdc.write(port, v); return;
      case D_UART: this.uart.write(port, v); return;
      case D_COM2: this.com2.write(port, v); return;
      case D_LPT: this.lpt.write(port, v); return;
      case D_DMA: this.dma.write(port, v); return;
      case D_SB: this.sb.write(port, v); return;
      case D_OPL: this.sb.opl.write(port - 0x388, v); return;
      case D_MPU: this.mpu.write(port, v); return;
      case D_JOY: this.joy.write(); this.isaWait(ISA_IO_NS); return;
      case D_E9: this.cb.debug(v); return;
      case D_VGA: this.vga.write(port, v); return;
      case D_HGC: this.hgc.write(port, v); return;
      case D_BOARD:
        if (port === 0xF4) { this.exitCode = v; this.stopped = true; this.cpu.requestStop(); this.cb.exit(v); }
        else if (port === 0xF2) { this.setTurbo(!!(v & 1)); if (v & 2) this.setUnlocked(true); }
        else if (port === 0xF5) this.setAltCpu(v & 1);      // ELBOW's "x86 running" LED
        else if (port === 0xF6) this.nls.keyb = v;          // KEYB's layout (ARCH.md 4.6)
        else if (port === 0xF7) this.nls.cp = v;            // DISPLAY.SYS's code page
        else if (port >= 0xFC) {                            // ELBOW's descriptor address (ARCH.md 4.7)
          const sh = (port - 0xFC) * 8;
          this.elbowLatch = ((this.elbowLatch & ~(0xFF << sh)) | (v << sh)) >>> 0;
          if (port === 0xFF) this.elbowDesc = this.elbowLatch;
        }
        return;
    }
    if (port === 0x80) this.postCode = v;     // POST code latch (debug aid only)
  }

  // ------------------------------------------------------------- device glue
  /** An I/O cycle that holds the CPU for ns of bus time (the game port's 1 us ISA cycle).
   *  Emulated time moves on without instructions; a slice that ran past its end stops. */
  isaWait(ns) {
    this.nowNs += ns; this.isaWaitNs += ns;
    if (this.timeNs() >= this.sliceEndNs) this.cpu.requestStop();
  }
  get speakerEnable() { return (this.port61 & 2) !== 0; }
  speaker(on, freq) { this.cb.speaker(on, freq); this.audio.speaker(on, freq); }
  serialOut(b) { this.cb.serial(b); }
  printOut(b) { this.cb.print(b); }
  leds(bits) { this.cb.leds(bits); }
  modeChanged(mode) { this.setLinearFb(mode === 0x62); this.cb.mode(mode); }
  /** The VGA's planar window (dev/vga.mjs): A0000h-AFFFFh stops being RAM and
   *  becomes an MMIO window onto the four planes (stores through pflags bit 3,
   *  loads through cpu.mmioSeg; compiled code is flushed so the JIT's load fast
   *  paths get the extra check only while the window is open). */
  setVgaWindow(on) {
    const cpu = this.cpu, pf = cpu.pflags;
    for (let l = 0xA0000 >>> 7; l < 0xB0000 >>> 7; l++) pf[l] = on ? pf[l] | 8 : pf[l] & ~8;
    cpu.mmioSeg = on ? 0xA : 0x10000;
    if (cpu.jit) cpu.jit.flushAll();
    cpu.brk = 1;
  }
  /** Mode 62h on/off: the video memory from C0000h to LFB_END becomes RAM on
   *  the card (cleared when it appears), or the empty hole again (reads FFh,
   *  writes vanish). */
  setLinearFb(on) {
    on = !!on;
    if (on === !!this.lfb || this.hgc) return;
    this.lfb = on;
    const cpu = this.cpu;
    cpu.m8.fill(on ? 0 : 0xFF, HOLE_START, LFB_END);
    for (let l = HOLE_START >>> 7; l < LFB_END >>> 7; l++) cpu.pflags[l] = on ? 0 : 1;
  }
  diskActivity(drive, lba, count, isWrite, cyl, prevCyl) { this.cb.disk(drive, lba, count, isWrite, cyl, prevCyl); }
  diskWritten(drive, lba, count) { this.cb.diskWrite(drive, lba, count); }
  dmaToMemory(a, bytes) {
    if (this.cpu.act !== null) this.cpu.act.span(a, bytes.length, 1);
    this.cpu.hostWrite(a, bytes);
    const hs = this.lfb ? LFB_END : HOLE_START;
    if (a < HOLE_END && a + bytes.length > hs) this.cpu.m8.fill(0xFF, Math.max(a, hs), Math.min(a + bytes.length, HOLE_END));
    if (a + bytes.length > this.ramEnd) this.cpu.m8.fill(0xFF, Math.max(a, this.ramEnd), Math.min(a + bytes.length, RAM_SIZE));
  }

  // ------------------------------------------------------------- host API
  keyDown(code) { return this.kbc.keyDown(code); }
  keyUp(code) { return this.kbc.keyUp(code); }
  mouseMove(dx, dy) { this.kbc.mouseMove(dx, dy); }
  mouseButtons(bits) { this.kbc.mouseButtons(bits); }
  serialInput(str) { this.uart.input(typeof str === 'string' ? [...str].map((c) => c.charCodeAt(0)) : str); }
  insertFloppy(img, writeProtected = false) { this.fdc.insert(img, writeProtected); }
  ejectFloppy() { this.fdc.eject(); }
  get floppy() { return this.fdc.img; }

  /**
   * Queue text to be typed (US layout). Escapes: {ESC} {ENTER} {F1}..{F12}
   * {UP} {DOWN} {LEFT} {RIGHT} {HOME} {END} {PGUP} {PGDN} {INS} {DEL} {TAB}
   * {BS} {PAUSE} {WAIT:ms}, combos like {CTRL+C} {ALT+F} {CTRL+ALT+DEL}, and
   * raw KeyboardEvent.code names like {KeyA} or {Numpad5}.
   */
  typeText(text) {
    const q = this.typeQ;
    let i = 0;
    const press = (codes) => { for (const c of codes) q.push({ code: c, down: true }); for (const c of codes.slice().reverse()) q.push({ code: c, down: false }); };
    while (i < text.length) {
      const ch = text[i];
      if (ch === '{') {
        const j = text.indexOf('}', i);
        if (j > i) {
          const name = text.slice(i + 1, j); i = j + 1;
          if (/^WAIT:\d+$/i.test(name)) { q.push({ wait: +name.split(':')[1] * 1e6 }); continue; }
          const parts = name.split('+').map((p) => KEYNAMES[p.toUpperCase()] || (p.length === 1 && CHARMAP[p.toLowerCase()] ? CHARMAP[p.toLowerCase()][0] : p));
          press(parts);
          continue;
        }
      }
      i++;
      const map = CHARMAP[ch];
      if (!map) continue;
      press(map[1] ? ['ShiftLeft', map[0]] : [map[0]]);
    }
    if (this.typeNextNs < this.timeNs()) this.typeNextNs = this.timeNs();
    this.reschedule();
  }
  typeStep(now) {
    // wait until the controller has drained what we sent before
    if (!this.kbc.queueEmpty()) { this.typeNextNs = now + 1e6; return; }
    const e0 = this.typeQ[0];
    if (e0.down) {                 // don't overrun the BIOS type-ahead buffer (BDA 0x41A/0x41C)
      const m8 = this.cpu.m8, hd = m8[0x41A] | (m8[0x41B] << 8), tl = m8[0x41C] | (m8[0x41D] << 8);
      if (hd >= 0x1E && hd < 0x3E && tl >= 0x1E && tl < 0x3E && ((tl - hd + 32) % 32) / 2 >= 12) { this.typeNextNs = now + 5e6; return; }
    }
    const e = this.typeQ.shift();
    if (e.wait) { this.typeNextNs = now + e.wait; return; }
    if (e.down) this.kbc.keyDown(e.code); else this.kbc.keyUp(e.code);
    this.typeNextNs = now + this.typeDelayNs;
  }
  typingDone() { return this.typeQ.length === 0 && this.kbc.queueEmpty(); }

  /** BIOS keyboard ring buffer (BDA 0x41A head / 0x41C tail) is empty. */
  biosKeyBufferEmpty() {
    const m8 = this.cpu.m8;
    return (m8[0x41A] | (m8[0x41B] << 8)) === (m8[0x41C] | (m8[0x41D] << 8));
  }
}

/**
 * Real-time driver: runs the machine in slices following the wall clock,
 * never ahead of it. Works in browsers (requestAnimationFrame) and node
 * (setTimeout). onFrame(stats) is called after each slice.
 */
export class RealtimeDriver {
  constructor(machine, { onFrame = noop, maxSliceMs = 12, maxLagMs = 200 } = {}) {
    this.m = machine; this.onFrame = onFrame; this.maxSliceMs = maxSliceMs; this.maxLagMs = maxLagMs;
    this.running = false; this._u0 = null;
    this.stats = { mips: 0, effMips: 0, hostLoad: 0, emuMs: 0, lagMs: 0 };
    this._acc = { insns: 0, busyMs: 0, wallMs: 0 };
    this._tick = this._tick.bind(this);
  }
  start() {
    if (this.running) return;
    this.running = true;
    this.wall0 = performance.now(); this.emu0 = this.m.timeNs();
    this._last = this.wall0;
    this._schedule();
  }
  stop() { this.running = false; }
  // TURBO MAX: the clock follows the host frame by frame, erring high. Each slice
  // measures how much wall time the CPU needed per emulated millisecond. With room to
  // spare (under UNLOCKED_BUSY of the frame) the clock rises, up to 2x per frame; it
  // only comes down when emulation falls behind the wall clock, and never below the
  // turbo clock. (Much of what a heavy program costs doesn't scale with the clock -
  // planar VGA writes, scan-line waits - and frame-locked code spends extra cycles in
  // polling loops the spin detector skips, so a lower clock rarely buys time back.)
  _unlockedBefore() { this._u0 = this.m.timeNs(); }
  // (this.ceil: see _unlockedAfter)
  _unlockedAfter(t0, t1, target) {
    const m = this.m;
    if (this._u0 === null || this._u0 === undefined) return;       // (unlocked during this slice)
    const dEmu = (m.timeNs() - this._u0) / 1e6, busy = t1 - t0;
    if (!(dEmu > 0.5)) return;
    const frac = busy / dEmu, behind = target - m.timeNs() > 10e6;
    // falling behind also sets a ceiling (80% of the clock that failed) that lifts ~35%/s,
    // so the clock doesn't bounce straight back into the same wall
    this.ceil = behind ? m.mhz * 0.8 : Math.min(UNLOCKED_MAX_MHZ, (this.ceil || UNLOCKED_MAX_MHZ) * 1.005);
    let want = m.mhz;
    if (behind) want *= Math.min(0.8, Math.max(0.5, UNLOCKED_BUSY / frac));
    else if (frac < UNLOCKED_BUSY) want = Math.min(this.ceil, want * Math.min(2, UNLOCKED_BUSY / Math.max(frac, 1e-3)));
    if (want !== m.mhz) m.unlockedClock(want);
  }
  _schedule() {
    if (!this.running) return;
    if (typeof requestAnimationFrame === 'function' && !(typeof document !== 'undefined' && document.hidden)) requestAnimationFrame(this._tick);
    else setTimeout(this._tick, 4);
  }
  _tick() {
    if (!this.running) return;
    const m = this.m;
    const t0 = performance.now();
    let target = this.emu0 + (t0 - this.wall0) * 1e6;
    const lag = target - m.timeNs();
    if (lag > this.maxLagMs * 1e6) {           // can't keep up: drop the debt
      this.emu0 -= lag - this.maxLagMs * 1e6; target = m.timeNs() + this.maxLagMs * 1e6;
    }
    if (m.unlocked) this._unlockedBefore();
    else this._u0 = this.ceil = null;
    const n = m.stopped ? 0 : m.runUntil(target, t0 + this.maxSliceMs);
    const t1 = performance.now();
    if (m.unlocked) this._unlockedAfter(t0, t1, target);
    const a = this._acc; a.insns += n; a.busyMs += t1 - t0; a.wallMs += t1 - this._last; this._last = t1;
    if (a.wallMs >= 500) {
      this.stats.mips = a.busyMs > 0 ? a.insns / a.busyMs / 1000 : 0;   // host speed while running
      this.stats.effMips = a.insns / a.wallMs / 1000;                    // delivered instructions per wall second
      this.stats.hostLoad = a.busyMs / a.wallMs;
      a.insns = 0; a.busyMs = 0; a.wallMs = 0;
    }
    this.stats.emuMs = m.timeNs() / 1e6;
    this.stats.lagMs = (target - m.timeNs()) / 1e6;
    this.onFrame(this.stats);
    this._schedule();
  }
}
