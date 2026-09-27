#!/usr/bin/env node
// apps/print/tests/run.mjs - PRINT.COM against the real MS-DOS 4.00 PRINT.
//
// The expected screen texts were taken from the genuine PRINT.COM in DOSBox-X
// (apps/mslib/tools/dos400run.sh, see README.md); expected/TABS.PRN and
// expected/ALLCAN.PRN are bytes the genuine PRINT sent to LPT1 (DOSBox-X
// parallel1=file).  The printer here is the emulator's LPT1 (onPrint).
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { session, Checker, ROOT } from '../../dosutil/tests/harness.mjs';

const t = new Checker('PRINT');
const B = (p) => path.join(ROOT, 'build', p);
const EXP = (n) => fs.readFileSync(path.join(ROOT, 'apps/print/tests/expected', n));
const P = '\\DOS\\PRINT.COM';

// ------------------------------------------------ the resident part's layout
{
  const und = execFileSync('arm-none-eabi-nm', ['-u', B('obj/PRINT/res.c.o')]).toString().trim().split('\n')
    .map((l) => l.trim().split(/\s+/).pop()).sort();
  t.ok(JSON.stringify(und) === JSON.stringify(['r_callstk', 'r_end', 'r_i21']),
    'the resident part calls nothing outside itself', und.join(' '));
  const syms = execFileSync('arm-none-eabi-nm', ['-n', B('obj/PRINT/PRINT.elf')]).toString().split('\n')
    .map((l) => l.split(' ')).filter((p) => p.length === 3).map(([a, ty, n]) => [parseInt(a, 16), n]);
  const end = syms.find(([, n]) => n === 'r_end')[0];
  const resSyms = execFileSync('arm-none-eabi-nm', ['--defined-only', B('obj/PRINT/res.c.o')]).toString().split('\n')
    .map((l) => l.split(' ')[2]).filter(Boolean);
  const outside = syms.filter(([a, n]) => resSyms.includes(n) && a >= end).map(([, n]) => n);
  t.ok(outside.length === 0, `resident code and data lie below r_end (0x${end.toString(16)})`, outside.join(' '));
  t.ok(syms.find(([, n]) => n === 'main')[0] > end, 'the transient part follows it');
}

// ------------------------------------------------------------ the data
fs.mkdirSync(B('printtest'), { recursive: true });
{
  const lines = [];
  for (let i = 1; i <= 16000; i++) lines.push(`Line ${String(i).padStart(5, '0')}\tof BIG.TXT\tx`);
  fs.writeFileSync(B('printtest/BIG.TXT'), lines.join('\r\n') + '\r\n');
}
const files = [
  { src: 'build/printtest/BIG.TXT', dst: 'WORK\\' },
  { src: 'apps/print/tests/data/TABS.TXT', dst: 'WORK\\' },
  { src: 'build/printtest/SPIN.EXE', dst: 'T\\' },
];

// ============================================ session 1: /D:PRN, the works
{
  let printed = [];
  const marks = {};
  let dbg = '';
  const s = await session({
    name: 'print1', files,
    boot: {
      onPrint: (b) => printed.push(b),
      onDebug: (b) => { dbg += String.fromCharCode(b); if (/SPIN[<>]$/.test(dbg)) marks[dbg.slice(-5)] = printed.length; },
    },
  });
  const prn = () => Buffer.from(printed).toString('latin1');
  const memFree = () => { s.run('MEM'); const m = [...s.pc.serial.matchAll(/T:MEM (\d+)/g)]; return +m[m.length - 1][1]; };
  const listing = (cur, ...q) => ['', '', `  ${cur} is currently being printed`, ...q.map((n) => `  ${n} is in queue`)];
  const exitType = () => { const m = [...s.pc.serial.matchAll(/T:EXIT \\DOS\\PRINT\.COM (\d+) (\d+)/g)]; return m.length ? +m[m.length - 1][2] : -1; };

  const before = memFree();
  t.lines(s.run(`${P} /D:PRN`), ['Resident part of PRINT installed', 'PRINT queue is empty'], 'PRINT /D:PRN installs');
  t.ok(exitType() === 3, 'it terminates and stays resident (exit type 3)');
  const after = memFree();
  const kept = before - after;
  t.ok(kept > 0 && kept < 12 * 1024, `resident size ${kept} bytes (default /B:512 /Q:10)`);
  t.lines(s.run(P), ['PRINT queue is empty'], 'PRINT (again): the resident copy answers');
  t.ok(exitType() === 0, 'the second run just exits');

  printed = [];
  t.lines(s.run(`${P} \\WORK\\TABS.TXT`), listing('C:\\WORK\\TABS.TXT'), 'PRINT file: queue listing');
  s.pc.run(1500);
  t.bytes(Buffer.from(printed), EXP('TABS.PRN'), 'printed bytes: tabs to 8 columns, BS, ^Z ends it, form feed');

  printed = [];
  t.lines(s.run(`${P} \\WORK\\BIG.TXT \\WORK\\A.TXT \\WORK\\B.TXT`),
    listing('C:\\WORK\\BIG.TXT', 'C:\\WORK\\A.TXT', 'C:\\WORK\\B.TXT'), 'three files queued');
  s.run('SPIN 40');
  t.ok(marks['SPIN>'] > marks['SPIN<'], `prints from INT 1Ch while a program runs (${marks['SPIN>'] - marks['SPIN<']} bytes in 40 ticks)`);
  let n0 = printed.length;
  s.pc.run(1000);
  t.ok(printed.length > n0, `prints from INT 28h at the prompt (${printed.length - n0} bytes in 1 s)`);
  n0 = printed.length;
  const fo = s.run('\\DOS\\FIND.EXE "Line 00002" \\WORK\\BIG.TXT');
  t.ok(fo[1] === '---------- \\WORK\\BIG.TXT' && fo.length === 3 && fo[2].startsWith('Line 00002'),
    'other programs and typed commands work while it prints', fo.join('\n'));
  t.ok(printed.length > n0, 'still printing meanwhile');
  const big = fs.readFileSync(B('printtest/BIG.TXT'), 'latin1');
  // PRINT resets its column only at a CR: TABS.TXT ended at column 5 ("AFTER"^Z)
  const expand = (txt, col = 5) => { let o = ''; for (const c of txt) { if (c === '\x1a') break; if (c === '\r') col = 0; if (c === '\t') { do { o += ' '; col++; } while (col & 7); continue; } if (c === '\b') col--; if (c >= ' ') col++; o += c; } return o; };
  { const got = prn(), want = expand(big); let k = 0; while (k < got.length && got[k] === want[k]) k++;
    t.ok(k === got.length, `the printed text is BIG.TXT with its tabs expanded (${got.length} bytes so far)`,
      `differs at ${k}: ${JSON.stringify(got.slice(Math.max(0, k - 20), k + 40))} vs ${JSON.stringify(want.slice(Math.max(0, k - 20), k + 40))}`); }

  t.lines(s.run(`${P} /C \\WORK\\ZZ.TXT`),
    ['File not in PRINT queue - C:\\WORK\\ZZ.TXT', ...listing('C:\\WORK\\BIG.TXT', 'C:\\WORK\\A.TXT', 'C:\\WORK\\B.TXT')],
    'PRINT /C of a file not in the queue');
  t.lines(s.run(`${P} /C \\WORK\\A.TXT`), listing('C:\\WORK\\BIG.TXT', 'C:\\WORK\\B.TXT'), 'PRINT /C of a queued file');
  t.lines(s.run(`${P} /C \\WORK\\BIG.TXT`), listing('C:\\WORK\\B.TXT'), 'PRINT /C of the file being printed');
  s.pc.run(1500);
  const tail = prn();
  const cm = '\r\n\nFile C:\\WORK\\BIG.TXT canceled by operator\r\x0c\x07';
  const i = tail.indexOf(cm);
  t.ok(i > 0 && tail.slice(i + cm.length) === fs.readFileSync(path.join(ROOT, 'apps/dosutil/tests/data/WORK/B.TXT'), 'latin1') + '\x0c',
    'the cancel message goes to the printer, then the next file', JSON.stringify(tail.slice(-160)));

  // errors (the listing still follows)
  t.lines(s.run(`${P} \\NOFILE.TXT`), ['File not found - C:\\NOFILE.TXT', 'PRINT queue is empty'], 'missing file');
  t.lines(s.run(`${P} Q:X.TXT`), ['Invalid drive specification', 'PRINT queue is empty'], 'bad drive');
  t.lines(s.run(`${P} \\NODIR\\X.TXT`), ['Path not found - C:\\NODIR\\X.TXT', 'PRINT queue is empty'], 'bad path');
  t.lines(s.run(`${P} /D:LPT1`), ['Invalid switch -  /D:LPT1'], 'install switches are refused once resident');
  t.lines(s.run(`${P} /Q:5`), ['Invalid switch -  /Q:5'], '(/Q too)');
  t.lines(s.run(`${P} /X`), ['Invalid switch -  /X'], 'unknown switch');
  t.lines(s.run(`${P} A B C`), ['File not found - C:\\A', 'File not found - C:\\B', 'File not found - C:\\C', 'PRINT queue is empty'],
    'several missing files');

  // queue full: BIG, then README A B C BIG TABS, then README A B -> 10
  const W = (n) => `C:\\WORK\\${n}.TXT`;
  t.lines(s.run(`${P} \\WORK\\BIG.TXT \\WORK\\*.TXT \\WORK\\*.TXT`),
    ['PRINT queue is full', ...listing(W('BIG'), W('README'), W('A'), W('B'), W('C'), W('BIG'), W('TABS'), W('README'), W('A'), W('B'))],
    'wildcards, and "PRINT queue is full" once');
  t.lines(s.run(`${P} \\WORK\\*.TXT /C`), ['PRINT queue is empty'], 'PRINT *.TXT /C cancels every match (the current file too)');
  s.run(`${P} \\WORK\\BIG.TXT \\WORK\\A.TXT`);
  printed = [];
  t.lines(s.run(`${P} /T`), ['PRINT queue is empty'], 'PRINT /T');
  t.bytes(Buffer.from(printed).subarray(-36), EXP('ALLCAN.PRN'), '"All files canceled by operator" on the printer');
  t.lines(s.run(`${P} \\WORK\\A.TXT+\\WORK\\B.TXT`), listing(W('A'), W('B')), '"+" separates file names');
  s.pc.run(1500);
  t.ok(s.pc.faults.length === 0, 'no CPU faults', JSON.stringify(s.pc.faults.slice(0, 3)));
}

// ======================================== session 2: before installation
{
  const s = await session({ name: 'print2', files });
  const cases = [
    ['/B:100', 'Parameter value not in allowed range -  /B:100'],
    ['/Q:40', 'Parameter value not in allowed range -  /Q:40'],
    ['/S:0', 'Parameter value not in allowed range -  /S:0'],
    ['/B:600X', 'Parameter format not correct -  /B:600X'],
    ['/D:', 'Parameter format not correct -  /D:'],
  ];
  for (const [a, w] of cases) t.lines(s.run(`${P} ${a}`), [w], `PRINT ${a} (not installed)`);
  t.lines(s.run(P, { steps: [['Name of list device [PRN]:', 'lpt1:\r']] }),
    ['Name of list device [PRN]: lpt1:', 'Resident part of PRINT installed', 'PRINT queue is empty'], 'first run asks for the device');
  t.lines(s.run(`${P} /D:FOO`), ['Invalid switch -  /D:FOO'], 'then /D is refused');
}

// ============================= session 3: 4.0's "PRN" leftovers, /Q, /D:NUL
{
  const s = await session({ name: 'print3', files });
  t.lines(s.run(P, { steps: [['Name of list device [PRN]:', 'lp\r']] }),
    ['Name of list device [PRN]: lp', 'List output is not assigned to a device'],
    '"LP" leaves "LPN": no such device (as 4.0)');
  t.lines(s.run(`${P} /D:NUL /Q:4 /B:1024 /S:5 /U:3 /M:4 \\WORK\\BIG.TXT`),
    ['Resident part of PRINT installed', '', '', '  C:\\WORK\\BIG.TXT is currently being printed'], 'installed with all the switches');
  t.lines(s.run(`${P} \\WORK\\A.TXT \\WORK\\B.TXT \\WORK\\C.TXT \\WORK\\README.TXT`),
    ['PRINT queue is full', '', '', '  C:\\WORK\\BIG.TXT is currently being printed',
      '  C:\\WORK\\A.TXT is in queue', '  C:\\WORK\\B.TXT is in queue', '  C:\\WORK\\C.TXT is in queue'], '/Q:4');
  t.lines(s.run(`${P} /T \\WORK\\A.TXT`), ['', '', '  C:\\WORK\\A.TXT is currently being printed'], 'PRINT /T file');
}
// ===================== session 4: an error after the resident part went in
{
  const s = await session({ name: 'print4', files });
  t.lines(s.run(`${P} /D:PRN \\WORK\\A.TXT /X`), ['Resident part of PRINT installed', 'Invalid switch - /X'],
    'an error after installing: nothing stays behind');
  t.lines(s.run(`${P} /D:NUL`), ['Resident part of PRINT installed', 'PRINT queue is empty'], 'so the next PRINT installs');
  s.run('\\DOS\\FIND.EXE "x" \\WORK\\A.TXT');
  s.pc.run(500);
  t.ok(s.pc.faults.length === 0, 'and the machine is fine');
}
t.done();
