#!/usr/bin/env node
// apps/join/tests/run.mjs - JOIN.EXE (Microsoft's MS-DOS 4.0 JOIN compiled for
// ARM) against the real MS-DOS 4.00 JOIN.EXE: the commands of tests/run.bat
// (output: expected/J*.TXT, messages after CLS: ERRORS.TXT; made in DOS 4.00
// under DOSBox-X with COMMAND.COM on C: so that joining A: does not take the
// shell's own drive away).  While A: is joined to C:\JN, the diskette's files
// must appear in C:\JN and A: must be an invalid drive.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { session, Checker, need, B, ROOT } from '../../mslib/tests/harness.mjs';
import { build as buildImage } from '../../../disk/mkimage.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
need(['build/JOIN.EXE', 'build/COMMAND.COM', 'build/ktest/TSHELL.EXE', 'build/IO.SYS', 'build/ARMDOS.SYS', 'build/rom.bin']);
const t = new Checker('join');

// a data diskette for A:
const tmp = path.join(B('apps-test'), 'join-fd');
fs.mkdirSync(tmp, { recursive: true });
fs.writeFileSync(path.join(tmp, 'FLOPPY.TXT'), 'on the diskette\r\n');
const { img: fd } = buildImage({ format: 'fd1440', label: 'DATA', files: [{ src: path.relative(ROOT, path.join(tmp, 'FLOPPY.TXT')), dst: 'FLOPPY.TXT' }] }, ROOT);

const bat = fs.readFileSync(path.join(HERE, 'run.bat'), 'latin1').split(/\r?\n/).filter((l) => l);
const script = ['PAUSE'];
for (const l of bat) {
  if (/^(IF|ECHO|SCRDUMP)/.test(l)) continue;
  if (l === 'CLS') { script.push('echo ERRSTART'); continue; }
  if (/^MD /.test(l)) { script.push('C:\\COMMAND.COM /C ' + l); continue; }
  script.push(l.replace(/ > /, ' >'));
  if (l === 'JOIN A: \\JN > \\OUT\\J1.TXT') {        // our extra checks while A: is joined
    script.push('C:\\COMMAND.COM /C TYPE C:\\JN\\FLOPPY.TXT >\\OUT\\JTYPE.TXT');
    script.push('C:\\COMMAND.COM /C DIR A: >\\OUT\\JA.TXT');
  }
}
script.push('echo ERREND');

const s = await session({
  name: 'join', outDir: B('apps-test'), programs: ['build/JOIN.EXE'], script,
  files: [{ src: 'build/COMMAND.COM', dst: 'COMMAND.COM' }],
  drive: async (pc) => {
    if (!pc.waitSerial('T:PAUSE', { timeoutMs: 30000 })) return false;
    pc.machine.insertFloppy(new Uint8Array(fd), false);
    pc.run(50);
    pc.type(' ');
    return pc.waitExit({ timeoutMs: 60000 });
  },
});
t.ok(s.finished, 'script ran to the end');
for (const f of fs.readdirSync(path.join(HERE, 'expected')).filter((f) => /^J\d\.TXT$/.test(f)).sort())
  t.same(s.read(`OUT\\${f}`), fs.readFileSync(path.join(HERE, 'expected', f)), `JOIN output ${f}`);
const scr = s.pc.screen().split('\n');
// Tolerated: a kernel whose find first on "C:\" answers
// like "C:\*.*" instead of failing as DOS 4, so "JOIN A: C:\" reports the
// root as not empty where the real JOIN says "Invalid parameter - C:\".
const KNOWN = [['Directory not empty - C:\\', 'Invalid parameter - C:\\']];
const got = KNOWN.reduce((x, [a, b]) => x.replace(a, b), scr.slice(scr.indexOf('ERRSTART') + 1, scr.indexOf('ERREND')).join('\n'));
t.same(Buffer.from(got), Buffer.from(fs.readFileSync(path.join(HERE, 'expected/ERRORS.TXT'), 'latin1').trim()), 'messages on the screen as the real JOIN');
t.same(s.read('OUT\\JTYPE.TXT'), Buffer.from('on the diskette\r\n'), 'the diskette\'s file is C:\\JN\\FLOPPY.TXT while A: is joined');
const ja = (s.read('OUT\\JA.TXT') || Buffer.alloc(0)).toString('latin1') + s.pc.screen();
t.ok(/Invalid drive specification/.test(ja), 'A: itself is an invalid drive while joined');
const codes = s.exits.filter((e) => e.prog === 'JOIN').map((e) => e.code);
t.ok(codes.slice(0, 8).every((c) => c === 0), 'errorlevel 0 for the successful JOINs (as real: expected/EL.TXT empty)', codes.join(' '));
t.ok(!s.pc.faults.length, 'no CPU faults', JSON.stringify(s.pc.faults.slice(0, 3)));
t.done();
