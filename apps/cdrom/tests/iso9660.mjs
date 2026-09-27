#!/usr/bin/env node
// apps/cdrom/tests/iso9660.mjs - the ISO 9660 builder (tools/iso9660.mjs):
// build a tree, read it back with our reader, and (if installed) with xorriso.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { buildIso, IsoReader } from '../tools/iso9660.mjs';

let fails = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) fails++; };

// a deterministic pseudo-random tree
let seed = 12345;
const rnd = (n) => { seed = (seed * 1103515245 + 12345) >>> 0; return seed % n; };
const bytes = (n) => { const b = new Uint8Array(n); for (let i = 0; i < n; i++) b[i] = rnd(256); return b; };
const files = [
  { path: 'README.TXT', data: 'Hello from 1993\r\n' },
  { path: 'COPYRGHT.TXT', data: 'Copyright\r\n' },
  { path: 'EMPTY.DAT', data: new Uint8Array(0) },
  { path: 'NOEXT', data: 'no extension' },
  { path: 'EXACT.BIN', data: bytes(2048) },
  { path: 'BIG.BIN', data: bytes(300000) },
  { path: 'A\\B\\C\\D\\E\\F\\G\\DEEP.TXT', data: 'eight levels deep (root = 1)' },
  { path: 'A\\A_.TXT', data: 'x' }, { path: 'A\\A.TXT', data: 'y' }, { path: 'A\\A0.TXT', data: 'z' },
  { path: 'A\\AB', data: 'w' }, { path: 'A\\Z.Z', data: 'q' },
];
// a directory with enough entries to span several sectors
for (let i = 0; i < 150; i++) files.push({ path: `MANY\\FILE${String(i).padStart(3, '0')}.${['TXT', 'EXE', 'BAT'][i % 3]}`, data: bytes(rnd(5000)) });
const img = buildIso({ volumeId: 'TESTDISC', files, dirs: ['EMPTYDIR'], publisher: 'EUROPA MICRO SYSTEMS', copyrightFile: 'COPYRGHT.TXT', date: '1993-06-17 12:34:56' });
check(img.length % 2048 === 0, `image is whole sectors (${img.length / 2048})`);
const img2 = buildIso({ volumeId: 'TESTDISC', files, dirs: ['EMPTYDIR'], publisher: 'EUROPA MICRO SYSTEMS', copyrightFile: 'COPYRGHT.TXT', date: '1993-06-17 12:34:56' });
check(Buffer.compare(Buffer.from(img), Buffer.from(img2)) === 0, 'deterministic');

const r = new IsoReader(img);
check(r.pvd.volumeId === 'TESTDISC' && r.pvd.systemId === 'ARM-DOS' && r.pvd.publisher === 'EUROPA MICRO SYSTEMS', 'PVD identifiers');
check(r.pvd.volumeSpace === img.length / 2048 && r.pvd.volumeSpaceBE === r.pvd.volumeSpace && r.pvd.blockSize === 2048, 'volume space size (both byte orders), block size');
check(r.pvd.terminator, 'volume descriptor set terminator at sector 17');
check(r.pvd.copyrightFile === 'COPYRGHT.TXT;1' && r.pvd.created === '1993061712345600', 'copyright file id, creation date');
let allOk = true;
for (const f of files) {
  const want = typeof f.data === 'string' ? Buffer.from(f.data) : Buffer.from(f.data);
  const got = r.readFile(f.path);
  if (!got || Buffer.compare(Buffer.from(got), want) !== 0) { allOk = false; console.log('  mismatch', f.path); }
}
check(allOk, `all ${files.length} files read back byte-identical`);
const many = r.list('MANY');
check(many.length === 150 && r.lookup('MANY').size > 2048, `150-entry directory spans ${r.lookup('MANY').size / 2048} sectors`);
const names = r.list('A').map((e) => e.name);
check(JSON.stringify(names) === JSON.stringify(['A.TXT', 'A0.TXT', 'AB', 'A_.TXT', 'B', 'Z.Z']), `ISO 9660 sort order (blank-padded names): ${names.join(' ')}`);
check(r.list('EMPTYDIR').length === 0, 'empty directory');
check(r.lookup('README.TXT').rawName === 'README.TXT;1' && r.lookup('NOEXT').rawName === 'NOEXT.;1', 'file identifiers with ;1 (NOEXT.;1)');
check(r.lookup('README.TXT').date.y === 1993 && r.lookup('README.TXT').date.s === 56, 'recording date 1993');
const ptL = r.pathTable(false), ptM = r.pathTable(true);
check(JSON.stringify(ptL) === JSON.stringify(ptM), 'L and M path tables agree');
const allDirs = r.walk().filter((e) => e.rec.dir);
check(ptL.length === allDirs.length + 1 && ptL.every((p) => p.name === '' || allDirs.some((d) => d.rec.extent === p.extent)), `path table lists every directory (${ptL.length})`);
let ptOrder = true;
for (let i = 1; i < ptL.length; i++) if (ptL[i].parent < ptL[i - 1].parent) ptOrder = false;
check(ptOrder, 'path table ordered by parent number');
// no directory record crosses a sector boundary (our reader would stop at the 0 pad otherwise; it found all 150)
let threw = false;
try { buildIso({ files: [{ path: 'TOOLONGNAME.TXT', data: 'x' }] }); } catch { threw = true; }
check(threw, 'rejects a non-8.3 name');
threw = false;
try { buildIso({ files: [{ path: 'A\\B\\C\\D\\E\\F\\G\\H\\X.TXT', data: 'x' }] }); } catch { threw = true; }
check(threw, 'rejects depth > 8');

// independent check with xorriso
const xo = spawnSync('xorriso', ['-version'], { encoding: 'utf8' });
if (xo.status === 0) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'iso-'));
  const iso = path.join(tmp, 't.iso');
  fs.writeFileSync(iso, img);
  const ls = spawnSync('xorriso', ['-abort_on', 'FAILURE', '-return_with', 'WARNING', '32', '-indev', iso, '-find', '/', '-type', 'f'], { encoding: 'utf8' });
  const found = (ls.stdout || '').split('\n').filter((l) => l.startsWith("'/")).length;
  check(ls.status === 0 && found === files.length, `xorriso reads the image without warnings and finds ${found} files (status ${ls.status})`);
  if (ls.status !== 0) console.log(ls.stderr.split('\n').slice(-12).join('\n'));
  const out = path.join(tmp, 'x');
  const ex = spawnSync('xorriso', ['-osirrox', 'on', '-indev', iso, '-extract', '/', out], { encoding: 'utf8' });
  let same = ex.status === 0;
  for (const f of files) {
    const p = path.join(out, ...f.path.split('\\'));
    const want = Buffer.from(typeof f.data === 'string' ? f.data : f.data);
    if (!fs.existsSync(p) || Buffer.compare(fs.readFileSync(p), want) !== 0) { same = false; console.log('  xorriso mismatch', f.path); }
  }
  check(same, 'xorriso -extract: every file byte-identical');
  const pvd = spawnSync('xorriso', ['-indev', iso, '-pvd_info'], { encoding: 'utf8' });
  check(/Volume Id\s*:\s*TESTDISC/.test(pvd.stdout) && /Publisher Id\s*:\s*EUROPA MICRO SYSTEMS/.test(pvd.stdout), 'xorriso -pvd_info: volume and publisher ids');
  spawnSync('chmod', ['-R', 'u+w', tmp]);
  fs.rmSync(tmp, { recursive: true, force: true });
} else console.log('skip xorriso not installed (brew install xorriso / apt install xorriso)');

console.log(fails ? `${fails} FAILED` : 'iso9660: all passed');
process.exit(fails ? 1 : 0);
