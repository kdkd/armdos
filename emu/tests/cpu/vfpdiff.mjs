#!/usr/bin/env node
// VFPv2 differential test: random CP10/CP11 instructions on our CPU vs
// qemu-system-arm -M versatilepb -cpu arm926 (which has a VFP9-S / VFPv2).
// Each case loads r0-r12/lr, CPSR flags, FPSCR and all of D0-D15, executes one
// instruction, and stores everything back (plus a scratch-buffer checksum and
// a count of undefined-instruction traps).
//
// usage: node emu/tests/cpu/vfpdiff.mjs [--seed N] [--per N] [--classes a,b] [--jit] [--exact] [--keep] [--show N]
//   --exact  enable cpu.vfpExactFlags and compare IXC/UFC too (otherwise masked)
import { execFileSync } from 'node:child_process';
import { mkdirSync, writeFileSync, readFileSync, rmSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { rng, BUF, BUFSZ } from './gen.mjs';
import { CPU } from '../../cpu.mjs';

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf('--' + k); return i >= 0 ? args[i + 1] : d; };
const seed = +opt('seed', 1), per = +opt('per', 300);
const CLASSES = ['v_dp_s', 'v_dp_d', 'v_ext_s', 'v_ext_d', 'v_cvt', 'v_cmp', 'v_ls', 'v_ldm', 'v_xfer', 'v_vec', 'v_und'];
const classes = opt('classes', CLASSES.join(',')).split(',');
const useJit = args.includes('--jit'), exact = args.includes('--exact');
const work = join(process.env.EMU_TEST_TMP || join(tmpdir(), 'armdos-vfpdiff'), `v${seed}`);
mkdirSync(work, { recursive: true });

// ---------------------------------------------------------------- generator
const R = rng(seed * 7919 + 13);
const rnd = (n) => R() % n;
const F_SPECIAL = [0, 0x80000000, 0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 0x7FC12345, 0x7F812345, 0xFF800001,
  0x00000001, 0x807FFFFF, 0x00800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x3F800000, 0xBF800000, 0x40000000, 0x3F000000, 0x4F000000, 0xCF000000,
  0x4F800000, 0x5F000000, 0x3EFFFFFF, 0x3FC00000, 0x40200000, 0xC0200000, 0x00FFFFFF, 0x0DA24260];
const D_SPECIAL = [[0, 0], [0x80000000, 0], [0x7FF00000, 0], [0xFFF00000, 0], [0x7FF80000, 0], [0xFFF80000, 0], [0x7FF81234, 0x56789ABC],
  [0x7FF01234, 0x1], [0x00000000, 1], [0x800FFFFF, 0xFFFFFFFF], [0x00100000, 0], [0x7FEFFFFF, 0xFFFFFFFF], [0x3FF00000, 0], [0xBFF00000, 0],
  [0x41E00000, 0], [0xC1E00000, 0], [0x41F00000, 0], [0x3FE00000, 0], [0x3FF80000, 0], [0x400C0000, 0], [0x47EFFFFF, 0xE0000000],
  [0x47EFFFFF, 0xF0000000], [0x36A00000, 0], [0x380FFFFF, 0xF0000000], [0x3E700000, 0], [0x41DFFFFF, 0xFFC00000]];
function randFloatBits() {
  const k = rnd(10);
  if (k < 3) return F_SPECIAL[rnd(F_SPECIAL.length)] >>> 0;
  if (k < 8) { const e = 127 - 30 + rnd(60); return (((R() & 1) << 31) | (e << 23) | (R() & 0x7FFFFF)) >>> 0; }
  if (k < 9) { const e = 127 - 2 + rnd(5); return (((R() & 1) << 31) | (e << 23) | (R() & 0x7FF000)) >>> 0; }  // short mantissas (exact results)
  return R();
}
function randDoubleBits() {
  const k = rnd(10);
  if (k < 3) { const [h, l] = D_SPECIAL[rnd(D_SPECIAL.length)]; return [l >>> 0, h >>> 0]; }
  if (k < 8) { const e = 1023 - 60 + rnd(120); return [R(), (((R() & 1) << 31) | (e << 20) | (R() & 0xFFFFF)) >>> 0]; }
  if (k < 9) { const e = 1023 - 2 + rnd(5); return [0, (((R() & 1) << 31) | (e << 20) | (R() & 0xFF000)) >>> 0]; }
  if (k < 10 && rnd(2)) { const e = rnd(2) ? 1 + rnd(40) : 2046 - rnd(40); return [R(), (((R() & 1) << 31) | (e << 20) | (R() & 0xFFFFF)) >>> 0]; }
  return [R(), R()];
}
function vfpWords() {
  const w = [];
  for (let d = 0; d < 16; d++) {
    if (rnd(2)) { const [lo, hi] = randDoubleBits(); w.push(lo, hi); }
    else w.push(randFloatBits(), randFloatBits());
  }
  return w;
}
// QEMU steps single-precision vectors by 4 for STRIDE=0b11 (the architecture says 2),
// so single-precision vector cases use stride 1 only.
function randFpscr(vec, vecDp) {
  let f = (R() & 0xF000009F) >>> 0;                 // NZCV + cumulative flags
  if (rnd(3) === 0) f |= rnd(4) << 22;              // rounding mode
  if (rnd(5) === 0) f |= 1 << 24;                   // FZ
  if (rnd(5) === 0) f |= 1 << 25;                   // DN
  if (vec) { f |= (1 + rnd(3)) << 16; if (vecDp && rnd(2)) f |= 3 << 20; }
  return f >>> 0;
}
const cond = () => (rnd(5) === 0 ? rnd(15) : 14);
const reg = (ex = []) => { for (;;) { const x = rnd(15); if (x !== 13 && !ex.includes(x)) return x; } };
const sreg = () => rnd(32), dreg = () => rnd(16);
const sEnc = (s, lo, bit) => (((s >>> 1) << lo) | ((s & 1) << bit));    // Sx field split
const encSd = (s) => sEnc(s, 12, 22), encSn = (s) => sEnc(s, 16, 7), encSm = (s) => sEnc(s, 0, 5);

function genCase(cls) {
  const ov = {}; let mem = 0;
  let w;
  switch (cls) {
    case 'v_dp_s': case 'v_dp_d': case 'v_vec': {
      const dp = cls === 'v_dp_d' || (cls === 'v_vec' && rnd(2));
      let opc = rnd(9);
      // (unary vector ops FCPY/FABS/FNEG/FSQRT are not compared: QEMU stores their
      //  second and later elements to the advanced *source* register)
      const p = (opc >> 3) & 1, q = (opc >> 2) & 1, rr = (opc >> 1) & 1, s = opc & 1;
      w = (cond() << 28) | 0x0E000A00 | (p << 23) | (q << 21) | (rr << 20) | (s << 6) | (dp ? 0x100 : 0);
      if (opc === 15) { const ext = rnd(4); w |= (0xB << 20) | (s << 6) | (dp ? (ext >> 1) << 16 | (ext & 1) << 7 : encSn(ext)) | (1 << 23) | (1 << 21) | (1 << 20) | (1 << 6); }
      if (cls === 'v_vec') {
        // non-overlapping banks: d in bank 1-3, n in another, m scalar (bank 0) or a third bank
        const banks = [1, 2, 3].sort(() => rnd(3) - 1);
        const bs = dp ? 4 : 8, pick = (b) => b * bs + rnd(bs);
        const d = pick(banks[0]), n = pick(banks[1]), m = rnd(2) ? rnd(bs) : pick(banks[2]);
        if (dp) w |= (d << 12) | (opc === 15 ? 0 : n << 16) | m;
        else w |= encSd(d) | (opc === 15 ? 0 : encSn(n)) | encSm(m);
        return { arm: w >>> 0, vec: true, vecDp: dp };
      }
      if (dp) w |= (dreg() << 12) | (opc === 15 ? 0 : dreg() << 16) | dreg();
      else w |= encSd(sreg()) | (opc === 15 ? 0 : encSn(sreg())) | encSm(sreg());
      return { arm: w >>> 0 };
    }
    case 'v_ext_s': case 'v_ext_d': {
      const dp = cls === 'v_ext_d';
      const ext = [0, 1, 2, 3][rnd(4)];
      w = (cond() << 28) | 0x0EB00A40 | (dp ? 0x100 : 0) | ((ext >> 1) << 16) | ((ext & 1) << 7);
      if (dp) w |= (dreg() << 12) | dreg(); else w |= encSd(sreg()) | encSm(sreg());
      return { arm: w >>> 0 };
    }
    case 'v_cmp': {
      const dp = rnd(2), ext = 8 + rnd(4);
      w = (cond() << 28) | 0x0EB00A40 | (dp ? 0x100 : 0) | ((ext >> 1) << 16) | ((ext & 1) << 7);
      const z = ext & 2;
      if (dp) w |= (dreg() << 12) | (z ? 0 : dreg()); else w |= encSd(sreg()) | (z ? 0 : encSm(sreg()));
      if (rnd(3) === 0) { const f = (cond() << 28) | 0x0EF1FA10; return { arm: [w >>> 0, f >>> 0] }; }   // + FMSTAT
      return { arm: w >>> 0 };
    }
    case 'v_cvt': {
      const dp = rnd(2), ext = [15, 16, 17, 24, 25, 26, 27][rnd(7)];
      w = (cond() << 28) | 0x0EB00A40 | (dp ? 0x100 : 0) | ((ext >> 1) << 16) | ((ext & 1) << 7);
      const dDouble = (ext === 15 && !dp) || ((ext === 16 || ext === 17) && dp);
      const mDouble = (ext === 15 && dp) || (ext >= 24 && dp);
      w |= dDouble ? dreg() << 12 : encSd(sreg());
      w |= mDouble ? dreg() : encSm(sreg());
      return { arm: w >>> 0 };
    }
    case 'v_ls': {
      const dp = rnd(2), U = rnd(2);
      const rn = rnd(8) === 0 ? 15 : reg();
      const L = rn === 15 ? 1 : rnd(2);             // pc-relative: loads only
      const imm = rnd(64);
      if (rn !== 15) ov[rn] = { buf: 512 + (rnd(64) * 4) - (U ? imm * 4 : -imm * 4) };
      w = (cond() << 28) | 0x0D000A00 | (U << 23) | (L << 20) | (rn << 16) | imm | (dp ? 0x100 : 0);
      w |= dp ? dreg() << 12 : encSd(sreg());
      mem = 1;
      return { arm: w >>> 0, ov, mem, pcrel: rn === 15 };
    }
    case 'v_ldm': {
      const dp = rnd(2), L = rnd(2), mode = rnd(3);   // 0 IA, 1 IA!, 2 DB!
      const P = mode === 2 ? 1 : 0, U = mode === 2 ? 0 : 1, Wb = mode === 0 ? 0 : 1;
      const rn = reg();
      let first, imm;
      if (dp) { first = dreg(); const n = 1 + rnd(16 - first); imm = 2 * n + (rnd(4) === 0 ? 1 : 0); }
      else { first = sreg(); const n = 1 + rnd(32 - first); imm = n; }
      ov[rn] = { buf: 256 + rnd(64) * 4 + (mode === 2 ? imm * 4 : 0) };
      w = (cond() << 28) | 0x0C000A00 | (P << 24) | (U << 23) | (Wb << 21) | (L << 20) | (rn << 16) | imm | (dp ? 0x100 : 0);
      w |= dp ? first << 12 : encSd(first);
      return { arm: w >>> 0, ov, mem: 1 };
    }
    case 'v_xfer': {
      const k = rnd(6), L = rnd(2), rd = reg();
      if (k === 0) w = (cond() << 28) | 0x0E000A10 | (L << 20) | (rd << 12) | encSn(sreg());                       // FMSR/FMRS
      else if (k === 1) w = (cond() << 28) | 0x0E000B10 | (rnd(2) << 21) | (L << 20) | (rd << 12) | (dreg() << 16); // FMDLR/FMDHR/FMRDL/FMRDH
      else if (k === 2) { const rn = reg([rd]); w = (cond() << 28) | 0x0C400B10 | (L << 20) | (rn << 16) | (rd << 12) | dreg(); }       // FMDRR/FMRRD
      else if (k === 3) { const rn = reg([rd]); const s = rnd(31); w = (cond() << 28) | 0x0C400A10 | (L << 20) | (rn << 16) | (rd << 12) | encSm(s); } // FMSRR/FMRRS
      else if (k === 4) w = (cond() << 28) | 0x0EF10A10 | (rd << 12);                                               // FMRX rd, FPSCR
      else { w = (cond() << 28) | 0x0EF1FA10; }                                                                     // FMSTAT
      return { arm: w >>> 0 };
    }
    case 'v_und': {   // encodings that must be UNDEFINED on VFPv2
      const k = rnd(5);
      if (k === 0) w = 0xEE000B00 | (1 << 22) | (1 << 20) | 0x30000;            // double op with D bit set
      else if (k === 1) w = 0xEE800A00 | (1 << 20) | (1 << 6) | (dreg() << 12); // opc 1011 (pqrs = 1 0 1 1)
      else if (k === 2) w = 0xEEB00A40 | (0x2 << 16) | (1 << 7);                 // ext 5: undefined
      else if (k === 3) w = 0xEEF20A10 | (reg() << 12);                          // FMRX of an unknown register
      else w = 0xEEB10A40 | 0x100 | (1 << 5);                                     // FABSD with M bit
      return { arm: (w | (rnd(2) << 12)) >>> 0 };
    }
  }
  throw new Error(cls);
}

const cases = [];
for (const cls of classes) for (let k = 0; k < per; k++) {
  const c = genCase(cls);
  const regs = []; for (let r = 0; r < 15; r++) regs.push(R());
  for (const [r, v] of Object.entries(c.ov || {})) regs[+r] = (BUF + v.buf) >>> 0;
  cases.push({ cls, ...c, regs, flags: (R() & 0xF8000000) >>> 0, vfp: vfpWords(), fpscr: randFpscr(c.vec, c.vecDp) });
}

// ---------------------------------------------------------------- program
const L = ['.syntax unified', '.arch armv5te', '.fpu vfp', '.arm', '.text', '.global _start', '_start:',
  '  mov r11, r1', '  ldr r0, =board', '  str r11, [r0]',
  '  mrc p15, 0, r0, c1, c0, 0', '  bic r0, r0, #0x2000', '  mcr p15, 0, r0, c1, c0, 0',
  '  mov r0, #0', '  adr r1, vecs', '  mov r2, #16', '6: ldr r3, [r1], #4', '  str r3, [r0], #4', '  subs r2, r2, #1', '  bne 6b',
  '  mrs r0, cpsr', '  bic r0, r0, #0x100', '  msr cpsr_cx, r0',
  '  mov r0, #0x40000000', '  vmsr fpexc, r0',
  `  ldr r0, =${BUF}`, `  ldr r1, =${BUFSZ / 4}`, '  ldr r2, =0x12345678', '  ldr r3, =1103515245',
  '1: mla r2, r3, r2, r3', '  add r2, r2, #12288', '  add r2, r2, #57', '  str r2, [r0], #4', '  subs r1, r1, #1', '  bne 1b',
  '  ldr r13, =table', '  b tests',
  'vecs:', ...[0, 1, 2, 3, 4, 5, 6, 7].map(() => '  ldr pc, [pc, #0x18]'), ...[0, 1, 2, 3, 4, 5, 6, 7].map((k) => `  .word exc${k}`),
  'exc1: ldr r13, =undcount', '  str r0, [r13, #4]', '  ldr r0, [r13]', '  add r0, r0, #1', '  str r0, [r13]', '  ldr r0, [r13, #4]', '  movs pc, lr',
  ...[0, 2, 3, 4, 5, 6, 7].map((k) => `exc${k}: mov r0, #${k}\n  b excc`),
  'excc:', '  ldr r1, =excinfo', '  str r0, [r1]', '  str lr, [r1, #4]', '  msr cpsr_c, #0xd3', '  str r13, [r1, #8]', '  b done',
  '.ltorg',
  'cksum:', `  ldr r1, =${BUF}`, '  mov r0, #0', `  mov r2, #${BUFSZ / 4}`,
  '2: ldr r3, [r1], #4', '  add r0, r3, r0, ror #3', '  subs r2, r2, #1', '  bne 2b', '  str r0, [r13, #60]', '  bx lr', '.ltorg',
  'tests:'];
cases.forEach((c, idx) => {
  L.push(`@ case ${idx} ${c.cls}`);
  L.push('  ldr r0, [r13, #56]', '  msr cpsr_f, r0', '  ldr r0, [r13, #252]', '  vmsr fpscr, r0', '  add r0, r13, #64', '  vldmia r0, {d0-d15}',
    '  ldr r0, =undcount', '  mov r1, #0', '  str r1, [r0]', '  ldmia r13, {r0-r12, r14}');
  for (const w of [].concat(c.arm)) L.push(`  .word 0x${w.toString(16)}`);
  L.push('  stmia r13, {r0-r12, r14}', '  mrs r0, cpsr', '  str r0, [r13, #56]', '  vmrs r0, fpscr', '  str r0, [r13, #252]',
    '  mov r1, #0', '  vmsr fpscr, r1', '  add r0, r13, #64', '  vstmia r0, {d0-d15}', '  ldr r0, =undcount', '  ldr r0, [r0]', '  str r0, [r13, #248]');
  if (c.mem) L.push('  bl cksum'); else L.push('  mov r0, #0', '  str r0, [r13, #60]');
  L.push('  add r13, r13, #256');
  if (idx % 32 === 31) L.push('  b 4f', '.ltorg', '4:');
});
L.push('done:', '  ldr r0, =board', '  ldr r0, [r0]', '  ldr r1, =0x183', '  cmp r0, r1', '  bne ours',
  '  ldr r1, =blk', '  ldr r0, =fname', '  str r0, [r1]', '  mov r0, #5', '  str r0, [r1, #4]', '  mov r0, #7', '  str r0, [r1, #8]',
  '  mov r0, #1', '  svc 0x123456', '  mov r5, r0',
  '  ldr r1, =blk', '  str r5, [r1]', '  ldr r0, =excinfo', '  str r0, [r1, #4]', `  ldr r0, =${cases.length * 256 + 16}`, '  str r0, [r1, #8]',
  '  mov r0, #5', '  svc 0x123456', '  ldr r1, =blk', '  str r5, [r1]', '  mov r0, #2', '  svc 0x123456',
  '  mov r0, #0x18', '  ldr r1, =0x20026', '  svc 0x123456',
  'ours:', '  ldr r0, =0x100000F4', '  mov r1, #0', '  strb r1, [r0]', '5: b 5b', '.ltorg',
  '.data', '.p2align 4', 'board: .word 0', 'blk: .word 0,0,0,0', 'fname: .asciz "out.bin"', '.p2align 2', 'undcount: .word 0, 0',
  '.p2align 4', 'excinfo: .word 0xffffffff,0,0,0', 'table:');
for (const c of cases) {
  const rec = [...c.regs.slice(0, 13), c.regs[14], c.flags, 0, ...c.vfp, ...new Array(14).fill(0), 0, c.fpscr];
  L.push('  .word ' + rec.map((x) => '0x' + (x >>> 0).toString(16)).join(','));
}
writeFileSync(join(work, 't.s'), L.join('\n') + '\n');
const sh = (cmd, a) => { try { return execFileSync(cmd, a, { cwd: work, stdio: ['ignore', 'pipe', 'pipe'] }).toString(); } catch (e) { console.error(e.stderr?.toString()); process.exit(3); } };
sh('arm-none-eabi-as', ['-march=armv5te', '-mfpu=vfp', 't.s', '-o', 't.o']);
sh('arm-none-eabi-ld', ['-Ttext=0x10000', 't.o', '-o', 't.elf']);
sh('arm-none-eabi-objcopy', ['-O', 'binary', 't.elf', 't.bin']);
const nm = sh('arm-none-eabi-nm', ['t.elf']).split('\n');
const sym = (n) => parseInt(nm.find((l) => l.endsWith(' ' + n)).split(' ')[0], 16);
const table = sym('table');
const bin = readFileSync(join(work, 't.bin'));

// ---- QEMU
if (existsSync(join(work, 'out.bin'))) rmSync(join(work, 'out.bin'));
execFileSync('qemu-system-arm', ['-M', 'versatilepb', '-cpu', 'arm926', '-m', '128M', '-nographic', '-monitor', 'none', '-serial', 'null',
  '-semihosting-config', 'enable=on,target=native', '-kernel', 't.bin'], { cwd: work, timeout: 120000, stdio: 'ignore' });
const qall = new Int32Array(new Uint8Array(readFileSync(join(work, 'out.bin'))).buffer);
if (qall[0] !== -1) console.log(`QEMU took exception vector ${qall[0]} lr=${(qall[1] >>> 0).toString(16)} case=${(qall[2] - table) / 256}`);
const q = qall.subarray(4);

// ---- ours
let done = false;
const cpu = new CPU({ read: () => -1, write: (a) => { if ((a >>> 0) === 0x100000F4) { done = true; cpu.requestStop(); cpu.halted = 1; return true; } return false; } });
cpu.setVfpExactFlags(exact);
if (useJit) { const { JIT } = await import('../../jit.mjs'); cpu.jit = new JIT(cpu, { threshold: +(process.env.JIT_THRESHOLD || 1) }); }
cpu.m8.set(bin, 0x10000); cpu.r[1] = 0; cpu.pc = 0x10000;
const t0 = performance.now();
for (let n = 0; !done && n < 2e8;) { const k = cpu.run(1e6); n += k; if (!k) break; }
if (!done) { console.log('ours did not finish, pc=' + (cpu.pc >>> 0).toString(16), cpu.lastFault); process.exit(2); }
{ const e = new Int32Array(cpu.buf, sym('excinfo'), 3); if (e[0] !== -1) console.log(`OURS took exception vector ${e[0]} lr=${(e[1] >>> 0).toString(16)} case=${(e[2] - table) / 256}`); }
const o = new Int32Array(cpu.buf, table, cases.length * 64);

const hex = (x) => (x >>> 0).toString(16).padStart(8, '0');
const name = (j) => j < 13 ? 'r' + j : j === 13 ? 'lr' : j === 14 ? 'cpsr' : j === 15 ? 'bufsum' : j < 48 ? `s${j - 16}` : j === 62 ? 'undcount' : j === 63 ? 'fpscr' : 'w' + j;
const fmask = exact ? 0xFFFFFFFF : ~0x18;            // IXC, UFC
const fails = {}; let nfail = 0;
for (let k = 0; k < cases.length; k++) {
  const bad = [];
  for (let j = 0; j < 64; j++) {
    let a = q[k * 64 + j], b = o[k * 64 + j];
    if (j === 63) { a &= fmask; b &= fmask; }
    if (a !== b) bad.push(j);
  }
  if (!bad.length) continue;
  const c = cases[k]; nfail++; fails[c.cls] = (fails[c.cls] || 0) + 1;
  if (fails[c.cls] > +opt('show', 3)) continue;
  console.log(`FAIL #${k} ${c.cls} ${[].concat(c.arm).map(hex).join(' ')}  fpscr_in=${hex(c.fpscr)}`);
  for (const j of bad) {
    const extra = j >= 16 && j < 48 ? ` (in ${hex(c.vfp[j - 16])})` : '';
    console.log(`   ${name(j)}: qemu=${hex(q[k * 64 + j])} ours=${hex(o[k * 64 + j])}${extra}`);
  }
}
console.log(`vfp seed ${seed}: ${cases.length} cases, ${nfail} mismatches [${classes.map((c) => `${c}:${fails[c] || 0}`).join(' ')}] (${(performance.now() - t0).toFixed(0)} ms${useJit ? ', jit' : ''}${exact ? ', exact flags' : ''})`);
if (!args.includes('--keep')) rmSync(work, { recursive: true, force: true });
process.exit(nfail ? 1 : 0);
