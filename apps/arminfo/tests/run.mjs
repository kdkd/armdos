#!/usr/bin/env node
// apps/arminfo/tests/run.mjs - ARMINFO.EXE headless tests.
//   node apps/arminfo/tests/run.mjs [--out DIR]
// Screenshots of every page go to build/showcase-test/arminfo/.

import fs from 'node:fs';
import path from 'node:path';
import { startPC, check, failed, tap, B, mode, readHdFile } from './harness.mjs';

const argv = process.argv.slice(2);
const oi = argv.indexOf('--out');
const OUT = path.resolve(oi >= 0 ? argv[oi + 1] : B('showcase-test/arminfo'));
fs.mkdirSync(OUT, { recursive: true });

const files = [{ src: 'build/ARMINFO.EXE', dst: 'DOS\\' }];

async function tty() {
  console.log('== ARMINFO /T (teletype report, redirected to a file)');
  const pc = await startPC({ name: 'tty', outDir: OUT, files });
  check(pc.bootOk, `booted to the ${pc.prompt} prompt`);
  pc.cmd('C:\\DOS\\ARMINFO.EXE /T >C:\\SI.TXT', { timeoutMs: 60000 });
  const s = readHdFile(pc, 'SI.TXT') || '';
  console.log(s.split('\n').map((l) => '     ' + l).join('\n'));
  check(/Computer Name: ARM\/AT \(Europa Micro Systems\)/.test(s), 'computer name ARM/AT, maker from the ROM');
  check(/BIOS dated: Saturday, September 24, 1988/.test(s), 'BIOS date read from the ROM (09/24/88) with weekday');
  check(/Operating System: ARM-DOS 4\.00/.test(s), 'INT 21h AH=30h: ARM-DOS 4.00');
  check(/CPU ID \(CP15\): 41069265, ARMv5TEJ/.test(s), 'CP15 main ID 41069265');
  check(/640 K-bytes main memory/.test(s), 'INT 12h: 640K');
  check(/Available Disk Drives: 2, A: C:/.test(s), 'drives A: and C:');
  check(/Computing Index \(CI\), relative to IBM\/XT: [\d.]+\r\n/.test(s), 'report redirected to a file with CR LF line ends');
  check(/Video Graphics Array \(VGA\), Color/.test(s), 'INT 10h AH=1Ah: VGA colour');
  pc.cmd('C:\\DOS\\ARMINFO.EXE /T');
  await pc.shot('tty.png');
  const s2 = pc.screen();
  const ci = /Computing Index \(CI\), relative to IBM\/XT: ([\d.]+)/.exec(s2);
  const di = /Disk Index \(DI\), relative to IBM\/XT: ([\d.]+)/.exec(s2);
  check(ci && +ci[1] > 300 && +ci[1] < 800, `Computing Index at 100 MHz = ${ci && ci[1]} (expected ~500)`);
  check(di && +di[1] > 10, `Disk Index = ${di && di[1]}`);
  return ci ? +ci[1] : 0;
}

async function full(mhz, tag) {
  console.log(`== ARMINFO full screen at ${mhz} MHz`);
  const pc = await startPC({ name: 'full' + tag, outDir: OUT, files, mhz });
  check(pc.bootOk, `booted (${mhz} MHz)`);
  pc.type('C:\\DOS\\ARMINFO.EXE\r');
  check(pc.waitText('System Summary', { timeoutMs: 20000 }), 'page 1: System Summary');
  pc.run(300);
  await pc.shot(`p1-summary${tag}.png`);
  const s1 = pc.screen();
  check(s1.includes('Computer Name: ARM/AT'), 'summary shows the computer name');
  check(/Main Processor: ARM926EJ-S \(ARMv5TEJ\)\s+Clock: \d+ MHz/.test(s1), 'summary shows the CPU and clock');
  tap(pc, 'PageDown', 300);
  check(pc.waitText('Processor: ARM926EJ-S'), 'page 2: CPU');
  const s2 = pc.screen();
  check(s2.includes('41069265'), 'CP15 ID shown');
  check(/SYS \(System\)/.test(s2), 'CPSR mode = SYS');
  check(!/^.*\s-\s+(Thumb|DSP|CLZ)/m.test(s2) && (s2.match(/\u221a/g) || []).length >= 7, 'all executed feature probes pass (7 check marks)');
  await pc.shot(`p2-cpu${tag}.png`);
  tap(pc, 'PageDown', 300);
  check(pc.waitText('Memory Control Blocks'), 'page 3: memory');
  check(pc.hasText('ARMINFO'), 'MCB chain lists ARMINFO itself');
  await pc.shot(`p3-memory${tag}.png`);
  tap(pc, 'PageDown', 100);
  check(pc.waitText('Performance Index', { timeoutMs: 30000 }), 'page 4: benchmarks finished');
  const s4 = pc.screen();
  const ci = /Computing Index \(CI\), relative to IBM\/XT: ([\d.]+)/.exec(s4);
  check(ci, 'CI shown');
  console.log(`     CI = ${ci && ci[1]}`);
  await pc.shot(`p4-bench${tag}.png`);
  tap(pc, 'PageDown', 100);
  check(pc.waitText('LDMIA/STMIA', { timeoutMs: 30000 }) && pc.until(() => /LDMIA\/STMIA.*MB\/s/.test(pc.screen()), { timeoutMs: 30000 }), 'page 5: ARM tests finished');
  pc.run(200);
  const s5 = pc.screen();
  const num = (re) => { const m = re.exec(s5); return m ? +m[1].replace(/,/g, '') : 0; };
  const sep = num(/8086 way\)\s+([\d.]+) M\/s/), bar = num(/LSL #2 \(asm\)\s+([\d.]+) M\/s/);
  const gb = num(/BEQ \/ BLT \/ B \(8086 way\)\s+([\d,]+)\/s/), gc = num(/SUBLT \/ BNE \(asm\)\s+([\d,]+)\/s/);
  const mb = num(/REP MOVSB way\)\s+([\d.]+) MB\/s/), mm = num(/r3-r10 \(asm\)\s+([\d.]+) MB\/s/);
  check(bar > sep * 1.2, `barrel shifter beats separate shifts (${bar} vs ${sep} M/s)`);
  check(gc > gb * 1.1, `conditional execution beats branches (${gc} vs ${gb} GCD/s)`);
  check(mm > mb * 3, `LDM/STM beats LDRB/STRB (${mm} vs ${mb} MB/s)`);
  check(!s5.includes('WARNING'), 'the asm and C versions compute the same results');
  await pc.shot(`p5-arm${tag}.png`);
  tap(pc, 'Escape', 300);
  check(pc.until(() => pc.lines().some((l) => l.startsWith(pc.prompt)), { timeoutMs: 5000 }) && !pc.hasText('System Summary'), 'Esc restores the DOS screen');
  return ci ? +ci[1] : 0;
}

const ci100 = await tty();
const ciF = await full(100, '');
const ciS = await full(12, '-12mhz');
check(ciS > 0 && ciF / ciS > 6 && ciF / ciS < 10.5, `CI scales with the clock: ${ciF} at 100 MHz, ${ciS} at 12 MHz`);
void ci100; void mode;
console.log(failed() ? `\n${failed()} FAILED` : '\nall ARMINFO tests passed');
process.exit(failed() ? 1 : 0);
