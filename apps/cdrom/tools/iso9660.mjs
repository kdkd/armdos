// apps/cdrom/tools/iso9660.mjs - a small, deterministic ISO 9660 (Level 1) builder and reader.
//
//   import { buildIso, IsoReader } from './iso9660.mjs';
//   const img = buildIso({
//     volumeId: 'SAMPLER93', date: '1993-06-17 12:00:00',
//     files: [ { path: 'README.TXT', data: Uint8Array | string } , { path: 'DEMO\\DEMO.EXE', src: '/abs/path' } ],
//     dirs: [ 'EMPTY' ],                        // extra (possibly empty) directories
//     copyrightFile: 'COPYRGHT.TXT', abstractFile: 'ABSTRACT.TXT', biblioFile: 'BIBLIO.TXT',
//   });
//   const r = new IsoReader(img); r.pvd; r.list('\\DEMO'); r.readFile('DEMO\\DEMO.EXE');
//
// What it writes (ECMA-119): 16 zero sectors of system area, the primary volume
// descriptor (sector 16), the volume descriptor set terminator (17), the type-L path
// table, the type-M path table, every directory (breadth-first, path-table order),
// then the files in directory order, each contiguous. Level 1: names are 8.3 upper-case
// d-characters (A-Z 0-9 _) with ";1", directories 8 characters without extension,
// at most 8 levels deep. Directory records never cross a sector boundary. Every date
// is the one given (default 1993-06-17 12:00:00, GMT offset 0) unless a file has its own.
// Node only for `src` (fs); `data` works anywhere.

const SECTOR = 2048;
const DCHARS = /^[A-Z0-9_]+$/;

function parseDate(s) {
  const m = /^(\d{4})-(\d\d)-(\d\d)(?:[ T](\d\d):(\d\d)(?::(\d\d))?)?$/.exec(s || '1993-06-17 12:00:00');
  if (!m) throw new Error(`bad date ${s}`);
  return { y: +m[1], mo: +m[2], d: +m[3], h: +(m[4] || 0), mi: +(m[5] || 0), s: +(m[6] || 0) };
}
const date7 = (t) => [t.y - 1900, t.mo, t.d, t.h, t.mi, t.s, 0];
const p2 = (n) => String(n).padStart(2, '0');
const date17 = (t) => {
  const s = `${t.y}${p2(t.mo)}${p2(t.d)}${p2(t.h)}${p2(t.mi)}${p2(t.s)}00`;
  return [...s].map((c) => c.charCodeAt(0)).concat([0]);
};
const NODATE17 = [...'0000000000000000'].map((c) => c.charCodeAt(0)).concat([0]);

function checkName(name, isDir) {
  if (isDir) {
    if (!DCHARS.test(name) || name.length > 8) throw new Error(`ISO 9660 level 1: bad directory name "${name}"`);
    return;
  }
  const [b, e, extra] = name.split('.');
  if (extra !== undefined || !b || !DCHARS.test(b) || b.length > 8 || (e !== undefined && e !== '' && (!DCHARS.test(e) || e.length > 3)))
    throw new Error(`ISO 9660 level 1: bad file name "${name}"`);
}
const splitName = (n) => { const i = n.indexOf('.'); return i < 0 ? [n, ''] : [n.slice(0, i), n.slice(i + 1)]; };
function cmpNames(a, b) {                      // ECMA-119 9.3: name, then extension, padded with blanks
  const [an, ae] = splitName(a), [bn, be] = splitName(b);
  const pad = (s, n) => s.padEnd(n, ' ');
  const L = Math.max(an.length, bn.length);
  if (pad(an, L) !== pad(bn, L)) return pad(an, L) < pad(bn, L) ? -1 : 1;
  const E = Math.max(ae.length, be.length, 1);
  if (pad(ae, E) !== pad(be, E)) return pad(ae, E) < pad(be, E) ? -1 : 1;
  return 0;
}

const both16 = (v) => [v & 255, (v >> 8) & 255, (v >> 8) & 255, v & 255];
const both32 = (v) => {
  const le = [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255];
  return le.concat([...le].reverse());
};
const strA = (s, n) => { const a = new Array(n).fill(0x20); for (let i = 0; i < Math.min(n, s.length); i++) a[i] = s.charCodeAt(i) & 0x7F; return a; };

function dirRecord(name, extent, size, flags, t) {
  const id = typeof name === 'number' ? [name] : [...name].map((c) => c.charCodeAt(0));
  let len = 33 + id.length;
  if (len & 1) len++;
  const r = new Array(len).fill(0);
  r[0] = len; r[1] = 0;
  r.splice(2, 8, ...both32(extent));
  r.splice(10, 8, ...both32(size));
  r.splice(18, 7, ...date7(t));
  r[25] = flags; r[26] = 0; r[27] = 0;
  r.splice(28, 4, ...both16(1));
  r[32] = id.length;
  r.splice(33, id.length, ...id);
  return r.slice(0, len);
}

export function buildIso(opts) {
  const baseDate = parseDate(opts.date);
  let fs = null;
  const load = (f) => {
    if (f.data !== undefined) return typeof f.data === 'string' ? new TextEncoder().encode(f.data) : new Uint8Array(f.data);
    if (!fs) throw new Error('src needs node:fs (pass opts.fs)');
    return new Uint8Array(fs.readFileSync(f.src));
  };
  fs = opts.fs || null;

  // ---- the tree
  const mkdir = (name, parent) => ({ name, parent, dirs: new Map(), files: new Map(), depth: parent ? parent.depth + 1 : 1, date: baseDate });
  const root = mkdir('', null);
  const ensureDir = (parts) => {
    let d = root;
    for (const p of parts) {
      const n = p.toUpperCase();
      checkName(n, true);
      if (!d.dirs.has(n)) {
        const nd = mkdir(n, d);
        if (nd.depth > 8) throw new Error(`ISO 9660: directory ${parts.join('\\')} is deeper than 8 levels`);
        d.dirs.set(n, nd);
      }
      d = d.dirs.get(n);
    }
    return d;
  };
  for (const p of opts.dirs || []) ensureDir(p.split(/[\\/]/).filter(Boolean));
  for (const f of opts.files) {
    const parts = f.path.split(/[\\/]/).filter(Boolean);
    const name = parts.pop().toUpperCase();
    checkName(name, false);
    const d = ensureDir(parts);
    if (d.files.has(name)) throw new Error(`ISO 9660: duplicate file ${f.path}`);
    if (d.dirs.has(name)) throw new Error(`ISO 9660: ${f.path} is a directory`);
    d.files.set(name, { name, data: load(f), date: f.date ? parseDate(f.date) : baseDate });
  }

  // ---- directories in path-table order (breadth-first; parents numbered first, then by name)
  const order = [root];
  root.num = 1;
  for (let i = 0; i < order.length; i++) {
    const d = order[i];
    const kids = [...d.dirs.values()].sort((a, b) => cmpNames(a.name, b.name));
    for (const k of kids) { order.push(k); }
  }
  // the loop above appends children of each directory in turn, which is exactly
  // "by level, then by parent number, then by name" since parents come in number order
  order.forEach((d, i) => { d.num = i + 1; });

  // ---- directory entries (records) and sizes
  const entriesOf = (d) => {
    const names = [...[...d.dirs.values()].map((x) => ({ name: x.name, dir: x })), ...[...d.files.values()].map((x) => ({ name: x.name, file: x }))];
    names.sort((a, b) => cmpNames(a.name, b.name));
    return names;
  };
  const recLen = (idLen) => { let l = 33 + idLen; return l + (l & 1); };
  const fileId = (n) => (n.includes('.') ? n : n + '.') + ';1';
  for (const d of order) {
    d.entries = entriesOf(d);
    let off = 0, sectors = 1;
    const place = (len) => { if (off + len > SECTOR) { sectors++; off = 0; } off += len; };
    place(34); place(34);
    for (const e of d.entries) place(recLen(e.dir ? e.name.length : fileId(e.name).length));
    d.size = sectors * SECTOR;
  }

  // ---- path tables
  const ptRecord = (d, big) => {
    const id = d === root ? [0] : [...d.name].map((c) => c.charCodeAt(0));
    const r = [id.length, 0];
    const ext = d.extent, par = d === root ? 1 : d.parent.num;
    r.push(...(big ? [(ext >>> 24) & 255, (ext >>> 16) & 255, (ext >>> 8) & 255, ext & 255] : [ext & 255, (ext >>> 8) & 255, (ext >>> 16) & 255, (ext >>> 24) & 255]));
    r.push(...(big ? [(par >> 8) & 255, par & 255] : [par & 255, (par >> 8) & 255]));
    r.push(...id);
    if (id.length & 1) r.push(0);
    return r;
  };
  const ptSize = order.reduce((s, d) => s + 8 + (d === root ? 1 : d.name.length) + ((d === root ? 1 : d.name.length) & 1), 0);
  const ptSectors = Math.ceil(ptSize / SECTOR);

  // ---- layout
  let lba = 18;
  const ptL = lba; lba += ptSectors;
  const ptM = lba; lba += ptSectors;
  for (const d of order) { d.extent = lba; lba += d.size / SECTOR; }
  const fileOrder = [];
  for (const d of order) for (const e of d.entries) if (e.file) fileOrder.push(e.file);
  for (const f of fileOrder) {
    if (f.data.length === 0) { f.extent = 0; continue; }
    f.extent = lba; lba += Math.ceil(f.data.length / SECTOR);
  }
  const total = lba;
  const img = new Uint8Array(total * SECTOR);

  // ---- directories
  for (const d of order) {
    let pos = d.extent * SECTOR, off = 0;
    const put = (rec) => {
      if (off + rec.length > SECTOR) { pos += SECTOR - off; off = 0; }
      img.set(rec, pos); pos += rec.length; off += rec.length;
    };
    put(dirRecord(0, d.extent, d.size, 2, d.date));
    const p = d.parent || d;
    put(dirRecord(1, p.extent, p.size, 2, p.date));
    for (const e of d.entries) {
      if (e.dir) put(dirRecord(e.name, e.dir.extent, e.dir.size, 2, e.dir.date));
      else put(dirRecord(fileId(e.name), e.file.extent, e.file.data.length, 0, e.file.date));
    }
  }
  // ---- files
  for (const f of fileOrder) if (f.data.length) img.set(f.data, f.extent * SECTOR);
  // ---- path tables
  let l = ptL * SECTOR, m = ptM * SECTOR;
  for (const d of order) { const a = ptRecord(d, false), b = ptRecord(d, true); img.set(a, l); l += a.length; img.set(b, m); m += b.length; }

  // ---- primary volume descriptor
  const pvd = new Array(SECTOR).fill(0);
  const set = (o, arr) => { for (let i = 0; i < arr.length; i++) pvd[o + i] = arr[i]; };
  pvd[0] = 1; set(1, strA('CD001', 5)); pvd[6] = 1;
  set(8, strA(opts.systemId || 'ARM-DOS', 32));
  set(40, strA((opts.volumeId || 'CDROM').toUpperCase(), 32));
  set(80, both32(total));
  set(120, both16(1)); set(124, both16(1)); set(128, both16(SECTOR));
  set(132, both32(ptSize));
  set(140, [ptL & 255, (ptL >>> 8) & 255, (ptL >>> 16) & 255, (ptL >>> 24) & 255]);
  set(148, [(ptM >>> 24) & 255, (ptM >>> 16) & 255, (ptM >>> 8) & 255, ptM & 255]);
  set(156, dirRecord(0, root.extent, root.size, 2, root.date));
  set(190, strA((opts.volumeSetId || opts.volumeId || '').toUpperCase(), 128));
  set(318, strA(opts.publisher || '', 128));
  set(446, strA(opts.preparer || '', 128));
  set(574, strA(opts.application || '', 128));
  const fid = (n) => strA(n ? n.toUpperCase() + ';1' : '', 37);
  set(702, fid(opts.copyrightFile)); set(739, fid(opts.abstractFile)); set(776, fid(opts.biblioFile));
  set(813, date17(baseDate)); set(830, date17(baseDate)); set(847, NODATE17); set(864, date17(baseDate));
  pvd[881] = 1;
  img.set(pvd, 16 * SECTOR);
  // ---- terminator
  img.set([255, ...strA('CD001', 5), 1], 17 * SECTOR);
  return img;
}

// ------------------------------------------------------------------ reader

const le32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const be32 = (b, o) => ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0;
const le16 = (b, o) => b[o] | (b[o + 1] << 8);
const str = (b, o, n) => String.fromCharCode(...b.subarray(o, o + n)).replace(/ +$/, '');

export class IsoReader {
  /** img: Uint8Array of the whole image, or { read(lba, count) -> Uint8Array } */
  constructor(img) {
    this.read = img instanceof Uint8Array ? (lba, n) => img.subarray(lba * SECTOR, (lba + n) * SECTOR) : img.read.bind(img);
    const p = this.read(16, 1);
    if (p[0] !== 1 || str(p, 1, 5) !== 'CD001') throw new Error('no ISO 9660 primary volume descriptor at sector 16');
    const t = this.read(17, 1);
    this.pvd = {
      systemId: str(p, 8, 32), volumeId: str(p, 40, 32), volumeSpace: le32(p, 80),
      volumeSpaceBE: be32(p, 84), blockSize: le16(p, 128), pathTableSize: le32(p, 132),
      pathTableL: le32(p, 140), pathTableM: be32(p, 148),
      volumeSetId: str(p, 190, 128), publisher: str(p, 318, 128), preparer: str(p, 446, 128),
      application: str(p, 574, 128), copyrightFile: str(p, 702, 37), abstractFile: str(p, 739, 37),
      biblioFile: str(p, 776, 37), created: str(p, 813, 16), terminator: t[0] === 255 && str(t, 1, 5) === 'CD001',
    };
    this.root = this.record(p, 156);
  }
  record(b, o) {
    const len = b[o], nl = b[o + 32];
    const id = b.subarray(o + 33, o + 33 + nl);
    let name = nl === 1 && id[0] === 0 ? '.' : nl === 1 && id[0] === 1 ? '..' : String.fromCharCode(...id);
    const d = b.subarray(o + 18, o + 25);
    return { len, extent: le32(b, o + 2), extentBE: be32(b, o + 6), size: le32(b, o + 10), sizeBE: be32(b, o + 14),
      flags: b[o + 25], dir: (b[o + 25] & 2) !== 0, name: name === '.' || name === '..' ? name : name.replace(/;1$/, '').replace(/\.$/, ''), rawName: name,
      date: { y: 1900 + d[0], mo: d[1], d: d[2], h: d[3], mi: d[4], s: d[5], gmt: d[6] } };
  }
  /** all records of a directory (incl. . and ..) */
  entries(rec) {
    const b = this.read(rec.extent, rec.size / SECTOR);
    const out = [];
    for (let s = 0; s < rec.size; s += SECTOR) {
      let o = s;
      while (o < s + SECTOR && b[o]) { out.push(this.record(b, o)); o += b[o]; }
    }
    return out;
  }
  lookup(path) {
    let r = this.root;
    for (const p of path.split(/[\\/]/).filter(Boolean)) {
      if (!r.dir) return null;
      r = this.entries(r).find((e) => e.name === p.toUpperCase() && e.name !== '.' && e.name !== '..');
      if (!r) return null;
    }
    return r;
  }
  list(path = '\\') { const r = this.lookup(path); return r && r.dir ? this.entries(r).filter((e) => e.name !== '.' && e.name !== '..') : null; }
  readFile(path) {
    const r = typeof path === 'string' ? this.lookup(path) : path;
    if (!r || r.dir) return null;
    if (!r.size) return new Uint8Array(0);
    return this.read(r.extent, Math.ceil(r.size / SECTOR)).slice(0, r.size);
  }
  /** every file: [{ path, rec }] */
  walk(rec = this.root, prefix = '') {
    const out = [];
    for (const e of this.entries(rec)) {
      if (e.name === '.' || e.name === '..') continue;
      const p = prefix ? prefix + '\\' + e.name : e.name;
      if (e.dir) { out.push({ path: p, rec: e }); out.push(...this.walk(e, p)); } else out.push({ path: p, rec: e });
    }
    return out;
  }
  pathTable(big = false) {
    const loc = big ? this.pvd.pathTableM : this.pvd.pathTableL;
    const b = this.read(loc, Math.ceil(this.pvd.pathTableSize / SECTOR));
    const out = [];
    for (let o = 0; o < this.pvd.pathTableSize;) {
      const nl = b[o];
      const ext = big ? be32(b, o + 2) : le32(b, o + 2);
      const par = big ? (b[o + 6] << 8) | b[o + 7] : le16(b, o + 6);
      const id = b.subarray(o + 8, o + 8 + nl);
      out.push({ name: nl === 1 && id[0] === 0 ? '' : String.fromCharCode(...id), extent: ext, parent: par });
      o += 8 + nl + (nl & 1);
    }
    return out;
  }
}
