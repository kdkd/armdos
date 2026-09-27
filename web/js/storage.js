// Persistence: a hard disk's changed sectors in IndexedDB and the CMOS battery RAM in
// localStorage. C:'s sectors are keyed by its factory image's hash (so a new C: never mixes
// with stale sectors; restoring drops the old ones); D:'s live in a database of their own
// (web/js/keepdisk.js), which nothing C: does can reach.

const DB_NAME = 'armdos', STORE = 'hdsectors', SECTOR = 512;

function req(r) { return new Promise((res, rej) => { r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); }); }

export class DiskStore {
  constructor(version, { dbName = DB_NAME, delay = 700 } = {}) {
    this.version = version;
    this.dbName = dbName;
    this.delay = delay;          // ms after the last write before the sectors are stored
    this.db = null;
    this.dirty = new Set();
    this.timer = 0;
    this.image = null;           // the live image to copy sectors from
    this.saved = 0;              // sectors currently stored for this version
    this.dropped = 0;            // sectors of other versions restore() found and deleted (a new C:)
    this.error = null;           // why the last save failed (null: saving works)
    this.failures = 0;           // consecutive failed saves (for the retry backoff)
    this.onChange = () => {};
  }
  async open() {
    if (this.db) return this.db;
    try {
      if (typeof indexedDB === 'undefined') throw new Error('this browser has no IndexedDB');
      const r = indexedDB.open(this.dbName, 1);
      r.onupgradeneeded = () => { r.result.createObjectStore(STORE); };
      this.db = await req(r);
      this.db.onclose = () => { this.db = null; };        // (the browser may close it: reopen next time)
    } catch (e) {
      console.warn(`IndexedDB unavailable; ${this.dbName} will not persist`, e);
      this.db = null;
      this.fail(e && (e.name || e.message) || 'unavailable');
    }
    return this.db;
  }
  /** A save failed: say so (the page shows it until a save works) and try again later. */
  fail(why) {
    this.error = String(why || 'error');
    this.failures++;
    clearTimeout(this.timer);
    this.timer = setTimeout(() => this.flush(), Math.min(60000, 1000 * 2 ** this.failures));
    this.onChange();
  }
  /** Apply saved sectors to img (in place); drop sectors saved for other image versions. */
  async restore(img, onSector = null) {
    this.image = img;
    const db = await this.open();
    if (!db) return 0;
    const tx = db.transaction(STORE, 'readwrite'), st = tx.objectStore(STORE);
    let n = 0;
    await new Promise((res, rej) => {
      const cur = st.openCursor();
      cur.onsuccess = () => {
        const c = cur.result;
        if (!c) return res();
        const [ver, lba] = c.key;
        if (ver !== this.version) { c.delete(); this.dropped++; }
        else if (lba >= 0 && (lba + 1) * SECTOR <= img.length) { img.set(new Uint8Array(c.value), lba * SECTOR); n++; if (onSector) onSector(lba); }
        c.continue();
      };
      cur.onerror = () => rej(cur.error);
    });
    this.saved = n;
    this.onChange();
    return n;
  }
  markDirty(lba, count) {
    for (let k = 0; k < count; k++) this.dirty.add(lba + k);
    clearTimeout(this.timer);
    this.timer = setTimeout(() => this.flush(), this.delay);
  }
  async flush() {
    clearTimeout(this.timer);
    if (!this.dirty.size || !this.image) return;
    const db = await this.open();
    if (!db) return;                                 // (open() reported it and scheduled a retry)
    // The sectors leave the pending set only once the transaction has committed: a failed
    // or aborted save (quota, the browser closing the database) puts them back for the retry.
    const lbas = [...this.dirty]; this.dirty.clear();
    let ok = false, why = null, cnt = null;
    try {
      const tx = db.transaction(STORE, 'readwrite'), st = tx.objectStore(STORE);
      for (const lba of lbas) {
        const off = lba * SECTOR;
        st.put(this.image.slice(off, off + SECTOR).buffer, [this.version, lba]);
      }
      cnt = st.count(IDBKeyRange.bound([this.version, 0], [this.version, Infinity]));
      ok = await new Promise((res) => {
        tx.oncomplete = () => res(true);
        tx.onerror = () => { why = tx.error || 'AbortError'; res(false); };
        tx.onabort = () => { why = tx.error || 'AbortError'; res(false); };
      });
    } catch (e) {                                    // e.g. InvalidStateError: the database was closed
      why = e; this.db = null;
    }
    if (!ok) {
      for (const lba of lbas) this.dirty.add(lba);   // (sectors written again meanwhile are there already)
      this.fail(why && (why.name || why.message) || why || 'error');
      return;
    }
    if (cnt && cnt.readyState === 'done' && !cnt.error) this.saved = cnt.result;
    if (this.error) { this.error = null; this.failures = 0; }
    this.onChange();
  }
  async clear() {
    this.dirty.clear(); clearTimeout(this.timer);
    const db = await this.open();
    if (!db) return;
    const tx = db.transaction(STORE, 'readwrite');
    tx.objectStore(STORE).delete(IDBKeyRange.bound([this.version, 0], [this.version, Infinity]));
    await new Promise((res) => { tx.oncomplete = res; tx.onerror = res; });
    this.saved = 0;
    this.onChange();
  }
}

export const cmosStore = {
  key: 'armdos.cmos',
  load() {
    try {
      const s = localStorage.getItem(this.key);
      if (!s) return null;
      const a = Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
      return a.length === 128 ? a : null;
    } catch { return null; }
  },
  save(ram) { try { localStorage.setItem(this.key, btoa(String.fromCharCode(...ram))); } catch {} },
  clear() { try { localStorage.removeItem(this.key); } catch {} },
};
