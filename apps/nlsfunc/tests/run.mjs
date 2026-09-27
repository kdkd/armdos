#!/usr/bin/env node
// apps/nlsfunc/tests/run.mjs - COUNTRY.SYS, COUNTRY=, NLSFUNC and CHCP tests
// ("make nlsfunc-test").
//
//   node apps/nlsfunc/tests/run.mjs
//
// 1. COUNTRY.SYS: tools/mkcountry.mjs assembles MS-DOS 4.00's MKCNTRY.ASM into
//    exactly MS-DOS 4.00's COUNTRY.SYS (compared when the original is at hand);
//    ARM-DOS's file adds Brazil (055).
// 2. COUNTRY=: DATE, TIME and DIR in each country's formats, as MS-DOS 4.00
//    printed them under DOSBox-X ("real"; the dates here are the emulator's
//    1 January 2026 and the image's 17 June 1988 12:00), and the code page
//    COUNTRY= picks.
// 3. CHCP with NLSFUNC and DISPLAY.SYS: the messages and what happens to the
//    screen's and KEYB's code page, as the genuine CHCP/NLSFUNC/DISPLAY.SYS did.

import fs from 'node:fs';
import path from 'node:path';
import { B, checker, run, redirected } from '../../keyb/tests/nlskit.mjs';
import { buildCountrySys, parseCountrySys } from '../tools/mkcountry.mjs';
import { rescheck } from '../../ansi/tests/rescheck.mjs';

const T = checker();
const E = (s) => s.split('\n').join('\r\n');

// ------------------------------------------------------------ 1. the file
{
  console.log('== COUNTRY.SYS');
  const ms = process.env.ARMDOS_REFS ? path.join(process.env.ARMDOS_REFS, 'msdos400-pcjs/files/COUNTRY.SYS') : '';   // optional: MS-DOS 4.00's own file
  if (ms && fs.existsSync(ms)) T.check(Buffer.compare(buildCountrySys({ ms: true }), fs.readFileSync(ms)) === 0, 'mkcountry + MKCNTRY.ASM = MS-DOS 4.00 COUNTRY.SYS, byte for byte');
  else console.log('skip (no MS-DOS 4.00 COUNTRY.SYS to compare with)');
  const built = fs.readFileSync(B('COUNTRY.SYS'));
  T.check(Buffer.compare(built, buildCountrySys()) === 0, 'build/COUNTRY.SYS is up to date');
  const e = parseCountrySys(built);
  const has = (c, cp) => e.some((x) => x.country === c && x.cp === cp);
  T.check([[1, 437], [44, 850], [49, 437], [33, 850], [34, 850], [39, 437], [31, 850], [45, 865], [47, 865], [46, 437], [358, 850],
    [41, 850], [2, 863], [351, 860], [3, 850], [55, 850], [55, 437]].every(([c, cp]) => has(c, cp)),
    'US UK DE FR SP IT NL DK NO SE FI CH CA PO LA and (ARM-DOS) BR, in their code pages');
  T.check(e.every((x) => x.items[2].equals(x.items[4])), 'upper case = file-name upper case everywhere (the kernel keeps one table)');
  const { bad } = rescheck(B('obj/NLSFUNC/NLSFUNC.elf'), 'zero', 'nls_res_end');
  T.check(bad.length === 0, 'NLSFUNC\'s resident part refers to nothing beyond nls_res_end' + (bad.length ? '\n       ' + bad.join('\n       ') : ''));
}

// ------------------------------------------------------------ 2. COUNTRY=
// [country line, date, date prompt, time (regex), DIR date/time, CHCP]
const COUNTRIES = [
  ['', 'Thu 01-01-2026', 'mm-dd-yy', /^12:00:\d\d\.\d\dp$/, '06-17-88  12:00p', 437, 'US, as without COUNTRY='],
  ['COUNTRY=001,850,C:\\DOS\\COUNTRY.SYS', 'Thu 01-01-2026', 'mm-dd-yy', /^12:00:\d\d\.\d\dp$/, '06-17-88  12:00p', 850, 'real'],
  ['COUNTRY=049,,C:\\DOS\\COUNTRY.SYS', 'Thu 01.01.2026', 'dd-mm-yy', /^ 12\.00\.\d\d,\d\d$/, '17.06.88   12.00', 437, 'real'],
  ['COUNTRY=049', 'Thu 01.01.2026', 'dd-mm-yy', /^ 12\.00\.\d\d,\d\d$/, '17.06.88   12.00', 437, 'ARM-DOS: \\DOS\\COUNTRY.SYS when the root has none'],
  ['COUNTRY=044,,C:\\DOS\\COUNTRY.SYS', 'Thu 01-01-2026', 'dd-mm-yy', /^12:00:\d\d\.\d\dp$/, '17-06-88  12:00p', 437, 'real'],
  ['COUNTRY=033,,C:\\DOS\\COUNTRY.SYS', 'Thu 01/01/2026', 'dd-mm-yy', /^ 12:00:\d\d,\d\d$/, '17/06/88   12:00', 437, 'real'],
  ['COUNTRY=046,,C:\\DOS\\COUNTRY.SYS', 'Thu 2026-01-01', 'yy-mm-dd', /^ 12\.00\.\d\d,\d\d$/, '88-06-17   12.00', 437, 'real'],
  ['COUNTRY=041,,C:\\DOS\\COUNTRY.SYS', 'Thu 01.01.2026', 'dd-mm-yy', /^ 12\.00\.\d\d\.\d\d$/, '17.06.88   12.00', 850, 'real'],
  ['COUNTRY=045,,C:\\DOS\\COUNTRY.SYS', 'Thu 01-01-2026', 'dd-mm-yy', /^ 12\.00\.\d\d,\d\d$/, '17-06-88   12.00', 850, 'real'],
  ['COUNTRY=002,,C:\\DOS\\COUNTRY.SYS', 'Thu 2026-01-01', 'yy-mm-dd', /^ 12:00:\d\d,\d\d$/, '88-06-17   12:00', 863, 'real'],
  ['COUNTRY=003,,C:\\DOS\\COUNTRY.SYS', 'Thu 01/01/2026', 'dd-mm-yy', /^ 12:00:\d\d\.\d\d$/, '17/06/88   12:00', 850, 'real'],
  ['COUNTRY=039,,C:\\DOS\\COUNTRY.SYS', 'Thu 01/01/2026', 'dd-mm-yy', /^ 12:00:\d\d,\d\d$/, '17/06/88   12:00', 437, 'real'],
  ['COUNTRY=031,,C:\\DOS\\COUNTRY.SYS', 'Thu 01-01-2026', 'dd-mm-yy', /^ 12:00:\d\d,\d\d$/, '17-06-88   12:00', 437, 'real'],
  ['COUNTRY=358,,C:\\DOS\\COUNTRY.SYS', 'Thu 01.01.2026', 'dd-mm-yy', /^ 12\.00\.\d\d,\d\d$/, '17.06.88   12.00', 850, 'real'],
  ['COUNTRY=047,,C:\\DOS\\COUNTRY.SYS', 'Thu 01.01.2026', 'dd-mm-yy', /^ 12\.00\.\d\d,\d\d$/, '17.06.88   12.00', 850, 'real'],
  ['COUNTRY=034,,C:\\DOS\\COUNTRY.SYS', 'Thu 01/01/2026', 'dd-mm-yy', /^ 12:00:\d\d,\d\d$/, '17/06/88   12:00', 850, 'real'],
  ['COUNTRY=351,,C:\\DOS\\COUNTRY.SYS', 'Thu 01/01/2026', 'dd-mm-yy', /^ 12:00:\d\d,\d\d$/, '17/06/88   12:00', 850, 'real'],
  ['COUNTRY=055,,C:\\DOS\\COUNTRY.SYS', 'Thu 01/01/2026', 'dd-mm-yy', /^ 12:00:\d\d,\d\d$/, '17/06/88   12:00', 850, 'ARM-DOS: Brazil'],
];
console.log('== COUNTRY=');
for (const [line, date, prompt, time, dir, cp, note] of COUNTRIES) {
  const pc = await run('country', {
    config: `FILES=20\n${line}\n`,
    autoexec: redirected(['ECHO.| DATE', 'ECHO.| TIME', 'DIR C:\\DOS\\EGA.CPI', 'CHCP']),
  });
  const d = pc.out(0) || '', t = pc.out(1) || '', di = pc.out(2) || '';
  const tm = (/Current time is (.*)\r\n/.exec(t) || [])[1] || '';
  const dirLine = (di.split('\r\n').find((l) => l.startsWith('EGA')) || '').slice(23);
  const ok = d === E(`Current date is ${date}\nEnter new date (${prompt}): \r\n`) && time.test(tm) && dirLine === dir && pc.out(3) === E(`Active code page: ${cp}\n`);
  T.check(pc.done && ok, `${line || '(no COUNTRY=)'}: date ${date}, time "${tm}", DIR ${dir}, code page ${cp}   [${note}]`,
    `${JSON.stringify(d)} ${JSON.stringify(tm)} ${JSON.stringify(dirLine)} ${JSON.stringify(pc.out(3))}`);
}
{
  const pc = await run('country-bad', { config: 'FILES=20\nCOUNTRY=049,,C:\\NOPE.SYS\nCOUNTRY=999,,C:\\DOS\\COUNTRY.SYS\nCOUNTRY=049,865,C:\\DOS\\COUNTRY.SYS\n', autoexec: '' });
  const s = pc.lines().map((l) => l.trimEnd()).join('\n');
  T.check(s.includes('\nBad or missing C:\\NOPE.SYS\nError in CONFIG.SYS line 2\n'), 'COUNTRY= with a missing file: "Bad or missing C:\\NOPE.SYS"   [real]');
  T.check(s.includes('\nInvalid country code or code page\nError in CONFIG.SYS line 3\n\nInvalid country code or code page\nError in CONFIG.SYS line 4'),
    'COUNTRY=999 and COUNTRY=049,865: "Invalid country code or code page"   [real]');
}

// ------------------------------------------------------------ 3. CHCP
{
  console.log('== CHCP');
  const cmds = [
    ['MODE CON CP PREPARE=((850) C:\\DOS\\EGA.CPI)', E('\nMODE prepare code page function completed\n'), 'real'],
    ['MODE CON CP SELECT=850', E('\nMODE select code page function completed\n'), 'real'],
    ['KEYB GR', '', 'real'],
    ['CHCP', E('Active code page: 437\n'), 'real: MODE does not change the global code page'],
    ['CHCP 850', '', 'real: "NLSFUNC not installed" on the screen'],
    ['NLSFUNC', '', 'real'],
    ['NLSFUNC', '', 'real: "NLSFUNC already installed" on the screen'],
    ['CHCP 437', '', 'real'],
    ['CHCP', E('Active code page: 437\n'), 'real'],
    ['MODE CON CP /STATUS', E('\nActive code page for device CON is 437\nHardware code pages:\n  code page 437\nPrepared code pages:\n  code page 850\n\nMODE status code page function completed\n'), 'real: CHCP selected 437 on CON'],
    ['KEYB', E('Current keyboard code: GR  code page: 437\nCurrent CON code page: 437\n'), 'real: and KEYB followed'],
    ['CHCP 850', '', 'real'],
    ['CHCP 865', '', 'real: "Invalid code page" (Germany has no 865)'],
    ['CHCP', E('Active code page: 850\n'), 'real'],
    ['KEYB', E('Current keyboard code: GR  code page: 850\nCurrent CON code page: 850\n'), 'real'],
    ['ECHO.| DATE', E('Current date is Thu 01.01.2026\nEnter new date (dd-mm-yy): \r\n'), 'real: still Germany'],
    ['MODE CON CP PREPARE=((860) C:\\DOS\\EGA.CPI)', E('\nMODE prepare code page function completed\n'), 'real'],
    ['CHCP 850', '', 'real: "Code page 850 not prepared for all devices"'],
    ['CHCP', E('Active code page: 850\n'), 'ARM-DOS: a failed CHCP changes nothing'],
  ];
  const pc = await run('chcp', {
    config: 'FILES=20\nCOUNTRY=049,,C:\\DOS\\COUNTRY.SYS\nDEVICE=C:\\DOS\\DISPLAY.SYS CON=(EGA,437,1)\n',
    autoexec: redirected(cmds.map((c) => c[0])),
  });
  T.check(pc.done, 'all commands ran');
  cmds.forEach(([cmd, want, note], i) => T.check(pc.out(i) === want, `${cmd}   [${note}]`, `got  ${JSON.stringify(pc.out(i))}\n       want ${JSON.stringify(want)}`));
  const s = pc.lines().map((l) => l.trimEnd()).filter(Boolean);
  const want = ['NLSFUNC not installed', 'NLSFUNC already installed', 'Invalid code page', 'Code page 850 not prepared for all devices'];
  const at = want.map((w) => s.indexOf(w));
  T.check(at.every((k, i) => k >= 0 && (i === 0 || k > at[i - 1])), 'on the screen, in order: ' + want.join(' / ') + '   [real]', s.join(' | '));

  // without COUNTRY=: NLSFUNC finds C:\DOS\COUNTRY.SYS (ARM-DOS; DOS 4.00 looks in \ only)
  const pc2 = await run('chcp2', { config: 'FILES=20\n', autoexec: 'NLSFUNC\n' + redirected(['CHCP 850', 'CHCP', 'NLSFUNC C:\\NOPE.SYS']) });
  T.eq(pc2.out(1), E('Active code page: 850\n'), 'no COUNTRY=: NLSFUNC finds \\DOS\\COUNTRY.SYS, CHCP 850 works   [ARM-DOS]');
  const pc3 = await run('chcp3', { config: 'FILES=20\n', autoexec: 'NLSFUNC C:\\NOPE.SYS\n' + redirected(['CHCP 850', 'CHCP']) });
  T.check(pc3.lines().some((l) => l.trim() === 'File not found'), 'NLSFUNC C:\\NOPE.SYS: "File not found"');
  T.eq(pc3.out(0), '', '   ... and CHCP 850 says "NLSFUNC not installed" (on the screen)');
}

console.log(T.failures ? `\n${T.failures} failure(s)` : '\nall NLSFUNC / COUNTRY tests passed');
process.exit(T.failures ? 1 : 0);
