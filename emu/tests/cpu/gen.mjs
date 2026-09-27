// Differential CPU test generator.
//
// Produces an ARM assembly program made of many small test cases. Each case:
//   load flags + r0-r12,r14 from its 16-word record (r13 points at it),
//   execute ONE instruction under test (ARM or Thumb),
//   store r0-r12,r14 + CPSR (+ checksum of the scratch buffer) back into the record.
// At the end the whole record table is written out: on QEMU via semihosting
// to out.bin, on our emulator the runner reads it from memory.
//
// r13 is reserved (never an operand that is written). The scratch buffer
// lives at BUF (fixed physical address) so memory tests can point bases at it.

export const BUF = 0x00F00000, BUFSZ = 1024;

export function rng(seed) {
  let s = seed >>> 0 || 1;
  return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s; };
}

const SPECIAL = [0, 1, 2, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0x80000001, 0xFFFFFFFE, 0x00008000, 0x0000FFFF, 0xFFFF8000, 0x00007FFF, 0x40000000, 0xC0000000];
const SHIFTS = [0, 1, 2, 7, 8, 15, 16, 30, 31, 32, 33, 34, 63, 64, 127, 128, 255, 256, 257, 0x1F, 0x120, 0xFFFFFF20, 0xFFFFFFFF];

export function makeGen(seed) {
  const R = rng(seed);
  const rnd = (n) => R() % n;
  const val = () => {
    const k = rnd(10);
    if (k < 3) return SPECIAL[rnd(SPECIAL.length)];
    if (k < 5) return rnd(256);
    if (k < 6) return SHIFTS[rnd(SHIFTS.length)] >>> 0;
    if (k < 7) return (0xFFFFFFFF - rnd(256)) >>> 0;
    return R();
  };
  const reg = (excl = []) => { for (;;) { const x = rnd(16); if (x !== 13 && x !== 15 && !excl.includes(x)) return x; } };
  const regOrPc = (excl = []) => { for (;;) { const x = rnd(16); if (x !== 13 && !excl.includes(x)) return x; } };
  const lowreg = () => rnd(8);
  const cond = () => (rnd(4) === 0 ? rnd(15) : 14);
  return { R, rnd, val, reg, regOrPc, lowreg, cond };
}

// ---- test case builders: return { arm: word } or { thumb: [hw...] } plus register overrides
// SBZ fields: MOV/MVN Rn = 0, TST/TEQ/CMP/CMN Rd = 0 (QEMU UNDEFs otherwise)
function sbz(word) {
  const op = (word >>> 21) & 15;
  if (op === 13 || op === 15) word &= ~0x000F0000;
  if (op >= 8 && op <= 11) word &= ~0x0000F000;
  return word >>> 0;
}
export function genCase(cls, g) {
  const { rnd, reg, regOrPc, lowreg, cond, val } = g;
  const ov = {};              // register overrides: reg -> value (or {buf: offset})
  const w = (x) => x >>> 0;
  switch (cls) {
    case 'dp_imm': {
      const op = rnd(16); const S = op >= 8 && op <= 11 ? 1 : rnd(2);
      return { arm: sbz((cond() << 28) | (1 << 25) | (op << 21) | (S << 20) | (regOrPc() << 16) | (reg() << 12) | (rnd(16) << 8) | rnd(256)) };
    }
    case 'dp_regimm': {
      const op = rnd(16); const S = op >= 8 && op <= 11 ? 1 : rnd(2);
      return { arm: sbz((cond() << 28) | (op << 21) | (S << 20) | (regOrPc() << 16) | (reg() << 12) | (rnd(32) << 7) | (rnd(4) << 5) | regOrPc()) };
    }
    case 'dp_regreg': {
      const op = rnd(16); const S = op >= 8 && op <= 11 ? 1 : rnd(2);
      const rs = reg();
      ov[rs] = g.R() & 1 ? SHIFTS[rnd(SHIFTS.length)] : rnd(40);
      return { arm: sbz((cond() << 28) | (op << 21) | (S << 20) | (reg() << 16) | (reg() << 12) | (rs << 8) | (rnd(4) << 5) | 0x10 | reg([rs])), ov };
    }
    case 'mul': {
      const A = rnd(2), S = rnd(2);
      return { arm: w((cond() << 28) | (A << 21) | (S << 20) | (reg() << 16) | ((A ? reg() : 0) << 12) | (reg() << 8) | 0x90 | reg()) };
    }
    case 'mull': {
      const hi = reg(), lo = reg([hi]);
      return { arm: w((cond() << 28) | (1 << 23) | (rnd(4) << 21) | (rnd(2) << 20) | (hi << 16) | (lo << 12) | (reg() << 8) | 0x90 | reg()) };
    }
    case 'dsp': {           // v5TE: QADD etc, SMULxy family, CLZ
      const k = rnd(3);
      if (k === 0) return { arm: w((cond() << 28) | 0x01000050 | (rnd(4) << 21) | (reg() << 16) | (reg() << 12) | reg()) };
      if (k === 1) return { arm: w((cond() << 28) | 0x016F0F10 | (reg() << 12) | reg()) };
      const op = rnd(4);
      let x = rnd(2);
      if (op === 1 && !x) {} // SMLAW
      const hi = reg(); let lo = reg(op === 2 ? [hi] : []);
      if (op === 3 || (op === 1 && x)) lo = 0;
      return { arm: w((cond() << 28) | 0x01000080 | (op << 21) | (hi << 16) | (lo << 12) | (reg() << 8) | (rnd(2) << 6) | (x << 5) | reg()) };
    }
    case 'ldst': {          // LDR/STR/LDRB/STRB imm and reg offset
      const I = rnd(2), P = rnd(2), U = rnd(2), B = rnd(2), W = rnd(2), L = rnd(2);
      const rn = reg(), rd = (L && (!P || W)) ? reg([rn]) : ((!P || W) ? reg([rn]) : regOrPc([]));
      const al = B ? 1 : 4;
      ov[rn] = { buf: 256 + rnd(512 / al) * al };
      let off;
      if (I) { const rm = reg([rn]); const sh = rnd(3); ov[rm] = (rnd(64) * al) >>> sh; if (!B) ov[rm] &= ~((4 >> sh) - 1); off = (sh << 7) | rm; }
      else off = rnd(200 / al) * al;
      const rdx = (L && rd === 15) ? 0 : rd;
      return { arm: w((cond() << 28) | (1 << 26) | (I << 25) | (P << 24) | (U << 23) | (B << 22) | (W << 21) | (L << 20) | (rn << 16) | (rdx << 12) | off), ov, mem: 1 };
    }
    case 'ldsth': {         // halfword / signed / doubleword
      const P = rnd(2), U = rnd(2), I = rnd(2), W = P ? rnd(2) : 0, L = rnd(2);
      let sh = 1 + rnd(3);
      const rn = reg();
      let rd = reg([rn]);
      if (!L && sh !== 1) { rd = rd & ~1; if (rd === 12) rd = 10; if (rd === rn || rd + 1 === rn) rd = (rn + 2) & 0xE; if (rd >= 12) rd = 0; if (rd === rn || rd + 1 === rn) rd = 4; if (rd === rn || rd + 1 === rn) rd = 8; }
      const al = sh === 1 ? 2 : !L ? 8 : sh === 2 ? 1 : 2;
      ov[rn] = { buf: 256 + rnd(512 / al) * al };
      let lo;
      if (I) { const off = rnd(200 / al) * al; lo = ((off & 0xF0) << 4) | (off & 0xF); }
      else { const rm = reg([rn, rd, rd + 1]); ov[rm] = rnd(64 / al) * al; lo = rm; }
      return { arm: w((cond() << 28) | (P << 24) | (U << 23) | (I << 22) | (W << 21) | (L << 20) | (rn << 16) | (rd << 12) | lo | 0x90 | (sh << 5)), ov, mem: 1 };
    }
    case 'ldm': {
      const P = rnd(2), U = rnd(2), W = rnd(2), L = rnd(2);
      const rn = reg();
      let list = (g.R() & 0x5FFF) & ~(1 << 13);
      if (!list) list = 1;
      if (W && L && (list & (1 << rn))) list &= ~(1 << rn);
      if (!list) list = rn === 0 ? 2 : 1;
      ov[rn] = { buf: (256 + rnd(384)) & ~3 };
      return { arm: w((cond() << 28) | (4 << 25) | (P << 24) | (U << 23) | (W << 21) | (L << 20) | (rn << 16) | list), ov, mem: 1 };
    }
    case 'swp': {
      const rn = reg(), rd = reg([rn]), rm = reg([rn]);
      const B = rnd(2);
      ov[rn] = { buf: 256 + (B ? rnd(512) : rnd(128) * 4) };
      return { arm: w((cond() << 28) | 0x01000090 | (B << 22) | (rn << 16) | (rd << 12) | rm), ov, mem: 1 };
    }
    case 'psr': {
      const k = rnd(3);
      if (k === 0) return { arm: w((cond() << 28) | 0x010F0000 | (reg() << 12)) };        // MRS cpsr
      if (k === 1) return { arm: w((cond() << 28) | 0x0328F000 | (rnd(16) << 8) | rnd(256)) }; // MSR cpsr_f, #imm
      return { arm: w((cond() << 28) | 0x0128F000 | reg()) };                            // MSR cpsr_f, rm
    }
    // ---------------- Thumb
    case 't_shift': return { thumb: [(rnd(3) << 11) | (rnd(32) << 6) | (lowreg() << 3) | lowreg()] };
    case 't_addsub': return { thumb: [0x1800 | (rnd(4) << 9) | (rnd(8) << 6) | (lowreg() << 3) | lowreg()] };
    case 't_imm': return { thumb: [0x2000 | (rnd(4) << 11) | (lowreg() << 8) | rnd(256)] };
    case 't_alu': {
      const rs = lowreg();
      if (rnd(2)) ov[rs] = SHIFTS[rnd(SHIFTS.length)] >>> 0;
      return { thumb: [0x4000 | (rnd(16) << 6) | (rs << 3) | lowreg()], ov };
    }
    case 't_hi': {
      const op = rnd(3);
      let rd = reg(), rm = regOrPc();
      if (op === 1 && rm === 15) rm = 14;
      return { thumb: [0x4400 | (op << 8) | ((rd & 8) << 4) | (rm << 3) | (rd & 7)] };
    }
    case 't_ldpc': return { thumb: [0x4800 | (lowreg() << 8) | rnd(256)], mem: 1 };
    case 't_ldreg': {
      const rb = lowreg(); let ro = lowreg(); while (ro === rb) ro = lowreg(); let rd = lowreg();
      const op = rnd(8), al = [4, 2, 1, 1, 4, 2, 1, 2][op];
      if (ro === rb) ov[rb] = { buf: 256 + rnd(64) * 4 }; else { ov[rb] = { buf: 256 + rnd(256 / al) * al }; ov[ro] = rnd(128 / al) * al; }
      return { thumb: [0x5000 | (op << 9) | (ro << 6) | (rb << 3) | rd], ov, mem: 1 };
    }
    case 't_ldimm': {
      const rb = lowreg();
      const k = rnd(6), al = [4, 4, 1, 1, 2, 2][k];
      ov[rb] = { buf: 256 + rnd(256 / al) * al };
      const top = [0x6000, 0x6800, 0x7000, 0x7800, 0x8000, 0x8800][k];
      return { thumb: [top | (rnd(32) << 6) | (rb << 3) | lowreg()], ov, mem: 1 };
    }
    case 't_addpcsp': return { thumb: [0xA000 | (rnd(2) << 11) | (lowreg() << 8) | rnd(256)] };
    case 't_ldm': {
      const rb = lowreg(); const L = rnd(2);
      let list = rnd(255) + 1;
      if (L && (list & (1 << rb)) && rnd(2)) list &= ~(1 << rb) || 1;
      if (!list) list = 1 << ((rb + 1) & 7);
      ov[rb] = { buf: 256 + (rnd(128) & ~3) };
      return { thumb: [0xC000 | (L << 11) | (rb << 8) | list], ov, mem: 1 };
    }
    case 't_bcond': {       // conditional branch forward by 0 (skips nothing) or over one nop
      const c = rnd(14);
      return { thumb: [0xD000 | (c << 8) | 0x00, 0x2100 | rnd(256)] };   // b<c> +4 over "movs r1,#x"
    }
  }
  throw new Error('unknown class ' + cls);
}

export const ARM_CLASSES = ['dp_imm', 'dp_regimm', 'dp_regreg', 'mul', 'mull', 'dsp', 'ldst', 'ldsth', 'ldm', 'swp', 'psr'];
export const THUMB_CLASSES = ['t_shift', 't_addsub', 't_imm', 't_alu', 't_hi', 't_ldpc', 't_ldreg', 't_ldimm', 't_addpcsp', 't_ldm', 't_bcond'];

// Build assembly text + the list of cases (for reporting).
export function buildProgram(classes, perClass, seed) {
  const g = makeGen(seed);
  const cases = [];
  for (const cls of classes) for (let k = 0; k < perClass; k++) {
    const c = genCase(cls, g);
    const regs = [];
    for (let r = 0; r < 15; r++) regs.push(g.val());
    for (const [r, v] of Object.entries(c.ov || {})) regs[+r] = typeof v === 'object' ? (BUF + v.buf) >>> 0 : v >>> 0;
    const flags = (g.R() & 0xF8000000) >>> 0;
    cases.push({ cls, ...c, regs, flags });
  }
  const L = [];
  L.push('.syntax unified', '.arm', '.text', '.global _start', '_start:',
    '  mov r11, r1',                 // board id: 0x183 = QEMU versatilepb
    '  ldr r0, =board', '  str r11, [r0]',
    // init buffer with LCG
    `  ldr r0, =${BUF}`, `  ldr r1, =${BUFSZ / 4}`, '  ldr r2, =0x12345678', '  ldr r3, =1103515245',
    '1: mla r2, r3, r2, r3', '  add r2, r2, #12288', '  add r2, r2, #57', '  str r2, [r0], #4', '  subs r1, r1, #1', '  bne 1b',
    // exception vectors at 0 (V=0 on both machines), each records and bails
    '  mrc p15, 0, r0, c1, c0, 0', '  bic r0, r0, #0x2000', '  mcr p15, 0, r0, c1, c0, 0',
    '  mov r0, #0', '  adr r1, vecs', '  mov r2, #16',
    '6: ldr r3, [r1], #4', '  str r3, [r0], #4', '  subs r2, r2, #1', '  bne 6b',
    '  mrs r0, cpsr', '  bic r0, r0, #0x100', '  msr cpsr_cx, r0',
    '  ldr r13, =table', '  b tests',
    'vecs:', ...[0,1,2,3,4,5,6,7].map((k) => '  ldr pc, [pc, #0x18]'), ...[0,1,2,3,4,5,6,7].map((k) => `  .word exc${k}`),
    ...[0,1,2,3,4,5,6,7].map((k) => `exc${k}: mov r0, #${k}\n  b excc`),
    'excc:', '  ldr r1, =excinfo', '  str r0, [r1]', '  str lr, [r1, #4]', '  msr cpsr_c, #0xd3', '  str r13, [r1, #8]', '  b done',
    '.ltorg',
    'cksum:', `  ldr r1, =${BUF}`, '  mov r0, #0', `  mov r2, #${BUFSZ / 4}`,
    '2: ldr r3, [r1], #4', '  add r0, r3, r0, ror #3', '  subs r2, r2, #1', '  bne 2b', '  str r0, [r13, #60]', '  bx lr', '.ltorg',
    'tests:');
  cases.forEach((c, idx) => {
    L.push(`@ case ${idx} ${c.cls}`);
    L.push('  ldr r0, [r13, #56]', '  msr cpsr_f, r0');
    if (c.arm !== undefined) {
      L.push('  ldmia r13, {r0-r12, r14}');
      L.push(`  .word 0x${c.arm.toString(16)}`);
      L.push('  stmia r13, {r0-r12, r14}');
    } else {
      L.push('  ldmia r13, {r0-r12}');
      L.push(`  blx 3f`, '.thumb', '.p2align 2', '3:');
      if (c.thumb.length === 1) L.push('  nop'); // keep alignment: nop(2) + insn(2) => bx pc at 4-aligned
      for (const h of c.thumb) L.push(`  .short 0x${h.toString(16)}`);
      if ((c.thumb.length + (c.thumb.length === 1 ? 1 : 0)) % 2) L.push('  nop');
      L.push('  bx pc', '  nop', '.arm');
      L.push('  stmia r13, {r0-r12, r14}');
    }
    L.push('  mrs r0, cpsr', '  str r0, [r13, #56]');
    if (c.mem) L.push('  bl cksum'); else L.push('  mov r0, #0', '  str r0, [r13, #60]');
    L.push('  add r13, r13, #64');
    if (idx % 64 === 63) L.push('  b 4f', '.ltorg', '4:');
  });
  L.push('done:',
    '  ldr r0, =board', '  ldr r0, [r0]', '  ldr r1, =0x183', '  cmp r0, r1', '  bne ours',
    // QEMU: semihosting write table to out.bin
    '  ldr r1, =blk', '  ldr r0, =fname', '  str r0, [r1]', '  mov r0, #5', '  str r0, [r1, #4]', '  mov r0, #7', '  str r0, [r1, #8]',
    '  mov r0, #1', '  svc 0x123456', '  mov r5, r0',
    '  ldr r1, =blk', '  str r5, [r1]', '  ldr r0, =excinfo', '  str r0, [r1, #4]', `  ldr r0, =${cases.length * 64 + 16}`, '  str r0, [r1, #8]',
    '  mov r0, #5', '  svc 0x123456',
    '  ldr r1, =blk', '  str r5, [r1]', '  mov r0, #2', '  svc 0x123456',
    '  mov r0, #0x18', '  ldr r1, =0x20026', '  svc 0x123456',
    'ours:', '  ldr r0, =0x100000F4', '  mov r1, #0', '  strb r1, [r0]', '5: b 5b', '.ltorg',
    '.data', '.p2align 4', 'board: .word 0', 'blk: .word 0,0,0,0', 'fname: .asciz "out.bin"', '.p2align 4', 'excinfo: .word 0xffffffff,0,0,0', 'table:');
  for (const c of cases) {
    const rec = [...c.regs.slice(0, 13), c.regs[14], c.flags, 0];
    L.push('  .word ' + rec.map((x) => '0x' + (x >>> 0).toString(16)).join(','));
  }
  return { asm: L.join('\n') + '\n', cases };
}
