// The disk box and drive A:. Diskettes are listed in disks.json (staged by
// web/tools/build-site.mjs). Each one keeps its own contents for the session,
// so a disk you wrote to, ejected and put back still has your files.

import { $, el, fetchBinary, download } from './util.js';

const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

function floppySvg(d) {
  const body = d.colour || '#2b2d33', stripe = d.stripe || '#c0392b';
  const wpOpen = d.writeProtected;
  return `<svg viewBox="0 0 104 108" aria-hidden="true">
    <defs><linearGradient id="sh-${d.id}" x1="0" x2="1"><stop offset="0" stop-color="#d7dade"/><stop offset=".5" stop-color="#f4f5f6"/><stop offset="1" stop-color="#aeb3b9"/></linearGradient>
    <linearGradient id="bd-${d.id}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".16"/><stop offset=".6" stop-color="#fff" stop-opacity="0"/></linearGradient></defs>
    <path d="M4 2h88l10 10v92a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2z" fill="${body}"/>
    <path d="M4 2h88l10 10v92a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2z" fill="url(#bd-${d.id})"/>
    <rect x="26" y="2" width="52" height="34" rx="1.5" fill="url(#sh-${d.id})"/>
    <rect x="58" y="7" width="11" height="24" rx="1" fill="#2a2c30"/>
    <rect x="27" y="4" width="1.2" height="30" fill="#fff" opacity=".5"/>
    <rect x="12" y="46" width="80" height="58" rx="2" fill="#f4f0e4"/>
    <rect x="12" y="46" width="80" height="9" rx="2" fill="${stripe}"/>
    <rect x="12" y="52" width="80" height="3" fill="${stripe}"/>
    <g stroke="#b9c3cf" stroke-width=".6">${[66, 74, 82, 90, 98].map((y) => `<line x1="16" x2="88" y1="${y}" y2="${y}"/>`).join('')}</g>
    <text x="17" y="70" font-family="Newsreader, Georgia, serif" font-style="italic" font-size="11.5" fill="#1d2a5a">${esc(d.label)}</text>
    <text x="17" y="86" font-family="Newsreader, Georgia, serif" font-style="italic" font-size="8.5" fill="#2e3b6a">${esc(d.sub || '')}</text>
    <rect x="${wpOpen ? 5 : 5}" y="96" width="6" height="6" fill="${wpOpen ? '#0b0b0c' : body}" stroke="#000" stroke-opacity=".35" stroke-width=".5"/>
    <rect x="93" y="96" width="6" height="6" fill="#0b0b0c"/>
  </svg>`;
}

export class DiskBox {
  constructor({ sound, onInsert, onEject, isImage, onDropFiles }) {
    this.sound = sound; this.onInsert = onInsert; this.onEject = onEject;
    this.isImage = isImage || (() => true); this.onDropFiles = onDropFiles;
    this.disks = []; this.inDrive = null;    // { id, label, data, writeProtected }
    this.box = $('diskbox'); this.drive = $('drive');
    this.bindDrive();
  }

  async load() {
    let manifest = { disks: [] };
    try { manifest = await (await fetch('disks.json', { cache: 'no-cache' })).json(); } catch (e) { console.warn('disks.json', e); }
    this.disks = manifest.disks.map((d) => ({ ...d, data: null, modified: false }));
    this.render();
  }

  render() {
    this.box.textContent = '';
    for (const d of this.disks) {
      const b = el('button', { class: 'floppy' + (d.missing ? ' missing' : '') + (this.inDrive === d ? ' in-drive' : ''), draggable: d.missing ? 'false' : 'true', 'aria-label': `${d.label} ${d.sub || ''}: ${d.description || ''}` });
      b.innerHTML = floppySvg(d) + `<span class="tip">${esc(d.missing ? 'This one is still being written. Check back soon.' : d.description || '')}${d.writeProtected ? ' <i>(write-protected)</i>' : ''}</span>` + (d.modified ? '<span class="modified" title="changed since it came out of the box">✎</span>' : '');
      b.onclick = () => { if (!d.missing && this.inDrive !== d) this.insert(d); };
      b.addEventListener('dragstart', (e) => { e.dataTransfer.setData('text/x-armdos-disk', d.id); e.dataTransfer.effectAllowed = 'move'; });
      this.box.append(b);
    }
  }

  async insert(d) {
    if (this.inDrive) this.eject(true);
    if (!d.data) {
      try { d.data = await fetchBinary(d.file, null, d.size ? d : null); }
      catch (e) { console.error(e); return; }
    }
    this.inDrive = d;
    this.sound.fdInsert();
    this.drive.classList.add('has-disk');
    this.drive.style.setProperty('--disk-colour', d.colour || '#2b2d33');
    $('fdDownload').disabled = false;
    this.onInsert(d.data, !!d.writeProtected);
    this.render();
  }
  /** A user-supplied image (file picker or drag and drop). */
  async insertFile(file) {
    const data = new Uint8Array(await file.arrayBuffer());
    if (data.length < 160 * 1024 || data.length > 2949120) { alertBox(`${file.name} doesn't look like a diskette image (${data.length.toLocaleString()} bytes).`); return; }
    const d = { id: 'file-' + Date.now(), label: file.name.replace(/\.[^.]+$/, '').slice(0, 14), sub: 'your disk', colour: '#6b5b3e', stripe: '#e8a13a', description: file.name, data, custom: true };
    this.disks = this.disks.filter((x) => !x.custom || x === this.inDrive);
    this.disks.push(d);
    await this.insert(d);
  }
  eject(quiet = false) {
    if (!this.inDrive) return;
    if (!quiet) this.sound.fdEject();
    this.inDrive = null;
    this.drive.classList.remove('has-disk');
    $('fdDownload').disabled = true;
    this.onEject();
    this.render();
  }
  markWritten() { if (this.inDrive && !this.inDrive.modified) { this.inDrive.modified = true; this.render(); } }
  downloadCurrent() {
    const d = this.inDrive;
    if (d && d.data) download(d.data, (d.label || 'floppy').replace(/[^\w.-]+/g, '-').toLowerCase() + '.img');
  }

  bindDrive() {
    $('eject').onclick = (e) => { e.stopPropagation(); this.eject(); };
    $('fdDownload').onclick = () => this.downloadCurrent();
    $('fdFile').onchange = (e) => { const f = e.target.files[0]; if (f) this.insertFile(f); e.target.value = ''; };
    const dr = this.drive, veil = $('dropVeil');
    const isDisk = (e) => [...(e.dataTransfer?.types || [])].some((t) => t === 'Files' || t === 'text/x-armdos-disk');
    dr.addEventListener('dragover', (e) => { if (isDisk(e)) { e.preventDefault(); dr.classList.add('drag-over'); } });
    dr.addEventListener('dragleave', () => dr.classList.remove('drag-over'));
    // files can be dropped anywhere on the page
    let depth = 0;
    window.addEventListener('dragenter', (e) => { if ([...(e.dataTransfer?.types || [])].includes('Files')) { depth++; veil.hidden = false; } });
    window.addEventListener('dragleave', () => { if (--depth <= 0) { depth = 0; veil.hidden = true; } });
    window.addEventListener('dragover', (e) => { if (isDisk(e)) e.preventDefault(); });
    window.addEventListener('drop', (e) => {
      depth = 0; veil.hidden = true; dr.classList.remove('drag-over');
      if (e.defaultPrevented) return;                                   // taken by the Files panel
      if (!isDisk(e)) return;
      e.preventDefault();
      const id = e.dataTransfer.getData('text/x-armdos-disk');
      if (id) { const d = this.disks.find((x) => x.id === id); if (d && !d.missing) this.insert(d); return; }
      const fs = [...e.dataTransfer.files];
      if (fs.length === 1 && this.isImage(fs[0])) this.insertFile(fs[0]);
      else if (fs.length && this.onDropFiles) this.onDropFiles(fs);     // ordinary files: copy them onto the diskette
      else if (fs.length) this.insertFile(fs[0]);
    });
  }
}

function alertBox(msg) {
  const h = $('hdStatus'); const old = h.textContent;
  h.textContent = msg; h.style.color = '#f0b3a6';
  setTimeout(() => { h.textContent = old; h.style.color = ''; }, 5000);
}
