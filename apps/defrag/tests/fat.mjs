// apps/defrag/tests/fat.mjs - an independent FAT12/FAT16 reader for the
// DEFRAG tests: the volume (a floppy image or the first partition of a hard
// disk image), every file's cluster chain, fragmentation, the layout.
// And fragment(): builds fragmented volumes with mtools (the authority on
// how DOS-like allocation behaves), keeping a model of every file's content.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

export const partOff = (img) => (img[0x1FE] === 0x55 && img.length > 3000000 ? (img[0x1C6] | (img[0x1C7] << 8) | (img[0x1C8] << 16)) * 512 : 0);

export function volume(img) {
  const off = partOff(img);
  const b = img.subarray(off);
  const u16 = (o) => b[o] | (b[o + 1] << 8), u32 = (o) => (u16(o) | (u16(o + 2) << 16)) >>> 0;
  const bps = u16(11), spc = b[13], res = u16(14), nfats = b[16], rootEnts = u16(17);
  const total = u16(19) || u32(32), fatSz = u16(22);
  const rootSec = res + nfats * fatSz, rootSecs = (rootEnts * 32 + bps - 1) / bps | 0;
  const dataSec = rootSec + rootSecs;
  const nclus = ((total - dataSec) / spc) | 0, maxc = nclus + 1;
  const fat16 = nclus + 1 >= 4086;
  const fatOff = res * bps;
  const fget = (c) => {
    if (fat16) return u16(fatOff + c * 2);
    const o = fatOff + c + (c >> 1), v = u16(o);
    return c & 1 ? v >> 4 : v & 0xFFF;
  };
  const eof = fat16 ? 0xFFF8 : 0xFF8;
  const csize = spc * bps;
  const cl2off = (c) => (dataSec + (c - 2) * spc) * bps;
  const chain = (c) => { const r = []; while (c >= 2 && c <= maxc && r.length <= nclus) { r.push(c); c = fget(c); if (c >= eof) break; } return r; };
  const files = [];
  const readDir = (cl) => {
    if (cl === 0) return b.subarray(rootSec * bps, (rootSec + rootSecs) * bps);
    return Buffer.concat(chain(cl).map((c) => b.subarray(cl2off(c), cl2off(c) + csize)));
  };
  const walk = (cl, prefix, parent) => {
    const d = readDir(cl);
    for (let i = 0; i < d.length; i += 32) {
      if (d[i] === 0) break;
      if (d[i] === 0xE5 || d[i] === 0x2E) continue;
      const attr = d[i + 11];
      if (attr & 8) continue;
      const nm = Buffer.from(d.subarray(i, i + 8)).toString('latin1').trimEnd();
      const ex = Buffer.from(d.subarray(i + 8, i + 11)).toString('latin1').trimEnd();
      const name = prefix + nm + (ex ? '.' + ex : '');
      const first = d[i + 26] | (d[i + 27] << 8);
      const size = (d[i + 28] | (d[i + 29] << 8) | (d[i + 30] << 16) | (d[i + 31] << 24)) >>> 0;
      const ch = first ? chain(first) : [];
      let frags = ch.length ? 1 : 0;
      for (let k = 1; k < ch.length; k++) if (ch[k] !== ch[k - 1] + 1) frags++;
      const f = { name, attr, first, size, chain: ch, frags, dir: !!(attr & 0x10), parent: cl, index: i / 32 };
      files.push(f);
      if (f.dir) {
        const sub = readDir(first);
        f.dot = sub[26] | (sub[27] << 8);
        f.dotdot = sub[32 + 26] | (sub[32 + 27] << 8);
        walk(first, name + '\\', cl);
      }
    }
  };
  walk(0, '\\', 0);
  const used = new Set();
  for (const f of files) for (const c of f.chain) used.add(c);
  const fixed = (f) => !!(f.attr & 6);
  const content = (f) => Buffer.concat(f.chain.map((c) => b.subarray(cl2off(c), cl2off(c) + csize))).subarray(0, f.dir ? undefined : f.size);
  return { off, bps, spc, csize, nclus, maxc, fat16, files, fget, used, fixed, content, rootEntries: () => readDir(0) };
}

/** fragmentation summary */
export function fragInfo(v) {
  const movable = v.files.filter((f) => !v.fixed(f) && f.chain.length);
  const fragFiles = movable.filter((f) => f.frags > 1);
  // free clusters below the last movable used cluster (Full Optimization packs them away)
  let last = 0;
  for (const f of movable) for (const c of f.chain) last = Math.max(last, c);
  let holes = 0;
  const fixedC = new Set(v.files.filter((f) => v.fixed(f)).flatMap((f) => f.chain));
  for (let c = 2; c < last; c++) if (!v.used.has(c) && v.fget(c) === 0) holes++;
  return { files: movable.length, fragFiles: fragFiles.length, frags: fragFiles.reduce((a, f) => a + f.frags, 0), holes, last, fixedC };
}

// ------------------------------------------------------------ mtools
// mtools and dosfstools are optional test extras (README.md): without mtools
// the fragmented volumes cannot be built (run.mjs skips those checks), without
// dosfstools fsckVol() does not run fsck.fat.
const have = (t) => { try { execFileSync('which', [t], { stdio: 'ignore' }); return true; } catch { return false; } };
export const HAVE_MTOOLS = ['mmd', 'mcopy', 'mdel', 'mattrib'].every(have), HAVE_FSCK = have('fsck.fat');
const MENV = { ...process.env, MTOOLS_SKIP_CHECK: '1', MTOOLS_NO_VFAT: '1' };

export function rng(seed) {
  return () => { seed |= 0; seed = (seed + 0x6D2B79F5) | 0; let t = Math.imul(seed ^ (seed >>> 15), 1 | seed); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

/**
 * Run a script of mtools operations on a copy of img (in dir), return the new image.
 * ops: ['md', 'SUB'] ['put', 'SUB\\X.DAT', Buffer] ['del', 'X.DAT'] ['attr', 'X', '+h']
 */
export function mt(img, dir, ops, model) {
  fs.mkdirSync(dir, { recursive: true });
  const p = path.join(dir, 'work.img');
  fs.writeFileSync(p, img);
  const off = partOff(img);
  const I = `${p}@@${off}`;
  const u = (n) => '::/' + n.replace(/\\/g, '/');
  let k = 0;
  for (const op of ops) {
    const [what, name, arg] = op;
    if (what === 'md') execFileSync('mmd', ['-i', I, u(name)], { env: MENV });
    else if (what === 'put') {
      const h = path.join(dir, 'f' + (k++));
      fs.writeFileSync(h, arg);
      execFileSync('mcopy', ['-o', '-i', I, h, u(name)], { env: MENV });
      fs.rmSync(h);
      if (model) model.set(name, Buffer.from(arg));
    } else if (what === 'del') { execFileSync('mdel', ['-i', I, u(name)], { env: MENV }); if (model) model.delete(name); }
    else if (what === 'attr') execFileSync('mattrib', ['-i', I, arg, u(name)], { env: MENV });
  }
  const out = new Uint8Array(fs.readFileSync(p));
  fs.rmSync(p);
  return out;
}

/** fsck.fat -n on the volume: '' if clean (or if dosfstools is not installed), else the report */
export function fsckVol(img, dir, name) {
  if (!HAVE_FSCK) return '';
  const off = partOff(img);
  const p = path.join(dir, name + '.part');
  fs.writeFileSync(p, img.subarray(off));
  try { execFileSync('fsck.fat', ['-n', p], { stdio: 'pipe' }); return ''; }
  catch (e) { return (e.stdout?.toString() || '') + (e.stderr?.toString() || ''); }
  finally { fs.rmSync(p, { force: true }); }
}

/** every file of the model byte-identical when extracted with mtools; returns the mismatches */
export function verifyModel(img, dir, model) {
  const p = path.join(dir, 'verify.img');
  fs.writeFileSync(p, img);
  const off = partOff(img);
  const bad = [];
  for (const [name, want] of model) {
    let got;
    try { got = execFileSync('mcopy', ['-n', '-i', `${p}@@${off}`, '::/' + name.replace(/\\/g, '/'), '-'], { env: MENV, stdio: 'pipe', maxBuffer: 1 << 28 }); }
    catch { got = null; }
    if (!got || !Buffer.from(got).equals(want)) bad.push(name);
  }
  fs.rmSync(p);
  return bad;
}
