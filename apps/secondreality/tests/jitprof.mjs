// the hottest translated x86 blocks while Second Reality runs (ELBOW /JITDUMP):
//   node apps/secondreality/tests/jitprof.mjs "<keys>" "<command incl. /JITDUMP>" skipms samples
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';
import { session } from '../../dosutil/tests/harness.mjs';
import { x86mem } from '../../x86/tests/x86state.mjs';
import os from 'node:os';
const BLK = os.tmpdir() + '/srjp-blk.bin';
const SR = '3rdparty/secondreality';
const files = [{ src: 'build/ELBOW.EXE', dst: 'DOS\\' }, { src: 'build/HIMEM.SYS', dst: 'DOS\\' }];
for (const f of fs.readdirSync(SR)) files.push({ src: `${SR}/${f}`, dst: 'SR\\' });
const s = await session({ name: 'srjp', files, dirs: ['SR'], sizeMB: 64, config: 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=30\nSHELL=C:\\T\\TSHELL.EXE\n' });
s.pc.type('CD \\SR\r'); s.waitPrompt();
s.pc.type(process.argv[3] + '\r');
s.pc.run(3000); s.pc.type(process.argv[2]);
s.pc.run(+process.argv[4]);
const counts = new Map(); const N = +(process.argv[5] || 3000);
for (let i = 0; i < N; i++) { s.pc.machine.runFor(0.29); const p = s.pc.cpu.r[15] >>> 0; counts.set(p, (counts.get(p) || 0) + 1); }
const blocks = [];
for (const l of s.pc.debug.split('\n')) {
  const m = l.match(/^JIT ([0-9A-F]+):([0-9A-F]+) (\d+) insns (\d+) words @0x([0-9a-f]+) stub 0x([0-9a-f]+)-0x([0-9a-f]+):/);
  if (m) blocks.push({ cs: m[1], ip: m[2], n: +m[3], words: +m[4], at: parseInt(m[5], 16) });
}
blocks.sort((a, b) => a.at - b.at);
const find = (pc) => { let lo = 0, hi = blocks.length - 1, r = null; while (lo <= hi) { const mid = (lo + hi) >> 1; if (blocks[mid].at <= pc) { r = blocks[mid]; lo = mid + 1; } else hi = mid - 1; } return r && pc < r.at + r.words * 4 ? r : null; };
const per = new Map(); let inb = 0;
for (const [pc, c] of counts) { const b = find(pc); if (b) { per.set(b, (per.get(b) || 0) + c); inb += c; } }
console.log(`${N} samples, ${(inb * 100 / N).toFixed(1)}% in ${blocks.length} blocks`);
for (const [b, c] of [...per].sort((a, b) => b[1] - a[1]).slice(0, +(process.env.TOP || 8))) {
  const lin = parseInt(b.cs, 16) * 16 + parseInt(b.ip, 16);
  const bytes = x86mem(s.pc, lin, 64);
  fs.writeFileSync(BLK, bytes);
  let dis = '';
  try { dis = execFileSync('ndisasm', ['-b', '16', '-o', '0x' + b.ip, BLK]).toString().split('\n').slice(0, Math.min(40, b.n + 2)).join('\n'); } catch { dis = bytes.toString('hex'); }
  console.log(`\n=== ${b.cs}:${b.ip}  ${b.n} x86 insns -> ${b.words} ARM words (${(b.words / b.n).toFixed(1)}/insn), ${(c * 100 / N).toFixed(1)}% of samples\n${dis}`);
}
