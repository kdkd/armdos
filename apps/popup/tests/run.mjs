#!/usr/bin/env node
// apps/popup/tests/run.mjs - POPUP.COM headless tests.
//   node apps/popup/tests/run.mjs [--out DIR]
// Screenshots go to build/showcase-test/popup/.

import fs from 'node:fs';
import path from 'node:path';
import { startPC, check, failed, tap, combo, B, mode, readHdFile } from '../../arminfo/tests/harness.mjs';

const argv = process.argv.slice(2);
const oi = argv.indexOf('--out');
const OUT = path.resolve(oi >= 0 ? argv[oi + 1] : B('showcase-test/popup'));
fs.mkdirSync(OUT, { recursive: true });

const files = [{ src: 'build/POPUP.COM', dst: 'DOS\\' }, { src: 'build/ARMINFO.EXE', dst: 'DOS\\' }];
if (fs.existsSync(B('DEMO.EXE'))) files.push({ src: 'build/DEMO.EXE', dst: 'DEMO\\' });

const vram = (pc) => Array.from(pc.m.cpu.m16.subarray(0xB8000 / 2, 0xB8000 / 2 + 2000));
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const hotkey = (pc) => combo(pc, ['ControlLeft', 'AltLeft', 'KeyP'], 300);
const typeKeys = (pc, s) => { pc.type(s); pc.until(() => pc.m.typingDone(), { timeoutMs: 10000 }); pc.run(200); };

const pc = await startPC({ name: 'popup', outDir: OUT, files, dirs: ['DEMO'] });
check(pc.bootOk, `booted to the ${pc.prompt} prompt`);
const HOOKS = [8, 9, 0x10, 0x13, 0x15, 0x28, 0x2F];
const vec0 = HOOKS.map((n) => pc.m.cpu.m32[n] >>> 0);
pc.cmd('C:\\DOS\\POPUP');
check(pc.hasText('POPUP installed. Press Ctrl+Alt+P to activate.'), 'install message');
pc.cmd('C:\\DOS\\POPUP');
check(pc.hasText('POPUP is already installed.'), 'second copy refuses (INT 2Fh check)');
pc.cmd('ECHO The quick brown fox');
const ivt = (n) => pc.m.cpu.m32[n] >>> 0;
check([8, 9, 0x10, 0x13, 0x15, 0x28, 0x2F].every((n) => ivt(n) < 0xA0000), 'INT 08/09/10/13/15/28/2F point into conventional memory (resident)');

// --- pop up over the command prompt (InDOS = 1: through INT 28h)
const before = vram(pc);
hotkey(pc);
check(pc.waitText('Calculator', { timeoutMs: 3000 }) && pc.hasText('ASCII table'), 'Ctrl+Alt+P at the prompt shows the menu (INT 28h path)');
await pc.shot('menu.png');

// calculator: hex 3FC00 -> MOV r0,#0x3FC00 = E3A00BFF
typeKeys(pc, 'C');
check(pc.waitText('Tab dec/hex'), 'calculator window');
typeKeys(pc, '{TAB}3FC00');
check(pc.hasText('MOV r0,#0x0003FC00 = E3A00BFF (FF ROR 22)'), 'ARM immediate encoding of 3FC00h');
await pc.shot('calc-imm.png');
typeKeys(pc, '{TAB}C1234*1000=');
check(pc.hasText('Dec  1,234,000') && pc.hasText('no immediate: LDR r0,=0x0012D450'), '1234*1000 = 1,234,000 (not an ARM immediate)');
typeKeys(pc, 'C0-1=');
check(pc.hasText('MVN r0,#0x00000000 = E3E00000'), '-1 is MVN r0,#0');
typeKeys(pc, 'C255<24=');
check(pc.hasText('MOV r0,#0xFF000000 = E3A004FF (FF ROR 8)'), '255<<24 = FF ROR 8');
await pc.shot('calc.png');
typeKeys(pc, '{ESC}');

// notepad
typeKeys(pc, 'N');
check(pc.waitText('Notepad - C:\\POPUP.TXT'), 'notepad window');
typeKeys(pc, 'Remember: the ARM926 has 37 registers.\rBuy more floppies');
await pc.shot('notepad.png');
typeKeys(pc, '{F2}');
check(pc.hasText('Saved 59 bytes to C:\\POPUP.TXT'), 'F2 saves (59 bytes)');
typeKeys(pc, '{ESC}');

// ASCII table
typeKeys(pc, 'A');
check(pc.waitText('ASCII Table'), 'ASCII table');
typeKeys(pc, '{RIGHT}{DOWN}');
check(pc.hasText('Dec  82') && pc.hasText('Hex  52h'), 'cursor keys move the selection (A -> R)');
await pc.shot('ascii.png');
typeKeys(pc, '{ESC}');

typeKeys(pc, 'B');     // not a menu letter: ignored
typeKeys(pc, '{DOWN}{ENTER}');     // the bar is on "ASCII table"; one down is "About"
check(pc.hasText('About POPUP'), 'arrow keys + Enter select');
check(pc.hasText('million instructions') && pc.hasText('hooks ran'), 'About: instruction counter and hook count');
await pc.shot('about.png');
typeKeys(pc, 'x');
typeKeys(pc, '{ESC}');
pc.run(300);
check(same(vram(pc), before), 'Esc restores the screen exactly');
const note = readHdFile(pc, 'POPUP.TXT');
check(note === 'Remember: the ARM926 has 37 registers.\r\nBuy more floppies\r\n', 'C:\\POPUP.TXT content');

// --- the prompt still works afterwards
pc.cmd('ECHO still alive');
check(pc.hasText('still alive'), 'COMMAND continues after the pop-up');

// --- over a full-screen text program (ARMINFO waits in INT 16h: InDOS = 0)
pc.type('C:\\DOS\\ARMINFO.EXE /A\r');
check(pc.waitText('LDMIA/STMIA', { timeoutMs: 20000 }) && pc.until(() => /r3-r10 \(asm\)\s+[\d.]+ MB/.test(pc.screen()), { timeoutMs: 30000 }), 'ARMINFO running');
tap(pc, 'Digit3', 300);
check(pc.waitText('Memory Control Blocks') && /POPUP\s+08 09 10 13 15 28 2F/.test(pc.screen()), 'ARMINFO lists POPUP and its hooked vectors');
const before2 = vram(pc);
hotkey(pc);
check(pc.waitText('ASCII table', { timeoutMs: 3000 }), 'pops up over ARMINFO (INT 09h path)');
await pc.shot('over-arminfo.png');
typeKeys(pc, 'C');
typeKeys(pc, '{TAB}FF0{ENTER}');
check(pc.hasText('FF ROR 28'), 'calculator works over ARMINFO');
typeKeys(pc, '{ESC}{ESC}');
check(same(vram(pc), before2), 'ARMINFO screen restored exactly');
tap(pc, 'Escape', 300);
pc.until(() => pc.m.biosKeyBufferEmpty(), { timeoutMs: 3000 });

// --- graphics mode: refuse with a beep
if (fs.existsSync(B('DEMO.EXE'))) {
  pc.type('C:\\DEMO\\DEMO.EXE\r');
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 20000 }), 'DEMO in mode 13h');
  pc.run(1000);
  const n0 = pc.speaker.length;
  hotkey(pc);
  pc.run(500);
  const beeped = pc.speaker.slice(n0).some(([, on, hz]) => on && Math.abs(hz - 880) < 5);
  check(beeped && mode(pc) === 0x13, 'Ctrl+Alt+P in mode 13h: 880 Hz beep, no pop-up');
  tap(pc, 'Escape', 1500);
  pc.until(() => mode(pc) === 3, { timeoutMs: 5000 });
}

// --- removal
pc.cmd('C:\\DOS\\POPUP /U');
check(pc.hasText('POPUP removed from memory.'), 'POPUP /U');
check(HOOKS.every((n, i) => ivt(n) === vec0[i]), 'all seven vectors restored');
hotkey(pc);
check(!pc.hasText('ASCII table'), 'hot key does nothing after removal');
pc.cmd('C:\\DOS\\POPUP');
check(pc.hasText('POPUP installed.'), 'can be installed again');
await pc.shot('end.png');

console.log(failed() ? `\n${failed()} FAILED` : '\nall POPUP tests passed');
process.exit(failed() ? 1 : 0);
