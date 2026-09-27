#!/usr/bin/env node
// apps/replace/tests/run.mjs - REPLACE.EXE against the real MS-DOS 4.00 REPLACE.
// The disk has \R1\A.TXT B.TXT C.TXT, \R2\SUB (empty) and \R2\A.TXT, as the
// reference run made them in DOSBox-X (apps/mslib/tools/dos400run.sh);
// expected/*.TXT are the genuine REPLACE.EXE's redirected stdout, the
// screens below were read off its screen.
import { session, Checker, expected } from '../../dosutil/tests/harness.mjs';

const t = new Checker('REPLACE');
const D = 'apps/replace/tests/data/';
const R = '\\DOS\\REPLACE.EXE';

async function fresh(name, { ro = false } = {}) {
  const files = ['YN.TXT', 'NY.TXT', 'K.TXT'].map((n) => ({ src: D + n, dst: 'T\\' }));
  files.push({ src: D + 'E1.TXT', dst: 'R1\\A.TXT' }, { src: D + 'F1.TXT', dst: 'R1\\B.TXT' },
             { src: D + 'G1.TXT', dst: 'R1\\C.TXT' }, { src: D + 'E2.TXT', dst: 'R2\\A.TXT', ...(ro ? { attr: 'R' } : {}) });
  const s = await session({ name, files, dirs: ['R1', 'R2', 'R2\\SUB'] });
  s.run('CD \\T');
  return s;
}
const bytes = (s, n, cmd) => { s.run(cmd); t.bytes(s.file(`OUT\\${n}.TXT`), expected('replace', n + '.TXT'), cmd.replace(/REDIR \S+ \S+ /, '')); };

// replace, /S, /A (one disk, in this order, as the reference run)
let s = await fresh('replace1');
bytes(s, 'R1', `REDIR - \\OUT\\R1.TXT ${R} \\R1\\*.TXT \\R2`);
bytes(s, 'R2', `REDIR - \\OUT\\R2.TXT ${R} \\R1\\*.TXT \\R2 /S`);
bytes(s, 'R3', `REDIR - \\OUT\\R3.TXT ${R} \\R1\\*.TXT \\R2 /A`);
t.ok(['A.TXT', 'B.TXT', 'C.TXT'].every((n) => s.exists('R2\\' + n)), 'files added to \\R2');
t.ok(s.file('R2\\A.TXT').equals(s.file('R1\\A.TXT')), '\\R2\\A.TXT replaced');

// errors (screen)
s = await fresh('replace2');
const errs = [
  ['\\R1\\*.TXT \\R2 /A /S', ['Invalid parameter combination']],
  ['', ['Source path required']],
  ['\\R1\\*.XYZ \\R2', ['', 'No files found - C:\\R1\\*.XYZ']],
  ['\\R1\\*.TXT \\R2 /A /A', ['Invalid switch - /A']],
  ['\\R1\\*.TXT \\R2 /Q', ['Invalid switch - /Q']],
  ['A B C', ['Too many parameters - C']],
  ['\\R1\\A.TXT \\R1', ['File cannot be copied onto itself - C:\\R1\\A.TXT', '', 'No files replaced']],
  ['\\R1\\A.TXT Q:', ['Invalid drive specification - Q:\\', '', 'No files replaced']],
  ['\\R1\\A.TXT \\NODIR', ['Path not found - C:\\NODIR', '', 'No files replaced']],
];
for (const [a, want] of errs) t.lines(s.run(`${R}${a ? ' ' + a : ''}`), want, `REPLACE ${a} (screen)`);

// read-only target, /R, default target, relative target
s = await fresh('replace3', { ro: true });
const ro = [
  ['\\R1\\*.TXT \\R2', ['', 'Replacing C:\\R2\\A.TXT', 'Access denied  - C:\\R2\\A.TXT', '', 'No files replaced']],
  ['\\R1\\*.TXT \\R2 /R', ['', 'Replacing C:\\R2\\A.TXT', '', '1 file(s) replaced']],
];
for (const [a, want] of ro) t.lines(s.run(`${R} ${a}`), want, `REPLACE ${a}, read-only target (screen)`);
{
  const e = s.reader().lookup('R2\\A.TXT');
  t.ok(e && (e.attr & 0x21) === 0x21, 'read-only attribute kept, archive set', e && e.attr.toString(16));
}
s.run('CD \\R2');
t.lines(s.run(`${R} \\R1\\A.TXT`), ['', 'Replacing C:\\R2\\A.TXT', 'Access denied  - C:\\R2\\A.TXT', '', 'No files replaced'],
  'REPLACE into the current directory');
s.run('CD \\');
t.lines(s.run(`${R} \\R1\\A.TXT R2`), ['', 'Replacing C:\\R2\\A.TXT', 'Access denied  - C:\\R2\\A.TXT', '', 'No files replaced'],
  'REPLACE into a relative directory');

// /P and /W with the answers from files, screen and stdout
s = await fresh('replace4');
t.lines(s.run(`REDIR \\T\\YN.TXT - ${R} \\R1\\*.TXT \\R2 /S /P`),
  ['', 'Replace C:\\R2\\A.TXT? (Y/N)', 'Replacing C:\\R2\\A.TXT', '', '1 file(s) replaced'], 'REPLACE /S /P, y');
t.lines(s.run(`REDIR \\T\\NY.TXT - ${R} \\R1\\*.TXT \\R2 /A /P`),
  ['', 'Add C:\\R2\\B.TXT? (Y/N)', 'Add C:\\R2\\C.TXT? (Y/N)', 'Adding C:\\R2\\C.TXT', '', '1 file(s) added'], 'REPLACE /A /P, n y');
t.lines(s.run(`REDIR \\T\\K.TXT - ${R} \\R1\\A.TXT \\R2 /W`),
  ['Press any key to continue . . .', '', 'Replacing C:\\R2\\A.TXT', '', '1 file(s) replaced'], 'REPLACE /W');
s = await fresh('replace5');
bytes(s, 'P1', `REDIR \\T\\YN.TXT \\OUT\\P1.TXT ${R} \\R1\\*.TXT \\R2 /S /P`);
bytes(s, 'P2', `REDIR \\T\\NY.TXT \\OUT\\P2.TXT ${R} \\R1\\*.TXT \\R2 /A /P`);
bytes(s, 'W', `REDIR \\T\\K.TXT \\OUT\\W.TXT ${R} \\R1\\A.TXT \\R2 /W`);
bytes(s, 'NF', `REDIR - \\OUT\\NF.TXT ${R} \\R1\\*.XYZ \\R2`);
bytes(s, 'SAME', `REDIR - \\OUT\\SAME.TXT ${R} \\R1\\A.TXT \\R1`);
bytes(s, 'NODIR', `REDIR - \\OUT\\NODIR.TXT ${R} \\R1\\A.TXT \\NODIR`);

// /U: only a newer source replaces
s = await fresh('replace6');
t.lines(s.run(`${R} \\R1\\*.TXT \\R2 /U`), ['', 'No files replaced'], 'REPLACE /U, same date: nothing');
t.done();
