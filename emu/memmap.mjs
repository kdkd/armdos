// ARM-DOS machine: live memory activity counters (the inspector's MEMORY MAP).
//
// Counts, per 256-byte cell of the address space, the instructions EXECUTED
// there and the memory READS and WRITES that touch it, for a heat-map display.
// Nothing here runs unless a MemActivity is attached (cpu.act !== null); then
// jit.mjs uses its runAct() dispatcher instead of run():
//
// * execute: exact. Each compiled region's retired count (cpu.jx) goes to the
//   cell of its entry, each interpreted run to the cell where it started
//   (blocks are at most 64 instructions and never cross a 4 KB page).
// * reads/writes: SAMPLED. `window` instructions out of every `period` run in
//   the interpreter with hook() as its per-instruction trace callback (called
//   before the instruction executes); hook() decodes the loads and stores the
//   instruction is about to do (ARM LDR/STR/LDRH/LDRD/LDM/STM/SWP and VFP
//   FLD/FST(M), Thumb loads/stores/PUSH/POP/LDMIA/STMIA) and adds `scale` =
//   period / window to each cell. Dispatch budgets end at phase boundaries, so
//   the windows fall uniformly in (instruction) time: unbiased estimates of all
//   accesses, compiled code or not. (A first version compiled instrumented
//   copies of the JIT's regions instead; V8 runs rarely-called functions
//   unoptimised for a long time, and that cost 15-25% - emu/README.md.)
// * written lines: EXACT in space, per frame in time. Every RAM line (128 bytes)
//   is armed in cpu.pflags (bit 2) at each drain(); the first store to an
//   armed line takes the store slow path (compiled code and interpreter alike:
//   any non-zero pflags byte does), which disarms it and calls dirty(). So `wp`
//   counts, per 256-byte cell, the lines written since the last drain: a
//   sweep through a buffer (DOOM's framebuffer copy) lights all of it, which a
//   1/2048 time sample does not. Cost: one slow-path store per written line
//   per frame.
// * I/O ports and the font RAM (machine.read/write) and ISA DMA: exact.
//
// Cell index layout (N cells):
//   0 .. 65535            RAM, 256 bytes each (16 MB)
//   ROM0 .. +4095         BIOS ROM 0xFFF00000-0xFFFFFFFF, 256 bytes each
//   IO0 .. +64            ISA ports: 16 ports per cell for 0-3FFh, cell 64 = the rest
//   FONT0 .. +31          VGA character generator RAM (0x11000000, 8 KB)

export const RAM_CELLS = 65536;
export const ROM0 = RAM_CELLS, ROM_CELLS = 4096;
export const IO0 = ROM0 + ROM_CELLS, IO_CELLS = 65;
export const FONT0 = IO0 + IO_CELLS, FONT_CELLS = 32;
export const NCELLS = FONT0 + FONT_CELLS;

/** cell index of an address, or -1 (unmapped) */
export function cellOf(a) {
  a >>>= 0;
  if (a < 0x1000000) return a >>> 8;
  if (a >= 0xFFF00000) return ROM0 + ((a >>> 8) & 0xFFF);
  if ((a >>> 16) === 0x1000) { const p = a & 0xFFFF; return IO0 + (p < 0x400 ? p >>> 4 : 64); }
  if (a >= 0x11000000 && a < 0x11002000) return FONT0 + ((a >>> 8) & 31);
  return -1;
}
/** first address of a cell */
export function cellAddr(i) {
  if (i < ROM0) return i << 8;
  if (i < IO0) return (0xFFF00000 + ((i - ROM0) << 8)) >>> 0;
  if (i < FONT0) return 0x10000000 + ((i - IO0) << 4);
  return 0x11000000 + ((i - FONT0) << 8);
}

export class MemActivity {
  /** period/window: instructions (defaults: 256 of every 524288 sampled, scale 2048) */
  constructor({ period = 1 << 19, window = 256, exact = false } = {}) {
    this.x = new Float64Array(NCELLS);      // instructions executed
    this.r = new Float64Array(NCELLS);      // reads (sampled estimate)
    this.w = new Float64Array(NCELLS);      // writes (sampled estimate, + exact bus/DMA)
    this.wp = new Float64Array(RAM_CELLS);  // lines written since the last drain (0-2 per cell)
    this.period = period; this.window = Math.min(window, period);
    this.scale = this.window > 0 ? period / this.window : 0;   // (window 0: no sampling windows)
    this.exact = exact;                     // count every region call's instructions (else execution is sampled too)
    this.rec = false;                       // inside a sampling window (jit.runAct)
    this.cnt = period - this.window;        // instructions left in the current phase
    this.tbuf = new Int32Array(2);          // (cpu.trace must be non-null for the hook to run)
    this.machine = null;
    this.hook = null;
  }
  /** called by the dispatcher when the current phase is used up */
  flip() {
    if (this.window >= this.period) { this.rec = true; this.cnt = this.period; return; }   // (tests: sample everything)
    do { this.rec = !this.rec; this.cnt += this.rec ? this.window : this.period - this.window; } while (this.cnt <= 0);
  }
  /** the trace callback for sampling windows: (pc, instruction) before it executes */
  makeHook(cpu) {
    const r = cpu.r, X = this.x, R = this.r, W = this.w, S = this.scale, XS = this.exact ? 1 : S;
    const acc = (a, st) => {
      a >>>= 0;
      let i;
      if (a < 0x1000000) i = a >>> 8;
      else if (a >= 0xFFF00000) i = ROM0 + ((a >>> 8) & 0xFFF);
      else return;                          // I/O, font RAM: counted exactly by the bus (io())
      if (st) W[i] += S; else R[i] += S;
    };
    const words = (a, n, st) => { for (let k = 0; k < n; k++) acc(a + 4 * k, st); };
    const pop = (v) => { let n = 0; while (v) { v &= v - 1; n++; } return n; };
    const arm = (pc, i) => {
      const cls = (i >>> 25) & 7;
      if (cls === 1 || cls === 5 || cls === 7) return;                                   // data processing imm, branches, SWI/CP regs
      if (cls === 0 && ((i & 0x90) !== 0x90 || ((i & 0x60) === 0 && (i & 0x0FB00FF0) !== 0x01000090))) return;   // DP, multiplies
      const cond = i >>> 28;
      if (cond === 15 || (cond !== 14 && !cpu.condPass(cond))) return;
      const rn = (i >>> 16) & 15, base = rn === 15 ? pc + 8 : r[rn];
      const P = i & 0x01000000, U = i & 0x00800000, L = i & 0x00100000;
      switch (cls) {
        case 0: {
          if ((i & 0x0FB00FF0) === 0x01000090) { acc(base, 0); acc(base, 1); return; }    // SWP/SWPB
          const off = (i & 0x00400000) ? ((i >>> 4) & 0xF0) | (i & 0xF) : r[i & 15];
          const a = P ? (U ? base + off : base - off) : base, sh = (i >>> 5) & 3;
          if (L) acc(a, 0);
          else if (sh === 1) acc(a, 1);
          else { acc(a, sh === 3); acc(a + 4, sh === 3); }                                   // LDRD / STRD
          return;
        }
        case 3: if (i & 0x10) return;       // (media / undefined) else fall through: register offset
        // falls through
        case 2: {
          let off = i & 0xFFF;
          if (i & 0x02000000) {
            const m = i & 15, rm = m === 15 ? pc + 8 : r[m], sh = (i >>> 7) & 31;
            switch ((i >>> 5) & 3) {
              case 0: off = rm << sh; break;
              case 1: off = sh ? rm >>> sh : 0; break;
              case 2: off = rm >> (sh || 31); break;
              default: off = sh ? (rm >>> sh) | (rm << (32 - sh)) : (cpu.c << 31) | (rm >>> 1);
            }
          }
          acc(P ? (U ? base + off : base - off) : base, !L);
          return;
        }
        case 4: {                           // LDM / STM
          const n = pop(i & 0xFFFF);
          words(U ? (P ? base + 4 : base) : (P ? base - 4 * n : base - 4 * n + 4), n, !L);
          return;
        }
        case 6: {                           // VFP FLDS/FSTS/FLDD/FSTD/FLDM/FSTM (coprocessors 10, 11)
          if (((i >>> 8) & 14) !== 10 || (!P && !U && !(i & 0x00200000))) return;
          const imm = (i & 0xFF) << 2, single = P && !(i & 0x00200000);
          const a = P ? (U ? base + imm : base - imm) : base;
          words(a, single ? (((i >>> 8) & 1) ? 2 : 1) : i & 0xFF, !L);
          return;
        }
      }
    };
    const thumb = (pc, i) => {
      const op = i >>> 11, rb = r[(i >>> 3) & 7];
      switch (op) {
        case 9: acc(((pc + 4) & ~3) + ((i & 0xFF) << 2), false); return;
        case 10: case 11: acc(rb + r[(i >>> 6) & 7], ((i >>> 9) & 7) <= 2); return;
        case 12: case 13: acc(rb + ((i >>> 4) & 0x7C), op === 12); return;
        case 14: case 15: acc(rb + ((i >>> 6) & 31), op === 14); return;
        case 16: case 17: acc(rb + ((i >>> 5) & 0x3E), op === 16); return;
        case 18: case 19: acc(r[13] + ((i & 0xFF) << 2), op === 18); return;
        case 22: case 23: {
          const o = (i >>> 8) & 15, n = pop(i & 0xFF) + ((i >>> 8) & 1);
          if (o === 4 || o === 5) words(r[13] - 4 * n, n, true);
          else if (o === 12 || o === 13) words(r[13], n, false);
          return;
        }
        case 24: case 25: words(r[(i >>> 8) & 7], pop(i & 0xFF), op === 24); return;
      }
    };
    return (pc, i) => {
      X[(pc & 0xFF000000) === 0 ? pc >>> 8 : (pc >>> 20) === 0xFFF ? ROM0 + ((pc >>> 8) & 0xFFF) : 0] += XS;
      if (cpu.t) thumb(pc, i); else arm(pc, i);
    };
  }
  /** bus access (I/O ports, font RAM): exact */
  io(a, st) {
    const i = cellOf(a);
    if (i < 0) return;
    if (st) this.w[i]++; else this.r[i]++;
  }
  /** first store to an armed line (cpu.lineWritten) */
  dirty(a) { this.wp[a >>> 8]++; }
  /** arm (or disarm) every RAM line: the next store to it is recorded once */
  arm(on = true) {
    const pf = this.machine?.cpu.pflags;
    if (!pf) return;
    if (on) for (let l = 0; l < pf.length; l++) pf[l] |= 4;
    else for (let l = 0; l < pf.length; l++) pf[l] &= ~4;
  }
  /** DMA / host transfers into or out of RAM */
  span(a, len, st) {
    const arr = st ? this.w : this.r;
    for (let p = a >>> 8, e = (a + len - 1) >>> 8; p <= e && p < RAM_CELLS; p++) arr[p] += Math.min(len, 256);
  }
  attach(machine) {
    if (this.machine && this.machine !== machine) this.detach();
    this.machine = machine;
    if (!this.hook || this.hookCpu !== machine.cpu) { this.hook = this.makeHook(machine.cpu); this.hookCpu = machine.cpu; }
    machine.cpu.act = this;
    machine.cpu.brk = 1;                    // leave the current dispatcher loop: the next one records
    this.arm(true);
  }
  detach() {
    const m = this.machine;
    if (m && m.cpu.act === this) { this.arm(false); m.cpu.act = null; m.cpu.brk = 1; }
    this.machine = null;
  }
  get attached() { return !!this.machine && this.machine.cpu.act === this; }
  /** hand the counts accumulated since the last call to fn(x, r, w, wp), clear them, re-arm the lines */
  drain(fn) {
    if (fn) fn(this.x, this.r, this.w, this.wp);
    this.x.fill(0); this.r.fill(0); this.w.fill(0); this.wp.fill(0);
    if (this.attached) this.arm(true);
  }
}

// ------------------------------------------------------------------ regions
// What lives where, read from guest memory: the IVT/BDA, the DOS kernel below
// the MCB arena, each MCB block with its owner's name, video RAM, the adapter
// hole, the BIOS's HMA, HIMEM's XMS blocks, the empty bus above the SIMMs and
// the ROM. Pure reads (no side effects), cheap enough to run once a second.

const TOP_CONV = 0xA000;          // paragraph: end of conventional memory

function mcbName(m8, a) {
  let s = '';
  for (let k = 0; k < 8; k++) {
    const ch = m8[a + 8 + k];
    if (ch === 0) break;
    if (ch < 0x20 || ch > 0x7E) return '';
    s += String.fromCharCode(ch);
  }
  return s.trim();
}
/** walk an MCB chain from paragraph seg; null if it is not a valid chain to the top of conventional memory */
export function mcbChain(m8, seg) {
  const out = [];
  for (let k = 0; k < 4000; k++) {
    const a = seg << 4;
    if (a + 16 > 0xA0000) return null;
    const sig = m8[a];
    if (sig !== 0x4D && sig !== 0x5A) return null;
    const owner = m8[a + 1] | (m8[a + 2] << 8), size = m8[a + 3] | (m8[a + 4] << 8);
    out.push({ seg, owner, size, name: mcbName(m8, a) });
    const next = seg + size + 1;
    if (sig === 0x5A) return next <= TOP_CONV && next >= TOP_CONV - 0x40 && out.length >= 2 ? out : null;
    if (next >= TOP_CONV) return null;
    seg = next;
  }
  return null;
}
/** the first MCB (paragraph) of the DOS arena, or -1; `hint` is tried first */
export function findArena(m8, hint = -1) {
  if (hint > 0 && mcbChain(m8, hint)) return hint;
  for (let seg = 0x50; seg < 0x9000; seg++) if (m8[seg << 4] === 0x4D && mcbChain(m8, seg)) return seg;
  return -1;
}

const XMS_BASE = 0x110000;
const XMS_SIG = [0x58, 0x4D, 0x53, 0x58, 0x58, 0x58, 0x58, 0x30];   // "XMSXXXX0"
/** HIMEM's device header (address of the header, name at +16), or -1 */
export function findHimem(m8, hint = -1) {
  const at = (a) => { for (let k = 0; k < 8; k++) if (m8[a + 16 + k] !== XMS_SIG[k]) return false; return true; };
  if (hint > 0 && at(hint)) return hint;
  for (let a = 0x500; a < 0xA0000 - 24; a += 4) if (m8[a + 16] === 0x58 && at(a)) return a;
  return -1;
}
/**
 * HIMEM's used handles [{ n, base, size }] (addresses, bytes), or null. The handle
 * table sits right after HIMEM's bss (kernel/himem/himem.c: 12-byte entries
 * { u16 used, locks; u32 base, size (KB) }, 32 by default) and a static pointer
 * to it lives in the driver's image. Candidates are the words in the image that
 * point a little way into it; the first whose table validates wins (a pointer
 * stored right before its table first: that is where the linker puts it).
 */
export function xmsBlocks(m8, hdr, xmsEnd) {
  const m32 = new Int32Array(m8.buffer, 0, 0xA0000 >>> 2);
  const kbMax = (xmsEnd - XMS_BASE) >>> 10;
  const parse = (tab) => {
    const out = [];
    for (let k = 0; k < 32; k++) {
      const e = tab + 12 * k;
      if (e + 12 > 0xA0000) return null;
      const used = m8[e] | (m8[e + 1] << 8), locks = m8[e + 2] | (m8[e + 3] << 8);
      const base = m32[(e + 4) >>> 2] >>> 0, size = m32[(e + 8) >>> 2] >>> 0;
      if (used > 1 || locks > 255) return null;
      if (used === 0) continue;
      if (size === 0 || base + size > kbMax) return null;
      out.push({ n: k + 1, base: XMS_BASE + base * 1024, size: size * 1024 });
    }
    out.sort((x, y) => x.base - y.base);
    for (let k = 1; k < out.length; k++) if (out[k].base < out[k - 1].base + out[k - 1].size) return null;
    return out;
  };
  const cands = [];
  for (let a = hdr; a < hdr + 0x2000 && a < 0xA0000; a += 4) {
    const v = m32[a >>> 2];
    if (v > hdr + 0x100 && v < hdr + 0x4000 && (v & 3) === 0) cands.push([a, v]);
  }
  cands.sort((x, y) => (y[1] === y[0] + 4) - (x[1] === x[0] + 4));
  for (const [, v] of cands) { const t = parse(v); if (t) return t; }
  return null;
}

/**
 * The region table: [{ start, end, name, kind, owner? }] sorted, covering
 * 0-16 MB and the ROM. kind: sys, dos, prog, env, free, video, hole, bios,
 * xms, xmsfree, none, rom. cache: an object kept between calls (arena/HIMEM hints).
 */
export function memoryRegions(machine, cache = {}) {
  const m8 = machine.cpu.m8;
  const R = [];
  const add = (start, end, name, kind, extra) => { if (end > start) R.push({ start, end, name, kind, ...extra }); };
  add(0, 0x400, 'IVT', 'sys');
  add(0x400, 0x500, 'BIOS data area', 'sys');
  const arena = cache.arena = findArena(m8, cache.arena ?? -1);
  if (arena > 0) {
    add(0x500, arena << 4, 'DOS kernel', 'dos');
    const chain = mcbChain(m8, arena);
    const names = new Map();                       // PSP segment -> program name
    for (const b of chain) if (b.owner === b.seg + 1 && b.name) names.set(b.owner, b.name);
    for (const b of chain) {
      const start = b.seg << 4, end = (b.seg + 1 + b.size) << 4;
      if (b.owner === 0) add(start, end, 'free', 'free');
      else if (b.owner === 8) add(start, end, b.name && b.name !== 'SC' ? `DOS ${b.name}` : 'DOS system', 'dos');
      else if (b.owner === b.seg + 1) add(start, end, b.name || `PSP ${hex4(b.owner)}`, 'prog', { owner: b.owner });
      else add(start, end, `${names.get(b.owner) || 'PSP ' + hex4(b.owner)} data`, 'env', { owner: b.owner });
    }
    const top = (chain[chain.length - 1].seg + 1 + chain[chain.length - 1].size) << 4;
    add(top, 0xA0000, 'reserved', 'sys');
  } else add(0x500, 0xA0000, 'conventional memory', 'dos');
  const hgc = !!machine.hgc;
  if (machine.lfb) add(0xA0000, 0xA0000 + 640 * 480, 'VGA linear framebuffer', 'video');
  else {
    add(0xA0000, 0xB0000, hgc ? 'empty (no VGA)' : 'VGA graphics', hgc ? 'none' : 'video');
    add(0xB0000, 0xB8000, hgc ? 'Hercules page 0' : 'MDA window', hgc ? 'video' : 'hole');
    add(0xB8000, 0xC0000, hgc ? 'Hercules page 1' : 'text / CGA', 'video');
  }
  add(machine.lfb ? 0xA0000 + 640 * 480 : 0xC0000, 0x100000, 'adapter ROM hole', 'hole');
  add(0x100000, 0x110000, 'BIOS data + stacks (HMA)', 'bios');
  const ramEnd = machine.ramEnd ?? 0x1000000;
  const hdr = cache.himem = findHimem(m8, cache.himem ?? -1);
  const blocks = hdr > 0 ? xmsBlocks(m8, hdr, ramEnd) : null;
  if (blocks) {
    let pos = XMS_BASE;
    for (const b of blocks) {
      add(pos, b.base, 'XMS free', 'xmsfree');
      add(b.base, b.base + b.size, `XMS handle ${b.n}`, 'xms');
      pos = b.base + b.size;
    }
    add(pos, ramEnd, 'XMS free', 'xmsfree');
  } else add(XMS_BASE, ramEnd, hdr > 0 ? 'XMS' : 'extended memory', 'xmsfree');
  add(ramEnd, 0x1000000, 'no SIMMs (empty bus)', 'none');
  add(0xFFF00000, 0x100000000, 'BIOS ROM', 'rom');
  R.sort((a, b) => a.start - b.start);
  return R;
}
/** the region containing address a (binary search), or null */
export function regionAt(regions, a) {
  let lo = 0, hi = regions.length - 1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1, r = regions[mid];
    if (a < r.start) hi = mid - 1; else if (a >= r.end) lo = mid + 1; else return r;
  }
  return null;
}
const hex4 = (v) => v.toString(16).toUpperCase().padStart(4, '0');

// ISA port names for the I/O strip (16-port cells)
export const PORT_NAMES = [
  [0x000, 0x00F, 'DMA 1'], [0x020, 0x021, 'PIC'], [0x040, 0x043, 'PIT'], [0x060, 0x064, 'keyboard ctrl'],
  [0x070, 0x071, 'CMOS/RTC'], [0x080, 0x08F, 'DMA pages / POST'], [0x0A0, 0x0A1, 'PIC'], [0x0C0, 0x0DF, 'DMA 2'],
  [0x0F0, 0x0FF, 'system board'], [0x170, 0x177, 'ATAPI CD-ROM'], [0x1F0, 0x1F7, 'ATA hard disk'], [0x201, 0x201, 'game port'],
  [0x220, 0x22F, 'Sound Blaster 16'], [0x2F8, 0x2FF, 'COM2 modem'], [0x300, 0x307, 'floppy ctrl'], [0x330, 0x331, 'MPU-401'],
  [0x378, 0x37A, 'LPT1'], [0x388, 0x38B, 'OPL3'], [0x3B0, 0x3BF, 'MDA/Hercules'], [0x3C0, 0x3DF, 'VGA'], [0x3F8, 0x3FF, 'COM1'],
];
export function portName(lo, hi) {
  const n = PORT_NAMES.filter(([a, b]) => a <= hi && b >= lo).map((p) => p[2]);
  return [...new Set(n)].join(', ');
}
