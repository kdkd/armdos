#!/usr/bin/env node
// CPU unit tests for behaviour the QEMU differential cannot check:
// reset state, CP15, unaligned rotation, data/prefetch aborts, IRQ/FIQ entry
// and return, WFI, high vectors. Snippets are assembled with arm-none-eabi-as.
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { CPU, SVC, IRQ, FIQ, ABT, UND } from '../../cpu.mjs';

const useJit = process.argv.includes('--jit');
const JITmod = useJit ? await import('../../jit.mjs') : null;
const work = mkdtempSync(join(tmpdir(), 'armdos-unit-'));
function asm(src, base = 0x1000) {
  writeFileSync(join(work, 'a.s'), '.syntax unified\n.arm\n.fpu vfp\n' + src + '\n');
  execFileSync('arm-none-eabi-as', ['-march=armv5te', '-mfpu=vfp', '-o', join(work, 'a.o'), join(work, 'a.s')]);
  execFileSync('arm-none-eabi-ld', ['-Ttext=' + base.toString(16), '-o', join(work, 'a.elf'), join(work, 'a.o')], { stdio: 'ignore' });
  execFileSync('arm-none-eabi-objcopy', ['-O', 'binary', '-j', '.text', join(work, 'a.elf'), join(work, 'a.bin')]);
  return readFileSync(join(work, 'a.bin'));
}
function mk(src, base = 0x1000) {
  const cpu = new CPU({ read: () => -1, write: () => false });
  if (JITmod) cpu.jit = new JITmod.JIT(cpu, { threshold: 1 });
  cpu.m8.set(asm(src, base), base);
  cpu.pc = base;
  return cpu;
}
// run until pc == stop (or n instructions); uses single steps for precise stops
function runTo(cpu, stop, max = 10000) {
  for (let k = 0; k < max; k++) { if ((cpu.pc >>> 0) === stop >>> 0) return true; if (cpu.run(1) === 0) return false; }
  return false;
}
let fails = 0, passes = 0;
const eq = (name, got, want) => {
  if ((got >>> 0) === (want >>> 0)) passes++;
  else { fails++; console.log(`FAIL ${name}: got ${(got >>> 0).toString(16)} want ${(want >>> 0).toString(16)}`); }
};
const hex = (x) => (x >>> 0).toString(16);

// ---- reset state
{
  const cpu = new CPU({ read: () => -1, write: () => false });
  eq('reset pc', cpu.pc, 0xFFFF0000);
  eq('reset cpsr', cpu.getCPSR(), 0xD3);
}
// ---- CP15
{
  const cpu = mk(`mrc p15, 0, r0, c0, c0, 0\n mrc p15, 0, r1, c1, c0, 0\n bic r2, r1, #0x2000\n mcr p15, 0, r2, c1, c0, 0\n mrc p15, 0, r3, c1, c0, 0\n
    mov r4, #0\n mcr p15, 0, r4, c7, c7, 0\n mcr p15, 0, r4, c7, c10, 4\n mcr p15, 0, r4, c8, c7, 0\n
    1: mrc p15, 0, r15, c7, c10, 3\n bne 1b\n mov r5, #1\n e: b e`);
  runTo(cpu, 0x1000 + 13 * 4);
  eq('cp15 id', cpu.r[0], 0x41069265);
  eq('cp15 ctl V', cpu.r[1] & 0x2000, 0x2000);
  eq('cp15 ctl write', cpu.r[3] & 0x2000, 0);
  eq('test-and-clean loop exits', cpu.r[5], 1);
}
// ---- unaligned LDR rotation, STR alignment, SWP rotation, LDRH/STRH low bit ignored
{
  const cpu = mk(`ldr r0, =0x2000\n ldr r1, =0x44332211\n str r1, [r0]\n ldr r2, [r0, #1]\n ldr r3, [r0, #2]\n ldr r4, [r0, #3]\n
    ldr r5, =0x2005\n ldr r6, =0xAABBCCDD\n str r6, [r5]\n ldr r7, [r0, #4]\n
    ldr r8, =0x2009\n ldr r9, =0x12345678\n str r9, [r0, #8]\n swp r10, r9, [r8]\n ldrh r11, [r0, #1]\n
    ldr r12, =0x5555\n strh r12, [r0, #3]\n ldr r12, [r0]\n e: b e\n .ltorg`);
  runTo(cpu, 0x1000 + 18 * 4);
  eq('ldr +1 rot', cpu.r[2], 0x11443322);
  eq('ldr +2 rot', cpu.r[3], 0x22114433);
  eq('ldr +3 rot', cpu.r[4], 0x33221144);
  eq('str unaligned -> aligned', cpu.r[7], 0xAABBCCDD);
  eq('swp unaligned rot', cpu.r[10], 0x78123456);
  eq('ldrh odd -> aligned', cpu.r[11], 0x2211);
  eq('strh odd -> aligned', cpu.r[12], 0x55552211);
}
// ---- data abort: load with post-index writeback, base restored, LR = pc+8, SPSR, vector
{
  const cpu = mk(`msr cpsr_f, #0x60000000\n ldr r1, =0x20000000\n mov r2, #7\n nop\n ldr r2, [r1], #4\n mov r3, #1\n e: b e\n .ltorg`);
  const abortPc = 0x1000 + 4 * 4;
  runTo(cpu, abortPc); cpu.run(1);
  eq('dabt pc', cpu.pc, 0xFFFF0010);
  eq('dabt mode', cpu.mode, ABT);
  eq('dabt lr', cpu.r[14], abortPc + 8);
  eq('dabt spsr', cpu.spsr, 0x600000D3);
  eq('dabt base restored', cpu.r[1], 0x20000000);
  eq('dabt rd unchanged', cpu.r[2], 7);
  eq('dabt I set', cpu.i, 1);
  eq('dabt FAR', cpu.cp15[6], 0x20000000);
}
// ---- data abort in Thumb: LR = pc+8, SPSR.T
{
  const cpu = mk(`ldr r1, =0x20000000\n adr r0, 1f+1\n bx r0\n .thumb\n1: nop\n ldr r2, [r1]\n nop\n .arm\n .ltorg`);
  const t0 = 0x1000 + 12;
  runTo(cpu, t0 + 2); cpu.run(1);
  eq('thumb dabt lr', cpu.r[14], t0 + 2 + 8);
  eq('thumb dabt spsr.T', cpu.spsr & 0x20, 0x20);
  eq('thumb dabt T cleared', cpu.t, 0);
}
// ---- store abort, STM abort
{
  const cpu = mk(`ldr r1, =0x30000000\n str r0, [r1, #-4]!\n e: b e\n .ltorg`);
  runTo(cpu, 0x1004); cpu.run(1);
  eq('str abort pc', cpu.pc, 0xFFFF0010);
  eq('str abort writeback suppressed', cpu.r[1], 0x30000000);
}
// ---- prefetch abort: jump to unmapped
{
  const cpu = mk(`ldr r0, =0x20000000\n bx r0\n .ltorg`);
  runTo(cpu, 0x20000000); cpu.run(1);
  eq('pabt pc', cpu.pc, 0xFFFF000C);
  eq('pabt lr', cpu.r[14], 0x20000004);
  eq('pabt mode', cpu.mode, ABT);
}
// ---- SVC with high vectors, BKPT -> prefetch abort, undefined
{
  const cpu = mk(`svc #5`);
  cpu.run(1);
  eq('svc vector', cpu.pc, 0xFFFF0008); eq('svc lr', cpu.r[14], 0x1004); eq('svc mode', cpu.mode, SVC);
  const c2 = mk(`bkpt #1`); c2.run(1); eq('bkpt vector', c2.pc, 0xFFFF000C); eq('bkpt lr', c2.r[14], 0x1004);
  const c3 = mk(`.word 0xe7f000f0`); c3.run(1); eq('und vector', c3.pc, 0xFFFF0004); eq('und lr', c3.r[14], 0x1004); eq('und mode', c3.mode, UND);
  const c4 = mk(`mcr p14, 0, r0, c1, c0, 0`); c4.run(1); eq('cp14 -> und', c4.pc, 0xFFFF0004);
  const c5 = mk(`ldc p15, c1, [r0]`); c5.run(1); eq('ldc -> und', c5.pc, 0xFFFF0004);
}
// ---- IRQ entry/return: vector table in RAM at 0 (V=0)
{
  const cpu = mk(`
    mrc p15, 0, r0, c1, c0, 0\n bic r0, r0, #0x2000\n mcr p15, 0, r0, c1, c0, 0
    ldr r0, =0xe59ff018\n mov r1, #0\n str r0, [r1, #0x18]\n ldr r0, =irqh\n str r0, [r1, #0x38]
    str r0, [r1, #0x1c]\n ldr r0, =0xe59ff018\n str r0, [r1, #0x1c]\n ldr r0, =fiqh\n str r0, [r1, #0x3c]
    msr cpsr_c, #0xd2\n ldr sp, =0x8000\n msr cpsr_c, #0xd3
    mov r4, #0\n mov r8, #0x88
    msr cpsr_c, #0x53          @ enable IRQ (F still set)
  loop: add r4, r4, #1\n add r4, r4, #1\n b loop
  irqh: stmfd sp!, {r0, lr}\n mrs r0, spsr\n ldr lr, =0x3000\n str r0, [lr]\n ldmfd sp!, {r0, lr}\n add r5, r5, #1\n subs pc, lr, #4
  fiqh: mov r8, #0x99\n add r6, r6, #1\n subs pc, lr, #4
  .ltorg`);
  // run into the loop
  for (let k = 0; k < 60 && cpu.i !== 0; k++) cpu.run(1);
  cpu.run(5);
  const r4 = cpu.r[4];
  cpu.irqLine = 1;
  cpu.run(1);                      // takes IRQ first, then executes 1 handler insn
  eq('irq mode', cpu.mode, IRQ);
  cpu.irqLine = 0;
  for (let k = 0; k < 20 && cpu.mode !== SVC; k++) cpu.run(1);
  eq('irq returned to svc', cpu.mode, SVC);
  eq('irq handler ran', cpu.r[5], 1);
  eq('irq spsr saved I=0', cpu.m32[0x3000 >> 2] & 0x80, 0);
  eq('irq resumed loop', (cpu.pc >>> 0) >= 0x1000, 1);
  cpu.run(10);
  eq('loop continues', cpu.r[4] > r4 ? 1 : 0, 1);
  // FIQ masked while F=1
  cpu.fiqLine = 1; cpu.run(3); eq('fiq masked', cpu.r[6], 0);
  // enable FIQ: banked r8
  cpu.setCPSR(cpu.getCPSR() & ~0x40, 1);
  cpu.run(2);
  cpu.fiqLine = 0;
  for (let k = 0; k < 20 && cpu.mode !== SVC; k++) cpu.run(1);
  eq('fiq handler ran', cpu.r[6], 1);
  eq('fiq r8 banked', cpu.r[8], 0x88);
  eq('fiq bank kept 0x99', cpu.fiqBank[0], 0x99);
}
// ---- WFI: halts; IRQ line wakes even if masked; with I=0 takes IRQ, LR -> after WFI
{
  const cpu = mk(`mov r0, #0\n mcr p15, 0, r0, c7, c0, 4\n mov r1, #1\n mov r2, #2\n e: b e`);
  const n = cpu.run(100);
  eq('wfi halts after 2 insns', n, 2);
  eq('wfi halted', cpu.halted, 1);
  eq('wfi stays halted', cpu.run(100), 0);
  cpu.irqLine = 1;                  // I=1: wake without taking the IRQ
  cpu.run(1);
  eq('wfi woke (masked)', cpu.r[1], 1);
  const c2 = mk(`msr cpsr_c, #0x53\n mov r0, #0\n mcr p15, 0, r0, c7, c0, 4\n mov r1, #1\n e: b e`);
  c2.run(100);
  c2.irqLine = 1; c2.run(1);
  eq('wfi irq taken', c2.mode, IRQ);
  eq('wfi irq lr', c2.r[14], 0x1000 + 12 + 4);
}
// ---- LDM with ^ and PC: exception return restores CPSR (incl. T)
{
  const cpu = mk(`ldr sp, =0x8000\n ldr r0, =0x600000f3\n msr spsr_cxsf, r0\n adr r1, t+1\n stmfd sp!, {r1}\n ldmfd sp!, {pc}^\n .thumb\n t: movs r2, #3\n .arm\n .ltorg`);
  for (let k = 0; k < 7; k++) cpu.run(1);
  eq('ldm^ restored T', cpu.t, 1);
  eq('ldm^ restored C (movs cleared Z)', cpu.getCPSR() >>> 28, 2);
  eq('ldm^ thumb insn ran', cpu.r[2], 3);
}
// ---- MSR in user mode cannot change mode or I/F
{
  const cpu = mk(`msr cpsr_c, #0x10\n msr cpsr_c, #0xd3\n mrs r0, cpsr\n e: b e`);
  cpu.run(3);
  eq('user msr ignored', cpu.r[0] & 0xFF, 0x10);
}
// ---- LR offsets from Thumb: SVC LR = next, undefined LR = next
{
  const cpu = mk(`adr r0, 1f+1\n bx r0\n .thumb\n1: svc #7\n .arm`);
  cpu.run(3);
  eq('thumb svc lr', cpu.r[14], 0x1008 + 2);
  eq('thumb svc spsr T', cpu.spsr & 0x20, 0x20);
}

// ---- self-modifying code / reloading code (JIT invalidation)
{
  const cpu = mk(`
    ldr sp, =0x8000
    mov r4, #0
 mov r5, #100
  1: bl 0x3000
 add r4, r4, r0
 subs r5, r5, #1
 bne 1b
    ldr r1, =0x3000
 ldr r2, =0xe3a00002
 str r2, [r1]      @ patch: mov r0, #2
    mov r5, #100
  2: bl 0x3000
 add r4, r4, r0
 subs r5, r5, #1
 bne 2b
    ldr r1, =0x3004
 ldr r2, =0xe2800005
 str r2, [r1]      @ patch the 2nd insn: add r0, r0, #5
    ldr r2, =0xe12fff1e
 str r2, [r1, #4]                     @ bx lr
    mov r5, #10
  3: bl 0x3000
 add r4, r4, r0
 subs r5, r5, #1
 bne 3b
  e: b e
 .ltorg`);
  cpu.m8.set(asm(`mov r0, #1
 bx lr`, 0x3000), 0x3000);
  cpu.run(20000);
  eq('SMC: patched callee seen', cpu.r[4], 100 + 200 + 70);
}
{
  // code replaced by the host (loader / DMA path): hostWrite invalidates
  const cpu = mk(`ldr sp, =0x8000
 mov r4, #0
 mov r5, #50
 1: bl 0x3000
 add r4, r4, r0
 subs r5, r5, #1
 bne 1b
 e: b e
 .ltorg`);
  cpu.m8.set(asm(`mov r0, #1
 bx lr`, 0x3000), 0x3000);
  cpu.run(100);
  cpu.hostWrite(0x3000, asm(`mov r0, #3
 bx lr`, 0x3000));
  cpu.run(2000);
  eq('hostWrite replaces compiled code', cpu.r[4] > 50 && cpu.r[4] < 150 && cpu.r[4] % 2 === 0 ? 1 : 0, 1);
  eq('hostWrite: loop finished', cpu.r[5], 0);
}
{
  // a data store into the same 4 KB page as hot code must not break it
  const cpu = mk(`ldr r1, =0x1800
 mov r4, #0
 mov r5, #1000
 1: str r4, [r1], #4
 add r4, r4, #1
 subs r5, r5, #1
 bne 1b
 e: b e
 .ltorg`);
  cpu.run(5000);
  eq('data next to code', cpu.r[4], 1000);
  eq('data next to code: memory', cpu.m32[(0x1800 >> 2) + 999], 999);
}

{
  // folded PC-relative literal: a store to just the literal word must be seen by compiled code
  const cpu = mk(`ldr sp, =0x8000\n mov r4, #0\n mov r5, #100
  1: bl f\n add r4, r4, r0\n subs r5, r5, #1\n bne 1b
    adr r1, lit\n mov r2, #5\n str r2, [r1]
    mov r5, #100
  2: bl f\n add r4, r4, r0\n subs r5, r5, #1\n bne 2b
  e: b e
  f: ldr r0, lit\n bx lr
  lit: .word 1
  .ltorg`);
  cpu.run(20000);
  eq('literal patched by a store', cpu.r[4], 100 + 500);
}

{
  // a store made while the JIT is detached (cpu.step(), spin-skip verification)
  // must still invalidate compiled code (apps/keyb one-boot regression)
  const cpu = mk(`ldr sp, =0x8000\n mov r4, #0\n mov r5, #50\n 1: bl 0x3000\n add r4, r4, r0\n subs r5, r5, #1\n bne 1b
    ldr r1, =0x3000\n ldr r2, =0xe3a00009\n str r2, [r1]\n mov r5, #50\n 2: bl 0x3000\n add r4, r4, r0\n subs r5, r5, #1\n bne 2b\n e: b e\n .ltorg`);
  cpu.m8.set(asm(`mov r0, #1\n bx lr`, 0x3000), 0x3000);
  const stopAt = 0x1000 + 9 * 4;                 // the "str r2, [r1]"
  for (let k = 0; k < 1000 && cpu.r[4] < 38; k++) cpu.run(130);           // compiled loop + callee
  for (let k = 0; k < 1000 && (cpu.pc >>> 0) !== stopAt; k++) cpu.run(1);
  const compiled = !!(cpu.jit && cpu.jit.pages[3] && cpu.jit.pages[3].ab[0]);
  cpu.step();                                     // the store, with the JIT detached
  cpu.run(5000);
  eq('store while the JIT is detached invalidates', cpu.r[4], 50 + 9 * 50);
  if (cpu.jit) eq('(the callee had been compiled)', compiled ? 1 : 0, 1);
}

// ---- VFP (VFP9-S / VFPv2)
{
  // disabled at reset: VFP instructions are undefined; FPSID/FPEXC still accessible (privileged)
  const cpu = mk(`vmrs r0, fpsid\n vmrs r1, fpexc\n vadd.f32 s0, s1, s2`);
  cpu.run(2);
  eq('FPSID', cpu.r[0], 0x41011090);
  eq('FPEXC reset', cpu.r[1], 0);
  eq('VFP op with EN=0 -> undefined', (cpu.run(1), cpu.pc), 0xFFFF0004);
  const c2 = mk(`vmrs r0, fpscr`); c2.run(1); eq('FMRX FPSCR with EN=0 -> undefined', c2.pc, 0xFFFF0004);
  // user mode: FPEXC is privileged, FPSCR and data processing are not
  const c3 = mk(`mov r0, #0x40000000\n vmsr fpexc, r0\n msr cpsr_c, #0x10\n vmrs r1, fpscr\n vmrs r2, fpsid\n vadd.f32 s0, s1, s2\n vmrs r3, fpexc`);
  c3.run(6); eq('user FPSCR/FPSID/VADD ok', c3.pc, 0x1000 + 24); c3.run(1);
  eq('user FPEXC -> undefined', c3.pc, 0xFFFF0004);
}
const vfpMk = (body) => mk(`mov r0, #0x40000000\n vmsr fpexc, r0\n ${body}\n e: b e\n .ltorg`);
{
  // arithmetic, conversions, transfers, FMSTAT
  const cpu = vfpMk(`ldr r0, =0x40490fdb\n vmov s0, r0\n vcvt.f64.f32 d1, s0\n vmul.f64 d2, d1, d1\n vcvt.s32.f64 s6, d2\n vmov r1, s6
    vcmp.f64 d2, d1\n vmrs APSR_nzcv, fpscr\n movgt r2, #1\n movle r2, #2\n vmov r4, r5, d2`);
  cpu.run(40);
  eq('pi^2 truncated', cpu.r[1], 9);
  eq('FMSTAT gt', cpu.r[2], 1);
  eq('FMRRD hi', cpu.r[5], 0x4023BD3C);
}
{
  // short vectors (LEN=3, stride 1): s8..s11 = s16..s19 + s24 (scalar m in bank 3? no: m in bank 0 -> scalar)
  const cpu = vfpMk(`ldr r0, =0x3f800000\n vmov s0, r0\n ldr r1, =0x40000000\n vmov s16, r1\n vmov s17, r0\n vmov s18, r1\n vmov s19, r0
    ldr r2, =0x00030000\n vmsr fpscr, r2\n vadd.f32 s8, s16, s0\n vabs.f32 s12, s16\n mov r3, #0\n vmsr fpscr, r3\n vmov r4, r5, s8, s9\n vmov r6, r7, s10, s11\n vmov r8, s15`);
  cpu.run(40);
  eq('vector add el0 (2+1)', cpu.r[4], 0x40400000); eq('vector add el1 (1+1)', cpu.r[5], 0x40000000);
  eq('vector add el2', cpu.r[6], 0x40400000); eq('vector add el3', cpu.r[7], 0x40000000);
  eq('vector unary writes the destination vector (s12..s15)', cpu.r[8], 0x3f800000);
  // doubles, stride 2 (STRIDE=0b11): d4, d6 = d8, d10 * d12 (vector) ; bank 1 wraps
  const c2 = vfpMk(`ldr r0, =0x40000000\n mov r1, #0\n vmov d8, r1, r0\n vmov d10, r1, r0\n vmov d12, r1, r0\n vmov d14, r1, r0
    ldr r2, =0x00310000\n vmsr fpscr, r2\n vmul.f64 d4, d8, d12\n mov r3, #0\n vmsr fpscr, r3\n vmov r4, r5, d4\n vmov r6, r7, d6`);
  c2.run(40);
  eq('double vector stride 2 el0', c2.r[5], 0x40100000); eq('double vector stride 2 el1', c2.r[7], 0x40100000);
}
{
  // exact-flags mode: inexact and underflow are tracked; directed rounding
  const cpu = vfpMk(`ldr r0, =0x3f800000\n vmov s0, r0\n ldr r0, =0x40400000\n vmov s1, r0\n vdiv.f32 s2, s0, s1\n vmrs r1, fpscr
    ldr r2, =0x00C00000\n vmsr fpscr, r2\n vdiv.f32 s3, s0, s1\n vmov r3, s2\n vmov r4, s3`);
  cpu.setVfpExactFlags(true);
  cpu.run(40);
  eq('exact flags: 1/3 inexact (IXC)', cpu.r[1] & 0x10, 0x10);
  eq('1/3 RN', cpu.r[3], 0x3eaaaaab); eq('1/3 RZ', cpu.r[4], 0x3eaaaaaa);
}

rmSync(work, { recursive: true, force: true });
console.log(`unit: ${passes} passed, ${fails} failed${useJit ? ' (jit)' : ''}`);
process.exit(fails ? 1 : 0);
