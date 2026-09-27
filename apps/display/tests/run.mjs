#!/usr/bin/env node
// apps/display/tests/run.mjs - DISPLAY.SYS, EGA.CPI and MODE CON CP tests
// ("make display-test").
//
//   node apps/display/tests/run.mjs
//
// 1. EGA.CPI: the FONT file structure, five code pages x three fonts, the 437
//    fonts = the ROM's (emu/fonts).
// 2. DISPLAY.SYS's CONFIG.SYS messages and MODE CON CP's output, compared with
//    what MS-DOS 4.00's DISPLAY.SYS and MODE printed under DOSBox-X ("real").
// 3. The glyphs: after MODE CON CP SELECT=850 the character generator RAM holds
//    code page 850's 8x16 font, also after a mode set (MODE CO80), 850's 8x8
//    font in 50-line mode (ANSI.SYS, which builds it with INT 10h AX=1130h),
//    the ROM font again after SELECT=437, and 850 again after NLSFUNC + CHCP 850 and
//    a mode set; screenshots of each go to
//    build/display-test/*.png (look at them).

import fs from 'node:fs';
import path from 'node:path';
import { B, ROOT, checker, run, redirected } from '../../keyb/tests/nlskit.mjs';
import { buildCpi, fontFile, CPI_PAGES, CPI_HEIGHTS } from '../tools/mkcpi.mjs';
import { rescheck } from '../../ansi/tests/rescheck.mjs';

const T = checker();
const E = (s) => s.split('\n').join('\r\n');
const OUT = B('display-test');
fs.mkdirSync(OUT, { recursive: true });

// ------------------------------------------------------------ 1. EGA.CPI
{
  console.log('== EGA.CPI');
  const cpi = fs.readFileSync(B('EGA.CPI'));
  T.check(Buffer.compare(cpi, buildCpi()) === 0, 'build/EGA.CPI is up to date');
  T.eq(cpi.toString('latin1', 0, 8), '\xFFFONT   ', 'FONT signature');
  const fih = cpi.readUInt32LE(0x13);
  T.eq(cpi.readUInt16LE(fih), 5, 'five code pages');
  let p = fih + 2, ok = true;
  const seen = [];
  for (let i = 0; i < 5; i++) {
    const cp = cpi.readUInt16LE(p + 16), cpih = cpi.readUInt32LE(p + 24);
    seen.push(cp);
    ok &&= cpi.readUInt16LE(p) === 0x1C && cpi.readUInt16LE(p + 6) === 1 && cpi.toString('latin1', p + 8, p + 16) === 'EGA     ';
    let f = cpih + 6;
    ok &&= cpi.readUInt16LE(cpih + 2) === 3;
    for (const h of CPI_HEIGHTS) {
      ok &&= cpi[f] === h && cpi[f + 1] === 8 && cpi.readUInt16LE(f + 4) === 256;
      ok &&= Buffer.compare(cpi.subarray(f + 6, f + 6 + 256 * h), fontFile(cp, h)) === 0;
      f += 6 + 256 * h;
    }
    p = cpi.readUInt32LE(p + 2);
  }
  T.check(ok, 'each code page: a 1Ch-byte "EGA" entry, three fonts (16, 14, 8 lines) of 256 characters');
  T.eq(seen.join(' '), CPI_PAGES.join(' '), 'code pages 437 850 860 863 865');
  T.check(Buffer.compare(fontFile(437, 16), fs.readFileSync(path.join(ROOT, 'emu/fonts/vga8x16.bin'))) === 0 &&
          Buffer.compare(fontFile(437, 8), fs.readFileSync(path.join(ROOT, 'emu/fonts/cga8x8.bin'))) === 0,
          'the 437 fonts are the ROM\'s (8x16, 8x8)');
  const f850 = fontFile(850, 16), f437 = fontFile(437, 16);
  const differs = [];
  for (let c = 0x80; c < 0x100; c++) if (Buffer.compare(f850.subarray(c * 16, c * 16 + 16), f437.subarray(c * 16, c * 16 + 16))) differs.push(c);
  T.check(differs.length > 40 && differs.includes(0x9B) && !differs.includes(0x81), `850 differs from 437 where the code pages do (${differs.length} glyphs; 9Bh o-slash, not 81h u-umlaut)`);
  const { bad } = rescheck(B('obj/DISPLAY/DISPLAY.elf'), '0', 'display_res_end', ['disp_init', 'display_res_end']);
  T.check(bad.length === 0, 'resident part refers to nothing beyond display_res_end but INIT' + (bad.length ? '\n       ' + bad.join('\n       ') : ''));
}

// ------------------------------------------------------------ 2. messages
const CPNS = 'Code page operation not supported on this device\r\n';
async function configMessage(name, line, want, note, boot) {
  const pc = await run(name, { config: `FILES=20\n${line}\n`, autoexec: redirected(['MODE CON CP /STATUS']), boot });
  const lines = pc.lines().map((l) => l.trimEnd());
  T.check(pc.done && lines.includes(want), `${line}  ->  "${want}"  [${note}]`, lines.filter(Boolean).slice(-6).join(' | '));
  T.eq(pc.out(0), CPNS, `   ... and DISPLAY.SYS is not installed [real]`);
}
console.log('== DISPLAY.SYS in CONFIG.SYS');
await configMessage('disp-nosyntax', 'DEVICE=C:\\DOS\\DISPLAY.SYS', 'Invalid syntax on DISPLAY.SYS code page driver', 'real');
await configMessage('disp-vga', 'DEVICE=C:\\DOS\\DISPLAY.SYS CON=(VGA,437,1)', 'CON code page driver cannot be initialized', 'real');
await configMessage('disp-toomany', 'DEVICE=C:\\DOS\\DISPLAY.SYS CON=(EGA,437,20)', 'Insufficient memory', 'real');
await configMessage('disp-herc', 'DEVICE=C:\\DOS\\DISPLAY.SYS CON=(EGA,437,1)', 'CON code page driver cannot be initialized', 'Hercules: its font is a ROM', { video: 'hercules' });

const ST = (active, prepared, hw = [437]) => (active ? `\nActive code page for device CON is ${active}\n` : 'No code page has been selected\n') +
  (hw.length ? 'Hardware code pages:\n' + hw.map((c) => `  code page ${c}\n`).join('') : '') +
  'Prepared code pages:\n' + prepared.map((c) => c ? `  code page ${c}\n` : '  code page not prepared\n').join('') +
  '\nMODE status code page function completed\n';
const mode = [
  ['MODE CON CP /STATUS', ST(0, [0]), 'real'],
  ['MODE CON', '\nStatus for device CON:\n----------------------\n' + ST(0, [0]), 'real'],
  ['MODE CON CP SELECT=850', 'Code page not prepared\n', 'real'],
  ['MODE CON CP PREPARE=((850) C:\\DOS\\EGA.CPI)', '\nMODE prepare code page function completed\n', 'real'],
  ['MODE CON CP /STATUS', ST(0, [850]), 'real'],
  ['MODE CON CP SELECT=850', '\nMODE select code page function completed\n', 'real'],
  ['MODE CON CP /STATUS', ST(850, [850]), 'real'],
  ['MODE CON', '\nStatus for device CON:\n----------------------\n' + ST(850, [850]), 'real'],
  ['MODE CON CP REFRESH', '\nMODE refresh code page function completed\n', 'real'],
  ['MODE CON CP PREPARE=((865,850) C:\\DOS\\EGA.CPI)', 'Device error during prepare\n', 'real: more code pages than slots'],
  ['MODE CON CP PREPARE=((860) C:\\DOS\\EGA.CPI)', '\nMODE prepare code page function completed\n', 'real'],
  ['MODE CON CP /STATUS', ST(0, [860]), 'real: the selected code page was replaced'],
  ['MODE CON CP SELECT=437', '\nMODE select code page function completed\n', 'real'],
  ['MODE CON CP SELECT=863', 'Code page not prepared\n', 'real'],
  ['MODE CON CP PREPARE=((850) C:\\NO.CPI)', '\nFailure to access code page font file\n', 'real'],
  ['MODE CON CP', ST(437, [860]), 'real'],
  ['MODE CON CODEPAGE /STA', ST(437, [860]), 'real'],
  ['MODE CON CP PREP=((999) C:\\DOS\\EGA.CPI)', '\nFont file contents invalid\nDevice error during prepare\n', 'real'],
  ['MODE CON CP /STATUS', ST(437, [0]), 'real: the failed slot is empty'],
  ['MODE CON CP PREP=((850) C:\\DOS\\COUNTRY.SYS)', '\nFont file contents invalid\nDevice error during prepare\n', 'real (with a printer .CPI)'],
  ['MODE CON CP SEL=X', '\nInvalid parameter - SEL=X \n', 'MODE 4.00 quotes the last operand with the blank before ">"'],
];
{
  console.log('== MODE CON CP with DISPLAY.SYS CON=(EGA,437,1)');
  const pc = await run('disp-mode', { config: 'FILES=20\nDEVICE=C:\\DOS\\DISPLAY.SYS CON=(EGA,437,1)\n', autoexec: redirected(mode.map((m) => m[0])) });
  T.check(pc.done, 'all commands ran');
  mode.forEach(([cmd, want, note], i) => T.check(pc.out(i) === E(want), `${cmd}${note ? `   [${note}]` : ''}`,
    `got  ${JSON.stringify(pc.out(i))}\n       want ${JSON.stringify(E(want))}`));
  const two = [
    ['MODE CON CP PREP=((850,860) C:\\DOS\\EGA.CPI)', '\nMODE prepare code page function completed\n', 'real'],
    ['MODE CON CP /STATUS', ST(0, [850, 860]), 'real'],
    ['MODE CON CP PREP=((,865) C:\\DOS\\EGA.CPI)', '\nMODE prepare code page function completed\n', 'real: -1 keeps a slot'],
    ['MODE CON CP /STATUS', ST(0, [850, 865]), 'real'],
    ['MODE CON CP PREP=((850,850) C:\\DOS\\EGA.CPI)', 'Device error during prepare\n', 'DISPLAY 4.00: the same code page twice'],
  ];
  const pc2 = await run('disp-mode2', { config: 'FILES=20\nDEVICE=C:\\DOS\\DISPLAY.SYS CON=(EGA,437,2)\n', autoexec: redirected(two.map((m) => m[0])) });
  two.forEach(([cmd, want, note], i) => T.check(pc2.out(i) === E(want), `(EGA,437,2) ${cmd}${note ? `   [${note}]` : ''}`,
    `got  ${JSON.stringify(pc2.out(i))}\n       want ${JSON.stringify(E(want))}`));
}

// ------------------------------------------------------------ 3. glyphs
{
  console.log('== glyphs');
  let chars = '';
  for (let r = 0; r < 8; r++) chars += Buffer.from(Array.from({ length: 16 }, (_, k) => 0x80 + r * 16 + k)).toString('latin1') + '\n';
  const fontRam = (pc) => pc.machine.font;
  const same = (ram, font, h, rows = h, skip = 0) => {
    for (let c = 0; c < 256; c++) for (let y = 0; y < 32; y++) {
      const want = y < rows ? font[c * h + y + skip] : 0;
      if (ram[c * 32 + y] !== want) return `glyph ${c.toString(16)} row ${y}: ${ram[c * 32 + y]} != ${want}`;
    }
    return '';
  };
  const f850 = fontFile(850, 16), f850_8 = fontFile(850, 8), f437 = fontFile(437, 16);
  const shots = {};
  const pc = await run('disp-glyphs', {
    config: 'FILES=20\nDEVICE=C:\\DOS\\DISPLAY.SYS CON=(EGA,437,2)\nDEVICE=C:\\DOS\\ANSI.SYS\n',
    autoexec: 'MODE CON CP PREPARE=((850,865) C:\\DOS\\EGA.CPI) > NUL\nMODE CON CP SELECT=850 > NUL\nCLS\nTYPE C:\\CHARS.TXT\nECHO STEP-1\nPAUSE > NUL\n' +
      'MODE CO80\nTYPE C:\\CHARS.TXT\nECHO STEP-2\nPAUSE > NUL\nMODE CON LINES=50\nTYPE C:\\CHARS.TXT\nECHO STEP-3\nPAUSE > NUL\n' +
      'MODE CON LINES=25\nMODE CON CP SELECT=437 > NUL\nTYPE C:\\CHARS.TXT\nECHO STEP-4\nPAUSE > NUL\n' +
      'NLSFUNC\nCHCP 850\nMODE CO80\nTYPE C:\\CHARS.TXT\nECHO STEP-5\nPAUSE > NUL\n',
    extra: [{ dst: 'CHARS.TXT', text: Buffer.from(chars.replace(/\n/g, '\r\n'), 'latin1') }],
  }, async (pc) => {
    for (let s = 1; s <= 5; s++) {
      if (!pc.until(() => pc.hasText(`STEP-${s}`), { timeoutMs: 60000 })) break;
      pc.run(300);
      shots[s] = { font: Uint8Array.from(fontRam(pc)), rows: pc.cpu.m8[0x484] + 1, height: pc.cpu.m8[0x485], cp: pc.machine.nls.cp };
      await pc.png(path.join(OUT, `step${s}.png`));
      pc.key('Space');
    }
  });
  T.check(pc.done, 'all steps ran');
  const s1 = shots[1], s2 = shots[2], s3 = shots[3], s4 = shots[4];
  T.check(s1 && !same(s1.font, f850, 16), 'MODE CON CP SELECT=850: the character generator holds code page 850\'s 8x16 font (build/display-test/step1.png)', s1 && same(s1.font, f850, 16));
  T.eq(s1 && s1.cp, 1, 'port F7h says code page 850 (index 1)');
  T.check(s2 && !same(s2.font, f850, 16), 'after a mode set (MODE CO80) still code page 850 (step2.png)', s2 && same(s2.font, f850, 16));
  T.check(s3 && s3.rows === 50 && s3.height === 8 && !same(s3.font, f850_8, 8), 'MODE CON LINES=50 (ANSI.SYS): 850\'s 8x8 font (step3.png)',
    s3 && `${s3.rows} rows, height ${s3.height}: ${same(s3.font, f850_8, 8)}`);
  T.check(s4 && !same(s4.font, f437, 16), 'MODE CON CP SELECT=437: the ROM font again (step4.png)', s4 && same(s4.font, f437, 16));
  T.eq(s4 && s4.cp, 0, 'port F7h says 437 again');
  const s5 = shots[5];
  T.check(s5 && !same(s5.font, f850, 16), 'NLSFUNC, CHCP 850, MODE CO80: code page 850\'s font again (step5.png)', s5 && same(s5.font, f850, 16));
}

console.log(T.failures ? `\n${T.failures} failure(s)` : '\nall DISPLAY tests passed');
process.exit(T.failures ? 1 : 0);
