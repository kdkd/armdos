// ARM-DOS machine: ARMv5TE CPU core (ARM926EJ-S flavour), ARM + Thumb.
//
// Two execution tiers share one architectural state:
//   * a table-dispatched interpreter (this file) - the reference semantics,
//     used for cold code, single-stepping and tracing;
//   * a basic-block JIT (jit.mjs) that turns hot blocks into JS functions.
//
// State conventions (both tiers rely on them):
//   r[0..15]   Int32Array, current-mode view. While an instruction executes,
//              r[15] holds the architectural PC read value (addr+8 ARM, +4 Thumb).
//   nv, zv     N is set iff nv < 0, Z is set iff zv === 0 (a flag-setting op just
//              stores its result in both).
//   c, v, q    0/1.
//   t, i, f    0/1 (CPSR T, I, F).  mode = CPSR[4:0].
//   pc         address of the next instruction to execute (outside run()).
//
// Memory: one ArrayBuffer = 16 MB RAM followed by the 1 MB ROM. RAM is at
// physical 0, ROM at 0xFFF00000. Everything else goes through bus.read/bus.write.
// pflags[addr>>>7] (one byte per 128-byte line of RAM): 0 plain RAM, bit0 =
// write-ignored (the 0xC0000-0xFFFFF hole), bit1 = line holds JIT-compiled code,
// bit2 = armed by the memory map (memmap.mjs): the next store records the line,
// bit3 = MMIO (the VGA's planar window at A0000h): stores go to the bus; loads
// from the segment mmioSeg (A000h then) are sent to the bus as well.

import { vfpInit, vfpReset, vfpExec, vfpState, vfpUpdate } from './vfp.mjs';

export const RAM_SIZE = 0x1000000;
export const ROM_BASE = 0xFFF00000;
export const ROM_SIZE = 0x100000;

export const USR = 0x10, FIQ = 0x11, IRQ = 0x12, SVC = 0x13, ABT = 0x17, UND = 0x1B, SYS = 0x1F;

export const ABORT = { toString() { return 'ARM ABORT'; } };   // thrown by memory slow paths

// bank index per mode (low 5 bits); usr/sys share bank 0; invalid modes -> 0
const BANK = new Int8Array(32);
BANK[FIQ] = 1; BANK[IRQ] = 2; BANK[SVC] = 3; BANK[ABT] = 4; BANK[UND] = 5;
const VALID_MODE = new Uint8Array(32);
for (const m of [USR, FIQ, IRQ, SVC, ABT, UND, SYS]) VALID_MODE[m] = 1;

// ---------------------------------------------------------------------------
// ARM decode table: index = bits[27:20] << 4 | bits[7:4]
export const
  H_UND = 0, H_DP = 1, H_MUL = 2, H_MULL = 3, H_SWP = 4, H_XLS = 5, H_MRS = 6, H_MSR = 7,
  H_BX = 8, H_BLXR = 9, H_CLZ = 10, H_QOP = 11, H_BKPT = 12, H_SMULXY = 13,
  H_LS = 14, H_LDM = 15, H_B = 16, H_BL = 17, H_SVC = 18, H_CPREG = 19, H_CPOTHER = 20;

export const ARM_TABLE = new Uint8Array(4096);
(function buildTable() {
  for (let idx = 0; idx < 4096; idx++) {
    const hi = idx >>> 4, lo = idx & 15;        // hi = bits 27:20, lo = bits 7:4
    const top = hi >>> 5;                       // bits 27:25
    const b24_23 = (hi >>> 3) & 3, b20 = hi & 1, b21 = (hi >>> 1) & 1;
    let h = H_UND;
    switch (top) {
      case 0:
        if (lo === 9) {
          if ((hi & 0x10) === 0) h = (hi & 0x08) ? H_MULL : ((hi & 0x04) === 0 ? H_MUL : H_UND);
          else h = ((hi & 0x1B) === 0x10) ? H_SWP : H_UND;
        } else if ((lo & 9) === 9) {
          h = H_XLS;                               // extra load/store (lo = 1011,1101,1111)
        } else if (b24_23 === 2 && b20 === 0) {   // misc space
          const op = (hi >>> 1) & 3;
          switch (lo) {
            case 0: h = b21 ? H_MSR : H_MRS; break;
            case 1: h = op === 1 ? H_BX : op === 3 ? H_CLZ : H_UND; break;
            case 2: h = op === 1 ? H_BX : H_UND; break;          // BXJ = BX
            case 3: h = op === 1 ? H_BLXR : H_UND; break;
            case 5: h = H_QOP; break;
            case 7: h = op === 1 ? H_BKPT : H_UND; break;
            case 8: case 10: case 12: case 14: h = H_SMULXY; break;
            default: h = H_UND;
          }
        } else h = H_DP;
        break;
      case 1:
        if (b24_23 === 2 && b20 === 0) h = b21 ? H_MSR : H_UND;
        else h = H_DP;
        break;
      case 2: h = H_LS; break;
      case 3: h = (lo & 1) ? H_UND : H_LS; break;
      case 4: h = H_LDM; break;
      case 5: h = (hi & 0x10) ? H_BL : H_B; break;
      case 6: h = H_CPOTHER; break;                              // LDC/STC/MCRR/MRRC
      case 7: h = (hi & 0x10) ? H_SVC : ((lo & 1) ? H_CPREG : H_CPOTHER); break;
    }
    ARM_TABLE[idx] = h;
  }

})();

// 32x32 -> 64 multiply helpers; results in MR[0] (lo), MR[1] (hi)
export const MR = new Int32Array(2);
export function umul64(a, b) {
  a >>>= 0; b >>>= 0;
  const al = a & 0xFFFF, ah = a >>> 16, bl = b & 0xFFFF, bh = b >>> 16;
  const mid = al * bh + ah * bl;                 // < 2^33
  const lo = al * bl + (mid % 65536) * 65536;    // < 2^33
  const hi = ah * bh + Math.floor(mid / 65536) + Math.floor(lo / 4294967296);
  MR[0] = lo; MR[1] = hi;                        // Int32Array store wraps mod 2^32
}
export function smul64(a, b) {
  umul64(a, b);
  let hi = MR[1];
  if (a < 0) hi = (hi - b) | 0;
  if (b < 0) hi = (hi - a) | 0;
  MR[1] = hi;
}
export function add64(lo, hi, lo2, hi2) {
  const l = (lo >>> 0) + (lo2 >>> 0);
  MR[0] = l;
  MR[1] = (hi + hi2 + (l > 0xFFFFFFFF ? 1 : 0)) | 0;
}
function sat32(x) {  // x is a double; returns [value], sets SAT flag
  if (x > 0x7FFFFFFF) { SAT = 1; return 0x7FFFFFFF; }
  if (x < -0x80000000) { SAT = 1; return -0x80000000; }
  return x | 0;
}
let SAT = 0;

export class CPU {
  constructor(bus) {
    this.bus = bus;                       // { read(addr,size)->u32|-1, write(addr,size,v)->bool, pageWritten? }
    this.buf = new ArrayBuffer(RAM_SIZE + ROM_SIZE);
    this.m8 = new Uint8Array(this.buf);
    this.m16 = new Uint16Array(this.buf);
    this.m32 = new Int32Array(this.buf);
    this.pflags = new Uint8Array(RAM_SIZE >>> 7);
    this.mmioSeg = 0x10000;     // a 64 KB RAM segment (addr >>> 16) whose loads go to the bus: the VGA's planar window (none)
    this.r = new Int32Array(16);
    this.r13b = new Int32Array(6); this.r14b = new Int32Array(6); this.spsrb = new Int32Array(6);
    this.fiqBank = new Int32Array(5); this.usrBank = new Int32Array(5);
    this.nv = 0; this.zv = 1; this.c = 0; this.v = 0; this.q = 0;
    this.t = 0; this.i = 1; this.f = 1; this.mode = SVC;
    this.pc = 0;
    this.icount = 0;          // instructions retired (double)
    this.irqLine = 0; this.fiqLine = 0;
    this.halted = 0;          // WFI
    this.brk = 0;             // request: leave the inner loop ASAP
    this.stopRun = 0;         // request: leave run() ASAP
    this._n = 0;              // instructions retired so far in the current run() call (for timing)
    this._nb = 0;             // base added by the interpreter loops to their local count
    this.cp15ctl = 0x00052078;
    this.cp15 = new Int32Array(16);
    vfpInit(this);            // VFP9-S (VFPv2) state: F32/F64/FW, fpscr, fpexc (see vfp.mjs)
    this.jit = null;          // optional JIT tier (jit.mjs); may be detached temporarily (step(), spin checks)
    this.codeCache = null;    // the JIT that owns the compiled-code line flags - invalidation always goes here,
                              // even while this.jit is detached (else stale code survives: keyb regression)
    this.act = null;          // optional memory activity counters (memmap.mjs; used by jit.mjs runAct)
    this.trace = null;        // optional Int32Array ring [pc, insn, ...]
    this.traceHook = null;    // optional (pc, insn) callback instead of the ring (only while trace is set)
    this.tracePos = 0;
    this.onFault = null;      // optional host hook (kind, pc, addr) on undefined/abort
    this.lastFault = null;
    this.reset();
  }

  // ------------------------------------------------------------------ state
  reset() {
    this.r.fill(0); this.r13b.fill(0); this.r14b.fill(0); this.spsrb.fill(0);
    this.fiqBank.fill(0); this.usrBank.fill(0);
    this.mode = SVC; this.t = 0; this.i = 1; this.f = 1;
    this.nv = 0; this.zv = 1; this.c = 0; this.v = 0; this.q = 0;
    this.cp15ctl = 0x00052078;          // ARM926 reset value with V (bit 13) = 1
    this.cp15.fill(0);
    if (this.FW) vfpReset(this);
    this.pc = 0xFFFF0000 | 0;
    this.halted = 0; this.brk = 1;
  }

  get vbase() { return (this.cp15ctl & 0x2000) ? 0xFFFF0000 | 0 : 0; }

  getCPSR() {
    return ((this.nv < 0 ? 0x80000000 : 0) | (this.zv === 0 ? 0x40000000 : 0) |
      (this.c << 29) | (this.v << 28) | (this.q << 27) |
      (this.i << 7) | (this.f << 6) | (this.t << 5) | this.mode) | 0;
  }

  setFlags(val) {   // bits 31:27
    this.nv = val < 0 ? -1 : 0;
    this.zv = (val & 0x40000000) ? 0 : 1;
    this.c = (val >>> 29) & 1; this.v = (val >>> 28) & 1; this.q = (val >>> 27) & 1;
  }

  // write CPSR fields (mask of bytes: bit0 c, bit1 x, bit2 s, bit3 f); allowT for exception returns
  setCPSR(val, fieldMask = 15, allowT = true) {
    if (fieldMask & 8) this.setFlags(val);
    if ((fieldMask & 1) && this.mode !== USR) {
      this.i = (val >>> 7) & 1; this.f = (val >>> 6) & 1;
      if (allowT) this.t = (val >>> 5) & 1;
      let m = val & 0x1F; if (!VALID_MODE[m]) m = this.mode;
      this.switchMode(m);
    }
    this.brk = 1;
  }

  get spsr() { const b = BANK[this.mode]; return b ? this.spsrb[b] : this.getCPSR(); }
  set spsr(v) { const b = BANK[this.mode]; if (b) this.spsrb[b] = v; }

  switchMode(nm) {
    const om = this.mode;
    if (om === nm) return;
    const r = this.r, ob = BANK[om], nb = BANK[nm];
    if (ob !== nb) {
      this.r13b[ob] = r[13]; this.r14b[ob] = r[14];
      if (om === FIQ) { for (let k = 0; k < 5; k++) { this.fiqBank[k] = r[8 + k]; r[8 + k] = this.usrBank[k]; } }
      if (nm === FIQ) { for (let k = 0; k < 5; k++) { this.usrBank[k] = r[8 + k]; r[8 + k] = this.fiqBank[k]; } }
      r[13] = this.r13b[nb]; r[14] = this.r14b[nb];
    }
    this.mode = nm;
  }

  // user-bank register access (LDM/STM with ^)
  getUserReg(n) {
    if (n < 8 || n === 15) return this.r[n];
    if (n < 13) return this.mode === FIQ ? this.usrBank[n - 8] : this.r[n];
    if (BANK[this.mode] === 0) return this.r[n];
    return n === 13 ? this.r13b[0] : this.r14b[0];
  }
  setUserReg(n, v) {
    if (n < 8 || n === 15) { this.r[n] = v; return; }
    if (n < 13) { if (this.mode === FIQ) this.usrBank[n - 8] = v; else this.r[n] = v; return; }
    if (BANK[this.mode] === 0) { this.r[n] = v; return; }
    if (n === 13) this.r13b[0] = v; else this.r14b[0] = v;
  }

  // generic register accessors in any mode's bank (debugger / tests)
  regs() {
    const o = [];
    for (let k = 0; k < 16; k++) o.push(this.r[k] >>> 0);
    return o;
  }

  // ------------------------------------------------------------- exceptions
  exception(off, nmode, lr) {
    const cpsr = this.getCPSR();
    this.switchMode(nmode);
    this.spsrb[BANK[nmode]] = cpsr;
    this.r[14] = lr;
    this.t = 0; this.i = 1;
    if (nmode === FIQ || off === 0) this.f = 1;
    this.pc = (this.vbase + off) | 0;
    this.brk = 1;
    this.halted = 0;
  }
  undefinedInsn(pc) {  // pc = address of the undefined instruction
    if (this.onFault) this.onFault('undefined', pc, pc);
    this.exception(0x04, UND, (pc + (this.t ? 2 : 4)) | 0);
    return this.pc;
  }
  prefetchAbort(pc) {
    this.lastFault = { type: 'prefetch', pc: pc >>> 0, addr: pc >>> 0 };
    if (this.onFault) this.onFault('prefetch', pc, pc);
    this.exception(0x0C, ABT, (pc + 4) | 0);
  }
  // A memory slow path hit unmapped memory: record the fault and unwind. The
  // exception is entered by takeDataAbort() in the catcher (cpu.run / the JIT
  // dispatcher), after compiled code has written its register copies back.
  dataAbort(pc, addr) {
    this.lastFault = { type: 'data', pc: pc >>> 0, addr: addr >>> 0 };
    throw ABORT;
  }
  takeDataAbort() {
    const f = this.lastFault;
    if (this.onFault) this.onFault('data', f.pc | 0, f.addr | 0);
    this.cp15[6] = f.addr | 0;         // FAR
    this.cp15[5] = 0x8;                // FSR-ish: external abort
    this.exception(0x10, ABT, (f.pc + 8) | 0);
  }
  takeIRQ() { this.exception(0x18, IRQ, (this.pc + 4) | 0); }
  takeFIQ() { this.exception(0x1C, FIQ, (this.pc + 4) | 0); }

  // ------------------------------------------------------------ memory slow
  // All take pc (address of the executing instruction) for abort reporting.
  ld32(a, pc) {            // LDR semantics (rotating unaligned)
    let w;
    if ((a & 0xFF000000) === 0 && (a >>> 16) !== this.mmioSeg) w = this.m32[a >>> 2];
    else if ((a >>> 20) === 0xFFF) w = this.m32[((a & 0xFFFFF) + RAM_SIZE) >>> 2];
    else { w = this.bus.read(a & ~3, 4); if (w === -1) this.dataAbort(pc, a); }
    const rot = (a & 3) << 3;
    return rot ? ((w >>> rot) | (w << (32 - rot))) : (w | 0);
  }
  ld32a(a, pc) {           // aligned word (LDM, LDRD): low bits ignored
    if ((a & 0xFF000000) === 0 && (a >>> 16) !== this.mmioSeg) return this.m32[a >>> 2];
    if ((a >>> 20) === 0xFFF) return this.m32[((a & 0xFFFFF) + RAM_SIZE) >>> 2];
    const w = this.bus.read(a & ~3, 4); if (w === -1) this.dataAbort(pc, a);
    return w | 0;
  }
  ld16(a, pc) {            // zero-extended halfword; bit0 ignored
    if ((a & 0xFF000000) === 0 && (a >>> 16) !== this.mmioSeg) return this.m16[a >>> 1];
    if ((a >>> 20) === 0xFFF) return this.m16[((a & 0xFFFFF) + RAM_SIZE) >>> 1];
    const w = this.bus.read(a & ~1, 2); if (w === -1) this.dataAbort(pc, a);
    return w & 0xFFFF;
  }
  ld8(a, pc) {
    if ((a & 0xFF000000) === 0 && (a >>> 16) !== this.mmioSeg) return this.m8[a];
    if ((a >>> 20) === 0xFFF) return this.m8[(a & 0xFFFFF) + RAM_SIZE];
    const w = this.bus.read(a, 1); if (w === -1) this.dataAbort(pc, a);
    return w & 0xFF;
  }
  st32(a, v, pc) {
    a &= ~3;
    if ((a & 0xFF000000) === 0) {
      const pf = this.pflags[a >>> 7];
      if (pf & 8) { this.bus.write(a, 4, v); return; }     // the VGA's planar window
      if (pf & 1) return;
      if (pf & 4) this.lineWritten(a);
      if (pf & 2) this.codeWrite(a, 4);
      this.m32[a >>> 2] = v; return;
    }
    if ((a >>> 20) === 0xFFF) return;             // ROM: writes ignored
    if (!this.bus.write(a, 4, v)) this.dataAbort(pc, a);
  }
  st16(a, v, pc) {
    a &= ~1;
    if ((a & 0xFF000000) === 0) {
      const pf = this.pflags[a >>> 7];
      if (pf & 8) { this.bus.write(a, 2, v & 0xFFFF); return; }     // the VGA's planar window
      if (pf & 1) return;
      if (pf & 4) this.lineWritten(a);
      if (pf & 2) this.codeWrite(a, 2);
      this.m16[a >>> 1] = v; return;
    }
    if ((a >>> 20) === 0xFFF) return;
    if (!this.bus.write(a, 2, v & 0xFFFF)) this.dataAbort(pc, a);
  }
  st8(a, v, pc) {
    if ((a & 0xFF000000) === 0) {
      const pf = this.pflags[a >>> 7];
      if (pf & 8) { this.bus.write(a, 1, v & 0xFF); return; }     // the VGA's planar window
      if (pf & 1) return;
      if (pf & 4) this.lineWritten(a);
      if (pf & 2) this.codeWrite(a, 1);
      this.m8[a] = v; return;
    }
    if ((a >>> 20) === 0xFFF) return;
    if (!this.bus.write(a, 1, v & 0xFF)) this.dataAbort(pc, a);
  }
  lineWritten(a) {         // first store to a line the memory map armed (bit 2) since its last frame
    this.pflags[a >>> 7] &= ~4;
    if (this.act !== null) this.act.dirty(a);
  }
  codeWrite(a, size) {     // a store hit a line holding compiled code
    if (this.codeCache) this.codeCache.codeWrite(a, size);
    else this.pflags[a >>> 7] &= ~2;
    this.brk = 1;            // compiled code must not continue past this store
  }
  // host-side helpers (no aborts, no side effects on I/O beyond bus semantics)
  peek8(a) { a >>>= 0; if (a < RAM_SIZE) return this.m8[a]; if (a >= ROM_BASE) return this.m8[a - ROM_BASE + RAM_SIZE]; const v = this.bus.read(a, 1); return v === -1 ? 0 : v & 0xFF; }
  peek16(a) { return this.peek8(a) | (this.peek8(a + 1) << 8); }
  peek32(a) { return (this.peek16(a) | (this.peek16(a + 2) << 16)) >>> 0; }
  // fetch helpers used for disassembly (no side effects, no I/O)
  fetchWord(a) { a >>>= 0; if (a < RAM_SIZE) return this.m32[a >>> 2]; if (a >= ROM_BASE) return this.m32[(a - ROM_BASE + RAM_SIZE) >>> 2]; return 0; }
  fetchHalf(a) { a >>>= 0; if (a < RAM_SIZE) return this.m16[a >>> 1]; if (a >= ROM_BASE) return this.m16[(a - ROM_BASE + RAM_SIZE) >>> 1]; return 0; }
  // host write into RAM that may overwrite code (loaders)
  hostWrite(a, bytes) {
    a >>>= 0;
    this.m8.set(bytes, a);
    if (this.codeCache && bytes.length) this.codeCache.invalidateRange(a, bytes.length);
  }

  condPass(cond) {
    switch (cond) {
      case 0: return this.zv === 0;
      case 1: return this.zv !== 0;
      case 2: return this.c !== 0;
      case 3: return this.c === 0;
      case 4: return this.nv < 0;
      case 5: return this.nv >= 0;
      case 6: return this.v !== 0;
      case 7: return this.v === 0;
      case 8: return this.c !== 0 && this.zv !== 0;
      case 9: return this.c === 0 || this.zv === 0;
      case 10: return (this.nv < 0) === (this.v !== 0);
      case 11: return (this.nv < 0) !== (this.v !== 0);
      case 12: return this.zv !== 0 && (this.nv < 0) === (this.v !== 0);
      case 13: return this.zv === 0 || (this.nv < 0) !== (this.v !== 0);
      case 14: return true;
    }
    return false;
  }

  // flag-setting arithmetic helpers
  adds(a, b) { const res = (a + b) | 0; this.c = (res >>> 0) < (a >>> 0) ? 1 : 0; this.v = ((a ^ res) & (b ^ res)) >>> 31; this.nv = this.zv = res; return res; }
  adcs(a, b) {
    const ci = this.c, res = (a + b + ci) | 0;
    this.c = ((a >>> 0) + (b >>> 0) + ci) > 0xFFFFFFFF ? 1 : 0;
    this.v = ((a ^ res) & (b ^ res)) >>> 31; this.nv = this.zv = res; return res;
  }
  subs(a, b) { const res = (a - b) | 0; this.c = (a >>> 0) >= (b >>> 0) ? 1 : 0; this.v = ((a ^ b) & (a ^ res)) >>> 31; this.nv = this.zv = res; return res; }
  sbcs(a, b) {
    const bi = 1 - this.c, res = (a - b - bi) | 0;
    this.c = (a >>> 0) >= (b >>> 0) + bi ? 1 : 0;
    this.v = ((a ^ b) & (a ^ res)) >>> 31; this.nv = this.zv = res; return res;
  }

  // ---------------------------------------------------------------- running
  // Execute up to `budget` instructions. Returns the number executed.
  // Stops early on WFI (halted) or when brk is raised with nothing to do.
  run(budget) {
    let done = 0;
    this.stopRun = 0;
    while (done < budget && !this.stopRun) {
      if (this.halted) {
        if (this.irqLine || this.fiqLine) this.halted = 0; else break;
      }
      if (this.fiqLine && !this.f) this.takeFIQ();
      else if (this.irqLine && !this.i) this.takeIRQ();
      this.brk = 0;
      const want = budget - done;
      let n;
      this._nb = 0;
      try {
        if (this.jit) n = this.jit.run(want);
        else n = this.t ? this.runThumb(want) : this.runArm(want);
      } catch (e) {
        if (e !== ABORT) throw e;
        this.takeDataAbort();
        n = this._n + 1;           // instructions completed before the abort + the aborted one
      }
      done += n;
      this.icount += n;
      this._n = 0;
    }
    return done;
  }

  // ask run() to return as soon as possible (device schedule changed, exit, ...)
  requestStop() { this.stopRun = 1; this.brk = 1; }

  step() { const j = this.jit; this.jit = null; try { return this.run(1); } finally { this.jit = j; } }

  // ARM interpreter loop. Runs until budget, brk, or (if stopOnBranch) a
  // non-sequential PC change. this.pc must be valid on entry and is valid on exit.
  runArm(budget, stopOnBranch = false) {
    const r = this.r, m32 = this.m32, tr = this.trace;
    let pc = this.pc, n = 0;
    this._n = this._nb;
    loop: while (n < budget) {
      let i;
      if ((pc & 0xFF000000) === 0) i = m32[pc >>> 2];
      else if ((pc >>> 20) === 0xFFF) i = m32[((pc & 0xFFFFF) + RAM_SIZE) >>> 2];
      else { this.prefetchAbort(pc); pc = this.pc; n++; break; }
      if (tr !== null) this.traceRec(pc, i);
      this.pc = pc;              // for aborts and helpers
      this._n = this._nb + n;
      r[15] = pc + 8;
      let npc = pc + 4;
      const cond = i >>> 28;
      if (cond !== 14 && !this.condPass(cond)) {
        if (cond === 15) { npc = this.armUncond(i, pc); n++; pc = npc; if (this.brk || stopOnBranch) break; continue; }
        n++; pc = npc; continue;
      }
      switch (ARM_TABLE[((i >>> 16) & 0xFF0) | ((i >>> 4) & 0xF)]) {
        case H_DP: {
          // ---- operand 2
          let b, sc = this.c, a;
          const rn = (i >>> 16) & 15;
          if (i & 0x02000000) {
            const rot = (i >>> 7) & 30; b = i & 0xFF;
            if (rot) { b = (b >>> rot) | (b << (32 - rot)); sc = b >>> 31; }
            a = r[rn];
          } else if ((i & 0x10) === 0) {
            const rm = r[i & 15], sh = (i >>> 7) & 31;
            switch ((i >>> 5) & 3) {
              case 0: if (sh === 0) b = rm; else { b = rm << sh; sc = (rm >>> (32 - sh)) & 1; } break;
              case 1: if (sh === 0) { b = 0; sc = rm >>> 31; } else { b = rm >>> sh; sc = (rm >>> (sh - 1)) & 1; } break;
              case 2: if (sh === 0) { b = rm >> 31; sc = rm >>> 31; } else { b = rm >> sh; sc = (rm >>> (sh - 1)) & 1; } break;
              default: if (sh === 0) { b = (sc << 31) | (rm >>> 1); sc = rm & 1; } else { b = (rm >>> sh) | (rm << (32 - sh)); sc = (rm >>> (sh - 1)) & 1; } break;
            }
            a = r[rn];
          } else {
            const rmi = i & 15;
            let rm = r[rmi]; if (rmi === 15) rm = (rm + 4) | 0;
            const s = r[(i >>> 8) & 15] & 0xFF;
            a = r[rn]; if (rn === 15) a = (a + 4) | 0;
            b = rm;
            if (s !== 0) {
              switch ((i >>> 5) & 3) {
                case 0: if (s < 32) { b = rm << s; sc = (rm >>> (32 - s)) & 1; } else if (s === 32) { b = 0; sc = rm & 1; } else { b = 0; sc = 0; } break;
                case 1: if (s < 32) { b = rm >>> s; sc = (rm >>> (s - 1)) & 1; } else if (s === 32) { b = 0; sc = rm >>> 31; } else { b = 0; sc = 0; } break;
                case 2: if (s < 32) { b = rm >> s; sc = (rm >>> (s - 1)) & 1; } else { b = rm >> 31; sc = rm >>> 31; } break;
                default: { const s5 = s & 31; if (s5 === 0) { sc = rm >>> 31; } else { b = (rm >>> s5) | (rm << (32 - s5)); sc = (rm >>> (s5 - 1)) & 1; } }
              }
            }
          }
          const rd = (i >>> 12) & 15, S = i & 0x00100000;
          let res;
          switch ((i >>> 21) & 15) {
            case 0: res = a & b; if (S) { this.nv = this.zv = res; this.c = sc; } break;
            case 1: res = a ^ b; if (S) { this.nv = this.zv = res; this.c = sc; } break;
            case 2: res = S ? this.subs(a, b) : (a - b) | 0; break;
            case 3: res = S ? this.subs(b, a) : (b - a) | 0; break;
            case 4: res = S ? this.adds(a, b) : (a + b) | 0; break;
            case 5: res = S ? this.adcs(a, b) : (a + b + this.c) | 0; break;
            case 6: res = S ? this.sbcs(a, b) : (a - b - 1 + this.c) | 0; break;
            case 7: res = S ? this.sbcs(b, a) : (b - a - 1 + this.c) | 0; break;
            case 8: res = a & b; this.nv = this.zv = res; this.c = sc; n++; pc = npc; continue;
            case 9: res = a ^ b; this.nv = this.zv = res; this.c = sc; n++; pc = npc; continue;
            case 10: this.subs(a, b); n++; pc = npc; continue;
            case 11: this.adds(a, b); n++; pc = npc; continue;
            case 12: res = a | b; if (S) { this.nv = this.zv = res; this.c = sc; } break;
            case 13: res = b; if (S) { this.nv = this.zv = res; this.c = sc; } break;
            case 14: res = a & ~b; if (S) { this.nv = this.zv = res; this.c = sc; } break;
            default: res = ~b; if (S) { this.nv = this.zv = res; this.c = sc; } break;
          }
          if (rd !== 15) { r[rd] = res; n++; pc = npc; continue; }
          // write to PC
          if (S) this.setCPSR(this.spsr, 15, true);        // exception return
          npc = this.t ? res & ~1 : res & ~3;
          n++; pc = npc;
          if (stopOnBranch || this.brk) break loop;
          continue;
        }
        case H_LS: {
          const rn = (i >>> 16) & 15, rd = (i >>> 12) & 15;
          let off;
          if (i & 0x02000000) {
            const rm = r[i & 15], sh = (i >>> 7) & 31;
            switch ((i >>> 5) & 3) {
              case 0: off = rm << sh; break;
              case 1: off = sh ? rm >>> sh : 0; break;
              case 2: off = rm >> (sh ? sh : 31); break;
              default: off = sh ? (rm >>> sh) | (rm << (32 - sh)) : (this.c << 31) | (rm >>> 1);
            }
          } else off = i & 0xFFF;
          const base = r[rn];
          const upd = (i & 0x00800000) ? (base + off) | 0 : (base - off) | 0;
          const addr = (i & 0x01000000) ? upd : base;
          const wb = (i & 0x01000000) === 0 || (i & 0x00200000) !== 0;
          if (i & 0x00100000) {    // load
            let v;
            if (i & 0x00400000) v = (addr & 0xFF000000) === 0 && (addr >>> 16) !== this.mmioSeg ? this.m8[addr] : this.ld8(addr, pc);
            else v = (addr & 0xFF000003) === 0 && (addr >>> 16) !== this.mmioSeg ? m32[addr >>> 2] : this.ld32(addr, pc);
            if (wb) r[rn] = upd;
            if (rd === 15) {
              if (v & 1) { this.t = 1; npc = v & ~1; } else npc = v & ~3;
              n++; pc = npc; if (this.t) this.brk = 1; if (stopOnBranch || this.brk) break loop; continue;
            }
            r[rd] = v;
          } else {
            const v = r[rd];      // STR pc stores pc+8 (QEMU-compatible)
            if (i & 0x00400000) {
              if ((addr & 0xFF000000) === 0 && this.pflags[addr >>> 7] === 0) this.m8[addr] = v; else this.st8(addr, v, pc);
            } else {
              if ((addr & 0xFF000003) === 0 && this.pflags[addr >>> 7] === 0) m32[addr >>> 2] = v; else this.st32(addr, v, pc);
            }
            if (wb) r[rn] = upd;
          }
          n++; pc = npc;
          if (this.brk) break loop;      // an I/O access may have raised brk
          continue;
        }
        case H_B:
          npc = (pc + 8 + ((i << 8) >> 6)) | 0;
          n++; pc = npc; if (stopOnBranch) break loop; continue;
        case H_BL:
          r[14] = pc + 4;
          npc = (pc + 8 + ((i << 8) >> 6)) | 0;
          n++; pc = npc; if (stopOnBranch) break loop; continue;
        case H_LDM: {
          npc = this.armLdm(i, pc, npc);
          n++;
          if (this.brk || (stopOnBranch && npc !== ((pc + 4) | 0))) { pc = npc; break loop; }
          pc = npc; continue;
        }
        case H_XLS: npc = this.armXls(i, pc, npc); n++; pc = npc; if (this.brk) break loop; continue;
        default: {
          const seq = (pc + 4) | 0;
          npc = this.armMisc(i, pc, npc);
          n++;
          pc = npc;
          if (this.brk || (stopOnBranch && npc !== seq)) break loop;
          continue;
        }
      }
    }
    this.pc = pc;
    this._n = this._nb + n;
    return n;
  }

  // execute exactly one instruction at pc through the interpreter (JIT fallback);
  // keeps the JIT's instruction accounting (_n) intact. Returns the next pc.
  armFallback(i, pc) { const sn = this._n, sb = this._nb; this._nb = sn; this.pc = pc; this.runArm(1); this._n = sn; this._nb = sb; return this.pc; }
  thumbFallback(pc) { const sn = this._n, sb = this._nb; this._nb = sn; this.pc = pc; this.runThumb(1); this._n = sn; this._nb = sb; return this.pc; }

  traceRec(pc, i) {
    if (this.traceHook !== null) { this.traceHook(pc, i); return; }      // (memmap.mjs sampling windows)
    const tr = this.trace; let p = this.tracePos;
    tr[p] = pc | (this.t ? 1 : 0); tr[p + 1] = i;
    p += 2; if (p >= tr.length) p = 0; this.tracePos = p;
  }

  // unconditional (cond = 1111) space: BLX imm, PLD, else undefined
  armUncond(i, pc) {
    if ((i & 0x0E000000) === 0x0A000000) {     // BLX imm
      this.r[14] = pc + 4;
      this.t = 1; this.brk = 1;
      return (pc + 8 + ((i << 8) >> 6) + ((i >>> 23) & 2)) | 0;
    }
    if ((i & 0x0D70F000) === 0x0550F000) return pc + 4;   // PLD
    return this.undefinedInsn(pc);
  }

  // extra loads/stores: LDRH/STRH/LDRSB/LDRSH/LDRD/STRD
  armXls(i, pc, npc) {
    const r = this.r;
    const rn = (i >>> 16) & 15, rd = (i >>> 12) & 15;
    const off = (i & 0x00400000) ? (((i >>> 4) & 0xF0) | (i & 0xF)) : r[i & 15];
    const base = r[rn];
    const upd = (i & 0x00800000) ? (base + off) | 0 : (base - off) | 0;
    const addr = (i & 0x01000000) ? upd : base;
    const wb = (i & 0x01000000) === 0 || (i & 0x00200000) !== 0;
    const sh = (i >>> 5) & 3;
    if (i & 0x00100000) {
      let v;
      if (sh === 1) v = this.ld16(addr, pc);
      else if (sh === 2) v = (this.ld8(addr, pc) << 24) >> 24;
      else v = (this.ld16(addr, pc) << 16) >> 16;
      if (wb) r[rn] = upd;
      if (rd === 15) { this.brk = 1; return v & ~3; }   // unpredictable; treat as branch
      r[rd] = v;
    } else if (sh === 1) {
      this.st16(addr, r[rd], pc);
      if (wb) r[rn] = upd;
    } else if (sh === 2) {       // LDRD
      if (rd & 1) return this.undefinedInsn(pc);
      const lo = this.ld32a(addr, pc), hi = this.ld32a((addr + 4) | 0, pc);
      if (wb) r[rn] = upd;
      r[rd] = lo; r[rd + 1] = hi;
    } else {                     // STRD
      if (rd & 1) return this.undefinedInsn(pc);
      this.st32(addr, r[rd], pc); this.st32((addr + 4) | 0, r[rd + 1], pc);
      if (wb) r[rn] = upd;
    }
    return npc;
  }

  // LDM/STM. Returns next pc.
  armLdm(i, pc, npc) {
    const r = this.r;
    const rn = (i >>> 16) & 15, list = i & 0xFFFF;
    let cnt = 0; for (let x = list; x; x &= x - 1) cnt++;
    const base = r[rn];
    let addr;
    const U = i & 0x00800000, P = i & 0x01000000;
    if (U) addr = P ? base + 4 : base; else addr = P ? base - cnt * 4 : base - cnt * 4 + 4;
    addr |= 0;
    const wbv = (U ? base + cnt * 4 : base - cnt * 4) | 0;
    const W = i & 0x00200000, S = i & 0x00400000;
    if (i & 0x00100000) {       // LDM
      const userBank = S && !(list & 0x8000);
      let a = addr;
      // load all first (abort => registers unchanged except those done; base restored)
      const vals = this._ldmTmp || (this._ldmTmp = new Int32Array(16));
      for (let k = 0; k < 16; k++) if (list & (1 << k)) { vals[k] = this.ld32a(a, pc); a = (a + 4) | 0; }
      if (W) r[rn] = wbv;
      for (let k = 0; k < 15; k++) if (list & (1 << k)) { if (userBank) this.setUserReg(k, vals[k]); else r[k] = vals[k]; }
      if (list & 0x8000) {
        let v = vals[15];
        if (S) { this.setCPSR(this.spsr, 15, true); return this.t ? v & ~1 : v & ~3; }
        if (v & 1) { this.t = 1; this.brk = 1; return v & ~1; }
        return v & ~3;
      }
      return npc;
    } else {                    // STM
      let a = addr;
      for (let k = 0; k < 16; k++) if (list & (1 << k)) {
        const v = S ? this.getUserReg(k) : r[k];      // r15 reads pc+8
        this.st32(a, v, pc); a = (a + 4) | 0;
      }
      if (W) r[rn] = wbv;
      return npc;
    }
  }

  // everything else in ARM state. Returns next pc.
  armMisc(i, pc, npc) {
    const r = this.r;
    switch (ARM_TABLE[((i >>> 16) & 0xFF0) | ((i >>> 4) & 0xF)]) {
      case H_MUL: {
        const rd = (i >>> 16) & 15;
        let res = Math.imul(r[i & 15], r[(i >>> 8) & 15]);
        if (i & 0x00200000) res = (res + r[(i >>> 12) & 15]) | 0;
        r[rd] = res;
        if (i & 0x00100000) { this.nv = this.zv = res; }
        if (rd === 15) { this.brk = 1; return res & ~3; }
        return npc;
      }
      case H_MULL: {
        const rdhi = (i >>> 16) & 15, rdlo = (i >>> 12) & 15;
        if (i & 0x00400000) smul64(r[i & 15], r[(i >>> 8) & 15]); else umul64(r[i & 15], r[(i >>> 8) & 15]);
        if (i & 0x00200000) add64(MR[0], MR[1], r[rdlo], r[rdhi]);
        r[rdlo] = MR[0]; r[rdhi] = MR[1];
        if (i & 0x00100000) { this.nv = MR[1]; this.zv = MR[1] | MR[0]; }
        return npc;
      }
      case H_SWP: {
        const a = r[(i >>> 16) & 15], rm = r[i & 15], rd = (i >>> 12) & 15;
        if (i & 0x00400000) { const t = this.ld8(a, pc); this.st8(a, rm, pc); r[rd] = t; }
        else { const t = this.ld32(a, pc); this.st32(a, rm, pc); r[rd] = t; }
        return npc;
      }
      case H_MRS:
        r[(i >>> 12) & 15] = (i & 0x00400000) ? this.spsr : this.getCPSR();
        return npc;
      case H_MSR: {
        let val;
        if (i & 0x02000000) { const rot = (i >>> 7) & 30; val = i & 0xFF; if (rot) val = (val >>> rot) | (val << (32 - rot)); }
        else val = r[i & 15];
        const mask = (i >>> 16) & 15;
        if (i & 0x00400000) {       // SPSR
          const b = BANK[this.mode];
          if (b) {
            let m = 0; if (mask & 1) m |= 0xFF; if (mask & 2) m |= 0xFF00; if (mask & 4) m |= 0xFF0000; if (mask & 8) m |= 0xFF000000;
            this.spsrb[b] = (this.spsrb[b] & ~m) | (val & m);
          }
        } else {
          // control byte: T is not writable by MSR
          this.setCPSR(val, mask, false);
        }
        return npc;
      }
      case H_BX: {
        const v = r[i & 15];
        if (v & 1) { this.t = 1; this.brk = 1; return v & ~1; }
        return v & ~3;
      }
      case H_BLXR: {
        const v = r[i & 15];
        r[14] = pc + 4;
        if (v & 1) { this.t = 1; this.brk = 1; return v & ~1; }
        return v & ~3;
      }
      case H_CLZ: r[(i >>> 12) & 15] = Math.clz32(r[i & 15]); return npc;
      case H_QOP: {
        const rm = r[i & 15], rnv = r[(i >>> 16) & 15], rd = (i >>> 12) & 15;
        SAT = 0;
        let res;
        switch ((i >>> 21) & 3) {
          case 0: res = sat32(rm + rnv); break;
          case 1: res = sat32(rm - rnv); break;
          case 2: res = sat32(rm + sat32(rnv * 2)); break;
          default: res = sat32(rm - sat32(rnv * 2)); break;
        }
        if (SAT) this.q = 1;
        r[rd] = res;
        return npc;
      }
      case H_BKPT:
        this.exception(0x0C, ABT, pc + 4);
        return this.pc;
      case H_SMULXY: {
        const op = (i >>> 21) & 3, rd = (i >>> 16) & 15, rn = (i >>> 12) & 15;
        const rm = r[i & 15], rs = r[(i >>> 8) & 15];
        const x = (i & 0x20) ? rm >> 16 : (rm << 16) >> 16;
        const y = (i & 0x40) ? rs >> 16 : (rs << 16) >> 16;
        switch (op) {
          case 0: {  // SMLAxy
            const p = x * y, acc = r[rn], res = (p + acc) | 0;
            if (p + acc !== res) this.q = 1;
            r[rd] = res; break;
          }
          case 1: {  // SMLAWy / SMULWy
            const p = Math.floor((rm * y) / 65536);     // exact: |rm*y| < 2^47
            if (i & 0x20) r[rd] = p | 0;                 // SMULWy
            else { const acc = r[rn], res = (p + acc) | 0; if (p + acc !== res) this.q = 1; r[rd] = res; }
            break;
          }
          case 2: {  // SMLALxy: RdHi=rd, RdLo=rn
            const p = x * y;
            add64(r[rn], r[rd], p | 0, p < 0 ? -1 : 0);
            r[rn] = MR[0]; r[rd] = MR[1]; break;
          }
          default: r[rd] = x * y; break;  // SMULxy
        }
        return npc;
      }
      case H_SVC:
        this.exception(0x08, SVC, pc + 4);
        return this.pc;
      case H_CPREG:
        return this.cpReg(i, pc, npc);
      case H_CPOTHER:
        if (((i >>> 8) & 14) === 10) return vfpExec(this, i, pc);   // CDP/LDC/STC/MCRR/MRRC to CP10/11
        return this.undefinedInsn(pc);
      case H_UND:
      default:
        return this.undefinedInsn(pc);
    }
  }

  // MCR/MRC
  // CP10/CP11 = VFP (the JIT calls this too)
  vfpExec(i, pc) { return vfpExec(this, i, pc); }
  vfpState() { return vfpState(this); }
  /** exact-flags mode: also track IXC/UFC (every VFP op takes the exact, slower path) */
  setVfpExactFlags(on) { this.vfpExactFlags = !!on; vfpUpdate(this); }

  cpReg(i, pc, npc) {
    const cp = (i >>> 8) & 15;
    if ((cp & 14) === 10) return vfpExec(this, i, pc);
    if (cp !== 15 || this.mode === USR) return this.undefinedInsn(pc);
    const crn = (i >>> 16) & 15, crm = i & 15, op1 = (i >>> 21) & 7, op2 = (i >>> 5) & 7, rd = (i >>> 12) & 15;
    if (i & 0x00100000) {        // MRC
      let v = 0;
      switch (crn) {
        case 0: v = op2 === 1 ? 0x1D152152 : op2 === 2 ? 0 : 0x41069265; break;
        case 1: v = this.cp15ctl; break;
        case 7: v = (crm === 10 || crm === 14) && op2 === 3 ? 0x40000000 : 0; break;  // test-and-clean: Z set
        default: v = this.cp15[crn];
      }
      if (rd === 15) this.setFlags(v); else this.r[rd] = v;
      return npc;
    }
    const v = this.r[rd];
    switch (crn) {
      case 0: break;
      case 1: this.cp15ctl = v | 0x00050078; break;
      case 7:
        if (crm === 0 && op2 === 4 && op1 === 0) { this.halted = 1; this.brk = 1; }   // WFI
        break;
      default: this.cp15[crn] = v;
    }
    return npc;
  }

  // ------------------------------------------------------------- Thumb
  runThumb(budget, stopOnBranch = false) {
    const r = this.r, m16 = this.m16, m32 = this.m32, tr = this.trace;
    let pc = this.pc, n = 0;
    this._n = this._nb;
    while (n < budget) {
      let i;
      if ((pc & 0xFF000000) === 0) i = m16[pc >>> 1];
      else if ((pc >>> 20) === 0xFFF) i = m16[((pc & 0xFFFFF) + RAM_SIZE) >>> 1];
      else { this.prefetchAbort(pc); pc = this.pc; n++; break; }
      if (tr !== null) this.traceRec(pc, i);
      this.pc = pc;
      this._n = this._nb + n;
      r[15] = pc + 4;
      let npc = pc + 2;
      switch (i >>> 11) {
        case 0: {     // LSL imm
          const rm = r[(i >>> 3) & 7], sh = (i >>> 6) & 31;
          let res;
          if (sh === 0) res = rm; else { res = rm << sh; this.c = (rm >>> (32 - sh)) & 1; }
          r[i & 7] = res; this.nv = this.zv = res; break;
        }
        case 1: {     // LSR imm
          const rm = r[(i >>> 3) & 7], sh = (i >>> 6) & 31;
          let res;
          if (sh === 0) { res = 0; this.c = rm >>> 31; } else { res = rm >>> sh; this.c = (rm >>> (sh - 1)) & 1; }
          r[i & 7] = res; this.nv = this.zv = res; break;
        }
        case 2: {     // ASR imm
          const rm = r[(i >>> 3) & 7], sh = (i >>> 6) & 31;
          let res;
          if (sh === 0) { res = rm >> 31; this.c = rm >>> 31; } else { res = rm >> sh; this.c = (rm >>> (sh - 1)) & 1; }
          r[i & 7] = res; this.nv = this.zv = res; break;
        }
        case 3: {     // ADD/SUB reg/imm3
          const a = r[(i >>> 3) & 7];
          const b = (i & 0x400) ? (i >>> 6) & 7 : r[(i >>> 6) & 7];
          r[i & 7] = (i & 0x200) ? this.subs(a, b) : this.adds(a, b);
          break;
        }
        case 4: { const v = i & 0xFF; r[(i >>> 8) & 7] = v; this.nv = this.zv = v; break; }
        case 5: this.subs(r[(i >>> 8) & 7], i & 0xFF); break;
        case 6: { const d = (i >>> 8) & 7; r[d] = this.adds(r[d], i & 0xFF); break; }
        case 7: { const d = (i >>> 8) & 7; r[d] = this.subs(r[d], i & 0xFF); break; }
        case 8:
          if (i & 0x400) {        // hi register ops / BX
            const rd = (i & 7) | ((i >>> 4) & 8), rm = (i >>> 3) & 15;
            const v = r[rm];
            switch ((i >>> 8) & 3) {
              case 0: {
                const res = (r[rd] + v) | 0;
                if (rd === 15) { npc = res & ~1; n++; pc = npc; if (stopOnBranch) { this.pc = pc; this._n = this._nb + n; return n; } continue; }
                r[rd] = res; break;
              }
              case 1: this.subs(r[rd], v); break;
              case 2:
                if (rd === 15) { npc = v & ~1; n++; pc = npc; if (stopOnBranch) { this.pc = pc; this._n = this._nb + n; return n; } continue; }
                r[rd] = v; break;
              default:   // BX / BLX
                if (i & 0x80) r[14] = (pc + 2) | 1;
                n++;
                if (v & 1) { pc = v & ~1; if (stopOnBranch) { this.pc = pc; this._n = this._nb + n; return n; } continue; }
                this.t = 0; this.brk = 1; this.pc = v & ~3; this._n = this._nb + n; return n;
            }
          } else this.thumbAlu(i);
          break;
        case 9: {     // LDR pc-relative
          const a = ((pc + 4) & ~3) + ((i & 0xFF) << 2);
          r[(i >>> 8) & 7] = (a & 0xFF000003) === 0 && (a >>> 16) !== this.mmioSeg ? m32[a >>> 2] : this.ld32(a, pc);
          break;
        }
        case 10: case 11: {  // register offset
          const a = (r[(i >>> 3) & 7] + r[(i >>> 6) & 7]) | 0, rd = i & 7;
          switch ((i >>> 9) & 7) {
            case 0: if ((a & 0xFF000003) === 0 && this.pflags[a >>> 7] === 0) m32[a >>> 2] = r[rd]; else this.st32(a, r[rd], pc); break;
            case 1: this.st16(a, r[rd], pc); break;
            case 2: if ((a & 0xFF000000) === 0 && this.pflags[a >>> 7] === 0) this.m8[a] = r[rd]; else this.st8(a, r[rd], pc); break;
            case 3: r[rd] = (this.ld8(a, pc) << 24) >> 24; break;
            case 4: r[rd] = (a & 0xFF000003) === 0 && (a >>> 16) !== this.mmioSeg ? m32[a >>> 2] : this.ld32(a, pc); break;
            case 5: r[rd] = this.ld16(a, pc); break;
            case 6: r[rd] = (a & 0xFF000000) === 0 && (a >>> 16) !== this.mmioSeg ? this.m8[a] : this.ld8(a, pc); break;
            default: r[rd] = (this.ld16(a, pc) << 16) >> 16; break;
          }
          break;
        }
        case 12: { const a = (r[(i >>> 3) & 7] + ((i >>> 4) & 0x7C)) | 0; if ((a & 0xFF000003) === 0 && this.pflags[a >>> 7] === 0) m32[a >>> 2] = r[i & 7]; else this.st32(a, r[i & 7], pc); break; }
        case 13: { const a = (r[(i >>> 3) & 7] + ((i >>> 4) & 0x7C)) | 0; r[i & 7] = (a & 0xFF000003) === 0 && (a >>> 16) !== this.mmioSeg ? m32[a >>> 2] : this.ld32(a, pc); break; }
        case 14: { const a = (r[(i >>> 3) & 7] + ((i >>> 6) & 31)) | 0; if ((a & 0xFF000000) === 0 && this.pflags[a >>> 7] === 0) this.m8[a] = r[i & 7]; else this.st8(a, r[i & 7], pc); break; }
        case 15: { const a = (r[(i >>> 3) & 7] + ((i >>> 6) & 31)) | 0; r[i & 7] = (a & 0xFF000000) === 0 && (a >>> 16) !== this.mmioSeg ? this.m8[a] : this.ld8(a, pc); break; }
        case 16: { const a = (r[(i >>> 3) & 7] + ((i >>> 5) & 0x3E)) | 0; this.st16(a, r[i & 7], pc); break; }
        case 17: { const a = (r[(i >>> 3) & 7] + ((i >>> 5) & 0x3E)) | 0; r[i & 7] = this.ld16(a, pc); break; }
        case 18: { const a = (r[13] + ((i & 0xFF) << 2)) | 0; const v = r[(i >>> 8) & 7]; if ((a & 0xFF000003) === 0 && this.pflags[a >>> 7] === 0) m32[a >>> 2] = v; else this.st32(a, v, pc); break; }
        case 19: { const a = (r[13] + ((i & 0xFF) << 2)) | 0; r[(i >>> 8) & 7] = (a & 0xFF000003) === 0 && (a >>> 16) !== this.mmioSeg ? m32[a >>> 2] : this.ld32(a, pc); break; }
        case 20: r[(i >>> 8) & 7] = (((pc + 4) & ~3) + ((i & 0xFF) << 2)) | 0; break;
        case 21: r[(i >>> 8) & 7] = (r[13] + ((i & 0xFF) << 2)) | 0; break;
        case 22: case 23: {
          const op = (i >>> 8) & 15;
          if (op === 0) { const o = (i & 0x7F) << 2; r[13] = (i & 0x80) ? r[13] - o : r[13] + o; break; }
          if (op === 4 || op === 5) {         // PUSH
            let cnt = 0; for (let x = i & 0x1FF; x; x &= x - 1) cnt++;
            let a = (r[13] - cnt * 4) | 0; const start = a;
            for (let k = 0; k < 8; k++) if (i & (1 << k)) { this.st32(a, r[k], pc); a += 4; }
            if (i & 0x100) this.st32(a, r[14], pc);
            r[13] = start; break;
          }
          if (op === 12 || op === 13) {       // POP
            let a = r[13];
            const vals = this._ldmTmp || (this._ldmTmp = new Int32Array(16));
            for (let k = 0; k < 8; k++) if (i & (1 << k)) { vals[k] = this.ld32a(a, pc); a = (a + 4) | 0; }
            let pv = 0;
            if (i & 0x100) { pv = this.ld32a(a, pc); a = (a + 4) | 0; }
            for (let k = 0; k < 8; k++) if (i & (1 << k)) r[k] = vals[k];
            r[13] = a;
            if (i & 0x100) {
              n++;
              if (pv & 1) { pc = pv & ~1; if (stopOnBranch) { this.pc = pc; this._n = this._nb + n; return n; } continue; }
              this.t = 0; this.brk = 1; this.pc = pv & ~3; this._n = this._nb + n; return n;
            }
            break;
          }
          if (op === 14) { this.exception(0x0C, ABT, pc + 4); n++; this._n = this._nb + n; return n; }   // BKPT
          npc = this.undefinedInsn(pc); n++; this._n = this._nb + n; return n;
        }
        case 24: {   // STMIA
          const rb = (i >>> 8) & 7; let a = r[rb]; const list = i & 0xFF;
          if (list === 0) { npc = this.undefinedInsn(pc); n++; this._n = this._nb + n; return n; }
          let cnt = 0; for (let x = list; x; x &= x - 1) cnt++;
          const wbv = (a + cnt * 4) | 0;
          for (let k = 0; k < 8; k++) if (list & (1 << k)) { this.st32(a, r[k], pc); a = (a + 4) | 0; }
          r[rb] = wbv; break;
        }
        case 25: {   // LDMIA
          const rb = (i >>> 8) & 7; let a = r[rb]; const list = i & 0xFF;
          if (list === 0) { npc = this.undefinedInsn(pc); n++; this._n = this._nb + n; return n; }
          const vals = this._ldmTmp || (this._ldmTmp = new Int32Array(16));
          for (let k = 0; k < 8; k++) if (list & (1 << k)) { vals[k] = this.ld32a(a, pc); a = (a + 4) | 0; }
          r[rb] = a;
          for (let k = 0; k < 8; k++) if (list & (1 << k)) r[k] = vals[k];
          break;
        }
        case 26: case 27: {  // Bcond / SWI
          const cond = (i >>> 8) & 15;
          if (cond === 15) { this.exception(0x08, SVC, pc + 2); n++; this._n = this._nb + n; return n; }
          if (cond === 14) { this.undefinedInsn(pc); n++; this._n = this._nb + n; return n; }
          if (this.condPass(cond)) {
            pc = (pc + 4 + ((i << 24) >> 23)) | 0; n++;
            if (stopOnBranch) { this.pc = pc; this._n = this._nb + n; return n; }
            continue;
          }
          break;
        }
        case 28:     // B
          pc = (pc + 4 + ((i << 21) >> 20)) | 0; n++;
          if (stopOnBranch) { this.pc = pc; this._n = this._nb + n; return n; }
          continue;
        case 29: {   // BLX suffix
          if (i & 1) { this.undefinedInsn(pc); n++; this._n = this._nb + n; return n; }
          const tgt = (r[14] + ((i & 0x7FF) << 1)) & ~3;
          r[14] = (pc + 2) | 1;
          this.t = 0; this.brk = 1; this.pc = tgt; n++; this._n = this._nb + n; return n;
        }
        case 30:     // BL prefix
          r[14] = (pc + 4 + (((i & 0x7FF) << 21) >> 9)) | 0; break;
        default: {   // BL suffix
          const tgt = (r[14] + ((i & 0x7FF) << 1)) | 0;
          r[14] = (pc + 2) | 1;
          pc = tgt & ~1; n++;
          if (stopOnBranch) { this.pc = pc; this._n = this._nb + n; return n; }
          continue;
        }
      }
      n++; pc = npc;
      if (this.brk) break;
    }
    this.pc = pc;
    this._n = this._nb + n;
    return n;
  }

  thumbAlu(i) {
    const r = this.r, rd = i & 7, rs = r[(i >>> 3) & 7], a = r[rd];
    let res;
    switch ((i >>> 6) & 15) {
      case 0: res = a & rs; break;
      case 1: res = a ^ rs; break;
      case 2: { const s = rs & 0xFF; res = a; if (s) { if (s < 32) { res = a << s; this.c = (a >>> (32 - s)) & 1; } else { res = 0; this.c = s === 32 ? a & 1 : 0; } } break; }
      case 3: { const s = rs & 0xFF; res = a; if (s) { if (s < 32) { res = a >>> s; this.c = (a >>> (s - 1)) & 1; } else { res = 0; this.c = s === 32 ? a >>> 31 : 0; } } break; }
      case 4: { const s = rs & 0xFF; res = a; if (s) { if (s < 32) { res = a >> s; this.c = (a >>> (s - 1)) & 1; } else { res = a >> 31; this.c = a >>> 31; } } break; }
      case 5: r[rd] = this.adcs(a, rs); return;
      case 6: r[rd] = this.sbcs(a, rs); return;
      case 7: { const s = rs & 0xFF; res = a; if (s) { const s5 = s & 31; if (s5 === 0) this.c = a >>> 31; else { res = (a >>> s5) | (a << (32 - s5)); this.c = (a >>> (s5 - 1)) & 1; } } break; }
      case 8: res = a & rs; this.nv = this.zv = res; return;
      case 9: r[rd] = this.subs(0, rs); return;
      case 10: this.subs(a, rs); return;
      case 11: this.adds(a, rs); return;
      case 12: res = a | rs; break;
      case 13: res = Math.imul(a, rs); break;
      case 14: res = a & ~rs; break;
      default: res = ~rs; break;
    }
    r[rd] = res; this.nv = this.zv = res;
  }
}
