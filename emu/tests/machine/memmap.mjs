#!/usr/bin/env node
// Memory activity map (emu/memmap.mjs + jit.mjs runAct): counters land in the
// right cells, sampled estimates are unbiased, detaching leaves plain code, and
// the region table of a booted ARM-DOS names its parts.
// usage: node emu/tests/machine/memmap.mjs   (the boot part needs build/rom.bin + build/hd.img)
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync, existsSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { Machine } from '../../machine.mjs';
import { MemActivity, memoryRegions, regionAt, cellOf, IO0, ROM0 } from '../../memmap.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const work = mkdtempSync(join(tmpdir(), 'armdos-memmap-'));
function asm(src, base, thumb = false) {
  writeFileSync(join(work, 'a.s'), `.syntax unified\n${thumb ? '.thumb' : '.arm'}\n` + src + '\n');
  execFileSync('arm-none-eabi-as', ['-march=armv5te', '-o', join(work, 'a.o'), join(work, 'a.s')]);
  execFileSync('arm-none-eabi-ld', ['-Ttext=' + base.toString(16), '-o', join(work, 'a.elf'), join(work, 'a.o')], { stdio: 'ignore' });
  execFileSync('arm-none-eabi-objcopy', ['-O', 'binary', '-j', '.text', join(work, 'a.elf'), join(work, 'a.bin')]);
  return readFileSync(join(work, 'a.bin'));
}
let fails = 0, passes = 0;
const ok = (cond, what) => { if (cond) passes++; else { fails++; console.log('FAIL ' + what); } };
const sum = (arr, a, b) => { let s = 0; for (let i = a; i < b; i++) s += arr[i]; return s; };
const total = (arr) => sum(arr, 0, arr.length);

// Fill 0x80000-0x83FFF with words and sum 0x20000-0x27FFF, 40 times; poke port E9h each round.
const ARM = `
  mov r9, #40
round:
  ldr r0, =0x80000
  ldr r1, =0x84000
  mov r2, #7
fill: str r2, [r0], #4
  cmp r0, r1
  bne fill
  ldr r0, =0x20000
  ldr r1, =0x28000
  mov r3, #0
sum: ldr r2, [r0], #4
  add r3, r3, r2
  cmp r0, r1
  bne sum
  ldr r4, =0x100000E9
  strb r9, [r4]
  subs r9, r9, #1
  bne round
done: b done
.ltorg`;
// Thumb: byte stores to 0x90000-0x90FFF and halfword loads from 0x30000-0x31FFF, 40 times
const THUMB = `
  movs r7, #40
round:
  ldr r0, =0x90000
  ldr r1, =0x91000
fill: strb r7, [r0]
  adds r0, #1
  cmp r0, r1
  bne fill
  ldr r0, =0x30000
  ldr r1, =0x32000
sum: ldrh r2, [r0]
  adds r0, #2
  cmp r0, r1
  bne sum
  subs r7, #1
  bne round
done: b done
.ltorg`;

function machineWith(code, base, thumb) {
  const m = new Machine({ jit: true });
  m.cpu.hostWrite(base, code);
  m.cpu.pc = base; m.cpu.t = thumb ? 1 : 0;
  m.cpu.i = 1; m.cpu.f = 1;
  return m;
}
function runInsns(m, n) { let done = 0; while (done < n) done += m.cpu.run(Math.min(20000, n - done)); return done; }

// ---- 1. exact recording (window = period): ARM
{
  const code = asm(ARM, 0x10000);
  const m = machineWith(code, 0x10000, false);
  const act = new MemActivity({ period: 4096, window: 4096 });
  act.attach(m);
  const n = runInsns(m, 40 * (0x4000 / 4 * 3 + 0x8000 / 4 * 4 + 20));
  const W = act.w, R = act.r, X = act.x;
  const wIn = sum(W, 0x800, 0x840), rIn = sum(R, 0x200, 0x280);
  const wantW = 40 * 0x1000, wantR = 40 * 0x2000;
  ok(wIn === wantW, `ARM writes in 80000-83FFF: ${wIn} of ${wantW}`);
  ok(rIn === wantR, `ARM reads in 20000-27FFF: ${rIn} of ${wantR}`);
  ok(total(W) - wIn - sum(W, IO0, IO0 + 65) === 0, `no stray RAM writes (${total(W) - wIn})`);
  ok(sum(W, 0x800, 0x840) > 0 && Math.min(...W.subarray(0x800, 0x840)) >= 0.9 * 64 * 40, 'every write cell lit');
  ok(W[cellOf(0x100000E9)] === 40, `port E9h writes counted exactly: ${W[cellOf(0x100000E9)]}`);
  ok(Math.abs(total(X) - n) < 1, `execute counts = instructions run (${total(X)} vs ${n})`);
  ok(X[0x100] / n > 0.99, `execution lands in the code's cell 0x100 (${(X[0x100] / n * 100).toFixed(1)}%)`);
  act.detach();
}
// ---- 2. exact recording: Thumb, byte stores and halfword loads
{
  const code = asm(THUMB, 0x10000, true);
  const m = machineWith(code, 0x10000, true);
  const act = new MemActivity({ period: 4096, window: 4096 });
  act.attach(m);
  runInsns(m, 40 * (0x1000 * 4 + 0x1000 * 4 + 10));
  const wIn = sum(act.w, 0x900, 0x910), rIn = sum(act.r, 0x300, 0x320);
  ok(wIn === 40 * 0x1000, `Thumb byte writes in 90000-90FFF: ${wIn}`);
  ok(rIn === 40 * 0x1000, `Thumb halfword reads in 30000-31FFF: ${rIn}`);
  ok(total(act.w) === wIn, 'no stray Thumb writes');
}
// ---- 3. sampled (default 1/16): unbiased estimate; results unchanged; detached = plain code
{
  const code = asm(ARM, 0x10000);
  const ref = machineWith(code, 0x10000, false);
  const nRun = 40 * (0x4000 / 4 * 3 + 0x8000 / 4 * 4 + 20);
  runInsns(ref, nRun);
  const m = machineWith(code, 0x10000, false);
  ok(new MemActivity().scale === 2048, 'default sampling 1/2048');
  const act = new MemActivity({ period: 1 << 12, window: 1 << 8 });     // (more windows in this short run)
  act.attach(m);
  runInsns(m, nRun);
  // written lines: every line of the filled buffer once per drain, nothing else in RAM
  ok(sum(act.wp, 0x800, 0x840) === 128 && Math.min(...act.wp.subarray(0x800, 0x840)) === 2, `written lines: all 128 of 80000-83FFF (${sum(act.wp, 0x800, 0x840)})`);
  ok(total(act.wp) === 128, `no other lines written (${total(act.wp)})`);
  const wIn = sum(act.w, 0x800, 0x840), wantW = 40 * 0x1000;
  ok(Math.abs(wIn - wantW) / wantW < 0.2, `sampled write estimate ${wIn} ~ ${wantW}`);
  ok(total(act.w) - wIn - sum(act.w, IO0, IO0 + 65) === 0, 'sampled: no stray writes');
  ok(m.cpu.r[3] === ref.cpu.r[3] && m.cpu.pc === ref.cpu.pc && m.cpu.icount === ref.cpu.icount, 'same registers/instruction count as without the map');
  let wp2 = -1;
  act.drain((x, r, w, wp) => { wp2 = sum(wp, 0x800, 0x840); });
  ok(wp2 === 128 && total(act.wp) === 0, 'drain hands over the written lines and clears them');
  m.cpu.pc = 0x10000;                        // (the program had finished: run it again)
  runInsns(m, 50000);
  ok(sum(act.wp, 0x800, 0x840) === 128, `re-armed: the next sweep is seen again (${sum(act.wp, 0x800, 0x840)})`);
  act.detach();
  ok(m.cpu.act === null, 'detached');
  ok(m.cpu.pflags.every((f) => (f & 4) === 0), 'detached: no line left armed');
  const before = total(act.x);
  runInsns(m, 10000);
  ok(total(act.x) === before, 'no counting after detach');
}
// ---- 4. the other access forms (exact mode): LDM/STM, LDRD/STRD, STRH/LDRSB, SWP, PUSH/POP, register offsets
{
  const code = asm(`
  ldr sp, =0x70000
  ldr r0, =0x40000
  stmia r0, {r1-r8}          @ 8 writes 40000
  ldr r0, =0x40100
  ldmdb r0, {r1-r4}          @ 4 reads 400F0 (cell 0x400)
  ldr r0, =0x41000
  strd r2, [r0, #8]          @ 2 writes cell 0x410
  ldrd r2, [r0, #-16]        @ 2 reads cell 0x40F
  ldr r0, =0x42000
  strh r1, [r0, #2]          @ 1 write cell 0x420
  ldrsb r1, [r0, #0xFF]      @ 1 read cell 0x420
  ldr r0, =0x43000
  swp r1, r2, [r0]           @ read + write cell 0x430
  mov r5, #0x200
  ldr r1, [r0, r5, lsl #2]   @ read 43800 (cell 0x438)
  str r1, [r0, -r5]          @ write 42E00 (cell 0x42E)
  push {r0-r3, lr}           @ 5 writes 6FFEC (cell 0x6FF)
  pop {r0-r3, lr}            @ 5 reads
  cmp r0, r0
  ldrne r1, [r0]             @ condition fails: no read
  ldreq r1, [r0, #4]         @ 1 read cell 0x430
  done: b done
  .ltorg`, 0x10000);
  const m = machineWith(code, 0x10000, false);
  const act = new MemActivity({ period: 4096, window: 4096 });
  act.attach(m);
  runInsns(m, 60);
  const W = act.w, R = act.r;
  const want = [['W', 0x400, 8], ['R', 0x400, 4], ['W', 0x410, 2], ['R', 0x40F, 2], ['W', 0x420, 1], ['R', 0x420, 1],
    ['R', 0x430, 2], ['W', 0x430, 1], ['R', 0x438, 1], ['W', 0x42E, 1], ['W', 0x6FF, 5], ['R', 0x6FF, 5]];
  for (const [k, c, n] of want) ok((k === 'W' ? W : R)[c] === n, `${k} cell ${c.toString(16)}: ${(k === 'W' ? W : R)[c]} want ${n}`);
  const tw = total(W), tr = total(R);
  ok(tw === 8 + 2 + 1 + 1 + 1 + 5, `total writes ${tw}`);
  ok(tr === 4 + 2 + 1 + 1 + 1 + 5 + 1 + 1, `total reads ${tr} (incl. the one literal-pool load: the other constants assemble to MOV)`);
}
// ---- 5. ROM execution and cells
{
  ok(cellOf(0xFFFF0000) === ROM0 + 0xF00, 'ROM cell');
  ok(cellOf(0x100003F8) === IO0 + 0x3F, 'I/O cell');
}
// ---- 6. a booted ARM-DOS: regions, and DIR lights the text buffer
const rom = join(ROOT, 'build/rom.bin'), hd = join(ROOT, 'build/hd.img');
if (existsSync(rom) && existsSync(hd)) {
  const { boot } = await import('../../testkit.mjs');
  const pc = await boot({ rom, hd });
  ok(pc.waitText('C:\\>', { timeoutMs: 60000 }), 'booted to C:\\>');
  pc.waitIdle({ timeoutMs: 20000 });
  const regs = memoryRegions(pc.machine);
  const names = regs.map((r) => r.name);
  ok(names.includes('IVT') && names.includes('BIOS data area') && names.includes('DOS kernel'), 'IVT, BDA, DOS kernel');
  ok(regs.some((r) => r.kind === 'prog' && /COMMAND/.test(r.name)), 'COMMAND in the MCB chain: ' + regs.filter((r) => r.kind === 'prog').map((r) => r.name).join(','));
  ok(regionAt(regs, 0xB8000)?.kind === 'video' && regionAt(regs, 0x100000)?.kind === 'bios' && regionAt(regs, 0xFFFF0000)?.kind === 'rom', 'video, HMA, ROM');
  ok(regs.some((r) => r.kind === 'xms' || r.kind === 'xmsfree'), 'extended memory');
  for (let k = 1; k < regs.length; k++) if (regs[k].start < regs[k - 1].end) { ok(false, `regions overlap: ${regs[k - 1].name} / ${regs[k].name}`); break; }
  const act = new MemActivity();
  act.attach(pc.machine);
  // enough console work that the 1/2048 sampler sees both DOS and the BIOS's INT 10h code
  pc.type('DIR\rDIR C:\\DOS\rTYPE C:\\DOS\\TRYME.TXT\r'); pc.waitIdle({ timeoutMs: 30000 });
  const text = sum(act.wp, 0xB80, 0xB90);
  ok(text > 0, `DIR wrote the text buffer (${text} lines)`);
  const conv = sum(act.x, 0, 0xA00), rom2 = sum(act.x, ROM0, ROM0 + 4096);
  ok(conv > 0 && rom2 > 0, `executed in DOS (${conv}) and the BIOS ROM (${rom2})`);
  act.detach();
  console.log('     regions: ' + regs.filter((r) => r.end <= 0xA0000).map((r) => `${r.name}@${r.start.toString(16)}`).join(' '));
} else console.log('     (no build/rom.bin + build/hd.img: boot checks skipped)');

console.log(`memmap: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
