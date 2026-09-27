#!/usr/bin/env node
// JIT regression test (apps/keyb/tests/run.mjs KEYB_ONE_BOOT=1): many different
// programs loaded one after another at the same address, while a resident
// "TSR" (the timer interrupt handler, which also calls into the loaded
// program's memory) keeps running, in a real Machine with the JIT and the
// spin-loop skip. The guest loader copies each image with LDM/STM + byte
// loops long enough for spin-skip verification windows (which single-step
// with the JIT detached) to land inside the copy - stores made there must
// still invalidate compiled code (the bug: they only cleared the line flags,
// so stale compiled code survived and ran the previous program).
// Every program's result is checked, and the whole run must equal jit: false.
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { Machine } from '../../machine.mjs';

const work = mkdtempSync(join(tmpdir(), 'armdos-tsr-'));
function asm(src, base) {
  writeFileSync(join(work, 'a.s'), '.syntax unified\n.arm\n' + src + '\n');
  try { execFileSync('arm-none-eabi-as', ['-march=armv5te', '-o', join(work, 'a.o'), join(work, 'a.s')], { stdio: ['ignore', 'pipe', 'pipe'] }); }
  catch (e) { console.log(e.stderr.toString()); process.exit(2); }
  execFileSync('arm-none-eabi-ld', ['-Ttext=' + base.toString(16), '-o', join(work, 'a.elf'), join(work, 'a.o')], { stdio: 'ignore' });
  execFileSync('arm-none-eabi-objcopy', ['-O', 'binary', '-j', '.text', join(work, 'a.elf'), join(work, 'a.bin')]);
  return new Uint8Array(readFileSync(join(work, 'a.bin')));
}
const LOAD = 0x20000, STAGE = 0x40000, SLOT = 0x4000, RESULTS = 0x7000, N = 40;
// the loader + the resident timer handler ("TSR": counts ticks at 0x6000 and calls
// LOAD+4, the loaded program's tick hook, like a TSR chaining into a program)
const loader = asm(`
  mrc p15, 0, r0, c1, c0, 0\n bic r0, r0, #0x2000\n mcr p15, 0, r0, c1, c0, 0
  ldr r0, =0xe59ff018\n mov r1, #0\n str r0, [r1, #0x18]\n ldr r0, =irqh\n str r0, [r1, #0x38]
  msr cpsr_c, #0xd2\n ldr sp, =0x9000\n msr cpsr_c, #0xd3\n ldr sp, =0xA000
  ldr r9, =0x10000000
  mov r0, #0x34\n strb r0, [r9, #0x43]\n ldr r0, =1193\n strb r0, [r9, #0x40]\n lsr r0, r0, #8\n strb r0, [r9, #0x40]
  mov r0, #0xFE\n strb r0, [r9, #0x21]
  msr cpsr_c, #0x53
  mov r8, #0                          @ program number
next:
  ldr r0, =${STAGE}\n add r0, r0, r8, lsl #14   @ source slot
  ldr r3, =${LOAD}
  ldr r1, =${SLOT - 3}                @ bytes (not a multiple of 32: byte tail)
2: cmp r1, #32\n blt 3f\n ldmia r0!, {r4-r7, r10-r12, r14}\n stmia r3!, {r4-r7, r10-r12, r14}\n sub r1, r1, #32
  ldrb r4, [r9, #0x21]                                @ poll a status port (like a loader): spin checks fire here
  b 2b
3: cmp r1, #0\n beq 4f\n ldrb r4, [r0], #1\n strb r4, [r3], #1\n sub r1, r1, #1\n b 3b
4: ldr r0, =0x6004\n mov r1, #1\n str r1, [r0]         @ the TSR may call the program now
  ldr r2, =${LOAD}\n blx r2                           @ run it: result in r0
  ldr r1, =${RESULTS}\n str r0, [r1, r8, lsl #2]
  ldr r0, =0x6004\n mov r1, #0\n str r1, [r0]
  add r8, r8, #1\n cmp r8, #${N}\n bne next
  mov r7, #0x77
e: b e
irqh: stmfd sp!, {r0-r3, r12, lr}
  ldr r0, =0x1000001F\n ldrb r1, [r0, #1]           @ acknowledge (read port 20h)
  ldr r0, =0x6000\n ldr r1, [r0]\n add r1, r1, #1\n str r1, [r0]
  ldr r1, [r0, #4]\n cmp r1, #0\n beq 1f
  ldr r2, =${LOAD + 4}\n blx r2                     @ the program's tick hook
1: ldr r0, =0x10000020\n mov r1, #0x20\n strb r1, [r0]
  ldmfd sp!, {r0-r3, r12, lr}\n subs pc, lr, #4
  .ltorg`, 0x8000);
// program i: entry at +0 computes f_i over a loop (with a call and literals),
// tick hook at +4 adds i to 0x6008; padded with code to ~16 KB
const prog = (i) => {
  const k1 = (0x1000193 * (i + 1)) >>> 0, k2 = (i * 77 + 5) >>> 0, op = ['add', 'eor', 'sub'][i % 3];
  const body = op === 'add' ? 'add r0, r0, r1\n add r0, r0, r2' : op === 'eor' ? 'eor r0, r0, r1\n add r0, r0, r2' : 'sub r0, r0, r1\n eor r0, r0, r2';
  return { src: `
  b start
  b tick
  .space ${0x178 + 4 * (i % 8)}                    @ the hot code where the first spin checks of the next copy land
start: stmfd sp!, {r4, lr}
  ldr r1, =${k1}\n ldr r2, =${k2}\n mov r0, #${i}\n ldr r4, =30000
1: bl f\n subs r4, r4, #1\n bne 1b
  ldmfd sp!, {r4, pc}
f: ${body}
  bx lr
tick: ldr r0, =0x6008\n ldr r1, [r0]\n add r1, r1, #${i + 1}\n str r1, [r0]\n bx lr
  .ltorg
  .rept ${3000 + i * 7}
  add r${(i % 7) + 1}, r${(i % 7) + 1}, #${i + 1}
  .endr
  bx lr`, k1, k2, op };
};
const expect = ({ k1, k2, op }, i) => { let r = i; for (let n = 0; n < 30000; n++) r = op === 'add' ? (r + k1 + k2) | 0 : op === 'eor' ? ((r ^ k1) + k2) | 0 : ((r - k1) ^ k2) | 0; return r >>> 0; };
const images = [], want = [];
for (let i = 0; i < N; i++) { const p = prog(i); const img = asm(p.src, LOAD); if (img.length > SLOT) throw new Error('image too big'); images.push(img); want.push(expect(p, i)); }

let fails = 0, passes = 0;
const check = (name, ok, extra = '') => { if (ok) passes++; else { fails++; console.log('FAIL ' + name + (extra ? ': ' + extra : '')); } };
const res = {};
for (const jit of [true, false]) {
  const m = new Machine({ jit });
  m.cpu.hostWrite(0x8000, loader);
  for (let i = 0; i < N; i++) m.cpu.hostWrite(STAGE + i * SLOT, images[i]);
  m.cpu.pc = 0x8000;
  for (let k = 0; k < 3000 && m.cpu.r[7] !== 0x77 && !m.cpu.lastFault; k++) m.runFor(10);
  const got = [...m.cpu.m32.subarray(RESULTS >> 2, (RESULTS >> 2) + N)].map((x) => x >>> 0);
  const bad = got.map((g, i) => (g === want[i] ? -1 : i)).filter((i) => i >= 0);
  check(`${jit ? 'jit' : 'interpreter'}: all ${N} programs ran correctly`, m.cpu.r[7] === 0x77 && bad.length === 0,
    `finished ${m.cpu.r[7] === 0x77}, fault ${JSON.stringify(m.cpu.lastFault)}, wrong programs ${bad.slice(0, 8).join(',')}`);
  check(`${jit ? 'jit' : 'interpreter'}: the TSR ran and called the programs`, m.cpu.m32[0x6000 >> 2] > 10 && m.cpu.m32[0x6008 >> 2] > 0);
  res[jit] = { r: [...m.cpu.r.slice(0, 15)], ic: m.cpu.icount, ticks: m.cpu.m32[0x6000 >> 2], hook: m.cpu.m32[0x6008 >> 2], got,
    spin: m.spin.stats.attempts };
}
check('jit run identical to the interpreter run', JSON.stringify(res.true) === JSON.stringify({ ...res.false, spin: res.true.spin }),
  `${JSON.stringify(res.true).slice(0, 160)}\n   vs ${JSON.stringify(res.false).slice(0, 160)}`);
check('spin-skip verification windows happened', res.true.spin > 0);
rmSync(work, { recursive: true, force: true });
console.log(`tsr-reload: ${passes} passed, ${fails} failed (${res.true.spin} spin checks)`);
process.exit(fails ? 1 : 0);
