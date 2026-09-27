#!/usr/bin/env node
// Exactness test for the JIT's native libgcc division (emu/libgcc.mjs):
// links libgcc's __aeabi_idiv / __aeabi_uidiv / __aeabi_idivmod /
// __aeabi_uidivmod (two multilibs), then calls them with random and edge-case
// operands on the interpreter and on the JIT (native path) and compares
// r0-r3, r12, lr, N/Z/C/V and the number of instructions executed.
// usage: node emu/tests/cpu/divtest.mjs [--n 20000]
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { CPU } from '../../cpu.mjs';
import { JIT } from '../../jit.mjs';
import { rng } from './gen.mjs';

const args = process.argv.slice(2);
const N = args.includes('--n') ? +args[args.indexOf('--n') + 1] : 20000;
const work = mkdtempSync(join(tmpdir(), 'armdos-divtest-'));
let fails = 0, checked = 0, nativeCalls = 0;
for (const flags of [['-march=armv5te', '-mfloat-abi=soft'], ['-march=armv5te', '-mfpu=vfp', '-mfloat-abi=softfp']]) {
  writeFileSync(join(work, 'd.s'), `.global _start\n_start: bl __aeabi_idiv\n bl __aeabi_uidiv\n bl __aeabi_idivmod\n bl __aeabi_uidivmod\n` +
    `.global halt\nhalt: mcr p15, 0, r0, c7, c0, 4\n b halt\n.global __aeabi_idiv0\n__aeabi_idiv0: b halt\n`);
  execFileSync('arm-none-eabi-gcc', [...flags, '-nostdlib', '-Ttext=0x10000', '-o', join(work, 'd.elf'), join(work, 'd.s'), '-lgcc']);
  execFileSync('arm-none-eabi-objcopy', ['-O', 'binary', join(work, 'd.elf'), join(work, 'd.bin')]);
  const nm = execFileSync('arm-none-eabi-nm', [join(work, 'd.elf')]).toString();
  const sym = (n) => parseInt(nm.split('\n').find((l) => l.endsWith(' ' + n)).split(' ')[0], 16);
  const bin = readFileSync(join(work, 'd.bin'));
  const halt = sym('halt');
  const mk = (jit) => {
    const c = new CPU({ read: () => -1, write: () => false });
    if (jit) c.jit = new JIT(c, { threshold: 1 });
    c.m8.set(bin, 0x10000);
    return c;
  };
  const A = mk(false), B = mk(true);
  const R = rng(7 + flags.length);
  const SPEC = [0, 1, 2, 3, -1, -2, 7, 8, 0x7FFFFFFF, 0x80000000 | 0, 0x80000001 | 0, 0x40000000, 0xFFFF, 0x10000, 1000, -1000];
  const val = () => { const k = R() % 6; return k === 0 ? SPEC[R() % SPEC.length] : k === 1 ? (R() & 0xFF) : k === 2 ? (R() & 0xFFFF) : k === 3 ? (R() | 0) >> (R() % 31) : R() | 0; };
  const routines = ['__aeabi_idiv', '__aeabi_uidiv', '__aeabi_idivmod', '__aeabi_uidivmod'].map((n) => [n, sym(n)]);
  for (let k = 0; k < N; k++) {
    const [name, addr] = routines[k % routines.length];
    const a = val(), b = val() || 3;
    const regs = [a, b, R() | 0, R() | 0, 0, 0, 0, 0, 0, 0, 0, 0, R() | 0, 0x7F000, halt, 0];
    const fl = R() & 0xF0000000;
    const out = [];
    for (const c of [A, B]) {
      c.r.set(regs); c.setFlags(fl); c.pc = addr; c.halted = 0;
      const i0 = c.icount;
      for (let g = 0; g < 20 && !c.halted; g++) c.run(10000);
      out.push({ r: [...c.r.slice(0, 4), c.r[12], c.r[14]], cpsr: c.getCPSR() >>> 28, n: c.icount - i0, pc: c.pc });
    }
    checked++;
    const same = JSON.stringify(out[0]) === JSON.stringify(out[1]);
    if (!same && fails++ < 10) console.log(`FAIL ${name}(${a}, ${b}) [${flags.join(' ')}]\n  interp ${JSON.stringify(out[0])}\n  jit    ${JSON.stringify(out[1])}`);
  }
  // make sure the native path was actually used
  let found = 0;
  for (const pg of B.jit.pages) if (pg) for (const r of pg.list) if (r.src.includes('H.udiv') || r.src.includes('H.sdiv')) found++;
  nativeCalls += found;
  // the v5te multilib (softfp) has the CLZ routines; the default multilib's loop version is not native
  if (!found && flags.includes('-mfloat-abi=softfp')) { console.log(`FAIL: native division not recognised for ${flags.join(' ')}`); fails++; }
}
rmSync(work, { recursive: true, force: true });
console.log(`divtest: ${checked} calls compared, ${fails} mismatches (${nativeCalls} regions with native division)`);
process.exit(fails ? 1 : 0);
