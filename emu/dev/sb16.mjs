// Sound Blaster 16 (CT1745 mixer, DSP 4.05) at 0x220, IRQ 7, DMA 1 / 5, and
// its OPL3 (YMF262) at 0x220-0x223 / 0x228-0x229 and 0x388-0x38B (AdLib).
// ARCH.md §4.3 is the contract; BLASTER=A220 I7 D1 H5 T6.
//
// DSP (base+6 reset, +0xA read data, +0xC command/data (read: bit 7 = busy),
// +0xE read-buffer status and 8-bit IRQ acknowledge, +0xF 16-bit acknowledge):
//   10 direct DAC; 14/1C/90/91 8-bit single/auto-init/high-speed output; 24/2C/
//   98/99 8-bit input; 16/17/74-77/7D/7F ADPCM (played as 8-bit PCM - not
//   decoded); 40 time constant; 41/42 rate; 48 block size; 80 silence; Bx/Cx
//   SB16 16/8-bit I/O (mode byte bit 4 signed, bit 5 stereo); D0/D4 D5/D6
//   pause/continue; D1/D3/D8 speaker; D9/DA exit auto-init; 45/47 continue
//   auto-init; E0 invert; E1 version 4.05; E3 copyright; E4/E8 test register;
//   F2/F3 raise IRQ; 04/05/0E/0F (ASP) accepted.
// Transfers are paced by emulated time at the programmed rate: the DSP pulls
// each frame through the 8237 (dev/dma.mjs) when it is due (lazily, but always
// before anything could observe the difference: at block ends, on DSP/DMA
// register access, and when audio is rendered), and raises its IRQ at the
// exact emulated time a block ends. A masked or finished DMA channel yields
// silence (the DSP keeps its timing). Input transfers record silence.
// Mixer (base+4 index, +5 data): 00 reset, 04/22/26/28/2E/0A SB Pro aliases,
// 0E SB Pro stereo, 30-47 SB16 volumes (master 30/31, voice 32/33, FM 34/35
// are applied to the output), 80 IRQ select, 81 DMA select, 82 IRQ status.

import { OPL3, OPL_RATE } from './opl3.mjs';

const OPL_NS = 1e9 / OPL_RATE;
const COPYRIGHT = 'COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.';

// command -> number of argument bytes
const ARGS = new Int8Array(256).fill(-1);
(() => {
  const set = (list, n) => { for (const c of list) ARGS[c] = n; };
  set([0x1C, 0x1F, 0x20, 0x2C, 0x45, 0x47, 0x7D, 0x7F, 0x90, 0x91, 0x98, 0x99, 0xA0, 0xA8,
    0xD0, 0xD1, 0xD3, 0xD4, 0xD5, 0xD6, 0xD8, 0xD9, 0xDA, 0xE1, 0xE3, 0xE7, 0xE8,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF8, 0xFA, 0xFB, 0xFC, 0xFD, 0x30, 0x31, 0x34, 0x35, 0x36, 0x37], 0);
  set([0x04, 0x0F, 0x10, 0x38, 0x40, 0xE0, 0xE2, 0xE4], 1);
  set([0x05, 0x0E, 0x14, 0x16, 0x17, 0x24, 0x41, 0x42, 0x48, 0x74, 0x75, 0x76, 0x77, 0x80], 2);
  for (let c = 0xB0; c <= 0xCF; c++) ARGS[c] = 3;
})();

const volGain = (reg) => { const v = reg >> 3; return v === 0 ? 0 : Math.pow(10, (v - 31) / 10); };   // 2 dB steps

/** A queue of timestamped DAC samples (the SB's output as it happened). */
class PcmQueue {
  constructor() { this.cap = 4096; this.t = new Float64Array(this.cap); this.l = new Float32Array(this.cap); this.r = new Float32Array(this.cap); this.head = 0; this.len = 0; }
  push(t, l, r) {
    if (this.head + this.len >= this.cap) {
      if (this.head > this.cap / 2) { this.t.copyWithin(0, this.head, this.head + this.len); this.l.copyWithin(0, this.head, this.head + this.len); this.r.copyWithin(0, this.head, this.head + this.len); this.head = 0; }
      else {
        const c = this.cap * 2, t = new Float64Array(c), l = new Float32Array(c), r = new Float32Array(c);
        t.set(this.t.subarray(this.head, this.head + this.len)); l.set(this.l.subarray(this.head, this.head + this.len)); r.set(this.r.subarray(this.head, this.head + this.len));
        this.t = t; this.l = l; this.r = r; this.cap = c; this.head = 0;
      }
    }
    const i = this.head + this.len++;
    this.t[i] = t; this.l[i] = l; this.r[i] = r;
  }
  clear() { this.head = 0; this.len = 0; }
}

/** The OPL3 as seen on the ISA bus: address latches, timers, status, and the timed register stream. */
export class OplPort {
  constructor(m) {
    this.m = m;
    this.chip = new OPL3();
    this.reset();
  }
  reset() {
    this.chip.reset();
    this.addr = [0, 0];
    this.t1 = { val: 0, run: false, start: 0, flag: 0, mask: 0, unit: 80000 };
    this.t2 = { val: 0, run: false, start: 0, flag: 0, mask: 0, unit: 320000 };
    this.queue = []; this.qi = 0;
    this.nextNs = 0; this.prevL = 0; this.prevR = 0; this.curL = 0; this.curR = 0;
    this.idle = true; this.sinceCheck = 0;
  }
  audioStart(now) { this.queue.length = 0; this.nextNs = now; this.prevL = this.prevR = this.curL = this.curR = 0; this.idle = this.chip.isSilent(); }
  audioStop() { for (let i = 0; i < this.queue.length; i += 3) this.chip.writeReg(this.queue[i + 1], this.queue[i + 2]); this.queue.length = 0; }

  timerUpdate(t, now) {
    if (!t.run) return;
    const period = (256 - t.val) * t.unit;
    if (now >= t.start + period) {
      if (!t.mask) t.flag = 1;
      t.start += Math.floor((now - t.start) / period) * period;
    }
  }
  status() {
    const now = this.m.timeNs();
    this.timerUpdate(this.t1, now); this.timerUpdate(this.t2, now);
    const f = (this.t1.flag << 6) | (this.t2.flag << 5);
    return f ? f | 0x80 : 0;          // bits 1-2 = 0: an OPL3 (an OPL2 would read 06h there)
  }
  /** off: 0 = address bank 0, 1 = data, 2 = address bank 1, 3 = data bank 1 */
  write(off, v) {
    if (!(off & 1)) { this.addr[off >> 1] = v; return; }
    const reg = ((off >> 1) << 8) | this.addr[off >> 1];
    const now = this.m.timeNs();
    if (reg === 0x02 || reg === 0x03 || reg === 0x04) {
      this.timerUpdate(this.t1, now); this.timerUpdate(this.t2, now);
      if (reg === 0x02) this.t1.val = v;
      else if (reg === 0x03) this.t2.val = v;
      else if (v & 0x80) {                     // reset the IRQ / status flags
        this.t1.flag = 0; this.t2.flag = 0;
      } else {
        this.t1.mask = (v >> 6) & 1; this.t2.mask = (v >> 5) & 1;
        for (const [t, bit] of [[this.t1, 1], [this.t2, 2]]) {
          const on = !!(v & bit);
          if (on && !t.run) { t.run = true; t.start = now; } else if (!on) t.run = false;
        }
      }
      return;
    }
    if (this.m.audio.enabled) this.queue.push(now, reg, v);
    else this.chip.writeReg(reg, v);
  }
  read(off) { return (off & 1) ? 0xFF : this.status(); }

  /** Mix into L/R: n output samples from t0, dt ns apart, with gains gl/gr. */
  render(L, R, n, t0, dt, gl, gr) {
    const q = this.queue, chip = this.chip;
    let qi = 0;
    const sl = gl / 32768, sr = gr / 32768;
    for (let i = 0; i < n; i++) {
      const t = t0 + i * dt;
      while (this.nextNs <= t) {
        if (qi < q.length && q[qi] <= this.nextNs) {
          do { chip.writeReg(q[qi + 1], q[qi + 2]); qi += 3; } while (qi < q.length && q[qi] <= this.nextNs);
          this.idle = false;
        }
        this.prevL = this.curL; this.prevR = this.curR;
        if (this.idle) { this.curL = 0; this.curR = 0; }
        else {
          chip.generate(); this.curL = chip.outL; this.curR = chip.outR;
          if (++this.sinceCheck >= 512) { this.sinceCheck = 0; if (chip.isSilent()) this.idle = true; }
        }
        this.nextNs += OPL_NS;
      }
      if (sl === 0 && sr === 0) continue;
      const f = 1 - (this.nextNs - t) / OPL_NS;
      L[i] += (this.prevL + (this.curL - this.prevL) * f) * sl;
      R[i] += (this.prevR + (this.curR - this.prevR) * f) * sr;
    }
    q.splice(0, qi);
  }
}

export class SB16 {
  constructor(m, { base = 0x220, irq = 7, dma8 = 1, dma16 = 5 } = {}) {
    this.m = m;
    this.base = base; this.cfg = { irq, dma8, dma16 };
    this.opl = new OplPort(m);
    this.pcm = new PcmQueue();
    this.mixer = new Uint8Array(256);
    this.buf8 = new Uint8Array(0);
    m.dma.onAccess(() => this.catchUp());
    this.reset();
  }

  reset() {
    this.irq = this.cfg.irq; this.dma8 = this.cfg.dma8; this.dma16 = this.cfg.dma16;
    this.mixerIndex = 0; this.mixerReset();
    this.opl.reset();
    this.dspReset();
    this.resetLine = 0;
  }
  dspReset() {
    this.stopTransfer();
    this.outQ = []; this.lastOut = 0xAA;
    this.cmd = -1; this.args = []; this.need = 0;
    this.rate = 22050; this.rateIsTc = false; this.tc = 0;
    this.blockSize = 0x800;
    this.speakerOn = false; this.highSpeed = false; this.testReg = 0;
    this.irq8 = false; this.irq16 = false;
    this.aaAtNs = -1;
    if (this.m.pic) this.m.pic.lower(this.irqLine());
  }
  mixerReset() {
    const x = this.mixer;
    x.fill(0);
    for (const r of [0x30, 0x31, 0x32, 0x33, 0x34, 0x35]) x[r] = 0xC0;      // -14 dB, as the CT1745 powers up
    x[0x3C] = 0x1F; x[0x3D] = 0x15; x[0x3E] = 0x0B;
    for (const r of [0x44, 0x45, 0x46, 0x47]) x[r] = 0x80;
    this.setIrqDmaRegs();
  }
  setIrqDmaRegs() {
    this.mixer[0x80] = { 2: 1, 9: 1, 5: 2, 7: 4, 10: 8 }[this.irq] || 0;
    this.mixer[0x81] = ((this.dma8 < 4 ? 1 << this.dma8 : 0) | (this.dma16 >= 4 && this.dma16 !== this.dma8 ? 1 << this.dma16 : 0)) & 0xFF;
  }
  irqLine() { return this.irq === 2 ? 9 : this.irq; }

  audioStart(now) { this.pcm.clear(); this.pcmPrev = { t: now, l: 0, r: 0 }; this.opl.audioStart(now); }
  audioStop() { this.opl.audioStop(); this.pcm.clear(); }

  // ------------------------------------------------------------ ports
  read(port) {
    const off = port - this.base;
    if (off <= 3) return this.opl.read(off);
    if (off === 8 || off === 9) return this.opl.read(off - 8);
    switch (off) {
      case 0x4: return this.mixerIndex;
      case 0x5: return this.mixerRead(this.mixerIndex);
      case 0xA: this.checkReset(); if (this.outQ.length) this.lastOut = this.outQ.shift(); return this.lastOut;
      case 0xC: return 0x7F;                                   // always ready for the next byte
      case 0xE: this.checkReset(); this.ack(8); return this.outQ.length ? 0xFF : 0x7F;
      case 0xF: this.ack(16); return 0xFF;
    }
    return 0xFF;
  }
  write(port, v) {
    const off = port - this.base;
    if (off <= 3) { this.opl.write(off, v); return; }
    if (off === 8 || off === 9) { this.opl.write(off - 8, v); return; }
    switch (off) {
      case 0x4: this.mixerIndex = v; return;
      case 0x5: this.mixerWrite(this.mixerIndex, v); return;
      case 0x6:
        if ((v & 1) && !this.resetLine) { this.dspReset(); this.outQ = []; }
        else if (!(v & 1) && this.resetLine) this.aaAtNs = this.m.timeNs() + 20000;     // 0AAh after 20 us
        this.resetLine = v & 1;
        return;
      case 0xC: this.dspWrite(v); return;
    }
  }
  checkReset() {
    if (this.aaAtNs >= 0 && this.m.timeNs() >= this.aaAtNs) { this.aaAtNs = -1; this.outQ = [0xAA]; }
  }
  ack(bits) {
    if (bits === 8) this.irq8 = false; else this.irq16 = false;
    if (!this.irq8 && !this.irq16) this.m.pic.lower(this.irqLine());
  }
  raiseIrq(bits) {
    if (bits === 8) this.irq8 = true; else this.irq16 = true;
    this.m.pic.raise(this.irqLine());
  }

  // ------------------------------------------------------------ mixer
  mixerRead(i) {
    const x = this.mixer;
    const nib = (a, b) => (x[a] & 0xF0) | (x[b] >> 4);
    switch (i) {
      case 0x04: return nib(0x32, 0x33);
      case 0x22: return nib(0x30, 0x31);
      case 0x26: return nib(0x34, 0x35);
      case 0x28: return nib(0x36, 0x37);
      case 0x2E: return nib(0x38, 0x39);
      case 0x0A: return (x[0x3A] >> 5) & 7;
      case 0x82: return (this.irq8 ? 1 : 0) | (this.irq16 ? 2 : 0) | 0x20;
    }
    return x[i];
  }
  mixerWrite(i, v) {
    const x = this.mixer;
    const split = (a, b) => { x[a] = (((v >> 4) << 1) | 1) << 3; x[b] = (((v & 15) << 1) | 1) << 3; };
    switch (i) {
      case 0x00: this.mixerReset(); return;
      case 0x04: split(0x32, 0x33); return;
      case 0x22: split(0x30, 0x31); return;
      case 0x26: split(0x34, 0x35); return;
      case 0x28: split(0x36, 0x37); return;
      case 0x2E: split(0x38, 0x39); return;
      case 0x0A: x[0x3A] = ((v & 7) << 5) | 0x10; return;
      case 0x80: {
        const irq = v & 1 ? 9 : v & 2 ? 5 : v & 4 ? 7 : v & 8 ? 10 : 0;
        if (irq) { this.m.pic.lower(this.irqLine()); this.irq = irq; if (this.irq8 || this.irq16) this.m.pic.raise(this.irqLine()); }
        this.setIrqDmaRegs(); return;
      }
      case 0x81: {
        const d8 = v & 1 ? 0 : v & 2 ? 1 : v & 8 ? 3 : -1;
        const d16 = v & 0x20 ? 5 : v & 0x40 ? 6 : v & 0x80 ? 7 : -1;
        if (d8 >= 0) this.dma8 = d8;
        this.dma16 = d16 >= 0 ? d16 : this.dma8;
        this.setIrqDmaRegs(); return;
      }
      case 0x82: return;
    }
    x[i] = v;
  }

  // ------------------------------------------------------------ DSP commands
  dspWrite(v) {
    if (this.highSpeed && this.pb && this.pb.active) return;        // only a reset leaves high-speed mode
    if (this.cmd < 0) {
      if (ARGS[v] < 0) return;                                   // unknown: ignored
      this.cmd = v; this.args = []; this.need = ARGS[v];
    } else this.args.push(v);
    if (this.args.length < this.need) return;
    const c = this.cmd, a = this.args;
    this.cmd = -1;
    this.command(c, a);
  }
  reply(...bytes) { this.checkReset(); this.outQ.push(...bytes); }
  command(c, a) {
    const len16 = () => a[0] | (a[1] << 8);
    const legacyStereo = () => (this.mixer[0x0E] & 2) !== 0;
    switch (c) {
      case 0x10: this.catchUp(); this.dac(a[0]); return;
      case 0x14: case 0x16: case 0x17: case 0x74: case 0x75: case 0x76: case 0x77:
        this.startTransfer({ bits: 8, stereo: legacyStereo(), signed: false, auto: false, input: false, units: len16() + 1, legacy: true }); return;
      case 0x1C: case 0x1F: case 0x7D: case 0x7F:
        this.startTransfer({ bits: 8, stereo: legacyStereo(), signed: false, auto: true, input: false, units: this.blockSize, legacy: true }); return;
      case 0x90: case 0x91:
        this.highSpeed = true;
        this.startTransfer({ bits: 8, stereo: legacyStereo(), signed: false, auto: c === 0x90, input: false, units: this.blockSize, legacy: true }); return;
      case 0x24: this.startTransfer({ bits: 8, stereo: false, signed: false, auto: false, input: true, units: len16() + 1, legacy: true }); return;
      case 0x2C: this.startTransfer({ bits: 8, stereo: false, signed: false, auto: true, input: true, units: this.blockSize, legacy: true }); return;
      case 0x98: case 0x99:
        this.highSpeed = true;
        this.startTransfer({ bits: 8, stereo: false, signed: false, auto: c === 0x98, input: true, units: this.blockSize, legacy: true }); return;
      case 0x20: this.reply(0x80); return;
      case 0x40: this.tc = a[0]; this.rateIsTc = true; return;
      case 0x41: case 0x42: this.rate = (a[0] << 8) | a[1]; this.rateIsTc = false; return;
      case 0x48: this.blockSize = len16() + 1; return;
      case 0x80: this.startTransfer({ bits: 8, stereo: false, signed: false, auto: false, input: false, units: len16() + 1, legacy: true, silent: true }); return;
      case 0xD0: case 0xD5: this.pause(true); return;
      case 0xD4: case 0xD6: this.pause(false); return;
      case 0xD1: this.speakerOn = true; return;
      case 0xD3: this.speakerOn = false; return;
      case 0xD8: this.reply(this.speakerOn ? 0xFF : 0x00); return;
      case 0xD9: case 0xDA: if (this.pb) this.pb.exitAuto = true; return;
      case 0x45: case 0x47: if (this.pb) this.pb.exitAuto = false; return;
      case 0xE0: this.reply((~a[0]) & 0xFF); return;
      case 0xE1: this.reply(4, 5); return;
      case 0xE3: this.reply(...[...COPYRIGHT].map((ch) => ch.charCodeAt(0)), 0); return;
      case 0xE4: this.testReg = a[0]; return;
      case 0xE8: this.reply(this.testReg); return;
      case 0xF2: this.raiseIrq(8); return;
      case 0xF3: this.raiseIrq(16); return;
      case 0xF8: this.reply(0); return;
      case 0x0F: this.reply(0); return;
    }
    if (c >= 0xB0 && c <= 0xCF && !(c & 1)) {                    // SB16 Bx (16-bit) / Cx (8-bit)
      const bits = c < 0xC0 ? 16 : 8, mode = a[0];
      this.startTransfer({
        bits, stereo: (mode & 0x20) !== 0, signed: (mode & 0x10) !== 0, auto: (c & 4) !== 0,
        input: (c & 8) !== 0, units: (a[1] | (a[2] << 8)) + 1, legacy: false,
      });
    }
  }

  // ------------------------------------------------------------ transfers
  frameRate(stereo, legacy) {
    if (!this.rateIsTc) return Math.max(1000, Math.min(this.rate, 48000));   // 41h/42h: frames per second
    const bytesPerSec = 1e6 / (256 - this.tc);
    return legacy && stereo ? bytesPerSec / 2 : bytesPerSec;
  }
  startTransfer(o) {
    this.catchUp();
    const now = this.m.timeNs();
    const ch = o.bits === 16 ? this.dma16 : this.dma8;
    const wide = ch >= 4;
    const bytesPerFrame = (o.bits / 8) * (o.stereo ? 2 : 1);
    const unitsPerFrame = wide ? Math.max(1, bytesPerFrame >> 1) : bytesPerFrame;
    // the DSP's count is in samples (8-bit: bytes; 16-bit: words), stereo counting both channels
    const frames = Math.max(1, Math.floor(o.units / (o.stereo ? 2 : 1)));
    const rate = this.frameRate(o.stereo, o.legacy);
    this.pb = {
      ...o, ch, wide, bytesPerFrame, unitsPerFrame, frames, frameNs: 1e9 / rate,
      startNs: now, done: 0, active: true, paused: false, exitAuto: false,
      irqBits: o.bits === 16 ? 16 : 8,
    };
    const need = frames * bytesPerFrame + 4;
    if (this.buf8.length < need) this.buf8 = new Uint8Array(need);
    this.m.reschedule();
  }
  stopTransfer() {
    if (this.pb && this.pb.active) { this.catchUp(); this.dacSilence(); }
    this.pb = null;
    this.highSpeed = false;
  }
  pause(on) {
    const p = this.pb; if (!p || !p.active) return;
    this.catchUp();
    if (on && !p.paused) { p.paused = true; this.dacSilence(); }
    else if (!on && p.paused) { p.paused = false; p.startNs = this.m.timeNs() - p.done * p.frameNs; }
    this.m.reschedule();
  }
  /** Pull every frame that is due by now (or by `upTo`) through DMA. */
  catchUp(upTo = this.m.timeNs()) {
    const p = this.pb;
    if (!p || !p.active || p.paused) return;
    let due = Math.floor((upTo - p.startNs) / p.frameNs + 1e-7);
    if (due > p.frames) due = p.frames;
    const k = due - p.done;
    if (k <= 0) return;
    const dma = this.m.dma, b = this.buf8;
    if (p.silent) {
      if (this.m.audio.enabled) for (let i = 0; i < k; i++) this.pcmPush(p.startNs + (p.done + i) * p.frameNs, 0, 0);
      p.done = due; return;
    }
    const units = k * p.unitsPerFrame;
    if (p.input) {
      b.fill(p.bits === 8 && !p.signed ? 0x80 : 0, 0, units * (p.wide ? 2 : 1));
      dma.write8or16(p.ch, units, b, 0);
      p.done = due; return;
    }
    const got = dma.read8or16(p.ch, units, b, 0);
    const bytesGot = got * (p.wide ? 2 : 1);
    if (this.m.audio.enabled) {
      const bpf = p.bytesPerFrame;
      for (let i = 0; i < k; i++) {
        const o = i * bpf;
        let l = 0, r = 0;
        if (o + bpf <= bytesGot) {
          if (p.bits === 8) {
            l = p.signed ? ((b[o] << 24) >> 24) / 128 : (b[o] - 128) / 128;
            r = p.stereo ? (p.signed ? ((b[o + 1] << 24) >> 24) / 128 : (b[o + 1] - 128) / 128) : l;
          } else {
            const s0 = b[o] | (b[o + 1] << 8);
            l = p.signed ? ((s0 << 16) >> 16) / 32768 : (s0 - 32768) / 32768;
            if (p.stereo) { const s1 = b[o + 2] | (b[o + 3] << 8); r = p.signed ? ((s1 << 16) >> 16) / 32768 : (s1 - 32768) / 32768; }
            else r = l;
          }
        }
        this.pcmPush(p.startNs + (p.done + i) * p.frameNs, l, r);
      }
    }
    p.done = due;
  }
  pcmPush(t, l, r) { this.pcm.push(t, l, r); }
  dac(v) { if (this.m.audio.enabled) { const s = (v - 128) / 128; this.pcmPush(this.m.timeNs(), s, s); } }
  dacSilence() { if (this.m.audio.enabled) this.pcmPush(this.m.timeNs(), 0, 0); }

  nextEventNs() {
    const p = this.pb;
    return p && p.active && !p.paused ? p.startNs + p.frames * p.frameNs : Infinity;
  }
  service() {
    const p = this.pb;
    const end = p.startNs + p.frames * p.frameNs;
    this.catchUp(end);
    this.raiseIrq(p.irqBits);
    if (p.auto && !p.exitAuto) { p.startNs = end; p.done = 0; }
    else { p.active = false; this.dacSilence(); this.pb = null; this.highSpeed = false; }
  }

  // ------------------------------------------------------------ rendering
  render(L, R, n, t0, dt) {
    const x = this.mixer;
    const mL = volGain(x[0x30]), mR = volGain(x[0x31]);
    this.catchUp(t0 + n * dt);
    // the DAC: interpolate the timestamped sample stream
    const q = this.pcm, vL = volGain(x[0x32]) * mL, vR = volGain(x[0x33]) * mR;
    let prev = this.pcmPrev;
    let h = q.head, end = q.head + q.len;
    // Render 0.25 ms late: interpolation needs the frame AFTER each output sample, and
    // catchUp() only fetches frames due by the chunk's end. Without this lag the last
    // samples of every chunk held flat and the next chunk jumped (audible crackle on loud
    // low-rate PCM). 250 us covers any SB frame period (>= 4 kHz) and is inaudible.
    const LAG = 250000;
    for (let i = 0; i < n; i++) {
      const t = t0 + i * dt - LAG;
      while (h < end && q.t[h] <= t) { prev = { t: q.t[h], l: q.l[h], r: q.r[h] }; h++; }
      let l = prev.l, r = prev.r;
      if (h < end) {
        const span = q.t[h] - prev.t;
        if (span > 0 && span < 2e6) { const f = (t - prev.t) / span; l += (q.l[h] - l) * f; r += (q.r[h] - r) * f; }
      }
      L[i] += l * vL; R[i] += r * vR;
    }
    q.len -= h - q.head; q.head = h;
    if (!q.len) q.head = 0;
    this.pcmPrev = prev;
    // FM
    this.opl.render(L, R, n, t0, dt, volGain(x[0x34]) * mL, volGain(x[0x35]) * mR);
  }
}
