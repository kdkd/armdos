// apps/format/tests/lib.mjs - shared harness for the disk utility tests
// (FORMAT, SYS, CHKDSK, DISKCOPY, DISKCOMP).
// The test shell cannot take both '<' and '>' on one line: C:\T\REDIR.EXE
// (tests/redir.c) does that: REDIR out|- in|- program args.
//
// Boots ARM-DOS headless from a scratch hard disk (IO.SYS, ARMDOS.SYS, the
// kernel's test shell in interactive mode, the utilities in C:\DOS) with a
// given floppy image in drive A:, types commands at the shell's "T>" prompt
// and lets the tests look at the screen, the redirected output files on C:
// (read with disk/mkimage.mjs's own FAT reader) and the disk images
// afterwards (fsck.fat -n, mtools). dosfstools and mtools are optional test
// extras: where they are not installed, the checks that need them are skipped
// with a note (checkFsck, checkMdir), and everything else still runs.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const B = (p) => path.join(ROOT, 'build', p);
export const OUT = B('diskutil-test');
fs.mkdirSync(OUT, { recursive: true });

let failures = 0, checks = 0, skipped = 0;
export const check = (ok, what, detail) => {
  checks++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`);
  if (!ok) { failures++; if (detail !== undefined) console.log(String(detail).split('\n').map(l => '     | ' + l).join('\n')); }
  return ok;
};
export const summary = () => {
  console.log(`${checks - failures}/${checks} checks passed${skipped ? `, ${skipped} skipped (host tool not installed)` : ''}`);
  return failures;
};

export const UTILS = ['FORMAT', 'SYS', 'CHKDSK', 'DISKCOPY', 'DISKCOMP'];

/** a C: image: kernel, shell, utilities; extra = { 'DOS\\X': Buffer|string } */
export function hdImage(name, { extra = {}, label = 'DUTEST', dirs = [] } = {}) {
  const dir = path.join(OUT, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/ktest/TSHELL.EXE', dst: 'T\\TSHELL.EXE' },
    { src: 'build/diskutil-test/DUREDIR.EXE', dst: 'T\\REDIR.EXE' },
  ];
  for (const u of UTILS) if (fs.existsSync(B(u + '.COM'))) files.push({ src: `build/${u}.COM`, dst: 'DOS\\' });
  const put = (dst, content) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, typeof content === 'string' ? content.replace(/\r?\n/g, '\r\n') : content);
    files.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', 'FILES=20\nSHELL=C:\\T\\TSHELL.EXE C:\\T\\S.TXT\n');
  put('T\\S.TXT', 'interactive\n');
  for (const [n, c] of Object.entries(extra)) put(n, c);
  const m = {
    format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label, date: '1988-06-17 12:00:00',
    boot: { src: 'build/bootsect.bin' }, files, dirs: ['DOS', 'T', ...dirs],
  };
  return new Uint8Array(buildImage(m, ROOT).img);
}

/** a 1.44 MB floppy built by mkimage; files = { NAME: Buffer|string } */
export function floppyImage(label, files = {}, { dirs = [], date = '1988-06-17 12:00:00', serial = '1234-5678' } = {}) {
  const dir = path.join(OUT, 'fdsrc-' + Math.random().toString(36).slice(2));
  fs.mkdirSync(dir, { recursive: true });
  const list = [];
  for (const [n, c] of Object.entries(files)) {
    const host = path.join(dir, n.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, c);
    list.push({ src: path.relative(ROOT, host), dst: n });
  }
  const m = { format: 'fd1440', label, date, serial, files: list, dirs };
  const img = new Uint8Array(buildImage(m, ROOT).img);
  fs.rmSync(dir, { recursive: true, force: true });
  return img;
}

export const blankFloppy = () => new Uint8Array(1474560);

/** boot to the test shell's prompt */
export async function start(hd, fd, opts = {}) {
  // boot from C: (the BIOS would boot a diskette in A:), then insert it
  const pc = await boot({ rom: B('rom.bin'), hd, ...opts });
  pc.hd = hd;
  if (!pc.waitText('T>', { timeoutMs: 30000 })) throw new Error('no shell prompt:\n' + pc.screen() + '\n' + pc.serial);
  if (fd) { pc.machine.insertFloppy(fd, !!opts.wp); pc.run(50); }
  return pc;
}

/** type a command line at T> and (optionally) wait for the program to end */
export function cmd(pc, line, { wait = true, timeoutMs = 120000 } = {}) {
  const before = (pc.serial.match(/T:EXIT /g) || []).length;
  pc.type(line + '\r');
  if (!wait) return true;
  const ok = pc.until(() => (pc.serial.match(/T:EXIT /g) || []).length > before, { timeoutMs, stepMs: 20 });
  if (ok) pc.waitText('T>', { timeoutMs: 5000 });
  return ok;
}
export const exitCode = (pc) => {
  const m = [...pc.serial.matchAll(/T:EXIT \S+ (\d+) (\d+)/g)].pop();
  return m ? +m[1] : -1;
};

/** wait for text, then type */
export function answer(pc, waitFor, keys, timeoutMs = 60000) {
  const ok = pc.waitText(waitFor, { timeoutMs, stepMs: 20 });
  if (ok) pc.type(keys);
  return ok;
}

// ------------------------------------------------------------ images
function tool(t) { try { execFileSync('which', [t], { stdio: 'ignore' }); return true; } catch { return false; } }
export const HAVE_FSCK = tool('fsck.fat'), HAVE_MTOOLS = tool('mdir');
const MENV = { ...process.env, MTOOLS_SKIP_CHECK: '1', MTOOLS_NO_VFAT: '1' };
const INSTALL = {
  mtools: 'mtools not installed (brew install mtools / apt install mtools dosfstools)',
  dosfstools: 'dosfstools not installed (brew install dosfstools / apt install dosfstools)',
};
const noted = new Set();
/** a check that needs a missing host tool: skipped; the note is printed once per tool */
function skip(pkg) {
  skipped++;
  if (!noted.has(pkg)) { noted.add(pkg); console.log(`skip: ${INSTALL[pkg]}`); }
  return true;
}

export function save(img, name) {
  const p = path.join(OUT, name);
  fs.writeFileSync(p, img);
  return p;
}
const partOff = (img) => (img[0x1FE] === 0x55 && img.length > 3000000 ? (img[0x1C6] | (img[0x1C7] << 8) | (img[0x1C8] << 16)) * 512 : 0);

/** fsck.fat -n on a floppy or the C: partition: '' if clean, else the report
 *  ('' also when dosfstools is not installed: use checkFsck for checks) */
export function fsck(img, name) {
  if (!HAVE_FSCK) return '';
  const off = partOff(img);
  const p = save(img.subarray(off), name + '.part');
  try { execFileSync('fsck.fat', ['-n', p], { stdio: 'pipe' }); return ''; }
  catch (e) { return (e.stdout?.toString() || '') + (e.stderr?.toString() || ''); }
  finally { fs.rmSync(p, { force: true }); }
}

/** check: fsck.fat -n finds the image clean (skipped without dosfstools) */
export function checkFsck(img, name, what) {
  if (!HAVE_FSCK) return skip('dosfstools');
  const r = fsck(img, name);
  return check(r === '', what, r);
}

/** an mtools command's output ('' when mtools is not installed: use checkMdir for checks) */
export function mtools(toolName, img, args, name = 'mt') {
  if (!HAVE_MTOOLS) return '';
  const p = save(img, name + '.img');
  const off = partOff(img);
  try { return execFileSync(toolName, ['-i', `${p}@@${off}`, ...args], { env: MENV, stdio: 'pipe' }).toString(); }
  catch (e) { return 'ERROR ' + (e.stdout?.toString() || '') + (e.stderr?.toString() || ''); }
  finally { fs.rmSync(p, { force: true }); }
}

/** check: test(mdir's listing of the image) (skipped without mtools) */
export function checkMdir(img, args, test, what) {
  if (!HAVE_MTOOLS) return skip('mtools');
  const dir = mtools('mdir', img, args);
  return check(test(dir), what, dir);
}

/** a file from an image (floppy or partitioned hard disk), as a latin1
 *  string, or null if it is not there; read with mkimage's FAT reader, so
 *  the output comparisons need no host tools */
export function readFile(img, dosPath) {
  try {
    const r = new FatReader(Buffer.from(img.buffer, img.byteOffset, img.byteLength));
    const ent = r.lookup(dosPath);
    return ent && !(ent.attr & 0x10) ? r.readFile(ent).toString('latin1') : null;
  } catch { return null; }
}

/** show CR LF as visible text for diffs */
export const vis = (s) => s.replace(/\r/g, '\\r').replace(/\n/g, '\\n\n');

/** compare two outputs; prints a small diff on mismatch */
export function same(got, want, what) {
  if (got === want) return check(true, what);
  const g = vis(got ?? '(null)').split('\n'), w = vis(want).split('\n');
  const out = [];
  for (let i = 0; i < Math.max(g.length, w.length); i++) {
    if (g[i] !== w[i]) out.push(`line ${i + 1}:\n  got  ${JSON.stringify(g[i])}\n  want ${JSON.stringify(w[i])}`);
    if (out.length > 6) break;
  }
  return check(false, what, out.join('\n'));
}

// FAT12 editing for corrupted test floppies (1.44 MB layout: FATs at 1 and 10, root at 19)
export class Fat12 {
  constructor(img) { this.b = img; }
  get(n) { const o = 512 + ((n * 3) >> 1), v = this.b[o] | (this.b[o + 1] << 8); return n & 1 ? v >> 4 : v & 0xFFF; }
  set(n, v) {
    for (const fo of [512, 512 + 9 * 512]) {
      const o = fo + ((n * 3) >> 1); let w = this.b[o] | (this.b[o + 1] << 8);
      w = n & 1 ? (w & 0x000F) | (v << 4) : (w & 0xF000) | v;
      this.b[o] = w & 0xFF; this.b[o + 1] = (w >> 8) & 0xFF;
    }
  }
  ent(name11) {
    for (let i = 0; i < 224; i++) {
      const e = 19 * 512 + i * 32;
      if (String.fromCharCode(...this.b.subarray(e, e + 11)) === name11) return e;
    }
    throw new Error('no ' + name11);
  }
  first(name11) { const e = this.ent(name11); return this.b[e + 26] | (this.b[e + 27] << 8); }
  setSize(name11, n) { const e = this.ent(name11); this.b[e + 28] = n & 255; this.b[e + 29] = (n >> 8) & 255; this.b[e + 30] = (n >> 16) & 255; this.b[e + 31] = n >>> 24; }
  free() { const r = []; for (let n = 2; n < 2849; n++) if (this.get(n) === 0) r.push(n); return r; }
}
