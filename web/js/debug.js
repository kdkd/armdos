// Run control for the inspector: a real-time driver that can stop on
// breakpoints, single-stepping, and the interrupt log.
//
// The emulator has no debugger hooks of its own, so this uses two it already
// has: the interpreter's per-instruction trace callback (cpu.traceRec, only
// active while cpu.trace is set, and only in the interpreter, so the JIT is
// switched off while breakpoints exist) and instance-level wrappers around
// cpu.exception (SVC = "INT n") and pic.ack (IRQs). Nothing is installed while
// the inspector is closed and no breakpoint is set, so the machine runs at
// full JIT speed.

import { RealtimeDriver } from '../emu/machine.js';

const BREAK = { toString() { return 'ARM-DOS breakpoint'; } };

export class Driver extends RealtimeDriver {
  constructor(m, opts) {
    super(m, opts);
    this.onBreak = opts.onBreak || (() => {});
    this.onCrash = opts.onCrash || (() => {});
  }
  _tick() {
    try { super._tick(); }
    catch (e) {
      this.running = false;
      if (e === BREAK) this.onBreak();
      else { console.error(e); this.onCrash(e); }
    }
  }
}

export class Debugger {
  constructor(machine) {
    this.m = machine;
    this.bps = new Set();          // addresses (unsigned)
    this.temp = null;              // one-shot breakpoint (STEP OVER)
    this.skip = -1;                // resume past a breakpoint at this pc once
    this.traceOn = false;
    this.logOn = false;
    this.onLog = null;             // (entry) => void
    this.lastInt = null;
  }

  // ------------------------------------------------------------ breakpoints
  addBp(a) { this.bps.add(a >>> 0); this.syncTrace(); }
  removeBp(a) { this.bps.delete(a >>> 0); this.syncTrace(); }
  toggleBp(a) { if (this.bps.has(a >>> 0)) this.removeBp(a); else this.addBp(a); }
  syncTrace() {
    const need = this.bps.size > 0 || this.temp !== null;
    const cpu = this.m.cpu;
    if (need && !this.traceOn) {
      this.traceOn = true;
      this.m.enableJit(false);
      cpu.trace = new Int32Array(2);
      cpu.traceRec = (pc) => this.check(pc);
    } else if (!need && this.traceOn) {
      this.traceOn = false;
      delete cpu.traceRec; cpu.trace = null;
      this.m.enableJit(true);
    }
  }
  check(pc) {
    pc >>>= 0;
    if (pc === this.skip) { this.skip = -1; return; }
    this.skip = -1;
    if (this.bps.has(pc) || pc === this.temp) {
      const c = this.m.cpu;
      c.pc = pc | 0;
      c.icount += c._n; c._n = 0;          // keep the instruction count (and so emulated time) honest
      if (pc === this.temp) { this.temp = null; this.hitTemp = true; }
      this.hit = pc;
      throw BREAK;
    }
  }
  /** Call before resuming so a breakpoint at the current pc doesn't fire again at once. */
  prepareResume() { this.skip = this.m.cpu.pc >>> 0; this.hit = null; }
  clearTemp() { if (this.temp !== null) { this.temp = null; this.syncTrace(); } }

  // ------------------------------------------------------------ stepping
  /** Execute exactly one instruction (advancing time past WFI sleep if needed). */
  step() {
    const m = this.m, c = m.cpu;
    m.sync();
    m.processEvents(m.nowNs);
    for (let guard = 0; c.halted && !c.irqLine && !c.fiqLine && guard < 100000; guard++) {
      const nx = m.nextEventNs();
      if (!isFinite(nx)) break;
      if (nx > m.nowNs) { m.haltedNs += nx - m.nowNs; m.nowNs = nx; }
      m.processEvents(m.nowNs);
    }
    this.skip = c.pc >>> 0;
    try { c.step(); } catch (e) { if (e !== BREAK) throw e; }
    this.skip = -1;
    m.sync();
  }
  /** If the instruction at pc is a call (BL, BLX, SVC), return the address after it. */
  callReturn() {
    const c = this.m.cpu, pc = c.pc >>> 0;
    if (c.t) {
      const h = c.fetchHalf(pc) & 0xFFFF;
      if ((h & 0xF800) === 0xF000) return (pc + 4) >>> 0;          // BL/BLX pair
      if ((h & 0xFF87) === 0x4780) return (pc + 2) >>> 0;          // BLX Rm
      if ((h & 0xFF00) === 0xDF00) return (pc + 2) >>> 0;          // SVC
      return null;
    }
    const w = c.fetchWord(pc) >>> 0;
    if ((w & 0x0F000000) === 0x0B000000 && (w >>> 28) !== 15) return (pc + 4) >>> 0;   // BL
    if ((w >>> 25) === 0x7D) return (pc + 4) >>> 0;                                        // BLX imm
    if ((w & 0x0FFFFFF0) === 0x012FFF30) return (pc + 4) >>> 0;                           // BLX Rm
    if ((w & 0x0F000000) === 0x0F000000 && (w >>> 28) !== 15) return (pc + 4) >>> 0;   // SVC
    return null;
  }
  /** Arm a one-shot breakpoint after the call at pc. Returns false if pc is not a call. */
  armStepOver() {
    const ret = this.callReturn();
    if (ret === null) return false;
    this.temp = ret; this.syncTrace();
    return true;
  }

  // ------------------------------------------------------------ interrupt log
  setLogging(on) {
    const m = this.m, c = m.cpu, pic = m.pic;
    if (on === this.logOn) return;
    this.logOn = on;
    if (on) {
      const proto = Object.getPrototypeOf(c), pproto = Object.getPrototypeOf(pic);
      c.exception = (off, mode, lr) => {
        if (off === 0x08) this.logSvc(lr);
        return proto.exception.call(c, off, mode, lr);
      };
      pic.ack = () => {
        const n = pproto.ack.call(pic);
        if (n !== 0xFF && n >= 0) this.logIrq(n);
        return n;
      };
    } else {
      delete c.exception; delete pic.ack;
    }
  }
  logSvc(lr) {
    const c = this.m.cpu;
    const n = c.t ? (c.fetchHalf((lr - 2) >>> 0) & 0xFF) : (c.fetchWord((lr - 4) >>> 0) & 0xFF);
    const ah = (c.r[0] >>> 8) & 0xFF, al = c.r[0] & 0xFF;
    const e = { kind: 'int', n, ah, al, t: this.m.timeMs() };
    this.lastInt = e;
    if (this.onLog) this.onLog(e);
  }
  logIrq(irq) {
    const e = { kind: 'irq', irq, n: irq < 8 ? 8 + irq : 0x70 + irq - 8, t: this.m.timeMs() };
    if (this.onLog) this.onLog(e);
  }
}

// Names for the interrupt log. (INT number -> name, or -> {AH: name})
const INTS = {
  0x08: 'timer tick', 0x09: 'keyboard', 0x0E: 'floppy', 0x70: 'RTC', 0x74: 'mouse', 0x76: 'hard disk', 0x0C: 'COM1',
  0x10: { _: 'video', 0x00: 'set mode', 0x01: 'cursor shape', 0x02: 'set cursor', 0x03: 'get cursor', 0x05: 'page', 0x06: 'scroll up', 0x07: 'scroll down', 0x08: 'read char', 0x09: 'write char+attr', 0x0A: 'write char', 0x0C: 'put pixel', 0x0D: 'get pixel', 0x0E: 'teletype', 0x0F: 'get mode', 0x10: 'palette', 0x11: 'font', 0x12: 'info', 0x13: 'write string', 0x1A: 'display code' },
  0x11: 'equipment', 0x12: 'memory size',
  0x13: { _: 'disk', 0x00: 'reset', 0x01: 'status', 0x02: 'read sectors', 0x03: 'write sectors', 0x04: 'verify', 0x08: 'drive params', 0x15: 'drive type', 0x16: 'change line', 0x41: 'ext check', 0x42: 'ext read', 0x43: 'ext write' },
  0x14: { _: 'serial', 0x00: 'init', 0x01: 'send', 0x02: 'receive', 0x03: 'status' },
  0x15: { _: 'system', 0x86: 'wait', 0x88: 'ext memory', 0xC0: 'config', 0xC2: 'mouse' },
  0x16: { _: 'keyboard', 0x00: 'read key', 0x01: 'key ready?', 0x02: 'shift flags', 0x05: 'stuff key', 0x10: 'read key', 0x11: 'key ready?', 0x12: 'shift flags' },
  0x17: { _: 'printer', 0x00: 'print char', 0x01: 'init', 0x02: 'status' },
  0x18: 'ROM BASIC', 0x19: 'bootstrap',
  0x1A: { _: 'time', 0x00: 'ticks', 0x01: 'set ticks', 0x02: 'RTC time', 0x03: 'set RTC time', 0x04: 'RTC date', 0x05: 'set RTC date' },
  0x1B: 'ctrl-break', 0x1C: 'user tick', 0x20: 'terminate', 0x22: 'terminate addr', 0x23: 'ctrl-C', 0x24: 'critical error',
  0x25: 'abs disk read', 0x26: 'abs disk write', 0x27: 'TSR', 0x28: 'DOS idle', 0x29: 'fast console', 0x2E: 'COMMAND exec',
  0x2F: { _: 'multiplex', 0x43: 'XMS', 0x10: 'SHARE', 0x11: 'network', 0x01: 'PRINT', 0xAE: 'COMMAND hook' },
  0x33: { _: 'mouse' },
  0x21: { _: 'DOS', 0x00: 'terminate', 0x01: 'read char', 0x02: 'write char', 0x05: 'print char', 0x06: 'direct console', 0x07: 'raw input', 0x08: 'input', 0x09: 'print string', 0x0A: 'buffered input', 0x0B: 'input status', 0x0C: 'flush + input', 0x0D: 'disk reset', 0x0E: 'select drive',
    0x19: 'current drive', 0x1A: 'set DTA', 0x1C: 'drive info', 0x25: 'set vector', 0x29: 'parse filename', 0x2A: 'get date', 0x2B: 'set date', 0x2C: 'get time', 0x2D: 'set time', 0x2F: 'get DTA',
    0x30: 'version', 0x31: 'TSR', 0x33: 'break flag', 0x34: 'InDOS ptr', 0x35: 'get vector', 0x36: 'free space', 0x37: 'switch char', 0x38: 'country',
    0x39: 'mkdir', 0x3A: 'rmdir', 0x3B: 'chdir', 0x3C: 'create', 0x3D: 'open', 0x3E: 'close', 0x3F: 'read', 0x40: 'write', 0x41: 'delete', 0x42: 'seek', 0x43: 'attributes',
    0x44: 'IOCTL', 0x45: 'dup', 0x46: 'dup2', 0x47: 'get cwd', 0x48: 'alloc', 0x49: 'free', 0x4A: 'resize', 0x4B: 'exec', 0x4C: 'exit', 0x4D: 'exit code', 0x4E: 'find first', 0x4F: 'find next',
    0x50: 'set PSP', 0x51: 'get PSP', 0x52: 'list of lists', 0x54: 'verify flag', 0x56: 'rename', 0x57: 'file date', 0x58: 'alloc strategy', 0x59: 'extended error', 0x5A: 'temp file', 0x5B: 'create new',
    0x5C: 'lock', 0x60: 'truename', 0x62: 'get PSP', 0x65: 'country info', 0x66: 'code page', 0x67: 'handle count', 0x68: 'commit', 0x6C: 'extended open' },
};
const IRQNAMES = ['timer', 'keyboard', 'cascade', 'COM2', 'COM1', 'LPT2', 'floppy', 'LPT1', 'RTC', 'IRQ9', 'IRQ10', 'IRQ11', 'mouse', 'FPU', 'hard disk', 'IRQ15'];

const h2 = (v) => v.toString(16).toUpperCase().padStart(2, '0');
/** -> [main text, comment] for a log entry */
export function describe(e) {
  if (e.kind === 'irq') return [`IRQ${e.irq} → INT ${h2(e.n)}h`, IRQNAMES[e.irq]];
  const d = INTS[e.n];
  if (d && typeof d === 'object') {
    const name = d[e.ah];
    return [`INT ${h2(e.n)}h AH=${h2(e.ah)}h`, name || d._];
  }
  return [`INT ${h2(e.n)}h`, d || ''];
}
