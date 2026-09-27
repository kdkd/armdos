#!/usr/bin/env node
// JIT regression test: program A runs (hot enough to be compiled: loops,
// calls, literal pools), exits; program B is loaded at the same address and
// must run correctly. B is loaded (1) by the host (hostWrite, like floppy
// DMA) and (2) by guest code copying it with LDM/STM and byte stores (like DOS
// reading an EXE into memory). Both ARM and Thumb programs.
// (Regression: stale compiled regions survived the reload - apps/keen/tests/jit-repro.mjs.)
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { CPU } from '../../cpu.mjs';
import { JIT } from '../../jit.mjs';

const work = mkdtempSync(join(tmpdir(), 'armdos-reload-'));
function asm(src, base) {
  writeFileSync(join(work, 'a.s'), '.syntax unified\n' + src + '\n');
  execFileSync('arm-none-eabi-as', ['-march=armv5te', '-o', join(work, 'a.o'), join(work, 'a.s')]);
  execFileSync('arm-none-eabi-ld', ['-Ttext=' + base.toString(16), '-o', join(work, 'a.elf'), join(work, 'a.o')], { stdio: 'ignore' });
  execFileSync('arm-none-eabi-objcopy', ['-O', 'binary', '-j', '.text', join(work, 'a.elf'), join(work, 'a.bin')]);
  return new Uint8Array(readFileSync(join(work, 'a.bin')));
}
const LOAD = 0x20000, COPIER = 0x8000, STAGE = 0x60000, RESULT = 0x7F00;
// a program: r0 = f(K1, K2) computed in a loop with a call and literals; stores
// r0 to RESULT and halts (wfi). Same layout for A and B, different code/literals.
const prog = (k1, k2, op, thumb) => thumb ? `
  .thumb
  .thumb_func
start: ldr r1, =${k1}
  ldr r2, =${k2}
  movs r0, #0
  movs r3, #200
1: bl f
  subs r3, r3, #1
  bne 1b
  ldr r1, =${RESULT}
  str r0, [r1]
  ldr r1, =halt
  bx r1
  .thumb_func
f: ${op === 'add' ? 'adds r0, r0, r1\n  adds r0, r0, r2' : 'eors r0, r0, r1\n  subs r0, r0, r2'}
  bx lr
  .ltorg
  .arm
halt: mov r4, #0
  mcr p15, 0, r4, c7, c0, 4
  b halt
  .ltorg` : `
  .arm
start: ldr r1, =${k1}
  ldr r2, =${k2}
  mov r0, #0
  mov r3, #200
1: bl f
  subs r3, r3, #1
  bne 1b
  ldr r1, =${RESULT}
  str r0, [r1]
halt: mov r4, #0
  mcr p15, 0, r4, c7, c0, 4
  b halt
f: ${op === 'add' ? 'add r0, r0, r1\n  add r0, r0, r2' : 'eor r0, r0, r1\n  sub r0, r0, r2'}
  cmp r0, #0
  addeq r0, r0, #1
  bx lr
  .ltorg`;
const expect = (k1, k2, op) => { let r = 0; for (let i = 0; i < 200; i++) { if (op === 'add') r = (r + k1 + k2) | 0; else r = ((r ^ k1) - k2) | 0; } return r >>> 0; };
const expectArm = (k1, k2, op) => { let r = 0; for (let i = 0; i < 200; i++) { r = op === 'add' ? (r + k1 + k2) | 0 : ((r ^ k1) - k2) | 0; if (r === 0) r = 1; } return r >>> 0; };
// guest copier: copies r1 bytes from STAGE to LOAD with LDM/STM (32 bytes) + a byte tail, then jumps to LOAD | r12
const copier = asm(`
  .arm
  ldr r0, =${STAGE}
  ldr r3, =${LOAD}
  orr r2, r3, r12          @ r12 = 1: enter the program in Thumb state
2: cmp r1, #32
  blt 3f
  ldmia r0!, {r4-r11}
  stmia r3!, {r4-r11}
  sub r1, r1, #32
  b 2b
3: cmp r1, #0
  beq 4f
  ldrb r4, [r0], #1
  strb r4, [r3], #1
  sub r1, r1, #1
  b 3b
4: bx r2
  .ltorg`, COPIER);

let fails = 0, passes = 0;
const eq = (name, got, want) => { if ((got >>> 0) === (want >>> 0)) passes++; else { fails++; console.log(`FAIL ${name}: got ${(got >>> 0).toString(16)} want ${(want >>> 0).toString(16)}`); } };
for (const thumb of [false, true]) for (const how of ['host', 'guest']) for (const threshold of [1, 16]) {
  const cpu = new CPU({ read: () => -1, write: () => false });
  cpu.jit = new JIT(cpu, { threshold });
  cpu.m8.set(copier, COPIER);
  const run = (entry, t) => { cpu.halted = 0; cpu.pc = entry; cpu.t = t; cpu.brk = 1; for (let k = 0; k < 50 && !cpu.halted; k++) cpu.run(100000); };
  const tag = `${thumb ? 'thumb' : 'arm'} ${how} thr${threshold}`;
  const f = thumb ? expect : expectArm;
  // A, then B, then A again (at the same address)
  const progs = [[0x1234567, 0x89, 'add'], [0x7654321, 0x3, 'eor'], [0x55AA55, 0x777, 'add']];
  for (const [k1, k2, op] of progs) {
    const img = asm(prog(k1, k2, op, thumb), LOAD);
    if (how === 'host') { cpu.hostWrite(LOAD, img); run(LOAD, thumb ? 1 : 0); }
    else { cpu.m8.set(img, STAGE); cpu.r[1] = img.length; cpu.r[12] = thumb ? 1 : 0; run(COPIER, 0); }
    eq(`${tag} ${op} ${k1.toString(16)}`, cpu.m32[RESULT >> 2], f(k1, k2, op));
    cpu.m32[RESULT >> 2] = 0;
  }
}
rmSync(work, { recursive: true, force: true });
console.log(`reload: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
