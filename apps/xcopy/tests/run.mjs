#!/usr/bin/env node
// apps/xcopy/tests/run.mjs - XCOPY.EXE against the real MS-DOS 4.00 XCOPY.
//
// Every group below was first run with the genuine XCOPY.EXE in DOSBox-X
// (apps/mslib/tools/dos400run.sh) on a disk prepared the same way: a tree
//   \S\A.TXT (archive bit off)  \S\OLD.TXT (1990-05-05)
//   \S\D1\B.TXT  \S\D1\NEW.TXT (2025-03-03)  \S\D1\D2\C.TXT  \S\E1 (empty)
// tests/expected/ holds its redirected output (X*.TXT, P*.TXT), the
// resulting directory trees (tree_*.txt, from mdir) and its screens
// (screen_*.txt).  The same commands run here in the same order, since
// XCOPY 4.00 changes the disk as it goes (it even removes an empty source
// directory in one case - see README.md).
import fs from 'node:fs';
import path from 'node:path';
import { session, Checker, expected, ROOT } from '../../dosutil/tests/harness.mjs';

const t = new Checker('XCOPY');
const X = '\\DOS\\XCOPY.EXE';
const W = 'apps/dosutil/tests/data/WORK/', D = 'apps/xcopy/tests/data/';
const tree = {
  dirs: ['S', 'S\\D1', 'S\\D1\\D2', 'S\\E1'],
  files: [
    { src: W + 'A.TXT', dst: 'S\\', attr: '-', date: '2026-09-24 11:24' },
    { src: D + 'OLD.TXT', dst: 'S\\', date: '1990-05-05 10:00' },
    { src: W + 'B.TXT', dst: 'S\\D1\\', date: '2026-09-24 11:24' },
    { src: D + 'NEW.TXT', dst: 'S\\D1\\', date: '2025-03-03 10:00' },
    { src: W + 'C.TXT', dst: 'S\\D1\\D2\\', date: '2026-09-24 11:28' },
  ],
};
const F = (s) => ({ steps: [[null, s]] });     // answer a prompt the output hides

// the tree as "mdir -/ -b" lists it: \S and the X* targets
function listing(s) {
  const r = s.reader(), out = [];
  const walk = (ent, p) => {
    for (const e of r.readDir(ent)) {
      if (e.name === '.' || e.name === '..' || (e.attr & 0x08)) continue;
      const q = `${p}/${e.name}`;
      if (!p && !/^(S|X\d+[A-Z]?(\.TXT)?)$/.test(e.name)) continue;
      if (e.attr & 0x10) { out.push(`::${q}/`); walk(e, q); } else out.push(`::${q}`);
    }
  };
  walk(null, '');
  return out.sort();
}
const expTree = (g) => expected('xcopy', `tree_${g}.txt`).toString().split('\n').filter(Boolean).sort();

async function group(name, cmds) {
  const s = await session({ name: `xcopy-${name}`, ...tree });
  for (const [out, args, opts] of cmds) {
    s.run(`${X} ${args} >\\OUT\\${out}`, opts);
    t.bytes(s.file(`OUT\\${out}`), expected('xcopy', out), `XCOPY ${args}`);
  }
  return s;
}

// ------------------------------------------------ group a: output + trees
{
  const s = await group('a', [
    ['X1.TXT', '\\S \\X1\\'],
    ['X2.TXT', '\\S \\X2\\ /S'],
    ['X3.TXT', '\\S \\X3\\ /S /E'],
    ['X4.TXT', '\\S \\X4\\ /A /S'],
    ['X5.TXT', '\\S \\X5\\ /M /S'],
  ]);
  // /M cleared the archive bits of the files it copied (ATTRIB shows none)
  const r = s.reader();
  const attrs = ['S\\A.TXT', 'S\\OLD.TXT', 'S\\D1\\B.TXT', 'S\\D1\\NEW.TXT', 'S\\D1\\D2\\C.TXT'].map((p) => r.lookup(p).attr & 0x20);
  t.ok(attrs.every((a) => a === 0), 'XCOPY /M turns the source archive bits off', JSON.stringify(attrs));
  const src = r.lookup('S\\OLD.TXT'), dst = r.lookup('X1\\OLD.TXT');
  t.ok(dst && src.date === dst.date && src.time === dst.time, 'the copy keeps the date and time');
  t.ok(dst && (dst.attr & 0x20), 'the copy has its archive bit set');
  for (const [out, args, opts] of [
    ['X5B.TXT', '\\S \\X5B\\ /M /S'],
    ['X6.TXT', '\\S \\X6\\ /D:01-01-2000 /S'],
    ['X7.TXT', '\\S\\A.TXT \\X7\\'],
    ['X9.TXT', '\\S\\*.TXT \\X9\\*.BAK'],
    ['X10.TXT', 'S\\D1 X10\\ /S'],
    ['X12.TXT', '\\S\\E1 \\X12\\'],
    ['X13.TXT', '\\S\\E1 \\X13\\ /S /E'],
    ['X14.TXT', '\\S\\D1 \\X14\\SUB\\DEEP\\ /S'],
    ['X15.TXT', '\\S\\NEW.TXT \\X15\\'],
    ['X16.TXT', '\\S\\*.XYZ \\X16\\'],
    ['X17.TXT', '\\S\\D1\\*.* \\X17\\ /S'],
    ['X18.TXT', '\\NOFILE.TXT \\X18\\'],
    ['X19.TXT', '\\S\\D1\\B.TXT \\X19\\B2.TXT /V', F('f')],
    ['X20.TXT', '\\S\\D1 \\X20\\ /S /E /D:3-3-2025'],
    ['X21.TXT', '\\S\\D1\\D2 \\X21\\ /E'],
    ['X22.TXT', '\\S\\?.TXT \\X22\\'],
  ]) {
    s.run(`${X} ${args} >\\OUT\\${out}`, opts);
    t.bytes(s.file(`OUT\\${out}`), expected('xcopy', out), `XCOPY ${args}`);
  }
  t.lines(listing(s), expTree('a'), 'the trees after group a (incl. the source directory E1 XCOPY removed)');
}

// ------------------------------------------ group b: the F/D question etc.
{
  const s = await group('b', [
    ['P1.TXT', '\\S\\A.TXT \\S\\D1\\A.TXT', F('f')],
    ['P2.TXT', '\\S\\D1\\*.* \\X24\\X??.*'],
    ['P3.TXT', '\\S \\X25 /S', F('d')],
    ['P4.TXT', '\\S\\A.TXT \\X26.TXT', F('f')],
    ['P5.TXT', '\\S\\*.TXT \\X27.TXT', F('f')],
    ['P6.TXT', '\\S\\A.TXT \\S\\A.TXT', F('f')],
  ]);
  t.lines(listing(s), expTree('b'), 'the trees after group b');
}

// ---------------------------------------------------- group c: /P /W
{
  const s = await group('c', [
    ['P7.TXT', '\\S \\X28\\ /P /S', F('ynyny')],
    ['P8.TXT', '\\S\\A.TXT \\X29\\ /W', F('x')],
    ['P9.TXT', '\\S\\A.TXT NUL', F('f')],
    ['P10.TXT', '\\S\\A.TXT \\S\\D1\\NEWD', F('d')],
    ['P11.TXT', '\\S\\D1 \\X30\\ /P', F('zyy')],
  ]);
  t.lines(listing(s), expTree('c'), 'the trees after group c');
}

// --------------------------------------------- screens (stderr messages)
function blocks(g) {
  const lines = expected('xcopy', `screen_${g}.txt`).toString().split('\n')
    .map((l) => l.replace(/^\d\d \|/, '').replace(/\s+$/, ''));
  const b = {};
  let cur = null;
  for (const l of lines) {
    const m = l.match(/^\[(\d+)\]$/);
    if (m) { cur = b[m[1]] = []; continue; }
    if (l.startsWith('C:\\>')) cur = null;
    if (cur) cur.push(l);
  }
  for (const k in b) while (b[k].length && !b[k][b[k].length - 1]) b[k].pop();
  return b;
}
const E = blocks('e'), Fb = blocks('f'), G = blocks('g');
const Q = '(F = file, D = directory)?';
const fd = (n) => [`Does ${n} specify a file name`, 'or directory name on the target'];
const zero = '        0 File(s) copied';
const screens = [
  [null, '', ['Invalid number of parameters', zero]],
  [E, '\\NOFILE.TXT \\X18\\', '2'],
  [E, '\\S \\X13\\ /Q', '3'],
  [E, '\\S \\X14\\ /D:13-45-99', '4'],
  [E, 'a b c', '5'],
  [E, '\\S \\X15\\ /S /S', '6'],
  [E, 'Q:\\X \\X16\\', '7'],
  [E, '\\S\\A.TXT \\S', '8'],
  [Fb, '\\S \\S\\D1\\ /S', '9'],
  [Fb, '\\S\\NUL \\X17\\', '10'],
  [Fb, '\\NODIR\\*.* \\X18\\', '12'],
  [Fb, '\\S\\*.* \\S', '13'],
  [Fb, '\\S \\X19\\ /D', '14'],
  [Fb, '\\S \\X20\\ /Z:3', '15'],
  // [16] was typed as \S\A (the transcript's "\\" was an escaped backslash):
  // the real 4.00 then says File not found - A; \S\\A really gives Invalid path
  // (re-run on DOSBox-X + the real 4.00 kernel, 2026-09-26: CHDIR "\S\" fails)
  [Fb, '\\S\\A \\X21\\', '16'],
  [null, '\\S\\\\A \\X21\\', ['Invalid path', '        0 File(s) copied']],
  // DOSBox-X screens read off during the capture (group d)
  [null, '/Q', ['Invalid switch -  /Q', zero]],
  [null, '\\S \\X1\\ /D:abc', ['Invalid parameter - /D:abc', zero]],
  [null, '\\S \\S /S', ['Cannot perform a cyclic copy', zero]],
  [null, '\\ \\X2\\ /S', ['Cannot perform a cyclic copy', zero]],
  [null, '\\S\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEF \\X3\\', ['Path too long', zero]],
  [null, '\\S \\X4\\ /S/Q', ['Invalid switch - /Q', zero]],
  [null, '\\S \\X5\\ /A /M /P /A', ['Invalid switch - /A', zero]],
  [null, '\\S /E \\X6\\ \\X7\\', ['Invalid number of parameters - \\X7\\', zero]],
];
{
  const s = await session({ name: 'xcopy-scr', ...tree });
  for (const [set, args, want] of screens) {
    const w = set ? set[want] : want;
    let got = s.run(`${X}${args ? ' ' + args : ''}`);
    if (args.length > 60) got = got.slice(1);          // the command line wrapped
    t.lines(got, w, `XCOPY ${args} (screen)`);
  }
}
{
  // group g: prompts on the screen
  const s = await session({ name: 'xcopy-scr2', ...tree });
  t.lines(s.run(`${X} \\S\\A.TXT \\S\\D1\\A.TXT`, { steps: [[Q, 'x'], [Q + 'x', 'f']] }),
    [...fd('A.TXT'), Q + 'x', ...fd('A.TXT'), Q + 'f', 'Reading source file(s)...', '\\S\\A.TXT', '        1 File(s) copied'],
    'XCOPY \\S\\A.TXT \\S\\D1\\A.TXT, answers x then F (screen)');
  t.lines(s.run(`${X} \\S\\A.TXT NUL`, { steps: [[Q, 'f']] }), G['19'], 'XCOPY \\S\\A.TXT NUL, F (screen)');
  t.lines(s.run(`${X} \\S\\E1 \\X22\\`), G['20'], 'XCOPY \\S\\E1 \\X22\\ (screen)');
  t.lines(s.run(`${X} \\S \\X23\\ /W`, { steps: [['file(s)', 'q']] }), G['21'], 'XCOPY /W (screen)');
  t.lines(s.run(`${X} \\S\\A.TXT \\S\\A.TXT`, { steps: [[Q, 'f']] }),
    [...fd('A.TXT'), Q + 'f', 'Reading source file(s)...', '\\S\\A.TXT', 'File cannot be copied onto itself', zero],
    'XCOPY \\S\\A.TXT \\S\\A.TXT (screen)');
}

// ------------------------------- big files: the buffer fills, several passes
{
  const data = (n, seed) => { const b = Buffer.alloc(n); let x = seed; for (let i = 0; i < n; i++) { x = (x * 1103515245 + 12345) >>> 0; b[i] = x >>> 24; } return b; };
  const text = {};
  for (let i = 1; i <= 4; i++) text[`L\\F${i}.DAT`] = data(200000, i);
  text['M\\SM.DAT'] = Buffer.from('small one\r\n');
  text['M\\BIG.DAT'] = data(1000000, 9);
  text['M\\SM2.DAT'] = Buffer.from('small two\r\n');
  const s = await session({ name: 'xcopy-big', dirs: ['L', 'M'], text });
  s.run(`${X} \\L \\Y1\\ >\\OUT\\Y1.TXT`);
  t.bytes(s.file('OUT\\Y1.TXT'), expected('xcopy', 'Y1.TXT'), 'XCOPY of 4 x 200000 bytes: two reading passes');
  s.run(`${X} \\M \\Y2\\ >\\OUT\\Y2.TXT`);
  t.bytes(s.file('OUT\\Y2.TXT'), expected('xcopy', 'Y2.TXT'), 'XCOPY of a 1,000,000-byte file between small ones');
  let same = true;
  for (const [k, v] of Object.entries(text)) {
    const dst = k.replace(/^L/, 'Y1').replace(/^M/, 'Y2');
    const got = s.file(dst);
    if (!got || Buffer.compare(got, v)) { same = false; console.log('     differs: ' + dst); }
  }
  t.ok(same, 'every copied byte is right');
}
t.done();
