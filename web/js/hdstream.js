// The streamed hard disk. images.json describes C: as fixed-size chunks named by
// their content hash (web/tools/build-site.mjs); only the chunks the machine reads
// are downloaded, the rest trickle in behind it when the disk is idle.
//
//   const disk = new StreamedDisk(images.hd);   // disk.img: the full-size image (zeros until fetched)
//   machine.ata.source = disk;                   // ready / fetch / wrote (emu/dev/ata.mjs)
//
// Sectors the guest (or the persistence layer) has written are an overlay: a chunk
// that arrives later never overwrites them.

import { maybeGunzip, verifyBytes } from './util.js';

const SECTOR = 512;
const MISSING = 0, LOADING = 1, READY = 2;

export class StreamedDisk {
  constructor(info, { onProgress = () => {}, onWait = () => {} } = {}) {
    this.info = info;
    this.size = info.size;
    this.chunkSize = info.chunkSize;
    this.img = new Uint8Array(info.size);
    this.n = info.chunks.length;
    this.state = new Uint8Array(this.n);
    this.overlay = new Uint8Array(Math.ceil(info.size / SECTOR));    // 1 = written here, authoritative
    this.pending = new Map();            // chunk -> Promise<bool>
    this.urgent = 0;                     // demand fetches in flight
    this.failed = new Set();
    this.bytesFetched = 0;               // gzipped bytes over the wire (or from the SW cache)
    this.onProgress = onProgress; this.onWait = onWait;
    this.waiting = 0;
    this.stored = 0; this.readyStored = 0;
    info.chunks.forEach((h, k) => { if (h === null) this.state[k] = READY; else this.stored++; });
    this.prefetchOn = true;
    this.prefetchTimer = 0;
  }
  url(k) { return this.info.chunkBase + this.info.chunks[k] + '.gz'; }
  chunksOf(lba, n) {
    const a = Math.floor(lba * SECTOR / this.chunkSize), b = Math.floor(((lba + n) * SECTOR - 1) / this.chunkSize);
    const out = []; for (let k = a; k <= b && k < this.n; k++) out.push(k); return out;
  }
  get progress() { return this.stored ? this.readyStored / this.stored : 1; }
  get complete() { return this.readyStored >= this.stored; }

  // ------------------------------------------------------------ the ATA source interface
  ready(lba, n) { for (const k of this.chunksOf(lba, n)) if (this.state[k] !== READY) return false; return true; }
  fetch(lba, n) {
    const ks = this.chunksOf(lba, n).filter((k) => this.state[k] !== READY);
    this.waiting++; this.onWait(true);
    return Promise.all(ks.map((k) => this.load(k, true))).then((oks) => oks.every(Boolean)).finally(() => {
      if (--this.waiting === 0) this.onWait(false);
      this.schedulePrefetch();
    });
  }
  wrote(lba) { this.overlay[lba] = 1; }

  // ------------------------------------------------------------ fetching
  /** Load chunk k (once). urgent = the machine is waiting for it. Resolves true/false. */
  load(k, urgent = false) {
    if (this.state[k] === READY) return Promise.resolve(true);
    if (this.pending.has(k)) return this.pending.get(k);
    this.state[k] = LOADING;
    if (urgent) this.urgent++;
    const p = (async () => {
      for (let attempt = 0; attempt < 3; attempt++) {
        const ctl = typeof AbortController === 'function' ? new AbortController() : null;
        const timer = setTimeout(() => ctl && ctl.abort(), 20000);
        try {
          const r = await fetch(this.url(k), ctl ? { signal: ctl.signal } : {});
          if (!r.ok) throw new Error('HTTP ' + r.status);
          const gz = new Uint8Array(await r.arrayBuffer());
          this.bytesFetched += gz.length;
          // exactly this chunk (its length, and the content hash it is named by) or it
          // counts as a failed read: DOS gets a disk error, never someone's error page
          const raw = await verifyBytes(await maybeGunzip(gz),
            { size: Math.min(this.chunkSize, this.size - k * this.chunkSize), sha: this.info.chunks[k] }, `C: chunk ${k}`);
          this.install(k, raw);
          return true;
        } catch (e) {
          if (attempt === 2 || (typeof navigator !== 'undefined' && navigator.onLine === false)) { console.warn(`C: chunk ${k} unavailable`, e.message || e); break; }
          await new Promise((res) => setTimeout(res, 400 * (attempt + 1)));
        } finally { clearTimeout(timer); }
      }
      this.state[k] = MISSING; this.failed.add(k);
      return false;
    })().finally(() => { this.pending.delete(k); if (urgent) this.urgent--; });
    this.pending.set(k, p);
    return p;
  }
  install(k, raw) {
    const base = k * this.chunkSize, s0 = base / SECTOR, ns = Math.ceil(raw.length / SECTOR);
    let clean = true;
    for (let s = 0; s < ns; s++) if (this.overlay[s0 + s]) { clean = false; break; }
    if (clean) this.img.set(raw, base);
    else for (let s = 0; s < ns; s++) if (!this.overlay[s0 + s]) this.img.set(raw.subarray(s * SECTOR, (s + 1) * SECTOR), base + s * SECTOR);
    this.state[k] = READY; this.failed.delete(k); this.readyStored++;
    this.onProgress(this.progress);
  }

  // ------------------------------------------------------------ background prefetch
  /** Fetch the rest a few chunks at a time while nothing urgent is in flight and the tab is visible. */
  schedulePrefetch(delay = 600) {
    clearTimeout(this.prefetchTimer);
    if (!this.prefetchOn || this.complete) return;
    this.prefetchTimer = setTimeout(() => this.prefetchStep(), delay);
  }
  prefetchStep() {
    if (!this.prefetchOn || this.complete) return;
    const hidden = typeof document !== 'undefined' && document.hidden;
    if (this.urgent > 0 || this.waiting > 0 || hidden || this.pending.size >= 3) { this.schedulePrefetch(hidden ? 2000 : 250); return; }
    const want = [];
    for (let k = 0; k < this.n && want.length < 3 - this.pending.size; k++) if (this.state[k] === MISSING && !this.failed.has(k)) want.push(k);
    if (!want.length) return;
    Promise.all(want.map((k) => this.load(k, false))).then(() => this.schedulePrefetch(30));
  }
  /** Everything (downloads, Files panel copy-in). onStep(progress) while it works. */
  async fetchAll(onStep = () => {}) {
    this.failed.clear();
    for (let k = 0; k < this.n; k += 6) {
      const ks = []; for (let j = k; j < Math.min(this.n, k + 6); j++) if (this.state[j] !== READY) ks.push(j);
      const oks = await Promise.all(ks.map((j) => this.load(j, true)));
      if (!oks.every(Boolean)) throw new Error('part of C: could not be downloaded');
      onStep(this.progress);
    }
  }
  /** Make sure a byte range of the image is present. */
  async ensureBytes(off, len) {
    const a = Math.floor(off / this.chunkSize), b = Math.floor((off + Math.max(1, len) - 1) / this.chunkSize);
    const ks = []; for (let k = a; k <= b && k < this.n; k++) if (this.state[k] !== READY) ks.push(k);
    const oks = await Promise.all(ks.map((k) => this.load(k, true)));
    if (!oks.every(Boolean)) throw new Error('C: is not available offline yet');
  }
  /** Make sure a set of [offset, length] byte ranges is present (one request per missing chunk). */
  async ensureRanges(ranges) {
    const ks = new Set();
    for (const [off, len] of ranges) {
      const a = Math.floor(off / this.chunkSize), b = Math.floor((off + Math.max(1, len) - 1) / this.chunkSize);
      for (let k = a; k <= b && k < this.n; k++) if (this.state[k] !== READY) ks.add(k);
    }
    const oks = await Promise.all([...ks].map((k) => this.load(k, true)));
    if (!oks.every(Boolean)) throw new Error('C: is not available offline yet');
  }
  /** Factory reset: forget written sectors; chunks that held them are fetched again. */
  resetOverlay() {
    for (let k = 0; k < this.n; k++) {
      const s0 = k * this.chunkSize / SECTOR, s1 = Math.min(this.overlay.length, s0 + this.chunkSize / SECTOR);
      let dirty = false; for (let s = s0; s < s1; s++) if (this.overlay[s]) { dirty = true; break; }
      if (!dirty) continue;
      if (this.info.chunks[k] === null) this.img.fill(0, k * this.chunkSize, Math.min(this.size, (k + 1) * this.chunkSize));
      else if (this.state[k] === READY) { this.state[k] = MISSING; this.readyStored--; }
    }
    this.overlay.fill(0);
    this.onProgress(this.progress);
    this.schedulePrefetch();
  }
}
