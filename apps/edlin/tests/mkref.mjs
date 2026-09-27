#!/usr/bin/env node
// apps/edlin/tests/mkref.mjs - record the reference results of tests/scenarios.mjs
// with the GENUINE MS-DOS 4.00 EDLIN.COM, run in DOSBox-X (the verification
// set-up in $ARMDOS_REFS/dos400-verify: its 32 MB hard disk with C:\DOS, booted
// from the original DISK1 with AUTOEXEC.BAT replaced). Needs dosbox-x and mtools.
//
//   node apps/edlin/tests/mkref.mjs [--work DIR] [--seconds N]
//
// Writes apps/edlin/tests/ref/<NAME>.OUT (captured stdout) and ref/<NAME>/*
// (the files left in the scenario directory). Developer tool; not run by
// "make test" (the references are checked in).

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { scenarios as allScen, cmdline as allCmd, probes } from './scenarios.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const argv = process.argv.slice(2);
const opt = (n, d) => { const i = argv.indexOf(n); return i >= 0 ? argv[i + 1] : d; };
const WORK = path.resolve(opt('--work', path.join(HERE, '../../../build/edlin-ref')));
const SECONDS = +opt('--seconds', 90);
// ARMDOS_REFS: a directory with dos400-verify/tools/hd-after-tests.img (a 32 MB hard disk
// image with MS-DOS 4.00 installed in C:\DOS) and msdos400-pcjs/MSDOS400-DISK1.img (the
// original MS-DOS 4.00 disk 1). Not distributed: MS-DOS 4.00 binaries are not open source.
const REFS = process.env.ARMDOS_REFS || '';
const VERIFY = path.join(REFS, 'dos400-verify');
const DISK1 = path.join(REFS, 'msdos400-pcjs/MSDOS400-DISK1.img');
const REF = path.join(HERE, 'ref');
const ONLY = opt('--only', '') ? opt('--only', '').split(',') : null;
const PROBE = argv.includes('--probe');     // run tests/scenarios.mjs "probes", print, save nothing
const pick = (l) => l.filter((s) => !ONLY || ONLY.includes(s.name));
const scenarios = PROBE ? pick(probes) : pick(allScen), cmdline = PROBE ? [] : pick(allCmd);

if (!REFS || !fs.existsSync(path.join(VERIFY, 'tools/hd-after-tests.img')) || !fs.existsSync(DISK1)) {
  console.log('mkref: skipped - set ARMDOS_REFS to the MS-DOS 4.00 reference images (see the comment at the top)');
  process.exit(0);
}
fs.rmSync(WORK, { recursive: true, force: true });
fs.mkdirSync(WORK, { recursive: true });
const m = (cmd, ...a) => execFileSync(cmd, a, { stdio: ['ignore', 'pipe', 'pipe'] }).toString();
const out = path.join(WORK, 'E');
fs.mkdirSync(out, { recursive: true });
const groups = [...new Set([...scenarios, ...cmdline].map((s) => s.group ?? 0))];
for (const g of groups) runGroup(scenarios.filter((s) => (s.group ?? 0) === g), cmdline.filter((s) => (s.group ?? 0) === g), g);

// one DOSBox-X session: a fresh copy of the disks, a batch file running the scenarios
function runGroup(scenarios, cmdline, g) {
  const wd = path.join(WORK, `g${g}`);
  fs.mkdirSync(wd, { recursive: true });
  const hd = path.join(wd, 'hd.img'), fd = path.join(wd, 'fdc.img');
  fs.copyFileSync(path.join(VERIFY, 'tools/hd-after-tests.img'), hd);
  fs.copyFileSync(DISK1, fd);
  const C = `${hd}@@32256`;
  const put = (img, data, dst) => {
    const t = path.join(wd, 'tmp.bin');
    fs.writeFileSync(t, typeof data === 'string' ? Buffer.from(data, 'latin1') : data);
    m('mcopy', '-o', '-i', img, t, dst);
  };
  m('mmd', '-i', C, '::/E');
  let bat = '@ECHO OFF\r\n';
  for (const s of scenarios) {
    m('mmd', '-i', C, `::/E/${s.name}`);
    for (const [n, d] of Object.entries(s.files)) put(C, d, `::/E/${s.name}/${n}`);
    put(C, s.script, `::/E/${s.name}.SCR`);
    bat += `CD \\E\\${s.name}\r\nEDLIN ${s.args} < \\E\\${s.name}.SCR > \\E\\${s.name}.OUT\r\n`;
  }
  put(C, '', '::/E/NOINPUT.SCR');
  for (const s of cmdline) bat += `CD \\E\r\nEDLIN ${s.args} < \\E\\NOINPUT.SCR > \\E\\${s.name}.OUT\r\n`;
  bat += 'CD \\\r\nECHO done> \\E\\DONE.TXT\r\n';
  put(C, bat, '::/E/RUN.BAT');
  put(fd, '@ECHO OFF\r\nPATH C:\\DOS\r\nC:\r\nCALL \\E\\RUN.BAT\r\n', '::/AUTOEXEC.BAT');
  fs.writeFileSync(path.join(wd, 'dosbox.conf'), `[sdl]
output=surface
[dosbox]
machine=svga_s3
memsize=4
quit warning=false
[cpu]
cycles=max
[autoexec]
IMGMOUNT 2 ${hd} -t hdd -fs none -size 512,63,16,65
IMGMOUNT A ${fd} -t floppy
BOOT -L A
`);
  console.log(`group ${g}: running DOSBox-X for ${SECONDS} s ...`);
  try {
    execFileSync('timeout', [String(SECONDS), 'dosbox-x', '-conf', path.join(wd, 'dosbox.conf'), '-nopromptfolder', '-fastlaunch'],
      { stdio: 'ignore', env: { ...process.env, SDL_VIDEODRIVER: 'dummy', SDL_AUDIODRIVER: 'dummy' } });
  } catch { /* timeout ends it */ }
  m('mcopy', '-s', '-n', '-o', '-i', C, '::/E', WORK);
  if (!PROBE && !fs.existsSync(path.join(out, 'DONE.TXT'))) { console.error(`group ${g}: the batch did not finish`); process.exit(1); }
  fs.rmSync(path.join(out, 'DONE.TXT'), { force: true });
}

if (PROBE) {
  for (const s of scenarios) {
    console.log(`=== ${s.name}`);
    const o = path.join(out, `${s.name}.OUT`);
    console.log(fs.existsSync(o) ? JSON.stringify(fs.readFileSync(o, 'latin1')) : '(no output)');
    for (const f of fs.readdirSync(path.join(out, s.name))) console.log(`  ${f}: ${JSON.stringify(fs.readFileSync(path.join(out, s.name, f), 'latin1'))}`);
  }
  process.exit(0);
}
if (ONLY) { console.error('--only: references not saved'); process.exit(0); }
fs.rmSync(REF, { recursive: true, force: true });
fs.mkdirSync(REF, { recursive: true });
for (const s of [...scenarios, ...cmdline]) {
  fs.copyFileSync(path.join(out, `${s.name}.OUT`), path.join(REF, `${s.name}.OUT`));
  if (!s.files) continue;
  fs.mkdirSync(path.join(REF, s.name));
  for (const f of fs.readdirSync(path.join(out, s.name))) fs.copyFileSync(path.join(out, s.name, f), path.join(REF, s.name, f));
}
console.log(`references written to ${REF}`);
