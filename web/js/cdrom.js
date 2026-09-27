// The ARM-PC's CD-ROM drive (emu/dev/atapi.mjs, secondary IDE master) on the page:
// a 5.25" half-height drive in the case (tray, tray button, busy LED, headphone jack,
// volume knob) and a box of CD-ROMs (images.json "cdroms", staged by build-site from
// apps/cdrom's build/cdrom/<id>/disc.json).
//
//   const cd = new CdRom({ sound, machine: () => state.machine });
//   cd.mount($('case'))                                   // the drive bay + the disc box
//   cd.load(images)                                       // once images.json is in
//   new Machine({ ..., ...cd.machineOptions() })          // the disc in the drive at power-on + callbacks
//   cd.powerOff()                                         // lights out
//
// A disc's data track (the ISO, gzipped) is fetched when the disc goes in; its audio
// tracks only when the drive asks for them (CdDisc io.audio.request): Opus, or MP3 where
// the browser cannot decode Opus, decoded at 44.1 kHz by an OfflineAudioContext and
// kept as 16-bit PCM (two tracks at most; the drive asks again when it needs one back).
// While a track is on its way the drive "seeks" (holds the play position) and its LED
// flickers.

import { CdDisc } from '../emu/dev/atapi.js';
import { $, el, fetchBinary, prefs } from './util.js';

const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const mmss = (s) => `${Math.floor(s / 60)}:${String(Math.round(s % 60)).padStart(2, '0')}`;
const KEEP_TRACKS = 2;

/** The page's side of one disc: builds the emulator's CdDisc and feeds it data and audio. */
class DiscSource {
  constructor(entry, { onStatus }) {
    this.entry = entry; this.onStatus = onStatus;
    this.pcm = new Map();          // track number -> { left, right } Int16Array (LRU order)
    this.loading = new Map();      // track number -> Promise
    this.requests = [];            // track numbers the drive asked for (tests)
    const data = entry.tracks.find((t) => t.type === 'data');
    const img = new Uint8Array(data ? data.data.size : 0);
    let ready = !data, fetching = null;
    const fetchIso = () => fetching || (fetching = fetchBinary(data.data.file, null, data.data).then((b) => { img.set(b.subarray(0, img.length)); ready = true; return true; },
      (e) => { console.error('CD data track', e); fetching = null; return false; }));
    this.disc = new CdDisc(entry, {
      data: { img, ready: () => ready, fetch: () => fetchIso() },
      audio: { get: (n) => this.get(n), request: (n) => this.request(n) },
    });
    this.prefetch = fetchIso;
  }
  get(n) {
    const p = this.pcm.get(n);
    if (p) { this.pcm.delete(n); this.pcm.set(n, p); }      // most recently used last
    return p || null;
  }
  request(n) {
    this.requests.push(n);
    if (this.pcm.has(n)) return Promise.resolve();
    if (!this.loading.has(n)) {
      const t = this.entry.tracks.find((x) => x.number === n);
      const job = this.decode(t).then((pcm) => {
        this.pcm.set(n, pcm);
        while (this.pcm.size > KEEP_TRACKS) this.pcm.delete(this.pcm.keys().next().value);
      }).finally(() => { this.loading.delete(n); this.onStatus(); });
      this.loading.set(n, job);
      this.onStatus();
    }
    return this.loading.get(n);
  }
  async decode(t) {
    if (!t || !t.audio) throw new Error('no audio for track ' + (t && t.number));
    let lastErr;
    for (const kind of ['opus', 'mp3']) {
      const f = t.audio[kind];
      if (!f) continue;
      try {
        const r = await fetch(f.file);
        if (!r.ok) throw new Error(`${f.file}: HTTP ${r.status}`);
        const bytes = await r.arrayBuffer();
        const Ctx = window.OfflineAudioContext || window.webkitOfflineAudioContext;
        const ctx = new Ctx(2, 1, 44100);             // decodeAudioData resamples to the context's rate
        const buf = await new Promise((res, rej) => { const p = ctx.decodeAudioData(bytes, res, rej); if (p && p.catch) p.catch(rej); });
        const n = Math.min(buf.length, t.frames || buf.length);
        const conv = (ch) => {
          const src = buf.getChannelData(Math.min(ch, buf.numberOfChannels - 1)), out = new Int16Array(n);
          for (let i = 0; i < n; i++) { const v = src[i] * 32767; out[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
          return out;
        };
        const left = conv(0), right = buf.numberOfChannels > 1 ? conv(1) : left;
        this.decodedWith = kind;
        return { left, right };
      } catch (e) { lastErr = e; console.warn(`CD track ${t.number}: ${kind} did not decode`, e); }
    }
    throw lastErr || new Error('no audio file');
  }
  get busyTracks() { return [...this.loading.keys()]; }
}

// ---------------------------------------------------------------- the visitor's own ISO
// A local .iso is never read whole: the ATAPI drive only uses data.img.length and
// data.img.subarray(), so this stands in for the image and serves 64 KB blocks read
// from the File on demand (ready/fetch = the streamed data-track interface), keeping
// at most 64 MB of them.
const ISO_BLOCK = 64 * 1024, ISO_KEEP = 1024;
class LazyFileImage {
  constructor(file) { this.file = file; this.length = Math.ceil(file.size / 2048) * 2048; this.blocks = new Map(); this.loading = new Map(); }
  covers(a, b) { const out = []; for (let k = Math.floor(a / ISO_BLOCK); k * ISO_BLOCK < b; k++) out.push(k); return out; }
  has(a, b) { return this.covers(a, b).every((k) => this.blocks.has(k)); }
  async load(a, b) {
    await Promise.all(this.covers(a, b).map((k) => {
      if (this.blocks.has(k)) return null;
      if (!this.loading.has(k)) this.loading.set(k, this.file.slice(k * ISO_BLOCK, (k + 1) * ISO_BLOCK).arrayBuffer().then((buf) => {
        this.blocks.set(k, new Uint8Array(buf));
        while (this.blocks.size > ISO_KEEP) this.blocks.delete(this.blocks.keys().next().value);
      }).finally(() => this.loading.delete(k)));
      return this.loading.get(k);
    }));
  }
  subarray(a, b) {
    b = Math.min(b ?? this.length, this.length);
    const out = new Uint8Array(Math.max(0, b - a));
    for (const k of this.covers(a, b)) {
      const blk = this.blocks.get(k); if (!blk) continue;
      const s = Math.max(a, k * ISO_BLOCK), e = Math.min(b, k * ISO_BLOCK + blk.length);
      if (e > s) out.set(blk.subarray(s - k * ISO_BLOCK, e - k * ISO_BLOCK), s - a);
    }
    return out;
  }
}
/** Read the ISO 9660 primary volume descriptor: the volume id, or null if this isn't an ISO. */
export async function isoVolumeId(file) {
  if (file.size < 17 * 2048) return null;
  const pvd = new Uint8Array(await file.slice(16 * 2048, 17 * 2048).arrayBuffer());
  if (pvd[0] !== 1 || String.fromCharCode(...pvd.subarray(1, 6)) !== 'CD001') return null;
  return String.fromCharCode(...pvd.subarray(40, 72)).trim() || 'NO_LABEL';
}
class LocalIsoSource {
  constructor(entry, file) {
    this.entry = entry; this.file = file;
    const img = new LazyFileImage(file);
    this.img = img;
    this.disc = new CdDisc(entry, {
      data: { img, ready: (lba, n) => img.has(lba * 2048, (lba + n) * 2048), fetch: (lba, n) => img.load(lba * 2048, (lba + n) * 2048).then(() => true, () => false) },
    });
    this.requests = [];
  }
  prefetch() { return this.img.load(0, 64 * 2048); }     // volume descriptors + the start of the directory
  get busyTracks() { return []; }
}

export class CdRom {
  constructor({ sound, machine }) {
    this.sound = sound; this.getMachine = machine;
    this.entries = []; this.sources = new Map();
    this.pending = null;           // before the machine exists: the disc "in the drive"
    this.pendingOpen = false;
    this.view = { open: false, disc: null, playing: false, paused: false, holding: false, track: 0 };
    this.headphones = prefs.get('cdPhones', false);
    this.knob = prefs.get('cdKnob', 0.7);
    this.blinkT = 0;
    this.build();
  }

  // ------------------------------------------------------------ DOM
  build() {
    this.bay = el('div', { class: 'bay cd', id: 'cdDrive', title: 'The CD-ROM drive (drag a disc here, or click a disc in the box below)' });
    this.bay.innerHTML = `
      <div class="cd-face">
        <div class="cd-door"><div class="cd-door-lip"></div></div>
        <div class="cd-tray" aria-hidden="true"><div class="cd-tray-well"><div class="cd-disc"></div></div></div>
        <div class="cd-row">
          <span class="cd-jack" id="cdJack" role="switch" tabindex="0" aria-checked="false" title="Headphone jack: click to plug headphones in (CD audio then bypasses the Sound Blaster; the knob sets the level)"><i></i></span>
          <span class="cd-knob" id="cdKnob" role="slider" tabindex="0" aria-label="Headphone volume" aria-valuemin="0" aria-valuemax="100" title="Headphone volume (drag, scroll or arrow keys)"><i></i></span>
          <span class="cd-logo">CD-ROM <b>2X</b></span>
          <span class="led amber small cd-led" id="cdLed" title="Busy"></span>
          <button class="cd-btn" id="cdEject" aria-label="Open or close the CD-ROM tray" title="Open / close the tray"></button>
        </div>
      </div>`;
    this.led = this.bay.querySelector('#cdLed');
    this.box = el('div', { class: 'cdbox-wrap', id: 'cdBox' });
    this.box.innerHTML = `
      <h2>CD-ROMs <span class="h2-sub">drive D: (with ARMCD.SYS + ARMCDEX)</span></h2>
      <div class="cdbox-shelf" id="cdShelf"></div>
      <p class="hint small cd-status" id="cdStatus" aria-live="polite">Click a disc to put it in the CD-ROM drive, or drag it there.</p>
      <div class="cd-own"><label class="btn small">Choose ISO&hellip;<input type="file" id="cdIsoFile" accept=".iso,application/x-iso9660-image" hidden></label>
      <span class="hint small">Your own CD image (ISO 9660, data only) as drive D:. Drop an <code>.iso</code> on the drive or here. It is read from your disk as needed and never uploaded.</span></div>`;
  }
  mount(caseEl) {
    const bays = caseEl.querySelector('.bays');
    bays.prepend(this.bay);
    const serial = $('serialPanel');
    if (serial) serial.before(this.box); else document.querySelector('.peripherals')?.append(this.box);
    this.bindDrive();
    // the app layout (no case on screen): a CD button in its bar - puts the first disc in, or opens/closes the tray
    const bar = $('appbar'), kbd = $('abKbd');
    if (bar && kbd) {
      this.abBtn = el('button', { class: 'ab', id: 'abCd', title: 'CD-ROM drive: insert the disc / open or close the tray' });
      this.abBtn.innerHTML = 'CD <b id="abCdName">empty</b>';
      this.abBtn.onclick = () => { const cur = this.current(), open = this.m ? this.view.open : this.pendingOpen; if (!cur && !open && this.entries[0]) this.insert(this.entries[0]); else this.trayButton(); };
      kbd.before(this.abBtn);
    }
    this.render();
  }

  /** The drive is fitted or not (web/js/openbox.js): without it the bay has a blank plate. */
  setPresent(on) {
    this.present = on;
    this.bay.hidden = !on;
    if (!this.blank) {
      this.blank = el('div', { class: 'bay blank cd-blank', 'aria-hidden': 'true' }, el('div', { class: 'blank-plate' }, el('span', { text: 'NO CD-ROM DRIVE' })));
      this.bay.after(this.blank);
    }
    this.blank.hidden = on;
    if (this.abBtn) this.abBtn.hidden = !on;
    this.status();
  }

  load(images) {
    this.entries = (images && images.cdroms) || [];
    const shelf = $('cdShelf');
    shelf.textContent = '';
    for (const e of this.entries) shelf.append(this.caseEl(e));
    if (!this.entries.length) shelf.append(el('p', { class: 'hint small', text: 'No discs in this build.' }));
    this.credits();
  }

  caseEl(e) {
    const audio = e.tracks.filter((t) => t.type === 'audio');
    const wrap = el('div', { class: 'jewel-wrap' });
    const b = el('button', { class: 'jewel', draggable: 'true', 'data-disc': e.id, 'aria-label': `${e.title}: put it in the CD-ROM drive` });
    b.innerHTML = `
      <span class="jewel-spine"></span>
      <span class="jewel-inlay">
        <span class="inlay-grid"></span>
        <span class="inlay-shapes"><i class="s1"></i><i class="s2"></i><i class="s3"></i><i class="s4"></i></span>
        <span class="inlay-top">EUROPA MICRO SYSTEMS</span>
        <span class="inlay-title">ARM-DOS<br><em>Multimedia</em><br>Sampler <b>'93</b></span>
        <span class="inlay-sub">Programs &middot; Demos &middot; Pictures &middot; Texts<br>${audio.length} CD Audio Tracks</span>
        <span class="inlay-foot"><span class="inlay-cd">CD-ROM</span><span>MIXED MODE &middot; ISO 9660</span></span>
      </span>
      <span class="jewel-glare"></span>
      <span class="jewel-hinge"></span>`;
    b.onclick = () => this.insert(e);
    b.addEventListener('dragstart', (ev) => { ev.dataTransfer.setData('text/x-armdos-cd', e.id); ev.dataTransfer.effectAllowed = 'move'; });
    const back = el('div', { class: 'jewel-back' });
    back.innerHTML = `<div class="back-title">${esc(e.title)}</div>
      <ol class="back-tracks">
        <li value="1"><span class="bt-t">Data <small>(ISO 9660: DIR D:, MENU)</small></span></li>
        ${audio.map((t) => `<li value="${t.number}"><span class="bt-t">${esc(t.title)}</span> <span class="bt-len">${mmss(t.frames ? t.frames / 44100 : t.seconds)}</span>
          <span class="bt-credit">&ldquo;${esc(t.title)}&rdquo; ${esc(t.artist)} (<a href="https://incompetech.com/" target="_blank" rel="noopener">incompetech.com</a>)<br>Licensed under Creative Commons: <a href="${esc(t.licenceUrl)}" target="_blank" rel="noopener license">By Attribution 4.0 License</a> &middot; <a href="${esc(t.source)}" target="_blank" rel="noopener">source</a></span></li>`).join('')}
      </ol>`;
    wrap.append(b, back);
    return wrap;
  }

  credits() {
    const p = $('moreCredits');
    if (!p || p.querySelector('.cd-credit')) return;
    const titles = [];
    for (const e of this.entries) for (const t of e.tracks) if (t.type === 'audio') titles.push(t);
    if (!titles.length) return;
    const s = el('span', { class: 'cd-credit' });
    s.innerHTML = `CD audio on the Multimedia Sampler '93: ${titles.map((t) => `&ldquo;<a href="${esc(t.source)}">${esc(t.title)}</a>&rdquo;`).join(', ')}
      by Kevin MacLeod (<a href="https://incompetech.com/">incompetech.com</a>), licensed under Creative Commons:
      <a href="http://creativecommons.org/licenses/by/4.0/">By Attribution 4.0 License</a> (loudness-normalised). Pictures on the disc: NASA (public domain). `;
    p.prepend(s);
  }

  // ------------------------------------------------------------ the drive
  source(e) {
    let s = this.sources.get(e.id);
    if (!s) { s = new DiscSource(e, { onStatus: () => this.status() }); this.sources.set(e.id, s); }
    return s;
  }
  machineOptions() {
    const d = this.pending && !this.pendingOpen ? this.pending.disc : undefined;
    this.pending = null;
    return {
      ...(d ? { cdrom: d } : {}),
      onCdrom: (st) => this.onState(st),
      onCdActivity: (lba, n) => this.activity(lba, n),
    };
  }
  get m() { const m = this.getMachine(); return m && m.cdrom ? m : null; }
  /** the disc (entry) in the drive, or on the open tray */
  current() {
    const d = this.m ? this.m.cdrom.disc : this.pending?.disc;
    if (!d) return null;
    for (const s of this.sources.values()) if (s.disc === d) return s;
    return null;
  }

  insert(e) {
    if (this.present === false) { this.flash('There is no CD-ROM drive in this machine. Switch it off and open the case to fit one.'); return; }
    const src = this.source(e);
    src.prefetch();
    this.sound?.init?.();
    const m = this.m;
    if (!m) {                                         // no machine yet: it goes in at power-on
      this.pending = src; this.pendingOpen = true; this.render();
      setTimeout(() => { this.pendingOpen = false; this.render(); this.status(); }, 700);
      return;
    }
    if (m.cdrom.locked) { this.flash('The drive is locked by a program (PREVENT MEDIUM REMOVAL).'); return; }
    if (m.cdrom.disc === src.disc && !m.cdrom.trayOpen) return;
    // the tray comes out, the disc goes on it, the tray goes in
    const open = () => { this.view.open = true; this.view.disc = src.disc; this.render(); };
    if (!m.cdrom.trayOpen) { m.cdrom.openTray(); }
    open();
    setTimeout(() => { if (this.m && !this.m.cdrom.insert(src.disc)) this.flash('The drive is locked.'); this.sync(); }, 700);
  }
  /** The visitor's own ISO: check it, give it a jewel-case-free "disc", and put it in the drive. */
  async insertIso(file) {
    let vol = null;
    try { vol = await isoVolumeId(file); } catch (e) { console.warn(e); }
    if (!vol) { this.flash(`${file.name} isn't an ISO 9660 CD image. D: takes plain .iso files (a single data track); BIN/CUE and other formats aren't supported.`); return false; }
    if (file.size > 900 * 1048576) this.flash(`${file.name} is bigger than a CD (${Math.round(file.size / 1048576)} MB); trying anyway.`);
    const entry = { id: 'iso-' + Date.now(), title: `${file.name} (${vol})`, volumeId: vol, local: true,
      tracks: [{ number: 1, type: 'data', sectors: Math.ceil(file.size / 2048) }] };
    for (const [id, s] of this.sources) if (s instanceof LocalIsoSource && s.disc !== this.m?.cdrom.disc) this.sources.delete(id);
    this.sources.set(entry.id, new LocalIsoSource(entry, file));
    $('cdStatus')?.classList.remove('warn');
    this.insert(entry);
    return true;
  }
  trayButton() {
    this.sound?.init?.(); this.sound?.click?.('button');
    const m = this.m;
    if (!m) { this.pendingOpen = !this.pendingOpen; if (this.pendingOpen) this.pending = this.pending || null; this.render(); return; }
    if (!m.cdrom.trayButton()) this.flash('The drive is locked by a program: the tray stays shut.');
    this.sync();
  }
  sync() { const m = this.m; if (m) this.onState(m.cdrom.state()); }
  onState(st) {
    const v = this.view;
    v.open = st.trayOpen; v.disc = this.m ? this.m.cdrom.disc : null;
    v.playing = st.playing; v.paused = st.paused; v.holding = st.holding; v.track = st.track;
    this.render(); this.status();
  }
  activity(lba, n) {
    this.reads = (this.reads || 0) + 1;
    this.led.classList.add('on');
    clearTimeout(this.blinkT);
    this.blinkT = setTimeout(() => this.led.classList.remove('on'), n ? 120 : 400);
  }
  flash(msg) { const s = $('cdStatus'); if (s) { s.textContent = msg; s.classList.add('warn'); setTimeout(() => { s.classList.remove('warn'); this.status(); }, 4000); } }

  render() {
    const m = this.m, v = this.view;
    const open = m ? v.open : this.pendingOpen;
    const disc = m ? v.disc : this.pending?.disc;
    this.bay.classList.toggle('open', !!open);
    this.bay.classList.toggle('has-disc', !!disc);
    this.led.classList.toggle('play', !!(m && v.playing && !v.holding));
    this.led.classList.toggle('net', !!(m && v.holding));
    const jack = $('cdJack'), knob = $('cdKnob');
    if (jack) { jack.classList.toggle('plugged', this.headphones); jack.setAttribute('aria-checked', String(this.headphones)); }
    if (knob) { knob.style.setProperty('--turn', `${-135 + 270 * this.knob}deg`); knob.setAttribute('aria-valuenow', String(Math.round(this.knob * 100))); }
    if (m) { m.cdrom.headphones = this.headphones ? { volume: this.knob } : null; m.cdrom.knob = this.knob; }
    const nm = $('abCdName');
    if (nm) nm.textContent = open ? 'open' : disc ? (m && v.playing ? `\u266A${v.track}` : 'disc') : 'empty';
    for (const b of document.querySelectorAll('.jewel')) {
      const s = this.sources.get(b.dataset.disc);
      b.classList.toggle('in-drive', !!(s && disc && s.disc === disc));
    }
  }
  status() {
    const s = $('cdStatus'); if (!s || s.classList.contains('warn')) return;
    const src = this.current(), v = this.view, m = this.m;
    const open = m ? v.open : this.pendingOpen;
    let t;
    if (this.present === false) t = 'This machine has no CD-ROM drive: open the case to fit one.';
    else if (open) t = src ? `The tray is open with ${src.entry.title} on it.` : 'The tray is open and empty.';
    else if (!src) t = 'The drive is empty. Click a disc to put it in the CD-ROM drive, or drag it there.';
    else {
      const busy = src.busyTracks;
      t = `In the drive: ${src.entry.title}.`;
      if (busy.length) t += ` Loading audio track ${busy.join(', ')}\u2026`;
      else if (m && v.playing) t += ` Playing track ${v.track}${v.paused ? ' (paused)' : ''}${this.headphones ? ', on the headphones' : ''}.`;
      else if (!m) t += ' Switch the machine on; then DEVICE=C:\\DOS\\ARMCD.SYS in CONFIG.SYS and ARMCDEX make it drive D:.';
    }
    s.textContent = t;
  }
  powerOff() { this.led.classList.remove('on', 'play', 'net'); }

  bindDrive() {
    $('cdEject').onclick = (e) => { e.stopPropagation(); this.trayButton(); };
    const jack = $('cdJack'), knob = $('cdKnob');
    const toggleJack = () => { this.headphones = !this.headphones; prefs.set('cdPhones', this.headphones); this.sound?.click?.('button'); this.render(); this.status(); };
    jack.onclick = toggleJack;
    jack.onkeydown = (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); toggleJack(); } };
    const setKnob = (v) => { this.knob = Math.max(0, Math.min(1, v)); prefs.set('cdKnob', this.knob); this.render(); };
    knob.addEventListener('wheel', (e) => { e.preventDefault(); setKnob(this.knob - Math.sign(e.deltaY) * 0.05); }, { passive: false });
    knob.onkeydown = (e) => {
      const d = { ArrowUp: .05, ArrowRight: .05, ArrowDown: -.05, ArrowLeft: -.05, PageUp: .2, PageDown: -.2, Home: -1, End: 1 }[e.key];
      if (d !== undefined) { e.preventDefault(); setKnob(this.knob + d); }
    };
    knob.onpointerdown = (e) => {
      e.preventDefault(); knob.setPointerCapture(e.pointerId);
      const y0 = e.clientY, x0 = e.clientX, k0 = this.knob;
      knob.onpointermove = (ev) => setKnob(k0 + ((y0 - ev.clientY) + (ev.clientX - x0)) / 120);
      knob.onpointerup = knob.onpointercancel = () => { knob.onpointermove = null; };
    };
    const bay = this.bay;
    $('cdIsoFile').onchange = (e) => { const f = e.target.files[0]; e.target.value = ''; if (f) this.insertIso(f); };
    // an .iso dropped anywhere on the page goes into the CD-ROM drive (before the diskette handlers see it)
    const isoIn = (e) => [...(e.dataTransfer?.files || [])].find((f) => /\.iso$/i.test(f.name));
    window.addEventListener('drop', (e) => {
      const f = isoIn(e);
      if (!f) return;
      e.preventDefault(); bay.classList.remove('drag-over'); this.box.classList.remove('drag-over');
      const veil = $('dropVeil'); if (veil) veil.hidden = true;
      this.insertIso(f);
    }, true);
    for (const t of [bay, this.box]) {
      t.addEventListener('dragover', (e) => { if ([...(e.dataTransfer?.types || [])].includes('Files')) { e.preventDefault(); t.classList.add('drag-over'); } });
      t.addEventListener('dragleave', () => t.classList.remove('drag-over'));
    }
    const isCd = (e) => [...(e.dataTransfer?.types || [])].includes('text/x-armdos-cd');
    bay.addEventListener('dragover', (e) => { if (isCd(e)) { e.preventDefault(); bay.classList.add('drag-over'); } });
    bay.addEventListener('dragleave', () => bay.classList.remove('drag-over'));
    bay.addEventListener('drop', (e) => {
      bay.classList.remove('drag-over');
      if (!isCd(e)) return;
      e.preventDefault();
      const id = e.dataTransfer.getData('text/x-armdos-cd'), en = this.entries.find((x) => x.id === id);
      if (en) this.insert(en);
    });
  }
}
