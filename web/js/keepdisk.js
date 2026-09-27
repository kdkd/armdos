// Drive D:, the one that is the user's to keep (disk/d.json, disk/d/README.TXT): the primary
// IDE slave, 128 MB. Its sectors live in an IndexedDB database of its own ("armdos-d") that
// nothing else on the page opens - not C:'s store (which drops the sectors of old C: images),
// not a new release, not a new build of D: itself.
//
// The first time a browser runs ARM-DOS, D: is made from the build's few non-zero sectors
// (images.json "d": gzipped [lba u32 LE][512 bytes] records, a few KB) and every one of them is
// stored at once; from then on D: depends on nothing but this browser's storage. It is made only
// when its store is really empty: a store that can't be read is an error, never "empty", so a
// bad moment can't put a blank D: over the user's.
//
//   const d = new KeepDisk(images.d, { onChange });
//   const img = await d.open();        // the whole drive, for the machine (new Machine({ hd2: img }))
//   d.wrote(lbas)                       // the machine or the Files panel wrote these sectors
//   await d.backup()  -> { data, name } // the drive as a file (gzipped where the browser can)
//   await d.restore(file) / d.erase()  // in place, in one transaction; switch the machine off first

import { DiskStore } from './storage.js';
import { fetchBinary, maybeGunzip } from './util.js';

const SECTOR = 512;

export class KeepDisk {
  constructor(info, { onChange = () => {} } = {}) {
    this.info = info;                 // images.json "d": { file, size, sha, sectors }
    this.store = new DiskStore('d', { dbName: 'armdos-d', delay: 200 });     // (stored soon: a tab closing may not finish a late save)
    this.store.onChange = () => onChange();
    this.onChange = onChange;
    this.img = null;
    this.created = false;             // made new in this browser this time
    this.kept = null;                 // navigator.storage.persisted(): the browser won't clear it on its own
  }
  get bytes() { return this.info.sectors * SECTOR; }

  async open() {
    const img = new Uint8Array(this.bytes);
    const n = await this.store.restore(img);        // (throws if the store can't be read)
    this.img = img;
    if (!n) {
      await this.applyBase(img);
      if (!this.store.error) {                       // really empty (a first visit, or cleared site data): store it all
        this.created = true;
        for (const lba of this.nonZero(img)) this.store.dirty.add(lba);
        await this.store.flush();
      }
    }
    try { this.kept = await navigator.storage?.persisted?.() ?? null; } catch { this.kept = null; }
    this.onChange();
    return img;
  }
  /** The factory D: (the few non-zero sectors the build made) into img. */
  async applyBase(img) {
    const recs = await fetchBinary(this.info.file, null, this.info);
    for (let o = 0; o + 4 + SECTOR <= recs.length; o += 4 + SECTOR) {
      const lba = recs[o] | (recs[o + 1] << 8) | (recs[o + 2] << 16) | (recs[o + 3] << 24);
      if ((lba + 1) * SECTOR <= img.length) img.set(recs.subarray(o + 4, o + 4 + SECTOR), lba * SECTOR);
    }
  }
  *nonZero(img) {
    const w = new Uint32Array(img.buffer, img.byteOffset, img.length >> 2);
    for (let lba = 0, n = img.length / SECTOR; lba < n; lba++) {
      for (let i = lba * 128, e = i + 128; i < e; i++) if (w[i]) { yield lba; break; }
    }
  }

  /** Sectors written (by DOS, or by the Files panel): store them. The first write is also when the
   *  page asks the browser to keep this site's storage for good - D: now holds something. */
  wrote(lbas) {
    for (const l of lbas) this.store.markDirty(l, 1);
    if (!this.kept && !this.asked) {
      this.asked = true;
      navigator.storage?.persist?.().then((ok) => { this.kept = ok; this.onChange(); }, () => {});
    }
  }

  async backup() {
    await this.store.flush();
    if (typeof CompressionStream !== 'undefined') {
      const gz = await new Response(new Blob([this.img]).stream().pipeThrough(new CompressionStream('gzip'))).arrayBuffer();
      return { data: new Uint8Array(gz), name: 'armdos-d.img.gz' };
    }
    return { data: this.img, name: 'armdos-d.img' };
  }
  /** Replace D: with a backup: a .img of the right size, or the .img.gz Back up D: makes. */
  async restore(file) {
    const buf = await maybeGunzip(new Uint8Array(await file.arrayBuffer()));
    if (buf.length !== this.bytes) throw new Error(`that isn't a D: backup (${buf.length.toLocaleString('en-US')} bytes; D: is ${this.bytes.toLocaleString('en-US')})`);
    if (buf[510] !== 0x55 || buf[511] !== 0xAA) throw new Error("that image has no partition table, so it isn't a hard disk");
    await this.replace(buf);
  }
  /** D: as it came: formatted, with only its README. */
  async erase() {
    const img = new Uint8Array(this.bytes);
    await this.applyBase(img);
    await this.replace(img);
  }
  /** In place (the machine keeps the same buffer), every changed sector in one transaction:
   *  those that hold something now, and those that held something before (now zeros). */
  async replace(src) {
    const lbas = new Set([...this.nonZero(this.img), ...this.nonZero(src)]);
    this.img.set(src);
    for (const l of lbas) this.store.dirty.add(l);
    await this.store.flush();
    if (this.store.error) throw new Error(`the browser would not store it (${this.store.error}); it is on D: for now and the page keeps trying`);
  }
}
