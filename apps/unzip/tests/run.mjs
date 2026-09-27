#!/usr/bin/env node
// apps/unzip/tests/run.mjs - UNZIP.EXE on ARM-DOS, headless. The real Keen
// Dreams shareware zip (deflate + stored) and zips built here with the host's
// `zip` (directories, an empty file, stored files, a corrupted member) are put
// on a private C:, extracted by UNZIP, and read back from the disk image and
// compared byte for byte with what the host's `unzip` produces.
import fs from 'node:fs';
import path from 'node:path';
import { execSync } from 'node:child_process';
import { makeDisk, startPC, check, failed, readFile, ROOT, B } from '../../term/tests/lib.mjs';
import { FatReader } from '../../../disk/mkimage.mjs';

const W = B('unzip-test');
fs.rmSync(W, { recursive: true, force: true });
fs.mkdirSync(W, { recursive: true });
const KEEN = path.join(ROOT, '3rdparty/keen/KEENDRMS.ZIP');

// ---- host-side reference data
const src = path.join(W, 'src');
fs.mkdirSync(path.join(src, 'SUB/DEEP'), { recursive: true });
fs.writeFileSync(path.join(src, 'SUB/NUMS.TXT'), Array.from({ length: 5000 }, (_, i) => `${i + 1}\r\n`).join(''));
fs.writeFileSync(path.join(src, 'EMPTY.TXT'), '');
fs.writeFileSync(path.join(src, 'SUB/DEEP/HI.TXT'), 'hello from a deep directory\r\n');
let seed = 12345; const rnd = Buffer.alloc(3000, 0).map(() => (seed = (seed * 1103515245 + 12345) >>> 0) >>> 24);
fs.writeFileSync(path.join(src, 'RAND.BIN'), rnd);
fs.writeFileSync(path.join(src, 'README.TXT'), 'The quick brown fox jumps over the lazy dog.\r\n'.repeat(200));
execSync('zip -qrX ../TREE.ZIP README.TXT SUB EMPTY.TXT && zip -qX0 ../TREE.ZIP RAND.BIN', { cwd: src });
const tree = fs.readFileSync(path.join(W, 'TREE.ZIP'));
const bad = Buffer.from(tree);
{ const i = bad.indexOf('SUB/NUMS.TXT') + 12 + 300; bad[i] ^= 0x55; }
fs.writeFileSync(path.join(W, 'BAD.ZIP'), bad);
fs.writeFileSync(path.join(W, 'TRUNC.ZIP'), fs.readFileSync(KEEN).subarray(0, 200000));
const ref = path.join(W, 'ref');
fs.mkdirSync(ref);
execSync(`unzip -qo "${KEEN}" -d "${ref}"`);
const hostList = execSync(`unzip -v "${KEEN}"`).toString();
const members = [...hostList.matchAll(/^\s*(\d+)\s+(Defl:X|Stored)\s+(\d+)\s+\d+%\s+\S+\s+\S+\s+([0-9a-f]{8})\s+(\S+)$/gm)]
  .map((m) => ({ len: +m[1], size: +m[3], crc: m[4], name: m[5] }));
check(members.length === 24, `host unzip -v lists the 24 members of KEENDRMS.ZIP`);

// errorlevel reporter: RC.BAT <command...>
const rcbat = '@ECHO OFF\n%1 %2 %3 %4 %5 %6 %7 %8 %9\n' +
  [51, 50, 11, 10, 9, 3, 1].map((n) => `IF ERRORLEVEL ${n} GOTO E${n}`).join('\n') + '\nECHO RC=0\nGOTO END\n' +
  [51, 50, 11, 10, 9, 3, 1].map((n) => `:E${n}\nECHO RC=${n}\nGOTO END`).join('\n') + '\n:END\nECHO ZZDONE\n';

const img = makeDisk('unzip', {
  files: [
    { src: 'build/UNZIP.EXE', dst: 'DOS\\UNZIP.EXE' },
    { src: KEEN, dst: 'ZIPS\\KEENDRMS.ZIP' },
    { src: path.join(W, 'TREE.ZIP'), dst: 'ZIPS\\TREE.ZIP' },
    { src: path.join(W, 'BAD.ZIP'), dst: 'ZIPS\\BAD.ZIP' },
    { src: path.join(W, 'TRUNC.ZIP'), dst: 'ZIPS\\TRUNC.ZIP' },
  ],
  texts: { 'RC.BAT': rcbat },
  dirs: ['ZIPS', 'K', 'T', 'OUT', 'W'],
});
// nojit: run on the interpreter (the emulator's JIT off)
const pc = await startPC(img, { jit: !process.argv.includes('nojit') });
check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'ARM-DOS boots to C:\\>');
const shot = (n) => pc.shot(`unzip-${n}.png`);

let outN = 0;
function run(cmd, timeoutMs = 120000) {
  pc.type('CLS\r'); pc.waitIdle();
  const t0 = pc.m.timeMs();
  pc.type(`\\RC ${cmd}\r`);
  pc.until(() => pc.screen().includes('ZZDONE') || pc.screen().includes('(y/n/a/r)?'), { timeoutMs });
  const scr = pc.screen();
  const rc = +(scr.match(/RC=(\d+)/) || [])[1];
  return { scr, rc, ms: pc.m.timeMs() - t0 };
}
function out(cmd) {                       // stdout redirected to a file
  const f = `OUT\\O${++outN}.TXT`;
  pc.type('CLS\r'); pc.waitIdle();
  pc.type(`${cmd} >\\${f}\r`);             // (a batch file's output cannot be redirected in DOS)
  pc.waitIdle({ timeoutMs: 120000 });
  pc.type('\\RC\r');                       // RC with no command: just reports ERRORLEVEL
  pc.until(() => pc.screen().includes('ZZDONE'));
  const o = (readFile(pc, f) || Buffer.alloc(0)).toString('latin1');
  return { out: o, rc: +(pc.screen().match(/RC=(\d+)/) || [])[1], scr: pc.screen() };
}
const dirEnt = (p) => { const img2 = pc.m.ata.img; try { return new FatReader(Buffer.from(img2.buffer, img2.byteOffset, img2.length)).lookup(p); } catch { return null; } };

// ---- banner + usage
let r = run('UNZIP');
check(r.rc === 0 && r.scr.includes('UNZIP  Extract Utility  Version 1.0  ARM-DOS') && r.scr.includes('Usage:'), 'UNZIP alone: banner and usage, errorlevel 0');
check(!/PKWARE|PKUNZIP/.test(r.scr), 'no PKWARE / PKUNZIP branding');

// ---- -v listing vs the host
r = out('UNZIP -v \\ZIPS\\KEENDRMS');
check(r.rc === 0 && r.out.includes('Searching ZIP: \\ZIPS\\KEENDRMS.ZIP'), 'UNZIP -v (".ZIP" added to the name), errorlevel 0');
let okList = true;
for (const m of members) {
  const re = new RegExp(`^\\s*${m.len}\\s+(DeflatX|Stored)\\s+${m.size}\\s+\\d+%\\s+\\d\\d-\\d\\d-9\\d\\s+\\d\\d:\\d\\d\\s+${m.crc}\\s+\\S{4}\\s+${m.name.replace('.', '\\.')}\\s*$`, 'm');
  if (!re.test(r.out)) { okList = false; console.log(`     missing/different: ${m.name}`); }
}
check(okList, '-v: every member with the host\'s length, size, CRC-32');
check(/444558\s+356106\s+20%\s+24/.test(r.out), '-v totals line: 444558 356106 20% 24 files');
console.log(r.out.split('\r\n').slice(3, 8).map((l) => '     | ' + l).join('\n'));

// ---- test
r = out('UNZIP -t \\ZIPS\\KEENDRMS.ZIP');
check(r.rc === 0 && (r.out.match(/Testing: \S+\s+OK/g) || []).length === 24, '-t: 24 x "Testing: ... OK"');

// ---- extract the Keen zip, measure
pc.type('CD \\K\r'); pc.waitIdle();
r = run('UNZIP -o \\ZIPS\\KEENDRMS.ZIP');
await shot('keen');
const tExtract = r.ms;
check(r.rc === 0 && r.scr.includes('Inflating: KDREAMS.EGA') && r.scr.includes('Extracting: KDREAMS.CMP'),
  'UNZIP -o KEENDRMS.ZIP: "Inflating: KDREAMS.EGA", "Extracting: KDREAMS.CMP", errorlevel 0');
let same = 0;
for (const m of members) {
  const got = readFile(pc, `K\\${m.name}`);
  if (got && Buffer.compare(got, fs.readFileSync(path.join(ref, m.name))) === 0) same++;
  else console.log(`     differs: ${m.name}`);
}
check(same === 24, `all 24 files byte-identical to the host's unzip (${same}/24)`);
const ega = dirEnt('K\\KDREAMS.EGA');
// 1992-08-05 08:22 -> date ((92-80)<<9)|(8<<5)|5, time (8<<11)|(22<<5)
check(ega && ega.date === ((12 << 9) | (8 << 5) | 5) && (ega.time & ~31) === ((8 << 11) | (22 << 5)), 'file date/time from the zip (KDREAMS.EGA 08-05-92 8:22a)');
console.log(`     emulated time to extract KEENDRMS.ZIP (444,558 bytes, 24 files): ${(tExtract / 1000).toFixed(2)} s (incl. batch file overhead)`);

// ---- overwrite prompt
r = run('UNZIP \\ZIPS\\KEENDRMS.ZIP HELP.BAT');
check(r.scr.includes('WARNING: HELP.BAT already exists.  Overwrite (y/n/a/r)?'), 'existing file without -o: "Overwrite (y/n/a/r)?"');
await shot('prompt');
pc.type('n'); pc.until(() => pc.screen().includes('ZZDONE'));
check(pc.screen().includes('RC=0'), '"n" skips it, errorlevel 0');
r = run('UNZIP \\ZIPS\\KEENDRMS.ZIP *.BAT');
check(r.scr.includes('HELP.BAT already exists'), 'wildcard *.BAT selects HELP.BAT (prompt)');
pc.type('a'); pc.until(() => pc.screen().includes('ZZDONE'));
check(pc.screen().includes('Extracting: VENDOR.BAT') && !pc.screen().includes('VENDOR.BAT already') && pc.screen().includes('RC=0'), '"a" = all: VENDOR.BAT overwritten without a second question');

// ---- -n: nothing newer
r = run('UNZIP -n \\ZIPS\\KEENDRMS.ZIP');
check(r.rc === 0 && !r.scr.includes('Inflating') && !r.scr.includes('WARNING'), '-n: files on disk are as new, nothing extracted');

// ---- -d, directories, empty file, stored; output directory argument
r = run('UNZIP -d \\ZIPS\\TREE.ZIP \\T\\');
check(r.rc === 0, '-d TREE.ZIP \\T\\: errorlevel 0');
const want = { 'SUB/NUMS.TXT': 'T\\SUB\\NUMS.TXT', 'SUB/DEEP/HI.TXT': 'T\\SUB\\DEEP\\HI.TXT', 'EMPTY.TXT': 'T\\EMPTY.TXT', 'RAND.BIN': 'T\\RAND.BIN', 'README.TXT': 'T\\README.TXT' };
let dok = 0;
for (const [h, d] of Object.entries(want)) { const g = readFile(pc, d); if (g && Buffer.compare(g, fs.readFileSync(path.join(src, h))) === 0) dok++; else console.log(`     ${d} wrong`); }
check(dok === 5, '-d: SUB\\DEEP\\HI.TXT, SUB\\NUMS.TXT, empty, stored and deflated files intact');
check(r.scr.includes('Extracting: \\T\\RAND.BIN') && r.scr.includes('Inflating: \\T\\SUB\\NUMS.TXT'), 'stored member "Extracting:", deflated "Inflating:"');
r = run('UNZIP -o \\ZIPS\\TREE.ZIP \\W\\ HI.TXT');
check(r.rc === 0 && readFile(pc, 'W\\HI.TXT')?.toString() === 'hello from a deep directory\r\n' && !dirEnt('W\\SUB'), 'without -d the path is stripped (\\W\\HI.TXT)');

// ---- -c
r = run('UNZIP -c \\ZIPS\\TREE.ZIP HI.TXT');
check(r.scr.includes('hello from a deep directory'), '-c: member shown on the console');

// ---- errors
r = run('UNZIP -o \\ZIPS\\BAD.ZIP \\W\\');
check(r.rc === 3 && r.scr.includes('NUMS.TXT') && /CRC error|bad compressed data/.test(r.scr), 'corrupted member: warning, errorlevel 3');
check(readFile(pc, 'W\\README.TXT') !== null, 'the other members are still extracted');
await shot('bad');
r = run('UNZIP -t \\ZIPS\\TRUNC.ZIP');
check(r.rc === 51 || r.rc === 3, `truncated zip: errorlevel ${r.rc} (51 or 3)`);
r = run('UNZIP \\ZIPS\\NOSUCH.ZIP');
check(r.rc === 9 && r.scr.includes('not found'), 'missing zip: errorlevel 9');
r = run('UNZIP -q \\ZIPS\\TREE.ZIP');
check(r.rc === 10 && r.scr.includes('Invalid option'), 'bad option: errorlevel 10');
r = run('UNZIP -o \\ZIPS\\TREE.ZIP \\W\\ NOPE.*');
check(r.rc === 11 && r.scr.includes('No file(s) found'), 'no matching files: errorlevel 11');

console.log(failed() ? `\n${failed()} FAILED` : '\nall UNZIP tests passed');
process.exit(failed() ? 1 : 0);
