#!/usr/bin/env node
// apps/x86/tests/cputest.mjs - the CPU conformance suite: CPUTn.COM under
// ELBOW.EXE on ARM-DOS against the same program run natively on the host's
// x86 CPU (build/x86-cpu/refN.txt, made by tests/cpu/build.sh).
//   node apps/x86/tests/cputest.mjs [--jit|--nojit] [N...]
import fs from 'node:fs';
import path from 'node:path';
import { session, Checker, ROOT } from '../../dosutil/tests/harness.mjs';

const args = process.argv.slice(2);
const mode = args.includes('--nojit') ? '/NOJIT ' : args.includes('--jit') ? '/JIT ' : '';
const D = path.join(ROOT, 'build/x86-cpu');
const tmpl = fs.readFileSync(path.join(D, 'templates.txt'), 'utf8').trim().split('\n').map((l) => l.split('\t'));
let nums = args.filter((a) => /^\d+$/.test(a)).map(Number);
if (!nums.length) nums = fs.readdirSync(D).filter((f) => /^CPUT\d+\.COM$/.test(f)).map((f) => +f.match(/\d+/)[0]).sort();

const files = [{ src: 'build/ELBOW.EXE', dst: 'DOS\\' }];
for (const n of nums) files.push({ src: `build/x86-cpu/CPUT${n}.COM`, dst: 'X\\' });
const s = await session({ name: 'x86cpu', files, dirs: ['X'] });
const t = new Checker('X86 CPU conformance' + (mode ? ` (${mode.trim()})` : ''));
for (const n of nums) {
  s.run(`\\DOS\\ELBOW ${mode}\\X\\CPUT${n}.COM > \\OUT\\CPUT${n}.TXT`, { timeoutMs: 300000 });
  const got = (s.file(`OUT\\CPUT${n}.TXT`) || Buffer.alloc(0)).toString('latin1').split('\r\n');
  const want = fs.readFileSync(path.join(D, `ref${n}.txt`), 'latin1').split('\r\n');
  const bad = new Map();
  for (let i = 0; i < want.length; i++) {
    if (got[i] === want[i]) continue;
    const ti = parseInt((want[i] || got[i] || '0').slice(0, 4), 16);
    if (!bad.has(ti)) bad.set(ti, { n: 0, first: [got[i], want[i]] });
    bad.get(ti).n++;
  }
  const detail = [...bad].slice(0, 40).map(([ti, b]) =>
    `${(tmpl[ti] || [])[1]} [${(tmpl[ti] || [])[2] || ''}]: ${b.n} cases, e.g.\n  got  ${b.first[0]}\n  want ${b.first[1]}`).join('\n');
  t.ok(bad.size === 0, `CPUT${n}: ${want.length - 1} cases, ${bad.size} templates differ`, detail);
}
t.done();
