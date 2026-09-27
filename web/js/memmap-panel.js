// The inspector's MEMORY MAP: a live heat map of the machine's address space.
// Every cell glows amber for instructions EXECUTED there, green for READS and
// magenta-red for WRITES, fading over about half a second, like the memory
// visualisers of 8-bit days or a logic analyser's state display.
//
//   top:    all 16 MB of RAM as 64x64 cells of 4 KB (one row = 256 KB), with
//           the BIOS ROM, the ISA I/O ports and the font RAM as side strips
//   bottom: a 1 MB window as 64x64 cells of 256 bytes (one row = 16 KB),
//           conventional memory by default; click the top map to move it
//   right:  the regions, read from guest memory (emu/memmap.mjs memoryRegions:
//           IVT/BDA, DOS kernel, each MCB block with its owner's name, video
//           RAM, the BIOS's HMA, HIMEM's XMS blocks, ROM)
//
// The counters are emu/memmap.mjs's MemActivity: attached to the machine only
// while this section is open, the inspector is open and the tab is visible;
// otherwise the emulator runs exactly as without it. Drawn at ~15 Hz from an
// ImageData per map (one pixel per cell, scaled up) plus a grid and labels.

import { MemActivity, memoryRegions, regionAt, NCELLS, ROM0, IO0, FONT0, portName } from '../emu/memmap.js';
import { $, hex, el, prefs } from './util.js';

const TAU = 0.18;                   // s: heat time constant (x0.06 after 0.5 s)
const DECADES = 4;                  // dynamic range of the log brightness scale
const FRAME_MS = 66;
// colour of each channel at full heat (added together: exec+read = yellow, all = white)
const CH = [[255, 150, 30], [60, 255, 110], [255, 40, 150]];
// dim base tint per region kind
const KIND = {
  sys: [30, 26, 22], dos: [34, 27, 18], prog: [22, 25, 30], env: [18, 20, 24], free: [11, 11, 12],
  video: [16, 24, 34], hole: [7, 7, 8], bios: [32, 25, 18], xms: [25, 20, 34], xmsfree: [13, 12, 16],
  none: [4, 4, 4], rom: [24, 20, 12], io: [18, 22, 30], font: [18, 22, 30], cur: [36, 44, 58],
};
const LABEL = {
  sys: '#c9a46a', dos: '#ffb340', prog: '#74f29a', env: '#4fa76a', free: '#6d6d6d', video: '#6fc6ff',
  hole: '#5b5b5b', bios: '#e89a4a', xms: '#b692ff', xmsfree: '#6f668a', none: '#4a4a4a', rom: '#e0b050',
};

export class MemMapPanel {
  /** ctl: { machine() -> Machine|null, running() -> bool } */
  constructor(ctl) {
    this.ctl = ctl;
    this.act = null;
    this.heat = [new Float32Array(NCELLS), new Float32Array(NCELLS), new Float32Array(NCELLS)];
    this.hp = new Float32Array(65536);  // written-line presence per RAM cell: recent share of frames with a store (0..1)
    this.ref = [[1, 1], [1, 1]];     // [overview, zoom] x [exec, access] brightness references
    this.zoomBase = 0;               // first address of the 1 MB window (or 0xFFF00000 = ROM)
    this.regions = []; this.regCache = {}; this.regAt = 0;
    this.hover = null;
    this.last = 0;
    this.build();
    this.setOpen(prefs.get('memmap', false));
    this.timer = setInterval(() => this.tick(), FRAME_MS);
    document.addEventListener('visibilitychange', () => this.tick());
  }

  // ------------------------------------------------------------------ DOM
  build() {
    const memCap = $('memAddr')?.closest('.section-cap');
    this.btn = el('button', { class: 'hwkey small', id: 'mmToggle', 'aria-expanded': 'false', 'aria-controls': 'memmap', text: 'OPEN' });
    this.stateEl = el('span', { class: 'mm-state', id: 'mmState', text: '' });
    const cap = el('div', { class: 'section-cap mm-cap' }, el('span', { text: 'MEMORY MAP' }), this.stateEl, this.btn);
    this.all = el('canvas', { class: 'mm-all', id: 'mmAll', 'aria-label': 'Activity map of all 16 MB, 4 KB cells' });
    this.zoom = el('canvas', { class: 'mm-zoom', id: 'mmZoom', 'aria-label': 'Activity map of a 1 MB window, 256-byte cells' });
    this.zoomCap = el('span', { class: 'mm-zcap', text: '' });
    this.readout = el('div', { class: 'mm-readout', id: 'mmHover', text: '' });
    this.top = el('div', { class: 'mm-top', id: 'mmTop' });
    const legend = el('div', { class: 'mm-legend' },
      el('span', { class: 'k x', text: 'EXECUTE' }), el('span', { class: 'k r', text: 'READ' }), el('span', { class: 'k w', text: 'WRITE' }),
      el('span', { class: 'mm-note', text: 'log brightness · 0.5 s decay' }));
    this.box = el('div', { class: 'win mm', id: 'memmap', hidden: true },
      el('div', { class: 'mm-vcap' }, el('span', { text: '16 MB · 4 KB CELLS · ROW = 256 KB' }), el('span', { class: 'mm-strips', text: 'ROM  I/O  FONT' })),
      this.all,
      el('div', { class: 'mm-vcap' }, this.zoomCap, el('span', { class: 'mm-hint', text: 'click: hex view' })),
      this.zoom, legend, this.readout, this.top);
    if (memCap) memCap.before(cap, this.box);
    else $('inspBody')?.append(cap, this.box);
    this.btn.onclick = () => this.setOpen(!this.open);
    for (const [cv, which] of [[this.all, 'all'], [this.zoom, 'zoom']]) {
      cv.addEventListener('mousemove', (e) => { this.hover = this.hit(which, e); this.renderReadout(); });
      cv.addEventListener('mouseleave', () => { this.hover = null; this.renderReadout(); });
      cv.addEventListener('click', (e) => this.click(which, e));
    }
    this.small = document.createElement('canvas');       // one pixel per cell, scaled up
    this.sctx = this.small.getContext('2d');
    this.layout();
    this.setZoom(0);
    this.renderReadout();
  }

  // geometry (CSS pixels): overview grid 64x64 at P1, strips to its right; zoom grid at P2 + label column
  layout() {
    const W = 404;
    this.W = W;
    this.P1 = 4; this.P2 = 5;
    this.g1 = { x: 0, y: 0, n: 64, p: this.P1 };                 // overview grid
    const sx = 64 * this.P1 + 10;
    this.strips = {                                               // x, rows, cols, first cell, cells per pixel
      rom: { x: sx, cols: 4, rows: 64, c0: ROM0, agg: 16, kind: 'rom' },
      io: { x: sx + 4 * this.P1 + 8, cols: 1, rows: 65, c0: IO0, agg: 1, kind: 'io' },
      font: { x: sx + 5 * this.P1 + 16, cols: 1, rows: 32, c0: FONT0, agg: 1, kind: 'font' },
    };
    this.lab1 = sx + 6 * this.P1 + 26;                            // overview labels from here
    this.T1 = 12;                                                 // caption band above the overview
    this.H1 = this.T1 + 65 * this.P1 + 1;
    this.g2 = { x: 0, y: 0, n: 64, p: this.P2 };
    this.lab2 = 64 * this.P2 + 10;
    this.H2 = 64 * this.P2 + 1;
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    this.dpr = dpr;
    for (const [cv, h] of [[this.all, this.H1], [this.zoom, this.H2]]) {
      cv.width = Math.round(W * dpr); cv.height = Math.round(h * dpr);
      cv.style.aspectRatio = `${W} / ${h}`;
    }
    this.c1 = this.all.getContext('2d'); this.c2 = this.zoom.getContext('2d');
    this.grid1 = this.gridCanvas(64, 64, this.P1); this.grid2 = this.gridCanvas(64, 64, this.P2);
  }
  gridCanvas(cols, rows, p) {          // dark lines between the cells (the LED-matrix look)
    const d = this.dpr, c = document.createElement('canvas');
    c.width = Math.round(cols * p * d) + 1; c.height = Math.round(rows * p * d) + 1;
    const x = c.getContext('2d');
    x.fillStyle = 'rgba(0,0,0,0.78)';
    for (let k = 0; k <= cols; k++) x.fillRect(Math.round(k * p * d), 0, Math.max(1, Math.round(d)), c.height);
    for (let k = 0; k <= rows; k++) x.fillRect(0, Math.round(k * p * d), c.width, Math.max(1, Math.round(d)));
    x.fillStyle = 'rgba(0,0,0,0.35)';            // every 4th/16th line a little stronger: 1 MB / 64 KB boundaries
    for (let k = 0; k <= rows; k += 4) x.fillRect(0, Math.round(k * p * d), c.width, Math.max(1, Math.round(d)));
    return c;
  }

  setOpen(on) {
    this.open = on;
    this.box.hidden = !on;
    this.btn.textContent = on ? 'CLOSE' : 'OPEN';
    this.btn.setAttribute('aria-expanded', String(on));
    this.btn.classList.toggle('lit', on);
    prefs.set('memmap', on);
    if (on) { this.last = 0; this.tick(); } else this.sync(false);
  }
  setZoom(base) {
    this.zoomBase = base >>> 0;
    const rom = this.zoomBase === 0xFFF00000;
    this.zoomCap.textContent = rom ? 'BIOS ROM FFF00000-FFFFFFFF · 256-BYTE CELLS' : `${hex(this.zoomBase, 6)}-${hex(this.zoomBase + 0xFFFFF, 6)} · 256-BYTE CELLS · ROW = 16 KB`;
  }

  // ------------------------------------------------------------------ counters on/off
  visible() {
    return this.open && !document.hidden && !$('inspector')?.classList.contains('closed') && !!this.ctl.machine();
  }
  sync(want) {
    const m = this.ctl.machine();
    if (want && m) {
      if (!this.act) this.act = new MemActivity();
      if (this.act.machine !== m || !this.act.attached) { this.act.attach(m); this.heat.forEach((h) => h.fill(0)); this.hp.fill(0); }
    } else if (this.act) this.act.detach();
  }

  tick() {
    const want = this.visible();
    this.sync(want);
    if (!want) { this.stateEl.textContent = this.open ? 'PAUSED' : ''; return; }
    const now = performance.now();
    if (now - this.last < FRAME_MS * 0.8) return;
    const dt = this.last ? Math.min(0.5, (now - this.last) / 1000) : FRAME_MS / 1000;
    this.last = now;
    const m = this.ctl.machine();
    if (now - this.regAt > 1000 || !this.regions.length) {
      this.regAt = now;
      try { this.regions = memoryRegions(m, this.regCache); } catch { this.regions = []; }
      this.topDue = true;
    }
    // counts since the last frame -> rates -> exponentially decaying heat
    const k = Math.exp(-dt / TAU), g = (1 - k) / dt;
    this.act.drain((x, r, w, wp) => {
      // (values below 1e-3/s are flushed to 0: no denormals, and "never touched" stays exactly 0)
      const [HX, HR, HW] = this.heat, hp = this.hp, k1 = 1 - k;
      for (let i = 0; i < NCELLS; i++) {
        let v = HX[i] * k + x[i] * g; HX[i] = v < 1e-3 ? 0 : v;
        v = HR[i] * k + r[i] * g; HR[i] = v < 1e-3 ? 0 : v;
        v = HW[i] * k + w[i] * g; HW[i] = v < 1e-3 ? 0 : v;
      }
      for (let i = 0; i < 65536; i++) { const v = wp[i] > 0 ? hp[i] * k + k1 : hp[i] * k; hp[i] = v < 1e-3 ? 0 : v; }
    });
    const jit = !!m.cpu.jit, running = this.ctl.running();
    this.stateEl.textContent = !running ? 'HALTED' : jit ? `SAMPLING 1/${this.act.scale}` : 'JIT OFF: WRITES + I/O ONLY';
    this.pc = m.cpu.pc >>> 0;
    this.drawAll(); this.drawZoom();
    if (this.hover) this.renderReadout();
    if (this.topDue || (this.frame = (this.frame || 0) + 1) % 8 === 0) { this.topDue = false; this.renderTop(); }
  }

  // ------------------------------------------------------------------ drawing
  // brightness 0..1 on a log scale relative to ref (DECADES decades of range)
  static lum(v, ref) { return v <= 0 ? 0 : Math.max(0, Math.min(1, 1 + Math.log10(v / ref) / DECADES)); }
  // aggregate heat of cells [c0, c0+n) for the three channels -> out[0..2]
  sumCells(c0, n, out) {
    const [X, R, W] = this.heat, HP = this.hp; let x = 0, r = 0, w = 0, p = 0;
    for (let i = c0; i < c0 + n; i++) { x += X[i]; r += R[i]; w += W[i]; if (i < 65536) p += HP[i]; }
    out[0] = x; out[1] = r; out[2] = w; out[3] = p / n;
  }
  kindAt(a) { if (a >= 0xFFF00000) return 'rom'; const g = regionAt(this.regions, a); return g ? g.kind : 'none'; }
  // fill an ImageData pixel from base tint + channel heat
  static px(d, o, base, lx, lr, lw) {
    let r = base[0], gg = base[1], b = base[2];
    r += lx * CH[0][0] + lr * CH[1][0] + lw * CH[2][0];
    gg += lx * CH[0][1] + lr * CH[1][1] + lw * CH[2][1];
    b += lx * CH[0][2] + lr * CH[1][2] + lw * CH[2][2];
    d[o] = r > 255 ? 255 : r; d[o + 1] = gg > 255 ? 255 : gg; d[o + 2] = b > 255 ? 255 : b; d[o + 3] = 255;
  }
  // cells of a view -> values (vals[4*j+c]: exec, read, write, written-line share) and the frame's peaks
  paint(ctx, cols, rows, cellFn, vals, refIdx, gx, gy, p, grid) {
    const n = cols * rows, tmp = [0, 0, 0, 0];
    let mx = 0, ma = 0;
    for (let j = 0; j < n; j++) {
      if (!cellFn(j, tmp)) { vals[4 * j] = -1; continue; }
      vals[4 * j] = tmp[0]; vals[4 * j + 1] = tmp[1]; vals[4 * j + 2] = tmp[2]; vals[4 * j + 3] = tmp[3];
      if (tmp[0] > mx) mx = tmp[0];
      if (tmp[1] > ma) ma = tmp[1];
      if (tmp[2] > ma) ma = tmp[2];
    }
    const ref = this.ref[refIdx];
    ref[0] = Math.max(mx, ref[0] * 0.97, 1); ref[1] = Math.max(ma, ref[1] * 0.97, 1);
    if (this.small.width !== cols || this.small.height !== rows) { this.small.width = cols; this.small.height = rows; this.img = null; }
    if (!this.img || this.img.width !== cols || this.img.height !== rows) this.img = this.sctx.createImageData(cols, rows);
    const d = this.img.data;
    for (let j = 0; j < n; j++) {
      const o = 4 * j;
      if (vals[4 * j] < 0) { d[o] = d[o + 1] = d[o + 2] = 0; d[o + 3] = 0; continue; }
      const base = KIND[this.kinds[j]] || KIND.none;
      // writes: the sampled rate, but a line stored to in recent frames glows at least by that share
      const p = vals[4 * j + 3], lw = Math.max(MemMapPanel.lum(vals[4 * j + 2], ref[1]), p > 0.03 ? 0.22 + 0.55 * p : 0);
      MemMapPanel.px(d, o, base, MemMapPanel.lum(vals[4 * j], ref[0]), MemMapPanel.lum(vals[4 * j + 1], ref[1]), lw);
    }
    this.sctx.putImageData(this.img, 0, 0);
    const D = this.dpr;
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(this.small, 0, 0, cols, rows, Math.round(gx * D), Math.round(gy * D), Math.round(cols * p * D), Math.round(rows * p * D));
    if (this.glow) {                       // phosphor bloom: the same image blurred, added on top
      ctx.save(); ctx.globalCompositeOperation = 'lighter'; ctx.globalAlpha = 0.55; ctx.filter = `blur(${(p * D * 0.9).toFixed(1)}px)`;
      ctx.imageSmoothingEnabled = true;
      ctx.drawImage(this.small, 0, 0, cols, rows, Math.round(gx * D), Math.round(gy * D), Math.round(cols * p * D), Math.round(rows * p * D));
      ctx.restore();
    }
    if (grid) ctx.drawImage(grid, Math.round(gx * D), Math.round(gy * D));
  }

  // a bright frame around the cell the PC is in
  pcMark(ctx, j, cols, gx, gy, p) {
    if (j < 0) return;
    const D = this.dpr, x = gx + (j % cols) * p, y = gy + Math.floor(j / cols) * p;
    ctx.save(); ctx.setTransform(D, 0, 0, D, 0, 0);
    ctx.strokeStyle = '#fff6dc'; ctx.lineWidth = 1; ctx.shadowColor = 'rgba(255,240,200,.95)'; ctx.shadowBlur = 5;
    ctx.strokeRect(x - 0.5, y - 0.5, p + 1, p + 1);
    ctx.restore();
  }

  drawAll() {
    const ctx = this.c1, D = this.dpr, P = this.P1, T = this.T1;
    if (this.glow === undefined) this.glow = 'filter' in ctx;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.fillStyle = '#070808'; ctx.fillRect(0, 0, this.all.width, this.all.height);
    // 16 MB: cell j = 4 KB page j
    const pcPage = this.pc < 0x1000000 ? this.pc >>> 12 : -1;
    this.kinds = this.kinds1 || (this.kinds1 = new Array(4096));
    for (let j = 0; j < 4096; j++) this.kinds[j] = this.kindAt(j << 12);
    this.vals1 = this.vals1 || new Float32Array(4 * 4096);
    this.paint(ctx, 64, 64, (j, o) => { this.sumCells(j << 4, 16, o); return true; }, this.vals1, 0, 0, T, P, this.grid1);
    this.pcMark(ctx, pcPage, 64, 0, T, P);
    // strips (same brightness reference as the RAM)
    for (const s of Object.values(this.strips)) {
      const n = s.cols * s.rows;
      this.kinds = new Array(n).fill(s.kind);
      const pcCell = s.kind === 'rom' && this.pc >= 0xFFF00000 ? ((this.pc >>> 12) & 0xFF) : -1;
      const vals = new Float32Array(4 * n), ref = this.ref[0].slice();
      const lim = s.kind === 'io' ? 65 : s.kind === 'font' ? 32 : 4096;
      this.paint(ctx, s.cols, s.rows, (j, o) => { const c = j * s.agg; if (c >= lim) return false; this.sumCells(s.c0 + c, s.agg, o); return true; }, vals, 0, s.x, T, P, null);
      this.ref[0] = ref;                   // (strips do not move the reference)
      this.pcMark(ctx, pcCell, s.cols, s.x, T, P);
    }
    ctx.setTransform(D, 0, 0, D, 0, 0);
    ctx.font = '700 8px "Archivo", "Helvetica Neue", Arial, sans-serif'; ctx.textBaseline = 'top'; ctx.fillStyle = '#8b9199';
    ctx.fillText('0', 0, 1); ctx.fillText('ROM', this.strips.rom.x, 1); ctx.fillText('IO', this.strips.io.x - 2, 1); ctx.fillText('F', this.strips.font.x, 1);

    // zoom window frame
    ctx.strokeStyle = '#ffd27a'; ctx.lineWidth = 1; ctx.shadowColor = 'rgba(255,190,80,.9)'; ctx.shadowBlur = 4;
    if (this.zoomBase === 0xFFF00000) ctx.strokeRect(this.strips.rom.x - 1, T - 0.5, this.strips.rom.cols * P + 2, 64 * P + 1);
    else { const r0 = (this.zoomBase >>> 18); ctx.strokeRect(0.5, T + r0 * P + 0.5, 64 * P, 4 * P); }
    ctx.shadowBlur = 0;
    // labels: big regions only (the zoom shows the rest)
    const big = [];
    let conv = false;
    for (const g of this.regions) {
      if (g.start >= 0x1000000) continue;
      if (g.end <= 0xA0000) { if (!conv) { conv = true; big.push({ start: 0, end: 0xA0000, name: '640K DOS', kind: 'dos' }); } continue; }
      if (g.end - g.start >= 0x40000 || g.kind === 'xms' || g.kind === 'bios') big.push(g);
      else if (g.start === 0xA0000) big.push({ start: 0xA0000, end: 0x100000, name: 'video, ROMs', kind: 'video' });
    }
    this.labels(ctx, big, (a) => T + (a >>> 18) * P, (a) => T + (((a - 1) >>> 18) + 1) * P, this.lab1, T + 64 * P + 1, 9, 8);
  }

  drawZoom() {
    const ctx = this.c2, D = this.dpr, P = this.P2, base = this.zoomBase, rom = base === 0xFFF00000;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.fillStyle = '#070808'; ctx.fillRect(0, 0, this.zoom.width, this.zoom.height);
    const c0 = rom ? ROM0 : base >>> 8;
    const pcCell = rom ? (this.pc >= 0xFFF00000 ? (this.pc >>> 8) & 0xFFF : -1) : (this.pc >= base && this.pc < base + 0x100000 ? (this.pc - base) >>> 8 : -1);
    // the block of the program that is running (pc inside it) is tinted brighter
    let cur = !rom && this.pc < 0xA0000 ? regionAt(this.regions, this.pc) : null;
    if (cur && cur.kind !== 'prog') cur = null;
    this.kinds = this.kinds2 || (this.kinds2 = new Array(4096));
    for (let j = 0; j < 4096; j++) {
      const a = base + (j << 8);
      this.kinds[j] = rom ? 'rom' : cur && a >= cur.start && a < cur.end ? 'cur' : this.kindAt(a);
    }
    this.vals2 = this.vals2 || new Float32Array(4 * 4096);
    const H = this.heat;
    const HP = this.hp;
    this.paint(ctx, 64, 64, (j, o) => { const c = c0 + j; o[0] = H[0][c]; o[1] = H[1][c]; o[2] = H[2][c]; o[3] = c < 65536 ? HP[c] : 0; return true; }, this.vals2, 1, 0, 0, P, this.grid2);
    this.pcMark(ctx, pcCell, 64, 0, 0, P);
    ctx.setTransform(D, 0, 0, D, 0, 0);
    const regs = rom ? [{ start: 0xFFF00000, end: 0x100000000, name: 'BIOS ROM', kind: 'rom' }]
      : this.regions.filter((g) => g.end > base && g.start < base + 0x100000);
    const y = (a) => ((Math.max(a, base) - base) >>> 14) * P;
    const y2 = (a) => (((Math.min(a, base + 0x100000) - 1 - base) >>> 14) + 1) * P;
    this.labels(ctx, regs.map((g) => (g === cur ? { ...g, name: '▸ ' + g.name } : g)), y, y2, this.lab2, 64 * P + 1, 10);
  }

  // region labels in the right-hand column: a bracket for the rows it covers, the name at its start
  labels(ctx, regs, yOf, yEnd, x, maxY, gap, px = 9) {
    ctx.font = `600 ${px}px "IBM Plex Mono", ui-monospace, monospace`;
    ctx.textBaseline = 'top';
    const room = this.W - x - 8, placed = [];
    for (const g of regs) {
      const y0 = yOf(g.start), y1 = Math.max(y0 + 2, yEnd(g.end));
      const col = LABEL[g.kind] || '#999';
      ctx.fillStyle = col; ctx.globalAlpha = 0.9;
      ctx.fillRect(x - 5, y0 + 0.5, 1.5, y1 - y0 - 1);        // bracket over the rows it covers
      ctx.globalAlpha = 1;
      if (y0 > maxY - 8) continue;
      const size = g.end - g.start, prev = placed[placed.length - 1];
      if (prev && y0 < prev.y + gap) {                          // collision: the bigger region keeps the line
        if (size > prev.size && (placed.length < 2 || placed[placed.length - 2].y + gap <= prev.y)) { prev.g = g; prev.size = size; prev.col = col; }
        continue;
      }
      placed.push({ y: y0, g, size, col });
    }
    for (const p of placed) {                                   // (hover shows the ones left out)
      let name = p.g.name;
      while (name.length > 3 && ctx.measureText(name).width > room) name = name.slice(0, -2) + '…';
      ctx.fillStyle = p.col;
      ctx.fillText(name, x, p.y);
    }
  }

  // ------------------------------------------------------------------ interaction
  hit(which, e) {
    const cv = which === 'all' ? this.all : this.zoom, r = cv.getBoundingClientRect();
    const sx = (e.clientX - r.left) * (this.W / r.width), sy = (e.clientY - r.top) * (this.W / r.width);
    if (which === 'zoom') {
      const P = this.P2, col = Math.floor(sx / P), row = Math.floor(sy / P);
      if (col < 0 || col > 63 || row < 0 || row > 63) return null;
      const j = row * 64 + col;
      if (this.zoomBase === 0xFFF00000) return { a: (0xFFF00000 + (j << 8)) >>> 0, len: 256, cell: ROM0 + j, n: 1 };
      return { a: this.zoomBase + (j << 8), len: 256, cell: (this.zoomBase >>> 8) + j, n: 1 };
    }
    const P = this.P1, row = Math.floor((sy - this.T1) / P);
    if (sx < 64 * P) {
      const col = Math.floor(sx / P);
      if (col < 0 || row < 0 || row > 63) return null;
      const j = row * 64 + col;
      return { a: j << 12, len: 4096, cell: j << 4, n: 16 };
    }
    for (const s of Object.values(this.strips)) {
      if (sx >= s.x && sx < s.x + s.cols * P) {
        const j = row * s.cols + Math.floor((sx - s.x) / P), c = j * s.agg;
        if (row < 0 || row >= s.rows) return null;
        if (s.kind === 'rom') return { a: (0xFFF00000 + (j << 12)) >>> 0, len: 4096, cell: ROM0 + c, n: 16, strip: 'rom' };
        if (s.kind === 'io' && c < 65) return { port: c < 64 ? c << 4 : 0x400, cell: IO0 + c, n: 1, strip: 'io' };
        if (s.kind === 'font' && c < 32) return { a: 0x11000000 + (c << 8), len: 256, cell: FONT0 + c, n: 1, strip: 'font' };
      }
    }
    return null;
  }
  click(which, e) {
    const h = this.hit(which, e);
    if (!h) return;
    if (which === 'all') {
      if (h.strip === 'rom') this.setZoom(0xFFF00000);
      else if (!h.strip) this.setZoom((h.a >>> 20) << 20);
      this.tick();
      return;
    }
    const inp = $('memAddr');                     // the inspector's hex viewer follows
    if (inp && h.a !== undefined) { inp.value = hex(h.a, 1); inp.dispatchEvent(new Event('change')); }
  }
  rate(v) {
    if (v < 1) return '0';
    if (v >= 1e6) return (v / 1e6).toFixed(v >= 1e8 ? 0 : 1) + 'M';
    if (v >= 1e3) return (v / 1e3).toFixed(v >= 1e5 ? 0 : 1) + 'k';
    return v.toFixed(0);
  }
  renderReadout() {
    const h = this.hover;
    if (!h) { this.readout.innerHTML = '<span class="mm-dim">hover a cell · click the 16 MB map to move the window</span>'; return; }
    const o = [0, 0, 0, 0];
    this.sumCells(h.cell, h.n, o);
    let where, what;
    if (h.port !== undefined) {
      where = h.port < 0x400 ? `port ${hex(h.port, 3)}-${hex(h.port + 15, 3)}` : 'ports 400+';
      what = (h.port < 0x400 ? portName(h.port, h.port + 15) : '') || 'I/O';
    } else {
      where = `${hex(h.a, h.a >= 0x1000000 ? 8 : 6)}-${hex(h.a + h.len - 1, h.a >= 0x1000000 ? 8 : 6)}`;
      if (h.strip === 'font') what = 'VGA font RAM';
      else {
        const g = regionAt(this.regions, h.a) || (h.a >= 0xFFF00000 ? { name: 'BIOS ROM' } : null);
        what = g ? g.name + (g.start !== undefined && g.end - g.start >= 1024 ? ` (${hex(g.start, 5)}, ${Math.round((g.end - g.start) / 1024)}K)` : '') : '';
      }
    }
    const pcIn = h.a !== undefined && this.pc >= h.a && this.pc < h.a + h.len;
    this.readout.innerHTML = `<span class="a">${where}</span> <span class="o">${escapeHtml(what)}</span>${pcIn ? ' <span class="pc">◂ PC</span>' : ''}<br>` +
      `<span class="x">X ${this.rate(o[0])}/s</span> <span class="r">R ${this.rate(o[1])}/s</span> <span class="w">W ${this.rate(o[2])}/s</span>` +
      (o[3] > 0.03 ? ` <span class="w">· stored to in ${Math.round(o[3] * 100)}% of frames</span>` : '');
  }
  // the busiest regions (sum of the heat in their cells)
  renderTop() {
    const regs = this.regions;
    if (!regs.length) { this.top.textContent = ''; return; }
    const [X, R, W] = this.heat, sums = new Map();
    const add = (g, i) => { let s = sums.get(g); if (!s) sums.set(g, s = [0, 0, 0]); s[0] += X[i]; s[1] += R[i]; s[2] += W[i]; };
    let gi = 0;
    for (let i = 0; i < 65536; i++) {                     // regions are sorted: walk them with the cells
      if (X[i] === 0 && R[i] === 0 && W[i] === 0) continue;
      const a = i << 8;
      while (gi < regs.length && regs[gi].end <= a) gi++;
      if (gi < regs.length && regs[gi].start <= a) add(regs[gi], i);
    }
    const romG = regs[regs.length - 1];
    if (romG && romG.kind === 'rom') for (let i = ROM0; i < ROM0 + 4096; i++) if (X[i] || R[i] || W[i]) add(romG, i);
    const io = { name: 'I/O ports', kind: 'io' };
    for (let i = IO0; i < IO0 + 65; i++) if (R[i] || W[i]) add(io, i);
    const list = [...sums].sort((a, b) => (b[1][0] + b[1][1] + b[1][2]) - (a[1][0] + a[1][1] + a[1][2])).slice(0, 6);
    let mx = 1; for (const [, s] of list) mx = Math.max(mx, s[0], s[1], s[2]);
    const bar = (v, c) => `<i class="${c}" style="width:${(100 * MemMapPanel.lum(v, mx)).toFixed(0)}%"></i>`;
    this.top.innerHTML = '<div class="mm-th"><span>BUSIEST</span><span>X · R · W per second</span></div>' + list.map(([g, s]) =>
      `<div class="mm-tr"><span class="n" style="color:${LABEL[g.kind] || '#8fb4d8'}">${escapeHtml(g.name)}${g.start !== undefined && g.kind !== 'rom' ? ` <em>${hex(g.start, 5)}</em>` : ''}</span>` +
      `<span class="b">${bar(s[0], 'x')}${bar(s[1], 'r')}${bar(s[2], 'w')}</span>` +
      `<span class="v">${this.rate(s[0])} · ${this.rate(s[1])} · ${this.rate(s[2])}</span></div>`).join('');
  }
}

function escapeHtml(s) { return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }
