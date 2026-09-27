#!/usr/bin/env node
// Spin-loop idle skip (emu/spin.mjs): polling loops are fast-forwarded, and the
// result must be *identical* to executing every iteration: same registers,
// memory, instruction count and emulated time, with and without the JIT.
// Guest programs: vertical-retrace waits with DAC writes (a palette fade),
// a BIOS-tick poll (IRQ0 at 1 kHz increments 0x46C), a port 61h refresh-toggle
// wait, and a loop that counts while polling (must not be skipped).
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { Machine } from '../../machine.mjs';

const work = mkdtempSync(join(tmpdir(), 'armdos-spin-'));
function asm(src) {
  writeFileSync(join(work, 'a.s'), '.syntax unified\n.arm\n' + src + '\n');
  try { execFileSync('arm-none-eabi-as', ['-march=armv5te', '-o', join(work, 'a.o'), join(work, 'a.s')], { stdio: ['ignore', 'pipe', 'pipe'] }); }
  catch (e) { console.log(e.stderr.toString()); process.exit(2); }
  execFileSync('arm-none-eabi-ld', ['-Ttext=0x1000', '-o', join(work, 'a.elf'), join(work, 'a.o')], { stdio: 'ignore' });
  execFileSync('arm-none-eabi-objcopy', ['-O', 'binary', '-j', '.text', join(work, 'a.elf'), join(work, 'a.bin')]);
  return new Uint8Array(readFileSync(join(work, 'a.bin')));
}
// common startup: low vectors, IRQ handler (tick counter at 0x46C, EOI), PIT at 1 kHz, IRQ0 unmasked
const prologue = `
  mrc p15, 0, r0, c1, c0, 0\n bic r0, r0, #0x2000\n mcr p15, 0, r0, c1, c0, 0
  ldr r0, =0xe59ff018\n mov r1, #0\n str r0, [r1, #0x18]\n ldr r0, =irqh\n str r0, [r1, #0x38]
  msr cpsr_c, #0xd2\n ldr sp, =0x9000\n msr cpsr_c, #0xd3\n ldr sp, =0xA000
  ldr r9, =0x10000000
  mov r0, #0x34\n strb r0, [r9, #0x43]\n ldr r0, =1193\n strb r0, [r9, #0x40]\n lsr r0, r0, #8\n strb r0, [r9, #0x40]
  mov r0, #0xFE\n strb r0, [r9, #0x21]
  msr cpsr_c, #0x53
  b main
irqh: stmfd sp!, {r0, r1, lr}
  ldr r0, =0x1000001F\n ldrb r1, [r0, #1]          @ ack (read 0x20)
  ldr r0, =0x46C\n ldr r1, [r0]\n add r1, r1, #1\n str r1, [r0]
  ldr r0, =0x10000020\n mov r1, #0x20\n strb r1, [r0]
  ldmfd sp!, {r0, r1, lr}\n subs pc, lr, #4
  .ltorg
main:`;
const programs = {
  fade: `
  ldr r10, =0x100003DA\n mov r5, #0\n mov r6, #0
f0: mov r0, #0\n strb r0, [r9, #0x3C8]\n mov r2, #48
f1: strb r6, [r9, #0x3C9]\n subs r2, r2, #1\n bne f1
  @ wait for the end of retrace, then its start
1: ldrb r0, [r10]\n tst r0, #8\n bne 1b
2: ldrb r0, [r10]\n tst r0, #8\n beq 2b
  add r6, r6, #1\n and r6, r6, #63
  add r5, r5, #1\n cmp r5, #40\n bne f0
  mov r7, #0x77
e: b e\n .ltorg`,
  tick: `
  mov r5, #0\n ldr r4, =0x46C
t0: ldr r1, [r4]
t1: ldr r2, [r4]\n cmp r1, r2\n beq t1
  add r5, r5, #1\n cmp r5, #150\n bne t0
  mov r7, #0x77
e: b e\n .ltorg`,
  refresh: `
  ldr r10, =0x10000061\n mov r5, #0
r0: ldrb r0, [r10]\n and r1, r0, #0x10
r1: ldrb r0, [r10]\n and r0, r0, #0x10\n cmp r0, r1\n beq r1
  add r5, r5, #1\n ldr r3, =3000\n cmp r5, r3\n bne r0
  mov r7, #0x77
e: b e\n .ltorg`,
  counting: `
  ldr r10, =0x100003DA\n mov r5, #0\n mov r6, #0
c1: ldrb r0, [r10]\n add r6, r6, #1\n tst r0, #8\n beq c1
c2: ldrb r0, [r10]\n add r6, r6, #1\n tst r0, #8\n bne c2
  add r5, r5, #1\n cmp r5, #10\n bne c1
  mov r7, #0x77
e: b e\n .ltorg`,
};

let fails = 0, passes = 0;
const check = (name, ok, extra = '') => { if (ok) passes++; else { fails++; console.log('FAIL ' + name + (extra ? ': ' + extra : '')); } };
const hash = (m8) => { let h = 0; for (let i = 0; i < 0x20000; i++) h = (Math.imul(h, 31) + m8[i]) | 0; return h; };
for (const [name, body] of Object.entries(programs)) {
  const img = asm(prologue + body);
  const res = {};
  for (const jit of [false, true]) for (const spin of [false, true]) {
    const m = new Machine({ jit, spinSkip: spin });
    m.cpu.hostWrite(0x1000, img);
    m.cpu.pc = 0x1000;
    const h0 = performance.now();
    let executed = 0;
    for (let k = 0; k < 400 && m.cpu.r[7] !== 0x77; k++) executed += m.runFor(10);
    m.runFor(3);   // a little past the end: same final time for all
    const st = { r: [...m.cpu.r.slice(0, 15)], ic: m.cpu.icount, t: m.timeNs(), mem: hash(m.cpu.m8), done: m.cpu.r[7] === 0x77 };
    res[`${jit}/${spin}`] = { st, executed: executed - m.spin.stats.skippedInsns, ms: performance.now() - h0, skips: m.spin.stats.skips };
  }
  const ref = JSON.stringify(res['false/false'].st);
  check(`${name}: finishes`, res['false/false'].st.done);
  for (const k of ['false/true', 'true/false', 'true/true']) check(`${name}: jit/spin ${k} identical to interpreter without skip`, JSON.stringify(res[k].st) === ref,
    `${JSON.stringify(res[k].st).slice(0, 200)}\n   vs ${ref.slice(0, 200)}`);
  const on = res['true/true'], off = res['true/false'];
  console.log(`   ${name.padEnd(9)} ${(res['false/false'].st.ic / 1e6).toFixed(2)}M instructions of emulated work; executed with skip: ${(on.executed / 1e6).toFixed(2)}M (${on.skips} skips), ` +
    `host ${off.ms.toFixed(0)} ms -> ${on.ms.toFixed(0)} ms`);
  if (name === 'counting') check(`${name}: not skipped (the loop counts)`, on.skips === 0, `${on.skips} skips`);
  else check(`${name}: skipped (executed < 10% of the work)`, on.executed < 0.1 * res['false/false'].st.ic, `${on.executed}`);
}
rmSync(work, { recursive: true, force: true });
console.log(`spin: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
