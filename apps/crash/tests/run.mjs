#!/usr/bin/env node
// apps/crash/tests/run.mjs - CRASH.EXE headless tests: every exception is
// reported by ARM-DOS and COMMAND.COM carries on; the vector-table smash
// ends on the BIOS crash screen.
//   node apps/crash/tests/run.mjs [--out DIR]

import fs from 'node:fs';
import path from 'node:path';
import { startPC, check, failed, tap, B } from '../../arminfo/tests/harness.mjs';

const argv = process.argv.slice(2);
const oi = argv.indexOf('--out');
const OUT = path.resolve(oi >= 0 ? argv[oi + 1] : B('showcase-test/crash'));
fs.mkdirSync(OUT, { recursive: true });
const files = [{ src: 'build/CRASH.EXE', dst: 'DOS\\' }];

const pc = await startPC({ name: 'crash', outDir: OUT, files });
check(pc.bootOk, `booted to the ${pc.prompt} prompt`);
const scr = () => pc.screen();
const cls = () => pc.cmd('CLS');

// menu
pc.type('C:\\DOS\\CRASH\r');
check(pc.waitText('Choose your disaster', { timeoutMs: 10000 }), 'menu shown');
tap(pc, 'ArrowDown', 200);
check(pc.hasText('Loads a word from address 20000000h'), 'explanation follows the highlight');
await pc.shot('menu.png');
tap(pc, 'Escape', 300);
check(pc.until(() => !pc.hasText('Choose your disaster') && pc.lines().some((l) => l.startsWith(pc.prompt)), { timeoutMs: 3000 }), 'Esc returns to DOS');

const cases = [
  ['UNDEF', /Exception 06h: undefined instruction at [0-9A-F]{8} in CRASH\.EXE/, 'undefined instruction (UDF E7F198F8h) -> INT 06h'],
  ['DATA', /Exception 0Dh: data abort at [0-9A-F]{8} in CRASH\.EXE \(address 20000000\)/, 'load from 20000000h -> data abort INT 0Dh with the address'],
  ['PREFETCH', /Exception 0Eh: prefetch abort at 30000000 in CRASH\.EXE/, 'jump to 30000000h -> prefetch abort INT 0Eh'],
  ['DIVIDE', /Divide overflow/, 'divide by zero -> INT 00h -> "Divide overflow"'],
  ['STACK', /run-time error R6000\s+- stack overflow/, 'runaway recursion -> R6000 stack overflow'],
  ['ALIGN', /LDR r0,\[[0-9A-F]{8}\]  ->  44332211/, 'unaligned load: no exception'],
];
for (const [arg, re, what] of cases) {
  cls();
  pc.cmd(`C:\\DOS\\CRASH ${arg}`, { timeoutMs: 20000 });
  const s = scr();
  check(re.test(s) && pc.lines().some((l) => l.startsWith(pc.prompt)), what);
  await pc.shot(`${arg.toLowerCase()}.png`);
}
{
  const s = scr();
  const m = /LDR r0,\[[0-9A-F]{8}\]  ->  ([0-9A-F]{8})\s+\(an 8086 would read ([0-9A-F]{8})\)/g;
  const rows = [...s.matchAll(m)].map((x) => [x[1], x[2]]);
  check(rows.length === 4 && rows[1][0] === '11443322' && rows[1][1] === '55443322', `ARMv5 rotation: +1 gives 11443322, not 55443322 (${JSON.stringify(rows)})`);
}
cls();
pc.cmd('C:\\DOS\\CRASH 9');
check(pc.hasText('Invalid parameter - 9'), 'bad argument');
pc.cmd('ECHO still alive');
check(pc.hasText('still alive'), 'DOS survived five exceptions');

// the one that kills: via the menu, answer N first, then Y
cls();
pc.type('C:\\DOS\\CRASH\r');
pc.waitText('Choose your disaster');
tap(pc, 'Digit7', 500);
check(pc.waitText('Continue (Y/N)?'), 'vector smash asks first');
tap(pc, 'KeyN', 500);
check(pc.until(() => pc.lines().some((l) => l.startsWith(pc.prompt)), { timeoutMs: 3000 }), 'N returns to DOS');
cls();
pc.type('C:\\DOS\\CRASH VECTOR\r');
pc.waitText('Continue (Y/N)?');
tap(pc, 'KeyY', 1500);
check(pc.waitText('System halted', { timeoutMs: 5000 }) && pc.hasText('PREFETCH ABORT') && pc.hasText('DEADBEE0'), 'BIOS crash screen: prefetch abort at DEADBEE0');
await pc.shot('vector.png');
// Ctrl+Alt+Del restarts
pc.m.keyDown('ControlLeft'); pc.m.keyDown('AltLeft'); pc.m.keyDown('Delete'); pc.run(100);
pc.m.keyUp('Delete'); pc.m.keyUp('AltLeft'); pc.m.keyUp('ControlLeft');
check(pc.until(() => pc.lines().some((l) => l.startsWith(pc.prompt)), { timeoutMs: 30000 }), 'Ctrl+Alt+Del reboots to DOS');

console.log(failed() ? `\n${failed()} FAILED` : '\nall CRASH tests passed');
process.exit(failed() ? 1 : 0);
