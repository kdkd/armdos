// Differential test of emu/disasm.mjs against GNU arm-none-eabi-objdump.
//   node emu/tests/disasm/test-disasm.mjs [-v] [--seed=N] [--n=COUNT]
// Generates random + targeted ARM and Thumb encodings, disassembles them with
// objdump (-marmv5tej, and -Mforce-thumb) and with disasm.mjs, compares after light
// normalisation (whitespace collapsed; objdump's "<UNDEFINED> instruction" line ==
// our ".word"/".short").  Encodings objdump decodes as something outside ARMv5TE and
// outside the cp10/cp11 (VFP) space (NEON, hlt, banked-register mrs/msr, v6+ Thumb)
// are excluded and counted.  In the cp10/cp11 space disasm.mjs decodes VFPv2 only
// (the VFP9-S): where it prints an instruction, the text must be objdump's (an
// "@ <UNPREDICTABLE>" it adds is ignored); where it prints .word but objdump has a
// VFP/NEON reading, that reading must be one arm-none-eabi-as -mfpu=vfpv2 refuses or
// assembles to a different word (VFPv3/v4/FP16/v8, d16-d31, NEON scalars: excluded
// and counted; gas's own gaps - d16-d31 in core transfers, MVFRn, f16 - are named
// explicitly) - an objdump text that assembles back to the same word under VFPv2
// is a failure.  Generic cdp/mcr/mrc/mcrr/mrrc/ldc/stc(2) readings of cp10/11 are
// UNDEFINED on a VFP machine (excluded).  arm.vfp_* are the targeted VFP classes.
// Exit 0 when every class meets its threshold.  If objdump is missing -> SKIP (exit 0).
import { writeFileSync, readFileSync, mkdtempSync, rmSync } from 'fs';
import { execFileSync } from 'child_process';
import { tmpdir } from 'os';
import { join } from 'path';
import { disasmArm, disasmThumb } from '../../disasm.mjs';

const args = process.argv.slice(2);
const verbose = args.includes('-v');
const seedArg = args.find((a) => a.startsWith('--seed='));
const nArg = args.find((a) => a.startsWith('--n='));
let seed = seedArg ? +seedArg.slice(7) : 12345;
const N = nArg ? +nArg.slice(4) : 3000;        // per targeted class
const OBJDUMP = process.env.OBJDUMP || 'arm-none-eabi-objdump';
const AS = process.env.AS || 'arm-none-eabi-as', OBJCOPY = process.env.OBJCOPY || 'arm-none-eabi-objcopy';
const VMA = 0x8000;
const RANDOM_MIN = 0.995, TARGET_MIN = 0.999;   // pass thresholds

function rnd() { // mulberry32
  seed = (seed + 0x6d2b79f5) | 0;
  let t = seed;
  t = Math.imul(t ^ (t >>> 15), t | 1);
  t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
  return (t ^ (t >>> 14)) >>> 0;
}
const pick = (a) => a[rnd() % a.length];

try { execFileSync(OBJDUMP, ['--version']); } catch {
  console.log('SKIP: ' + OBJDUMP + ' not found'); process.exit(0);
}
const dir = mkdtempSync(join(tmpdir(), 'disasm-test-'));

function objdump(buf, thumb) {
  const f = join(dir, thumb ? 't.bin' : 'a.bin');
  writeFileSync(f, buf);
  const out = execFileSync(OBJDUMP, ['-D', '-b', 'binary', '-marmv5tej', ...(thumb ? ['-Mforce-thumb'] : []),
    '--adjust-vma=' + VMA, f], { maxBuffer: 1 << 28 }).toString();
  const res = new Map();
  for (const line of out.split('\n')) {
    const m = /^\s*([0-9a-f]+):\t([0-9a-f]{4}(?: [0-9a-f]{4})?|[0-9a-f]{8})\s*\t(.*)$/.exec(line);
    if (m) res.set(parseInt(m[1], 16), m[3]);
  }
  return res;
}
const norm = (s) => s.replace(/@ <UNDEFINED> instruction: (0x[0-9a-f]+)/, (_, h) => (h.length > 6 ? '.word ' : '.short ') + h)
  .replace(/\s+/g, ' ').trim();

// VFPv2 oracle: which of these objdump texts does GNU as accept with -mfpu=vfpv2, and
// as which word?  Returns Map(index -> word); refused lines are left out.
function gasVfp2(texts) {
  let live = texts.map((l, i) => ({ l: l.replace(/\s*@.*$/, ''), i }));
  for (let round = 0; round < 8 && live.length; round++) {
    const src = '.syntax unified\n.arm\n' + live.map((x) => x.l).join('\n') + '\n';
    writeFileSync(join(dir, 'v.s'), src);
    try {
      execFileSync(AS, ['-march=armv5te', '-mfpu=vfpv2', '-o', join(dir, 'v.o'), join(dir, 'v.s')], { stdio: ['ignore', 'ignore', 'pipe'] });
    } catch (e) {
      const bad = new Set();
      for (const m of e.stderr.toString().matchAll(/v\.s:(\d+): Error/g)) bad.add(+m[1] - 3);
      if (!bad.size) throw e;
      live = live.filter((_, k) => !bad.has(k));
      continue;
    }
    execFileSync(OBJCOPY, ['-O', 'binary', '-j', '.text', join(dir, 'v.o'), join(dir, 'v.bin')]);
    const b = readFileSync(join(dir, 'v.bin'));
    const m = new Map();
    live.forEach((x, k) => m.set(x.i, b.readUInt32LE(k * 4)));
    return m;
  }
  return new Map();
}

// ---------------------------------------------------------------- ARM
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
  // ---- VFP (cp10 = single, cp11 = double)
  vfp_dp_s: () => gen(0x0e000a00 | pick([0, 0x100000, 0x200000, 0x300000, 0x800000]), 0x0fb00f10),
  vfp_dp_d: () => gen(0x0e000b00 | pick([0, 0x100000, 0x200000, 0x300000, 0x800000]), 0x0fb00f10),
  vfp_dp_v4: () => gen(0x0e000a00 | pick([0x900000, 0xa00000]), 0x0fb00e10),
  vfp_ext: () => gen(0x0eb00a40, 0x0fb00e50),                     // cpy/abs/neg/sqrt/cmp/cvt/...
  vfp_ext_v2: () => gen(0x0eb00a40 | (pick([0, 1, 4, 5, 7, 8, 12, 13]) << 16), 0x0fbf0e50, { fix: (w) => w & ~0x00400000 & ~(w & 0x100 ? 0x20 : 0) }),
  vfp_cmp: () => gen(0x0eb40a40 | (rnd() & 1) << 16, 0x0fbf0e50, { fix: (w) => rnd() & 1 ? w : w & ~0x2f }),
  vfp_cvt: () => gen(0x0eb00a40 | (pick([7, 8, 10, 11, 12, 13, 14, 15]) << 16), 0x0fbf0e50),
  vfp_vmov_imm: () => gen(0x0eb00a00, 0x0fb00ef0),
  vfp_ldr_str: () => gen(0x0d000a00, 0x0f200e00, { fix: (w) => rnd() % 4 ? w : (w | 0xf0000) }),
  vfp_ldm_stm: () => gen(0x0c000a00 | pick([0x00800000, 0x00a00000, 0x01200000]), 0x0fa00e00,
    { fix: (w) => rnd() % 3 ? (w & ~0xff) | (rnd() % 34) : w }),
  vfp_push_pop: () => gen(pick([0x0d2d0a00, 0x0cbd0a00]), 0x0fff0e00, { fix: (w) => (w & ~0xff) | (rnd() % 34) }),
  vfp_fldmx: () => gen(0x0c000b01 | pick([0x00800000, 0x00a00000, 0x01200000]), 0x0fa00f01, { fix: (w) => rnd() % 3 ? (w & ~0xfe) | ((rnd() % 17) << 1) : w }),
  vfp_mov_core_s: () => gen(0x0e000a10, 0x0fe00f7f),
  vfp_mov_core_d: () => gen(0x0e000b10, 0x0fd00fff),
  vfp_mov_scalar: () => gen(0x0e000b10, 0x0f000f10),              // NEON .8/.16/.32 + vdup
  vfp_mov_2reg: () => gen(0x0c400a10, 0x0fe00ed0),
  vfp_mcrr_mrrc: () => gen(0x0c400a00, 0x0fe00e00),
  vfp_sysreg: () => gen(0x0ee00a10, 0x0fe00fff, { fix: (w) => rnd() & 1 ? (w & ~0xf0000) | (pick([0, 1, 8, 9, 10]) << 16) : w }),
  vfp_uncond: () => pick([() => gen(0xfe000a00, 0xff000e00), () => gen(0xfc000a00, 0xfe000e00), () => gen(0xfeb80a40, 0xffb80e50)])(),
  vfp_random: () => gen(0x0c000a00, 0x0c000e00, { fix: (w) => rnd() % 8 ? w : (w | 0xf0000000) }),
};
// Encodings objdump decodes with a newer-architecture meaning (outside cp10/11).
const vfpSpace = (w) => ((w >>> 25) & 7) >= 6 && ((w >>> 24) & 15) !== 15 && ((w >>> 9) & 7) === 5;
const armExcluded = (w, od) => !vfpSpace(w) && (/^(v|f(ld|st)m|hlt|lda|stl|sha|aes)/.test(od)
  || /\(UNDEF: |_(usr|fiq|irq|svc|abt|und|mon|hyp)\b|ELR_hyp|SPSR_(fiq|irq|svc|abt|und|mon|hyp)/.test(od));

// ---------------------------------------------------------------- Thumb
const thumbV6 = (h) => [0xb1, 0xb3, 0xb9, 0xbb, 0xb2, 0xba, 0xb6, 0xbf].includes(h >>> 8);
function thumbSeq(kind) {
  if (kind === 'bl_blx') {
    const p = 0xf000 | (rnd() & 0x7ff);
    return [p, rnd() & 1 ? 0xf800 | (rnd() & 0x7ff) : 0xe800 | (rnd() & 0x7fe)];
  }
  for (;;) {
    let h = rnd() & 0xffff;
    if (kind === 'random16') { if (h >= 0xe800) continue; } else {
      const [v, m] = kind; h = (h & ~m) | v;
    }
    if ((h & 0xff00) === 0xbf00 && (h & 15)) continue;   // IT would change later output
    return [h];
  }
}
const thumbClasses = {
  random16: 'random16', shift_imm: [0x0000, 0xe000], addsub3: [0x1800, 0xf800], imm8: [0x2000, 0xe000],
  alu: [0x4000, 0xfc00], hireg_bx: [0x4400, 0xfc00], ldr_pc: [0x4800, 0xf800], ldst_reg: [0x5000, 0xf000],
  ldst_imm: [0x6000, 0xe000], ldst_h_sp: [0x8000, 0xe000], adr_addsp: [0xa000, 0xf000], misc_b: [0xb000, 0xf000],
  ldm_stm: [0xc000, 0xf000], bcond_svc: [0xd000, 0xf000], b: [0xe000, 0xf800], bl_blx: 'bl_blx',
};

// ---------------------------------------------------------------- run
const results = [];
let fail = false;
function report(name, total, excl, bad, min, examples) {
  const cmp = total - excl, rate = cmp ? (cmp - bad) / cmp : 1;
  const ok = rate >= min;
  if (!ok) fail = true;
  results.push(`${ok ? 'ok  ' : 'FAIL'} ${name.padEnd(22)} ${String(cmp - bad).padStart(6)}/${String(cmp).padEnd(6)} ${(rate * 100).toFixed(2).padStart(6)}%` + (excl ? `  (${excl} excluded)` : ''));
  if (verbose || !ok) for (const e of examples.slice(0, verbose ? 15 : 5)) results.push('       ' + e);
}

// ARM: one binary with all classes
{
  const words = [], cls = [];
  const R = N * 8;
  for (let i = 0; i < R; i++) { words.push(rnd()); cls.push('random'); }
  for (const [name, g] of Object.entries(armClasses)) for (let i = 0; i < N; i++) { words.push(g() >>> 0); cls.push(name); }
  const buf = Buffer.alloc(words.length * 4);
  words.forEach((w, i) => buf.writeUInt32LE(w, i * 4));
  const od = objdump(buf, false);
  const st = {};
  const oracle = [];         // [index, objdump text] for .word in the VFP space
  const generic = /^(cdp|mcrr|mrrc|mcr|mrc|ldc|stc)2?/;
  words.forEach((w, i) => {
    const a = VMA + i * 4, o = od.get(a), s = (st[cls[i]] ??= { t: 0, x: 0, b: 0, ex: [] });
    s.t++;
    if (o === undefined || armExcluded(w, o)) { s.x++; return; }
    let mine = disasmArm(w, a);
    if (vfpSpace(w)) {
      if (mine.startsWith('.word')) {
        if (norm(o).startsWith('.word') || generic.test(o)) { if (!norm(o).startsWith('.word')) s.x++; return; }
        oracle.push([i, o]);
        return;
      }
      if (!o.includes('<UNPREDICTABLE>')) mine = mine.replace('\t@ <UNPREDICTABLE>', '');
    }
    if (norm(mine) !== norm(o)) { s.b++; s.ex.push(`${w.toString(16).padStart(8, '0')}  od: ${norm(o)}  |  me: ${norm(mine)}`); }
  });
  const g = gasVfp2(oracle.map(([, o]) => o));
  oracle.forEach(([i, o], k) => {
    const s = st[cls[i]], w = words[i];
    // GNU as lets d16-d31 in core transfers, MVFRn and the f16 conversions through
    // under -mfpu=vfpv2; VFPv2 has none of them (16 D registers, FPSID/FPSCR/FPEXC)
    if (g.get(k) === w && !/\bd(1[6-9]|2\d|3[01])\b|mvfr|\.f16/.test(o)) { s.b++; s.ex.push(`${w.toString(16).padStart(8, '0')}  od: ${norm(o)}  |  me: .word, but VFPv2 has it`); }
    else s.x++;
  });
  results.push('ARM:');
  for (const [k, s] of Object.entries(st)) report('arm.' + k, s.t, s.x, s.b, k === 'random' ? RANDOM_MIN : TARGET_MIN, s.ex);
}
// Thumb
{
  const items = [];
  for (const [name, k] of Object.entries(thumbClasses)) {
    const n = name === 'random16' ? N * 8 : N;
    for (let i = 0; i < n; i++) items.push({ name, hws: thumbSeq(k) });
  }
  const hws = items.flatMap((x) => x.hws);
  const buf = Buffer.alloc(hws.length * 2);
  hws.forEach((h, i) => buf.writeUInt16LE(h, i * 2));
  const od = objdump(buf, true);
  const st = {};
  let a = VMA;
  for (const it of items) {
    const s = (st[it.name] ??= { t: 0, x: 0, b: 0, ex: [] });
    const o = od.get(a);
    s.t++;
    if (o === undefined || (it.hws.length === 1 && thumbV6(it.hws[0])) || /^b?x?ns\b|^blxns|^bxns/.test(o)) s.x++;
    else {
      const r = disasmThumb(it.hws[0], a, it.hws[1]);
      if (norm(r.text) !== norm(o) || r.size !== it.hws.length * 2) {
        s.b++; s.ex.push(`${it.hws.map((h) => h.toString(16).padStart(4, '0')).join(' ')}  od: ${norm(o)}  |  me: ${norm(r.text)} (${r.size})`);
      }
    }
    a += it.hws.length * 2;
  }
  results.push('Thumb:');
  for (const [k, s] of Object.entries(st)) report('thumb.' + k, s.t, s.x, s.b, k === 'random16' ? RANDOM_MIN : TARGET_MIN, s.ex);
}
rmSync(dir, { recursive: true, force: true });
console.log(results.join('\n'));
console.log(fail ? 'FAIL' : 'PASS');
process.exit(fail ? 1 : 0);
