#!/usr/bin/env node
// apps/doom/tests/profile.mjs - sample the guest PC during "DOOM -timedemo
// demo1" and print the hottest functions (symbols from DOOM.elf).
// Run apps/doom/tests/run.mjs timedemo first (it builds the disk image).
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const B = (p) => path.join(ROOT, 'build', p);
const pc = await boot({ rom: B('rom.bin'), hd: B('doom-test/timedemo.img') });
pc.until(() => pc.machine.vga.mode === 0x13, { timeoutMs: 60000, stepMs: 50 });
// load base = PSP + 0x100; find it from DOOM's MCB: the image's first word
// is crt0's entry. Simplest: symbols are relative to the load base, which we
// find by searching for the AR1 image's first 64 bytes in memory.
const exe = fs.readFileSync(B('DOOM.EXE'));
const imgOff = exe.readUInt32LE(0x80 + 8);
const sig = exe.subarray(imgOff, imgOff + 64);
const m8 = pc.cpu.m8;
let base = -1;
// (the last match: DOS's disk buffers may hold a copy of the file's start)
for (let a = 0x600; a < 0xA0000; a += 16) if (Buffer.from(m8.subarray(a, a + 64)).equals(sig)) base = a;
if (base < 0) throw new Error('image not found');
const syms = execFileSync('arm-none-eabi-nm', ['-n', '--defined-only', B('obj/DOOM/DOOM.elf')]).toString()
  .split('\n').map((l) => l.split(' ')).filter((p) => p.length === 3 && /[tT]/.test(p[1])).map((p) => [parseInt(p[0], 16), p[2]]);
const counts = new Map();
let total = 0;
const lookup = (a) => { let lo = 0, hi = syms.length - 1; if (a < syms[0][0]) return '?'; while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (syms[mid][0] <= a) lo = mid; else hi = mid - 1; } return syms[lo][1]; };
const samples = +(process.argv[2] || 20000);
for (let i = 0; i < samples && !pc.debug.includes('timed '); i++) {
  pc.machine.runFor(1.3);
  const p = pc.cpu.r[15] >>> 0;
  const rel = p - base;
  const name = rel >= 0 && rel < 0x100000 ? lookup(rel) : (p >= 0xFFF00000 ? 'BIOS' : 'DOS/other');
  counts.set(name, (counts.get(name) || 0) + 1); total++;
}
console.log(`base 0x${base.toString(16)}, ${total} samples`);
for (const [n, c] of [...counts].sort((a, b) => b[1] - a[1]).slice(0, 30)) console.log(`${(100 * c / total).toFixed(1).padStart(5)}%  ${n}`);
