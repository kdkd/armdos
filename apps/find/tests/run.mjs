#!/usr/bin/env node
// apps/find/tests/run.mjs - FIND.EXE against the real MS-DOS 4.00 FIND.
// expected/*.TXT are the redirected outputs of the genuine FIND.EXE for the
// same commands and files (DOSBox-X, apps/mslib/tools/dos400run.sh); the
// screen texts below (stderr messages) were read off its screen.
import { session, Checker, expected } from '../../dosutil/tests/harness.mjs';

const t = new Checker('FIND');
const D = 'apps/find/tests/data/';
const s = await session({
  name: 'find',
  files: ['L', 'N', 'Z', 'M', 'E', 'Q'].map((n) => ({ src: `${D}${n}.TXT`, dst: 'T\\' })),
});
const F = '\\DOS\\FIND.EXE';

// redirected output, byte for byte
const cases = [
  ['FIND.TXT', `"fox" \\WORK\\A.TXT \\WORK\\B.TXT`],
  ['FINDC.TXT', `/C "Line" \\WORK\\B.TXT`],
  ['FINDNV.TXT', `/N /V "fox" \\WORK\\B.TXT`],
  ['FINDNO.TXT', `"x" \\WORK\\NOFILE.TXT`],
  ['FINDC0.TXT', `/C "fox" <\\WORK\\B.TXT`],
  ['FINDE.TXT', `/n /v "" \\WORK\\B.TXT`],
  ['FINDLC.TXT', `"fox" \\work\\b.txt,\\work\\a.txt`],
  ['FL.TXT', `"fox" \\T\\L.TXT`],
  ['FN.TXT', `/N "fox" \\T\\N.TXT`],
  ['FZ.TXT', `"fox" \\T\\Z.TXT`],
  ['FM.TXT', `/C "fox" \\T\\M.TXT \\T\\M.TXT`],
  ['FS.TXT', `/N "x" <\\T\\N.TXT`],
  ['FCV.TXT', `/c /v "fox" \\T\\Z.TXT`],
  ['FE.TXT', `/C "fox" \\T\\E.TXT`],
  ['FQ.TXT', `"""hi""" \\T\\Q.TXT`],
];
for (const [name, args] of cases) {
  // "<file": through REDIR (the test shell cannot take "<" and ">" together)
  const m = args.match(/^(.*)<(\S+)$/);
  s.run(m ? `REDIR ${m[2]} \\OUT\\${name} ${F} ${m[1].trim()}` : `${F} ${args} >\\OUT\\${name}`);
  t.bytes(s.file(`OUT\\${name}`), expected('find', name), `FIND ${args}`);
}

// messages on the screen (stderr), as the real FIND shows them
const screens = [
  ['', ['FIND: Required parameter missing']],
  ['/V', ['FIND: Required parameter missing']],
  ['fox \\WORK\\B.TXT', ['FIND: Parameter format not correct']],
  ['"fox" "x"', ['FIND: Parameter format not correct']],
  ['/Q "x" \\WORK\\B.TXT', ['FIND: Invalid switch']],
  ['"fox" \\WORK\\B.TXT /C', ['', '---------- \\WORK\\B.TXT: 1']],
  ['"e" \\WORK\\*.TXT', ['File not found - \\WORK\\*.TXT']],
  ['"a /x" \\T\\N.TXT', ['FIND: Invalid switch']],
  ['"fox" \\T\\N.TXT "b"', ['', '---------- \\T\\N.TXT', 'a fox', '     c fox', 'Parameter format not correct']],
  ['"fox" \\T\\N.TXT /V /Q', ['FIND: Invalid switch']],
  ['"fox" "fox" \\T\\N.TXT', ['FIND: Parameter format not correct']],
  ['"fox', ['FIND: Parameter format not correct']],
  ['"fox" \\T\\N.TXT;\\T\\Z.TXT', ['', '---------- \\T\\N.TXT', 'a fox', '     c fox', '', '---------- \\T\\Z.TXT', 'fox1', 'fox2']],
  ['/C/N "fox" \\T\\Z.TXT', ['', '---------- \\T\\Z.TXT: 2']],
];
for (const [args, want] of screens) t.lines(s.run(`${F}${args ? ' ' + args : ''}`), want, `FIND ${args} (screen)`);

// a pipe as COMMAND.COM makes one: stdin from a file
s.run(`REDIR \\WORK\\B.TXT \\OUT\\P.TXT ${F} "Line"`);
t.bytes(s.file('OUT\\P.TXT'), 'Line three\r\nLine four\r\n', 'FIND as a filter (stdin, no heading)');

// with COMMAND.COM: a real pipe
const c = await session({ name: 'find-cmd', command: true });
if (c) {
  t.lines(c.run('TYPE \\WORK\\B.TXT | FIND "Line"'), ['Line three', 'Line four'], 'TYPE B.TXT | FIND "Line" (COMMAND.COM pipe)');
  t.lines(c.run('TYPE \\WORK\\B.TXT | FIND /C /V "Line"'), ['2'], 'TYPE B.TXT | FIND /C /V "Line"');
}
t.done();
