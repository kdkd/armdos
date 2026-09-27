#!/usr/bin/env node
// apps/defrag/tests/run.mjs - DEFRAG.EXE (ARM Disk Optimizer) on fragmented
// scratch volumes, booted headless:
//   - FAT12 1.44 MB diskette and the FAT16 C: it runs from (32 MB), built
//     fragmented with mtools (fat.mjs/disks.mjs): Full Optimization /AUTO,
//     then every file byte-identical (mcopy), fsck.fat -n clean, zero
//     fragmented files, the free space in one piece, IO.SYS/ARMDOS.SYS and
//     the hidden+system file untouched, "." / ".." right;
//   - a second Full Optimization moves nothing; Unfragment Files Only;
//     directory sorting (/S:N);
//   - the interactive run: recommendation dialog, the animated map mid-run
//     (paced, the emulated time it takes), Esc -> Stop, the Optimize menu by
//     mouse, screenshots in build/defrag-test/*.png;
//   - build/DEFRAG11.ZIP unpacked by ARM-DOS's UNZIP.
// FULL=1 adds a 128 MB FAT16 volume (paced and fast).
// mtools and dosfstools are optional (README.md): without mtools there are no
// fragmented volumes, and only the menus (on a diskette mkimage wrote, which has
// nothing to optimize) and UNZIP are checked; without dosfstools, no fsck.fat.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { floppyImage, start, cmd, exitCode, check, summary, readFile, B, ROOT } from '../../format/tests/lib.mjs';
import { build as buildImage } from '../../../disk/mkimage.mjs';
import { volume, fragInfo, fsckVol, verifyModel, partOff, HAVE_MTOOLS, HAVE_FSCK } from './fat.mjs';
import { fragment } from './disks.mjs';

const OUT = B('defrag-test');
fs.rmSync(OUT, { recursive: true, force: true });
fs.mkdirSync(OUT, { recursive: true });
const HERE = path.dirname(fileURLToPath(import.meta.url));

/** a hard disk: IO.SYS, ARMDOS.SYS, the test shell, DEFRAG and friends in C:\T */
function hdImage(sizeMB, name) {
  const dir = path.join(OUT, 'src-' + name);
  fs.mkdirSync(dir, { recursive: true });
  const put = (dst, content) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, typeof content === 'string' ? content.replace(/\r?\n/g, '\r\n') : content);
    return { src: path.relative(ROOT, host), dst };
  };
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/ktest/TSHELL.EXE', dst: 'T\\TSHELL.EXE' },
    { src: 'build/DEFRAG.EXE', dst: 'T\\DEFRAG.EXE' },
    { src: 'build/MOUSE.COM', dst: 'T\\MOUSE.COM' },
    { src: 'build/UNZIP.EXE', dst: 'T\\UNZIP.EXE' },
    { src: 'build/DEFRAG11.ZIP', dst: 'T\\DEFRAG11.ZIP' },
    put('CONFIG.SYS', 'FILES=20\nSHELL=C:\\T\\TSHELL.EXE C:\\T\\S.TXT\n'),
    put('T\\S.TXT', 'interactive\n'),
  ];
  const m = { format: 'hd', sizeMB, heads: 16, sectorsPerTrack: 63, label: 'DEFRAGHD', date: '1990-11-12 01:10:00',
    boot: { src: 'build/bootsect.bin' }, files, dirs: ['T'] };
  return new Uint8Array(buildImage(m, ROOT).img);
}

const W = (n) => path.join(OUT, n);
const fixedSnapshot = (img) => {
  const v = volume(img);
  return v.files.filter((f) => v.fixed(f)).map((f) => ({ name: f.name, chain: f.chain.join(','), data: v.content(f) }));
};

/** all the checks on a volume after a run */
function verify(label, img, model, before, { full = true, fixedBefore } = {}) {
  const v = volume(img), fi = fragInfo(v);
  fs.writeFileSync(W(label.replace(/[^A-Za-z0-9]+/g, '_') + '.img'), img);
  const fsck = fsckVol(img, OUT, label);
  check(fsck === '', `${label}: fsck.fat -n clean`, fsck);
  const bad = verifyModel(img, OUT, model);
  check(bad.length === 0, `${label}: all ${model.size} files byte-identical after optimizing`, bad.join(' '));
  const names = new Set(v.files.filter((f) => !f.dir).map((f) => f.name.slice(1)));
  const missing = [...model.keys()].filter((n) => !names.has(n));
  check(missing.length === 0 && v.files.length === before.files.length, `${label}: same ${v.files.length} directory entries`, missing.join(' '));
  if (full) {
    check(fi.fragFiles === 0, `${label}: no fragmented files left (was ${before.fi.fragFiles}, ${before.fi.frags} fragments)`,
      v.files.filter((f) => f.frags > 1 && !v.fixed(f)).map((f) => f.name + ':' + f.frags).join(' '));
    check(fi.holes === 0, `${label}: free space in one piece (was ${before.fi.holes} free clusters between files)`, fi.holes);
  }
  const fx = fixedSnapshot(img);
  const same = fixedBefore.length === fx.length && fixedBefore.every((f, i) => f.name === fx[i].name && f.chain === fx[i].chain && f.data.equals(fx[i].data));
  check(same, `${label}: unmovable files untouched (${fixedBefore.map((f) => f.name.slice(1)).join(', ')})`,
    JSON.stringify(fx.map((f) => [f.name, f.chain])));
  const dots = v.files.filter((f) => f.dir).every((f) => f.dot === f.first && f.dotdot === f.parent);
  check(dots, `${label}: every subdirectory's "." and ".." point where they should`);
  return v;
}

/** the menus by mouse and keyboard, on a diskette with nothing to optimize (MOUSE.COM loaded) */
async function menus(pc) {
  const shot = async (n) => { await pc.png(W(n + '.png')); };
  pc.type('C:\\T\\DEFRAG.EXE A:\r');
  check(pc.waitText('No optimization necessary.', { timeoutMs: 60000 }), 'an optimized diskette: "No optimization necessary."', pc.screen());
  pc.type('c');
  pc.waitText('Alt=Optimize menu', { timeoutMs: 5000 });
  const m = pc.machine;
  const moveTo = (cx, cy) => { for (let i = 0; i < 8; i++) { m.mouseMove(-300, -300); pc.run(20); } m.mouseMove(cx * 8 + 4, (cy * 8 + 4) * 2); pc.run(100); };
  const click = () => { m.mouseButtons(1); pc.run(60); m.mouseButtons(0); pc.run(150); };
  moveTo(4, 0); click();
  check(pc.waitText('Begin Optimization', { timeoutMs: 3000 }) && pc.hasText('Optimization Method...'), 'mouse: a click on "Optimize" opens the menu', pc.screen());
  await shot('4-menu');
  moveTo(8, 6); click();
  check(pc.waitText('Each block on the map', { timeoutMs: 3000 }), 'mouse: Map Legend...', pc.screen());
  await shot('5-legend');
  pc.type('\r');
  pc.run(300);
  pc.type('{F10}'); pc.run(300); pc.type('m'); pc.run(300);
  check(pc.hasText('Choose the optimization method:'), 'F10, M: the Optimization Method dialog', pc.screen());
  pc.type('{DOWN}\r'); pc.run(300);
  pc.type('{F10}'); pc.run(300); pc.type('a'); pc.run(300);
  check(pc.hasText('Version 1.1') && pc.hasText('Europa Micro Systems'), 'About Defrag', pc.screen());
  await shot('6-about');
  pc.type('\r'); pc.run(300);
  pc.type('{ALT+X}');
  check(pc.until(() => /T>\s*$/.test(pc.screen().trimEnd()), { timeoutMs: 20000 }), 'Alt+X exits', pc.screen());
}

/** build/DEFRAG11.ZIP unpacked by ARM-DOS's UNZIP into C:\X */
function unzip(pc) {
  cmd(pc, 'C:\\T\\UNZIP.EXE C:\\T\\DEFRAG11.ZIP C:\\X\\');
  const exe = readFile(pc.hd, '\\X\\DEFRAG.EXE'), doc = readFile(pc.hd, '\\X\\DEFRAG.DOC'), diz = readFile(pc.hd, '\\X\\FILE_ID.DIZ');
  check(exe === fs.readFileSync(B('DEFRAG.EXE')).toString('latin1'), 'UNZIP DEFRAG11.ZIP: DEFRAG.EXE identical', pc.screen());
  const docSrc = fs.readFileSync(path.join(HERE, '../dist/DEFRAG.DOC'), 'latin1').replace(/\r?\n/g, '\r\n');
  check(doc === docSrc && /\r\n/.test(doc) && !/[^\r]\n/.test(doc), 'DEFRAG.DOC identical, CR LF line ends');
  check(diz && diz.split('\r\n').filter(Boolean).every((l) => l.length <= 45) && diz.split('\r\n').length <= 11, 'FILE_ID.DIZ: at most 10 lines of 45 characters', diz);
}

const hd = hdImage(32, 'c');
if (!HAVE_FSCK) console.log('skip: dosfstools not installed (brew install dosfstools / apt install dosfstools): no fsck.fat -n checks');
if (!HAVE_MTOOLS) {
  // the fragmented test volumes are made with mtools: without it, what needs none
  console.log('skip: mtools not installed (brew install mtools / apt install mtools dosfstools): no fragmented volumes, only the menus and UNZIP');
  const files = {};
  for (let i = 0; i < 12; i++) files[`F${i}.DAT`] = Buffer.alloc(700 + i * 1500, 0x41 + i);
  const pc = await start(hd, floppyImage('FRAGTEST', files));
  cmd(pc, 'C:\\T\\DEFRAG.EXE /?');
  check(/DEFRAG \[d:\] \[\/F \| \/U\]/.test(pc.screen()), 'DEFRAG /?', pc.screen());
  cmd(pc, 'C:\\T\\MOUSE.COM');
  await menus(pc);
  unzip(pc);
  process.exit(summary() ? 1 : 0);
}
const fdA = fragment(floppyImage('FRAGTEST'), W('fdA'), { seed: 7 });
const cFrag = fragment(hd, W('c'), { seed: 11, scale: 6, count: 60 });
const hdImg = cFrag.img;
const beforeFd = { fi: fragInfo(volume(fdA.img)), files: volume(fdA.img).files };
const beforeHd = { fi: fragInfo(volume(hdImg)), files: volume(hdImg).files };
check(beforeFd.fi.fragFiles >= 5 && fsckVol(fdA.img, OUT, 'pre') === '', `test diskette: ${beforeFd.fi.fragFiles} fragmented files, ${beforeFd.fi.holes} holes, fsck clean`);
check(beforeHd.fi.fragFiles >= 5 && fsckVol(hdImg, OUT, 'prehd') === '', `test C: (FAT16): ${beforeHd.fi.fragFiles} fragmented files, ${beforeHd.fi.holes} holes, fsck clean`);
const fdFixed = fixedSnapshot(fdA.img), hdFixed = fixedSnapshot(hdImg);

const pc = await start(hdImg, fdA.img);

// ---- 1. the diskette: Full Optimization, /AUTO /FAST
let t0 = pc.timeMs;
cmd(pc, 'C:\\T\\DEFRAG.EXE A: /F /AUTO /FAST', { timeoutMs: 600000 });
check(exitCode(pc) === 0, `DEFRAG A: /F /AUTO /FAST exits with 0 (${((pc.timeMs - t0) / 1000).toFixed(1)} s emulated)`, pc.screen());
check(/ARM Disk Optimizer: drive A: Full Optimization complete\.\n[\d,]+ clusters moved, 100% of the drive is not fragmented\./.test(pc.screen()), 'the one-line report', pc.screen());
const fd1 = new Uint8Array(pc.machine.floppy);
verify('A: full', fd1, fdA.model, beforeFd, { fixedBefore: fdFixed });

// again: nothing to do
cmd(pc, 'C:\\T\\DEFRAG.EXE A: /F /AUTO /FAST', { timeoutMs: 600000 });
check(/Full Optimization complete\.\n0 clusters moved, 100% of the drive is not fragmented/.test(pc.screen()), 'a second Full Optimization moves nothing', pc.screen());

// ---- 2. C: itself (FAT16, the drive the program runs from)
t0 = pc.timeMs;
cmd(pc, 'C:\\T\\DEFRAG.EXE C: /F /AUTO /FAST', { timeoutMs: 1200000 });
check(exitCode(pc) === 0 && /drive C: Full Optimization complete/.test(pc.screen()),
  `DEFRAG C: /F /AUTO /FAST (${((pc.timeMs - t0) / 1000).toFixed(1)} s emulated)`, pc.screen());
const hd1 = new Uint8Array(pc.hd);
const vhd = verify('C: full', hd1, cFrag.model, beforeHd, { fixedBefore: hdFixed });
const io = vhd.files.find((f) => f.name === '\\IO.SYS');
check(io && io.first === 2 && io.frags === 1, 'IO.SYS still starts at cluster 2, in one piece');
// the system still works: run a program from a moved directory
cmd(pc, 'C:\\T\\DEFRAG.EXE /?');
check(/DEFRAG \[d:\] \[\/F \| \/U\]/.test(pc.screen()), 'DEFRAG /? (the program still loads from the optimized C:)', pc.screen());

const swap = (img) => { pc.machine.ejectFloppy(); pc.run(50); pc.machine.insertFloppy(img); pc.run(50); };
const shot = async (n) => { await pc.png(W(n + '.png')); };

// ---- 3. Unfragment Files Only + directory sort by name
{
  const d = fragment(floppyImage('FRAGTEST'), W('fdD'), { seed: 9 });
  const before = { fi: fragInfo(volume(d.img)), files: volume(d.img).files };
  const fixedB = fixedSnapshot(d.img);
  swap(d.img);
  cmd(pc, 'C:\\T\\DEFRAG.EXE A: /U /S:N /AUTO /FAST', { timeoutMs: 600000 });
  check(exitCode(pc) === 0 && /Unfragment Files Only complete/.test(pc.screen()), 'DEFRAG A: /U /S:N /AUTO /FAST', pc.screen());
  const img = new Uint8Array(pc.machine.floppy);
  const v = verify('A: unfrag', img, d.model, before, { full: false, fixedBefore: fixedB });
  const fi = fragInfo(v);
  check(fi.fragFiles === 0, `A: unfrag: no fragmented files left (was ${before.fi.fragFiles})`, v.files.filter((f) => f.frags > 1).map((f) => f.name).join(' '));
  let sorted = true;
  for (const dirc of new Set(v.files.map((f) => f.parent))) {
    const ents = v.files.filter((f) => f.parent === dirc && !v.fixed(f)).sort((a, b) => a.index - b.index);
    const key = (f) => (f.dir ? '0' : '1') + f.name.split('\\').pop().padEnd(12);
    for (let i = 1; i < ents.length; i++) if (key(ents[i - 1]) > key(ents[i])) sorted = false;
  }
  check(sorted, 'A: /S:N: every directory sorted (directories first, then by name)');
}

// ---- 4. interactive: recommendation, the paced animated run, the finished dialog
{
  const d = fragment(floppyImage('FRAGTEST'), W('fdB'), { seed: 3 });
  const before = { fi: fragInfo(volume(d.img)), files: volume(d.img).files };
  const fixedB = fixedSnapshot(d.img);
  swap(d.img);
  cmd(pc, 'C:\\T\\MOUSE.COM');
  pc.type('C:\\T\\DEFRAG.EXE A:\r');
  check(pc.waitText('Recommended optimization method:', { timeoutMs: 60000 }) && /\d+% of drive A: is not fragmented\./.test(pc.screen()),
    'interactive: "Reading disk information", then the recommendation', pc.screen());
  await shot('1-recommendation');
  const t1 = pc.timeMs;
  pc.type('\r');
  pc.run(9000);
  const mid = pc.screen();
  await shot('2-map-mid-run');
  check(/Elapsed Time: 00:00:\d\d/.test(mid) && /Cluster [\d,]+/.test(mid) && /Full Optimization/.test(mid) && /\d+%/.test(mid),
    'mid-run: status box (cluster, %, elapsed time, method)', mid);
  const done = pc.waitText('Finished Condensing', { timeoutMs: 300000, stepMs: 100 });
  const secs = (pc.timeMs - t1) / 1000;
  check(done && secs >= 15 && secs <= 60, `paced: the diskette's Full Optimization takes ${secs.toFixed(1)} s (a 1990 floppy pace: 15-60 s)`, pc.screen());
  await shot('3-finished');
  pc.type('\r');
  check(pc.until(() => /T>\s*$/.test(pc.screen().trimEnd()), { timeoutMs: 20000 }), 'Finished dialog: Exit DEFRAG (the default) returns to the shell', pc.screen());
  verify('A: interactive', new Uint8Array(pc.machine.floppy), d.model, before, { fixedBefore: fixedB });

  await menus(pc);
}

// ---- 5. Esc stops a run cleanly
{
  const d = fragment(floppyImage('FRAGTEST'), W('fdC'), { seed: 5 });
  const before = { fi: fragInfo(volume(d.img)), files: volume(d.img).files };
  const fixedB = fixedSnapshot(d.img);
  swap(d.img);
  pc.type('C:\\T\\DEFRAG.EXE A: /F\r');
  pc.waitText('Optimizing...', { timeoutMs: 60000 });
  pc.run(6000);
  pc.type('{ESC}');
  check(pc.waitText('Do you want to stop DEFRAG?', { timeoutMs: 10000 }), 'Esc during the run: "Do you want to stop DEFRAG?"', pc.screen());
  await shot('7-stop');
  pc.type('s');
  check(pc.waitText('Optimization Stopped', { timeoutMs: 20000 }), 'Stop: the "Optimization Stopped" dialog', pc.screen());
  pc.type('\r');
  pc.until(() => /T>\s*$/.test(pc.screen().trimEnd()), { timeoutMs: 20000 });
  verify('A: stopped', new Uint8Array(pc.machine.floppy), d.model, before, { full: false, fixedBefore: fixedB });
}

// ---- 6. the BBS archive unpacks with ARM-DOS's UNZIP
unzip(pc);

// ---- 7. (FULL=1) a 128 MB FAT16 drive: paced and fast
if (process.env.FULL) {
  const big = fragment(hdImage(128, 'big'), W('big'), { seed: 21, scale: 60, count: 90 });
  const before = { fi: fragInfo(volume(big.img)), files: volume(big.img).files };
  const fixedB = fixedSnapshot(big.img);
  const pc2 = await start(big.img);
  const t = pc2.timeMs;
  cmd(pc2, 'C:\\T\\DEFRAG.EXE C: /F /AUTO', { timeoutMs: 1800000 });
  console.log(pc2.screen().split('\n')[0]);
  check(exitCode(pc2) === 0, `128 MB C: paced Full Optimization: ${((pc2.timeMs - t) / 1000).toFixed(0)} s emulated (${before.fi.fragFiles} fragmented files)`, pc2.screen());
  verify('C: 128 MB', new Uint8Array(pc2.hd), big.model, before, { fixedBefore: fixedB });
}

process.exit(summary() ? 1 : 0);
