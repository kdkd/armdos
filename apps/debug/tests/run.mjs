#!/usr/bin/env node
// apps/debug/tests/run.mjs - DEBUG.COM running headless on ARM-DOS.
//
// Boots a private C: (IO.SYS, ARMDOS.SYS, the kernel's test shell, DEBUG.COM),
// types DEBUG sessions and checks the transcript (stdout redirected to a file:
// DOS echoes AH=0Ah input there too) or the screen, against DOS 4.00 DEBUG's
// formats (as documented for MS-DOS 4.00) adapted to the ARM register set.
//
//   node apps/debug/tests/run.mjs [session...]
import fs from 'node:fs';
import path from 'node:path';
import { startPC, typeWait, readHdFile, check, failed, B, OUT, screenToCursor } from './harness.mjs';

const only = process.argv.slice(2);
const want = (n) => !only.length || only.includes(n);
const hex8 = (v) => (v >>> 0).toString(16).toUpperCase().padStart(8, '0');

/** text to type literally (braces are key names to the test kit) */
const lit = (t) => [...t].map((c) => c === '{' ? '{SHIFT+BracketLeft}' : c === '}' ? '{SHIFT+BracketRight}' : c).join('');

/** start DEBUG with stdout in C:\T\OUT.TXT, type lines, return the transcript */
async function session(name, lines, { args = '', files = [], texts = {}, redirect = true } = {}) {
  const pc = await startPC({ name, files, texts });
  if (!pc.bootOk) { check(false, `${name}: boot`); return { pc, out: '' }; }
  typeWait(pc, `C:\\DOS\\DEBUG.COM${args ? ' ' + args : ''}${redirect ? ' >C:\\T\\OUT.TXT' : ''}\r`, { timeoutMs: 60000 });
  for (const l of lines) typeWait(pc, typeof l === 'string' ? lit(l) + '\r' : l.keys, { timeoutMs: 60000 });
  const out = redirect ? String(readHdFile(pc, 'T\\OUT.TXT') ?? '') : '';
  fs.writeFileSync(path.join(OUT, name + '.txt'), redirect ? out : screenToCursor(pc));
  return { pc, out };
}

/** the part of a transcript that a command produced: from "-CMD\r\n" to the next prompt */
function reply(out, cmd, nth = 0) {
  let at = -1;
  for (let i = 0; i <= nth; i++) { at = out.indexOf('-' + cmd + '\r', at + 1); if (at < 0) return null; }
  const s = out.indexOf('\n', at) + 1;
  const e = out.indexOf('\r\n-', s - 2);
  return out.slice(s, e < 0 ? undefined : e + 2);
}
/** a U line as DEBUG prints it: mnemonic at column 24, TAB, operands, comment at 56, padded to 62 */
function ul(a, hex, mn, ops = '', cmt = '') {
  let s = (a + ' ' + hex).padEnd(24) + mn, col = 24 + mn.length;
  if (ops) { s += '\t' + ops; col = ((col | 7) + 1) + ops.length; }
  if (cmt) { const pad = col < 56 ? 56 - col : 2; s += ' '.repeat(pad) + cmt; col += pad + cmt.length; }
  if (col < 62) s += ' '.repeat(62 - col);
  return s + '\r\n';
}
const exitLogged = (pc) => pc.serial.includes('T:EXIT C:\\DOS\\DEBUG.COM 0 0');

// ------------------------------------------------------------------ basics
if (want('basics')) {
  console.log('== basics: R, A, U, G (HELLO), D, E, F, S, C, M, H, I, errors');
  const { pc, out } = await session('basics', [
    'R', 'A 100', 'mov r0,#0x200', "mov r3,#'A'", 'svc #0x21', 'svc 20', '',
    'U 100 L10', 'G', 'G', 'D 100 L10', "E 200 'Hi' 0D 0A", 'D 200 L4', 'F 300 L8 AA BB', 'D 300 L8',
    'S 300 L8 BB', 'C 300 L4 302', 'M 300 L4 310', 'D 310 L4', 'H 1234 34', 'I F0', 'zz', 'D 100 XYZ',
    'R R3', '41', 'R F', 'ZR CY', 'R', 'R F', 'XX', 'R F', 'ZR NZ', 'RQ', 'G 0 1 2 3 4 5 6 7 8 9 A',
    'A 400', 'mov r0,#0x101', 'movs r1, r2, lsl #32', 'bogus', '', 'D 0:400 L8', 'D 200 100', 'T 0', 'H 1', 'Q',
  ]);
  const r = /^R0\(AX\)=([0-9A-F]{8}) {2}R1\(BX\)=([0-9A-F]{8}) {2}R2\(CX\)=([0-9A-F]{8}) {2}R3\(DX\)=00000000 {2}\r\nR4\(SI\)=00000000 {2}R5\(DI\)=00000000 {2}R6\(BP\)=00000000 {2}R7\(DS\)=00000000 {2}\r\nR8\(ES\)=00000000 {2}R9=00000000 {2}R10=00000000 {2}R11=00000000 {2}R12=00000000 {2}\r\nSP=([0-9A-F]{8}) {2}LR=([0-9A-F]{8}) {2}PC=([0-9A-F]{8}) {3}NV EI PL NZ NC AR SYS\r\n([0-9A-F]{8}) [0-9A-F]{8} {7}\S+/.exec(reply(out, 'R') || '');
  check(!!r, 'R: 16 registers in DEBUG\'s "NAME=VALUE  " style with x86 aliases, flags, next instruction', reply(out, 'R'));
  if (!r) { console.log(out); process.exit(1); }
  const psp = parseInt(r[1], 16), base = psp;
  const A = (off) => hex8(base + off);
  check(parseInt(r[2], 16) === psp + 0x100 && parseInt(r[6], 16) === psp + 0x100 && r[7] === r[6], 'R: entry state of a .COM: R1 = PC = PSP+100h, R0 = PSP');
  check(parseInt(r[5], 16) === (psp | 1) && parseInt(r[4], 16) % 8 === 0 && parseInt(r[4], 16) === parseInt(r[3], 16), 'R: LR = PSP|1 (bx lr = INT 20h), SP = R2 = end of the block');
  check(out.startsWith('-R\r\r\n'), 'prompt "-", input echoed, CR LF after the line', JSON.stringify(out.slice(0, 20)));
  const asm = `-A 100\r\r\n${A(0x100)} mov r0,#0x200\r\r\n${A(0x104)} mov r3,#'A'\r\r\n${A(0x108)} svc #0x21\r\r\n${A(0x10C)} svc 20\r\r\n${A(0x110)} \r\r\n`;
  check(out.includes(asm), 'A: "ADDRESS " prompts advance by 4, blank line ends', out.slice(out.indexOf('-A 100'), out.indexOf('-U')));
  check(reply(out, 'U 100 L10') === ul(A(0x100), 'E3A00C02', 'mov', 'r0, #512', '@ 0x200') + ul(A(0x104), 'E3A03041', 'mov', 'r3, #65', '@ 0x41') +
    ul(A(0x108), 'EF000021', 'svc', '0x00000021') + ul(A(0x10C), 'EF000020', 'svc', '0x00000020'),
    'U: address, word, mnemonic at column 24, TAB, operands in objdump syntax (padded like 4.00\'s lines)', JSON.stringify(reply(out, 'U 100 L10')));
  check(reply(out, 'G') === 'A\r\nProgram terminated normally\r\n', 'G: the HELLO program prints "A" via INT 21h AH=02h, then "Program terminated normally"', JSON.stringify(reply(out, 'G')));
  check(reply(out, 'G', 1) === 'A\r\nProgram terminated normally\r\n', 'G again after the end runs it again (as 4.00 does)', JSON.stringify(reply(out, 'G', 1)));
  check(reply(out, 'D 200 100') === '          ^ Error\r\n' && reply(out, 'T 0') === '   ^ Error\r\n' && reply(out, 'H 1') === '    ^ Error\r\n',
    'error columns as 4.00 (range end before the start, T 0, a missing parameter)', JSON.stringify([reply(out, 'D 200 100'), reply(out, 'T 0'), reply(out, 'H 1')]));
  check(reply(out, 'D 100 L10') === `${A(0x100)}  02 0C A0 E3 41 30 A0 E3-21 00 00 EF 20 00 00 EF   ....A0..!... ...\r\n`, 'D: 16 bytes, "-" after the 8th, ASCII column', reply(out, 'D 100 L10'));
  check(reply(out, 'D 200 L4') === `${A(0x200)}  48 69 0D 0A${' '.repeat(12 * 3)}   Hi..\r\n`, 'E with a list (string + hex), D of a partial line', JSON.stringify(reply(out, 'D 200 L4')));
  check(reply(out, 'D 300 L8') === `${A(0x300)}  AA BB AA BB AA BB AA BB${' '.repeat(8 * 3)}   ........\r\n`, 'F repeats the list');
  check(reply(out, 'S 300 L8 BB') === [0x301, 0x303, 0x305, 0x307].map((o) => A(o) + ' \r\n').join(''), 'S lists every match', reply(out, 'S 300 L8 BB'));
  check(reply(out, 'C 300 L4 302') === '', 'C: no output when equal');
  check(reply(out, 'D 310 L4') === `${A(0x310)}  AA BB AA BB${' '.repeat(12 * 3)}   ....\r\n`, 'M copies');
  check(reply(out, 'H 1234 34') === '00001268  00001200\r\n', 'H: sum and difference', reply(out, 'H 1234 34'));
  check(reply(out, 'I F0') === '41\r\n', 'I: port F0h reads the board ID "A" through the ISA window');
  check(reply(out, 'zz') === ' ^ Error\r\n', 'unknown command: "^ Error" under it', JSON.stringify(reply(out, 'zz')));
  check(reply(out, 'D 100 XYZ') === '       ^ Error\r\n', 'bad parameter: the caret under the offending character', JSON.stringify(reply(out, 'D 100 XYZ')));
  check(out.includes('-R R3\r\r\nR3 00000000\r\n:41\r\r\n'), 'R reg: "NAME value" then ":" prompt');
  check(/R3\(DX\)=00000041/.test(reply(out, 'R', 1) || '') && /PC=[0-9A-F]{8} {3}NV EI PL ZR CY AR SYS/.test(reply(out, 'R', 1) || ''), 'R: changed register and flags (R F ZR CY) shown', reply(out, 'R', 1));
  check(out.includes('-R F\r\r\nNV EI PL NZ NC AR  -ZR CY\r'), 'R F: flags then " -" prompt');
  check(out.includes(' -XX\r\r\nbf Error\r\n'), 'R F: bad flag -> "bf Error"');
  check(out.includes(' -ZR NZ\r\r\ndf Error\r\n'), 'R F: a flag twice -> "df Error"');
  check(reply(out, 'RQ') === 'br Error\r\n', 'R with a bad register -> "br Error"');
  check(reply(out, 'G 0 1 2 3 4 5 6 7 8 9 A') === 'bp Error\r\n', 'G with 11 breakpoints -> "bp Error"');
  check(out.includes(`-A 400\r\r\n${A(0x400)} mov r0,#0x101\r\r\n                ^ Error\r\n${A(0x400)} movs r1, r2, lsl #32\r\r\n                          ^ Error\r\n${A(0x400)} bogus\r\r\n         ^ Error\r\n${A(0x400)} \r\r\n`),
    'A: errors point at the bad operand (the prompt is 9 wide), the address stays', out.slice(out.indexOf('-A 400')));
  check(reply(out, 'D 0:400 L8') !== null && reply(out, 'D 0:400 L8').startsWith('00000400  '), 'SEG:OFF addresses are SEG*16+OFF (the BIOS data area)');
  check(exitLogged(pc), 'Q returns to the shell');
}

// ------------------------------------------------------------------ hello (the README's second example)
if (want('hello')) {
  console.log('== hello: INT 21h AH=09h with a string entered with E');
  const { pc, out } = await session('hello', ['A 100', 'mov r0,#0x900', 'adr r3,120', 'svc 21', 'svc 20', '', 'E 120 "Hello, world!$"', 'G', 'Q']);
  check(reply(out, 'G') === 'Hello, world!\r\nProgram terminated normally\r\n', 'adr r3 + AH=09h prints the string', JSON.stringify(reply(out, 'G')));
  check(exitLogged(pc), 'Q');
}

// ------------------------------------------------------------------ tracing

if (want('trace')) {
  console.log('== trace: T, P, G breakpoints, conditional branches, BL, BLX to Thumb, POP {pc}, LDR pc, LDM pc');
  // the base is needed for the literal: learn it from a first session
  const probe = await session('trace-probe', ['R', 'Q']);
  const base = parseInt(/R0\(AX\)=([0-9A-F]{8})/.exec(probe.out)?.[1] ?? '0', 16);
  const lines = [
    'A 100', 'mov r0,#3', 'subs r0,r0,#1', 'bne 104', 'bl 120', 'blx 200', 'ldr pc, 11C', 'nop',
    `dd ${(base + 0x130).toString(16)}`, 'bx lr', '', 'A 130', 'bl 140', 'svc 20', '', 'A 140', 'push {r4,lr}', 'pop {r4,pc}', '',
    // Thumb at 200: movs r0,#5; adds r0,#1; push {lr}; pop {pc}
    'E 200 05 20 01 30 00 B5 00 BD',
    'G 10C', 'P', 'T', 'U 201 L8', 'T 4', 'T', 'T', 'T', 'T', 'T', 'G =100 108', 'T', 'R R0', '1', 'T', 'T', 'G', 'Q',
  ];
  const { pc, out } = await session('trace', lines);
  const A = (off) => hex8(base + off);
  const pcs = [];
  for (const m of out.matchAll(/PC=([0-9A-F]{8}) {3}(.*?)\r\n/g)) pcs.push([parseInt(m[1], 16) - base, m[2]]);
  const seq = pcs.map(([o, f]) => o.toString(16) + (f.includes('TH') ? 't' : ''));
  const expect = ['10c', '110', '200t', '202t', '204t', '206t', '114', '130', '140', '144', '134', '108', '104', '108', '10c'];

  check(/-T\r\r\n\r\nProgram terminated normally\r\n/.test(out), 'T over "svc 20": the program ends');
  check(JSON.stringify(seq) === JSON.stringify(expect), `stops: ${seq.join(' ')}`, `expected ${expect.join(' ')}`);
  check(/R0\(AX\)=00000000/.test(reply(out, 'G 10C') || '') && /ZR CY AR/.test(reply(out, 'G 10C') || ''), 'G 10C: the loop ran to the breakpoint (R0 = 0, ZR CY from subs)');
  check(/LR=([0-9A-F]{8})/.exec(reply(out, 'P') || '')?.[1] === A(0x110), 'P steps over BL (the subroutine ran: LR = the return address)');
  check((reply(out, 'T') || '').includes('TH SYS') && (reply(out, 'T') || '').includes(ul(A(0x200), '2005', 'movs', 'r0, #5')), 'T into BLX: Thumb state, Thumb disassembly');
  check(reply(out, 'U 201 L8') === ul(A(0x200), '2005', 'movs', 'r0, #5') + ul(A(0x202), '3001', 'adds', 'r0, #1') + ul(A(0x204), 'B500', 'push', '{lr}') + ul(A(0x206), 'BD00', 'pop', '{pc}'),
    'U at an odd address disassembles Thumb', JSON.stringify(reply(out, 'U 201 L8')));
  check(out.includes(`${A(0x114)} E59FF000       ldr\tpc, [pc]`), 'LDR pc, <address> assembled pc-relative');
  check(new RegExp(`${A(0x114)} E59FF000       ldr\\tpc, \\[pc\\] +@ 0x[0-9a-f]+  ${A(0x11C)}=${hex8(base + 0x130)}\\r\\n`).test(out), 'the register display shows the memory operand "ADDRESS=VALUE" (as DEBUG\'s DS:0015=7510)');

  check(/Program terminated normally/.test(reply(out, 'G') || ''), 'G to the end: terminated normally', reply(out, 'G'));

  check(exitLogged(pc), 'Q');
}

// ------------------------------------------------------------------ programs
if (want('exe')) {
  console.log('== exe: DEBUG FOO.EXE (INT 21h 4B01h), entry registers, U, T/P in C code, G, L again, Q with a program loaded');
  const { pc, out } = await session('exe', ['R', 'U', 'T 3', 'P', 'G', 'L', 'R', 'G', 'L', 'Q'],
    { args: 'C:\\T\\K_HELLO.EXE ONE TWO', files: [{ src: 'build/ktest/K_HELLO.EXE', dst: 'T\\K_HELLO.EXE' }] });
  const r = /R0\(AX\)=([0-9A-F]{8}) {2}R1\(BX\)=([0-9A-F]{8}) {2}R2\(CX\)=([0-9A-F]{8})[\s\S]*?SP=([0-9A-F]{8}) {2}LR=([0-9A-F]{8}) {2}PC=([0-9A-F]{8})/.exec(reply(out, 'R') || '');
  check(!!r && parseInt(r[2], 16) === parseInt(r[1], 16) + 0x100 && parseInt(r[5], 16) === (parseInt(r[1], 16) | 1), 'R: the EXE\'s entry state (R0 = PSP, R1 = load base, LR = PSP|1)', reply(out, 'R'));
  check(!!r && parseInt(r[4], 16) < parseInt(r[3], 16) && parseInt(r[4], 16) > parseInt(r[2], 16), 'SP = the image\'s stack top, R2 = the end of its block');
  const u = reply(out, 'U') || '';
  check(u.split('\r\n').length === 9 && !u.includes('.word') && u.includes('mov\tr4, r0') && u.includes('bic\tsp, sp, #7'), 'U at the entry point: the SDK\'s crt0, real ARM code', u);
  check(/PC=[0-9A-F]{8}[\s\S]*PC=[0-9A-F]{8}[\s\S]*PC=[0-9A-F]{8}/.test(reply(out, 'T 3') || ''), 'T 3: three register displays');
  check((reply(out, 'G') || '').includes('Hello via AH=09h') && (reply(out, 'G') || '').includes('Program terminated normally'), 'G runs the program to its end', reply(out, 'G'));
  check(pc.serial.includes('argv[1] = ONE') || pc.serial.includes('ONE'), 'the command tail after the file name reaches the program', pc.serial);
  check(reply(out, 'R', 1) !== null && /PC=([0-9A-F]{8})/.exec(reply(out, 'R', 1))?.[1] === r?.[6], 'L reloads the named program: back at the entry point');
  check((reply(out, 'G', 1) || '').includes('Hello from printf'), 'and it runs again');
  check(exitLogged(pc), 'Q with a program loaded ends it and DEBUG');
  // memory: nothing leaked after DEBUG + a loaded program
  const m = await startPC({ name: 'exe-mem', texts: {}, files: [{ src: 'build/ktest/K_HELLO.EXE', dst: 'T\\K_HELLO.EXE' }] });
  typeWait(m, 'mem\r');
  typeWait(m, 'C:\\DOS\\DEBUG.COM C:\\T\\K_HELLO.EXE\r');
  typeWait(m, 'T\r'); typeWait(m, 'Q\r');
  typeWait(m, 'C:\\DOS\\DEBUG.COM\r');
  typeWait(m, 'A 100\r'); typeWait(m, 'svc 20\r'); typeWait(m, '\r'); typeWait(m, 'G\r'); typeWait(m, 'Q\r');
  typeWait(m, 'mem\r');
  const mems = [...m.serial.matchAll(/T:MEM (\d+)/g)].map((x) => +x[1]);
  check(mems.length === 2 && mems[0] === mems[1], `no memory lost (free before/after: ${mems.join(' / ')})`, m.serial);
}

if (want('command')) {
  console.log('== command: U on COMMAND.COM\'s entry point');
  if (!fs.existsSync(B('COMMAND.COM'))) console.log('     (skipped: no build/COMMAND.COM)');
  else {
    const { pc, out } = await session('command', ['U', 'Q'], { args: 'C:\\T\\COMMAND.COM', files: [{ src: 'build/COMMAND.COM', dst: 'T\\COMMAND.COM' }] });
    const u = reply(out, 'U') || '';
    const lines = u.split('\r\n').filter(Boolean);
    // crt0's entry: ldr r3 / add sp / mov fp / mov lr / bl main / b . , then its literal (shown as .word
    // or as whatever instruction its value happens to encode, which moves with the load address)
    const ops = lines.slice(0, 6).map((l) => (/^[0-9A-F]{8} [0-9A-F]{8} {7}([a-z]+)/.exec(l) || [])[1]);
    check(lines.length >= 8 && ops.join(' ') === 'ldr add mov mov bl b', 'U: COMMAND.COM\'s entry point is ARM code', u);
    check(exitLogged(pc), 'Q');
  }
}

// ------------------------------------------------------------------ files
if (want('files')) {
  console.log('== files: N, W, L, errors, absolute sectors');
  const { pc, out } = await session('files', [
    'N TEST.DAT', "E 100 'hello, world'", 'R CX', 'C', 'W', 'F 100 L20 0', 'L', 'D 100 LC', 'R',
    'N FOO.EXE', 'W', 'N NOPE.TXT', 'L', 'N NOPE.COM', 'L', 'N', 'W', 'L 400 2 0 1', 'D 5FE L2', 'Q',
  ]);
  check(reply(out, 'W') === 'Writing 0000000C bytes\r\n', 'W: "Writing n bytes" (CX = the count)', JSON.stringify(reply(out, 'W')));
  check(String(readHdFile(pc, 'TEST.DAT')) === 'hello, world', 'the file holds exactly the bytes');
  check((reply(out, 'D 100 LC') || '').includes('hello, world'), 'L loads it back to PSP:100');
  check(/R1\(BX\)=00000000 {2}R2\(CX\)=0000000C/.test(reply(out, 'R') || ''), 'L sets BX:CX = the file size');
  check(reply(out, 'W', 1) === 'EXE and HEX files cannot be written\r\n', 'W of an .EXE refused');
  check(reply(out, 'L', 1) === '', 'L of a missing data file: nothing (4.00 never shows its "File not found")', JSON.stringify(reply(out, 'L', 1)));
  check(reply(out, 'L', 2) === '\r\n', 'L of a missing .COM: an empty line (ditto, the EXEC path)', JSON.stringify(reply(out, 'L', 2)));
  check(reply(out, 'W', 2) === 'File creation error\r\n', 'W after "N" with no name: "File creation error"', JSON.stringify(reply(out, 'W', 2)));
  check((reply(out, 'D 5FE L2') || '').includes('55 AA'), 'L address drive sector count: the boot sector of C: (drive 2) via INT 25h');
  check(exitLogged(pc), 'Q');
  // DEBUG with a file that does not exist; W and L before any N
  const s2 = await session('nofile', ['Q'], { args: 'NOFILE.COM' });
  check(s2.out.startsWith('\r\n-Q'), 'DEBUG NOFILE.COM: an empty line, then the prompt', JSON.stringify(s2.out.slice(0, 40)));
  const s3 = await session('noname', ['W', 'L', 'Q']);
  check(reply(s3.out, 'W') === '(W)rite error, no destination defined\r\n' && reply(s3.out, 'L') === '', 'W with nothing named: "(W)rite error, no destination defined"; L: nothing', s3.out);

}

// ------------------------------------------------------------------ keyboard
if (want('keys')) {
  console.log('== keys: E interactive, ^C at the prompt and in a running program, F3 template');
  const pc = await startPC({ name: 'keys' });
  typeWait(pc, 'C:\\DOS\\DEBUG.COM\r');
  typeWait(pc, 'E 100\r');
  typeWait(pc, '41 42 4-');
  typeWait(pc, '5\r');
  typeWait(pc, 'D 100 L3\r');
  let s = screenToCursor(pc);
  const base = parseInt(/([0-9A-F]{8})  00\.41 /.exec(s)?.[1] ?? '0', 16) - 0x100;
  const A = (off) => hex8(base + off);
  check(s.includes(`-E 100\n${A(0x100)}  00.41   00.42   00.4-\n${A(0x101)}  42.5\n`), 'E: "ADDRESS  XX." fields 8 wide, space = next, "-" = back to the previous byte on a new line', s.split('\n').slice(-6).join('\n'));
  check(s.includes(`${A(0x100)}  41 05 04`), 'E stored 41 and 04, then went back and stored 05 over 42', s.split('\n').slice(-3).join('\n'));

  typeWait(pc, 'D 100{CTRL+C}');
  typeWait(pc, 'R');
  typeWait(pc, '{F3}');
  s = screenToCursor(pc);
  check(/-D 100\^C\n-R\s*$/.test(s) || /-D 100\^C\n\n?-R/.test(s), '^C at the prompt: "^C", a new prompt', s.split('\n').slice(-3).join('\n'));
  typeWait(pc, '\r');
  // a program that polls the keyboard (INT 21h AH=0Bh) forever; ^C stops it
  typeWait(pc, 'A 100\r'); typeWait(pc, 'mov r0,#0xb00\r'); typeWait(pc, 'svc 21\r'); typeWait(pc, 'b 100\r'); typeWait(pc, '\r');
  typeWait(pc, 'G\r');
  pc.run(500);
  typeWait(pc, '{CTRL+C}');
  s = screenToCursor(pc);
  check(new RegExp(`\\^C[\\s\\S]*R0\\(AX\\)=00000B00[\\s\\S]*PC=${A(0x108)}`).test(s), '^C in the program: DEBUG stops it after the DOS call and shows the registers', s.split('\n').slice(-8).join('\n'));
  typeWait(pc, 'T\r');
  s = screenToCursor(pc);
  check(s.includes(`PC=${A(0x100)}`), 'and it can be traced on from there');
  typeWait(pc, 'Q\r');
  check(exitLogged(pc), 'Q');
}

console.log(failed() ? `${failed()} FAILED` : 'all passed');
process.exit(failed() ? 1 : 0);
