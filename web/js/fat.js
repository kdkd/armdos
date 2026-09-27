// FAT12/FAT16 in the browser: list, read and write files on the disk images
// (floppies without a partition table, hard disks with an MBR). A port of the
// relevant parts of disk/mkimage.mjs (FatReader, 8.3 names, DOS dates), plus a
// writer that allocates free clusters, updates every FAT copy and adds or
// replaces directory entries.

const SECTOR = 512;
export class DiskFullError extends Error {}

const VALID_CH = /[A-Z0-9!#$%&'()\-@^_`{}~\x80-\xFF]/;

/** Host file name -> an unused 8.3 name ("BASE    EXT", 11 chars), DOS style. */
export function hostTo83(name, taken = new Set()) {
  let up = name.normalize('NFKD').replace(/[\u0300-\u036f]/g, '').toUpperCase();
  up = up.replace(/^\.+/, '');
  const dot = up.lastIndexOf('.');
  const clean = (s) => [...s].filter((c) => c !== ' ' && c !== '.').map((c) => (VALID_CH.test(c) ? c : '_')).join('');
  let base = clean(dot > 0 ? up.slice(0, dot) : up).slice(0, 8) || 'FILE';
  const ext = clean(dot > 0 ? up.slice(dot + 1) : '').slice(0, 3);
  let n83 = base.padEnd(8) + ext.padEnd(3);
  for (let k = 1; taken.has(n83); k++) {          // clash: end the base with a number (NAME1, NAM12, ...)
    const s = String(k);
    n83 = (base.slice(0, 8 - s.length) + s).padEnd(8) + ext.padEnd(3);
  }
  return n83;
}
export const show83 = (n83) => { const b = n83.slice(0, 8).trimEnd(), e = n83.slice(8).trimEnd(); return e ? `${b}.${e}` : b; };

export function dosStamp(d = new Date()) {
  const y = Math.min(Math.max(d.getFullYear(), 1980), 2107);
  return { date: ((y - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate(), time: (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1) };
}
export function fmtStamp(date, time) {
  const y = 1980 + (date >> 9), mo = (date >> 5) & 15, d = date & 31, h = time >> 11, mi = (time >> 5) & 63;
  return `${String(mo).padStart(2, '0')}-${String(d).padStart(2, '0')}-${String(y % 100).padStart(2, '0')}  ${String(h).padStart(2, ' ')}:${String(mi).padStart(2, '0')}`;
}

export class FatVolume {
  constructor(img) {
    this.img = img;
    this.dv = new DataView(img.buffer, img.byteOffset, img.byteLength);
    this.dirty = new Set();              // image sectors written (absolute LBA)
    const u16 = (o) => this.dv.getUint16(o, true), u32 = (o) => this.dv.getUint32(o, true);
    const plausible = (o) => u16(o + 0x0B) === SECTOR && [1, 2, 4, 8, 16, 32, 64, 128].includes(img[o + 0x0D]) &&
      img[o + 0x10] >= 1 && img[o + 0x10] <= 2 && u16(o + 0x0E) >= 1 && img[o + 0x15] >= 0xF0;
    let off = 0;
    if (img[0x1FE] === 0x55 && img[0x1FF] === 0xAA && !plausible(0)) {
      let pick = null;
      for (let i = 0; i < 4; i++) {
        const e = 0x1BE + 16 * i, type = img[e + 4];
        if ([1, 4, 6, 0x0E].includes(type) && (!pick || img[e] === 0x80)) pick = { start: u32(e + 8) };
      }
      if (!pick) throw new Error('no DOS partition on this disk');
      off = pick.start * SECTOR;
    } else if (!plausible(0)) throw new Error('not a DOS disk (no BPB)');
    if (!plausible(off)) throw new Error('the DOS partition has no valid boot sector');
    this.off = off;
    const total = u16(off + 0x13) || u32(off + 0x20);
    const L = { spc: img[off + 0x0D], reserved: u16(off + 0x0E), nfats: img[off + 0x10], root: u16(off + 0x11), total, fatSecs: u16(off + 0x16) };
    L.rootSecs = Math.ceil(L.root * 32 / SECTOR);
    L.firstRoot = L.reserved + L.nfats * L.fatSecs;
    L.firstData = L.firstRoot + L.rootSecs;
    L.clusters = Math.floor((total - L.firstData) / L.spc);
    L.type = L.clusters < 4085 ? 12 : 16;
    L.csize = L.spc * SECTOR;
    this.L = L;
    this.label = this.list(null).find((e) => e.attr & 0x08 && !(e.attr & 0x10))?.raw.trimEnd() || '';
  }

  // ------------------------------------------------------------ FAT
  fatGet(c) {
    const base = this.off + this.L.reserved * SECTOR;
    if (this.L.type === 12) { const v = this.dv.getUint16(base + ((c * 3) >> 1), true); return c & 1 ? v >> 4 : v & 0xFFF; }
    return this.dv.getUint16(base + c * 2, true);
  }
  fatSet(c, val) {
    for (let f = 0; f < this.L.nfats; f++) {
      const base = this.off + (this.L.reserved + f * this.L.fatSecs) * SECTOR;
      if (this.L.type === 12) {
        const o = base + ((c * 3) >> 1), v = this.dv.getUint16(o, true);
        this.dv.setUint16(o, c & 1 ? (v & 0x000F) | (val << 4) : (v & 0xF000) | (val & 0xFFF), true);
        this.touch(o, 2);
      } else { this.dv.setUint16(base + c * 2, val, true); this.touch(base + c * 2, 2); }
    }
  }
  get eoc() { return this.L.type === 12 ? 0xFFF : 0xFFFF; }
  isEoc(v) { return this.L.type === 12 ? v >= 0xFF8 : v >= 0xFFF8; }
  chain(c) {
    const out = [], seen = new Set();
    while (c >= 2 && !this.isEoc(c) && c <= this.L.clusters + 1) {
      if (seen.has(c)) throw new Error('cross-linked cluster chain');
      seen.add(c); out.push(c); c = this.fatGet(c);
    }
    return out;
  }
  freeClusters() { const out = []; for (let c = 2; c < this.L.clusters + 2; c++) if (this.fatGet(c) === 0) out.push(c); return out; }
  get freeBytes() { return this.freeClusters().length * this.L.csize; }
  clusterOff(c) { return this.off + (this.L.firstData + (c - 2) * this.L.spc) * SECTOR; }
  touch(o, len) { for (let s = Math.floor(o / SECTOR); s <= Math.floor((o + len - 1) / SECTOR); s++) this.dirty.add(s); }
  put(o, bytes) { this.img.set(bytes, o); this.touch(o, bytes.length); }

  // ------------------------------------------------------------ directories
  /** Byte offsets of a directory's 32-byte slots (root, or a subdirectory entry). */
  slots(dir) {
    const out = [];
    if (!dir) { const o0 = this.off + this.L.firstRoot * SECTOR; for (let k = 0; k < this.L.root; k++) out.push(o0 + k * 32); return out; }
    for (const c of this.chain(dir.cluster)) { const o0 = this.clusterOff(c); for (let k = 0; k < this.L.csize / 32; k++) out.push(o0 + k * 32); }
    return out;
  }
  list(dir) {
    const out = [], img = this.img;
    for (const o of this.slots(dir)) {
      if (img[o] === 0) break;
      if (img[o] === 0xE5 || img[o + 11] === 0x0F) continue;
      let raw = ''; for (let k = 0; k < 11; k++) raw += String.fromCharCode(img[o + k]);
      if (img[o] === 0x05) raw = '\xE5' + raw.slice(1);
      out.push({ raw, name: show83(raw), attr: img[o + 11], time: this.dv.getUint16(o + 22, true), date: this.dv.getUint16(o + 24, true),
        cluster: this.dv.getUint16(o + 26, true), size: this.dv.getUint32(o + 28, true), slot: o });
    }
    return out;
  }
  /** Visible files and subdirectories (no label, no . and ..). */
  entries(dir) { return this.list(dir).filter((e) => !(e.attr & 0x08) && e.raw[0] !== '.'); }
  readFile(ent) {
    const out = new Uint8Array(ent.size); let p = 0;
    for (const c of this.chain(ent.cluster)) {
      if (p >= ent.size) break;
      const n = Math.min(this.L.csize, ent.size - p);
      out.set(this.img.subarray(this.clusterOff(c), this.clusterOff(c) + n), p); p += n;
    }
    return out;
  }

  // ------------------------------------------------------------ writing
  /**
   * Write (or replace) a file in dir (null = root). name: a host name, mapped to
   * 8.3 with hostTo83 unless it replaces a file of the same 8.3 name.
   * Returns the 8.3 name used. Throws DiskFullError without changing anything.
   */
  writeFile(dir, name, data, { replace = true, when = new Date() } = {}) {
    const existing = this.list(dir);
    const taken = new Set(existing.map((e) => e.raw));
    let n83 = hostTo83(name, new Set());
    let old = existing.find((e) => e.raw === n83 && !(e.attr & 0x18));
    if (!old || !replace) { old = null; n83 = hostTo83(name, taken); }
    const csize = this.L.csize, need = Math.ceil(data.length / csize);
    const freed = old ? this.chain(old.cluster) : [];
    const free = this.freeClusters();
    let slot = old ? old.slot : this.freeSlot(dir);
    const needDirCluster = slot === null ? 1 : 0;
    if (slot === null && !dir) throw new DiskFullError('the root directory is full');
    if (need + needDirCluster > free.length + freed.length) {
      throw new DiskFullError(`not enough room: ${show83(n83)} needs ${(need * csize / 1024).toFixed(0)} KB, ${((free.length + freed.length) * csize / 1024).toFixed(0)} KB free`);
    }
    // release the old chain first, then allocate lowest clusters first (as DOS does)
    for (const c of freed) this.fatSet(c, 0);
    const pool = this.freeClusters();
    if (needDirCluster) slot = this.growDir(dir, pool.shift());
    const got = pool.slice(0, need);
    got.forEach((c, k) => {
      this.fatSet(c, k + 1 < got.length ? got[k + 1] : this.eoc);
      const part = data.subarray(k * csize, Math.min(data.length, (k + 1) * csize));
      const buf = new Uint8Array(csize); buf.set(part);
      this.put(this.clusterOff(c), buf);
    });
    const e = new Uint8Array(32);
    for (let k = 0; k < 11; k++) e[k] = n83.charCodeAt(k) & 0xFF;
    if (e[0] === 0xE5) e[0] = 0x05;
    e[11] = 0x20;                                            // archive
    const { date, time } = dosStamp(when);
    const v = new DataView(e.buffer);
    v.setUint16(22, time, true); v.setUint16(24, date, true);
    v.setUint16(26, got.length ? got[0] : 0, true); v.setUint32(28, data.length, true);
    this.put(slot, e);
    return show83(n83);
  }
  freeSlot(dir) {
    for (const o of this.slots(dir)) if (this.img[o] === 0 || this.img[o] === 0xE5) return o;
    return null;
  }
  growDir(dir, c) {
    const last = this.chain(dir.cluster).pop();
    this.fatSet(last, c); this.fatSet(c, this.eoc);
    this.put(this.clusterOff(c), new Uint8Array(this.L.csize));
    return this.clusterOff(c);
  }
  deleteFile(dir, ent) {
    for (const c of this.chain(ent.cluster)) this.fatSet(c, 0);
    this.put(ent.slot, new Uint8Array([0xE5]));
  }
  /** Absolute image LBAs written since the last call. */
  takeDirty() { const s = [...this.dirty].sort((a, b) => a - b); this.dirty.clear(); return s; }
}
