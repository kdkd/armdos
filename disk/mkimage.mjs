#!/usr/bin/env node
// mkimage.mjs - build and inspect ARM-DOS disk images (ARCH.md section 11).
// Node, no dependencies.
//
//   node disk/mkimage.mjs build MANIFEST.json [-o OUT.img] [--base DIR]
//   node disk/mkimage.mjs build --format fd1440|fd720|fd360|fd1200|fd2880|hd
//        [--size-mb N] [--heads H] [--spt S] [--dir TREE] [--boot FILE]
//        [--mbr FILE] [--label NAME] [--serial HEX] [--date "YYYY-MM-DD hh:mm:ss"]
//        [--mtime] -o OUT.img
//   node disk/mkimage.mjs info    IMG                 BPB / partition table
//   node disk/mkimage.mjs ls      IMG [PATH] [-R]     directory listing (DIR-like)
//   node disk/mkimage.mjs cat     IMG PATH            file contents to stdout
//   node disk/mkimage.mjs extract IMG [PATH] DEST     copy files out
//   node disk/mkimage.mjs check   IMG                 FAT consistency check
//   node disk/mkimage.mjs selftest                    check the embedded boot code
//
// Hard-disk images: `--partition N` (1-4) picks the partition for info/ls/cat/
// extract/check; by default the active one (or the first FAT one).
//
// See disk/README.md for the manifest format.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { execFileSync } from 'node:child_process';

const SECTOR = 512;

// disk/nosys.S assembled: "Non-System disk or disk error", key, INT 19h.
const NOSYS_CODE = hex(
  '28408fe20100d4e4000050e30300000a0e0c80e30710a0e3100000eff8ffffea0000a0e3160000ef190000effeffffea' +
  '0d0a4e6f6e2d53797374656d206469736b206f72206469736b206572726f720d0a5265706c61636520616e64207072' +
  '65737320616e79206b6579207768656e2072656164790d0a00');

// disk/mbr.S assembled (<= 440 bytes).
const MBR_BOOT = hex(
  'ff5003e21f0ba0e3061ca0e3022ca0e3043090e4043081e4042052e2fbffff1a060ca0e32c0080e210ff2fe170619fe5' +
  '0470a0e30080a0e30000d6e5800050e30300001a000058e33700001a0680a0e1010000ea000050e33300001a106086e2' +
  '017057e2f3ffff1a000058e30100001a180000ef340000ea0590a0e3020ba0e320119fe5001080e51f1ba0e3041080e5' +
  'b810d8e1ba20d8e1021881e1081080e50010a0e30c1080e50040a0e1420ca0e30530a0e1130000ef1000003a020ca0e3' +
  '010080e31f1ba0e30220d8e50330d8e5032482e10130d8e5033485e1130000ef0600003a0000a0e30530a0e1130000ef' +
  '019059e2e0ffff1a6c408fe20b0000eaa4009fe5b010d0e1a0209fe5020051e173408f120500001a0530a0e10840a0e1' +
  '1fdba0e31f0ba0e310ff2fe120408fe20100d4e4000050e30300000a0e0c80e30710a0e3100000eff8ffffea900f07ee' +
  'fdffffea496e76616c696420706172746974696f6e207461626c65004572726f72206c6f6164696e67206f7065726174' +
  '696e672073797374656d004d697373696e67206f7065726174696e672073797374656d00be07000010000100fe7d0000' +
  '55aa0000');

function hex(s) { return Buffer.from(s, 'hex'); }

// ------------------------------------------------------------ geometry ----
const FLOPPIES = {
  fd360:  { total: 720,  spt: 9,  heads: 2, spc: 2, root: 112, media: 0xFD, fat: 2 },
  fd720:  { total: 1440, spt: 9,  heads: 2, spc: 2, root: 112, media: 0xF9, fat: 3 },
  fd1200: { total: 2400, spt: 15, heads: 2, spc: 1, root: 224, media: 0xF9, fat: 7 },
  fd1440: { total: 2880, spt: 18, heads: 2, spc: 1, root: 224, media: 0xF0, fat: 9 },
  fd2880: { total: 5760, spt: 36, heads: 2, spc: 2, root: 240, media: 0xF0, fat: 9 },
};

/** Lay out a FAT volume of `total` sectors. */
function fatLayout({ total, spc, root, media, fatType, fat: fixedFat, nfats = 2, reserved = 1 }) {
  const rootSecs = Math.ceil(root * 32 / SECTOR);
  let fatSecs = fixedFat || 1;
  if (!fixedFat) {
    for (let i = 0; i < 20; i++) {
      const data = total - reserved - rootSecs - nfats * fatSecs;
      const clusters = Math.floor(data / spc);
      const bytes = fatType === 12 ? Math.ceil((clusters + 2) * 3 / 2) : (clusters + 2) * 2;
      const need = Math.ceil(bytes / SECTOR);
      if (need === fatSecs) break;
      fatSecs = need;
    }
  }
  const firstData = reserved + nfats * fatSecs + rootSecs;
  const clusters = Math.floor((total - firstData) / spc);
  const type = fatType || (clusters < 4085 ? 12 : 16);
  if (type === 12 && clusters >= 4085) throw new Error(`${clusters} clusters is too many for FAT12`);
  if (type === 16 && (clusters < 4085 || clusters > 65524)) throw new Error(`${clusters} clusters does not fit FAT16`);
  return { total, spc, root, media, nfats, reserved, fatSecs, rootSecs, firstRoot: reserved + nfats * fatSecs,
           firstData, clusters, type };
}

/** Pick FAT type and cluster size for a hard-disk partition (DOS 4 FORMAT style). */
function hdLayout(total) {
  if (total < 32680) return fatLayout({ total, spc: 8, root: 512, media: 0xF8, fatType: 12 });
  const mb = total * SECTOR / 1048576;
  const spc = mb <= 128 ? 4 : mb <= 256 ? 8 : mb <= 512 ? 16 : mb <= 1024 ? 32 : 64;
  return fatLayout({ total, spc, root: 512, media: 0xF8, fatType: 16 });
}

// ------------------------------------------------------------- names ------
const VALID = /^[A-Z0-9!#$%&'()\-@^_`{}~\x80-\xFF]+$/;
function to83(name, what) {
  const up = name.toUpperCase();
  if (up === '.' || up === '..') return up === '.' ? '.          ' : '..         ';
  const dot = up.lastIndexOf('.');
  const base = dot > 0 ? up.slice(0, dot) : up, ext = dot > 0 ? up.slice(dot + 1) : '';
  if (!base || base.length > 8 || ext.length > 3 || !VALID.test(base) || (ext && !VALID.test(ext)))
    throw new Error(`"${name}" (${what}) is not a valid 8.3 DOS name; give it a "dst" in the manifest`);
  return base.padEnd(8) + ext.padEnd(3);
}
const from83 = (raw) => {
  const b = raw.slice(0, 8).trimEnd(), e = raw.slice(8, 11).trimEnd();
  return e ? `${b}.${e}` : b;
};

// ----------------------------------------------------------- dates --------
const DEFAULT_DATE = '1988-06-17 12:00:00';
function parseDate(s) {
  const m = String(s).match(/^(\d{4})-(\d\d)-(\d\d)(?:[ T](\d\d):(\d\d)(?::(\d\d))?)?$/);
  if (!m) throw new Error(`bad date "${s}" (want YYYY-MM-DD hh:mm:ss)`);
  return { y: +m[1], mo: +m[2], d: +m[3], h: +(m[4] || 0), mi: +(m[5] || 0), s: +(m[6] || 0) };
}
function dateFromJs(d) {
  return { y: d.getFullYear(), mo: d.getMonth() + 1, d: d.getDate(), h: d.getHours(), mi: d.getMinutes(), s: d.getSeconds() };
}
function dosDate(t) {
  const y = Math.min(Math.max(t.y, 1980), 2107);
  return { date: ((y - 1980) << 9) | (t.mo << 5) | t.d, time: (t.h << 11) | (t.mi << 5) | (t.s >> 1) };
}
function fmtDosDate(date, time) {
  const y = 1980 + (date >> 9), mo = (date >> 5) & 15, d = date & 31;
  const h = time >> 11, mi = (time >> 5) & 63;
  return `${String(mo).padStart(2, '0')}-${String(d).padStart(2, '0')}-${String(y % 100).padStart(2, '0')}  ${String(h).padStart(2, ' ')}:${String(mi).padStart(2, '0')}`;
}

function parseAttr(a) {
  if (typeof a === 'number') return a;
  let v = 0;
  for (const c of String(a || '').toUpperCase()) {
    if (c === 'R') v |= 0x01; else if (c === 'H') v |= 0x02; else if (c === 'S') v |= 0x04; else if (c === 'A') v |= 0x20;
    else if (c !== ' ' && c !== '-') throw new Error(`bad attribute letter "${c}" (use R H S A)`);
  }
  return v;
}

// ------------------------------------------------------ the file tree -----
// node: { name83, host?, data?, dir: bool, children: [], attr, stamp, first }

function newDir(name83, stamp) { return { name83, dir: true, children: [], attr: 0x10, stamp }; }

function findChild(dir, name83) { return dir.children.find(c => c.name83 === name83); }

function ensureDir(root, parts, stamp) {
  let d = root;
  for (const p of parts) {
    const n = to83(p, 'directory');
    let c = findChild(d, n);
    if (!c) { c = newDir(n, stamp); d.children.push(c); }
    else if (!c.dir) throw new Error(`${p} is both a file and a directory`);
    d = c;
  }
  return d;
}

// Text files go onto a DOS disk with DOS line endings: a bare LF becomes CR LF.
// Decided by name (extension list, or LICENSE/README/COPYING-style names), and only
// if the data has no NUL bytes; CR LF already present is left alone. A manifest entry
// with "binary": true opts out.
const TEXT_EXT = new Set(['TXT', 'DOC', 'ME', '1ST', 'NFO', 'DIZ', 'BAT', 'SYS_TEXT', 'INI', 'CFG', 'MNU',
  'BAS', 'C', 'H', 'S', 'ASM', 'INC', 'MAK', 'LST', 'LOG', 'HLP_TEXT', 'MSG', 'FRM', 'ORD', 'ANS', 'ART']);
const TEXT_NAMES = /^(LICENSE|LICENCE|COPYING|README|READ\.?ME|CHANGES|AUTHORS|NEWS|INSTALL|NOTICE|HISTORY|CONFIG\.SYS|AUTOEXEC\.BAT)(\.|$)/i;
function toDosText(name, data) {
  const up = name.toUpperCase();
  const ext = up.includes('.') ? up.slice(up.lastIndexOf('.') + 1) : '';
  if (!(TEXT_EXT.has(ext) || TEXT_NAMES.test(up))) return data;
  if (data.includes(0)) return data;
  let bare = 0;
  for (let i = 0; i < data.length; i++) if (data[i] === 10 && (i === 0 || data[i - 1] !== 13)) bare++;
  if (!bare) return data;
  const out = Buffer.alloc(data.length + bare);
  for (let i = 0, j = 0; i < data.length; i++) {
    if (data[i] === 10 && (i === 0 || data[i - 1] !== 13)) out[j++] = 13;
    out[j++] = data[i];
  }
  return out;
}

function addFile(root, dstPath, data, opts) {
  const parts = dstPath.split(/[\\/]+/).filter(Boolean);
  const name = parts.pop();
  if (!opts.binary) data = toDosText(name, data);
  const dir = ensureDir(root, parts, opts.stamp);
  const n83 = to83(name, dstPath);
  const old = findChild(dir, n83);
  if (old) {
    if (old.dir) throw new Error(`${dstPath}: a directory with that name exists`);
    dir.children.splice(dir.children.indexOf(old), 1);        // later entries override (manifest files over tree)
  }
  dir.children.push({ name83: n83, dir: false, data, attr: opts.attr ?? 0x20, stamp: opts.stamp, first: opts.first, path: dstPath });
}

function addTree(root, hostDir, dstPrefix, cfg) {
  const walk = (h, dst) => {
    const ents = fs.readdirSync(h, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name));
    for (const e of ents) {
      if (e.name.startsWith('.') || e.name === 'Makefile' || /\.(md|mk)$/.test(e.name) && cfg.skipDocs) continue;
      const hp = path.join(h, e.name);
      const dp = dst ? `${dst}\\${e.name}` : e.name;
      const st = fs.statSync(hp);
      const stamp = cfg.stampFor(dp, st);
      if (st.isDirectory()) { ensureDir(root, dp.split('\\'), stamp); walk(hp, dp); }
      else if (st.isFile()) addFile(root, dp, fs.readFileSync(hp), { attr: cfg.attrFor(dp), stamp });
    }
  };
  walk(hostDir, dstPrefix);
}

// ----------------------------------------------------------- writer -------
class FatWriter {
  constructor(img, volOff, L) {
    this.img = img; this.off = volOff; this.L = L;
    this.fat = new Array(L.clusters + 2).fill(0);
    this.fat[0] = (L.type === 12 ? 0xF00 : 0xFF00) | L.media;
    this.fat[1] = L.type === 12 ? 0xFFF : 0xFFFF;
    this.next = 2;
  }
  get eoc() { return this.L.type === 12 ? 0xFFF : 0xFFFF; }
  clusterOff(c) { return this.off + (this.L.firstData + (c - 2) * this.L.spc) * SECTOR; }
  alloc(bytes) {
    const csize = this.L.spc * SECTOR;
    const n = Math.ceil(bytes / csize);
    if (!n) return 0;
    const first = this.next;
    if (first + n - 1 > this.L.clusters + 1) throw new Error(`disk full: need ${n} more clusters, ${this.L.clusters + 2 - first} left`);
    for (let i = 0; i < n; i++) this.fat[first + i] = i === n - 1 ? this.eoc : first + i + 1;
    this.next += n;
    return first;
  }
  write(c, data) { if (c) data.copy(this.img, this.clusterOff(c)); }
  flushFat() {
    const L = this.L, buf = Buffer.alloc(L.fatSecs * SECTOR);
    if (L.type === 12) {
      for (let i = 0; i < this.fat.length; i++) {
        const o = (i * 3) >> 1, v = this.fat[i];
        if (i & 1) { buf[o] = (buf[o] & 0x0F) | ((v & 0x0F) << 4); buf[o + 1] = v >> 4; }
        else { buf[o] = v & 0xFF; buf[o + 1] = (buf[o + 1] & 0xF0) | (v >> 8); }
      }
    } else for (let i = 0; i < this.fat.length; i++) buf.writeUInt16LE(this.fat[i], i * 2);
    for (let f = 0; f < L.nfats; f++) buf.copy(this.img, this.off + (L.reserved + f * L.fatSecs) * SECTOR);
  }
}

function dirEntry(name83, attr, stamp, cluster, size) {
  const e = Buffer.alloc(32);
  e.write(name83, 0, 11, 'latin1');
  e[11] = attr;
  const { date, time } = dosDate(stamp);
  e.writeUInt16LE(time, 22);
  e.writeUInt16LE(date, 24);
  e.writeUInt16LE(cluster, 26);
  e.writeUInt32LE(size >>> 0, 28);
  return e;
}

/** Order entries: "first" files by their number, [label if root], then the rest. */
function ordered(dir) {
  const firsts = dir.children.filter(c => c.first).sort((a, b) => a.first - b.first);
  const rest = dir.children.filter(c => !c.first);
  return { firsts, rest };
}

function writeVolume(img, volOff, L, root, bpbInfo, opt) {
  const w = new FatWriter(img, volOff, L);
  const csize = L.spc * SECTOR;
  const volStamp = opt.stamp;

  // allocation order: root "first" files (IO.SYS at cluster 2, contiguous),
  // then the rest of the tree, depth first, in directory order
  const { firsts: rootFirsts } = ordered(root);
  for (const f of rootFirsts) if (!f.dir) f.cluster = w.alloc(f.data.length);
  const assign = (dir, isRoot) => {
    const { firsts, rest } = ordered(dir);
    for (const c of [...firsts, ...rest]) {
      if (c.cluster !== undefined) continue;
      if (c.dir) {
        const nents = c.children.length + 2;
        c.cluster = w.alloc(Math.max(1, nents) * 32);
        c.dirBytes = Math.ceil(nents * 32 / csize) * csize;
      } else c.cluster = w.alloc(c.data.length);
    }
    for (const c of [...firsts, ...rest]) if (c.dir) assign(c, false);
  };
  assign(root, true);

  const emit = (dir, parentCluster, isRoot) => {
    const { firsts, rest } = ordered(dir);
    const ents = [];
    if (!isRoot) {
      ents.push(dirEntry('.          ', 0x10, dir.stamp, dir.cluster, 0));
      ents.push(dirEntry('..         ', 0x10, dir.stamp, parentCluster, 0));
    }
    for (const c of firsts) ents.push(dirEntry(c.name83, c.attr, c.stamp, c.cluster, c.dir ? 0 : c.data.length));
    if (isRoot && opt.label) ents.push(dirEntry(opt.label, 0x08, volStamp, 0, 0));
    for (const c of rest) ents.push(dirEntry(c.name83, c.dir ? 0x10 | (c.attr & 0x06) : c.attr, c.stamp, c.cluster, c.dir ? 0 : c.data.length));
    const buf = Buffer.concat(ents);
    if (isRoot) {
      if (ents.length > L.root) throw new Error(`root directory holds ${L.root} entries; ${ents.length} needed`);
      buf.copy(img, volOff + L.firstRoot * SECTOR);
    } else {
      const out = Buffer.alloc(dir.dirBytes);
      buf.copy(out);
      w.write(dir.cluster, out);
    }
    for (const c of [...firsts, ...rest]) {
      if (c.dir) emit(c, isRoot ? 0 : dir.cluster, false);
      else w.write(c.cluster, c.data);
    }
  };
  emit(root, 0, true);
  w.flushFat();

  // the boot sector
  const bs = img.subarray(volOff, volOff + SECTOR);
  if (opt.boot && opt.boot.length === SECTOR) {
    opt.boot.copy(bs);                                   // keep everything but the BPB and signature
  } else {
    bs.writeUInt32LE((0xEA000000 | ((0x40 - 8) >> 2)) >>> 0, 0);   // b 0x40
    const code = opt.boot || NOSYS_CODE;
    if (code.length > 0x1FE - 0x40) throw new Error(`boot code is ${code.length} bytes; at most ${0x1FE - 0x40} fit at 0x40-0x1FD`);
    code.copy(bs, 0x40);
  }
  if (!(opt.boot && opt.boot.length === SECTOR) || bs.subarray(4, 11).every(b => b === 0))
    bs.write(opt.oem.padEnd(7).slice(0, 7), 4, 7, 'latin1');        // OEM name in 0x04-0x0A (byte 3 is the branch)
  bs.writeUInt16LE(SECTOR, 0x0B);
  bs[0x0D] = L.spc;
  bs.writeUInt16LE(L.reserved, 0x0E);
  bs[0x10] = L.nfats;
  bs.writeUInt16LE(L.root, 0x11);
  bs.writeUInt16LE(L.total < 65536 ? L.total : 0, 0x13);
  bs[0x15] = L.media;
  bs.writeUInt16LE(L.fatSecs, 0x16);
  bs.writeUInt16LE(bpbInfo.spt, 0x18);
  bs.writeUInt16LE(bpbInfo.heads, 0x1A);
  bs.writeUInt32LE(bpbInfo.hidden, 0x1C);
  bs.writeUInt32LE(L.total < 65536 ? 0 : L.total, 0x20);
  bs[0x24] = bpbInfo.drive;
  bs[0x25] = 0;
  bs[0x26] = 0x29;
  bs.writeUInt32LE(opt.serial >>> 0, 0x27);
  bs.write((opt.label || 'NO NAME    ').padEnd(11), 0x2B, 11, 'latin1');
  bs.write(L.type === 12 ? 'FAT12   ' : 'FAT16   ', 0x36, 8, 'latin1');
  bs[0x1FE] = 0x55; bs[0x1FF] = 0xAA;
  return { usedClusters: w.next - 2 };
}

function chs(lba, heads, spt) {
  let c = Math.floor(lba / (heads * spt)), h = Math.floor(lba / spt) % heads, s = lba % spt + 1;
  if (c > 1023) { c = 1023; h = heads - 1; s = spt; }
  return [h, s | ((c >> 2) & 0xC0), c & 0xFF];
}

// ---------------------------------------------------------- manifest ------
function loadInput(spec, base, what) {
  if (!spec) return null;
  const o = typeof spec === 'string' ? { src: spec } : spec;
  const p = path.resolve(base, o.src);
  if (!fs.existsSync(p)) {
    if (o.optional) { console.error(`mkimage: note: ${what} ${o.src} not found, skipped`); return null; }
    throw new Error(`${what} ${p} not found`);
  }
  return fs.readFileSync(p);
}

function expandSrc(src, base) {
  const abs = path.resolve(base, src);
  const b = path.basename(abs);
  if (!/[*?]/.test(b)) return fs.existsSync(abs) ? [abs] : [];
  const dir = path.dirname(abs);
  if (!fs.existsSync(dir)) return [];
  const re = new RegExp('^' + b.replace(/[.+^${}()|[\]\\]/g, '\\$&').replace(/\*/g, '.*').replace(/\?/g, '.') + '$', 'i');
  return fs.readdirSync(dir).filter(n => re.test(n)).sort().map(n => path.join(dir, n)).filter(p => fs.statSync(p).isFile());
}

export function build(m, base) {
  const stampDefault = parseDate(m.date || DEFAULT_DATE);
  const stampOf = (spec, st) => spec ? parseDate(spec) : (m.useMtime && st ? dateFromJs(st.mtime) : stampDefault);
  const attrs = Object.fromEntries(Object.entries(m.attrs || {}).map(([k, v]) => [k.toUpperCase().replace(/\//g, '\\'), parseAttr(v)]));
  const root = newDir('', stampDefault);

  // trees
  const trees = m.tree ? (Array.isArray(m.tree) ? m.tree : [m.tree]) : [];
  for (const t of trees) {
    const o = typeof t === 'string' ? { src: t } : t;
    const h = path.resolve(base, o.src);
    if (!fs.existsSync(h)) { if (o.optional) continue; throw new Error(`tree ${h} not found`); }
    addTree(root, h, (o.dst || '').replace(/^[\\/]+|[\\/]+$/g, '').replace(/\//g, '\\'), {
      skipDocs: o.skipDocs !== false,
      stampFor: (dp, st) => stampOf(null, st),
      attrFor: (dp) => attrs[dp.toUpperCase()] ?? 0x20,
    });
  }
  // empty directories
  for (const d of m.dirs || []) ensureDir(root, d.split(/[\\/]+/).filter(Boolean), stampDefault);
  // explicit files
  for (const f of m.files || []) {
    const excl = (f.exclude || []).map(x => x.toUpperCase());
    const srcs = expandSrc(f.src, base).filter(p => !excl.includes(path.basename(p).toUpperCase()));
    if (!srcs.length) {
      if (f.optional) { console.error(`mkimage: note: ${f.src} not found, skipped`); continue; }
      throw new Error(`file ${path.resolve(base, f.src)} not found`);
    }
    for (const s of srcs) {
      // dst: a file name, or a directory ("DOS\\" / any dst of a wildcard src)
      let dst;
      if (!f.dst) dst = path.basename(s);
      else if (/[\\/]$/.test(f.dst) || /[*?]/.test(path.basename(f.src))) dst = f.dst.replace(/[\\/]+$/, '') + '\\' + path.basename(s);
      else dst = f.dst;
      dst = dst.replace(/\//g, '\\').replace(/^\\+/, '');
      const st = fs.statSync(s);
      addFile(root, dst, fs.readFileSync(s), {
        attr: f.attr !== undefined ? parseAttr(f.attr) : attrs[dst.toUpperCase()] ?? 0x20,
        stamp: stampOf(f.date, st), first: f.first ? Number(f.first) : 0, binary: !!f.binary,
      });
    }
  }
  // system files must be the first root entries, and IO.SYS contiguous from cluster 2
  const firsts = root.children.filter(c => c.first);
  if (firsts.some(c => c.dir)) throw new Error('"first" only applies to files');

  const label = m.label ? String(m.label).toUpperCase().slice(0, 11).padEnd(11) : null;
  const serial = m.serial !== undefined ? (typeof m.serial === 'number' ? m.serial : parseInt(String(m.serial).replace(/-/g, ''), 16))
                                        : parseInt(`${stampDefault.y}${String(stampDefault.mo).padStart(2, '0')}${String(stampDefault.d).padStart(2, '0')}`, 16) >>> 0;  // 1988-0617
  const opt = { label, serial, oem: m.oem || 'ARMDOS4', stamp: stampDefault,
                boot: loadInput(m.boot, base, 'boot sector') };
  const fmt = m.format || 'fd1440';

  if (FLOPPIES[fmt]) {
    const g = FLOPPIES[fmt];
    const L = fatLayout({ ...g, fatType: 12 });
    const img = Buffer.alloc(g.total * SECTOR);
    const r = writeVolume(img, 0, L, root, { spt: g.spt, heads: g.heads, hidden: 0, drive: 0x00 }, opt);
    return { img, L, used: r.usedClusters };
  }
  if (fmt !== 'hd') throw new Error(`unknown format "${fmt}" (fd360 fd720 fd1200 fd1440 fd2880 hd)`);

  const heads = m.heads || 16, spt = m.sectorsPerTrack || m.spt || 63;
  const cyls = m.cylinders || Math.ceil((m.sizeMB || 32) * 1048576 / (heads * spt * SECTOR));
  if (heads > 255 || spt > 63 || cyls > 1024) throw new Error(`geometry ${cyls}/${heads}/${spt} exceeds CHS limits (1024/255/63)`);
  const totalDisk = cyls * heads * spt;
  const start = spt;                                          // cylinder 0, head 1, sector 1
  const psize = totalDisk - start;
  const L = hdLayout(psize);
  const img = Buffer.alloc(totalDisk * SECTOR);
  const r = writeVolume(img, start * SECTOR, L, root, { spt, heads, hidden: start, drive: 0x80 }, opt);

  // MBR
  const mbrCode = loadInput(m.mbr, base, 'MBR') || MBR_BOOT;
  if (mbrCode.length === SECTOR) mbrCode.copy(img, 0, 0, 0x1BE);
  else {
    if (mbrCode.length > 440) throw new Error(`MBR code is ${mbrCode.length} bytes; at most 440 fit`);
    mbrCode.copy(img, 0);
  }
  const type = L.type === 12 ? 0x01 : psize < 65536 ? 0x04 : 0x06;
  const pe = 0x1BE;
  img[pe] = 0x80;
  Buffer.from(chs(start, heads, spt)).copy(img, pe + 1);
  img[pe + 4] = type;
  Buffer.from(chs(totalDisk - 1, heads, spt)).copy(img, pe + 5);
  img.writeUInt32LE(start, pe + 8);
  img.writeUInt32LE(psize, pe + 12);
  img[0x1FE] = 0x55; img[0x1FF] = 0xAA;
  return { img, L, used: r.usedClusters, geometry: { cyls, heads, spt }, type };
}



// ----------------------------------------------------------- reader -------
class FatReader {
  constructor(img, partition) {
    this.img = img;
    let off = 0;
    const bs0 = img.subarray(0, SECTOR);
    const plausible = (b) => b.readUInt16LE(0x0B) === SECTOR && [1, 2, 4, 8, 16, 32, 64, 128].includes(b[0x0D]) &&
      b[0x10] >= 1 && b[0x10] <= 2 && b.readUInt16LE(0x0E) >= 1 && b[0x15] >= 0xF0;
    this.parts = [];
    if (bs0[0x1FE] === 0x55 && bs0[0x1FF] === 0xAA && !plausible(bs0)) {
      for (let i = 0; i < 4; i++) {
        const e = 0x1BE + 16 * i;
        this.parts.push({ n: i + 1, active: bs0[e], type: bs0[e + 4], start: bs0.readUInt32LE(e + 8), size: bs0.readUInt32LE(e + 12),
                          chsStart: [...bs0.subarray(e + 1, e + 4)], chsEnd: [...bs0.subarray(e + 5, e + 8)] });
      }
      const pick = partition ? this.parts[partition - 1] :
        this.parts.find(p => p.active === 0x80 && p.type) || this.parts.find(p => [1, 4, 6, 0x0E].includes(p.type));
      if (!pick || !pick.type) throw new Error('no FAT partition found');
      this.part = pick;
      off = pick.start * SECTOR;
    } else if (!plausible(bs0)) throw new Error('no BPB and no partition table: not a DOS disk image');
    this.off = off;
    const b = img.subarray(off, off + SECTOR);
    this.bs = b;
    const total = b.readUInt16LE(0x13) || b.readUInt32LE(0x20);
    const L = { spc: b[0x0D], reserved: b.readUInt16LE(0x0E), nfats: b[0x10], root: b.readUInt16LE(0x11), total,
                media: b[0x15], fatSecs: b.readUInt16LE(0x16), spt: b.readUInt16LE(0x18), heads: b.readUInt16LE(0x1A),
                hidden: b.readUInt32LE(0x1C) };
    L.rootSecs = Math.ceil(L.root * 32 / SECTOR);
    L.firstRoot = L.reserved + L.nfats * L.fatSecs;
    L.firstData = L.firstRoot + L.rootSecs;
    L.clusters = Math.floor((total - L.firstData) / L.spc);
    L.type = L.clusters < 4085 ? 12 : 16;
    this.L = L;
  }
  fatEntry(c, fatNo = 0) {
    const base = this.off + (this.L.reserved + fatNo * this.L.fatSecs) * SECTOR;
    if (this.L.type === 12) {
      const o = base + ((c * 3) >> 1), v = this.img.readUInt16LE(o);
      return c & 1 ? v >> 4 : v & 0xFFF;
    }
    return this.img.readUInt16LE(base + c * 2);
  }
  isEoc(v) { return this.L.type === 12 ? v >= 0xFF8 : v >= 0xFFF8; }
  chain(c) {
    const out = [], seen = new Set();
    while (c >= 2 && !this.isEoc(c)) {
      if (seen.has(c) || c > this.L.clusters + 1) throw new Error(`bad cluster chain at ${c}`);
      seen.add(c); out.push(c);
      c = this.fatEntry(c);
    }
    return out;
  }
  readChain(c, size) {
    const cs = this.L.spc * SECTOR;
    const parts = this.chain(c).map(k => this.img.subarray(this.off + (this.L.firstData + (k - 2) * this.L.spc) * SECTOR, this.off + (this.L.firstData + (k - 2) * this.L.spc) * SECTOR + cs));
    const all = Buffer.concat(parts);
    return size === undefined ? all : all.subarray(0, size);
  }
  readDir(ent) {
    const raw = ent ? this.readChain(ent.cluster) :
      this.img.subarray(this.off + this.L.firstRoot * SECTOR, this.off + (this.L.firstRoot + this.L.rootSecs) * SECTOR);
    const out = [];
    for (let o = 0; o + 32 <= raw.length; o += 32) {
      if (raw[o] === 0) break;
      if (raw[o] === 0xE5) continue;
      const name83 = raw.toString('latin1', o, o + 11);
      out.push({ name83, name: from83(name83), attr: raw[o + 11], time: raw.readUInt16LE(o + 22), date: raw.readUInt16LE(o + 24),
                 cluster: raw.readUInt16LE(o + 26), size: raw.readUInt32LE(o + 28), index: o / 32 });
    }
    return out;
  }
  lookup(p) {
    const parts = String(p || '').split(/[\\/]+/).filter(Boolean);
    let ent = null;
    for (const part of parts) {
      if (ent && !(ent.attr & 0x10)) throw new Error(`${p}: not a directory`);
      const want = part.toUpperCase();
      const found = this.readDir(ent).find(e => !(e.attr & 0x08) && e.name === want);
      if (!found) throw new Error(`${p}: not found`);
      ent = found;
    }
    return ent;
  }
  readFile(ent) { return this.readChain(ent.cluster, ent.size); }
}

// ------------------------------------------------------------- commands ----
function cmdInfo(img, part) {
  const r = new FatReader(img, part);
  const L = r.L, b = r.bs;
  if (r.parts.length) {
    console.log('MBR partition table:');
    const c = ([h, s, cl]) => `${cl | ((s & 0xC0) << 2)}/${h}/${s & 63}`;
    for (const p of r.parts) if (p.type) console.log(`  ${p.n}: ${p.active === 0x80 ? 'active' : '      '} type ${p.type.toString(16).padStart(2, '0')}h  LBA ${p.start} + ${p.size} sectors (${(p.size * SECTOR / 1048576).toFixed(1)} MB)  C/H/S ${c(p.chsStart)} .. ${c(p.chsEnd)}`);
  }
  const branch = b.readUInt32LE(0);
  const isB = (branch >>> 24) === 0xEA;
  console.log(`boot sector at ${r.off}: ${isB ? `ARM "b 0x${(((branch & 0xFFFFFF) << 2) + 8).toString(16)}"` : 'first word ' + branch.toString(16)}, OEM "${b.toString('latin1', 3, 11).replace(/[^\x20-\x7e]/g, '.')}"`);
  console.log(`  ${L.type === 12 ? 'FAT12' : 'FAT16'}, ${L.total} sectors, ${L.spc} sector(s)/cluster, ${L.clusters} clusters, media ${L.media.toString(16)}h`);
  console.log(`  reserved ${L.reserved}, ${L.nfats} FATs x ${L.fatSecs} sectors, root ${L.root} entries, data from sector ${L.firstData}`);
  console.log(`  geometry ${L.heads} heads x ${L.spt} sectors, hidden ${L.hidden}, drive ${b[0x24].toString(16)}h`);
  if (b[0x26] === 0x29) console.log(`  serial ${b.readUInt32LE(0x27).toString(16).toUpperCase().padStart(8, '0').replace(/(....)(....)/, '$1-$2')}, label "${b.toString('latin1', 0x2B, 0x36)}", type "${b.toString('latin1', 0x36, 0x3E)}"`);
  let free = 0;
  for (let c = 2; c < L.clusters + 2; c++) if (r.fatEntry(c) === 0) free++;
  console.log(`  ${free * L.spc * SECTOR} bytes free`);
}

function attrStr(a) { return (a & 0x20 ? 'A' : '-') + (a & 0x04 ? 'S' : '-') + (a & 0x02 ? 'H' : '-') + (a & 0x01 ? 'R' : '-'); }

function cmdLs(img, p, part, recursive) {
  const r = new FatReader(img, part);
  const list = (ent, label) => {
    const ents = r.readDir(ent);
    const vol = !ent && ents.find(e => e.attr & 0x08);
    if (vol) console.log(` Volume in drive is ${vol.name83.trimEnd()}`);
    console.log(` Directory of ${label}\n`);
    let n = 0, bytes = 0;
    for (const e of ents) {
      if (e.attr & 0x08) continue;
      const [b, x] = [e.name83.slice(0, 8), e.name83.slice(8)];
      console.log(`${b} ${x}  ${e.attr & 0x10 ? '<DIR>    ' : String(e.size).padStart(9)}  ${fmtDosDate(e.date, e.time)}  ${attrStr(e.attr)}  cl ${e.cluster}`);
      n++; bytes += e.size;
    }
    console.log(`${String(n).padStart(9)} File(s) ${bytes} bytes\n`);
    if (recursive) for (const e of ents) if ((e.attr & 0x10) && !e.name.startsWith('.')) list(e, `${label.replace(/\\$/, '')}\\${e.name}`);
  };
  const ent = r.lookup(p);
  if (ent && !(ent.attr & 0x10)) { console.log(`${ent.name}  ${ent.size}  ${fmtDosDate(ent.date, ent.time)}  ${attrStr(ent.attr)}  cl ${ent.cluster}`); return; }
  list(ent, '\\' + String(p || '').replace(/^[\\/]+/, '').replace(/\//g, '\\'));
}

function cmdExtract(img, p, dest, part) {
  const r = new FatReader(img, part);
  const out = (ent, d) => {
    if (ent && !(ent.attr & 0x10)) { fs.mkdirSync(path.dirname(d), { recursive: true }); fs.writeFileSync(d, r.readFile(ent)); return; }
    fs.mkdirSync(d, { recursive: true });
    for (const e of r.readDir(ent)) if (!(e.attr & 0x08) && e.name !== '.' && e.name !== '..') out(e, path.join(d, e.name));
  };
  const ent = r.lookup(p);
  out(ent, ent && !(ent.attr & 0x10) ? path.join(dest, ent.name) : dest);
}

function cmdCheck(img, part) {
  const r = new FatReader(img, part);
  const L = r.L, owner = new Map(), problems = [];
  for (let f = 1; f < L.nfats; f++)
    for (let c = 0; c < L.clusters + 2; c++)
      if (r.fatEntry(c, f) !== r.fatEntry(c, 0)) { problems.push(`FAT copies differ at cluster ${c}`); break; }
  if ((r.fatEntry(0) & 0xFF) !== L.media) problems.push(`FAT[0] ${r.fatEntry(0).toString(16)} does not match media ${L.media.toString(16)}`);
  let files = 0, dirs = 0;
  const walk = (ent, where) => {
    for (const e of r.readDir(ent)) {
      if (e.attr & 0x08) continue;
      if (e.name === '.' || e.name === '..') {
        const want = e.name === '.' ? ent.cluster : where.parentCluster;
        if (e.cluster !== want) problems.push(`${where.path}\\${e.name} points to ${e.cluster}, expected ${want}`);
        continue;
      }
      const p = `${where.path}\\${e.name}`;
      let ch = [];
      try { ch = e.cluster ? r.chain(e.cluster) : []; } catch (x) { problems.push(`${p}: ${x.message}`); }
      for (const c of ch) { if (owner.has(c)) problems.push(`${p} and ${owner.get(c)} share cluster ${c}`); owner.set(c, p); }
      if (e.attr & 0x10) { dirs++; walk(e, { path: p, parentCluster: ent ? ent.cluster : 0 }); }
      else {
        files++;
        const cs = L.spc * SECTOR;
        if (Math.ceil(e.size / cs) !== ch.length) problems.push(`${p}: size ${e.size} needs ${Math.ceil(e.size / cs)} clusters, chain has ${ch.length}`);
      }
    }
  };
  walk(null, { path: '', parentCluster: 0 });
  let lost = 0;
  for (let c = 2; c < L.clusters + 2; c++) if (r.fatEntry(c) !== 0 && !owner.has(c)) lost++;
  if (lost) problems.push(`${lost} lost cluster(s)`);
  // DOS 4 system-disk rules, if IO.SYS is present
  const root = r.readDir(null).filter(e => !(e.attr & 0x08));
  const io = root.find(e => e.name === 'IO.SYS');
  if (io) {
    if (root[0] !== io || root[1]?.name !== 'ARMDOS.SYS') problems.push('IO.SYS and ARMDOS.SYS are not the first two root entries');
    const ch = r.chain(io.cluster);
    if (io.cluster !== 2 || ch.some((c, i) => c !== 2 + i)) problems.push('IO.SYS is not contiguous from cluster 2');
  }
  for (const p of problems) console.log(`  ${p}`);
  console.log(`${files} files, ${dirs} directories, ${problems.length ? problems.length + ' problem(s)' : 'no problems'}`);
  return problems.length ? 1 : 0;
}

function assemble(src) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'mkimage-'));
  try {
    execFileSync('arm-none-eabi-gcc', ['-marm', '-march=armv5te', '-c', src, '-o', path.join(tmp, 'a.o')]);
    execFileSync('arm-none-eabi-objcopy', ['-O', 'binary', path.join(tmp, 'a.o'), path.join(tmp, 'a.bin')]);
    return fs.readFileSync(path.join(tmp, 'a.bin'));
  } finally { fs.rmSync(tmp, { recursive: true, force: true }); }
}

function cmdSelftest() {
  const here = path.dirname(new URL(import.meta.url).pathname);
  let bad = 0;
  for (const [name, src, have] of [['nosys', 'nosys.S', NOSYS_CODE], ['mbr', 'mbr.S', MBR_BOOT]]) {
    const b = assemble(path.join(here, src));
    const same = b.equals(have);
    console.log(`${same ? 'ok  ' : 'FAIL'} embedded ${name} code (${have.length} bytes) == assembled disk/${src} (${b.length} bytes)`);
    if (!same) { bad++; console.log(`     assembled: ${b.toString('hex')}`); }
  }
  if (MBR_BOOT.length > 440) { console.log('FAIL MBR code longer than 440 bytes'); bad++; }
  return bad ? 1 : 0;
}

// ---------------------------------------------------------------- CLI ------
function main(argv) {
  const cmd = argv.shift();
  const flags = {}, pos = [];
  const valued = new Set(['-o', '--base', '--format', '--size-mb', '--heads', '--spt', '--cylinders', '--dir', '--boot', '--mbr',
                          '--label', '--serial', '--date', '--oem', '--partition']);
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (valued.has(a)) { if (i + 1 >= argv.length) throw new Error(`${a} needs a value`); flags[a] = argv[++i]; }
    else if (a.startsWith('-') && a.length > 1) flags[a] = true;
    else pos.push(a);
  }
  const part = flags['--partition'] ? Number(flags['--partition']) : undefined;
  switch (cmd) {
    case 'build': {
      let m, base;
      if (pos[0]) {
        const mf = path.resolve(pos[0]);
        m = JSON.parse(fs.readFileSync(mf, 'utf8'));
        base = path.resolve(path.dirname(mf), flags['--base'] || m.base || '.');
      } else { m = {}; base = process.cwd(); }
      const map = { '--format': 'format', '--size-mb': 'sizeMB', '--heads': 'heads', '--spt': 'sectorsPerTrack',
                    '--cylinders': 'cylinders', '--dir': 'tree', '--boot': 'boot', '--mbr': 'mbr', '--label': 'label',
                    '--serial': 'serial', '--date': 'date', '--oem': 'oem' };
      for (const [f, k] of Object.entries(map)) if (flags[f] !== undefined) m[k] = /^(sizeMB|heads|sectorsPerTrack|cylinders)$/.test(k) ? Number(flags[f]) : flags[f];
      if (flags['--dir']) m.tree = path.resolve(flags['--dir']);
      if (flags['--boot']) m.boot = path.resolve(flags['--boot']);
      if (flags['--mbr']) m.mbr = path.resolve(flags['--mbr']);
      if (flags['--mtime']) m.useMtime = true;
      const out = flags['-o'] || m.output && path.resolve(base, m.output);
      if (!out) throw new Error('no output file (-o or "output" in the manifest)');
      const r = build(m, base);
      fs.mkdirSync(path.dirname(path.resolve(out)), { recursive: true });
      fs.writeFileSync(out, r.img);
      const L = r.L;
      console.error(`mkimage: ${out}: ${r.img.length} bytes, FAT${L.type}, ${L.clusters} clusters of ${L.spc * SECTOR} bytes, ${r.used} used` +
                    (r.geometry ? `; CHS ${r.geometry.cyls}/${r.geometry.heads}/${r.geometry.spt}, partition type ${r.type.toString(16).padStart(2, '0')}h at LBA ${r.geometry.spt}` : ''));
      return 0;
    }
    case 'info': cmdInfo(fs.readFileSync(pos[0]), part); return 0;
    case 'ls': cmdLs(fs.readFileSync(pos[0]), pos[1], part, flags['-R']); return 0;
    case 'cat': {
      const r = new FatReader(fs.readFileSync(pos[0]), part);
      const e = r.lookup(pos[1]);
      if (!e || (e.attr & 0x10)) throw new Error(`${pos[1]}: is a directory`);
      process.stdout.write(r.readFile(e));
      return 0;
    }
    case 'extract': {
      const dest = pos.length >= 3 ? pos[2] : pos[1];
      const p = pos.length >= 3 ? pos[1] : '';
      if (!dest) throw new Error('usage: extract IMG [PATH] DEST');
      cmdExtract(fs.readFileSync(pos[0]), p, dest, part);
      return 0;
    }
    case 'check': return cmdCheck(fs.readFileSync(pos[0]), part);
    case 'selftest': return cmdSelftest();
    default:
      console.log(fs.readFileSync(new URL(import.meta.url), 'utf8').split('\n').filter(l => l.startsWith('//')).slice(0, 22).map(l => l.slice(3)).join('\n'));
      return cmd ? 2 : 0;
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(new URL(import.meta.url).pathname)) {
  try { process.exitCode = main(process.argv.slice(2)); }
  catch (e) { console.error(`mkimage: ${e.message}`); process.exitCode = 1; }
}

export { FatReader, fatLayout, hdLayout, FLOPPIES, NOSYS_CODE, MBR_BOOT };
