// where does ELBOW spend the ARM's time while Second Reality runs?  Samples the
// ARM PC and attributes it to ELBOW's functions (the interpreter, helpers, the
// services) or to the translation cache.
//   node apps/secondreality/tests/prof.mjs "<keys>" "<command>" skipms samples
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';
import { session } from '../../dosutil/tests/harness.mjs';
import { x86base } from '../../x86/tests/x86state.mjs';
const SR = '3rdparty/secondreality';
const files = [{ src: 'build/ELBOW.EXE', dst: 'DOS\\' }, { src: 'build/HIMEM.SYS', dst: 'DOS\\' }];
for (const f of fs.readdirSync(SR)) files.push({ src: `${SR}/${f}`, dst: 'SR\\' });
const s = await session({ name: 'srprof', files, dirs: ['SR'], sizeMB: 64, boot: process.env.MHZ ? { mhz: +process.env.MHZ } : undefined,
  config: 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=30\nSHELL=C:\\T\\TSHELL.EXE\n' });
s.pc.type('CD \\SR\r'); s.waitPrompt();
s.pc.type(process.argv[3] + '\r');
s.pc.run(3000); s.pc.type(process.argv[2]);
s.pc.run(+(process.argv[4] || 20000));
const syms = [];
for (const l of execFileSync('arm-none-eabi-nm', ['-n', '--defined-only', 'build/obj/ELBOW/ELBOW.elf']).toString().split('\n')) {
  const p = l.split(' '); if (p.length === 3 && /[tT]/.test(p[1])) syms.push([parseInt(p[0], 16), p[2]]);
}
const base = x86base(s.pc);
const end = syms.length ? syms[syms.length - 1][0] + base + 4096 : 0;
const counts = new Map(); let n = 0, wfi = 0;
const N = +(process.argv[5] || 4000);
for (let i = 0; i < N; i++) {
  s.pc.machine.runFor(0.37);
  if (s.pc.cpu.halted) { wfi++; continue; }
  const pc = s.pc.cpu.r[15] >>> 0; n++;
  let name = 'other (kernel/BIOS)';
  if (pc >= base && pc < end) { let lo = 0, hi = syms.length - 1; const a = pc - base; while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (syms[mid][0] <= a) lo = mid; else hi = mid - 1; } name = syms[lo][1]; }
  else if (pc >= 0x100000) name = 'translated x86 code (cache)';
  counts.set(name, (counts.get(name) || 0) + 1);
}
console.log(`${N} samples: ${(wfi * 100 / N).toFixed(1)}% idle (WFI)`);
for (const [k, c] of [...counts].sort((a, b) => b[1] - a[1]).slice(0, 25)) console.log(`${(c * 100 / N).toFixed(1).padStart(5)}%  ${k}`);
// why translated blocks end early (jit.c jit_why)
const why = execFileSync('arm-none-eabi-nm', ['build/obj/ELBOW/ELBOW.elf']).toString().split('\n').find((l) => / jit_why$/.test(l));
if (why) {
  const a = base + parseInt(why.split(' ')[0], 16), dv = new DataView(s.pc.cpu.m8.buffer, s.pc.cpu.m8.byteOffset);
  const w = []; for (let k = 0; k < 0x1000; k++) { const v = dv.getUint32(a + k * 4, true); if (v) w.push([k, v]); }
  w.sort((x, y) => y[1] - x[1]);
  console.log('block ends (refused by the translator / decoder):', w.slice(0, 20).map(([k, v]) => `${k & 0x400 ? 'dec ' : ''}${k & 0x200 ? '66 ' : ''}${(k & 0x100) ? '0F ' : ''}${(k & 0xFF).toString(16)}:${v}`).join('  '));
}
