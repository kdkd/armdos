#!/usr/bin/env node
// kernel/tests/run.mjs - the kernel test suite ("make kernel-test").
//
// Each scenario builds a hard disk image (IO.SYS, ARMDOS.SYS, a CONFIG.SYS,
// the test programs in \T, a TSHELL script), boots it headlessly and checks
// the COM1 log (T:PASS / T:FAIL lines written by the programs), the screen,
// and afterwards the disk image itself (fsck.fat, mtools, our own reader).
//
//   node kernel/tests/run.mjs [scenario-name-substring ...] [--keep] [--verbose] [--serial]
//   (--keep leaves before.img/after.img in build/ktest/img/<name>/; otherwise
//   the images are deleted after checking, as 31 x 2 x 34 MB adds up)

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { boot } from '../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../disk/mkimage.mjs';
import { scenarios } from './scenarios.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const B = (p) => path.join(ROOT, 'build', p);
const OUT = B('ktest/img');
fs.mkdirSync(OUT, { recursive: true });

const args = process.argv.slice(2);
const verbose = args.includes('--verbose');
const keep = args.includes('--keep');       // keep the disk images (34 MB each) for inspection
const filters = args.filter(a => !a.startsWith('--'));

function haveTool(t) { try { execFileSync('which', [t], { stdio: 'ignore' }); return true; } catch { return false; } }
const HAVE_FSCK = haveTool('fsck.fat'), HAVE_MTOOLS = haveTool('mdir');

// ------------------------------------------------------------- images
export function makeImage(sc, dir) {
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
  ];
  const put = (name, content) => {
    const host = path.join(dir, name.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, content);
    files.push({ src: path.relative(ROOT, host), dst: name });
  };
  if (sc.config !== null) put('CONFIG.SYS', (sc.config ?? 'SHELL=\\T\\TSHELL.EXE \\T\\S.TXT\n').replace(/\n/g, '\r\n'));
  if (sc.script) put('T\\S.TXT', sc.script.replace(/\n/g, '\r\n'));
  for (const [n, c] of Object.entries(sc.extraFiles || {})) put(n, c);
  for (const [n, src] of Object.entries(sc.extraCopies || {})) files.push({ src, dst: n });
  files.push({ src: 'build/ktest/*.EXE', dst: 'T\\' });
  files.push({ src: 'build/ktest/*.COM', dst: 'T\\', optional: true });
  files.push({ src: 'build/ktest/*.SYS', dst: 'T\\', optional: true });
  if (fs.existsSync(B('HIMEM.SYS'))) files.push({ src: 'build/HIMEM.SYS', dst: 'T\\HIMEM.SYS' });
  if (sc.withCommand && fs.existsSync(B('COMMAND.COM'))) files.push({ src: 'build/COMMAND.COM' });
  const m = {
    format: sc.format || 'hd', sizeMB: sc.sizeMB || 32, heads: 16, sectorsPerTrack: 63,
    label: sc.label ?? 'KTEST', date: '1988-06-17 12:00:00',
    boot: { src: 'build/bootsect.bin' }, files, dirs: sc.dirs || [],
  };
  const r = buildImage(m, ROOT);
  return r.img;
}

// ---------------------------------------------------------- checking
function partOffset(img) {
  const plausible = img.readUInt16LE(0x0B) === 512 && img[0x15] >= 0xF0;
  return plausible ? 0 : img.readUInt32LE(0x1BE + 8) * 512;
}

export function fsck(imgPath) {
  const out = [];
  const img = fs.readFileSync(imgPath);
  const off = partOffset(img);
  if (HAVE_FSCK) {
    const part = imgPath + '.part';
    fs.writeFileSync(part, img.subarray(off));
    try { execFileSync('fsck.fat', ['-n', part], { stdio: 'pipe' }); }
    catch (e) { out.push('fsck.fat: ' + (e.stdout?.toString() || '') + (e.stderr?.toString() || '')); }
    fs.rmSync(part, { force: true });
  }
  if (HAVE_MTOOLS) {
    try { execFileSync('mdir', ['-/', '-i', `${imgPath}@@${off}`, '::'], { stdio: 'pipe', env: { ...process.env, MTOOLS_SKIP_CHECK: '1' } }); }
    catch (e) { out.push('mdir: ' + (e.stderr?.toString() || e.message)); }
  }
  return out;
}

async function runScenario(sc) {
  const dir = path.join(OUT, sc.name);
  let hd = makeImage(sc, dir);
  if (sc.transformHd) hd = sc.transformHd(hd, (m) => buildImage(m, ROOT).img);
  if (keep) fs.writeFileSync(path.join(dir, 'before.img'), hd);
  let fd = null;
  if (sc.floppy) fd = sc.floppy(dir, (m) => buildImage(m, ROOT).img);
  // the BIOS would boot a diskette in A:, so it goes in after the shell said PAUSE
  let printed = '';
  const pc = await boot({ rom: B('rom.bin'), hd: new Uint8Array(hd), fd: sc.bootFloppy && fd ? new Uint8Array(fd) : null,
                          fdWriteProtected: !!sc.fdWriteProtected, onPrint: (b) => { printed += String.fromCharCode(b); } });
  Object.defineProperty(pc, 'printer', { get: () => printed });
  if (fd && !sc.bootFloppy) {
    if (!pc.waitSerial('T:PAUSE', { timeoutMs: 30000 })) console.log('   (no T:PAUSE for the diskette)');
    pc.machine.insertFloppy(new Uint8Array(fd), !!sc.fdWriteProtected);
    pc.run(50);
    pc.type(' ');
  }
  const log = [];
  const fail = (m) => log.push('FAIL: ' + m);
  const ctx = { pc, fail, dir, log };
  const t0 = performance.now();
  if (sc.run) await sc.run(ctx);
  else if (!pc.waitExit({ timeoutMs: (sc.timeout || 60) * 1000 })) fail('timeout; screen:\n' + pc.screen());
  const ms = performance.now() - t0;

  const serial = pc.serial;
  for (const line of serial.split(/\r?\n/)) {
    if (/^T:FAIL/.test(line)) fail(line);
    if (/^T:EXECFAIL/.test(line) && !sc.allowExecFail) fail(line);
  }
  for (const e of sc.expect || []) {
    if (e instanceof RegExp ? !e.test(serial) : !serial.includes(e)) fail(`expected serial ${e}`);
  }
  for (const e of sc.expectPrinter || []) {
    if (e instanceof RegExp ? !e.test(printed) : !printed.includes(e)) fail(`expected printer output ${e}; got ${JSON.stringify(printed)}`);
  }
  for (const e of sc.rejectPrinter || []) {
    if (e instanceof RegExp ? e.test(printed) : printed.includes(e)) fail(`unexpected printer output ${e}`);
  }
  for (const e of sc.expectScreen || []) {
    const s = pc.screen();
    if (e instanceof RegExp ? !e.test(s) : !s.includes(e)) fail(`expected screen ${e}\n${s}`);
  }
  for (const e of sc.rejectScreen || []) {
    const s = pc.screen();
    if (e instanceof RegExp ? e.test(s) : s.includes(e)) fail(`unexpected screen ${e}\n${s}`);
  }
  if (pc.faults.length && !sc.allowFaults) fail(`CPU faults: ${JSON.stringify(pc.faults.slice(0, 3))}`);
  // the disk afterwards
  const imgPath = path.join(dir, 'after.img');
  fs.writeFileSync(imgPath, pc.machine.ata.img);
  if (!sc.noFsck) for (const p of fsck(imgPath)) fail(p);
  if (pc.machine.fdc.img) {
    const fdPath = path.join(dir, 'after-fd.img');
    fs.writeFileSync(fdPath, pc.machine.fdc.img);
    if (sc.fsckFloppy) for (const p of fsck(fdPath)) fail('floppy ' + p);
  }
  if (sc.after) await sc.after({ ...ctx, img: fs.readFileSync(imgPath), reader: (img) => new FatReader(img) });
  if (!keep) {
    fs.rmSync(imgPath, { force: true });
    fs.rmSync(path.join(dir, 'after-fd.img'), { force: true });
  }
  const ok = !log.some(l => l.startsWith('FAIL'));
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${sc.name.padEnd(28)} ${(ms / 1000).toFixed(1)}s  emu ${(pc.timeMs / 1000).toFixed(1)}s` +
              (sc.report ? '  ' + sc.report(ctx) : ''));
  if (!ok || verbose || args.includes('--serial')) {
    for (const l of log) console.log('   ' + l.replace(/\n/g, '\n   '));
    if (verbose && pc.debug) console.log('   debug log:\n   ' + pc.debug.trim().split(/\r?\n/).slice(-60).join('\n   '));
    if (!ok || args.includes('--serial')) console.log('   serial log:\n   ' + serial.trim().split(/\r?\n/).slice(-40).join('\n   '));
  }
  return ok;
}

// mtools and dosfstools are optional test extras (README.md): the images are then checked by our own reader only
if (!HAVE_FSCK) console.log('skip: dosfstools not installed (brew install dosfstools / apt install dosfstools): no fsck.fat -n on the images');
if (!HAVE_MTOOLS) console.log('skip: mtools not installed (brew install mtools / apt install mtools dosfstools): no mdir on the images');
let pass = 0, failN = 0;
for (const sc of scenarios) {
  if (filters.length && !filters.some(f => sc.name.includes(f))) continue;
  try { if (await runScenario(sc)) pass++; else failN++; }
  catch (e) { failN++; console.log(`FAIL ${sc.name}: ${e.stack}`); }
}
console.log(`\nkernel tests: ${pass} passed, ${failN} failed`);
process.exit(failN ? 1 : 0);
