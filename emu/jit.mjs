// ARM-DOS machine: trace JIT for the ARMv5TE core.
//
// Hot code (ARM or Thumb) is translated into JavaScript with new Function().
// Cold code runs in the interpreter (cpu.runArm/runThumb with stopOnBranch),
// which also counts how often each entry address is reached (threshold 16).
//
// Unit of compilation: a BLOCK starting at a hot entry: straight-line code
// that continues past conditional branches (side exits) until an
// unconditional exit, 64 instructions or a page end; a branch back to the
// entry is a native JS loop. (Option follow: true continues the trace through
// unconditional B and BL - the callee joins it, lr set; measured ~30% slower
// on Quake, off by default.) Literal-pool loads (LDR rX, [pc, #n]) are folded
// into constants. libgcc's division routines are executed natively
// (libgcc.mjs, exact). Returns (POP {..pc}, BX, LDM pc) and LDRD/STRD are
// inlined. The compiled function has the region shape
//     function rgn(PC, budget) { ...locals...; L: for (;;) { <trace> } ...exit tail }
// (compile() still supports several entries per function, `switch (I)`
// between them; measured on Quake that was slower in V8 than one entry per
// function - large multi-entry functions exceed TurboFan's size limit or
// optimise worse - so every region has one entry.)
//
// Invalidation: RAM lines (128 bytes) holding compiled code or folded literals
// are flagged in cpu.pflags (bit 1); a store to such a line takes the slow path,
// drops every region whose ranges overlap it and raises cpu.brk, and compiled
// code checks cpu.brk after every slow-path store, so it never runs on past
// code it has just overwritten. Regions are listed on every page they have
// ranges in.
//
// Region function contract: runs from PC until an exit, returns the next PC
// and sets cpu.jx = instructions executed. Loops check the budget (n > budget -
// MAX_BLOCK) and cpu.brk. Guest registers and flags live in JS locals; they are
// written back at every exit (one shared tail), before helpers that read or
// change them (fallbacks), and in a catch clause when a memory helper throws a
// data abort (memory helpers only get cpu._n, the retired count, for I/O timing;
// the exception is entered by the dispatcher after the write-back).

import { ARM_TABLE, ABORT, MR, umul64, smul64, add64, RAM_SIZE,
  H_DP, H_MUL, H_MULL, H_XLS, H_MRS, H_CLZ, H_LS, H_LDM, H_B, H_BL,
  H_SWP, H_QOP, H_SMULXY, H_CPREG, H_CPOTHER, H_BX, H_BLXR } from './cpu.mjs';
import { genVfp } from './jitvfp.mjs';
import { divRoutineAt, udiv, sdiv, DV } from './libgcc.mjs';

const MAX_BLOCK = 64;
const MAX_SRC = 20000;           // characters of JS per region (multi-entry regions)
const COND = ['zv === 0', 'zv !== 0', 'cf !== 0', 'cf === 0', 'nv < 0', 'nv >= 0', 'vf !== 0', 'vf === 0',
  '(cf !== 0 && zv !== 0)', '(cf === 0 || zv === 0)', '((nv < 0) === (vf !== 0))', '((nv < 0) !== (vf !== 0))',
  '(zv !== 0 && (nv < 0) === (vf !== 0))', '(zv === 0 || (nv < 0) !== (vf !== 0))', 'true', 'false'];
// Placeholders expanded by finalize(): sync state to the CPU object (statement /
// expression form) and reload it after a helper that may have changed it.
const FL_STORE = '@S@';
const FL_EXPR = '@E@';
const FL_LOAD = '@L@';

// Registers r0-r14 used by a region live in locals rN; r15 is always constant
// or written straight to r[15] before helper calls. Placeholders:
//   @S<k>@ / @E<k>@  sync (statement / expression) with k instructions of the
//                    current block retired;   @L@  reload after a helper.
function finalize(code) {
  const used = new Set(), written = new Set();
  for (const m of code.matchAll(/\br\[(\d+)\]( = (?!=))?/g)) {
    const n = +m[1]; if (n === 15) continue;
    used.add(n); if (m[2]) written.add(n);
  }
  code = code.replace(/\br\[(\d+)\]/g, (m, n) => (n === '15' ? m : 'r' + n));
  const W = [...written].sort((a, b) => a - b), U = [...used].sort((a, b) => a - b);
  const S = 'c.nv = nv; c.zv = zv; c.c = cf; c.v = vf; ' + W.map((n) => `r[${n}] = r${n}; `).join('');
  const E = 'c.nv = nv, c.zv = zv, c.c = cf, c.v = vf, ' + W.map((n) => `r[${n}] = r${n}, `).join('');
  const L = 'nv = c.nv; zv = c.zv; cf = c.c; vf = c.v; ' + U.map((n) => `r${n} = r[${n}]; `).join('');
  code = code.replace(/@MS(\d+)@/g, (m, k) => `c._n = n0 + n + ${k}; `).replace(/@ME(\d+)@/g, (m, k) => `c._n = n0 + n + ${k}, `)
    .replace(/@S(\d+)@/g, (m, k) => `c._n = n0 + n + ${k}; ${S}`)
    .replace(/@E(\d+)@/g, (m, k) => `c._n = n0 + n + ${k}, ${E}`).replaceAll('@L@', L);
  let decl = 'let nv = c.nv, zv = c.zv, cf = c.c, vf = c.v, n = 0' + U.map((n) => `, r${n} = r[${n}]`).join('') + ';\n';
  decl += `const n0 = c._n, lim = budget - ${MAX_BLOCK};\n`;
  if (code.includes('fpx') || code.includes('fpo')) decl += 'const fpx = c.fpSlow, fpo = c.fpOff;\n';   // VFP mode (FMXR ends the region call)
  return { decl, code, sync: S };
}
// Tag the exits of one instruction's code (k = its 1-based index in the block):
// constant-target exits to an entry of the region become internal jumps.
function tagExits(code, k, entries, targets, nx) {
  if (code.indexOf('@') < 0 && code.indexOf('return') < 0) return code;   // (most instructions)
  // a slow-path store may have invalidated this very code (self-modifying code,
  // a folded literal) or changed interrupt state: cpu.brk is raised -> leave
  // (checked after the whole instruction, i.e. after its writeback and reloads)
  // (not when the call is returned: nothing runs after it - Firefox warns about unreachable code)
  if (nx !== undefined && /(?<!return )c\.(?:st8|st16|st32|armLdm|thumbFallback)\(/.test(code))
    code += ` if (c.brk !== 0) { X = ${nx}; K = ${k}; break L; }`;
  const exits = [];
  code = code.replace(/@S@return (-?\d+);/g, (m, t) => { exits.push(+t); if (targets) targets.push(+t); return `@X${exits.length - 1}@`; });
  code = code.replace(/return /g, `return c.jx = n + ${k}, `);
  code = code.replace(/@X(\d+)@/g, (m, x) => {
    const t = exits[+x];
    // exits go through one shared tail after the loop (sync once): X = target, K = retired in block
    if (entries.has(t)) return `{ n += ${k}; if (n > lim || c.brk !== 0) { X = ${t}; K = 0; break L; } I = ${entries.get(t)}; continue; }`;
    return `{ X = ${t}; K = ${k}; break L; }`;
  });
  // memory helpers (c.ld*/c.st*) only need the retired count (for I/O timing and
  // aborts); the region's catch clause writes registers back if they throw
  code = code.replace(/@([SE])@(?=c\.(?:ld|st)(?:8|16|32|32a)\()/g, (m, x) => `@M${x}${k - 1}@`);
  return code.replace(/@S@/g, `@S${k - 1}@`).replace(/@E@/g, `@E${k - 1}@`);
}
const HELPERS = { MR, umul64, smul64, add64, udiv, sdiv, DV };

// native libgcc division (libgcc.mjs): exact registers, flags and instruction
// count; division by zero or too little budget runs the routine's own code
function genDivision(kind) {
  const call = kind === 1 ? 'H.udiv(r[0], r[1], r[3], r[12])' : `H.sdiv(r[0], r[1], ${kind}, nv, r[2], r[3])`;
  return `if (r[1] !== 0) { ${call}; const cnt = H.DV[9]; if (n + cnt <= budget) {
  r[0] = H.DV[0]; r[1] = H.DV[1]; r[2] = H.DV[2]; r[3] = H.DV[3]; r[12] = H.DV[4]; nv = H.DV[5]; zv = H.DV[6]; cf = H.DV[7]; vf = H.DV[8];
  const v = r[14]; if (v & 1) { c.t = 1; c.brk = 1; X = v & ~1; } else X = v & ~3; K = cnt; break L; } }\n`;
}

const h8 = (x) => '0x' + (x >>> 0).toString(16);

export class JIT {
  constructor(cpu, { threshold = 16, debug = false, follow = false } = {}) {
    this.cpu = cpu;
    cpu.codeCache = this;               // invalidation target, also while cpu.jit is detached
    this.threshold = threshold;
    this.debug = debug;                 // keep guest address/opcode comments in the generated JS
    this.follow = follow;               // traces continue through unconditional B / BL (measured slower on Quake: off)
    this.litRanges = [];
    this.flushAll();
    this.stats = { compiled: 0, invalidations: 0, compileMs: 0 };
  }
  // the VGA's planar window (machine.setVgaWindow): while cpu.mmioSeg names a
  // 64 KB RAM segment, loads from it take the slow path (compiled in; the
  // window's switches flush all compiled code)
  mm(a) { const s = this.cpu.mmioSeg; return s === undefined || s > 0xFF ? '' : ` && (${a} >>> 16) !== ${s}`; }
  mm2(a, last) { const s = this.cpu.mmioSeg; return s === undefined || s > 0xFF ? '' : ` && (${a} >>> 16) !== ${s} && ((${a} + ${last}) >>> 16) !== ${s}`; }
  flushAll() {
    if (this.pages) for (const pg of this.pages) if (pg) for (const b of pg.list) b.alive = false;
    this.pages = new Array(4096 + 256).fill(null);
    // cpu.pflags has one byte per 128-byte line of RAM; bit 1 = line holds compiled code
    if (this.cpu.pflags) for (let l = 0; l < this.cpu.pflags.length; l++) this.cpu.pflags[l] &= ~2;
  }
  // page index for an executable address, or -1
  static pageIndex(pc) {
    if ((pc & 0xFF000000) === 0) return pc >>> 12;
    if ((pc >>> 20) === 0xFFF) return 4096 + ((pc >>> 12) & 0xFF);
    return -1;
  }
  invalidatePage(p) {
    const pg = this.pages[p];
    if (pg) { this.pages[p] = null; this.stats.invalidations += pg.list.length; for (const b of pg.list) b.alive = false; }
    if (p < 4096) for (let l = p << 5; l < (p + 1) << 5; l++) this.cpu.pflags[l] &= ~2;
  }
  // a store hit a 128-byte line holding compiled code (slow path)
  codeWrite(a, size = 4) { this.invalidateRange(a, size); }
  // drop every region with code overlapping [a, a+len) (RAM only)
  invalidateRange(a, len) {
    a >>>= 0;
    const end = a + len;
    for (let p = a >>> 12; p <= (end - 1) >>> 12 && p < 4096; p++) {
      const pg = this.pages[p];
      if (!pg) continue;
      const keep = [];
      for (const b of pg.list) {
        if (!b.alive) continue;
        if (b.ranges.some((r) => r[0] < end && r[1] > a)) this.kill(b.pg, b);
        else keep.push(b);
      }
      pg.list = keep;
      const pf = this.cpu.pflags, l0 = p << 5;
      for (let l = l0; l < l0 + 32; l++) pf[l] &= ~2;
      for (const b of keep) for (const [s0, e0] of b.ranges) for (let l = Math.max(s0 >>> 7, l0); l <= Math.min((e0 - 1) >>> 7, l0 + 31); l++) pf[l] |= 2;
    }
  }
  kill(pg, b) {
    b.alive = false;
    this.stats.invalidations++;
    const tab = b.t ? pg.tb : pg.ab, cnt = b.t ? pg.tc : pg.ac, sh = b.t ? 1 : 2;
    for (const e of b.entries) { const k = (e & 0xFFF) >>> sh; if (tab[k] === b) { tab[k] = null; cnt[k] = 0; } }
  }
  getPage(pi) {
    let pg = this.pages[pi];
    if (!pg) {
      pg = this.pages[pi] = { ab: new Array(1024).fill(null), tb: new Array(2048).fill(null), ac: new Uint8Array(1024), tc: new Uint8Array(2048),
        list: [] };                                        // regions with code (or folded literals) in this page
    }
    return pg;
  }

  // ---------------------------------------------------------------- dispatch
  lookup(pc, t) {
    const pi = JIT.pageIndex(pc);
    if (pi < 0) return null;
    let pg = this.pages[pi];
    if (!pg) {
      pg = this.getPage(pi);
      if (this.threshold > 1) { if (t) pg.tc[(pc & 0xFFF) >>> 1] = 1; else pg.ac[(pc & 0xFFF) >>> 2] = 1; return null; }
      if (this.disabled) return null;
      return this.hot(pg, pc, t);
    }
    if (t) { const k = (pc & 0xFFF) >>> 1; const b = pg.tb[k]; if (b !== null) return b; if (++pg.tc[k] >= this.threshold) return this.hot(pg, pc, 1); }
    else { const k = (pc & 0xFFF) >>> 2; const b = pg.ab[k]; if (b !== null) return b; if (++pg.ac[k] >= this.threshold) return this.hot(pg, pc, 0); }
    return null;
  }
  // an entry became hot: compile it (a block; a branch back to its start loops inside the function)
  hot(pg, pc, t) {
    if (this.disabled) return null;
    return this.compile([pc | 0], t, pg, MAX_SRC);
  }

  run(budget) {
    const c = this.cpu;
    if (c.act !== null) return this.runAct(budget);        // memory activity map open (memmap.mjs)
    let done = 0, prev = null;
    while (done < budget && c.brk === 0) {
      const pc = c.pc, t = c.t;
      const rem = budget - done;
      let blk = null;
      if (rem >= MAX_BLOCK) {
        // 2-way successor cache on the previous region (a block has at most two hot exits)
        if (prev !== null && prev.s0pc === pc && prev.s0.alive && prev.s0.t === t) blk = prev.s0;
        else if (prev !== null && prev.s1pc === pc && prev.s1.alive && prev.s1.t === t) blk = prev.s1;
        else {
          blk = this.lookup(pc, t);
          if (blk !== null && prev !== null) {
            if (prev.sw === 0) { prev.s0pc = pc; prev.s0 = blk; prev.sw = 1; } else { prev.s1pc = pc; prev.s1 = blk; prev.sw = 0; }
          }
        }
      }
      if (blk !== null) {
        c._n = done;
        let npc;
        try {
          npc = blk.fn(pc, rem);
        } catch (e) {
          if (e !== ABORT) throw e;
          c.takeDataAbort();                                // (the region synced its state in its catch)
          done = c._n + 1;                                  // retired before the faulting instruction + it
          continue;
        }
        c.pc = npc;
        done += c.jx;
        prev = blk;
        continue;
      }
      prev = null;
      // interpret up to the next taken branch
      c._nb = done; c._n = done;
      let n;
      try { n = t ? c.runThumb(rem, true) : c.runArm(rem, true); }
      catch (e) { if (e !== ABORT) throw e; c.takeDataAbort(); n = c._n - done + 1; }
      c._nb = 0;
      done += n;
    }
    c._n = done;
    return done;
  }

  // The dispatcher while a memory activity map is attached (cpu.act, memmap.mjs).
  // Alternates between run phases and SAMPLING WINDOWS: act.window out of every
  // act.period instructions run in the interpreter with act.hook as the trace
  // callback, which decodes each instruction's loads and stores (memmap.mjs).
  // Run phases end exactly at the next window, so windows fall uniformly in
  // (instruction) time. With act.exact, run phases also count every region call
  // (cpu.jx instructions to the 256-byte cell of its entry: runCounted).
  runAct(budget) {
    const c = this.cpu, act = c.act;
    let done = 0;
    while (done < budget && c.brk === 0) {
      let rem = budget - done;
      if (act.cnt < rem) rem = act.cnt;
      let n;
      if (act.rec && c.trace === null) {                    // sampling window
        c.trace = act.tbuf; c.traceHook = act.hook;
        c._nb = done; c._n = done;
        const t = c.t;
        try { n = t ? c.runThumb(rem) : c.runArm(rem); }
        catch (e) {
          if (e !== ABORT) { c.trace = null; c.traceHook = null; throw e; }
          c.takeDataAbort(); n = c._n - done + 1;
        }
        c.trace = null; c.traceHook = null; c._nb = 0;
      } else {
        const d0 = done;
        n = (act.exact ? this.runCounted(rem, done, act.x) : this.runPlain(rem, done)) - d0;
      }
      done += n;
      if ((act.cnt -= n) <= 0) act.flip();
    }
    c._n = done;
    return done;
  }
  // run()'s loop from `done` up to `budget` (returns the new done)
  runPlain(budget, done) {
    const c = this.cpu;
    let prev = null;
    budget += done;
    while (done < budget && c.brk === 0) {
      const pc = c.pc, t = c.t;
      const rem = budget - done;
      let blk = null;
      if (rem >= MAX_BLOCK) {
        if (prev !== null && prev.s0pc === pc && prev.s0.alive && prev.s0.t === t) blk = prev.s0;
        else if (prev !== null && prev.s1pc === pc && prev.s1.alive && prev.s1.t === t) blk = prev.s1;
        else {
          blk = this.lookup(pc, t);
          if (blk !== null && prev !== null) {
            if (prev.sw === 0) { prev.s0pc = pc; prev.s0 = blk; prev.sw = 1; } else { prev.s1pc = pc; prev.s1 = blk; prev.sw = 0; }
          }
        }
      }
      if (blk !== null) {
        c._n = done;
        let npc;
        try { npc = blk.fn(pc, rem); }
        catch (e) { if (e !== ABORT) throw e; c.takeDataAbort(); done = c._n + 1; prev = null; continue; }
        c.pc = npc; done += c.jx; prev = blk;
        continue;
      }
      prev = null;
      c._nb = done; c._n = done;
      let n;
      try { n = t ? c.runThumb(rem, true) : c.runArm(rem, true); }
      catch (e) { if (e !== ABORT) throw e; c.takeDataAbort(); n = c._n - done + 1; }
      c._nb = 0;
      done += n;
    }
    return done;
  }
  // runPlain + every region call / interpreted run counted in X (cell of its first instruction)
  runCounted(budget, done, X) {
    const c = this.cpu;
    let prev = null;
    budget += done;
    while (done < budget && c.brk === 0) {
      const pc = c.pc, t = c.t;
      const rem = budget - done, d0 = done;
      let blk = null;
      if (rem >= MAX_BLOCK) {
        if (prev !== null && prev.s0pc === pc && prev.s0.alive && prev.s0.t === t) blk = prev.s0;
        else if (prev !== null && prev.s1pc === pc && prev.s1.alive && prev.s1.t === t) blk = prev.s1;
        else {
          blk = this.lookup(pc, t);
          if (blk !== null && prev !== null) {
            if (prev.sw === 0) { prev.s0pc = pc; prev.s0 = blk; prev.sw = 1; } else { prev.s1pc = pc; prev.s1 = blk; prev.sw = 0; }
          }
        }
      }
      if (blk !== null) {
        c._n = done;
        let npc;
        try { npc = blk.fn(pc, rem); }
        catch (e) { if (e !== ABORT) throw e; c.takeDataAbort(); done = c._n + 1; prev = null; continue; }
        c.pc = npc; done += c.jx; prev = blk;
      } else {
        prev = null;
        c._nb = done; c._n = done;
        let n;
        try { n = t ? c.runThumb(rem, true) : c.runArm(rem, true); }
        catch (e) { if (e !== ABORT) throw e; c.takeDataAbort(); n = c._n - done + 1; }
        c._nb = 0;
        done += n;
      }
      X[(pc & 0xFF000000) === 0 ? pc >>> 8 : (pc >>> 20) === 0xFFF ? 65536 + ((pc >>> 8) & 0xFFF) : 0] += done - d0;
    }
    return done;
  }

  // ---------------------------------------------------------------- compile
  compile(entryList, t, pg, maxSrc = Infinity) {
    const t0 = performance.now();
    const c = this.cpu;
    // entry address -> dense case index (V8 compiles a dense switch to a jump table).
    // Blocks stop at other entries, so all candidate entries are known up front;
    // with a size limit, entries that do not fit are dropped (left for the next chunk).
    let entries = new Map();
    for (const e of entryList) if (!entries.has(e | 0)) entries.set(e | 0, entries.size);
    let cases, ranges, sel, len;
    for (;;) {
      cases = []; ranges = []; sel = []; len = 0; this.litRanges = [];
      let size = 0, cut = -1;
      for (const [e, x] of entries) {
        const b = t ? this.genThumb(e, entries) : this.genArm(e, entries);
        size += b.code.length;
        if (size > maxSrc && x > 0) { cut = x; break; }
        const div = t || this.noDivision ? 0 : divRoutineAt((a) => c.fetchWord(a), e);
        cases.push(`case ${x}: {\n${div ? genDivision(div) : ''}${b.code}\n}`);
        sel.push(`case ${e}: I = ${x}; break;`);
        for (const rg of b.ranges) if (rg[1] > rg[0]) ranges.push(rg);
        len += b.len;
      }
      if (cut < 0) break;
      entries = new Map([...entries].slice(0, cut));     // regenerate: exits to dropped entries leave the region
    }
    for (const lr of this.litRanges) ranges.push(lr);    // folded literals: a store to one recompiles
    // one entry (the normal case): no dispatch switch, the block is the loop body
    const single = entries.size === 1;
    const f = finalize(single ? `L: for (;;) {\n${cases[0].replace(/^case 0: \{\n/, '{\n')}\n}` : `L: for (;;) {\nswitch (I) {\n${cases.join('\n')}\n}\n}`);
    const code = f.decl + `let I = 0, X = 0, K = 0;\n` + (single ? '' : `switch (PC) { ${sel.join(' ')} default: c.jx = 0; return PC; }\n`) +
      `try {\n${f.code}\n} catch (e) { ${f.sync}throw e; }\n${f.sync}c.jx = n + K; return X;`;
    const first = entryList[0] >>> 0;
    let fn;
    try {
      const factory = new Function('c', 'r', 'm8', 'm16', 'm32', 'pf', 'H', 'F32', 'F64', 'FW',
        '"use strict";\nreturn function rgn_' + first.toString(16) + '(PC, budget) {\n' + code + '\n};');
      fn = factory(c, c.r, c.m8, c.m16, c.m32, c.pflags, HELPERS, c.F32, c.F64, c.FW);
    } catch (e) {
      if (e instanceof EvalError) {         // CSP without 'unsafe-eval': interpreter only
        this.threshold = Infinity; this.disabled = true;
        return null;
      }
      console.error('JIT compile error at ' + h8(first) + ':\n' + code);
      throw e;
    }
    const r = { fn, t, entries: [...entries.keys()], ranges, len, src: code, alive: true,
      s0pc: -1, s0: null, s1pc: -1, s1: null, sw: 0 };                       // successor cache (run())
    const tab = t ? pg.tb : pg.ab, sh = t ? 1 : 2;
    // the first entry is the one that became hot; the others (loop heads, join
    // points) are registered too unless another region already serves them
    let firstE = true;
    for (const e of entries.keys()) { const k = (e & 0xFFF) >>> sh; if (firstE || tab[k] === null) tab[k] = r; firstE = false; }
    r.pg = pg;
    pg.list.push(r);
    for (const [s0, e0] of ranges) {                     // also list it on other pages it has code/literals in
      if (s0 >= RAM_SIZE) continue;
      for (let l = s0 >>> 7; l <= (e0 - 1) >>> 7; l++) c.pflags[l] |= 2;
      for (let p = s0 >>> 12; p <= (e0 - 1) >>> 12; p++) { const q = this.getPage(p); if (q !== pg && !q.list.includes(r)) q.list.push(r); }
    }
    this.stats.compiled++;
    this.stats.blocks = (this.stats.blocks || 0) + entries.size;
    this.stats.compileMs += performance.now() - t0;
    return r;
  }

  // ================================================================= ARM
  // one basic block from `start`; stops at an unconditional exit, MAX_BLOCK,
  // the page end, or the next entry of the region (then jumps there)
  // One trace from `start`: straight-line code that continues through
  // unconditional B and BL (lr set; the callee's code joins the trace) until
  // an unconditional exit, MAX_BLOCK instructions, a page end, a target
  // already in the trace, or another entry of the region. Returns the code,
  // the instruction count and the address ranges it covers.
  genArm(start, entries, targets = null) {
    const c = this.cpu;
    const body = [], ranges = [], seen = new Set([start | 0]);
    let pc = start, seg = start, k = 0, ended = false;
    while (k < MAX_BLOCK && !ended) {
      if (k > 0 && entries.has(pc | 0)) break;   // (entries: Map address -> case index)
      const i = c.fetchWord(pc);
      k++;
      if (this.debug) body.push(`// ${h8(pc)}: ${h8(i)}`);
      const h = ARM_TABLE[((i >>> 16) & 0xFF0) | ((i >>> 4) & 0xF)];
      if ((i >>> 28) === 14 && (h === H_B || h === H_BL) && this.follow) {
        const tgt = (pc + 8 + ((i << 8) >> 6)) | 0;
        if (!entries.has(tgt) && !seen.has(tgt) && JIT.pageIndex(tgt) >= 0 && k < MAX_BLOCK - 1) {
          if (h === H_BL) body.push(`r[14] = ${(pc + 4) | 0};`);
          ranges.push([seg >>> 0, ((pc + 4) >>> 0)]);
          pc = seg = tgt; seen.add(tgt);
          continue;
        }
      }
      const g = this.armInsn(i, pc, k);
      body.push(tagExits(g.code, k, entries, targets, (pc + 4) | 0));
      ended = g.end;
      pc = (pc + 4) | 0;
      if ((pc & 0xFFF) === 0) break;
    }
    ranges.push([seg >>> 0, pc >>> 0]);
    if (!ended) body.push(tagExits(`${FL_STORE}return ${pc | 0};`, k, entries, targets));
    return { code: body.join('\n'), len: k, ranges };
  }

  // Generate code for one ARM instruction. k = index (1-based) within block.
  // Returns { code, end } where end = the instruction always leaves the block.
  armInsn(i, pc, k) {
    const cond = i >>> 28;
    const P8 = (pc + 8) | 0, NX = (pc + 4) | 0;
    let body, end = false;
    if (cond === 15) {
      if ((i & 0x0E000000) === 0x0A000000) {   // BLX imm
        const tgt = (pc + 8 + ((i << 8) >> 6) + ((i >>> 23) & 2)) | 0;
        return { code: `r[14] = ${NX}; c.t = 1; c.brk = 1; ${FL_STORE}return ${tgt};`, end: true };
      }
      if ((i & 0x0D70F000) === 0x0550F000) return { code: '', end: false };   // PLD
      return { code: `${FL_STORE}c.pc = ${pc}; r[15] = ${P8}; return c.undefinedInsn(${pc});`, end: true };
    }
    const h = ARM_TABLE[((i >>> 16) & 0xFF0) | ((i >>> 4) & 0xF)];
    if ((h === H_CPREG || h === H_CPOTHER) && ((i >>> 8) & 14) === 10) {       // VFP (jitvfp.mjs)
      const fb = `${FL_STORE}r[15] = ${P8}; c.pc = ${pc}; { const t = c.vfpExec(${i | 0}, ${pc}); if (t !== ${NX} || c.brk) return t; } ${FL_LOAD}`;
      body = genVfp(i, pc, fb, FL_STORE, FL_EXPR) ?? fb;
      if (cond === 14) return { code: body, end: false };
      return { code: `if (${COND[cond]}) { ${body} }`, end: false };
    }
    switch (h) {
      case H_DP: ({ body, end } = this.armDP(i, pc)); break;
      case H_LS: ({ body, end } = this.armLS(i, pc)); break;
      case H_B: body = `${FL_STORE}return ${(pc + 8 + ((i << 8) >> 6)) | 0};`; end = true; break;
      case H_BX: case H_BLXR: {         // BX / BXJ / BLX register: inline with interworking
        const m = i & 15;
        const v = m === 15 ? `${(pc + 8) | 0}` : `r[${m}]`;
        body = `{ const v = ${v}; ${h === H_BLXR ? `r[14] = ${NX}; ` : ''}${FL_STORE}if (v & 1) { c.t = 1; c.brk = 1; return v & ~1; } return v & ~3; }`;
        end = true; break;
      }
      case H_BL: body = `r[14] = ${NX}; ${FL_STORE}return ${(pc + 8 + ((i << 8) >> 6)) | 0};`; end = true; break;
      case H_MUL: {
        const d = (i >>> 16) & 15, S = i & 0x00100000;
        if (d === 15) { body = null; break; }
        let e = `Math.imul(r[${i & 15}], r[${(i >>> 8) & 15}])`;
        if (i & 0x00200000) e = `(${e} + r[${(i >>> 12) & 15}]) | 0`;
        body = S ? `{ const res = ${e}; r[${d}] = res; nv = zv = res; }` : `r[${d}] = ${e};`;
        break;
      }
      case H_MULL: {
        const hi = (i >>> 16) & 15, lo = (i >>> 12) & 15;
        if (hi === 15 || lo === 15) { body = null; break; }
        body = `H.${i & 0x00400000 ? 'smul64' : 'umul64'}(r[${i & 15}], r[${(i >>> 8) & 15}]);`;
        if (i & 0x00200000) body += ` H.add64(H.MR[0], H.MR[1], r[${lo}], r[${hi}]);`;
        body += ` r[${lo}] = H.MR[0]; r[${hi}] = H.MR[1];`;
        if (i & 0x00100000) body += ' nv = H.MR[1]; zv = H.MR[1] | H.MR[0];';
        break;
      }
      case H_CLZ: {
        const d = (i >>> 12) & 15;
        body = d === 15 ? null : `r[${d}] = Math.clz32(r[${i & 15}]);`;
        break;
      }
      case H_MRS: {
        const d = (i >>> 12) & 15;
        body = d === 15 ? null : `${FL_STORE}r[${d}] = ${(i & 0x00400000) ? 'c.spsr' : 'c.getCPSR()'};`;
        break;
      }
      case H_XLS: {
        const sh = (i >>> 5) & 3, L = i & 0x00100000;
        if (sh === 1 || L) ({ body, end } = this.armXLS(i, pc));
        else ({ body, end } = this.armLDRD(i, pc));
        break;
      }
      case H_LDM: {
        const g = this.armLDM(i, pc, k);
        body = g.body; end = g.end;
        break;
      }
      case H_SWP: case H_QOP: case H_SMULXY:
        body = `${FL_STORE}r[15] = ${P8}; c.pc = ${pc}; c.armMisc(${i | 0}, ${pc}, ${NX}); ${FL_LOAD}`;
        break;
      default:
        // everything else may branch, change mode, sleep or trap: leave the block
        body = `${FL_STORE}r[15] = ${P8}; c.pc = ${pc}; return c.armMisc(${i | 0}, ${pc}, ${NX});`;
        end = true;
    }
    if (body === null) {       // generic fallback through the interpreter's handlers
      body = `${FL_STORE}r[15] = ${P8}; c.pc = ${pc}; { const t = c.armFallback(${i | 0}, ${pc}); if (t !== ${NX} || c.brk) return t; } ${FL_LOAD}`;
    }
    if (cond === 14) return { code: body, end };
    return { code: `if (${COND[cond]}) { ${body} }`, end: false };
  }

  // operand 2 of a data-processing instruction.
  // Returns { pre, b, sc } : statements, value expr (int32), carry expr (or null if unused)
  op2(i, pc, needC) {
    if (i & 0x02000000) {
      const rot = (i >>> 7) & 30; let v = i & 0xFF;
      if (rot) v = (v >>> rot) | (v << (32 - rot));
      return { pre: '', b: `${v | 0}`, sc: needC ? (rot ? `${v >>> 31}` : 'cf') : null, regreg: false };
    }
    const m = i & 15, type = (i >>> 5) & 3;
    if ((i & 0x10) === 0) {
      const sh = (i >>> 7) & 31;
      const rm = m === 15 ? `${(pc + 8) | 0}` : `r[${m}]`;
      let pre = `const rm = ${rm};`, b, sc = null;
      switch (type) {
        case 0: if (sh === 0) { b = 'rm'; sc = 'cf'; } else { b = `(rm << ${sh})`; sc = `((rm >>> ${32 - sh}) & 1)`; } break;
        case 1: if (sh === 0) { b = '0'; sc = '(rm >>> 31)'; } else { b = `((rm >>> ${sh}) | 0)`; sc = `((rm >>> ${sh - 1}) & 1)`; } break;
        case 2: if (sh === 0) { b = '(rm >> 31)'; sc = '(rm >>> 31)'; } else { b = `(rm >> ${sh})`; sc = `((rm >>> ${sh - 1}) & 1)`; } break;
        default: if (sh === 0) { b = '((cf << 31) | (rm >>> 1))'; sc = '(rm & 1)'; } else { b = `((rm >>> ${sh}) | (rm << ${32 - sh}))`; sc = `((rm >>> ${sh - 1}) & 1)`; }
      }
      return { pre, b, sc: needC ? sc : null, regreg: false };
    }
    // register-specified shift
    const s = (i >>> 8) & 15;
    const rm = m === 15 ? `${(pc + 12) | 0}` : `r[${m}]`;
    let pre = `const rm = ${rm}, s = r[${s}] & 0xFF; let b = rm${needC ? ', sc = cf' : ''};`;
    switch (type) {
      case 0: pre += needC ? ' if (s !== 0) { if (s < 32) { b = rm << s; sc = (rm >>> (32 - s)) & 1; } else if (s === 32) { b = 0; sc = rm & 1; } else { b = 0; sc = 0; } }'
        : ' if (s !== 0) b = s < 32 ? rm << s : 0;'; break;
      case 1: pre += needC ? ' if (s !== 0) { if (s < 32) { b = rm >>> s; sc = (rm >>> (s - 1)) & 1; } else if (s === 32) { b = 0; sc = rm >>> 31; } else { b = 0; sc = 0; } }'
        : ' if (s !== 0) b = s < 32 ? (rm >>> s) | 0 : 0;'; break;
      case 2: pre += needC ? ' if (s !== 0) { if (s < 32) { b = rm >> s; sc = (rm >>> (s - 1)) & 1; } else { b = rm >> 31; sc = rm >>> 31; } }'
        : ' if (s !== 0) b = rm >> (s < 32 ? s : 31);'; break;
      default: pre += needC ? ' if (s !== 0) { const s5 = s & 31; if (s5 === 0) sc = rm >>> 31; else { b = (rm >>> s5) | (rm << (32 - s5)); sc = (rm >>> (s5 - 1)) & 1; } }'
        : ' { const s5 = s & 31; if (s5 !== 0) b = (rm >>> s5) | (rm << (32 - s5)); }';
    }
    return { pre, b: 'b', sc: needC ? 'sc' : null, regreg: true };
  }

  armDP(i, pc) {
    const op = (i >>> 21) & 15, S = (i & 0x00100000) !== 0;
    const n = (i >>> 16) & 15, d = (i >>> 12) & 15;
    const logical = [0, 1, 8, 9, 12, 13, 14, 15].includes(op);
    const o = this.op2(i, pc, S && logical);
    const a = n === 15 ? `${(pc + (o.regreg ? 12 : 8)) | 0}` : `r[${n}]`;
    let e, flags = '';
    const usesA = !(op === 13 || op === 15);
    const pre = o.pre + (usesA ? ` const a = ${a};` : '');
    switch (op) {
      case 0: case 8: e = `a & ${o.b}`; break;
      case 1: case 9: e = `a ^ ${o.b}`; break;
      case 12: e = `a | ${o.b}`; break;
      case 13: e = `${o.b} | 0`; break;
      case 14: e = `a & ~${o.b}`; break;
      case 15: e = `~${o.b}`; break;
      case 2: case 10: e = `(a - bb) | 0`; if (S) flags = 'cf = (a >>> 0) >= (bb >>> 0) ? 1 : 0; vf = ((a ^ bb) & (a ^ res)) >>> 31;'; break;
      case 3: e = `(bb - a) | 0`; if (S) flags = 'cf = (bb >>> 0) >= (a >>> 0) ? 1 : 0; vf = ((bb ^ a) & (bb ^ res)) >>> 31;'; break;
      case 4: case 11: e = `(a + bb) | 0`; if (S) flags = 'cf = (res >>> 0) < (a >>> 0) ? 1 : 0; vf = ((a ^ res) & (bb ^ res)) >>> 31;'; break;
      case 5: e = `(a + bb + cf) | 0`; if (S) flags = 'cf = ((a >>> 0) + (bb >>> 0) + cf) > 0xFFFFFFFF ? 1 : 0; vf = ((a ^ res) & (bb ^ res)) >>> 31;'; break;
      case 6: e = `(a - bb - 1 + cf) | 0`; if (S) flags = 'cf = (a >>> 0) >= (bb >>> 0) + 1 - cf ? 1 : 0; vf = ((a ^ bb) & (a ^ res)) >>> 31;'; break;
      case 7: e = `(bb - a - 1 + cf) | 0`; if (S) flags = 'cf = (bb >>> 0) >= (a >>> 0) + 1 - cf ? 1 : 0; vf = ((bb ^ a) & (bb ^ res)) >>> 31;'; break;
    }
    const arith = !logical;
    if (!S && d !== 15 && !o.regreg && !(op >= 8 && op <= 11)) {
      // compact form (most instructions): no temporaries
      const B = o.pre ? o.b.replace(/\brm\b/g, o.pre.match(/const rm = ([^;]*);/)[1]) : o.b;
      const expr = e.replace(/\bbb\b/g, `(${B})`).replace(/\ba\b/g, a).replace(/\$\{o\.b\}/g, B);
      const ex2 = arith ? expr : e.replace(o.b, `(${B})`).replace(/^a\b/, a);
      return { body: `r[${d}] = ${arith ? expr : ex2};`, end: false };
    }
    let code = `{ ${pre}${arith ? ` const bb = ${o.b};` : ''} const res = ${e};`;
    // note: for ADC/SBC/RSC the carry-in must be read before cf is updated: flags come after res
    if (S) code += logical ? ` nv = zv = res; cf = ${o.sc};` : ` ${flags} nv = zv = res;`;
    const test = op >= 8 && op <= 11;
    if (test) return { body: code + ' }', end: false };
    if (d !== 15) return { body: code + ` r[${d}] = res; }`, end: false };
    // write to PC
    if (S) code += ` ${FL_STORE}c.setCPSR(c.spsr, 15, true); return c.t ? res & ~1 : res & ~3; }`;
    else code += ` ${FL_STORE}return res & ~3; }`;
    return { body: code, end: true };
  }

  // the word at a (aligned RAM/ROM) as a compile-time constant; RAM literals are
  // added to the region's ranges so that a store to them recompiles it
  literal(a) {
    if (a & 3) return null;
    if ((a & 0xFF000000) === 0) { this.litRanges.push([a >>> 0, (a >>> 0) + 4]); return this.cpu.m32[a >>> 2]; }
    if ((a >>> 20) === 0xFFF) return this.cpu.m32[((a & 0xFFFFF) + RAM_SIZE) >>> 2];
    return null;
  }

  armLS(i, pc) {
    const n = (i >>> 16) & 15, d = (i >>> 12) & 15;
    const P = i & 0x01000000, U = i & 0x00800000, B = i & 0x00400000, W = i & 0x00200000, L = i & 0x00100000;
    let off;
    if (i & 0x02000000) {
      const m = i & 15, sh = (i >>> 7) & 31, rm = m === 15 ? `${(pc + 8) | 0}` : `r[${m}]`;
      switch ((i >>> 5) & 3) {
        case 0: off = sh ? `(${rm} << ${sh})` : rm; break;
        case 1: off = sh ? `(${rm} >>> ${sh})` : '0'; break;
        case 2: off = `(${rm} >> ${sh || 31})`; break;
        default: off = sh ? `((${rm} >>> ${sh}) | (${rm} << ${32 - sh}))` : `((cf << 31) | (${rm} >>> 1))`;
      }
    } else off = `${i & 0xFFF}`;
    const base = n === 15 ? `${(pc + 8) | 0}` : `r[${n}]`;
    const upd = off === '0' ? 'base' : `(base ${U ? '+' : '-'} ${off}) | 0`;
    const wb = !P || W;
    if (n === 15 && L && !B && !wb && d !== 15 && !(i & 0x02000000)) {       // PC-relative literal: fold it
      const lit = this.literal((pc + 8 + (U ? (i & 0xFFF) : -(i & 0xFFF))) | 0);
      if (lit !== null) return { body: `r[${d}] = ${lit};`, end: false };
    }
    let code = !wb ? `{ const a = ${off === '0' ? base : `(${base} ${U ? '+' : '-'} ${off}) | 0`};`
      : `{ const base = ${base};${wb || P ? ` const upd = ${upd};` : ''} const a = ${P ? 'upd' : 'base'};`;
    if (L) {
      code += B ? ` const v = (a & 0xFF000000) === 0${this.mm('a')} ? m8[a] : (${FL_EXPR}c.ld8(a, ${pc}));`
        : ` const v = (a & 0xFF000003) === 0${this.mm('a')} ? m32[a >>> 2] : (${FL_EXPR}c.ld32(a, ${pc}));`;
      if (wb && n !== 15) code += ` r[${n}] = upd;`;
      if (d === 15) return { body: code + ` ${FL_STORE}if (v & 1) { c.t = 1; c.brk = 1; return v & ~1; } return v & ~3; }`, end: true };
      return { body: code + ` r[${d}] = v; }`, end: false };
    }
    const val = d === 15 ? `${(pc + 8) | 0}` : `r[${d}]`;
    code += B ? ` if ((a & 0xFF000000) === 0 && pf[a >>> 7] === 0) m8[a] = ${val}; else { ${FL_STORE}c.st8(a, ${val}, ${pc}); }`
      : ` if ((a & 0xFF000003) === 0 && pf[a >>> 7] === 0) m32[a >>> 2] = ${val}; else { ${FL_STORE}c.st32(a, ${val}, ${pc}); }`;
    if (wb && n !== 15) code += ` r[${n}] = upd;`;
    return { body: code + ' }', end: false };
  }

  // LDRD / STRD (even Rd < 14; anything else through the interpreter)
  armLDRD(i, pc) {
    const n = (i >>> 16) & 15, d = (i >>> 12) & 15, L = ((i >>> 5) & 3) === 2;
    const P = i & 0x01000000, U = i & 0x00800000, W = i & 0x00200000;
    const wb = !P || W;
    if ((d & 1) || d === 14 || (n === 15 && wb) || (wb && (n === d || n === d + 1))) return { body: null, end: false };
    const off = (i & 0x00400000) ? `${((i >>> 4) & 0xF0) | (i & 0xF)}` : `r[${i & 15}]`;
    const base = n === 15 ? `${(pc + 8) | 0}` : `r[${n}]`;
    let code = `{ const base = ${base}; const upd = (base ${U ? '+' : '-'} ${off}) | 0; const a = ${P ? 'upd' : 'base'};`;
    if (L) code += ` let lo, hi; if ((a & 0xFF000003) === 0 && ((a + 4) & 0xFF000000) === 0${this.mm2('a', 4)}) { lo = m32[a >>> 2]; hi = m32[(a >>> 2) + 1]; } else { ${FL_STORE}lo = c.ld32a(a, ${pc}); hi = c.ld32a((a + 4) | 0, ${pc}); }`;
    else code += ` if ((a & 0xFF000003) === 0 && ((a + 4) & 0xFF000000) === 0 && pf[a >>> 7] === 0 && pf[(a + 4) >>> 7] === 0) { m32[a >>> 2] = r[${d}]; m32[(a >>> 2) + 1] = r[${d + 1}]; } else { ${FL_STORE}c.st32(a, r[${d}], ${pc}); c.st32((a + 4) | 0, r[${d + 1}], ${pc}); }`;
    if (wb) code += ` r[${n}] = upd;`;
    if (L) code += ` r[${d}] = lo; r[${d + 1}] = hi;`;
    return { body: code + ' }', end: false };
  }

  armXLS(i, pc) {
    const n = (i >>> 16) & 15, d = (i >>> 12) & 15, sh = (i >>> 5) & 3;
    const P = i & 0x01000000, U = i & 0x00800000, W = i & 0x00200000, L = i & 0x00100000;
    if (d === 15 || n === 15 && (!P || W)) return { body: null, end: false };
    const off = (i & 0x00400000) ? `${((i >>> 4) & 0xF0) | (i & 0xF)}` : `r[${i & 15}]`;
    const base = n === 15 ? `${(pc + 8) | 0}` : `r[${n}]`;
    const wb = !P || W;
    let code = `{ const base = ${base}; const upd = (base ${U ? '+' : '-'} ${off}) | 0; const a = ${P ? 'upd' : 'base'};`;
    if (L) {
      let ld;
      if (sh === 1) ld = `(a & 0xFF000001) === 0${this.mm('a')} ? m16[a >>> 1] : (${FL_EXPR}c.ld16(a, ${pc}))`;
      else if (sh === 2) ld = `((a & 0xFF000000) === 0${this.mm('a')} ? m8[a] : (${FL_EXPR}c.ld8(a, ${pc}))) << 24 >> 24`;
      else ld = `((a & 0xFF000001) === 0${this.mm('a')} ? m16[a >>> 1] : (${FL_EXPR}c.ld16(a, ${pc}))) << 16 >> 16`;
      code += ` const v = ${ld};`;
      if (wb) code += ` r[${n}] = upd;`;
      return { body: code + ` r[${d}] = v; }`, end: false };
    }
    code += ` if ((a & 0xFF000001) === 0 && pf[a >>> 7] === 0) m16[a >>> 1] = r[${d}]; else { ${FL_STORE}c.st16(a, r[${d}], ${pc}); }`;
    if (wb) code += ` r[${n}] = upd;`;
    return { body: code + ' }', end: false };
  }

  armLDM(i, pc, k) {
    const n = (i >>> 16) & 15, list = i & 0xFFFF;
    const P = i & 0x01000000, U = i & 0x00800000, S = i & 0x00400000, W = i & 0x00200000, L = i & 0x00100000;
    const NX = (pc + 4) | 0;
    if (L && (list & 0x8000) && !S && n !== 15 && !(W && (list & (1 << n)))) {
      // LDM with PC (function return, "pop {..., pc}"): inline, interworking on bit 0
      const regs = []; for (let x = 0; x < 15; x++) if (list & (1 << x)) regs.push(x);
      const cnt = regs.length + 1;
      const startOff = U ? (P ? 4 : 0) : (P ? -4 * cnt : -4 * cnt + 4);
      let code = `{ const base = r[${n}]; const a = (base + ${startOff}) | 0; if ((a & 0xFF000003) === 0 && ((a + ${4 * cnt - 4}) & 0xFF000000) === 0${this.mm2('a', 4 * cnt - 4)}) { const w = a >>> 2; `;
      code += regs.map((x, j) => `r[${x}] = m32[w + ${j}]; `).join('') + `const v = m32[w + ${cnt - 1}]; `;
      if (W) code += `r[${n}] = (base + ${U ? 4 * cnt : -4 * cnt}) | 0; `;
      code += `${FL_STORE}if (v & 1) { c.t = 1; c.brk = 1; return v & ~1; } return v & ~3; } `;
      code += `${FL_STORE}r[15] = ${(pc + 8) | 0}; c.pc = ${pc}; return c.armLdm(${i | 0}, ${pc}, ${NX}); }`;
      return { body: code, end: true };
    }
    if (S || n === 15 || list === 0 || (L && (list & 0x8000))) {
      // user-bank / exception return / load PC: through the interpreter, leave block
      return { body: `${FL_STORE}r[15] = ${(pc + 8) | 0}; c.pc = ${pc}; return c.armLdm(${i | 0}, ${pc}, ${NX});`, end: true };
    }
    const regs = []; for (let x = 0; x < 16; x++) if (list & (1 << x)) regs.push(x);
    const cnt = regs.length;
    const startOff = U ? (P ? 4 : 0) : (P ? -4 * cnt : -4 * cnt + 4);
    const wbOff = U ? 4 * cnt : -4 * cnt;
    let code = `{ const base = r[${n}]; const a = (base + ${startOff}) | 0;`;
    // fast path: aligned, all in RAM, (stores) no special pages
    const last = 4 * (cnt - 1);
    if (L) {
      code += ` if ((a & 0xFF000003) === 0 && ((a + ${last}) & 0xFF000000) === 0${this.mm2('a', last)}) { const w = a >>> 2;`;
      const tmp = regs.map((x, j) => `const t${j} = m32[w + ${j}];`).join(' ');
      code += ` ${tmp}`;
      if (W) code += ` r[${n}] = (base + ${wbOff}) | 0;`;
      code += ' ' + regs.map((x, j) => `r[${x}] = t${j};`).join(' ');
      code += ` } else { ${FL_STORE}r[15] = ${(pc + 8) | 0}; c.pc = ${pc}; c.armLdm(${i | 0}, ${pc}, ${NX}); ${FL_LOAD}} }`;
    } else {
      code += ` if ((a & 0xFF000003) === 0 && ((a + ${last}) & 0xFF000000) === 0 && pf[a >>> 7] === 0 && pf[(a + ${last}) >>> 7] === 0) { const w = a >>> 2;`;
      code += ' ' + regs.map((x, j) => `m32[w + ${j}] = ${x === 15 ? (pc + 8) | 0 : `r[${x}]`};`).join(' ');
      if (W) code += ` r[${n}] = (base + ${wbOff}) | 0;`;
      code += ` } else { ${FL_STORE}r[15] = ${(pc + 8) | 0}; c.pc = ${pc}; c.armLdm(${i | 0}, ${pc}, ${NX}); ${FL_LOAD}} }`;
    }
    void k;
    return { body: code, end: false };
  }

  // ================================================================= Thumb
  genThumb(start, entries, targets = null) {
    const c = this.cpu;
    const body = [], ranges = [], seen = new Set([start | 0]);
    let pc = start, seg = start, k = 0, ended = false;
    while (k < MAX_BLOCK && !ended) {
      if (k > 0 && entries.has(pc | 0)) break;
      const i = c.fetchHalf(pc);
      k++;
      // fuse BL prefix + suffix
      if ((i >>> 11) === 30 && ((pc + 2) & 0xFFF) !== 0) {
        const j = c.fetchHalf(pc + 2);
        if ((j >>> 11) === 31 || ((j >>> 11) === 29 && !(j & 1))) {
          const lr = (pc + 4 + (((i & 0x7FF) << 21) >> 9)) | 0;
          const tgt = (lr + ((j & 0x7FF) << 1)) | 0;
          const ret = (pc + 4) | 1;
          k++;
          if (this.debug) body.push(`// ${h8(pc)}: ${i.toString(16)} ${j.toString(16)} (bl pair)`);
          if ((j >>> 11) === 31 && this.follow && !entries.has(tgt & ~1) && !seen.has(tgt & ~1) && JIT.pageIndex(tgt) >= 0 && k < MAX_BLOCK - 1) {
            body.push(`r[14] = ${ret};`);               // BL: the callee joins the trace
            ranges.push([seg >>> 0, ((pc + 4) >>> 0)]);
            pc = seg = tgt & ~1; seen.add(pc);
            continue;
          }
          if ((j >>> 11) === 31) body.push(tagExits(`r[14] = ${ret}; ${FL_STORE}return ${tgt & ~1};`, k, entries, targets));
          else body.push(tagExits(`r[14] = ${ret}; c.t = 0; c.brk = 1; ${FL_STORE}return ${tgt & ~3};`, k, entries, targets));
          ended = true;
          break;
        }
      }
      if (this.debug) body.push(`// ${h8(pc)}: ${i.toString(16)}`);
      if ((i >>> 11) === 28 && this.follow) {         // unconditional B
        const tgt = (pc + 4 + ((i << 21) >> 20)) | 0;
        if (!entries.has(tgt) && !seen.has(tgt) && JIT.pageIndex(tgt) >= 0 && k < MAX_BLOCK - 1) {
          ranges.push([seg >>> 0, ((pc + 2) >>> 0)]);
          pc = seg = tgt; seen.add(tgt);
          continue;
        }
      }
      const g = this.thumbInsn(i, pc, k);
      body.push(tagExits(g.code, k, entries, targets, (pc + 2) | 0));
      ended = g.end;
      pc = (pc + 2) | 0;
      if ((pc & 0xFFF) === 0) break;
    }
    ranges.push([seg >>> 0, pc >>> 0]);
    if (!ended) body.push(tagExits(`${FL_STORE}return ${pc | 0};`, k, entries, targets));
    return { code: body.join('\n'), len: k, ranges };
  }

  thumbInsn(i, pc, k) {
    const P4 = (pc + 4) | 0, NX = (pc + 2) | 0;
    const ld32 = (a) => `((${a} & 0xFF000003) === 0${this.mm(a)} ? m32[${a} >>> 2] : (${FL_EXPR}c.ld32(${a}, ${pc})))`;
    const st32 = (a, v) => `if ((${a} & 0xFF000003) === 0 && pf[${a} >>> 7] === 0) m32[${a} >>> 2] = ${v}; else { ${FL_STORE}c.st32(${a}, ${v}, ${pc}); }`;
    const ld16 = (a) => `((${a} & 0xFF000001) === 0${this.mm(a)} ? m16[${a} >>> 1] : (${FL_EXPR}c.ld16(${a}, ${pc})))`;
    const ld8 = (a) => `((${a} & 0xFF000000) === 0${this.mm(a)} ? m8[${a}] : (${FL_EXPR}c.ld8(${a}, ${pc})))`;
    const st8 = (a, v) => `if ((${a} & 0xFF000000) === 0 && pf[${a} >>> 7] === 0) m8[${a}] = ${v}; else { ${FL_STORE}c.st8(${a}, ${v}, ${pc}); }`;
    const fallback = `${FL_STORE}c.thumbFallback(${pc}); ${FL_LOAD}`;
    const fallbackEnd = `${FL_STORE}return c.thumbFallback(${pc});`;
    const st16 = (a, v) => `if ((${a} & 0xFF000001) === 0 && pf[${a} >>> 7] === 0) m16[${a} >>> 1] = ${v}; else { ${FL_STORE}c.st16(${a}, ${v}, ${pc}); }`;
    const rd = i & 7, rs = (i >>> 3) & 7;
    switch (i >>> 11) {
      case 0: { const sh = (i >>> 6) & 31;
        if (sh === 0) return { code: `{ const v = r[${rs}]; r[${rd}] = v; nv = zv = v; }` };
        return { code: `{ const v = r[${rs}]; const res = v << ${sh}; cf = (v >>> ${32 - sh}) & 1; r[${rd}] = res; nv = zv = res; }` }; }
      case 1: { const sh = (i >>> 6) & 31;
        if (sh === 0) return { code: `{ const v = r[${rs}]; cf = v >>> 31; r[${rd}] = 0; nv = zv = 0; }` };
        return { code: `{ const v = r[${rs}]; const res = (v >>> ${sh}) | 0; cf = (v >>> ${sh - 1}) & 1; r[${rd}] = res; nv = zv = res; }` }; }
      case 2: { const sh = (i >>> 6) & 31;
        if (sh === 0) return { code: `{ const v = r[${rs}]; cf = v >>> 31; const res = v >> 31; r[${rd}] = res; nv = zv = res; }` };
        return { code: `{ const v = r[${rs}]; const res = v >> ${sh}; cf = (v >>> ${sh - 1}) & 1; r[${rd}] = res; nv = zv = res; }` }; }
      case 3: {
        const b = (i & 0x400) ? `${(i >>> 6) & 7}` : `r[${(i >>> 6) & 7}]`;
        if (i & 0x200) return { code: `{ const a = r[${rs}], bb = ${b}; const res = (a - bb) | 0; cf = (a >>> 0) >= (bb >>> 0) ? 1 : 0; vf = ((a ^ bb) & (a ^ res)) >>> 31; nv = zv = res; r[${rd}] = res; }` };
        return { code: `{ const a = r[${rs}], bb = ${b}; const res = (a + bb) | 0; cf = (res >>> 0) < (a >>> 0) ? 1 : 0; vf = ((a ^ res) & (bb ^ res)) >>> 31; nv = zv = res; r[${rd}] = res; }` };
      }
      case 4: return { code: `r[${(i >>> 8) & 7}] = ${i & 0xFF}; nv = zv = ${i & 0xFF};` };
      case 5: return { code: `{ const a = r[${(i >>> 8) & 7}], bb = ${i & 0xFF}; const res = (a - bb) | 0; cf = (a >>> 0) >= bb ? 1 : 0; vf = ((a ^ bb) & (a ^ res)) >>> 31; nv = zv = res; }` };
      case 6: { const d = (i >>> 8) & 7; return { code: `{ const a = r[${d}], bb = ${i & 0xFF}; const res = (a + bb) | 0; cf = (res >>> 0) < (a >>> 0) ? 1 : 0; vf = ((a ^ res) & (bb ^ res)) >>> 31; nv = zv = res; r[${d}] = res; }` }; }
      case 7: { const d = (i >>> 8) & 7; return { code: `{ const a = r[${d}], bb = ${i & 0xFF}; const res = (a - bb) | 0; cf = (a >>> 0) >= bb ? 1 : 0; vf = ((a ^ bb) & (a ^ res)) >>> 31; nv = zv = res; r[${d}] = res; }` }; }
      case 8:
        if (i & 0x400) {
          const d = (i & 7) | ((i >>> 4) & 8), m = (i >>> 3) & 15;
          const v = m === 15 ? `${P4}` : `r[${m}]`;
          switch ((i >>> 8) & 3) {
            case 0:
              if (d === 15) return { code: `${FL_STORE}return (${P4} + ${v}) & ~1;`, end: true };
              return { code: `r[${d}] = (r[${d}] + ${v}) | 0;` };
            case 1: { const a = d === 15 ? `${P4}` : `r[${d}]`;
              return { code: `{ const a = ${a}, bb = ${v}; const res = (a - bb) | 0; cf = (a >>> 0) >= (bb >>> 0) ? 1 : 0; vf = ((a ^ bb) & (a ^ res)) >>> 31; nv = zv = res; }` }; }
            case 2:
              if (d === 15) return { code: `${FL_STORE}return ${v} & ~1;`, end: true };
              return { code: `r[${d}] = ${v};` };
            default:
              return { code: `{ const v = ${v}; ${i & 0x80 ? `r[14] = ${NX | 1}; ` : ''}${FL_STORE}if (v & 1) return v & ~1; c.t = 0; c.brk = 1; return v & ~3; }`, end: true };
          }
        }
        return { code: this.thumbAlu(i) };
      case 9: { const a = ((pc + 4) & ~3) + ((i & 0xFF) << 2);
        const lit = this.literal(a | 0);
        if (lit !== null) return { code: `r[${(i >>> 8) & 7}] = ${lit};` };
        return { code: `r[${(i >>> 8) & 7}] = ${ld32(`${a | 0}`)};` }; }
      case 10: case 11: {
        const ro = (i >>> 6) & 7;
        let code = `{ const a = (r[${rs}] + r[${ro}]) | 0; `;
        switch ((i >>> 9) & 7) {
          case 0: code += st32('a', `r[${rd}]`); break;
          case 1: code += st16('a', `r[${rd}]`); break;
          case 2: code += st8('a', `r[${rd}]`); break;
          case 3: code += `r[${rd}] = ${ld8('a')} << 24 >> 24;`; break;
          case 4: code += `r[${rd}] = ${ld32('a')};`; break;
          case 5: code += `r[${rd}] = ${ld16('a')};`; break;
          case 6: code += `r[${rd}] = ${ld8('a')};`; break;
          default: code += `r[${rd}] = ${ld16('a')} << 16 >> 16;`; break;
        }
        return { code: code + ' }' };
      }
      case 12: return { code: `{ const a = (r[${rs}] + ${(i >>> 4) & 0x7C}) | 0; ${st32('a', `r[${rd}]`)} }` };
      case 13: return { code: `{ const a = (r[${rs}] + ${(i >>> 4) & 0x7C}) | 0; r[${rd}] = ${ld32('a')}; }` };
      case 14: return { code: `{ const a = (r[${rs}] + ${(i >>> 6) & 31}) | 0; ${st8('a', `r[${rd}]`)} }` };
      case 15: return { code: `{ const a = (r[${rs}] + ${(i >>> 6) & 31}) | 0; r[${rd}] = ${ld8('a')}; }` };
      case 16: return { code: `{ const a = (r[${rs}] + ${(i >>> 5) & 0x3E}) | 0; ${st16('a', `r[${rd}]`)} }` };
      case 17: return { code: `{ const a = (r[${rs}] + ${(i >>> 5) & 0x3E}) | 0; r[${rd}] = ${ld16('a')}; }` };
      case 18: return { code: `{ const a = (r[13] + ${(i & 0xFF) << 2}) | 0; ${st32('a', `r[${(i >>> 8) & 7}]`)} }` };
      case 19: return { code: `{ const a = (r[13] + ${(i & 0xFF) << 2}) | 0; r[${(i >>> 8) & 7}] = ${ld32('a')}; }` };
      case 20: return { code: `r[${(i >>> 8) & 7}] = ${(((pc + 4) & ~3) + ((i & 0xFF) << 2)) | 0};` };
      case 21: return { code: `r[${(i >>> 8) & 7}] = (r[13] + ${(i & 0xFF) << 2}) | 0;` };
      case 22: case 23: {
        const op = (i >>> 8) & 15;
        if (op === 0) return { code: `r[13] = (r[13] ${i & 0x80 ? '-' : '+'} ${(i & 0x7F) << 2}) | 0;` };
        if (op === 4 || op === 5) {             // PUSH
          const regs = []; for (let x = 0; x < 8; x++) if (i & (1 << x)) regs.push(x);
          if (i & 0x100) regs.push(14);
          const n = regs.length;
          let code = `{ const a = (r[13] - ${4 * n}) | 0; if ((a & 0xFF000003) === 0 && ((a + ${4 * n - 1}) & 0xFF000000) === 0 && pf[a >>> 7] === 0 && pf[(a + ${4 * n - 1}) >>> 7] === 0) { const w = a >>> 2; `;
          code += regs.map((x, j) => `m32[w + ${j}] = r[${x}];`).join(' ');
          code += ` } else { ${FL_STORE}${regs.map((x, j) => `c.st32((a + ${4 * j}) | 0, r[${x}], ${pc});`).join(' ')} } r[13] = a; }`;
          return { code };
        }
        if (op === 12 || op === 13) {           // POP
          const regs = []; for (let x = 0; x < 8; x++) if (i & (1 << x)) regs.push(x);
          const hasPc = (i & 0x100) !== 0;
          const n = regs.length + (hasPc ? 1 : 0);
          let code = `{ const a = r[13]; if ((a & 0xFF000003) === 0 && ((a + ${4 * n - 1}) & 0xFF000000) === 0${this.mm2('a', 4 * n - 1)}) { const w = a >>> 2; `;
          code += regs.map((x, j) => `r[${x}] = m32[w + ${j}];`).join(' ');
          code += `${hasPc ? ` const v = m32[w + ${regs.length}];` : ''} r[13] = (a + ${4 * n}) | 0;`;
          if (hasPc) code += ` ${FL_STORE}if (v & 1) return v & ~1; c.t = 0; c.brk = 1; return v & ~3;`;
          code += ` } else { ${hasPc ? fallbackEnd : fallback} } }`;
          return { code, end: hasPc };
        }
        return { code: fallbackEnd, end: true };  // BKPT / undefined
      }
      case 24: case 25: {
        const b = (i >>> 8) & 7, list = i & 0xFF;
        if (!list) return { code: fallbackEnd, end: true };
        const regs = []; for (let x = 0; x < 8; x++) if (list & (1 << x)) regs.push(x);
        const n = regs.length;
        if (i & 0x800) {       // LDMIA: if Rb in list, the loaded value wins (no writeback)
          let code = `{ const a = r[${b}]; if ((a & 0xFF000003) === 0 && ((a + ${4 * n - 1}) & 0xFF000000) === 0${this.mm2('a', 4 * n - 1)}) { const w = a >>> 2; `;
          code += `r[${b}] = (a + ${4 * n}) | 0; ` + regs.map((x, j) => `r[${x}] = m32[w + ${j}];`).join(' ');
          return { code: code + ` } else { ${fallback} } }` };
        }
        let code = `{ const a = r[${b}]; if ((a & 0xFF000003) === 0 && ((a + ${4 * n - 1}) & 0xFF000000) === 0 && pf[a >>> 7] === 0 && pf[(a + ${4 * n - 1}) >>> 7] === 0) { const w = a >>> 2; `;
        code += regs.map((x, j) => `m32[w + ${j}] = r[${x}];`).join(' ') + ` r[${b}] = (a + ${4 * n}) | 0;`;
        return { code: code + ` } else { ${fallback} } }` };
      }
      case 26: case 27: {
        const cond = (i >>> 8) & 15;
        if (cond >= 14) return { code: fallbackEnd, end: true };        // SWI / undefined
        const tgt = (pc + 4 + ((i << 24) >> 23)) | 0;
        return { code: `if (${COND[cond]}) { ${FL_STORE}return ${tgt}; }`, end: false, condBranch: true };
      }
      case 28: return { code: `${FL_STORE}return ${(pc + 4 + ((i << 21) >> 20)) | 0};`, end: true };
      case 29: case 31: return { code: fallbackEnd, end: true };          // lone BL/BLX suffix
      case 30: return { code: `r[14] = ${(pc + 4 + (((i & 0x7FF) << 21) >> 9)) | 0};` };
    }
    return { code: fallbackEnd, end: true };
  }

  thumbAlu(i) {
    const d = i & 7, s = (i >>> 3) & 7;
    const pre = `const a = r[${d}], b = r[${s}];`;
    const nz = `r[${d}] = res; nv = zv = res;`;
    switch ((i >>> 6) & 15) {
      case 0: return `{ ${pre} const res = a & b; ${nz} }`;
      case 1: return `{ ${pre} const res = a ^ b; ${nz} }`;
      case 2: return `{ ${pre} const s = b & 0xFF; let res = a; if (s) { if (s < 32) { res = a << s; cf = (a >>> (32 - s)) & 1; } else { res = 0; cf = s === 32 ? a & 1 : 0; } } ${nz} }`;
      case 3: return `{ ${pre} const s = b & 0xFF; let res = a; if (s) { if (s < 32) { res = (a >>> s) | 0; cf = (a >>> (s - 1)) & 1; } else { res = 0; cf = s === 32 ? a >>> 31 : 0; } } ${nz} }`;
      case 4: return `{ ${pre} const s = b & 0xFF; let res = a; if (s) { if (s < 32) { res = a >> s; cf = (a >>> (s - 1)) & 1; } else { res = a >> 31; cf = a >>> 31; } } ${nz} }`;
      case 5: return `{ ${pre} const res = (a + b + cf) | 0; cf = ((a >>> 0) + (b >>> 0) + cf) > 0xFFFFFFFF ? 1 : 0; vf = ((a ^ res) & (b ^ res)) >>> 31; ${nz} }`;
      case 6: return `{ ${pre} const res = (a - b - 1 + cf) | 0; cf = (a >>> 0) >= (b >>> 0) + 1 - cf ? 1 : 0; vf = ((a ^ b) & (a ^ res)) >>> 31; ${nz} }`;
      case 7: return `{ ${pre} const s = b & 0xFF; let res = a; if (s) { const s5 = s & 31; if (s5 === 0) cf = a >>> 31; else { res = (a >>> s5) | (a << (32 - s5)); cf = (a >>> (s5 - 1)) & 1; } } ${nz} }`;
      case 8: return `{ ${pre} const res = a & b; nv = zv = res; }`;
      case 9: return `{ const b = r[${s}]; const res = (0 - b) | 0; cf = b === 0 ? 1 : 0; vf = (b & res) >>> 31; ${nz} }`;
      case 10: return `{ ${pre} const res = (a - b) | 0; cf = (a >>> 0) >= (b >>> 0) ? 1 : 0; vf = ((a ^ b) & (a ^ res)) >>> 31; nv = zv = res; }`;
      case 11: return `{ ${pre} const res = (a + b) | 0; cf = (res >>> 0) < (a >>> 0) ? 1 : 0; vf = ((a ^ res) & (b ^ res)) >>> 31; nv = zv = res; }`;
      case 12: return `{ ${pre} const res = a | b; ${nz} }`;
      case 13: return `{ ${pre} const res = Math.imul(a, b); ${nz} }`;
      case 14: return `{ ${pre} const res = a & ~b; ${nz} }`;
      default: return `{ const res = ~r[${s}]; ${nz} }`;
    }
  }
}
