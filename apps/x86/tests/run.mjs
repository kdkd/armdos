#!/usr/bin/env node
// apps/x86/tests/run.mjs - ELBOW (the 8086 compatibility box) end to end on
// ARM-DOS: x86 programs run by the real kernel under the test shell, their
// screens, files and exit codes checked.  "make x86-test".
//   node apps/x86/tests/run.mjs [--quick]      (--quick: skip the CPU suite)
import fs from 'node:fs';
import path from 'node:path';
import { session, Checker, ROOT } from '../../dosutil/tests/harness.mjs';

const quick = process.argv.includes('--quick');
const B = (p) => path.join(ROOT, 'build', p);
const T = 'build/x86-test/', D = 'apps/x86/demo/', E = 'build/elbow/';
const t = new Checker('ELBOW');

const files = [
  { src: 'build/ELBOW.EXE', dst: 'DOS\\' }, { src: 'build/FILE.EXE', dst: 'DOS\\' }, { src: 'build/HIMEM.SYS', dst: 'DOS\\' },
  { src: 'build/MOUSE.COM', dst: 'DOS\\' },
];
for (const n of ['hello', 'child', 'exectest', 'irqtest', 'keys', 'smc', 'ctrlc', 'pm', 'mouse', 'gallery', 'carry', 'vgaems', 'adcfast'])
  files.push({ src: `${T}${n}.com`, dst: `X\\${n.toUpperCase()}.COM` });
files.push({ src: `${E}fire.COM`, dst: 'X\\FIRE.COM' }, { src: `${E}hello86.COM`, dst: 'X\\HELLO86.COM' },
  { src: `${E}BENCH86.EXE`, dst: 'X\\' }, { src: `${E}BENCHARM.EXE`, dst: 'X\\' });
for (const f of fs.readdirSync(path.join(ROOT, D, 'msdos20'))) files.push({ src: `${D}msdos20/${f}`, dst: 'M20\\' });
for (const f of fs.readdirSync(path.join(ROOT, D, 'freedos')).filter((f) => /\.(COM|EXE)$/.test(f))) files.push({ src: `${D}freedos/${f}`, dst: 'FD\\' });
files.push({ src: `${D}freedos/source/TREE.ZIP`, dst: 'FD\\' });
files.push({ src: `${D}apps/BASIC/GWBASIC.EXE`, dst: 'X\\' });
const cpu = fs.existsSync(B('x86-cpu/templates.txt')) && !quick;
if (cpu) for (const f of fs.readdirSync(B('x86-cpu')).filter((f) => /^CPUT\d\.COM$/.test(f))) files.push({ src: `build/x86-cpu/${f}`, dst: 'X\\' });

const s = await session({ name: 'elbow', files, dirs: ['X', 'M20', 'FD', 'WORK\\SUB1\\DEEP', 'WORK\\SUB2'], text: { 'X\\FRUIT.TXT': 'banana\r\napple\r\ncherry\r\n', 'X\\MATH.BAS': '10 PRINT SIN(2);COS(1);1#+.25#;.7#-.3#\r\n20 SYSTEM\r\n' },
  config: 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=20\nSHELL=C:\\T\\TSHELL.EXE\n', boot: process.env.NOSPIN ? { spinSkip: false } : {} });
const exitOf = () => { const m = [...s.pc.serial.matchAll(/T:EXIT \S+ (\d+) (\d+)/g)].pop(); return m ? +m[1] : -1; };
const run = (cmd, o) => { try { return s.run(cmd, o); } catch (e) { return ['FAILED: ' + e.message.split('\n')[0]]; } };
const has = (lines, re) => lines.some((l) => re.test(l));

// ---- the basics
let o = run('\\DOS\\ELBOW \\X\\HELLO.COM');
t.lines(o, ['Hello from x86!'], 'HELLO.COM: INT 21h AH=09h');
t.ok(exitOf() === 7, 'HELLO.COM: exit code 7 comes back through ELBOW', exitOf());
o = run('\\DOS\\ELBOW /?');
t.ok(has(o, /Emulated Legacy Binaries On Workstation/) && has(o, /\/NOJIT/), 'ELBOW /? shows the help');
o = run('\\DOS\\ELBOW NOSUCH');
t.lines(o, ['Bad command or file name'], 'a missing program');
o = run('\\DOS\\ELBOW \\X\\PM.COM');
t.ok(has(o, /protected mode \(MOV CR0\).*not supported/) && exitOf() === 255, 'protected mode is refused cleanly', o.join('\n'));
o = run('\\DOS\\ELBOW \\X\\HELLO86.COM');
t.ok(has(o, /sees an 80386 processor/), 'HELLO86: the CPU identifies as a 386', o.join('\n'));
t.ok(s.pc.screen().includes('Hello from a real x86 program on the ARM PC!'), 'HELLO86: text written straight to B800:0000 is on the screen');
o = run('\\DOS\\ELBOW \\X\\GALLERY.COM', { timeoutMs: 30000 });
t.ok(has(o, /^CLI POLL OK/) && has(o, /^TICKS AFTER STI OK/), 'a CLI\'d retrace poll runs while a hooked timer IRQ is pending (ZZT)', o.join('\n'));
t.ok(has(o, /^FONT 1FH OK/), 'INT 10h text in mode 4 uses the program\'s INT 1Fh font (Apogee CGA games)', o.join('\n'));
t.ok(has(o, /^READ CHAR OK/), 'INT 10h AH=08h reads a character back in mode 4 (GW-BASIC typing in SCREEN 1)', o.join('\n'));
t.ok(has(o, /^CGA PALETTE CALL IGNORED/), 'AX=1002h from a program that thinks it is on a CGA is ignored (GW-BASIC)', o.join('\n'));
run('CD \\X');
o = run('\\DOS\\ELBOW GWBASIC MATH.BAS', { timeoutMs: 60000 });
run('CD \\');
t.ok(has(o, /^ \.9092975 {2}\.5403023 {2}1\.25  \.4/), 'GW-BASIC (assembled from Microsoft\'s source on the ARM) computes SIN, COS and double precision', o.join('\n'));
run('\\DOS\\ELBOW \\X\\VGAEMS.COM > \\OUT\\VGAEMS.TXT', { timeoutMs: 60000 });
o = (s.file('OUT\\VGAEMS.TXT') || Buffer.alloc(0)).toString('latin1').split('\r\n');
for (const [k, what] of [['PSP55', 'INT 21h AH=55h takes the memory size from SI (PKLITE stubs of Second Reality\'s parts)'],
  ['IRQRACE', 'a hooked 1 kHz INT 08h keeps ticking through a STI/CLI loop (no lost IRQ0, Second Reality froze)'],
  ['EMS', 'LIM EMS 4.0: EMMXXXX0, frame E000h, allocate, map two pages, deallocate'],
  ['EGA', 'INT 10h AH=12h reports an EGA/VGA'], ['PLANAR', 'mode 12h: write mode 2, read map select'],
  ['LATCH', 'Mode X: REP MOVSB in write mode 1 copies the latches'], ['BIOS12', 'INT 10h in mode 12h: pixel, XOR pixel, read back, scroll'], ['DMA', 'DMA page registers read back as written']])
  t.ok(has(o, new RegExp(`^${k} OK`)), `VGAEMS: ${what}`, o.join('\n'));
o = run('\\DOS\\ELBOW \\X\\ADCFAST.COM');
t.ok(has(o, /^ADC FAST OK/), 'ADC/SBB after ADD/SUB/SHL/AND/CMP, 16 and 32 bits, translated (the carry from the ARM flags)', o.join('\n'));
o = run('\\DOS\\ELBOW \\X\\CARRY.COM');
t.ok(has(o, /^CARRY CHAIN OK/), 'ADC/SBB carry kept through INC and LOOP, translated (GW-BASIC double precision)', o.join('\n'));

// ---- the CPU against real x86 silicon (the host), both engines
if (cpu) {
  const tmpl = fs.readFileSync(B('x86-cpu/templates.txt'), 'utf8').trim().split('\n').map((l) => l.split('\t'));
  for (const mode of ['/NOJIT', '/JIT']) for (const f of fs.readdirSync(B('x86-cpu')).filter((f) => /^CPUT\d\.COM$/.test(f))) {
    const n = f.match(/\d/)[0];
    run(`\\DOS\\ELBOW ${mode} \\X\\${f} > \\OUT\\C${n}.TXT`, { timeoutMs: 300000 });
    const got = (s.file(`OUT\\C${n}.TXT`) || Buffer.alloc(0)).toString('latin1').split('\r\n');
    const want = fs.readFileSync(B(`x86-cpu/ref${n}.txt`), 'latin1').split('\r\n');
    const bad = want.filter((w, i) => got[i] !== w);
    const first = bad.length ? tmpl[parseInt(bad[0].slice(0, 4), 16)] : null;
    t.ok(!bad.length, `CPU ${mode} ${f}: ${want.length - 1} cases identical to a real x86`, first && `${bad.length} differ, first: ${first[1]}`);
  }
}

// ---- genuine MS-DOS 2.0 programs (Microsoft, MIT licence)
o = run('\\DOS\\ELBOW \\M20\\SORT.EXE < \\X\\FRUIT.TXT');
t.lines(o, ['apple', 'banana', 'cherry'], 'MS-DOS 2.0 SORT < file');
o = run('\\DOS\\ELBOW \\M20\\FIND.EXE "an" \\X\\FRUIT.TXT');
t.lines(o, ['', '---------- \\X\\FRUIT.TXT', 'banana'], 'MS-DOS 2.0 FIND');
run('\\DOS\\ELBOW \\M20\\SORT.EXE < \\X\\FRUIT.TXT > \\OUT\\SORTED.TXT');
// (SORT 2.0 drops the last CR LF: the same 21 bytes as in DOSBox-X)
t.bytes(s.file('OUT\\SORTED.TXT'), Buffer.from('apple\r\nbanana\r\ncherry'), 'SORT output redirected to a file (handles are ARM-DOS handles)');
run('CD \\M20');
o = run('\\DOS\\ELBOW MASM HELLO;', { timeoutMs: 120000 });
t.ok(has(o, /0\s+0/), 'MASM 1.10 assembles HELLO.ASM (0 warnings, 0 errors; the 512 KB shim applied)', o.join('\n'));
run('\\DOS\\ELBOW LINK HELLO;', { timeoutMs: 120000 });
run('\\DOS\\ELBOW EXE2BIN HELLO.EXE HELLO.COM');
o = run('\\DOS\\ELBOW HELLO.COM');
t.lines(o, ['Assembled with MASM, linked with LINK - on an ARM!'], 'MASM + LINK + EXE2BIN built a program that runs');
s.pc.type('CLS\r'); s.waitPrompt();
s.pc.type('\\DOS\\ELBOW DEBUG.COM\r');
s.pc.waitText('-', { timeoutMs: 20000 }); s.idle();
for (const l of ['a 100', 'mov ah,2', 'mov dl,41', 'int 21', 'int 20', '']) { s.pc.type(l + '\r'); s.idle(150); }
s.pc.type('g\r'); s.idle(); s.pc.type('q\r'); s.waitPrompt();
t.ok(s.pc.screen().includes('Program terminated normally'), 'MS-DOS 2.0 DEBUG: assemble, run the child, back in DEBUG', s.pc.screen());
run('CD \\');

// ---- FreeDOS programs (GPL)
o = run('\\DOS\\ELBOW \\FD\\TREE.COM \\WORK');
t.lines(o, ['Directory PATH listing for Volume ARMDOS', 'Volume serial number is 4069:12FF', 'C:\\WORK', '\u251c\u2500\u2500SUB1',
  '\u2502  \u2514\u2500\u2500DEEP', '\u2514\u2500\u2500SUB2'], 'FreeDOS TREE');
o = run('\\DOS\\ELBOW \\FD\\FIND.COM /I /N "FOX" \\WORK\\A.TXT');
t.lines(o, ['---------------- A.TXT', '1:The quick brown fox'], 'FreeDOS FIND /I /N');
s.pc.type('CLS\r'); s.waitPrompt();
s.pc.type('\\DOS\\ELBOW \\FD\\CHOICE.EXE /C:ABC Pick one\r'); s.pc.run(2500); s.pc.type('b'); s.waitPrompt();
t.ok(s.lines().includes('Pick one[A,B,C]?B') && exitOf() === 2, 'FreeDOS CHOICE: the key, errorlevel 2 through ELBOW', s.lines().join('\n'));

// ---- processes
run('CD \\X');
o = run('\\DOS\\ELBOW EXECTEST.COM', { timeoutMs: 60000 });
run('CD \\');
t.lines(o, ['child: tail=[ one two]', 'parent: x86 child exit code 42', '', '---------- \\WORK\\A.TXT', 'The quick brown fox',
  'parent: ARM child exit code 0'], 'EXEC: an x86 child (which closes ITS stdout) and an ARM child');

// ---- interrupts
o = run('\\DOS\\ELBOW \\X\\IRQTEST.COM');
const m = (o.join(' ').match(/int08=(\d+) int1c=(\d+)/) || []).map(Number);
t.ok(m[1] >= 17 && m[1] <= 19 && m[2] >= 17 && m[2] <= 19, 'INT 08h and INT 1Ch hooks get 18 ticks per second', o.join('\n'));
s.pc.type('CLS\r'); s.waitPrompt();
s.pc.type('\\DOS\\ELBOW \\X\\KEYS.COM\r'); s.pc.run(1500); s.pc.type('HeLlo'); s.waitPrompt();
const kl = s.lines();
t.ok(kl.includes('HeLlo') && kl.some((l) => /seen by the hook: 1[0-9]/.test(l)), 'an INT 9 hook that reads port 60h and chains: every key, in order, with Shift', kl.join('\n'));
s.pc.type('CLS\r'); s.waitPrompt();
s.pc.type('\\DOS\\ELBOW \\X\\CTRLC.COM\r'); s.pc.run(1500);
s.pc.type('ab{CTRL+C}cd'); s.pc.run(1500); s.pc.type('q'); s.waitPrompt();
const cl = s.lines();
t.ok(cl.some((l) => l.includes('[INT 23h: caught]cdq')) && cl.includes('bye') && exitOf() === 5, 'Ctrl-C calls the program\'s INT 23h, which continues it', cl.join('\n'));
for (const mode of ['/NOJIT', '/JIT']) {
  o = run(`\\DOS\\ELBOW ${mode} \\X\\SMC.COM`);
  t.lines(o, ['ZYXWVUTSRQPONMLKJIHGFEDCBA9876543210'], `self-modifying code ${mode}`);
}

// ---- graphics: FIRE (own INT 8 at 70 Hz, own INT 9, DAC, retrace)
s.pc.type('CLS\r'); s.waitPrompt();
s.pc.type('\\DOS\\ELBOW \\X\\FIRE.COM 3\r');
s.pc.run(2500);
const vga = s.pc.machine.vga;
const m8 = s.pc.cpu.m8;
let hot = 0; for (let a = 0xA0000 + 180 * 320; a < 0xA0000 + 200 * 320; a++) if (m8[a] > 100) hot++;
t.ok(m8[0x449] === 0x13 && hot > 500, 'FIRE: mode 13h, flames at the bottom', `mode ${m8[0x449]}, hot pixels ${hot}`);
fs.mkdirSync(B('x86-test'), { recursive: true });
await s.pc.png(B('x86-test/fire.png'));
s.waitPrompt();
t.ok(has(s.lines(), /frames in 3 s/), 'FIRE: 3 seconds by its own 70 Hz timer', s.lines().slice(-3).join('\n'));
s.pc.type('\\DOS\\ELBOW \\X\\FIRE.COM\r'); s.pc.run(2000); s.pc.type('x');
t.ok(s.waitPrompt(20000) && m8[0x449] === 3, 'FIRE: a key press (its own INT 9 handler) ends it, text mode is back');
void vga;

// ---- FILE.EXE
run('\\DOS\\FILE \\DOS\\ELBOW.EXE \\X\\HELLO.COM \\M20\\SORT.EXE \\DOS\\HIMEM.SYS \\X\\FRUIT.TXT \\M20 \\FD\\TREE.ZIP > \\OUT\\FILE.TXT');
o = (s.file('OUT\\FILE.TXT') || Buffer.alloc(0)).toString('latin1').split('\r\n');
t.ok(has(o, /^ELBOW.EXE: ARM-DOS .EXE \(MZ\/AR1\), ARMv5TE/), 'FILE: an ARM-DOS executable', o.join('\n'));
t.ok(has(o, /^HELLO.COM: x86 DOS .COM/), 'FILE: an x86 .COM');
t.ok(has(o, /^SORT.EXE: x86 DOS .EXE \(MZ\).*runs under ELBOW/), 'FILE: an x86 .EXE');
t.ok(has(o, /^HIMEM.SYS: ARM-DOS character device driver \(AR1\), device XMSXXXX0/), 'FILE: a device driver');
t.ok(has(o, /^FRUIT.TXT: text, CR LF line endings, 3 lines/), 'FILE: text');
t.ok(has(o, /^M20: directory/), 'FILE: a directory');
t.ok(has(o, /^TREE.ZIP: ZIP archive, 33 files/), 'FILE: a ZIP archive');

// ---- the benchmark, one C source for both CPUs
run('CD \\X');
run('BENCHARM.EXE ARM', { timeoutMs: 60000 });
run('\\DOS\\ELBOW BENCH86.EXE ELBOW > \\OUT\\BENCH.TXT', { timeoutMs: 60000 });
o = (s.file('OUT\\BENCH.TXT') || Buffer.alloc(0)).toString('latin1').split('\r\n');
const row = o.find((l) => l.includes('slower than the first'));
const ratio = row ? parseFloat(row.trim().split(/\s+/).pop()) : NaN;
t.ok(has(o, /x86 build, run "ELBOW"/) && ratio > 1 && ratio < 40, `BENCH86 (OpenWatcom 16-bit) under ELBOW vs BENCHARM (gcc ARM): ${ratio}x`, o.join('\n'));
run('CD \\');

// ---- EDLIN (MS-DOS 2.0) edits a file
s.pc.type('CLS\r'); s.waitPrompt();
s.pc.type('\\DOS\\ELBOW \\M20\\EDLIN.COM \\OUT\\NEW.TXT\r');
s.pc.waitText('*', { timeoutMs: 20000 }); s.idle();
for (const k of ['i\r', 'first line\r', 'second line\r', '{CTRL+C}', 'e\r']) { s.pc.type(k); s.idle(150); }
s.waitPrompt();
t.bytes(s.file('OUT\\NEW.TXT'), Buffer.from('first line\r\nsecond line\r\n\x1a'), 'EDLIN: insert two lines, save');

// ---- the mouse: an x86 INT 33h event handler over the ARM mouse driver (stays resident: last)
run('\\DOS\\MOUSE.COM');
s.pc.type('CLS\r'); s.waitPrompt();
s.pc.type('\\DOS\\ELBOW \\X\\MOUSE.COM\r'); s.pc.run(1500);
for (let i = 0; i < 8; i++) { s.pc.machine.mouseMove(20, 3); s.pc.run(100); }
s.waitPrompt(10000);
t.ok(s.lines().some((l) => /^mouse events: [5-9], last x: \d+/.test(l)), 'INT 33h: the x86 event handler is called', s.lines().slice(-3).join('\n'));

t.ok(s.pc.faults.length === 0, 'no CPU faults', JSON.stringify(s.pc.faults));
t.done();
