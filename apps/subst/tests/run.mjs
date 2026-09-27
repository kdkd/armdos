#!/usr/bin/env node
// apps/subst/tests/run.mjs - SUBST.EXE (Microsoft's MS-DOS 4.0 SUBST compiled
// for ARM) against the real MS-DOS 4.00 SUBST.EXE: the commands of
// tests/run.bat (output: expected/S*.TXT, messages after CLS: ERRORS.TXT,
// made in DOS 4.00 under DOSBox-X with apps/mslib/tools/dos400run.sh and the
// files of tests/data).  The safety case: "DEL E:*.TXT" (COMMAND.COM) with
// E: substituted for C:\SUB deletes C:\SUB\*.TXT and nothing in C:\.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { session, Checker, need, B } from '../../mslib/tests/harness.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
need(['build/SUBST.EXE', 'build/COMMAND.COM', 'build/ktest/TSHELL.EXE', 'build/IO.SYS', 'build/ARMDOS.SYS', 'build/rom.bin']);
const t = new Checker('subst');

const files = ['A.TXT', 'B.TXT', 'SUB\\A.TXT', 'SUB\\B.TXT', 'SUB\\C.DAT']
  .map((f) => ({ src: `apps/subst/tests/data/${f.replace(/\\/g, '/')}`, dst: f }));
files.push({ src: 'build/COMMAND.COM', dst: 'COMMAND.COM' });
const bat = fs.readFileSync(path.join(HERE, 'run.bat'), 'latin1').split(/\r?\n/).filter((l) => l);
const script = [];
for (const l of bat) {
  if (/^(IF|ECHO|SCRDUMP)/.test(l)) continue;
  if (l === 'CLS') { script.push('echo ERRSTART'); continue; }
  if (/^DEL /.test(l)) { script.push('C:\\COMMAND.COM /C ' + l); continue; }
  script.push(l.replace(/ > /, ' >'));
}
script.push('echo ERREND');

const s = await session({ name: 'subst', outDir: B('apps-test'), programs: ['build/SUBST.EXE'], files, dirs: ['SUB'], script });
t.ok(s.finished, 'script ran to the end');
for (const f of fs.readdirSync(path.join(HERE, 'expected')).filter((f) => /^S\d\.TXT$/.test(f)).sort())
  t.same(s.read(`OUT\\${f}`), fs.readFileSync(path.join(HERE, 'expected', f)), `SUBST output ${f}`);
const scr = s.pc.screen().split('\n');
const got = scr.slice(scr.indexOf('ERRSTART') + 1, scr.indexOf('ERREND')).join('\n');
t.same(Buffer.from(got), Buffer.from(fs.readFileSync(path.join(HERE, 'expected/ERRORS.TXT'), 'latin1').trim()), 'messages on the screen as the real SUBST');

// the safety case: only the substituted directory lost its .TXT files
const names = (dir) => (s.reader.readDir(dir ? s.reader.lookup(dir) : null) || []).map((e) => e.name).filter((n) => n !== '.' && n !== '..').sort();
let sub = [], root = [];
try { sub = names('SUB'); root = names(''); } catch (e) { t.ok(false, 'read the disk', e.stack); }
t.ok(JSON.stringify(sub) === JSON.stringify(['C.DAT']), 'DEL E:*.TXT through SUBST E: C:\\SUB deleted C:\\SUB\\*.TXT only (real: C.DAT left)', sub.join(' '));
t.ok(root.includes('A.TXT') && root.includes('B.TXT'), 'C:\\A.TXT and C:\\B.TXT untouched', root.join(' '));

// errorlevels (expected/EL.TXT: only the \NODIR one is 1)
const runs = script.filter((l) => /^SUBST/.test(l));
const codeOf = (line) => { const i = runs.indexOf(line); return s.exits.filter((e) => e.prog === 'SUBST')[i]?.code; };
t.ok(codeOf('SUBST E: \\SUB') === 0 && codeOf('SUBST E: \\NODIR') === 1, 'errorlevel 0 on success, 1 on error (as real)',
  `${codeOf('SUBST E: \\SUB')} ${codeOf('SUBST E: \\NODIR')}`);
t.ok(!s.pc.faults.length, 'no CPU faults', JSON.stringify(s.pc.faults.slice(0, 3)));
t.done();
