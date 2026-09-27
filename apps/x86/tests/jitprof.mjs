#!/usr/bin/env node
// jitprof.mjs - where does translated code spend its time?  Samples the ARM
// PC while ELBOW /JITDUMP runs a program and attributes samples to x86 blocks.
//   node apps/x86/tests/jitprof.mjs "ELBOW /JITDUMP \X\BENCH86.EXE" [skipms] [samples]
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';
import { session, ROOT } from '../../dosutil/tests/harness.mjs';
import { x86base } from './x86state.mjs';
process.chdir(ROOT);
const files = [{ src: 'build/ELBOW.EXE', dst: 'DOS\\' }];
for (const f of fs.readdirSync('build/x86-test')) files.push({ src: 'build/x86-test/' + f, dst: 'X\\' });
const s = await session({ name: 'x86jp', files, dirs: ['X'] });
s.pc.type('CD \\X\r'); s.waitPrompt();
s.pc.type(process.argv[2].replace(/^ELBOW/, '\\DOS\\ELBOW') + '\r');
s.pc.run(+(process.argv[3] || 3000));
const counts = new Map();
const N = +(process.argv[4] || 5000);
let total = 0;
for (let i = 0; i < N; i++) { s.pc.machine.runFor(0.29); const p = s.pc.cpu.r[15] >>> 0; counts.set(p, (counts.get(p) || 0) + 1); total++; }
const blocks = [];
for (const l of s.pc.debug.split('\n')) {
  const m = l.match(/^JIT ([0-9A-F]+):([0-9A-F]+) (\d+) insns (\d+) words @0x([0-9a-f]+) stub 0x([0-9a-f]+)-0x([0-9a-f]+):(.*)$/);
  if (m) blocks.push({ cs: m[1], ip: m[2], n: +m[3], words: +m[4], at: parseInt(m[5], 16), s0: parseInt(m[6], 16), s1: parseInt(m[7], 16), code: m[8].trim().split(/\s+/) });
}
const per = new Map(); let other = 0, inStub = 0;
for (const [pc, c] of counts) {
  const b = blocks.findLast((x) => pc >= x.at && pc < x.at + x.words * 4);
  if (b) per.set(b, (per.get(b) || 0) + c); else if (blocks.some((x) => pc >= x.s0 && pc < x.s1)) inStub += c; else other += c;
}
console.log(`${total} samples: ${((total - other - inStub) * 100 / total).toFixed(1)}% in blocks, ${(inStub * 100 / total).toFixed(1)}% in stubs, ${(other * 100 / total).toFixed(1)}% elsewhere (helpers/interpreter/DOS)`);
const top = [...per].sort((a, b) => b[1] - a[1]).slice(0, +(process.env.TOP || 4));
for (const [b, c] of top) {
  console.log(`\n=== ${b.cs}:${b.ip}  ${b.n} x86 insns, ${b.words} ARM words, ${(c * 100 / total).toFixed(1)}% of samples`);
  if (process.env.ARM) {
    const bin = Buffer.alloc(b.code.length * 4); b.code.forEach((w, i) => bin.writeUInt32LE(parseInt(w, 16), i * 4));
    fs.writeFileSync('/tmp/jp.bin', bin);
    console.log(execFileSync('arm-none-eabi-objdump', ['-D', '-b', 'binary', '-marm', `--adjust-vma=0x${b.at.toString(16)}`, '/tmp/jp.bin']).toString().split('\n').slice(7).map((l) => { const a = parseInt(l, 16); return (counts.get(a) ? String(counts.get(a)).padStart(5) : '     ') + l; }).join('\n'));
  }
}
const outside = [...counts].filter(([pc]) => !blocks.some((x) => pc >= x.at && pc < x.at + x.words * 4) && !blocks.some((x) => pc >= x.s0 && pc < x.s1)).sort((a, b) => b[1] - a[1]).slice(0, 8);
const base = x86base(s.pc);
const syms = execFileSync('arm-none-eabi-nm', ['-n', '--defined-only', 'build/obj/ELBOW/ELBOW.elf']).toString().split('\n').map((l) => l.split(' ')).filter((q) => q.length === 3 && /[tT]/.test(q[1])).map((q) => [parseInt(q[0], 16), q[2]]);
const fn = (a) => { let r = '?'; for (const [x, n] of syms) { if (x <= a) r = n; else break; } return r; };
const byfn = new Map();
for (const [pc, c] of counts) if (!blocks.some((x) => pc >= x.at && pc < x.at + x.words * 4)) { const n = pc >= base && pc < base + 0x40000 ? fn(pc - base) : pc >= 0xFFF00000 ? 'BIOS' : 'other ' + (pc >>> 16).toString(16); byfn.set(n, (byfn.get(n) || 0) + c); }
console.log('\noutside blocks:', [...byfn].sort((a, b) => b[1] - a[1]).slice(0, 12).map(([n, c]) => `${n} ${(c * 100 / total).toFixed(1)}%`).join(', '));
