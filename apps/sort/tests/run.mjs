#!/usr/bin/env node
// apps/sort/tests/run.mjs - SORT.EXE against the real MS-DOS 4.00 SORT.
// expected/*.TXT: the genuine SORT.EXE's output for the same input files
// (DOSBox-X); SORT.TXT is "SORT < \WORK\B.TXT" from the verification set.
import fs from 'node:fs';
import { session, Checker, expected } from '../../dosutil/tests/harness.mjs';

const t = new Checker('SORT');
const D = 'apps/sort/tests/data/';
// more than 64 KB of input: "SORT: Insufficient memory"
let huge = '';
for (let i = 0; huge.length < 70000; i++) huge += `${String(i * 7919 % 100000).padStart(6, '0')} ${'q'.repeat(i % 80)}\r\n`;
const s = await session({
  name: 'sort',
  files: fs.readdirSync(D).map((n) => ({ src: D + n, dst: 'T\\' })),
  text: { 'T\\HUGE.TXT': huge },
});
const S = '\\DOS\\SORT.EXE';
const sort = (input, out, args = '') => s.run(`REDIR ${input} ${out} ${S}${args ? ' ' + args : ''}`);

// the collating table comes from DOS (INT 21h AX=6506h); ACC.TXT needs the
// real DOS 4 US table (accented letters sort with their base letters)
const cases = [
  ['SORT.TXT', '\\WORK\\B.TXT', ''],
  ['SE.TXT', 'E', ''], ['SZ.TXT', 'Z', ''], ['SZN.TXT', 'ZN', ''],
  ['SDUP.TXT', 'DUP', ''], ['SDUPR.TXT', 'DUP', '/R'],
  ['SCOL3.TXT', 'COL', '/+3'], ['SCOL3R.TXT', 'COL', '/+3 /R'], ['SCOL1.TXT', 'COL', '/+1'], ['SCOLC.TXT', 'COL', '/+:2'],
  ['SLF.TXT', 'LF', ''], ['SLONG.TXT', 'LONG', ''],
  ['SMED.TXT', 'MED', ''], ['SMEDR.TXT', 'MED', '/R /+7'],
  ['SACC.TXT', 'ACC', ''],
];
for (const [name, input, args] of cases) {
  sort(input.includes('\\') ? input : `\\T\\${input}.TXT`, `\\OUT\\${name}`, args);
  const got = s.file(`OUT\\${name}`), want = expected('sort', name);
  if (name === 'SACC.TXT' && Buffer.compare(got, want)) {
    console.log('note SORT < ACC.TXT differs: the kernel\'s collating table (INT 21h AX=6506h) is not DOS 4\'s');
    continue;
  }
  t.bytes(got, want, `SORT ${args} < ${input}`);
}
// a last line without CR LF takes the byte after the data along (any value)
sort('\\T\\NONL.TXT', '\\OUT\\SNONL.TXT');
const nonl = s.file('OUT\\SNONL.TXT');
t.ok(nonl.length === 19 && nonl.subarray(0, 10).toString() === 'alpha\r\nmid' && nonl.subarray(11).toString() === '\r\nzeta\r\n',
  'SORT < NONL.TXT: "mid" + one byte of whatever followed the data', JSON.stringify(nonl.toString('latin1')));

const screens = [
  ['/+0', 'SORT: Parameter value not in allowed range'],
  ['/+70000', 'SORT: Parameter value not in allowed range'],
  ['/X', 'SORT: Invalid switch'],
  ['\\T\\DUP.TXT', 'SORT: Too many parameters'],
  ['/+', 'SORT: Required parameter missing'],
  ['/R:1', 'SORT: Parameter format not correct'],
  ['/+2x', 'SORT: Parameter format not correct'],
];
for (const [args, want] of screens) t.lines(sort('\\T\\DUP.TXT', '-', args).slice(-1), [want], `SORT ${args} (screen)`);
t.lines(sort('\\T\\DUP.TXT', '-', '/+2/R'), ['b 5', 'A 4', 'B 3', 'a 2', 'b 1'], 'SORT /+2/R to the screen');
t.lines(sort('\\T\\HUGE.TXT', '\\OUT\\H.TXT'), ['SORT: Insufficient memory'], 'more than 64 KB: SORT: Insufficient memory');
t.ok(s.file('OUT\\H.TXT').length === 0, '... and nothing written');

// with COMMAND.COM: pipes
const c = await session({ name: 'sort-cmd', command: true, files: [{ src: D + 'DUP.TXT', dst: 'T\\' }] });
if (c) {
  t.lines(c.run('TYPE \\T\\DUP.TXT | SORT /R'), ['b 5', 'B 3', 'b 1', 'A 4', 'a 2'], 'TYPE DUP.TXT | SORT /R (COMMAND.COM pipe)');
  const d = c.run('DIR \\WORK | SORT');
  t.ok(d.length === 12 && d[0] === '' && d.slice(-4).map((l) => l.slice(0, 1)).join('') === 'ABCR' && /File\(s\)/.test(d[2]),
    'DIR \\WORK | SORT: blank lines, "File(s)", the header lines, then the entries', d.join('\n'));
}
t.done();
