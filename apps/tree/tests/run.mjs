#!/usr/bin/env node
// apps/tree/tests/run.mjs - TREE.COM against the real MS-DOS 4.00 TREE.
// expected/T*.TXT: the redirected output of the genuine TREE.COM for the
// same commands on the same directory tree (made with MD and ECHO in this
// order on the reference disk, DOSBox-X); screen texts from its screen.
import { session, Checker, expected } from '../../dosutil/tests/harness.mjs';

const t = new Checker('TREE');
// \TT exactly as the reference: directories in MD order, then the files
const dirs = ['TT', 'TT\\A1', 'TT\\A2', 'TT\\LONGDIRN.EXT', 'TT\\A1\\B1', 'TT\\A1\\B2', 'TT\\A2\\C1',
  'TT\\A1\\B1\\D1', 'TT\\A1\\B1\\D1\\VERYLONG.NAM'];
const x = 'x \r\n';
const text = {
  'TT\\F1.TXT': x, 'TT\\LONGFILE.EXT': x, 'TT\\A1\\X.TXT': x, 'TT\\A1\\B1\\Y.TXT': x,
  'TT\\A1\\B1\\YY.TXT': x, 'TT\\A1\\B1\\D1\\Z.TXT': x, 'TT\\LONGDIRN.EXT\\Q.Q': x,
  // the reference disk's \WORK had these too, after the four common files
  'WORK\\X.TXT': x, 'WORK\\NEW.TXT': x, 'WORK\\T1.TXT': x, 'WORK\\T2.TXT': x,
};
const T = '\\DOS\\TREE.COM';

async function run(label, cases) {
  const s = await session({ name: 'tree', dirs, text, label });
  for (const [name, cmd, cd] of cases) {
    if (cd) s.run(`CD ${cd}`);
    s.run(`${T}${cmd ? ' ' + cmd : ''} >\\OUT\\${name}`);
    t.bytes(s.file(`OUT\\${name}`), expected('tree', name), `TREE ${cmd}${cd ? ` (in ${cd})` : ''}`);
    if (cd) s.run('CD \\');
  }
  return s;
}

const s = await run('ARMDOS', [
  ['T1.TXT', '\\TT'],
  ['T2.TXT', '\\TT /F'],
  ['T3.TXT', '\\TT /A'],
  ['T4.TXT', '\\TT /F /A'],
  ['T5.TXT', 'C:\\TT\\'],
  ['T6.TXT', '\\TT\\A2 /F'],
  ['T7.TXT', '\\WORK'],
  ['T8.TXT', '\\WORK /F'],
  ['T9.TXT', '\\NODIR'],
  ['T10.TXT', '', '\\TT'],
  ['T11.TXT', 'A1', '\\TT'],
  ['T12.TXT', 'C:', '\\TT'],
  ['T13.TXT', 'tt\\a1 /f'],
]);
await run('ABCDEFGH', [['T14.TXT', '\\TT\\A2']]);
await run('ABCDEFGHIJK', [['T15.TXT', '\\TT\\A2']]);

// errors (stderr, on the screen), as the real TREE prints them
const screens = [
  ['\\NODIR', 'Invalid path - \\NODIR'],
  ['Q:', 'Invalid drive specification'],
  ['/X', 'Invalid switch -  /X'],
  ['\\TT /F /F', 'Invalid switch - /F'],
  ['\\TT /Q', 'Invalid switch - /Q'],
  ['\\TT \\WORK', 'Too many parameters - \\WORK'],
  ['\\TT /F:1', 'Parameter format not correct - /F:1'],
];
for (const [cmd, want] of screens) t.lines(s.run(`${T} ${cmd} >NUL`), [want], `TREE ${cmd} (screen)`);

// the current directory and drive are restored
s.run('CD \\WORK');
s.run(`${T} \\TT >NUL`);
s.run(`${T} .. >\\OUT\\UP.TXT`);
const up = s.file('OUT\\UP.TXT').toString('latin1');
t.ok(up.includes('C:..\0\r\n') && up.includes('WORK'), 'TREE .. from \\WORK: relative start, directory restored after the previous TREE', up);
t.done();
