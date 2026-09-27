// The Files panel: browse drive A: (the diskette in the drive) and C:, click a
// file to download it, drop host files to copy them in (web/js/fat.js).
//
// DOS caches disk sectors, so the page never writes under a running DOS:
// the diskette is popped out, written and pushed back in (a media change DOS
// notices), and C: only takes files while the machine is switched off.

import { $, el, download } from './util.js';
import { FatVolume, DiskFullError, fmtStamp } from './fat.js';

const IMAGE_SIZES = new Set([368640, 737280, 1228800, 1474560, 2949120]);
/** A dropped file that is a diskette image rather than a file to copy. */
export function looksLikeImage(f) { return IMAGE_SIZES.has(f.size) && /\.(img|ima|vfd|flp|dsk)$/i.test(f.name); }

export class Files {
  constructor(o) {
    this.o = o;   // { floppy(), ensureFloppy(), hd(), powered(), machine(), sound, onFloppyWritten(), onHdWritten(lbas) }
    this.drive = 'A'; this.path = [];     // path: directory entries from the root
    this.list = $('filesList'); this.foot = $('filesFoot');
    for (const t of document.querySelectorAll('.ftab')) t.onclick = () => this.setDrive(t.dataset.drive);
    $('filesAdd').onchange = (e) => { const fs = [...e.target.files]; e.target.value = ''; if (fs.length) this.copyIn(fs, this.drive); };
    const box = $('files');
    box.addEventListener('dragover', (e) => { if ([...(e.dataTransfer?.types || [])].includes('Files')) { e.preventDefault(); e.stopPropagation(); box.classList.add('drag-over'); } });
    box.addEventListener('dragleave', () => box.classList.remove('drag-over'));
    box.addEventListener('drop', (e) => {
      box.classList.remove('drag-over');
      const fs = [...(e.dataTransfer?.files || [])];
      if (!fs.length) return;
      e.preventDefault();                 // the window's drop handler sees defaultPrevented and leaves it to us
      this.copyIn(fs, this.drive);
    });
    this.timer = 0;
    this.render();
  }

  setDrive(d) {
    this.drive = d; this.path = [];
    for (const t of document.querySelectorAll('.ftab')) t.setAttribute('aria-selected', String(t.dataset.drive === d));
    this.render();
  }
  /** The disks changed (a write, an insert, an eject): redraw soon. */
  changed() { clearTimeout(this.timer); this.timer = setTimeout(() => this.render(), 250); }

  image(drive = this.drive) { return drive === 'A' ? this.o.floppy()?.data || null : this.o.hd(); }
  volume(drive = this.drive) {
    const img = this.image(drive);
    if (!img) return null;
    return new FatVolume(img);
  }
  status(msg, kind = '') { this.foot.textContent = msg; this.foot.className = 'files-foot' + (kind ? ' ' + kind : ''); this.statusUntil = kind ? performance.now() + 6000 : 0; }

  // C: streams in chunks: the boot sector, FATs and root directory are fetched before
  // listing; a subdirectory or file is fetched when it is opened or downloaded.
  stream() { const s = this.o.hdStream?.(); return s && !s.complete ? s : null; }
  async prepareC() {
    const s = this.stream();
    if (!s || this.cMeta === s) return;
    await s.ensureBytes(0, 64 * 1024);                     // MBR + the partition's boot sector
    const v = new FatVolume(this.o.hd());
    await s.ensureBytes(0, v.off + v.L.firstData * 512);   // FATs and the root directory
    this.cMeta = s;
  }
  async ensureEnt(v, e) {
    const s = this.stream();
    if (s) await s.ensureRanges(v.chain(e.cluster).map((c) => [v.clusterOff(c), v.L.csize]));
  }
  render() {
    const drive = this.drive;
    $('filesAddLbl').hidden = drive === 'C' && this.o.powered();
    if (drive === 'C' && this.stream() && this.cMeta !== this.stream()) {
      this.showEmpty('Reading C:…');
      this.prepareC().then(() => this.render(), (e) => this.showEmpty(`C: can't be read right now (${e.message}).`));
      return;
    }
    let v;
    try { v = this.volume(); }
    catch (e) { this.showEmpty(`This diskette has no DOS file system (${e.message}).`); return; }
    if (!v) { this.showEmpty(drive === 'A' ? 'Drive A: is empty. Put a diskette in, or drop files here and I will use the blank one.' : 'Drive C: is still loading.'); return; }
    // re-find the directory path (entries move when directories grow)
    let dir = null; const names = [];
    try {
      for (const p of this.path) { dir = v.entries(dir).find((e) => e.name === p && (e.attr & 0x10)); if (!dir) throw 0; names.push(p); }
    } catch { this.path = []; dir = null; }
    $('filesPath').textContent = `${drive}:\\${this.path.join('\\')}${v.label ? `   [${v.label}]` : ''}`;
    const ents = v.entries(dir).sort((a, b) => ((b.attr & 0x10) - (a.attr & 0x10)) || a.name.localeCompare(b.name));
    this.list.textContent = '';
    if (this.path.length) this.list.append(this.row('..', '<DIR>', '', true, () => { this.path.pop(); this.render(); }));
    for (const e of ents) {
      const isDir = !!(e.attr & 0x10);
      this.list.append(this.row(e.name, isDir ? '<DIR>' : e.size.toLocaleString('en-US'), fmtStamp(e.date, e.time), isDir, () => {
        const s = drive === 'C' ? this.stream() : null;
        if (isDir) {
          if (s) this.ensureEnt(v, e).then(() => { this.path.push(e.name); this.render(); }, (err) => this.status(err.message, 'err'));
          else { this.path.push(e.name); this.render(); }
          return;
        }
        (async () => {
          if (s) { this.status(`Fetching ${e.name}…`, 'ok'); await this.ensureEnt(v, e); }
          download(v.readFile(e), e.name); this.status(`Downloaded ${e.name}.`, 'ok');
        })().catch((err) => this.status(`Could not read ${e.name}: ${err.message}`, 'err'));
      }));
    }
    if (!ents.length && !this.path.length) this.list.append(el('div', { class: 'files-empty', text: 'No files. Drop some here.' }));
    if (!this.statusUntil || performance.now() > this.statusUntil) {
      const files = ents.filter((e) => !(e.attr & 0x10));
      const bytes = files.reduce((s, e) => s + e.size, 0);
      const ro = drive === 'C' && this.o.powered() ? '   (read-only while the machine is on)' : this.o.floppy()?.writeProtected && drive === 'A' ? '   (write-protected)' : '';
      this.status(`${files.length} file(s) ${bytes.toLocaleString('en-US')} bytes   ${v.freeBytes.toLocaleString('en-US')} bytes free${ro}`);
    }
  }
  row(name, size, date, isDir, onclick) {
    return el('button', { class: 'frow' + (isDir ? ' dir' : ''), title: isDir ? `Open ${name}` : `Download ${name}`, onclick },
      el('span', { class: 'fn', text: name }), el('span', { class: 'fs', text: size }), el('span', { class: 'fd', text: date }));
  }
  showEmpty(msg) {
    this.list.textContent = ''; this.list.append(el('div', { class: 'files-empty', text: msg }));
    $('filesPath').textContent = `${this.drive}:\\`;
    if (!this.statusUntil || performance.now() > this.statusUntil) this.status('');
  }

  /** Copy host files onto A: or C: (current directory when it is the drive on show). */
  async copyIn(fileList, drive) {
    if (drive === 'C' && this.o.powered()) {
      this.status('C: is in use. Switch the machine off to copy files onto it (downloading works any time).', 'err'); return;
    }
    if (drive === 'A' && !this.o.floppy()) await this.o.ensureFloppy();
    const disk = drive === 'A' ? this.o.floppy() : null;
    if (drive === 'A' && !disk) { this.status('There is no diskette to copy onto.', 'err'); return; }
    if (disk && disk.writeProtected) { this.status(`"${disk.label}" is write-protected. Try the blank diskette.`, 'err'); return; }
    const img = this.image(drive);
    if (!img) return;
    if (drive === 'C') { try { await this.prepareC(); } catch (e) { this.status(e.message, 'err'); return; } }
    const files = await Promise.all(fileList.map(async (f) => ({ name: f.name, data: new Uint8Array(await f.arrayBuffer()), when: new Date(f.lastModified || Date.now()) })));
    const m = drive === 'A' ? this.o.machine() : null;
    this.status(`Writing ${files.length} file(s) to ${drive}:…`, 'ok');
    // pop the diskette out while we write
    if (m) { m.ejectFloppy(); this.o.sound.fdEject(); }
    const done = [], errors = [];
    let v;
    try {
      v = new FatVolume(img);
      let dir = null;
      if (this.drive === drive) for (const p of this.path) dir = v.entries(dir).find((e) => e.name === p && (e.attr & 0x10)) || dir;
      for (const f of files) {
        try { done.push([f.name, v.writeFile(dir, f.name, f.data, { when: f.when })]); }
        catch (e) { errors.push(e instanceof DiskFullError ? e.message : `${f.name}: ${e.message}`); if (e instanceof DiskFullError) break; }
      }
    } catch (e) { errors.push(e.message); }
    const lbas = v ? v.takeDirty() : [];
    if (drive === 'A') {
      if (lbas.length) this.o.onFloppyWritten();
      if (m) await new Promise((r) => setTimeout(() => { m.insertFloppy(img, !!disk.writeProtected); this.o.sound.fdInsert(); r(); }, 450));
    } else if (lbas.length) this.o.onHdWritten(lbas);
    const renamed = done.filter(([a, b]) => a.toUpperCase() !== b);
    let msg = done.length ? `Copied ${done.length} file(s) to ${drive}:` + (renamed.length ? ` (${renamed.slice(0, 3).map(([a, b]) => `${a} → ${b}`).join(', ')}${renamed.length > 3 ? ', …' : ''})` : '') + '.' : '';
    if (errors.length) msg += (msg ? ' ' : '') + errors.join(' ');
    if (this.drive !== drive) this.setDrive(drive); else this.render();
    this.status(msg || 'Nothing copied.', errors.length ? 'err' : 'ok');
  }
}
