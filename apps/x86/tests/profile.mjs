#!/usr/bin/env node
// apps/x86/tests/profile.mjs - sample the ARM PC while ELBOW.EXE runs a program
// and print the hottest functions / addresses (symbols from X86.elf).
//   node apps/x86/tests/profile.mjs "X86 /NOJIT \X\BENCH.COM" [--addr]
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { session, ROOT } from '../../dosutil/tests/harness.mjs';

const B = (p) => path.join(ROOT, 'build', p);
const files = [{ src: 'build/ELBOW.EXE', dst: 'DOS\\' }, { src: 'build/HIMEM.SYS', dst: 'DOS\\' }];
for (const f of fs.readdirSync(B('x86-test'))) files.push({ src: 'build/x86-test/' + f, dst: 'X\\' });
const s = await session({ name: 'x86prof', files, dirs: ['X'], config: 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=20\nSHELL=C:\\T\\TSHELL.EXE\n' });
const cmd = process.argv[2].replace(/^X86/, '\\DOS\\X86');
s.pc.type('CLS\r'); s.waitPrompt();
s.pc.type(cmd + '\r');
s.pc.run(+(process.env.SKIP || 300));
const exe = fs.readFileSync(B('ELBOW.EXE'));
const imgOff = exe.readUInt32LE(0x80 + 8);
const sig = exe.subarray(imgOff, imgOff + 64);
const m8 = s.pc.cpu.m8;
let base = -1;
for (let a = 0x600; a < 0xA0000; a += 16) if (Buffer.from(m8.subarray(a, a + 64)).equals(sig)) base = a;
if (base < 0) throw new Error('image not found');
const elf = B('obj/ELBOW/ELBOW.elf');
const syms = execFileSync('arm-none-eabi-nm', ['-n', '--defined-only', elf]).toString()
  .split('\n').map((l) => l.split(' ')).filter((p) => p.length === 3 && /[tT]/.test(p[1])).map((p) => [parseInt(p[0], 16), p[2]]);
const lookup = (a) => { let lo = 0, hi = syms.length - 1; if (a < syms[0][0]) return ['?', 0]; while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (syms[mid][0] <= a) lo = mid; else hi = mid - 1; } return [syms[lo][1], a - syms[lo][0]]; };
const counts = new Map(), addrs = new Map();
let total = 0;
const samples = +(process.env.SAMPLES || 20000);
for (let i = 0; i < samples; i++) {
  s.pc.machine.runFor(0.37);
  const p = s.pc.cpu.r[15] >>> 0;
  const rel = p - base;
  let name;
  if (rel >= 0 && rel < 0x40000) { const [n, o] = lookup(rel); name = n; addrs.set(rel, (addrs.get(rel) || 0) + 1); }
  else { name = p >= 0xFFF00000 ? 'BIOS' : p >= 0x100000 ? 'XMS(jit code?)' : 'DOS/other'; if (name === 'DOS/other') { const k = 'pc ' + (p & ~0xFF).toString(16); counts.set(k, (counts.get(k) || 0) + 1); } }
  counts.set(name, (counts.get(name) || 0) + 1); total++;
  if (s.atPrompt()) break;
}
console.log(`base 0x${base.toString(16)}, ${total} samples`);
for (const [n, c] of [...counts].sort((a, b) => b[1] - a[1]).slice(0, 25)) console.log(`${(100 * c / total).toFixed(1).padStart(5)}%  ${n}`);
if (process.argv.includes('--addr')) {
  const dis = execFileSync('arm-none-eabi-objdump', ['-d', elf], { maxBuffer: 64 << 20 }).toString().split('\n');
  const byAddr = new Map();
  for (const l of dis) { const m = l.match(/^\s+([0-9a-f]+):\s+[0-9a-f]+\s+(.*)$/); if (m) byAddr.set(parseInt(m[1], 16), m[2]); }
  for (const [a, c] of [...addrs].sort((x, y) => y[1] - x[1]).slice(0, 60)) {
    const [n, o] = lookup(a);
    console.log(`${(100 * c / total).toFixed(2).padStart(6)}% ${a.toString(16).padStart(6)} ${n}+${o.toString(16)}  ${byAddr.get(a) || ''}`);
  }
}
