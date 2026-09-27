#!/usr/bin/env node
// apps/comp/tests/run.mjs - COMP.COM against the real MS-DOS 4.00 COMP.
// expected/C*.TXT: redirected stdout of the genuine COMP.COM (DOSBox-X,
// apps/mslib/tools/dos400run.sh) for the same commands and files, with the
// Y/N answers from N.TXT; the screens below were read off its screen.
import fs from 'node:fs';
import { session, Checker, expected } from '../../dosutil/tests/harness.mjs';

const t = new Checker('COMP');
const D = 'apps/comp/tests/data/';
const files = fs.readdirSync(D).sort().map((n) => ({ src: D + n, dst: 'T\\' }));
// \CA and \CB as the real run made them (MD, COPY in this order)
files.push({ src: D + 'E1.TXT', dst: 'CA\\' }, { src: D + 'F1.TXT', dst: 'CA\\' }, { src: D + 'G1.TXT', dst: 'CA\\' },
           { src: D + 'E2.TXT', dst: 'CB\\E1.TXT' }, { src: D + 'G2.TXT', dst: 'CB\\G1.TXT' });
const s = await session({ name: 'comp', files, dirs: ['CA', 'CB'] });
const C = '\\DOS\\COMP.COM';
s.run('CD \\T');

// stdout (redirected) with "n" answers from N.TXT, byte for byte
const cases = [
  ['C1', 'D1.TXT D2.TXT'], ['C2', 'E1.TXT E2.TXT'], ['C3', 'F1.TXT F2.TXT'], ['C4', 'G1.TXT \\T\\G2.TXT'],
  ['C5', 'Z0.TXT Z1.TXT'], ['C6', '\\T\\E1.TXT C:'], ['C8', 'NOFILE.TXT E2.TXT'], ['C9', 'E1.TXT NOFILE.TXT'],
  ['C10', '\\NODIR\\E1.TXT E2.TXT'], ['C11', 'E1.TXT \\NODIR\\E2.TXT'], ['C12', 'Q:E1.TXT E2.TXT'], ['C13', 'A B C'],
];
for (const [n, a] of cases) {
  s.run(`REDIR N.TXT \\OUT\\${n}.TXT ${C} ${a}`);
  t.bytes(s.file(`OUT\\${n}.TXT`), expected('comp', n + '.TXT'), `COMP ${a} (stdout)`);
}

// whole screens (stdout and stderr), answers from files
const screens = [
  ['REDIR I1.TXT -', '', [
    'Enter primary filename', '', '', 'Enter primary filename', 'Invalid drive specification', '', '',
    'Enter primary filename', 'F1.TXT', '', 'Enter 2nd filename or drive id', '', '', 'Enter 2nd filename or drive id',
    'F2.TXT', '', 'C:F1.TXT and C:F2.TXT', '', 'Files compare OK', 'Compare more files (Y/N) ?', '', '',
    'Compare more files (Y/N) ?n'], 23],
  ['REDIR I2.TXT -', '', ['', '', 'Enter primary filename', 'E1.TXT E2.TXT', '', 'C:E1.TXT and C:E1.TXT', '',
    'EOF mark not found', 'Files compare OK', 'Compare more files (Y/N) ?', '', '', 'Compare more files (Y/N) ?n']],
  ['REDIR I3.TXT -', 'F1.TXT', ['', '', 'Enter 2nd filename or drive id', 'F2.TXT', '', 'C:F1.TXT and C:F2.TXT', '',
    'Files compare OK', 'Compare more files (Y/N) ?', '', '', 'Compare more files (Y/N) ?n']],
  ['REDIR N.TXT -', 'E?.TXT F?.TXT', ['', '', 'C:E1.TXT and C:F1.TXT', '', 'Files are different sizes', '', '',
    'C:E2.TXT and C:F2.TXT', '', 'Files are different sizes', 'Compare more files (Y/N) ?n']],
  ['REDIR N.TXT -', '\\CA \\CB\\', ['', '', 'C:\\CA\\E1.TXT and C:\\CB\\E1.TXT', '', 'EOF mark not found',
    'Files compare OK', '', '', 'C:\\CA\\F1.TXT and C:\\CB\\F1.TXT', '', '', 'File not found - C:\\CB\\F1.TXT', '', '',
    'C:\\CA\\G1.TXT and C:\\CB\\G1.TXT', '', 'Compare error at OFFSET 2', 'File 1 = 63', 'File 2 = 64',
    'EOF mark not found', 'Compare more files (Y/N) ?n']],
  ['', '/X', ['Invalid switch - /X']],
  ['', 'Q:E1.TXT E2.TXT', ['Invalid drive specification']],
  ['', 'A B C', ['Too many parameters - C']],
];
for (const [pre, args, want, tail] of screens) {
  const got = s.run(`${pre ? pre + ' ' : ''}${C}${args ? ' ' + args : ''}`);
  t.lines(tail ? got.slice(-tail) : got, want, `COMP ${args} ${pre} (screen)`);
}

// typed at the keyboard: both names asked for, "y" for another round, "n"
const kb = s.run(C, { steps: [['Enter primary filename', 'E1.TXT\r'], ['Enter 2nd', 'E2.TXT\r'],
  ['(Y/N) ?', 'y'], ['Enter primary filename', 'F1.TXT\r'], ['Enter 2nd', 'F2.TXT\r'], ['compare OK', 'n']] });
t.lines(kb.slice(-14), ['Compare more files (Y/N) ?y', '', '', '', 'Enter primary filename', 'F1.TXT', '',
  'Enter 2nd filename or drive id', 'F2.TXT', '', 'C:F1.TXT and C:F2.TXT', '', 'Files compare OK',
  'Compare more files (Y/N) ?n'], 'COMP from the keyboard, Y then N');
t.done();
