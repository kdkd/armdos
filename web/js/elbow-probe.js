// Reads ELBOW's state out of guest memory for the inspector's ELBOW view
// (web/js/elbow-panel.js; node test: apps/x86/tests/elbowview.mjs).
//
// While it runs, ELBOW (apps/x86) puts the address of a small descriptor on
// the system board's ports FCh-FFh (ARCH.md 4.7), machine.elbowDesc here:
// where its x86 CPU state, x86 memory, page table and translated-block table
// are, the layout of a block record and the bounds of the ARM code cache.
// Nothing is hooked: the page samples the ARM PC between emulator slices and
// looks it up among the blocks' code ranges, only while the view is open.

import { disasm86 } from './x86disasm.js';

const MAGIC = 0x57424C45;            // "ELBW"
const MODE_USR = 0x10, MODE_SYS = 0x1F;

/** The descriptor, or null when ELBOW is not running (or it does not look right). */
export function readElbow(m) {
  const a = m && m.elbowDesc >>> 0;
  if (!a) return null;
  const m8 = m.cpu.m8, ram = Math.min(m8.length, 0x1000000);
  if (a & 3 || a + 120 > ram) return null;
  const dv = new DataView(m8.buffer, m8.byteOffset, ram);
  const w = (k) => dv.getUint32(a + k * 4, true);
  if (w(0) !== MAGIC || w(1) !== 1 || w(2) < 30) return null;
  const inRam = (x) => x > 0 && x < ram;
  const E = {
    version: w(1), cpu: w(3), memVar: w(4), rpt: w(5), blksVar: w(6), nblkVar: w(7),
    bsize: w(8), oLin: w(9), oCs: w(10), oSeg: w(11), oCode: w(12), oEnd: w(13), oNseg: w(14), oDead: w(15),
    cache: w(16), stubs: w(17), cacheEnd: w(18), cpVar: w(19), oEip: w(20), oSreg: w(21), wpt: w(22), irqVar: w(23),
    image: w(24), imageEnd: w(25), jitVar: w(26), nBlocksVar: w(27), nFlushVar: w(28), maxseg: w(29),
    u8: (x) => (x < ram ? m8[x] : 0),
    u16: (x) => (x + 2 <= ram ? dv.getUint16(x, true) : 0),
    u32: (x) => (x + 4 <= ram && !(x & 3) ? dv.getUint32(x, true) : 0),
  };
  if (![E.cpu, E.memVar, E.rpt, E.nblkVar, E.cpVar, E.image].every(inRam) || E.bsize > 256) return null;
  E.jit = E.u32(E.jitVar) !== 0 && inRam(E.cache);
  E.blks = E.u32(E.blksVar);
  E.nblk = E.jit ? Math.min(E.u32(E.nblkVar), 8192) : 0;
  E.cp = E.u32(E.cpVar);
  E.translated = E.u32(E.nBlocksVar);
  E.flushes = E.u32(E.nFlushVar);
  /** block record i */
  E.block = (i) => {
    const r = E.blks + i * E.bsize;
    const nseg = Math.min(E.u8(r + E.oNseg), E.maxseg), seg = [];
    for (let s = 0; s < nseg; s++) seg.push([E.u32(r + E.oSeg + s * 8), E.u32(r + E.oSeg + s * 8 + 4)]);
    return { i, lin: E.u32(r + E.oLin), cs: E.u32(r + E.oCs) & 0xFFFF, code: E.u32(r + E.oCode), end: E.u32(r + E.oEnd), dead: E.u8(r + E.oDead) !== 0, seg };
  };
  E.blocks = () => { const out = []; for (let i = 0; i < E.nblk; i++) out.push(E.block(i)); return out; };
  /** the block whose ARM code holds address a (blocks are laid out in order: binary search) */
  E.blockAt = (a) => {
    let lo = 0, hi = E.nblk - 1;
    while (lo <= hi) {
      const mid = (lo + hi) >> 1, code = E.u32(E.blks + mid * E.bsize + E.oCode);
      if (code <= a) lo = mid + 1; else hi = mid - 1;
    }
    if (hi < 0) return null;
    const b = E.block(hi);
    return a >= b.code && a < b.end ? b : null;
  };
  E.blockByCode = (a) => { const b = E.blockAt(a); return b && b.code === a ? b : null; };
  /** an x86 byte at a linear address, through ELBOW's read page table */
  E.x86byte = (lin) => { lin &= 0x1FFFFF; const host = (E.u32(E.rpt + (lin >>> 8) * 4) + lin) >>> 0; return E.u8(host); };
  E.regs = () => {
    const c = E.cpu, eip = E.u32(c + E.oEip) & 0xFFFF, cs = E.u16(c + E.oSreg + 2);
    return { cs, ip: eip };
  };
  return E;
}

/** x86 instructions from cs:ip for n bytes (or count instructions) */
export function x86lines(E, cs, ip, { bytes = 0, count = 0 } = {}) {
  const read = (off) => E.x86byte((cs << 4) + (off & 0xFFFF));
  const out = [];
  let used = 0;
  for (let k = 0; k < 64; k++) {
    if (bytes && used >= bytes) break;
    if (count && k >= count) break;
    const d = disasm86(read, ip);
    const hx = [];
    for (let j = 0; j < d.len; j++) hx.push(read(ip + j).toString(16).padStart(2, '0'));
    out.push({ cs, ip, len: d.len, bytes: hx.join(''), mnem: d.mnem, ops: d.ops, text: d.text, target: d.target });
    ip = (ip + d.len) & 0xFFFF; used += d.len;
  }
  return out;
}

/** a block's x86 source and ARM code, ready to show; disasmArm(word, addr) is emu/disasm's
 *  (passed in: the page loads it from the built site's emu/, node from emu/*.mjs) */
export function blockDetail(E, b, disasmArm, pc = -1) {
  const x86 = [];
  b.seg.forEach(([s, e], k) => {
    const ip = (s - (b.cs << 4)) & 0xFFFF;
    const lines = x86lines(E, b.cs, ip, { bytes: Math.max(1, Math.min(e - s, 1024)) });
    if (k && lines.length) lines[0].follow = true;          // a JMP the translator followed
    x86.push(...lines);
  });
  // literals: the words LDR pc/rX, [pc, #n] load (chained exits and constants)
  const n = Math.max(0, Math.min((b.end - b.code) >>> 2, 4096));
  const words = new Uint32Array(n);
  for (let k = 0; k < n; k++) words[k] = E.u32(b.code + k * 4);
  const lit = new Map();
  for (let k = 0; k < n; k++) {
    const w = words[k];
    if ((w & 0x0F7F0000) === 0x051F0000) {                // LDR Rd, [pc, #+/-imm12]
      const t = b.code + k * 4 + 8 + ((w & 0x00800000) ? (w & 0xFFF) : -(w & 0xFFF));
      if (t >= b.code && t < b.end) lit.set(t, ((w >>> 12) & 15) === 15 ? 'exit' : 'const');
    }
  }
  const where = (v) => {
    if (v >= E.cache && v < E.stubs) { const t = E.blockByCode(v); return t ? `block ${hex4(t.cs)}:${hex4((t.lin - (t.cs << 4)) & 0xFFFF)}` : 'translated code'; }
    if (v >= E.stubs && v < E.cacheEnd) return 'exit stub';
    if (v >= E.image && v < E.imageEnd) return 'ELBOW';
    return '';
  };
  const arm = [];
  for (let k = 0; k < n; k++) {
    const a = b.code + k * 4, w = words[k];
    let text, note = '';
    if (lit.has(a)) {
      text = '.word 0x' + w.toString(16).padStart(8, '0');
      if (lit.get(a) === 'exit') { const t = where(w); note = t === 'exit stub' ? 'exit: not linked yet' : t ? 'exit: chained to ' + t : ''; }
    } else {
      try { text = disasmArm(w, a); } catch { text = '??'; }
      text = text.replace(/\t@.*$/, '').replace(/\t/, ' ').replace(/\t/g, ' ');
      const br = text.match(/^b(l?)(?:eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)?\s+0x([0-9a-f]+)$/);
      if (br) { const t = where(parseInt(br[2], 16)); if (t) note = (br[1] ? 'call ' : '') + (t === 'ELBOW' ? (br[1] ? 'ELBOW helper' : 'ELBOW') : t); }
    }
    arm.push({ addr: a, word: w, text, note, lit: lit.has(a), cur: a === pc });
  }
  return { cs: b.cs, ip: (b.lin - (b.cs << 4)) & 0xFFFF, lin: b.lin, code: b.code, end: b.end, dead: b.dead, x86, arm, nx86: x86.length, narm: arm.filter((l) => !l.lit).length };
}

/**
 * Where is the ARM CPU right now, as far as ELBOW is concerned?
 *   none    ELBOW is not running
 *   block   in a translated block (block: the block record)
 *   stub    in an exit stub (leaving a block)
 *   helper  in an ELBOW routine called from translated code (block: the caller, if known)
 *   interp  in ELBOW's own code while the x86 CPU is running: the interpreter (cs:ip)
 *   native  outside ELBOW or with the X86 LED off: DOS, the BIOS, an IRQ handler
 */
export function probeElbow(m, E = readElbow(m)) {
  if (!E) return { kind: 'none' };
  const cpu = m.cpu, pc = cpu.pc >>> 0, r = cpu.r;
  const res = { kind: 'native', pc, E };
  if (E.jit && pc >= E.cache && pc < E.stubs) {
    const b = E.blockAt(pc);
    if (b) return { ...res, kind: 'block', block: b };
    return { ...res, kind: 'stub' };
  }
  if (E.jit && pc >= E.stubs && pc < E.cacheEnd) return { ...res, kind: 'stub' };
  const userish = cpu.mode === MODE_SYS || cpu.mode === MODE_USR;
  const inElbow = pc >= E.image && pc < E.imageEnd;
  const lr = r[14] >>> 0;
  if (userish && inElbow && E.jit && lr >= E.cache && lr < E.cacheEnd) return { ...res, kind: 'helper', block: lr < E.stubs ? E.blockAt(lr - 4) : null, from: lr - 4 };
  // (jitasm.S: r8-r11 hold &irq_pending, wpt, rpt and &cpu inside translated code, and C
  // preserves them, so still being there means a helper is running on behalf of a block)
  if (userish && inElbow && E.jit) {
    // deeper down (jh_string -> string_op): a helper's frame holds its return address into the block
    const sp = r[13] >>> 0;
    for (let k = 0; k < 96; k++) {
      const v = E.u32(sp + k * 4);
      if (v >= E.cache && v < E.cacheEnd && !(v & 3)) {
        const b = v < E.stubs ? E.blockAt(v - 4) : null;       // (from an exit stub: a store that hit code)
        if (b || v >= E.stubs) return { ...res, kind: 'helper', block: b, from: v - 4 };
      }
    }
    if (r[11] >>> 0 === E.cpu && r[10] >>> 0 === E.rpt && r[9] >>> 0 === E.wpt && r[8] >>> 0 === E.irqVar) return { ...res, kind: 'helper' };
  }
  if (userish && inElbow && m.altCpu?.on) return { ...res, kind: 'interp', ...E.regs() };
  return res;
}

export const hex4 = (v) => (v & 0xFFFF).toString(16).toUpperCase().padStart(4, '0');
