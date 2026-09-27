#!/usr/bin/env node
// apps/debug/tests/asmtest.mjs - differential tests of DEBUG's disassembler and
// assembler (apps/debug/disasm.c, asm.c), built for the host (tests/hostasm.c):
//
//   1. disassembler vs emu/disasm.mjs (exact text) and vs arm-none-eabi-objdump,
//      random + per-class ARM encodings and Thumb halfwords;
//   2. round trip: every ARM disassembly (bar .word / UNPREDICTABLE / illegal
//      shifter operand) assembles back to a word that disassembles to the same text
//      (and, where no should-be-zero field was set, to the same word);
//   3. assembler vs arm-none-eabi-as on the same texts (branches excluded: their
//      absolute targets need relocation in an object file) and on hand-written
//      lines in DEBUG style (divided syntax, 2-operand forms, 41H numbers, ...).
//
//   node apps/debug/tests/asmtest.mjs [--n=COUNT] [--seed=N] [-v]
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { disasmArm, disasmThumb } from '../../../emu/disasm.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const verbose = args.includes('-v');
const N = +(args.find((a) => a.startsWith('--n='))?.slice(4) ?? 2000);
let seed = +(args.find((a) => a.startsWith('--seed='))?.slice(7) ?? 4242);
const tmp = path.join(HERE, '../../../build/debug-test/asm');
fs.mkdirSync(tmp, { recursive: true });
const HOST = path.join(tmp, 'hostasm');
execFileSync('gcc', ['-O1', '-o', HOST, path.join(HERE, 'hostasm.c'), path.join(HERE, '../disasm.c'), path.join(HERE, '../asm.c')]);

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; };

function rnd() { seed = (seed + 0x6d2b79f5) | 0; let t = seed; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return (t ^ (t >>> 14)) >>> 0; }
const pick = (a) => a[rnd() % a.length];
const COND = () => rnd() % 15;
const cpOk = (w) => { const cp = (w >>> 8) & 15; return cp === 10 || cp === 11 ? ((w & ~0xf00) | (pick([0, 1, 5, 7, 8, 9, 12, 13, 14, 15]) << 8)) >>> 0 : w; };
function gen(value, mask, opts = {}) {
  let w = ((rnd() & ~mask) | value) >>> 0;
  if (!(mask & 0xf0000000)) w = ((w & 0x0fffffff) | (COND() << 28)) >>> 0;
  if (opts.cp) w = cpOk(w);
  if (opts.fix) w = opts.fix(w) >>> 0;
  return w;
}
const armClasses = {
  dp_imm: () => gen(0x02000000, 0x0e000000),
  dp_regimm: () => gen(0x00000000, 0x0e000010),
  dp_regreg: () => gen(0x00000010, 0x0e000090),
  mov_shift: () => gen(0x01a00000, 0x0def0000),
  mul_mla: () => gen(0x00000090, 0x0fc000f0),
  mull_mlal: () => gen(0x00800090, 0x0f8000f0),
  swp: () => gen(0x01000090, 0x0fb00ff0),
  ldrh_strh_ldrs: () => gen(pick([0xb0, 0xd0, 0xf0]) | 0x00100000 * (rnd() & 1), 0x0e1000f0, { fix: (w) => rnd() & 1 ? w : w & ~0xf00 }),
  ldrd_strd: () => gen(pick([0xd0, 0xf0]), 0x0e1000f0, { fix: (w) => rnd() & 1 ? w : w & ~0xf00 }),
  ldr_str_imm: () => gen(0x04000000, 0x0e000000),
  ldr_str_reg: () => gen(0x06000000, 0x0e000010),
  ldm_stm: () => gen(0x08000000, 0x0e000000),
  push_pop: () => pick([() => gen(0x092d0000, 0x0fff0000), () => gen(0x08bd0000, 0x0fff0000),
    () => gen(0x092d0000 | (1 << (rnd() % 16)), 0x0fffffff), () => gen(0x08bd0000 | (1 << (rnd() % 16)), 0x0fffffff),
    () => gen(0x052d0004, 0x0fff0fff), () => gen(0x049d0004, 0x0fff0fff)])(),
  branch: () => gen(0x0a000000, 0x0e000000),
  bx_blx_bxj: () => gen(pick([0x012fff10, 0x012fff20, 0x012fff30]), 0x0ffffff0),
  clz: () => gen(0x016f0f10, 0x0fff0ff0),
  qadd_qsub: () => gen(0x01000050, 0x0f900ff0),
  smla_smul: () => gen(0x01000080, 0x0f900090),
  mrs: () => gen(0x010f0000, 0x0fbf0fff),
  msr: () => rnd() & 1 ? gen(0x0320f000, 0x0fb0f000) : gen(0x0120f000, 0x0fb0fff0),
  bkpt: () => gen(0xe1200070, 0xfff000f0),
  svc: () => gen(0x0f000000, 0x0f000000),
  cdp_mcr_mrc: () => gen(0x0e000000, 0x0f000000, { cp: 1 }),
  ldc_stc: () => gen(0x0c000000, 0x0e000000, { cp: 1 }),
  mcrr_mrrc: () => gen(0x0c400000, 0x0fe00000, { cp: 1 }),
  uncond: () => pick([() => gen(0xfa000000, 0xfe000000), () => gen(0xf450f000, 0xfc70f000),
    () => gen(0xfe000000, 0xff000000, { cp: 1 }), () => gen(0xfc000000, 0xfe000000, { cp: 1 }),
    () => gen(0xe7f000f0, 0xfff000f0), () => 0xe1a00000])(),
  // VFP (cp10 = single, cp11 = double): the VFPv2 forms and their neighbours
  vfp_dp: () => gen(0x0e000a00 | pick([0, 0x100000, 0x200000, 0x300000, 0x800000]), 0x0fb00e10,
    { fix: (w) => w & 0x100 && rnd() % 4 ? w & ~0x004000a0 : w }),
  vfp_ext: () => gen(0x0eb00a40 | (pick([0, 1, 4, 5, 7, 8, 12, 13]) << 16), 0x0fbf0e50,
    { fix: (w) => w & 0x100 && rnd() % 4 ? w & ~0x00400020 : w }),
  vfp_ldr_str: () => gen(0x0d000a00, 0x0f200e00, { fix: (w) => rnd() % 4 ? w & ~(w & 0x100 ? 0x00400000 : 0) : (w | 0xf0000) }),
  vfp_ldm_stm: () => gen(0x0c000a00 | pick([0x00800000, 0x00a00000, 0x01200000]), 0x0fa00e00,
    { fix: (w) => { w &= ~(w & 0x100 ? 0x00400000 : 0); return rnd() % 3 ? (w & ~0xff) | (rnd() % 34) : w; } }),
  vfp_push_pop: () => gen(pick([0x0d2d0a00, 0x0cbd0a00]), 0x0fff0e00, { fix: (w) => (w & ~0x004000ff) | (rnd() % 34) }),
  vfp_mov: () => pick([() => gen(0x0e000a10, 0x0fe00f7f), () => gen(0x0e000b10, 0x0fd00fff), () => gen(0x0e000b10, 0x0f000f10),
    () => gen(0x0c400a10, 0x0fe00ed0), () => gen(0x0c400a00, 0x0fe00e00)])(),
  vfp_sysreg: () => gen(0x0ee00a10, 0x0fe00fff, { fix: (w) => rnd() & 1 ? (w & ~0xf0000) | (pick([0, 1, 8, 9, 10]) << 16) : w }),
  vfp_random: () => gen(0x0c000a00, 0x0c000e00, { fix: (w) => rnd() % 8 ? w : (w | 0xf0000000) }),
};

const run = (mode, input) => execFileSync(HOST, [mode], { input, maxBuffer: 1 << 28 }).toString().split('\n').slice(0, -1);
const hex8 = (v) => (v >>> 0).toString(16).padStart(8, '0');
const BASE = 0x8000;

// ------------------------------------------------------------ 1. disassembler
const words = [], cls = [];
for (let i = 0; i < N * 8; i++) { words.push(rnd()); cls.push('random'); }
for (const [name, g] of Object.entries(armClasses)) for (let i = 0; i < N; i++) { words.push(g() >>> 0); cls.push(name); }
const addrOf = (i) => (BASE + i * 4) >>> 0;
const cText = run('d', words.map((w, i) => `${addrOf(i).toString(16)} ${w.toString(16)}`).join('\n') + '\n');
{
  let bad = 0; const ex = [];
  words.forEach((w, i) => { const js = disasmArm(w, addrOf(i)); if (js !== cText[i]) { bad++; if (ex.length < 8) ex.push(`${hex8(w)}  js: ${js} | c: ${cText[i]}`); } });
  check(bad === 0, `ARM disassembly == emu/disasm.mjs on ${words.length} encodings (${bad} differ)`);
  ex.forEach((e) => console.log('       ' + e));
}
{
  const items = [];
  for (let i = 0; i < N * 10; i++) {
    let h = rnd() & 0xffff, nx = rnd() & 0xffff;
    if (i % 5 === 0) { h = 0xf000 | (h & 0x7ff); nx = rnd() & 1 ? 0xf800 | (nx & 0x7ff) : 0xe800 | (nx & 0x7fe); }
    items.push([(BASE + i * 2) >>> 0, h, nx]);
  }
  const out = run('t', items.map(([a, h, n]) => `${a.toString(16)} ${h.toString(16)} ${n.toString(16)}`).join('\n') + '\n');
  let bad = 0; const ex = [];
  items.forEach(([a, h, n], i) => {
    const r = disasmThumb(h, a, n); const want = `${r.size} ${r.text}`;
    if (want !== out[i]) { bad++; if (ex.length < 8) ex.push(`${h.toString(16)} ${n.toString(16)}  js: ${want} | c: ${out[i]}`); }
  });
  check(bad === 0, `Thumb disassembly == emu/disasm.mjs on ${items.length} halfword pairs (${bad} differ)`);
  ex.forEach((e) => console.log('       ' + e));
}

// ------------------------------------------------------------ 2. round trip
const strip = (t) => t.replace(/\t@.*$/, '').replace(/\t/g, ' ').trim();
const cands = [];
words.forEach((w, i) => {
  const t = cText[i];
  if (/^\.word|UNPREDICTABLE|illegal shifter/.test(t) || t.includes('<UNPREDICTABLE>') || t.includes('<illegal')) return;
  // "push {sp}" (the str form) assembles to stmfd sp!, {sp}, as GNU as does
  if (/^push\S*\t\{sp\}/.test(t)) return;
  cands.push({ w, a: addrOf(i), t, s: strip(t), cls: cls[i] });
});
const asmOut = run('a', cands.map((c) => `${c.a.toString(16)}\t${c.s}`).join('\n') + '\n');
{
  let bad = 0, exact = 0; const ex = [];
  const back = [];
  cands.forEach((c, i) => { const o = asmOut[i]; if (/^[0-9a-f]{8}$/.test(o)) back.push({ i, w2: parseInt(o, 16) >>> 0 }); else { bad++; if (ex.length < 12) ex.push(`${hex8(c.w)} ${c.s}  -> ${o}`); } });
  const t2 = run('d', back.map(({ i, w2 }) => `${cands[i].a.toString(16)} ${w2.toString(16)}`).join('\n') + '\n');
  back.forEach(({ i, w2 }, k) => {
    if (t2[k] !== cands[i].t) { bad++; if (ex.length < 12) ex.push(`${hex8(cands[i].w)} ${cands[i].t} -> ${hex8(w2)} ${t2[k]}`); }
    else if (w2 === cands[i].w) exact++;
  });
  check(bad === 0, `round trip disasm -> asm -> disasm on ${cands.length} instructions (${bad} bad; ${exact} identical words, the rest differ only in should-be-zero bits)`);
  ex.forEach((e) => console.log('       ' + e));
}

// ------------------------------------------------------------ 3. vs GNU as
function gnuAs(lines) {
  // returns Map(index -> word) for the lines GNU as accepts (others dropped and retried)
  let live = lines.map((l, i) => ({ l, i }));
  for (let round = 0; round < 6; round++) {
    const src = '.syntax unified\n.arm\n' + live.map((x) => x.l).join('\n') + '\n';
    fs.writeFileSync(path.join(tmp, 'x.s'), src);
    try {
      execFileSync('arm-none-eabi-as', ['-march=armv5te', '-mcpu=arm926ej-s', '-mfpu=vfpv2', '-o', path.join(tmp, 'x.o'), path.join(tmp, 'x.s')], { stdio: ['ignore', 'ignore', 'pipe'] });
    } catch (e) {
      const bad = new Set();
      for (const m of e.stderr.toString().matchAll(/x\.s:(\d+): Error/g)) bad.add(+m[1] - 3);
      if (!bad.size) throw e;
      live = live.filter((_, k) => !bad.has(k));
      continue;
    }
    execFileSync('arm-none-eabi-objcopy', ['-O', 'binary', '-j', '.text', path.join(tmp, 'x.o'), path.join(tmp, 'x.bin')]);
    const b = fs.readFileSync(path.join(tmp, 'x.bin'));
    const m = new Map();
    live.forEach((x, k) => m.set(x.i, b.readUInt32LE(k * 4)));
    return m;
  }
  throw new Error('GNU as keeps failing');
}
{
  const sel = cands.filter((c) => !/0x[0-9a-f]+/.test(c.s) && !/\bpc\b/.test(c.s) && !/^(b|bl|blx)(eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?\s/.test(c.s)
    && !/^(tst|teq|cmp|cmn)p/.test(c.s) /* p-variants: GNU reads cmppls as cmp+pl+s */
    && !c.s.includes('{-0}') /* GNU encodes the unindexed {-0} with U=1 */);

  const m = gnuAs(sel.map((c) => c.s));
  let bad = 0, cmp = 0; const ex = [];
  const idx = [...m.keys()];
  const mine = run('a', idx.map((i) => `${sel[i].a.toString(16)}\t${sel[i].s}`).join('\n') + '\n');
  idx.forEach((i, k) => {
    cmp++;
    const g = m.get(i) >>> 0, o = mine[k];
    if (o !== hex8(g)) { bad++; if (ex.length < 12) ex.push(`${sel[i].s}  gnu ${hex8(g)} | me ${o}`); }
  });
  check(bad === 0 && cmp > sel.length * 0.8, `assembler == arm-none-eabi-as on ${cmp} of ${sel.length} texts (${sel.length - cmp} refused by GNU as, ${bad} differ)`);
  ex.forEach((e) => console.log('       ' + e));
}

// hand-written DEBUG-style lines: [text for me, text for GNU as (or null = same)]
const hand = [
  ['mov r0,#0x200', null], ['MOV R3,#41H', 'mov r3,#0x41'], ['mov r3,#\'A\'', null], ['svc #0x21', null],
  ['SVC 21', 'svc #0x21'], ['svc 20h', 'svc #0x20'], ['swi 0x21', 'svc #0x21'], ['bkpt', 'bkpt #0'], ['bkpt 12', 'bkpt #0x12'],
  ['ADD R0,R1', 'add r0,r0,r1'], ['add r0,#1', 'add r0,r0,#1'], ['addeqs r0,r1,r2', 'addseq r0,r1,r2'],
  ['ldreqb r0,[r1,#4]!', 'ldrbeq r0,[r1,#4]!'], ['ldmeqia r0!,{r1-r3}', 'ldmiaeq r0!,{r1-r3}'],
  ['stmfd sp!,{r4-r11,lr}', null], ['ldmfd sp!,{r4-r11,pc}', null], ['stmed r0,{r1}', 'stmda r0,{r1}'],
  ['ldmea r0,{r1,r2}', 'ldmdb r0,{r1,r2}'], ['stmfa r0!,{r1}', 'stmib r0!,{r1}'], ['ldmed r2,{r0}', 'ldmib r2,{r0}'],
  ['mov r0,#-1', 'mvn r0,#0'], ['and r0,r1,#0xffffff00', 'bic r0,r1,#0xff'], ['cmp r0,#-1', 'cmn r0,#1'],
  ['sub r0,r0,#-4', 'add r0,r0,#4'], ['mov r0,r1,lsl #3', null], ['lsl r0,r1,#3', 'mov r0,r1,lsl #3'],
  ['mov r0,r1,asl r2', 'mov r0,r1,lsl r2'], ['movs r0,r0,rrx', null], ['ror r1,r2,r3', 'mov r1,r2,ror r3'],
  ['ldrh r0,[r1],#-2', null], ['ldrsh r0,[r1,-r2]!', null], ['strd r4,[sp,#-8]!', null], ['ldrd r2,[r0]', null],
  ['ldr r0,[r1,r2,lsl #2]', null], ['str r0,[r1],-r2,asr #3', null], ['ldrbt r0,[r1],#1', null], ['strt r0,[r1]', 'strt r0,[r1],#0'],
  ['swpb r0,r1,[r2]', null], ['mrs r0,cpsr', null], ['msr cpsr_c,#0x1f', null], ['msr CPSR_fsxc,r0', 'msr cpsr_fsxc,r0'],
  ['msr cpsr,r0', 'msr cpsr_fc,r0'], ['msr spsr_f,#0xf0000000', null], ['mcr p15,0,r0,c7,c0,4', null],
  ['mrc 15,0,r0,cr0,cr0,{0}', 'mrc p15,0,r0,c0,c0,0'], ['cdp p5,1,c2,c3,c4,5', null], ['ldc p5,c1,[r0,#8]!', null],
  ['stcl p6,c2,[r1],#-4', null], ['mcrr p7,1,r0,r1,c2', null], ['umull r0,r1,r2,r3', null], ['smlals r0,r1,r2,r3', null],
  ['mul r0,r1,r2', null], ['mlaeq r0,r1,r2,r3', null], ['smulbt r0,r1,r2', null], ['smlawt r0,r1,r2,r3', null],
  ['smlaltb r0,r1,r2,r3', null], ['qdsub r0,r1,r2', null], ['clz r0,r1', null], ['bx lr', null], ['blx r3', null],
  ['push {r0}', null], ['pop {r4,pc}', null], ['nop', 'mov r0,r0'], ['pld [r0,#32]', null], ['udf #7', null],
  ['tst r0,#0x80000000', null], ['teq r1,r2,lsr r3', null], ['rsc r0,r1,#255', null], ['mov r0,#0xff000000', null],
  ['mov r0,#1,2', 'mov r0,#0x40000000'], ['ldr r0,[r1,#-0]', null],
  // VFPv2, DEBUG style and GNU's
  ['VADD.F32 S0,S1,S2', 'vadd.f32 s0,s1,s2'], ['vaddne.f64 d1,d2,d3', null], ['vmul.f32 s3,s4', 'vmul.f32 s3,s3,s4'],
  ['vnmlagt.f64 d15,d14,d13', null], ['vdiv.f32 s31,s30,s29', null], ['vsqrt.f64 d0,d1', null],
  ['vcmpe.f32 s0,#0.0', null], ['vcmp.f64 d2,#0', 'vcmp.f64 d2,#0.0'], ['vcmpeq.f32 s1,s2', null],
  ['vcvt.f64.f32 d0,s1', null], ['vcvt.f32.f64 s2,d3', null], ['vcvt.f32.s32 s0,s1', null], ['vcvt.f64.u32 d4,s9', null],
  ['vcvt.s32.f32 s0,s1', null], ['vcvtr.u32.f64 s5,d6', null], ['vcvtrle.s32.f64 s5,d6', null],
  ['vldr s0,[r1]', null], ['vldr d3,[r2,#-8]', null], ['vstrmi s7,[sp,#1020]', null], ['vldr.64 d0,[r0,#8]', 'vldr d0,[r0,#8]'],
  ['vldmia r0!,{s0-s3}', null], ['vstmdb sp!,{d8-d15}', null], ['vpush {s16-s31}', null], ['vpop {d8}', null],
  ['vldm r1,{d0,d1,d2}', 'vldmia r1,{d0-d2}'], ['fldmiax r4!,{d0-d3}', null], ['fstmdbx sp!,{d8}', null],
  ['vmov s0,r1', null], ['vmov r2,s31', null], ['vmov d5,r0,r1', null], ['vmov r0,r1,d5', null],
  ['vmov s4,s5,r2,r3', null], ['vmov r2,r3,s4,s5', null], ['vmov.32 d7[1],r4', null], ['vmov.32 r4,d7[0]', null],
  ['vmov.f32 s1,s2', null], ['vmov.f64 d1,d2', null], ['vabs.f32 s0,s0', null], ['vneg.f64 d9,d10', null],
  ['vmrs APSR_nzcv,fpscr', null], ['vmrs r0,fpscr', null], ['vmsr fpscr,r0', null], ['vmrs r1,fpexc', null],
  ['vmsr fpexc,r2', null], ['vmrs r3,fpsid', null], ['vmrs r3,fpinst', null], ['vmsr fpinst2,r3', null],
];
{
  const m = gnuAs(hand.map(([a, g]) => g ?? a));
  const mine = run('a', hand.map(([a]) => `8000\t${a}`).join('\n') + '\n');
  let bad = 0;
  hand.forEach(([a, g], i) => {
    const want = m.has(i) ? hex8(m.get(i)) : 'GNU as refused';
    if (mine[i] !== want) { bad++; console.log(`       ${a}  gnu ${want} | me ${mine[i]}`); }
  });
  check(bad === 0, `${hand.length} hand-written DEBUG-style lines == GNU as`);
}
// things that must be refused, with the error column
const errs = [
  ['mov r0,#0x101', 7], ['ldr r0,[r1,#4096]', 12], ['add r0,r1,r16', 10], ['foo r0', 0], ['mov r0', 6],
  ['b 0x8001', 2], ['lsl r0,r1,#32', 10], ['strsb r0,[r1]', 0], ['mov r0,r1 junk', 10], ['ldrh r0,[r1,#256]', 13],
  // VFPv2 has no d16-d31, no fused multiply-add, no vmov #imm; vldr offsets are words
  ['vadd.f64 d16,d1,d2', 9], ['vfma.f32 s0,s1,s2', 0], ['vmov.f32 s0,#1.0', 12], ['vldr s0,[r1,#2]', 13],
];
{
  const out = run('a', errs.map(([t]) => `8000\t${t}`).join('\n') + '\n');
  let bad = 0;
  errs.forEach(([t, col], i) => { if (out[i] !== `ERR ${col}`) { bad++; console.log(`       ${t}: want ERR ${col}, got ${out[i]}`); } });
  check(bad === 0, `${errs.length} bad lines refused with the error at the right column`);
}
// branches: targets relative to the line's address (DEBUG hex addresses)
{
  const lines = [['8000', 'b 0x8000', 'eafffffe'], ['8000', 'bl 0x9000', 'eb0003fe'], ['8000', 'beq 7ff8', '0afffffc'],
    ['8000', 'blx 0x8012', 'fb000002'], ['8004', 'b .', 'eafffffe'], ['8000', 'ldr r0, 0x8100', 'e59f00f8'],
    ['8000', 'adr r1, 0x8010', 'e28f1008'], ['8010', 'adr r1, 0x8000', 'e24f1018'], ['8000', 'ldrh r2, 0x8010', 'e1df20b8']];
  const out = run('a', lines.map(([a, t]) => `${a}\t${t}`).join('\n') + '\n');
  let bad = 0;
  lines.forEach(([a, t, w], i) => { if (out[i] !== w) { bad++; console.log(`       @${a} ${t}: want ${w} got ${out[i]}`); } });
  check(bad === 0, `${lines.length} branch / pc-relative lines`);
}
// data
{
  const lines = [['DB 41,42,\'hi\',0D,0A', '4142686' + '90d0a'], ['DW 1234,5', '00051234'], ['DD 12345678', '12345678'],
    ['.word 0x11223344, 5', '4433221105000000'], ['.byte \'A\', 10', '410a'], ['.short -1', 'ffff'], ['db "it\'s"', '73277469']];
  const out = run('a', lines.map(([t]) => `8000\t${t}`).join('\n') + '\n');
  let bad = 0;
  lines.forEach(([t, w], i) => { if (out[i] !== w) { bad++; console.log(`       ${t}: want ${w} got ${out[i]}`); } });
  check(bad === 0, `${lines.length} data lines (DB/DW/DD, .byte/.short/.word)`);
}

fs.rmSync(tmp, { recursive: true, force: true });
console.log(failures ? `${failures} FAILED` : 'all passed');
process.exit(failures ? 1 : 0);
