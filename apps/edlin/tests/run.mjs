#!/usr/bin/env node
// apps/edlin/tests/run.mjs - EDLIN.COM on ARM-DOS, headless.
//
//   node apps/edlin/tests/run.mjs [--only NAME,...] [--keep] [--verbose]
//
// 1. "redirected": every session of tests/scenarios.mjs runs as
//        EDLIN <args> < \E\<NAME>.SCR > \E\<NAME>.OUT
//    and the captured output and the files left behind must equal, byte for
//    byte, what the genuine MS-DOS 4.00 EDLIN produced (tests/ref/, recorded
//    by tests/mkref.mjs in DOSBox-X).
// 2. "interactive": sessions typed at the keyboard (template editing, ^C,
//    ^Z/F6, Y/N prompts, Continue?) compared with the screen DOS 4.00 shows
//    (UTILITIES.md 2.14, verified in DOSBox-X, and the same rules).
// 3. command-line errors, including the ones on STDERR.
//
// Uses the kernel's test shell (build/ktest/TSHELL.EXE) as SHELL=.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';
import { scenarios, cmdline } from './scenarios.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');
const B = (p) => path.join(ROOT, 'build', p);
const REF = path.join(HERE, 'ref');
const OUT = B('edlin-test');
const argv = process.argv.slice(2);
const opt = (n, d) => { const i = argv.indexOf(n); return i >= 0 ? argv[i + 1] : d; };
const ONLY = opt('--only', '') ? opt('--only', '').split(',') : null;
const VERBOSE = argv.includes('--verbose');
const SCREEN = argv.includes('--screen');     // debugging: output to the screen, not to .OUT

let failures = 0, passes = 0;
const check = (ok, what, detail) => {
  if (ok) passes++; else failures++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`);
  if ((!ok || VERBOSE) && detail) console.log(detail);
};

fs.mkdirSync(OUT, { recursive: true });

function image(name, extraFiles, shellScript, dirs = []) {
  const dir = path.join(OUT, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  let n = 0;
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/ktest/TSHELL.EXE', dst: 'T\\TSHELL.EXE' },
    { src: 'build/EDLIN.COM', dst: 'DOS\\EDLIN.COM' },
    { src: 'build/edlin-test/EDREDIR.EXE', dst: 'T\\EDREDIR.EXE' },
  ];
  const put = (dst, data, attr) => {
    const host = path.join(dir, `f${n++}`);
    fs.writeFileSync(host, typeof data === 'string' ? Buffer.from(data, 'latin1') : data);
    files.push({ src: path.relative(ROOT, host), dst, ...(attr ? { attr } : {}) });
  };
  for (const [dst, data, attr] of extraFiles) put(dst, data, attr);
  put('CONFIG.SYS', 'FILES=20\r\nSHELL=C:\\T\\TSHELL.EXE C:\\T\\S.TXT\r\n');
  put('T\\S.TXT', shellScript.join('\r\n') + '\r\nhalt\r\n');
  const m = { format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'EDLINTST',
    date: '1988-06-17 12:00:00', boot: { src: 'build/bootsect.bin' }, files, dirs: ['DOS', 'T', ...dirs] };
  const { img } = buildImage(m, ROOT);
  return img;
}

const start = (img) => boot({ rom: B('rom.bin'), hd: img });
const readFile = (pc, p) => {
  const r = new FatReader(pc.machine.ata.img);
  try { return Buffer.from(r.readFile(r.lookup(p))); } catch { return null; }
};
const listDir = (pc, p) => {
  const r = new FatReader(pc.machine.ata.img);
  return r.readDir(r.lookup(p)).filter((e) => e.name !== '.' && e.name !== '..' && !(e.attr & 0x10)).map((e) => e.name).sort();
};
const show = (b) => b == null ? '(missing)' : JSON.stringify(b.toString('latin1'));

function diffText(a, b) {
  const A = a.toString('latin1').split('\n'), Bl = b.toString('latin1').split('\n');
  for (let i = 0; i < Math.max(A.length, Bl.length); i++) {
    if (A[i] !== Bl[i]) return `   first difference at output line ${i + 1}:\n     ARM-DOS: ${JSON.stringify(A[i])}\n     DOS 4.00: ${JSON.stringify(Bl[i])}`;
  }
  return '';
}

// ------------------------------------------------------ 1. redirected
async function redirected() {
  console.log('== redirected sessions vs genuine MS-DOS 4.00 EDLIN (tests/ref)');
  const list = scenarios.filter((s) => !ONLY || ONLY.includes(s.name));
  const cl = cmdline.filter((s) => !ONLY || ONLY.includes(s.name));
  const extra = [], script = [], dirs = ['E'];
  for (const s of list) {
    dirs.push(`E\\${s.name}`);
    for (const [n, d] of Object.entries(s.files)) extra.push([`E\\${s.name}\\${n}`, d]);
    extra.push([`E\\${s.name}.SCR`, s.script]);
    script.push(`cd \\E\\${s.name}`, SCREEN ? `C:\\DOS\\EDLIN.COM ${s.args} <\\E\\${s.name}.SCR`
      : `C:\\T\\EDREDIR.EXE \\E\\${s.name}.SCR \\E\\${s.name}.OUT C:\\DOS\\EDLIN.COM ${s.args}`);
  }
  extra.push(['E\\NOINPUT.SCR', '']);
  for (const s of cl) script.push('cd \\E', `C:\\T\\EDREDIR.EXE \\E\\NOINPUT.SCR \\E\\${s.name}.OUT C:\\DOS\\EDLIN.COM ${s.args}`);
  script.push('cd \\', 'echo T:DONE');
  const pc = await start(image('redir', extra, script, dirs));
  const done = pc.until(() => pc.screen().includes('T:DONE') || pc.serial.includes('T:DONE'), { timeoutMs: 300000, stepMs: 100 });
  check(done, `all ${list.length + cl.length} sessions ran`, pc.screen() + '\n' + pc.serial);
  if (SCREEN) { console.log(pc.screen()); return; }
  if (!done) {
    const c = pc.cpu, hex = (v) => (v >>> 0).toString(16).padStart(8, '0');
    const samples = [];
    for (let i = 0; i < 8; i++) { pc.run(37); samples.push(`pc=${hex(c.r[15])} lr=${hex(c.r[14])} sp=${hex(c.r[13])} mode=${c.mode?.toString(16)} r0=${hex(c.r[0])}`); }
    console.log('   debug port: ' + JSON.stringify(pc.debug));
    console.log('   CPU samples:\n' + samples.map((x) => '     ' + x).join('\n'));
  }
  for (const s of [...list, ...cl]) {
    const got = readFile(pc, `E\\${s.name}.OUT`), want = fs.readFileSync(path.join(REF, `${s.name}.OUT`));
    check(got && got.equals(want), `${s.name}: output`, got ? diffText(got, want) : '   no output file');
    if (!s.files) continue;
    const refDir = path.join(REF, s.name);
    const wantNames = fs.readdirSync(refDir).sort(), gotNames = listDir(pc, `E\\${s.name}`);
    check(JSON.stringify(wantNames) === JSON.stringify(gotNames), `${s.name}: files ${wantNames.join(' ')}`,
      `   ARM-DOS has: ${gotNames.join(' ')}`);
    for (const f of wantNames) {
      const g = readFile(pc, `E\\${s.name}\\${f}`), w = fs.readFileSync(path.join(refDir, f));
      check(g && g.equals(w), `${s.name}: ${f} contents`, `   ARM-DOS: ${show(g)}\n   DOS 4.00: ${show(w)}`);
    }
  }
}

// ----------------------------------------------------- 2. interactive
async function interactive() {
  console.log('== interactive sessions (screen)');
  const text = (n) => Array.from({ length: n }, (_, i) => `text line ${i + 1}\r\n`).join('');
  const pc = await start(image('inter', [['W\\A.TXT', text(60)], ['W\\B.TXT', 'abc\r\n']], ['cd \\W', 'interactive'], ['W']));
  const typeWait = (s, t, ms = 20000) => { pc.type(s); return pc.waitText(t, { timeoutMs: ms }) && pc.waitIdle(); };
  const tail = (n) => pc.lines().map((l) => l.trimEnd()).filter((l, i, a) => a.slice(i).some((x) => x)).slice(-n);
  check(pc.waitText('T>', { timeoutMs: 30000 }), 'test shell prompt');
  pc.waitIdle();

  // UTILITIES.md 2.14, verified against real 4.00
  typeWait('C:\\DOS\\EDLIN.COM NEW.TXT\r', '*');
  typeWait('i\r', '1:*');
  typeWait('first line\r', '2:*');
  typeWait('second line\r', '3:*');
  pc.type('{CTRL+C}'); pc.waitIdle();
  typeWait('l\r', 'second line');
  typeWait('1\r', '1:*first line');
  pc.type('\r'); pc.waitIdle();
  typeWait('e\r', 'T>');
  const want = [
    'C:\\DOS\\EDLIN.COM NEW.TXT', 'New file', '*i', '       1:*first line', '       2:*second line', '       3:*^C', '',
    '*l', '       1: first line', '       2: second line', '*1', '       1:*first line', '       1:*', '*e',
  ];
  const scr = pc.lines().map((l) => l.trimEnd());
  const at = scr.findIndex((l) => l.endsWith('C:\\DOS\\EDLIN.COM NEW.TXT'));
  const got = scr.slice(at, at + want.length).map((l, i) => i === 0 ? l.slice(l.indexOf('C:\\')) : l);
  check(JSON.stringify(got) === JSON.stringify(want), 'the UTILITIES.md 2.14 session, screen as DOS 4.00',
    '   got:\n' + got.map((l) => '     |' + l).join('\n'));
  check(show(readFile(pc, 'W\\NEW.TXT')) === show(Buffer.from('first line\r\nsecond line\r\n\x1A', 'latin1')), 'NEW.TXT written with ^Z at the end',
    '   ' + show(readFile(pc, 'W\\NEW.TXT')));

  // template editing: F3 copies the old line, F1 one character; Esc cancels
  typeWait('C:\\DOS\\EDLIN.COM B.TXT\r', '*');
  typeWait('1\r', '1:*abc');
  pc.type('{F1}X{F3}\r'); pc.waitIdle();
  typeWait('l\r', '1:*aXc');
  typeWait('1\r', '1:*aXc');
  pc.type('zzz{ESC}{F3}\r'); pc.waitIdle();
  typeWait('l\r', '*');
  check(tail(3).join('|') === '*l|       1:*aXc|*', 'F1/F3 template editing of a line; Esc keeps the old line', tail(6).join('\n'));
  // F6 (^Z) ends insert; F3 at the * prompt recalls the last command
  typeWait('2i\r', '2:*');
  typeWait('new\r', '3:*');
  pc.type('{F6}\r'); pc.waitIdle();
  check(tail(2).join('|') === '       3:*^Z|*', 'F6 (^Z) ends insert', tail(4).join('\n'));
  pc.type('{F3}\r'); pc.waitIdle();
  check(tail(3).join('|') === '*2i|       2:*|' || tail(2).join('|') === '*2i|       2:*', 'F3 at the * prompt repeats "2i"', tail(4).join('\n'));
  pc.type('{CTRL+C}'); pc.waitIdle();
  check(tail(3).join('|') === '       2:*^C||*', '^C ends insert: ^C, blank line, *', tail(4).join('\n'));
  pc.type('{CTRL+C}'); pc.waitIdle();
  check(tail(3).join('|') === '*^C||*', '^C at the * prompt: ^C, blank line, *', tail(4).join('\n'));
  // Q with an answer that is neither Y nor N re-asks
  typeWait('q\r', 'Abort edit (Y/N)?');
  pc.type('x'); pc.waitIdle();
  pc.type('n'); pc.waitIdle();
  check(tail(3).join('|') === 'Abort edit (Y/N)? x|Abort edit (Y/N)? n|*', 'Abort edit (Y/N)? re-asks, N returns to *', tail(4).join('\n'));
  typeWait('q\r', 'Abort edit');
  pc.type('y'); pc.waitText('T>'); pc.waitIdle();
  check(show(readFile(pc, 'W\\B.TXT')) === show(Buffer.from('abc\r\n', 'latin1')) && !readFile(pc, 'W\\B.$$$') && !readFile(pc, 'W\\B.BAK'),
    'Q Y leaves the file untouched, removes B.$$$, no .BAK', '   ' + show(readFile(pc, 'W\\B.TXT')) + ' ' + listDir(pc, 'W').join(' '));

  // a listing longer than the screen asks "Continue (Y/N)?"
  typeWait('C:\\DOS\\EDLIN.COM A.TXT\r', '*');
  pc.type('1,60l\r');
  const asked = pc.waitText('Continue (Y/N)?', { timeoutMs: 20000 }); pc.waitIdle();
  const s1 = pc.lines().map((l) => l.trimEnd());
  check(asked && s1[s1.length - 1] === 'Continue (Y/N)?' && s1[s1.length - 2] === '      24: text line 24', 'Continue (Y/N)? after a screenful (24 lines)',
    s1.slice(-3).join('\n'));
  pc.type('y'); pc.waitText('Continue (Y/N)?y'); pc.waitIdle();
  const s2 = tail(3);
  check(s2[0] === '      47: text line 47' && s2[1] === '      48: text line 48' && s2[2] === 'Continue (Y/N)?', 'Y continues for another screenful', s2.join('\n'));
  pc.type('n'); pc.waitText('*'); pc.waitIdle();
  check(tail(2).join('|') === 'Continue (Y/N)?n|*', 'N stops the listing', tail(3).join('\n'));
  // ?S asks O.K.?
  typeWait('?stext line 5\r', 'O.K.?');
  check(tail(2).join('|') === '       5: text line 5|O.K.?', '?S shows the match and asks O.K.?', tail(3).join('\n'));
  pc.type('n'); pc.waitText('50: text'); pc.waitIdle();
  pc.type('y'); pc.waitIdle();
  typeWait('.\r', ':*text line 50');
  pc.type('{ESC}\r'); pc.waitIdle();
  const t5 = tail(6), dot = t5.lastIndexOf('*.');
  check(dot >= 0 && t5[dot + 1] === '      50:*text line 50', 'Y makes the match the current line', t5.join('\n'));
  pc.type('\r'); pc.waitIdle();
  typeWait('q\r', 'Abort');
  pc.type('y'); pc.waitText('T>'); pc.waitIdle();
  if (pc.faults.length) console.log('     faults:', pc.faults);
}

// ---------------------------------------------------- 3. command line
async function commandLine() {
  console.log('== command-line errors (screen)');
  const pc = await start(image('cmdl', [['W\\RO.TXT', 'read only\r\n', 'R']], ['cd \\W', 'interactive'], ['W']));
  check(pc.waitText('T>', { timeoutMs: 30000 }), 'test shell prompt');
  pc.waitIdle();
  const cases = [
    ['', 'File name must be specified'],
    ['X.BAK', 'Cannot edit .BAK file--rename file'],
    ['Q:X.TXT', 'Invalid drive or file name'],
    ['NODIR\\X.TXT', 'Invalid drive or file name'],
    ['A.TXT B.TXT', 'Invalid parameter'],
    ['/X A.TXT', 'Invalid parameter'],
    ['A.TXT /B /B', 'Invalid parameter'],
    ['RO.TXT', 'File is READ-ONLY'],
  ];
  for (const [args, msg] of cases) {
    const cmd = `C:\\DOS\\EDLIN.COM ${args}`.trimEnd();
    pc.type(cmd + '\r'); pc.waitIdle();
    const t = pc.lines().map((l) => l.trimEnd());
    const i = t.lastIndexOf('T>' + cmd);
    check(i >= 0 && t[i + 1] === msg && t[i + 2] === 'T>', `EDLIN ${args || '(no argument)'} -> ${msg}`, t.slice(i, i + 4).join('\n'));
  }
  if (pc.faults.length) console.log('     faults:', pc.faults);
}

// --------------------------------------- 4. files larger than the buffer
async function bigFiles() {
  console.log('== files larger than the 64 KB buffer (W and A)');
  const big = Array.from({ length: 1500 }, (_, i) => `big file line ${String(i + 1).padStart(4, '0')} ${'.'.repeat(40)}\r\n`).join('');
  const cases = [
    ['ROUND', 'e', big + '\x1A'],
    ['DEL', '1,10d\r\ne', big.split('\r\n').slice(10).join('\r\n') + '\x1A'],
    ['WA', '100w\r\na\r\n500w\r\na\r\nw\r\na\r\na\r\n#i\r\nthe end\r\n\x1A\r\ne', big + 'the end\r\n\x1A'],
  ];
  const extra = [], script = [], dirs = ['B'];
  for (const [name, scr] of cases) {
    dirs.push(`B\\${name}`);
    extra.push([`B\\${name}\\BIG.TXT`, big], [`B\\${name}.SCR`, scr + '\r\n']);
    script.push(`cd \\B\\${name}`, `C:\\T\\EDREDIR.EXE \\B\\${name}.SCR \\B\\${name}.OUT C:\\DOS\\EDLIN.COM BIG.TXT`);
  }
  script.push('cd \\', 'echo T:DONE');
  const pc = await start(image('big', extra, script, dirs));
  check(pc.until(() => pc.screen().includes('T:DONE'), { timeoutMs: 300000, stepMs: 100 }), 'sessions ran');
  for (const [name, , want] of cases) {
    const got = readFile(pc, `B\\${name}\\BIG.TXT`);
    check(got && got.equals(Buffer.from(want, 'latin1')), `${name}: BIG.TXT (${big.length} bytes) as expected`,
      `   got ${got?.length} bytes, want ${want.length}; output: ${show(readFile(pc, `B\\${name}.OUT`)).slice(0, 400)}`);
    check(readFile(pc, `B\\${name}\\BIG.BAK`)?.equals(Buffer.from(big, 'latin1')), `${name}: BIG.BAK is the original`);
  }
  const out = readFile(pc, 'B\\ROUND.OUT').toString('latin1');
  check(out === '*e\r\nEnd of input file\r\n', 'no "End of input file" at the start when the file does not fit; E reads the rest',
    show(Buffer.from(out, 'latin1')));
  const wa = readFile(pc, 'B\\WA.OUT').toString('latin1');
  check(wa.includes('End of input file'), 'A reads to the end: "End of input file"', show(Buffer.from(wa, 'latin1')).slice(0, 300));
}

const which = argv.filter((a, i) => !a.startsWith('--') && !(i > 0 && argv[i - 1].startsWith('--')));
if (!which.length || which.includes('redirected')) await redirected();
if (!which.length || which.includes('interactive')) await interactive();
if (!which.length || which.includes('cmdline')) await commandLine();
if (!which.length || which.includes('big')) await bigFiles();
console.log(`\n${passes} passed, ${failures} failed`);
process.exit(failures ? 1 : 0);
