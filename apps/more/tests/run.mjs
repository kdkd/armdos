#!/usr/bin/env node
// apps/more/tests/run.mjs - MORE.COM against the real MS-DOS 4.00 MORE.
// expected/: MORE.TXT (MORE < B.TXT > file, from the verification set),
// MP.TXT (a 60-line file through MORE into a file), MW.TXT (tabs, a 100-char
// line, BS, BEL, ^Z in the middle), and screen-NN.txt: the screens of
// "MORE < L60.TXT" paging (real DOS 4.00 in DOSBox-X).
import fs from 'node:fs';
import path from 'node:path';
import { session, Checker, expected, ROOT } from '../../dosutil/tests/harness.mjs';

const t = new Checker('MORE');
const D = 'apps/more/tests/data/';
const s = await session({ name: 'more', files: ['L60', 'W'].map((n) => ({ src: `${D}${n}.TXT`, dst: 'T\\' })) });
const M = '\\DOS\\MORE.COM';
const screenFile = (n) => fs.readFileSync(path.join(ROOT, 'apps/more/tests/expected', `screen-${n}.txt`), 'latin1')
  .split('\n').map((l) => l.replace(/\s+$/, '')).slice(0, 25);
const screen = () => s.pc.screen().split('\n').map((l) => l.replace(/\s+$/, ''));

// a short file into a file: no prompt, CR LF first
s.run(`REDIR \\WORK\\B.TXT \\OUT\\MORE.TXT ${M}`);
t.bytes(s.file('OUT\\MORE.TXT'), expected('more', 'MORE.TXT'), 'MORE < B.TXT > file');

// paging on the screen: 24 lines, "-- More --" on row 25, any key, CR LF CR LF
s.pc.type('CLS\r'); s.waitPrompt();
s.pc.type(`REDIR \\T\\L60.TXT - ${M}\r`);
t.ok(s.pc.waitText('-- More --', { timeoutMs: 10000 }), 'first page stops at -- More --');
s.idle();
t.lines(screen(), screenFile(11), 'page 1 (screen)');
s.pc.type(' ');
s.idle();
t.lines(screen(), screenFile(13), 'page 2 after a key (screen)');
s.pc.type('{F1}');          // an extended key: both bytes swallowed
s.waitPrompt();
const last = screen();
// (compared from the last prompt on: the shells differ in what follows)
const want16 = screenFile(16), wi = want16.indexOf('-- More --'), gi = last.indexOf('-- More --');
t.lines(last.slice(gi - 9, gi + 13), want16.slice(wi - 9, wi + 13), 'last page (screen)');

// output redirected: the prompts still go to the console (stderr)
s.pc.type('CLS\r'); s.waitPrompt();
s.pc.type(`REDIR \\T\\W.TXT \\OUT\\MW.TXT ${M} /X\r`);
t.ok(s.pc.waitText('-- More --', { timeoutMs: 10000 }), 'W.TXT: first prompt with stdout redirected');
s.idle(); s.pc.type('x');
s.waitPrompt();
t.lines(s.lines(), ['T>REDIR \\T\\W.TXT \\OUT\\MW.TXT \\DOS\\MORE.COM /X', '-- More --', '', 'T>'], 'W.TXT: one prompt, CR LF CR LF after the key (screen)');
t.bytes(s.file('OUT\\MW.TXT'), expected('more', 'MW.TXT'), 'MORE < W.TXT > file (tab, wrap, BS, BEL, ^Z)');

// 60 lines into a file: needs two keys
s.pc.type('CLS\r'); s.waitPrompt();
s.pc.type(`REDIR \\T\\L60.TXT \\OUT\\MP.TXT ${M}\r`);
s.pc.waitText('-- More --', { timeoutMs: 10000 }); s.idle(); s.pc.type(' ');
s.idle(); s.pc.type(' ');
s.waitPrompt();
t.bytes(s.file('OUT\\MP.TXT'), expected('more', 'MP.TXT'), 'MORE < L60.TXT > file');

// Ctrl-C at the prompt ends MORE (^C echoed by DOS)
s.pc.type('CLS\r'); s.waitPrompt();
s.pc.type(`REDIR \\T\\L60.TXT - ${M}\r`);
s.pc.waitText('-- More --', { timeoutMs: 10000 }); s.idle();
s.pc.type('{CTRL+C}');
t.ok(s.waitPrompt(10000), 'Ctrl-C at -- More -- returns to the shell');
t.ok(screen().some((l) => l.includes('^C')), 'Ctrl-C echoed as ^C');

// with COMMAND.COM: TYPE | MORE, the key comes from the console
const c = await session({ name: 'more-cmd', command: true, files: [{ src: `${D}L60.TXT`, dst: 'T\\' }] });
if (c) {
  c.pc.type('CLS\r'); c.waitPrompt();
  c.pc.type('TYPE \\T\\L60.TXT | MORE\r');
  t.ok(c.pc.waitText('-- More --', { timeoutMs: 10000 }), 'TYPE L60.TXT | MORE stops at -- More --');
  c.idle();
  const sc = c.pc.screen().split('\n').map((l) => l.replace(/\s+$/, ''));
  t.lines(sc.slice(0, 25), screenFile(11), 'TYPE L60.TXT | MORE: page 1 as with MORE < file');
  c.pc.type(' '); c.idle(); c.pc.type(' ');
  t.ok(c.waitPrompt(10000), 'two keys later: back at the prompt');
  // MORE file (not in 4.00, which ignored its command line; later versions took a file)
  c.pc.type('CLS\r'); c.waitPrompt();
  c.pc.type('MORE \\T\\L60.TXT\r');
  t.ok(c.pc.waitText('-- More --', { timeoutMs: 10000 }), 'MORE L60.TXT stops at -- More --');
  c.idle();
  const sf = c.pc.screen().split('\n').map((l) => l.replace(/\s+$/, ''));
  t.lines(sf.slice(0, 25), screenFile(11), 'MORE L60.TXT: page 1 as with MORE < file');
  c.pc.type(' '); c.idle(); c.pc.type(' ');
  t.ok(c.waitPrompt(10000), 'MORE L60.TXT: two keys later, back at the prompt');
  c.pc.type('CLS\r'); c.waitPrompt();
  c.pc.type('MORE /X \\T\\NOSUCH.TXT\r'); c.waitPrompt();
  t.ok(c.pc.screen().includes('File not found - \\T\\NOSUCH.TXT'), 'MORE with a missing file: File not found - name', c.pc.screen());
}
t.done();
