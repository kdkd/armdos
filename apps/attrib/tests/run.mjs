#!/usr/bin/env node
// apps/attrib/tests/run.mjs - ATTRIB.EXE (Microsoft's MS-DOS 4.0 ATTRIB
// compiled for ARM) against the real MS-DOS 4.00 ATTRIB.EXE: the same
// commands as tests/run.bat, whose output (expected/AT*.TXT, ERRORS.SCT) the
// real one wrote under DOSBox-X (apps/mslib/tools/dos400run.sh, the files of
// tests/data copied to C:\AT with the time stamp 09-24-2026 13:26).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { session, Checker, need, B } from '../../mslib/tests/harness.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
need(['build/ATTRIB.EXE', 'build/ktest/TSHELL.EXE', 'build/IO.SYS', 'build/ARMDOS.SYS', 'build/rom.bin']);
const t = new Checker('attrib');

// the directory order of the real disk (mcopy wrote F2.DAT first)
const DATE = '2026-09-24 13:26:00';
const files = ['AT\\F2.DAT', 'AT\\F1.TXT', 'AT\\SUB\\G5.BIN', 'AT\\SUB\\F3.TXT', 'AT\\SUB\\DEEP\\F4.TXT']
  .map((f) => ({ src: `apps/attrib/tests/data/${f.replace(/\\/g, '/')}`, dst: f, attr: 'A', date: DATE }));

const bat = fs.readFileSync(path.join(HERE, 'run.bat'), 'latin1').split(/\r?\n/).filter((l) => l);
const script = [];
for (const l of bat) {
  if (/^(IF|ECHO|SCRDUMP)/.test(l)) continue;
  if (l === 'CLS') { script.push('echo ERRSTART'); continue; }
  script.push(l.replace(/ > /, ' >').replace(/>NUL$/, '>\\OUT\\NUL.TXT'));
}
script.push('echo ERREND');

const s = await session({ name: 'attrib', outDir: B('apps-test'), programs: ['build/ATTRIB.EXE'], files,
  dirs: ['AT', 'AT\\SUB', 'AT\\SUB\\DEEP'], script });
t.ok(s.finished, 'script ran to the end');
for (const f of fs.readdirSync(path.join(HERE, 'expected')).filter((f) => /^AT\d+\.TXT$/.test(f))
  .sort((a, b) => parseInt(a.slice(2)) - parseInt(b.slice(2))))
  t.same(s.read(`OUT\\${f}`), fs.readFileSync(path.join(HERE, 'expected', f)), `ATTRIB output ${f}`);

// messages on the screen (stderr) after "CLS" in run.bat
const scr = s.pc.screen().split('\n');
const a = scr.indexOf('ERRSTART'), b = scr.indexOf('ERREND');
const want = fs.readFileSync(path.join(HERE, 'expected/ERRORS.SCT'), 'utf8').split('\n').filter((l) => l).join('\n');
t.same(Buffer.from(scr.slice(a + 1, b).join('\n')), Buffer.from(want), 'error messages on the screen as the real ATTRIB');

// exit codes: expected/EL.TXT (ATTRIB ok, file not found, bad parameter)
const byCmd = (re) => s.exits.filter((e, i) => re.test(script.filter((l) => /^ATTRIB/.test(l))[i] || ''));
const atRuns = script.filter((l) => /^ATTRIB/.test(l));
const codeOf = (line) => { const i = atRuns.indexOf(line); return i >= 0 && s.exits[i] ? s.exits[i].code : undefined; };
t.ok(codeOf('ATTRIB \\AT\\F1.TXT >\\OUT\\NUL.TXT') === 7, 'errorlevel 7 after a normal run (as the real one: >= 2, < 10 -> 7)', codeOf('ATTRIB \\AT\\F1.TXT >\\OUT\\NUL.TXT'));
t.ok(codeOf('ATTRIB \\NOFILE.TXT >\\OUT\\NUL.TXT') === 1, 'errorlevel 1 for file not found');
t.ok(codeOf('ATTRIB +X \\AT\\F1.TXT >\\OUT\\NUL.TXT') === 1, 'errorlevel 1 for a parse error');
void byCmd;
// the attributes on the disk afterwards (the script ends with -r)
const f1 = s.reader.lookup('AT\\F1.TXT');
t.ok(f1 && (f1.attr & 0x21) === 0x20, 'F1.TXT is archive, not read-only, at the end', f1 && f1.attr);
t.ok(!s.pc.faults.length, 'no CPU faults', JSON.stringify(s.pc.faults.slice(0, 3)));
t.done();
