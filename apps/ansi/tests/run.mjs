#!/usr/bin/env node
// apps/ansi/tests/run.mjs - ANSI.SYS tests ("make ansi-test").
//
//   node apps/ansi/tests/run.mjs [name-substring ...] [--keep]
//
// Each scenario builds a hard disk (IO.SYS, ARMDOS.SYS, a CONFIG.SYS with
// DEVICE=C:\DOS\ANSI.SYS, the kernel's test shell TSHELL and its script,
// the helper programs from build/ansi-test/), boots it headless and checks
// the screen memory, the COM1 log (T:... lines) and screenshots.
//
// "t1"/"t1raw" compare the screen after ACAT T1.ANS (every escape sequence
// and quirk ANSI.SYS has) cell by cell - character, foreground, background -
// with the screen the genuine MS-DOS 4.00 ANSI.SYS produced from the same
// file under DOSBox-X (tests/real/t1-dos400.txt, decoded from a screenshot).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage } from '../../../disk/mkimage.mjs';
import { CP437 } from '../../../emu/render.mjs';
import { rescheck } from './rescheck.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');
const B = (p) => path.join(ROOT, 'build', p);
const OUT = B('ansi-test');
fs.mkdirSync(OUT, { recursive: true });
const args = process.argv.slice(2);
const filters = args.filter((a) => !a.startsWith('--'));

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; return ok; };

// ------------------------------------------------------------ the disk
function makeImage(name, { config, script, extra = {} }) {
  const dir = path.join(OUT, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/ANSI.SYS', dst: 'DOS\\' },
    { src: 'build/ktest/TSHELL.EXE', dst: 'T\\' },
    { src: 'build/ansi-test/*.EXE', dst: 'T\\' },
    { src: 'apps/ansi/tests/T1.ANS', dst: 'T\\' },
    { src: 'apps/ansi/art/ANSI.ART', dst: 'DOS\\' },
  ];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, typeof text === 'string' ? text.replace(/\n/g, '\r\n') : text);
    files.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', `${config}SHELL=C:\\T\\TSHELL.EXE C:\\T\\S.TXT\n`);
  put('T\\S.TXT', script);
  for (const [dst, text] of Object.entries(extra)) put(dst, text);
  const m = {
    format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'ANSITEST',
    date: '1988-06-17 12:00:00', boot: { src: 'build/bootsect.bin' }, files, dirs: ['DOS', 'T'],
  };
  const { img } = buildImage(m, ROOT);
  if (args.includes('--keep')) fs.writeFileSync(path.join(OUT, name + '.img'), img);
  fs.rmSync(dir, { recursive: true, force: true });
  return img;
}

async function start(name, opts) {
  const hd = makeImage(name, opts);
  return boot({ rom: B('rom.bin'), hd });
}

// screen memory: [char, attr] of page 0
const cell = (pc, r, c, cols = 80) => {
  const a = 0xB8000 + (r * cols + c) * 2;
  return [pc.cpu.m8[a], pc.cpu.m8[a + 1]];
};

// CP437 -> Unicode as tools/decode.py prints it
function uni(b) { return b === 0 ? ' ' : CP437[b]; }

// tests/real/*.txt: rows "NN |text|" and attribute runs "NN: a-b=BG/FG ..."
function parseReal(file) {
  const t = fs.readFileSync(file, 'utf8').split('\n');
  const rows = [], attrs = [];
  for (const l of t) {
    let m = /^(\d\d) \|(.*)\|$/.exec(l);
    if (m) { rows[+m[1]] = [...m[2]]; continue; }
    m = /^(\d\d): (.*)$/.exec(l);
    if (m) {
      const a = [];
      for (const run of m[2].split(' ')) {
        const [, s, e, bg, fg] = /^(\d+)-(\d+)=([0-9A-F])\/([0-9A-F-])$/.exec(run);
        for (let c = +s; c <= +e; c++) a[c] = { bg: parseInt(bg, 16), fg: fg === '-' ? null : parseInt(fg, 16) };
      }
      attrs[+m[1]] = a;
    }
  }
  return { rows, attrs };
}

function compareReal(pc, realFile, skip) {
  const real = parseReal(realFile);
  const bad = [];
  for (let r = 0; r < 25; r++) {
    for (let c = 0; c < 80; c++) {
      if (skip(r, c)) continue;
      const [ch, at] = cell(pc, r, c);
      const want = real.attrs[r][c], wch = real.rows[r][c];
      const blink = at & 0x80, bg = (at >> 4) & 7, fg = at & 15;
      const blankish = ch === 32 || ch === 0 || ch === 255 || fg === bg;
      let ok = bg === want.bg;
      if (!blink) {
        if (want.fg === null) ok = ok && blankish;
        else ok = ok && fg === want.fg && uni(ch) === wch;
      }
      if (!ok) bad.push(`(${r},${c}) got '${uni(ch)}' ${at.toString(16).padStart(2, '0')}, real '${wch}' ${want.bg.toString(16)}/${want.fg === null ? '-' : want.fg.toString(16)}`);
    }
  }
  return bad;
}

// ------------------------------------------------------------ scenarios
const scenarios = [];

scenarios.push({
  name: 'resident',
  async run() {
    const { to, bad } = rescheck(B('obj/ANSI/ANSI.elf'), '0', 'ansi_res_end', ['ansi_init']);
    check(bad.length === 0, `resident part (${to} bytes) refers to nothing beyond ansi_res_end` +
      (bad.length ? '\n       ' + bad.join('\n       ') : ''));
  },
});

const t1 = (raw) => async () => {
  const pc = await start(raw ? 't1raw' : 't1', {
    config: 'DEVICE=C:\\DOS\\ANSI.SYS\n',
    script: `C:\\T\\ACAT.EXE ${raw ? '/R ' : ''}C:\\T\\T1.ANS\nhalt\n`,
  });
  check(pc.until(() => pc.serial.includes('T:EXIT C:\\T\\ACAT.EXE'), { timeoutMs: 30000 }), 'ACAT ran');
  pc.run(300);
  // row 18: TAB expansion is the kernel's (DOS counts columns itself);
  // row 24 from col 6: COMMAND's prompt in the reference run; (11,34-36):
  // the X pointer over the DOSBox-X window in the reference screenshot
  const bad = compareReal(pc, path.join(HERE, 'real/t1-dos400.txt'),
    (r, c) => r === 18 || (r === 24 && c >= 6) || (r === 11 && c >= 34 && c <= 36));
  check(bad.length === 0, `T1.ANS${raw ? ' (raw mode: driver WRITE)' : ' (INT 29h)'} matches MS-DOS 4.00 ANSI.SYS cell by cell` +
    (bad.length ? '\n       ' + bad.slice(0, 12).join('\n       ') : ''));
  await pc.png(path.join(OUT, raw ? 't1raw.png' : 't1.png'));
};
scenarios.push({ name: 't1', run: t1(false) });
scenarios.push({ name: 't1raw', run: t1(true) });

scenarios.push({
  name: 'func',
  async run() {
    const pc = await start('func', {
      config: 'DEVICE=C:\\DOS\\ANSI.SYS\n',
      script: 'C:\\T\\ATEST.EXE\nhalt\n',
    });
    const keys = { 1: 'A', 2: '{F1}q', 3: 'A{F1}q', 4: ' ', 5: ' ' };
    for (let k = 1; k <= 5; k++) {
      if (!check(pc.waitSerial(`T:WAITKEY ${k}`, { timeoutMs: 20000 }), `ATEST reached key ${k}`)) break;
      if (k === 4) {
        check(pc.lines().length === 50, `50-line mode shows ${pc.lines().length} lines`);
        check(pc.hasText('fifty lines'), '"fifty lines" on screen');
        await pc.png(path.join(OUT, 'lines50.png'));
      }
      if (k === 5) {
        check(pc.lines().length === 44, `43-line mode shows ${pc.lines().length} rows (43 + the unused 44th)`);
        await pc.png(path.join(OUT, 'lines43.png'));
      }
      pc.type(keys[k]);
    }
    check(pc.waitSerial('T:RESULT', { timeoutMs: 20000 }), 'ATEST finished');
    const fails = pc.serial.split('\n').filter((l) => l.startsWith('T:FAIL'));
    check(pc.serial.includes('T:RESULT PASS'), 'ATEST checks (DSR, key reassignment, 2Fh, IOCTL)' +
      (fails.length ? '\n       ' + fails.join('\n       ') : ''));
    check(pc.lines().length === 25, 'back to 25 lines');
  },
});

scenarios.push({
  name: 'art',
  async run() {
    const pc = await start('art', {
      config: 'DEVICE=C:\\DOS\\ANSI.SYS\n',
      script: 'C:\\T\\ACAT.EXE C:\\DOS\\ANSI.ART\nhalt\n',
    });
    check(pc.until(() => pc.serial.includes('T:EXIT C:\\T\\ACAT.EXE'), { timeoutMs: 30000 }), 'ACAT ANSI.ART ran');
    pc.run(300);
    check(pc.hasText('ARM-DOS Version 4.00') && pc.hasText('Europa Micro Systems'), 'ANSI.ART texts');
    const [ch, at] = cell(pc, 2, 7);
    check(ch === 0xDB && at === 0x1E, `big letters: full blocks, yellow on blue (${ch.toString(16)} ${at.toString(16)})`);
    check(!pc.hasText('\u2190['), 'no escape sequence shown raw');
    await pc.png(path.join(OUT, 'art.png'));
    console.log(`     screenshot ${path.join(OUT, 'art.png')}`);
  },
});

scenarios.push({
  name: 'prompt',
  async run() {
    // ECHO in the test shell writes through handle 1, like PROMPT $e[...
    const pc = await start('prompt', {
      config: 'DEVICE=C:\\DOS\\ANSI.SYS\n',
      // (no '>' here: the test shell would take it for a redirection)
      script: 'echo \x1b[2J\x1b[1;33;44m C:\\DOS \x1b[0m after\nhalt\n',
    });
    check(pc.until(() => pc.hasText(' C:\\DOS  after'), { timeoutMs: 30000 }), 'coloured prompt text');
    const [ch, at] = cell(pc, 0, 1);
    check(ch === 0x43 && at === 0x1E, `'C' is bright yellow on blue (${at.toString(16)})`);
    const [ch2, at2] = cell(pc, 0, 9);
    check(at2 === 0x07, `text after ESC[0m is grey on black (${ch2.toString(16)} ${at2.toString(16)})`);
  },
});

scenarios.push({
  name: 'badparm',
  async run() {
    const pc = await start('badparm', {
      config: 'DEVICE=C:\\DOS\\ANSI.SYS /X /Q\n',
      script: 'echo \x1b[5;5Hraw\nhalt\n',
    });
    check(pc.waitText('Invalid parameter -  /Q', { timeoutMs: 30000 }), 'Invalid parameter -  /Q');
    check(pc.waitText('Error in CONFIG.SYS line 1', { timeoutMs: 5000 }), 'Error in CONFIG.SYS line 1');
    check(pc.waitText('\u2190[5;5Hraw', { timeoutMs: 20000 }), 'driver not installed: ESC shown raw');
  },
});

for (const sc of scenarios) {
  if (filters.length && !filters.some((f) => sc.name.includes(f))) continue;
  console.log(`== ${sc.name}`);
  try { await sc.run(); } catch (e) { check(false, `${sc.name}: ${e.stack}`); }
}
console.log(failures ? `\n${failures} failure(s)` : '\nall ANSI.SYS tests passed');
process.exit(failures ? 1 : 0);
