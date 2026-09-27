#!/usr/bin/env node
// command/tests/run.mjs - COMMAND.COM tests: boot build/cmdtest/hd.img headless,
// run every case of cases.txt (each starts with CLS), and compare the screen with
// expected/NAME.txt.  The expected screens were captured from the real MS-DOS 4.00
// (tests/ref/capture.py; branding substituted) or written from COMMAND.md's
// verified transcripts.  Variable fields (dates, times, free space, serial
// numbers, COMMAND.COM's size, pipe file names) are normalised on both sides.
//
//   node command/tests/run.mjs [--only NAME,...] [--show] [--update]
//
// --show prints every screen; --update writes the actual screens as
// expected/NAME.actual.txt (never over the expected files).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../..');
const { boot } = await import(path.join(ROOT, 'emu/testkit.mjs'));
import { parseCases, normalise } from './cases.mjs';

const args = process.argv.slice(2);
const only = args.includes('--only') ? args[args.indexOf('--only') + 1].split(',') : null;
const show = args.includes('--show');
const update = args.includes('--update');

const cases = parseCases(fs.readFileSync(path.join(HERE, 'cases.txt'), 'utf8'));
const hd = new Uint8Array(fs.readFileSync(path.join(ROOT, 'build/cmdtest/hd.img')));
const fd = new Uint8Array(fs.readFileSync(path.join(ROOT, 'build/cmdtest/fd.img')));
const fdn = new Uint8Array(fs.readFileSync(path.join(ROOT, 'build/cmdtest/fdn.img')));

function toKeys(line) {
  let s = line, cr = true;
  if (s.endsWith('{NOCR}')) { s = s.slice(0, -6); cr = false; }
  return s + (cr ? '\r' : '');
}

async function session(from) {
  const opts = { rom: path.join(ROOT, 'build/rom.bin'), hd: hd.slice() };
  if (from === 'fd') opts.fd = fd.slice();
  if (from === 'fdn') opts.fd = fdn.slice();
  const pc = await boot(opts);
  const want = from === 'fd' ? 'A>' : from === 'fdn' ? 'Enter new date' : 'C>';
  if (!pc.waitText(want)) throw new Error('no prompt after boot:\n' + pc.screen());
  pc.waitIdle();
  return pc;
}

// idle can mean "waiting for the disk" too: settle twice
function settle(pc) {
  pc.waitIdle();
  pc.run(300);
  pc.waitIdle();
}

let pass = 0, fail = 0;
const failed = [];
let pc = await session();
for (const c of cases) {
  if (only && !only.includes(c.name)) continue;
  if (c.opts.reboot || c.opts.boot) pc = await session(c.opts.boot);
  if (!c.opts.nocls) {
    pc.type('cls\r');
    settle(pc);
  }
  for (const line of c.lines) {
    // machine actions (ARM-DOS-only cases)
    if (line === '{EJECT}') { pc.machine.ejectFloppy(); continue; }
    if (line === '{INSERT}') { pc.machine.insertFloppy(fd.slice(), false); continue; }
    pc.type(toKeys(line));
    settle(pc);
  }
  if (c.opts.settle) pc.run(Number(c.opts.settle));
  let got = pc.screen();
  if (c.opts.from) {
    // compare from the first line containing the text (e.g. below the BIOS screen)
    const ls = got.split('\n');
    const i = ls.findIndex((l) => l.includes(c.opts.from.replace(/_/g, ' ')));
    got = (i < 0 ? ls : ls.slice(i)).join('\n') + '\n';
  }
  const expFile = path.join(HERE, 'expected', c.name + '.txt');
  if (show) console.log(`=== ${c.name}\n${got}`);
  if (update) fs.writeFileSync(path.join(HERE, 'expected', c.name + '.actual.txt'), got);
  if (!fs.existsSync(expFile)) {
    console.log(`?? ${c.name}: no expected screen`);
    fail++;
    failed.push(c.name);
    continue;
  }
  const exp = fs.readFileSync(expFile, 'utf8');
  const a = normalise(got), e = normalise(exp);
  if (a === e) {
    pass++;
  } else {
    fail++;
    failed.push(c.name);
    const al = a.split('\n'), el = e.split('\n');
    const n = Math.max(al.length, el.length);
    let diff = '';
    for (let i = 0; i < n; i++) {
      const x = el[i] ?? '', y = al[i] ?? '';
      diff += (x === y ? '   ' : '!! ') + `exp |${x}|\n` + (x === y ? '' : `   got |${y}|\n`);
    }
    console.log(`FAIL ${c.name}\n${diff}`);
  }
  if (pc.faults.length) {
    console.log(`faults after ${c.name}: ${JSON.stringify(pc.faults)}`);
    pc.faults.length = 0;
  }
}
console.log(`command tests: ${pass} passed, ${fail} failed${failed.length ? ' (' + failed.join(' ') + ')' : ''}`);
process.exit(fail ? 1 : 0);
