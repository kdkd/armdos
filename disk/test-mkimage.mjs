#!/usr/bin/env node
// test-mkimage.mjs - tests for disk/mkimage.mjs. Every image is checked three
// ways: mkimage's own `check`, mtools (mdir/mcopy round trip) and
// `fsck.fat -n` (dosfstools); the external tools are skipped if absent.
// Then deliberate defects are applied to prove the checks fire.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { build, FatReader } from './mkimage.mjs';

const HERE = path.dirname(new URL(import.meta.url).pathname);
const TOOL = path.join(HERE, 'mkimage.mjs');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'armdos-disk-test-'));
let passes = 0, failures = 0;
const ok = (c, m) => { if (c) { passes++; console.log(`  ok   ${m}`); } else { failures++; console.log(`  FAIL ${m}`); } };
const have = (cmd) => spawnSync('which', [cmd]).status === 0;
const sh = (cmd, args) => spawnSync(cmd, args, { encoding: 'utf8' });
const HAVE_MTOOLS = have('mdir'), HAVE_FSCK = have('fsck.fat');
if (!HAVE_MTOOLS) console.log('skip: mtools not installed (brew install mtools / apt install mtools dosfstools): no mdir/mcopy/mlabel checks');
if (!HAVE_FSCK) console.log('skip: dosfstools not installed (brew install dosfstools / apt install dosfstools): no fsck.fat -n checks');

// a source tree with subdirectories, odd sizes and a cluster-exact file
const tree = path.join(tmp, 'tree');
const files = {
  'README.TXT': Buffer.from('ARM-DOS test disk\r\n'),
  'EMPTY.DAT': Buffer.alloc(0),
  'DOS/BIG.BIN': crypto.randomBytes(70001),
  'DOS/EXACT.BIN': crypto.randomBytes(4096),
  'DOS/SUB/DEEP.TXT': Buffer.from('deep\r\n'),
  'games/tetris.com': crypto.randomBytes(1234),
};
for (const [p, d] of Object.entries(files)) { fs.mkdirSync(path.dirname(path.join(tree, p)), { recursive: true }); fs.writeFileSync(path.join(tree, p), d); }
fs.writeFileSync(path.join(tmp, 'IO.SYS'), crypto.randomBytes(12345));
fs.writeFileSync(path.join(tmp, 'ARMDOS.SYS'), crypto.randomBytes(40000));

function manifest(extra) {
  return { tree: 'tree', label: 'TESTDISK', attrs: { 'README.TXT': 'R' }, dirs: ['EMPTYDIR'],
           files: [{ src: 'ARMDOS.SYS', attr: 'HSR', first: 2 }, { src: 'IO.SYS', attr: 'HSR', first: 1 }], ...extra };
}

function partOffset(img) {
  try { const r = new FatReader(img); return r.off; } catch { return 0; }
}

function verify(name, img, { expectType, bootCode } = {}) {
  const file = path.join(tmp, name + '.img');
  fs.writeFileSync(file, img);
  const r = new FatReader(img);
  ok(!expectType || r.L.type === expectType, `${name}: FAT${r.L.type}, ${r.L.clusters} clusters, ${r.L.spc * 512}-byte clusters`);
  const c = sh('node', [TOOL, 'check', file]);
  ok(c.status === 0, `${name}: mkimage check: ${c.stdout.trim().split('\n').pop()}`);
  // boot sector: ARM branch to 0x40, BPB fields, 55AA
  const bs = img.subarray(r.off, r.off + 512);
  ok(bs.readUInt32LE(0) === 0xEA00000E && bs[0x26] === 0x29 && bs[0x1FE] === 0x55 && bs[0x1FF] === 0xAA &&
     bs.toString('latin1', 0x36, 0x3E) === `FAT${r.L.type}   `, `${name}: boot sector "b 0x40", extended BPB, 55AA`);
  if (bootCode) ok(bs.subarray(0x40, 0x40 + bootCode.length).equals(bootCode), `${name}: boot code at 0x40`);
  // system files first and contiguous from cluster 2
  const root = r.readDir(null);
  ok(root[0].name === 'IO.SYS' && root[1].name === 'ARMDOS.SYS' && root[0].cluster === 2 &&
     r.chain(2).every((k, i) => k === 2 + i) && (root[0].attr & 7) === 7, `${name}: IO.SYS, ARMDOS.SYS first; IO.SYS contiguous from cluster 2, HSR`);
  ok(root[2].attr === 0x08 && root[2].name83 === 'TESTDISK   ', `${name}: volume label entry`);
  // contents round trip through our reader
  let same = true;
  for (const [p, d] of Object.entries(files)) {
    const e = r.lookup(p.toUpperCase());
    if (!e || !r.readFile(e).equals(d)) { same = false; console.log(`     ${p} differs`); }
  }
  ok(same, `${name}: every file reads back identical (mkimage reader)`);
  const off = r.off;
  if (HAVE_MTOOLS) {
    const target = `${file}@@${off}`;
    const d = sh('mdir', ['-/', '-a', '-i', target, '::']);
    ok(d.status === 0 && /DEEP\s+TXT/.test(d.stdout) && /EXACT\s+BIN\s+4096/.test(d.stdout) && /IO\s+SYS\s+12345/.test(d.stdout),
       `${name}: mdir -/ -a lists the tree (incl. hidden IO.SYS)`);
    let mt = true;
    for (const [p, dt] of Object.entries(files)) {
      const out = path.join(tmp, 'mcopy.out');
      const cp = sh('mcopy', ['-o', '-n', '-i', target, `::${p.toUpperCase()}`, out]);
      if (cp.status !== 0 || !fs.readFileSync(out).equals(dt)) { mt = false; console.log(`     mcopy ${p}: ${cp.stderr}`); }
    }
    ok(mt, `${name}: mcopy reads every file back identical`);
    const lab = sh('mlabel', ['-s', '-i', target, '::']);
    ok(/TESTDISK/.test(lab.stdout), `${name}: mlabel sees the label (${lab.stdout.trim()})`);
  }
  if (HAVE_FSCK) {
    let f = file;
    if (off) { f = path.join(tmp, name + '.part'); fs.writeFileSync(f, img.subarray(off)); }
    const k = sh('fsck.fat', ['-n', '-v', f]);
    const complaints = k.stdout.split('\n').filter(l => !/^Checking /.test(l)).join('\n');
    ok(k.status === 0 && !/[Ww]arning|differ|[Ll]ost|[Bb]ad |[Ff]ree cluster summary|[Rr]eclaim|[Dd]eleting|[Tt]runcat/.test(complaints),
       `${name}: fsck.fat -n clean (exit ${k.status})`);
    if (k.status !== 0) console.log(k.stdout + k.stderr);
  }
  return { file, r };
}

// ------------------------------------------------------------------ floppies
for (const [fmt, total] of [['fd360', 720], ['fd720', 1440], ['fd1200', 2400], ['fd1440', 2880], ['fd2880', 5760]]) {
  console.log(fmt);
  const { img, L } = build(manifest({ format: fmt }), tmp);
  ok(img.length === total * 512 && L.type === 12, `${fmt}: ${img.length} bytes`);
  verify(fmt, img, { expectType: 12 });
}

// ---------------------------------------------------------------- hard disks
console.log('hd (default ~32 MB)');
{
  const { img, L, geometry, type } = build(manifest({ format: 'hd' }), tmp);
  ok(geometry.heads === 16 && geometry.spt === 63 && img.length === geometry.cyls * 16 * 63 * 512, `CHS ${geometry.cyls}/16/63 = ${img.length} bytes`);
  ok(type === 0x06 && L.type === 16, `partition type 06h (FAT16, >= 65536 sectors)`);
  ok(img[0x1BE] === 0x80 && img.readUInt32LE(0x1BE + 8) === 63, 'partition active, starts at LBA 63 (C/H/S 0/1/1)');
  ok(img.readUInt32LE(0x1BE + 8 + 4) + 63 === img.length / 512, 'partition runs to the end of the disk');
  const { r } = verify('hd32', img, { expectType: 16 });
  ok(r.bs.readUInt32LE(0x1C) === 63 && r.bs[0x24] === 0x80 && r.bs.readUInt16LE(0x13) === 0 && r.bs.readUInt32LE(0x20) === r.L.total,
     'hidden sectors 63, drive 80h, 32-bit total sector count');
  if (have('sfdisk')) {
    const f = path.join(tmp, 'hd32.img');
    const s = sh('sfdisk', ['-d', f]);
    ok(/start=\s*63, size=\s*\d+, type=6, bootable/.test(s.stdout), `sfdisk agrees: ${s.stdout.split('\n').find(l => l.includes('start='))?.trim()}`);
  }
}
console.log('hd 20 MB (FAT16, type 04h) and 10 MB (FAT12, type 01h), 100 MB (8 KB... clusters)');
for (const [mb, t, ft] of [[20, 0x04, 16], [10, 0x01, 12], [100, 0x06, 16]]) {
  const { img, type, L } = build(manifest({ format: 'hd', sizeMB: mb }), tmp);
  ok(type === t && L.type === ft, `${mb} MB: type ${type.toString(16)}h FAT${L.type}, ${L.spc * 512}-byte clusters`);
  verify(`hd${mb}`, img, { expectType: ft });
}

// ---------------------------------------------------------------- boot code
console.log('boot code options');
{
  const code = Buffer.from('e1a00000e1a00000eafffffe', 'hex');         // nop; nop; b .
  fs.writeFileSync(path.join(tmp, 'code.bin'), code);
  let { img } = build(manifest({ format: 'fd1440', boot: 'code.bin' }), tmp);
  verify('bootblob', img, { bootCode: code });
  // a full 512-byte sector: everything but the BPB (0x0B-0x3D) and 55AA is kept
  const sec = Buffer.alloc(512, 0x11);
  sec.writeUInt32LE(0xEA00000E, 0);
  sec.write('KERNBOOT', 3, 'latin1');
  fs.writeFileSync(path.join(tmp, 'sector.bin'), sec);
  ({ img } = build(manifest({ format: 'fd1440', boot: 'sector.bin' }), tmp));
  ok(img.subarray(0x3E, 0x1FE).equals(sec.subarray(0x3E, 0x1FE)) && img.toString('latin1', 3, 11) === 'KERNBOOT' &&
     img.readUInt16LE(0x0B) === 512 && img[0x1FE] === 0x55, '512-byte boot sector file: code and OEM kept, BPB filled in');
  const def = build(manifest({ format: 'fd1440' }), tmp).img;
  ok(def.subarray(0x40, 0x200).includes(Buffer.from('Non-System disk or disk error')), 'default boot code prints "Non-System disk or disk error"');
  const hd = build(manifest({ format: 'hd' }), tmp).img;
  ok(hd.subarray(0, 440).includes(Buffer.from('Missing operating system')) && hd.readUInt32LE(0) !== 0, 'default MBR code present');
  const st = sh('node', [TOOL, 'selftest']);
  if (have('arm-none-eabi-gcc')) ok(st.status === 0, `selftest: embedded boot code == assembled sources\n${st.stdout.trimEnd()}`);
}

// ------------------------------------------------------------ errors --------
console.log('errors');
const expectErr = (m, re, what) => {
  try { build(m, tmp); ok(false, `${what}: no error`); }
  catch (e) { ok(re.test(e.message), `${what}: ${e.message}`); }
};
fs.writeFileSync(path.join(tmp, 'longfilename.text'), 'x');
expectErr(manifest({ files: [{ src: 'longfilename.text' }] }), /not a valid 8\.3/, 'long name rejected');
fs.writeFileSync(path.join(tmp, 'tiny.txt'), 'x');
expectErr({ format: 'fd360', files: Array.from({ length: 120 }, (_, i) => ({ src: 'tiny.txt', dst: `F${i}.X` })) }, /root directory holds 112/, 'root directory overflow');
fs.writeFileSync(path.join(tmp, 'huge.bin'), Buffer.alloc(800 * 1024));
expectErr({ format: 'fd720', files: [{ src: 'huge.bin' }] }, /disk full/, 'disk full');
expectErr({ format: 'fd1440', files: [{ src: 'missing.bin' }] }, /not found/, 'missing file (not optional)');

// ------------------------------------------------ the checks fire ----------
console.log('the checks fire');
{
  const { img } = build(manifest({ format: 'fd1440' }), tmp);
  const r = new FatReader(img);
  const e = r.lookup('DOS/BIG.BIN');
  // cross-link: point BIG.BIN's second cluster at IO.SYS's first
  const bad = Buffer.from(img);
  const fatOff = 512, c = e.cluster;
  for (const fo of [fatOff, fatOff + r.L.fatSecs * 512]) {       // both FAT copies
    const o = fo + ((c * 3) >> 1), v = bad.readUInt16LE(o);
    bad.writeUInt16LE(c & 1 ? (v & 0x000F) | (2 << 4) : (v & 0xF000) | 2, o);
  }
  const f = path.join(tmp, 'bad.img');
  fs.writeFileSync(f, bad);
  const k = sh('node', [TOOL, 'check', f]);
  ok(k.status !== 0 && /share cluster/.test(k.stdout), `a cross-linked FAT is reported: ${k.stdout.trim().split('\n')[0].trim()}`);
  if (HAVE_FSCK) ok(sh('fsck.fat', ['-n', f]).status !== 0, 'fsck.fat also rejects it');
  // swap IO.SYS out of first place
  const bad2 = Buffer.from(img);
  const rootOff = 19 * 512;
  const a = Buffer.from(bad2.subarray(rootOff, rootOff + 32));
  bad2.subarray(rootOff + 32, rootOff + 64).copy(bad2, rootOff);
  a.copy(bad2, rootOff + 32);
  fs.writeFileSync(f, bad2);
  const k2 = sh('node', [TOOL, 'check', f]);
  ok(k2.status !== 0 && /first two root entries/.test(k2.stdout), 'IO.SYS not first is reported');
}

// ------------------------------------------------------------ CLI ----------
console.log('command line');
{
  const f = path.join(tmp, 'cli.img');
  let r = sh('node', [TOOL, 'build', '--format', 'fd720', '--dir', tree, '--label', 'cli', '-o', f]);
  ok(r.status === 0, `build --format fd720 --dir: ${r.stderr.trim()}`);
  r = sh('node', [TOOL, 'cat', f, 'dos/sub/deep.txt']);
  ok(r.stdout === 'deep\r\n', 'cat (case-insensitive path)');
  r = sh('node', [TOOL, 'ls', f, 'DOS']);
  ok(/BIG\s+BIN\s+70001/.test(r.stdout), 'ls DOS');
  const x = path.join(tmp, 'x');
  r = sh('node', [TOOL, 'extract', f, x]);
  ok(r.status === 0 && fs.readFileSync(path.join(x, 'DOS', 'BIG.BIN')).equals(files['DOS/BIG.BIN']), 'extract');
  r = sh('node', [TOOL, 'info', f]);
  ok(/FAT12, 1440 sectors/.test(r.stdout) && /label "CLI/.test(r.stdout), 'info');
}

fs.rmSync(tmp, { recursive: true, force: true });
console.log(`\n${passes} passed, ${failures} failed`);
process.exit(failures ? 1 : 0);
