#!/usr/bin/env node
// The game port at 201h (emu/dev/gameport.mjs): bits, one-shot timing from the stick's
// resistance, the 1 us ISA cycle, the Machine option, and guest polling loops run with and
// without the JIT and the spin-loop skip: counting loops must measure the same counts, and
// pure waits (for a one-shot, for a button) must give identical registers, memory,
// instruction counts and time whether they are skipped or executed.
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { Machine } from '../../machine.mjs';

let fails = 0, passes = 0;
const check = (name, ok, extra = '') => { if (ok) passes++; else { fails++; console.log('FAIL ' + name + (extra ? ': ' + extra : '')); } };

// ------------------------------------------------------------------ port level (no CPU)
{
  const m = new Machine({ jit: false, rtcBaseMs: 0 });
  const j = m.joy;
  check('power-on: nothing plugged, idle, no buttons: F0h', m.in8(0x201) === 0xF0, m.in8(0x201).toString(16));
  m.out8(0x201, 0);
  check('fire with nothing plugged: the axes never time out (open circuit)', (m.in8(0x201) & 15) === 15);
  m.nowNs += 50e6;
  check('...still timing 50 ms later', (m.in8(0x201) & 15) === 15);
  m.reset();
  j.plug(0);
  check('stick A plugged: centred (50 kOhm)', j.ohms[0] === 50000 && j.ohms[1] === 50000 && j.ohms[2] === null);
  j.setAxis(0, -1); j.setAxis(1, 1);
  const t0 = m.timeNs();
  m.out8(0x201, 0x55);
  const tf = t0;                                    // the one-shots start at the write
  const at = (ns) => { m.nowNs = tf + ns - (m.timeNs() - m.nowNs); return m.in8(0x201); };
  check('fire: A-X, A-Y timing, B open', (at(1000) & 15) === 15);
  check('A-X at 0 ohm: 24.2 us', (at(24100) & 1) === 1 && (at(24300) & 1) === 0);
  check('A-Y at 100 kOhm: 1124.2 us', (at(1124100) & 2) === 2 && (at(1124300) & 2) === 0);
  j.setAxis(0, 0);
  m.out8(0x201, 0);
  const t1 = m.timeNs() - 1000;                     // (the write's own ISA cycle came after it fired)
  m.nowNs = t1 + 574100 - (m.timeNs() - m.nowNs);
  check('centre: 574.2 us', (m.in8(0x201) & 1) === 1);
  m.nowNs += 1000;
  check('...and done after it', (m.in8(0x201) & 1) === 0);
  // not retriggerable
  j.setAxis(0, 1);
  m.out8(0x201, 0); const t2 = m.timeNs() - 1000;
  m.nowNs += 500000; m.out8(0x201, 0);              // a fire while timing is ignored
  m.nowNs = t2 + 1124300 - (m.timeNs() - m.nowNs);
  check('a fire while a one-shot runs does not restart it', (m.in8(0x201) & 1) === 0);
  // buttons
  j.setButton(0, true); j.setButton(3, true);
  check('buttons: 0 = pressed (A1 bit 4, B2 bit 7)', (m.in8(0x201) & 0xF0) === 0x60, m.in8(0x201).toString(16));
  j.setButton(0, false); j.setButton(3, false);
  check('buttons released', (m.in8(0x201) & 0xF0) === 0xF0);
  // the ISA cycle
  const a = m.timeNs(); m.in8(0x201); m.out8(0x201, 0);
  check('each 201h access takes 1 us of bus time', Math.abs(m.timeNs() - a - 2000) < 1e-6, `${m.timeNs() - a}`);
  j.plug(0);
  m.out8(0x201, 0); j.plug(0, false); m.nowNs += 5e6;
  check('unplugged while timing: the one-shot never ends', (m.in8(0x201) & 1) === 1);
  j.plug(0); j.setAxis(0, -1);
  check('plugged in while the capacitor charges: the pulse ends by the new resistance', (m.in8(0x201) & 1) === 0);
  check('unplug: open circuit, buttons released', (j.plug(0, false), j.ohms[0] === null && j.buttons === 0));
}
{
  const m = new Machine({ jit: false, rtcBaseMs: 0, joystick: false });
  check('joystick: false = no game port (201h floats)', m.in8(0x201) === 0xFF && m.hw.joystick === false);
  m.setHardware({ joystick: true });
  check('setHardware({ joystick: true }) puts it back', m.in8(0x201) === 0xF0);
  const d = new Machine({ jit: false, rtcBaseMs: 0 });
  check('default: game port present', d.hw.joystick === true && d.in8(0x201) === 0xF0);
}

// ------------------------------------------------------------------ guest loops
const work = mkdtempSync(join(tmpdir(), 'armdos-joy-'));
function asm(src) {
  writeFileSync(join(work, 'a.s'), '.syntax unified\n.arm\n' + src + '\n');
  try { execFileSync('arm-none-eabi-as', ['-march=armv5te', '-o', join(work, 'a.o'), join(work, 'a.s')], { stdio: ['ignore', 'pipe', 'pipe'] }); }
  catch (e) { console.log(e.stderr.toString()); process.exit(2); }
  execFileSync('arm-none-eabi-ld', ['-Ttext=0x1000', '-o', join(work, 'a.elf'), join(work, 'a.o')], { stdio: 'ignore' });
  execFileSync('arm-none-eabi-objcopy', ['-O', 'binary', '-j', '.text', join(work, 'a.elf'), join(work, 'a.bin')]);
  return new Uint8Array(readFileSync(join(work, 'a.bin')));
}
// low vectors, IRQ0 at 1 kHz counting at 0x46C (so the loops get interrupted, as in DOS)
const prologue = `
  mrc p15, 0, r0, c1, c0, 0\n bic r0, r0, #0x2000\n mcr p15, 0, r0, c1, c0, 0
  ldr r0, =0xe59ff018\n mov r1, #0\n str r0, [r1, #0x18]\n ldr r0, =irqh\n str r0, [r1, #0x38]
  msr cpsr_c, #0xd2\n ldr sp, =0x9000\n msr cpsr_c, #0xd3\n ldr sp, =0xA000
  ldr r9, =0x10000000
  mov r0, #0x34\n strb r0, [r9, #0x43]\n ldr r0, =1193\n strb r0, [r9, #0x40]\n lsr r0, r0, #8\n strb r0, [r9, #0x40]
  mov r0, #0xFE\n strb r0, [r9, #0x21]
  msr cpsr_c, #0x53
  ldr r10, =0x10000201
  b main
irqh: stmfd sp!, {r0, r1, lr}
  ldr r0, =0x1000001F\n ldrb r1, [r0, #1]
  ldr r0, =0x46C\n ldr r1, [r0]\n add r1, r1, #1\n str r1, [r0]
  ldr r0, =0x10000020\n mov r1, #0x20\n strb r1, [r0]
  ldmfd sp!, {r0, r1, lr}\n subs pc, lr, #4
  .ltorg
main:`;
const programs = {
  // Quake's IN_ReadJoystick: fire, then add the X and Y bits up until both drop; 30 reads
  // stored at 0x2000 (x, y pairs)
  count: { src: `
  mov r5, #0\n ldr r4, =0x2000
c0: strb r0, [r10]\n mov r1, #0\n mov r2, #0
c1: ldrb r0, [r10]\n and r3, r0, #1\n add r1, r1, r3\n and r3, r0, #2\n add r2, r2, r3, lsr #1\n tst r0, #3\n bne c1
  str r1, [r4], #4\n str r2, [r4], #4
  @ let the other one-shots end before the next fire
c2: ldrb r0, [r10]\n tst r0, #3\n bne c2
  add r5, r5, #1\n cmp r5, #30\n bne c0
  mov r7, #0x77
e: b e\n .ltorg`, setup: (m) => { m.joy.plug(0); m.joy.setAxis(0, -0.5); m.joy.setAxis(1, 0.8); } },
  // wait for the one-shot without counting (skippable), 200 times
  wait: { src: `
  mov r5, #0
w0: strb r0, [r10]
w1: ldrb r0, [r10]\n tst r0, #1\n bne w1
  add r5, r5, #1\n cmp r5, #200\n bne w0
  mov r7, #0x77
e: b e\n .ltorg`, setup: (m) => { m.joy.plug(0); m.joy.setAxis(0, 0.9); } },
  // "press button 1": wait for it (the host presses it at 55 ms), then its release (80 ms)
  button: { src: `
b1: ldrb r0, [r10]\n tst r0, #0x10\n bne b1
  ldr r6, =0x46C\n ldr r6, [r6]
b2: ldrb r0, [r10]\n tst r0, #0x10\n beq b2
  ldr r8, =0x46C\n ldr r8, [r8]
  mov r7, #0x77
e: b e\n .ltorg`, setup: (m) => { m.joy.plug(0); }, at: { 50: (m) => m.joy.setButton(0, true), 80: (m) => m.joy.setButton(0, false) } },
};
const hash = (m8) => { let h = 0; for (let i = 0; i < 0x20000; i++) h = (Math.imul(h, 31) + m8[i]) | 0; return h; };
for (const [name, p] of Object.entries(programs)) {
  const img = asm(prologue + p.src);
  const res = {};
  for (const mhz of [100, 12]) for (const jit of [false, true]) for (const spin of [false, true]) {
    const m = new Machine({ jit, spinSkip: spin, mhz, turbo: true, rtcBaseMs: 0 });
    p.setup(m);
    m.cpu.hostWrite(0x1000, img);
    m.cpu.pc = 0x1000;
    let executed = 0;
    for (let k = 0; k < 400 && m.cpu.r[7] !== 0x77; k++) { if (p.at?.[k * 10]) p.at[k * 10](m); executed += m.runFor(10); }
    m.runFor(3);
    const st = { r: [...m.cpu.r.slice(0, 15)], ic: m.cpu.icount, t: m.timeNs(), mem: hash(m.cpu.m8), done: m.cpu.r[7] === 0x77 };
    res[`${mhz}/${jit}/${spin}`] = { st, m, executed: executed - m.spin.stats.skippedInsns, skips: m.spin.stats.skips };
  }
  for (const mhz of [100, 12]) {
    const ref = res[`${mhz}/false/false`];
    check(`${name} @${mhz} MHz: finishes`, ref.st.done);
    // The spin skip is exact: identical to executing every iteration (float noise in t at 12 MHz).
    // The JIT computes identical values; it leaves compiled code at a loop's back edge rather
    // than right after the 201h read that ran past the slice's end (the ISA wait), so its
    // slices, and the interrupts between them, may come one polling iteration (~1 us) later.
    const same = (a, b, tol) => JSON.stringify(a.r) === JSON.stringify(b.r) && a.mem === b.mem && a.done === b.done && Math.abs(a.t - b.t) <= tol;
    const sp = res[`${mhz}/false/true`].st;
    check(`${name} @${mhz}: the spin skip is exact (interpreter)`, same(sp, ref.st, 1e-3) && sp.ic === ref.st.ic, `${JSON.stringify(sp).slice(0, 160)}\n   vs ${JSON.stringify(ref.st).slice(0, 160)}`);
    for (const k of ['true/false', 'true/true']) check(`${name} @${mhz}: jit/spin ${k}: same values, time within 2 us`,
      same(res[`${mhz}/${k}`].st, ref.st, 2000), `${JSON.stringify(res[`${mhz}/${k}`].st).slice(0, 160)}\n   vs ${JSON.stringify(ref.st).slice(0, 160)}`);
  }
  if (name === 'count') {
    for (const mhz of [100, 12]) {
      const m8 = res[`${mhz}/true/true`].m.cpu.m32;
      const xs = [], ys = [];
      for (let i = 0; i < 30; i++) { xs.push(m8[(0x2000 >> 2) + 2 * i]); ys.push(m8[(0x2000 >> 2) + 2 * i + 1]); }
      // 7 instructions + 1 us per poll; x = 25 kOhm -> 299.2 us, y = 90 kOhm -> 1014.2 us
      const per = 1000 + 7000 / mhz;
      const ex = 299200 / per, ey = 1014200 / per;
      const okx = xs.every((v) => Math.abs(v - ex) <= 2), oky = ys.every((v) => Math.abs(v - ey) <= 2);
      check(`count @${mhz} MHz: X counts ~${ex.toFixed(0)} (25 kOhm)`, okx, xs.join(','));
      check(`count @${mhz} MHz: Y counts ~${ey.toFixed(0)} (90 kOhm)`, oky, ys.join(','));
      check(`count @${mhz} MHz: every read the same`, new Set(xs).size === 1 && new Set(ys).size === 1, `${[...new Set(xs)]} / ${[...new Set(ys)]}`);
      console.log(`   count @${mhz} MHz: x ${xs[0]} y ${ys[0]} (expected ${ex.toFixed(1)}, ${ey.toFixed(1)})`);
    }
  }
  if (name === 'button') {
    const r = res['100/true/true'].m.cpu.r;
    check('button: pressed at the 50-60 ms tick, released at 80-90', r[6] >= 50 && r[6] <= 61 && r[8] >= 80 && r[8] <= 91, `${r[6]} ${r[8]}`);
    const on = res['100/true/true'];
    check('button: the wait was skipped (executed < 10%)', on.executed < 0.1 * res['100/false/false'].st.ic, `${on.executed} of ${res['100/false/false'].st.ic}, ${on.skips} skips`);
  }
  if (name === 'wait') check('wait: one-shot waits skipped', res['100/true/true'].skips > 0);
}
rmSync(work, { recursive: true, force: true });
console.log(`gameport: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
