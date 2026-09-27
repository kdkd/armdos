#!/usr/bin/env node
// apps/fc/tests/run.mjs - FC.EXE (Microsoft's MS-DOS 4.0 FC compiled for ARM)
// against the output of the real MS-DOS 4.00 FC.EXE (tests/expected/, made
// by apps/mslib/tools/dos400run.sh with tests/run.bat - the same commands).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { session, Checker, need, B } from '../../mslib/tests/harness.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
need(['build/FC.EXE', 'build/ktest/TSHELL.EXE', 'build/IO.SYS', 'build/ARMDOS.SYS', 'build/rom.bin']);

// run.bat without the IF ERRORLEVEL lines (TSHELL logs the exit codes)
const bat = fs.readFileSync(path.join(HERE, 'run.bat'), 'latin1').split(/\r?\n/)
  .filter((l) => l && !/^(IF|ECHO)/.test(l)).map((l) => l.replace(/ > /, ' >'));
const files = fs.readdirSync(path.join(HERE, 'data')).map((f) => ({ src: `apps/fc/tests/data/${f}`, dst: `T\\${f}` }));

const t = new Checker('fc');
const s = await session({ name: 'fc', outDir: B('apps-test'), programs: ['build/FC.EXE'], files, script: bat });
t.ok(s.finished, 'script ran to the end');
for (const f of fs.readdirSync(path.join(HERE, 'expected')).sort()) {
  if (f === 'EL.TXT') continue;
  t.same(s.read(`OUT\\${f}`), fs.readFileSync(path.join(HERE, 'expected', f)), `FC output ${f}`);
}
// exit codes (the real ones: expected/EL.TXT, written by IF ERRORLEVEL)
const el = Object.fromEntries(fs.readFileSync(path.join(HERE, 'expected/EL.TXT'), 'latin1').trim().split(/\r?\n/).map((l) => l.trim().split(' ')));
const outName = (line) => (line.match(/\\OUT\\(\w+)\.TXT/) || [])[1];
const fcRuns = bat.filter((l) => /^FC\b/.test(l));
fcRuns.forEach((l, i) => {
  const n = outName(l);
  if (n && el[n] !== undefined) t.ok(s.exits[i] && s.exits[i].code === +el[n], `exit code of ${n} = ${el[n]}`, JSON.stringify(s.exits[i]));
});
t.ok(!s.pc.faults.length, 'no CPU faults', JSON.stringify(s.pc.faults.slice(0, 3)));
t.done();
