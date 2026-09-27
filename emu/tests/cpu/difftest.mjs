#!/usr/bin/env node
// Differential test: our CPU vs qemu-system-arm (-M versatilepb -cpu arm926).
// usage: node emu/tests/cpu/difftest.mjs [--seed N] [--per N] [--classes a,b] [--jit] [--keep]
import { execFileSync } from 'node:child_process';
import { mkdirSync, writeFileSync, readFileSync, rmSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';
import { buildProgram, ARM_CLASSES, THUMB_CLASSES } from './gen.mjs';
import { CPU } from '../../cpu.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf('--' + k); return i >= 0 ? args[i + 1] : d; };
const seed = +opt('seed', 1);
const per = +opt('per', 400);
const classes = opt('classes', [...ARM_CLASSES, ...THUMB_CLASSES].join(',')).split(',');
const useJit = args.includes('--jit');
const work = join(process.env.EMU_TEST_TMP || join(tmpdir(), 'armdos-emu-difftest'), `s${seed}`);
mkdirSync(work, { recursive: true });

const { asm, cases } = buildProgram(classes, per, seed);
writeFileSync(join(work, 't.s'), asm);
const sh = (cmd, a) => { try { return execFileSync(cmd, a, { cwd: work, stdio: ['ignore', 'pipe', 'pipe'] }).toString(); } catch (e) { console.error(e.stderr?.toString()); process.exit(3); } };
sh('arm-none-eabi-as', ['-march=armv5te', 't.s', '-o', 't.o']);
sh('arm-none-eabi-ld', ['-Ttext=0x10000', 't.o', '-o', 't.elf']);
sh('arm-none-eabi-objcopy', ['-O', 'binary', 't.elf', 't.bin']);
const table = parseInt(sh('arm-none-eabi-nm', ['t.elf']).split('\n').find((l) => / table$/.test(l)).split(' ')[0], 16);
const bin = readFileSync(join(work, 't.bin'));

// ---- QEMU
if (existsSync(join(work, 'out.bin'))) rmSync(join(work, 'out.bin'));
execFileSync('qemu-system-arm', ['-M', 'versatilepb', '-cpu', 'arm926', '-m', '128M', '-nographic', '-monitor', 'none', '-serial', 'null',
  '-semihosting-config', 'enable=on,target=native', '-kernel', 't.bin'], { cwd: work, timeout: 60000, stdio: 'ignore' });
const qall = new Int32Array(new Uint8Array(readFileSync(join(work, 'out.bin'))).buffer);
if (qall[0] !== -1) console.log(`QEMU took exception vector ${qall[0]} lr=${(qall[1]>>>0).toString(16)} case=${(qall[2]-table)/64}`);
const q = qall.subarray(4);
for (let k = 0; k < cases.length; k++) q[k * 16 + 14] &= ~0x100;   // QEMU sets CPSR.A (v6 bit) at reset

// ---- ours
let done = false;
const bus = {
  read: () => -1,
  write: (a, size, v) => { if ((a >>> 0) === 0x100000F4) { done = true; cpu.halted = 1; cpu.brk = 1; return true; } return false; },
};
const cpu = new CPU(bus);
if (useJit) { const { JIT } = await import('../../jit.mjs'); cpu.jit = new JIT(cpu, { threshold: +(process.env.JIT_THRESHOLD || 1) }); }
cpu.m8.set(bin, 0x10000);
cpu.r[1] = 0; cpu.pc = 0x10000;
const t0 = performance.now();
let steps = 0;
while (!done && steps < 5e8) { const n = cpu.run(1e6); steps += n; if (n === 0 && !done) break; }
const t1 = performance.now();
if (!done) { console.log('our CPU did not finish; pc=' + (cpu.pc >>> 0).toString(16), cpu.lastFault); process.exit(2); }
const o = new Int32Array(cpu.buf, table, cases.length * 16);
const excinfo = parseInt(sh('arm-none-eabi-nm', ['t.elf']).split('\n').find((l) => / excinfo$/.test(l)).split(' ')[0], 16);
{ const e = new Int32Array(cpu.buf, excinfo, 3); if (e[0] !== -1) console.log(`OURS took exception vector ${e[0]} lr=${(e[1]>>>0).toString(16)} case=${(e[2]-table)/64}`); }

const hex = (x) => (x >>> 0).toString(16).padStart(8, '0');
const regName = (k) => k < 13 ? 'r' + k : k === 13 ? 'lr' : k === 14 ? 'cpsr' : 'bufsum';
const fails = {};
let nfail = 0;
for (let k = 0; k < cases.length; k++) {
  const c = cases[k];
  let bad = [];
  for (let j = 0; j < 16; j++) if (q[k * 16 + j] !== o[k * 16 + j]) bad.push(j);
  if (bad.length) {
    nfail++;
    fails[c.cls] = (fails[c.cls] || 0) + 1;
    if ((fails[c.cls]) <= +opt('show', 4)) {
      const ins = c.arm !== undefined ? 'arm ' + hex(c.arm) : 'thumb ' + c.thumb.map((h) => h.toString(16).padStart(4, '0')).join(' ');
      console.log(`FAIL #${k} ${c.cls} ${ins}`);
      console.log('   in : ' + c.regs.slice(0, 13).map(hex).join(' ') + ' lr=' + hex(c.regs[14]) + ' flags=' + hex(c.flags));
      for (const j of bad) console.log(`   ${regName(j)}: qemu=${hex(q[k * 16 + j])} ours=${hex(o[k * 16 + j])}`);
    }
  }
}
const per_cls = classes.map((c) => `${c}:${fails[c] || 0}`).join(' ');
console.log(`seed ${seed}: ${cases.length} cases, ${nfail} mismatches  [${per_cls}]  (${(steps / 1e6).toFixed(1)}M insns in ${(t1 - t0).toFixed(0)} ms${useJit ? ', jit' : ''})`);
if (!args.includes('--keep')) rmSync(work, { recursive: true, force: true });
process.exit(nfail ? 1 : 0);
