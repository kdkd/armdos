#!/usr/bin/env node
// Round-trip test of web/js/fat.js against mtools and fsck.fat.
//   node web/tests/test-fat.mjs     (needs build/floppy-blank.img, build/hd.img; mtools + dosfstools)
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { FatVolume, DiskFullError, hostTo83 } from '../js/fat.js';

const ROOT = new URL('../..', import.meta.url).pathname;
let fails = 0, passes = 0;
const check = (name, ok, extra = '') => { if (ok) passes++; else { fails++; console.log(`FAIL ${name} ${extra}`); } };
const tmp = mkdtempSync(join(tmpdir(), 'armdos-fat-'));
const sh = (cmd, args, env = {}) => { try { return execFileSync(cmd, args, { encoding: 'latin1', env: { ...process.env, MTOOLS_SKIP_CHECK: '1', ...env } }); } catch (e) { return 'ERROR ' + (e.stdout || '') + (e.stderr || ''); } };
const rnd = (n, seed) => { const a = new Uint8Array(n); let x = seed; for (let i = 0; i < n; i++) { x = (x * 1103515245 + 12345) >>> 0; a[i] = x >>> 24; } return a; };

// ---- names
check('8.3 simple', hostTo83('readme.txt') === 'README  TXT');
check('8.3 truncate', hostTo83('A very long file name.markdown') === 'AVERYLONMAR');
check('8.3 clash', hostTo83('longfilename.txt', new Set(['LONGFILETXT'])) === 'LONGFIL1TXT');
check('8.3 invalid chars', hostTo83('a+b=c[1].c') === 'A_B_C_1_C  ');
check('8.3 dotfile / no base', hostTo83('.bashrc') === 'BASHRC     ');

// ---- floppy
{
  const img = new Uint8Array(readFileSync(join(ROOT, 'build/floppy-blank.img')));
  const v = new FatVolume(img);
  const files = { 'hello.txt': new TextEncoder().encode('Hello from the browser!\r\n'), 'big.bin': rnd(300000, 7), 'long file name.text': rnd(1500, 3), 'longfilx.txt': rnd(10, 1), 'empty.dat': new Uint8Array(0) };
  const names = {};
  for (const [n, d] of Object.entries(files)) names[n] = v.writeFile(null, n, d);
  check('names chosen', names['long file name.text'] === 'LONGFILE.TEX', JSON.stringify(names));
  // replace hello.txt with other content
  v.writeFile(null, 'HELLO.TXT', new TextEncoder().encode('replaced'));
  files['hello.txt'] = new TextEncoder().encode('replaced');
  // disk full
  let full = false; const before = img.slice();
  try { v.writeFile(null, 'huge.bin', new Uint8Array(1400000)); } catch (e) { full = e instanceof DiskFullError; }
  check('disk full is reported', full);
  check('disk full changes nothing', Buffer.compare(Buffer.from(before), Buffer.from(img)) === 0);
  check('dirty sectors tracked', v.takeDirty().length > 0);
  const p = join(tmp, 'fd.img'); writeFileSync(p, img);
  const dir = sh('mdir', ['-i', p, '::']);
  check('mdir lists the files', /HELLO\s+TXT/.test(dir) && /BIG\s+BIN\s+300000/.test(dir) && /LONGFILE TEX/.test(dir), dir);
  for (const [n, d] of Object.entries(files)) {
    const out = join(tmp, 'x'); sh('mcopy', ['-n', '-i', p, '::' + names[n], out]);
    let got; try { got = readFileSync(out); } catch { got = Buffer.alloc(0); }
    check(`mcopy round trip ${n}`, Buffer.compare(got, Buffer.from(d)) === 0, `${got.length} vs ${d.length}`);
    rmSync(out, { force: true });
  }
  const fsck = sh('fsck.fat', ['-n', p]);
  check('fsck.fat clean (floppy)', !/ERROR|differ|Free cluster summary wrong|lost/i.test(fsck) || /0 files/.test(''), fsck);
  // and our own reader agrees with a file mtools writes
  const q = join(tmp, 'note.txt'); writeFileSync(q, 'written by mtools');
  sh('mcopy', ['-i', p, q, '::NOTE.TXT']);
  const v2 = new FatVolume(new Uint8Array(readFileSync(p)));
  const e = v2.entries(null).find((x) => x.name === 'NOTE.TXT');
  check('reads mtools-written file', e && new TextDecoder().decode(v2.readFile(e)) === 'written by mtools');
  // fill the root directory (224 entries)
  let rootFull = false;
  try { for (let k = 0; k < 300; k++) v2.writeFile(null, `f${k}.txt`, new Uint8Array(1)); } catch (err) { rootFull = err instanceof DiskFullError && /root/.test(err.message); }
  check('root directory full is reported', rootFull);
  writeFileSync(p, v2.img);
  check('fsck.fat clean after filling root', !/ERROR|differ|wrong|lost/i.test(sh('fsck.fat', ['-n', p])));
}

// ---- hard disk: FAT16 in a partition, subdirectory growth
{
  const img = new Uint8Array(readFileSync(join(ROOT, 'build/hd.img')));
  const v = new FatVolume(img);
  check('hd is FAT16', v.L.type === 16);
  v.writeFile(null, 'fromweb.txt', new TextEncoder().encode('C: from the web'));
  const sub = v.entries(null).find((e) => e.attr & 0x10);
  if (sub) for (let k = 0; k < 80; k++) v.writeFile(sub, `n${k}.txt`, rnd(3000, k));   // grows the directory past one 2 KB cluster
  const p = join(tmp, 'hd.img'); writeFileSync(p, img);
  const off = v.off;
  const env = { MTOOLSRC: join(tmp, 'mtoolsrc') };
  writeFileSync(env.MTOOLSRC, `drive c: file="${p}" offset=${off}\nmtools_skip_check=1\n`);
  const dir = sh('mdir', ['c:'], env);
  check('mdir C: shows the file', /FROMWEB\s+TXT\s+15/.test(dir), dir);
  if (sub) {
    const sd = sh('mdir', ['c:/' + sub.name], env);
    check('subdirectory grew and lists 80 files', /N79\s+TXT/.test(sd) && /N0\s+TXT/.test(sd), sd.slice(-400));
    const out = join(tmp, 'y'); sh('mcopy', ['-n', `c:/${sub.name}/N42.TXT`, out], env);
    check('subdir file round trip', Buffer.compare(readFileSync(out), Buffer.from(rnd(3000, 42))) === 0);
  }
  // fsck on the partition slice
  const part = join(tmp, 'part.img'); writeFileSync(part, img.subarray(off));
  const f = sh('fsck.fat', ['-n', part]);
  check('fsck.fat clean (hard disk)', !/ERROR|differ|wrong|lost/i.test(f), f);
}
rmSync(tmp, { recursive: true, force: true });
console.log(`fat: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
