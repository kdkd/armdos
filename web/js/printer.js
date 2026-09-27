// The dot-matrix printer on LPT1: an Epson FX-80 compatible 9-pin printer.
// Bytes from the emulator's onPrint are queued and "printed" at a believable
// 9-pin speed onto continuous green-bar paper: each glyph of the IBM 8x8 font
// becomes a column of round ink dots, bit-image graphics (ESC K/L/Y/Z/*/^) put
// down 8 (or 9) pins per column, one left-to-right pass of the head at a time.
//
// ESC/P as on the FX-80: ESC @ reset; ESC E/F emphasized, ESC G/H double-strike,
// ESC 4/5 italic, ESC - n underline, ESC W n / SO / DC4 double width, SI / ESC SI /
// DC2 condensed, ESC M/P elite/pica, ESC S n / T super/subscript, ESC ! n master
// select; ESC 0/1/2 1/8" 7/72" 1/6" spacing, ESC 3 n (n/216"), ESC A n (n/72"),
// ESC J n (feed n/216" now), ESC C n / C 0 n page length, ESC N n / O skip over
// perforation, ESC l / Q margins, ESC $ / \ head position, ESC D tabs, and the
// bit images ESC K (60 dpi), L (120), Y (120 double speed), Z (240), ESC * m
// (m = 0..6: 60, 120, 120, 240, 80, 72, 90 dpi) and ESC ^ m (9-pin, 2 bytes a
// column). Other ESC sequences take the FX-80's parameter counts and are ignored.
// CR, LF, VT, FF, BS and TAB behave as on a printer.
// Tear off downloads everything printed so far as a PNG.

import { $, download } from './util.js';

const DPI = 144;                      // canvas pixels per inch
const COLS = 80, LPI = 6, CPI = 10;
const PAGE_LINES = 66;                // 11" at 6 lpi
const PAGE_W = Math.round(9.5 * DPI), PAGE_H = Math.round(11 * DPI);
const MARGIN = Math.round(0.75 * DPI);  // 0.5" tractor strip + 0.25" gap
const CW = DPI / CPI, LH = DPI / LPI;
const PRINT_W = COLS * CW;            // 8" of printable line
const TOP_LINE = 3;                   // first line printed on a fresh page
const CPS = 220;                      // characters per second
const GFX_PPS = 12 * DPI;             // the head in bit-image mode: 12 inches per second
const MAX_PAGES = 12;
const U = 216;                        // vertical units per inch (ESC 3, ESC J)
const PAGE_U = PAGE_LINES * 36, TOP_U = TOP_LINE * 36;
const HEAD_Y = (LH - 8 * 2.05) / 2;   // pin 1 below the top of the line
const PIN = DPI / 72;                 // pin pitch 1/72"
const INK = '#23242e';
const GFX_DPI = [60, 120, 120, 240, 80, 72, 90];   // ESC * m

// parameter bytes after ESC <cmd> (FX-80 / ESC/P); missing = none
const NPARAM = {};
for (const c of '-W3AJNlQSURtx!psjk/imqaIw%') NPARAM[c.charCodeAt(0)] = 1;
for (const c of '$\\?ef') NPARAM[c.charCodeAt(0)] = 2;
NPARAM[0x3A] = 3;                                   // ESC : 0 n 0 (copy ROM)

export class Printer {
  constructor(sound) {
    this.sound = sound;
    this.font = null;
    this.queue = [];
    this.pages = [];
    this.next = null;                 // the sheet after this one, when a band ran over the perforation
    this.speed = 1;                   // tests may hurry it
    this.stats = { chars: 0, gfxCols: 0, dots: 0, passes: 0 };
    this.reset();
    this.x = 0; this.y = TOP_U;
    this.lineChars = 0; this.lineStart = 0; this.lineGfx = false;
    this.printedAnything = false;
    this.feed = $('paperFeed'); this.view = $('paperView');
    this.head = $('printHead');
    $('formFeed').onclick = () => { this.sound.click('button'); this.enqueue(0x0C); };
    $('tearOff').onclick = () => this.tearOff();
    this.raf = 0;
    // a sheet that is already through the printer, so the paper runs up out of view
    const lead = document.createElement('canvas');
    lead.width = PAGE_W; lead.height = PAGE_H; paintPaper(lead.getContext('2d'));
    this.feed.append(lead);
    this.newPage();
    new ResizeObserver(() => this.position()).observe(this.view);
    window.armdosPrinter = this;      // for the curious (and for tests)
  }
  /** ESC @ (and power on): the DIP-switch defaults. */
  reset() {
    this.esc = null; this.gfx = null; this.skip = 0;
    this.spacing = 36;                // 1/6"
    this.pageLen = PAGE_U;
    this.perfSkip = true;             // a new sheet starts TOP_LINE lines down
    this.pitch = CW; this.condensed = false;
    this.dwide = false; this.dwideLine = false;
    this.bold = false; this.dstrike = false; this.italic = false; this.under = false; this.script = 0;
    this.lmargin = 0; this.rmargin = PRINT_W;
    this.tabs = []; for (let c = 8; c < 137; c += 8) this.tabs.push(c * CW);
    this.gfxMode = [0, 1, 2, 3];      // ESC K L Y Z -> ESC * mode
  }
  async loadFont() {
    if (this.font) return;
    const r = await fetch('emu/fonts/cga8x8.bin');
    this.font = new Uint8Array(await r.arrayBuffer());
  }
  setOnline(on) { $('prOnline').classList.toggle('on', on); }

  enqueue(b) {
    this.queue.push(b);
    $('prData').classList.add('on');
    if (!this.raf) { this.last = performance.now(); this.budget = 0; this.raf = requestAnimationFrame((t) => this.pump(t)); }
  }

  pump(now) {
    this.raf = 0;
    if (!this.font) { this.loadFont().then(() => { this.raf = requestAnimationFrame((t) => this.pump(t)); }); return; }
    this.budget = Math.min(this.budget, 0) + Math.max(1 / CPS, (now - this.last) / 1000) * this.speed;
    this.last = now;
    this.moved = false;
    let n = 0;
    while (this.budget > 0 && this.queue.length) { this.budget -= this.byte(this.queue.shift()); if (++n > 200000) break; }
    if (this.moved) this.position();
    this.moveHead();
    if (this.queue.length) this.raf = requestAnimationFrame((t) => this.pump(t));
    else { this.flushLineSound(); $('prData').classList.remove('on'); }
  }

  /** One byte. Returns the time it takes the head, in seconds. */
  byte(b) {
    if (this.gfx) return this.gfxByte(b);
    if (this.skip) { this.skip--; return 0; }
    if (this.esc) return this.escByte(b);
    switch (b) {
      case 0x1B: this.esc = { cmd: -1, a: [] }; return 0;
      case 0x0D: return this.carriageReturn();
      case 0x0A: case 0x0B: this.flushLineSound(); this.lineFeed(this.spacing); this.dwideLine = false; return 0;
      case 0x0C: this.flushLineSound(); this.formFeed(); this.dwideLine = false; return 0;
      case 0x08: this.x = Math.max(this.lmargin, this.x - this.advance()); return 0;
      case 0x09: { const t = this.tabs.find((p) => p > this.x + 0.01); this.x = Math.min(this.rmargin - this.advance(), t ?? this.x); return 0; }
      case 0x0E: this.dwideLine = true; return 0;           // SO: double width for this line
      case 0x14: this.dwideLine = false; return 0;          // DC4
      case 0x0F: this.condensed = true; return 0;           // SI
      case 0x12: this.condensed = false; return 0;          // DC2
      case 0x07: case 0x00: case 0x11: case 0x13: case 0x18: case 0x7F: return 0;
    }
    if (b < 0x20) return 0;
    return this.character(b);
  }

  advance() {
    let w = this.condensed ? (this.pitch === CW ? DPI / 17.16 : DPI / 20) : this.pitch;
    if (this.dwide || this.dwideLine) w *= 2;
    return w;
  }
  character(b) {
    const adv = this.advance();
    if (this.x + adv > this.rmargin + 0.01) { this.flushLineSound(); this.x = this.lmargin; this.lineFeed(this.spacing); }
    const plain = adv === CW && !this.bold && !this.dstrike && !this.italic && !this.under && !this.script;
    if (plain) {
      const col = Math.round(this.x / CW);             // on a column: exactly where the text-only printer put it
      this.glyph(b, Math.abs(col * CW - this.x) < 1e-6 ? col * CW : this.x, (this.y * DPI / U));
    }
    else this.glyphStyled(b, this.x, (this.y * DPI / U), adv);
    if (!this.lineChars) this.lineStart = this.x;
    this.lineChars++;
    this.x += adv;
    this.stats.chars++;
    this.inked();
    return adv / CW / CPS;
  }
  inked() {
    if (this.printedAnything) return;
    this.printedAnything = true;
    $('tearOff').disabled = false; $('paperEmpty').hidden = true;
  }
  carriageReturn() {
    this.flushLineSound();
    const cols = this.x / CW;
    let t = 0;
    if (this.x > 0) this.sound.printReturn(Math.round(cols));
    if (this.lineGfx) { t = 0.06 + cols * 0.0012; this.lineGfx = false; }
    this.x = this.lmargin;
    return t;
  }

  // ------------------------------------------------------------- escapes
  escByte(b) {
    const e = this.esc;
    if (e.cmd < 0) {
      e.cmd = b;
      const c = String.fromCharCode(b);
      if (c === 'D' || c === 'B') e.nul = true;
      else if ('KLYZ'.includes(c)) e.need = 2;
      else if (c === '*' || c === '^' || c === '&') e.need = 3;
      else if (c === 'C') e.need = 1;
      else e.need = NPARAM[b] || 0;
      if (!e.nul && !e.need) { this.esc = null; this.command(b, []); }
      return 0;
    }
    e.a.push(b);
    if (e.nul) {
      if (b === 0 || e.a.length > 32) { this.esc = null; this.command(e.cmd, e.a.slice(0, -1)); }
      return 0;
    }
    if (e.cmd === 0x43 && e.a.length === 1 && b === 0) e.need = 2;   // ESC C 0 n: inches
    if (e.a.length >= e.need) { this.esc = null; this.command(e.cmd, e.a); }
    return 0;
  }

  command(cmd, a) {
    const n = a[0] | 0, on = (v) => v === 1 || v === 0x31;
    switch (String.fromCharCode(cmd)) {
      case '@': this.flushLineSound(); this.reset(); break;
      case 'E': this.bold = true; break;
      case 'F': this.bold = false; break;
      case 'G': this.dstrike = true; break;
      case 'H': this.dstrike = false; break;
      case '4': this.italic = true; break;
      case '5': this.italic = false; break;
      case '-': this.under = on(n); break;
      case 'W': this.dwide = on(n); break;
      case '\x0E': this.dwideLine = true; break;
      case '\x0F': this.condensed = true; break;
      case 'M': this.pitch = DPI / 12; break;
      case 'P': this.pitch = CW; break;
      case 'g': this.pitch = DPI / 15; break;
      case 'S': this.script = (n & 1) ? 2 : 1; break;
      case 'T': this.script = 0; break;
      case '!':
        this.pitch = (n & 1) ? DPI / 12 : CW; this.condensed = !!(n & 4); this.bold = !!(n & 8);
        this.dstrike = !!(n & 16); this.dwide = !!(n & 32); this.italic = !!(n & 64); this.under = !!(n & 128); break;
      case '0': this.spacing = 27; break;
      case '1': this.spacing = 21; break;
      case '2': this.spacing = 36; break;
      case '3': this.spacing = n; break;
      case 'A': this.spacing = n * 3; break;
      case 'J': this.flushLineSound(); if (n) this.lineFeed(n); break;
      case 'C': this.pageLen = a.length === 2 ? Math.min(22, a[1]) * U : Math.min(127, n || 66) * this.spacing;
        this.pageLen = Math.max(36, Math.min(PAGE_U, this.pageLen)); break;
      case 'N': this.perfSkip = n > 0; break;
      case 'O': this.perfSkip = false; break;
      case 'l': this.lmargin = Math.min(PRINT_W - CW, n * this.pitch); if (this.x < this.lmargin) this.x = this.lmargin; break;
      case 'Q': if (n) this.rmargin = Math.max(this.lmargin + CW, Math.min(PRINT_W, n * this.pitch)); break;
      case '$': this.x = Math.min(this.rmargin, this.lmargin + (a[0] + a[1] * 256) * DPI / 60); break;
      case '\\': { let d = a[0] + a[1] * 256; if (d & 0x8000) d -= 0x10000; this.x = Math.max(this.lmargin, Math.min(this.rmargin, this.x + d * DPI / 120)); break; }
      case 'D': this.tabs = a.filter((c, i) => i === 0 || c > a[i - 1]).map((c) => c * this.pitch); break;
      case '?': { const k = 'KLYZ'.indexOf(String.fromCharCode(a[0])); if (k >= 0 && a[1] < 7) this.gfxMode[k] = a[1]; break; }
      case 'K': case 'L': case 'Y': case 'Z':
        this.startGfx(this.gfxMode['KLYZ'.indexOf(String.fromCharCode(cmd))], a[0] + a[1] * 256, 1); break;
      case '*': this.startGfx(n, a[1] + a[2] * 256, 1); break;
      case '^': this.startGfx(n, a[1] + a[2] * 256, 2); break;
      case '&': this.skip = Math.max(0, a[2] - a[1] + 1) * 12; break;   // user-defined characters: not kept
    }
  }

  // ------------------------------------------------------------- bit images
  startGfx(mode, cols, bpc) {
    if (cols <= 0) return;
    const dpi = GFX_DPI[mode] || 60;
    this.gfx = { left: cols * bpc, bpc, hi: 0, colW: DPI / dpi, fade: 0.84 + Math.random() * 0.12,
      jy: (Math.random() - 0.5) * 0.35, ph: Math.random() * 6.28 };
    // the head crosses the line once for this image: the buzz of the pins
    const pps = GFX_PPS * (mode === 2 ? 2 : 1);
    const w = Math.min(cols * this.gfx.colW, Math.max(0, this.rmargin - this.x));
    // printLine(n, rate): n pin columns at `rate` a second - the buzz is pitched at the column rate
    if (w > 0) this.sound.printLine(Math.max(1, Math.round(w / this.gfx.colW)), pps / this.gfx.colW);
    this.gfx.pps = pps;
    this.lineGfx = true;
    this.stats.passes++;
    this.inked();
  }
  gfxByte(b) {
    const G = this.gfx;
    let pins;
    if (G.bpc === 2) {
      if (!(G.left & 1) ) { G.hi = b; G.left--; return 0; }
      pins = (G.hi << 1) | (b >> 7);   // 9 pins: bits 8..0 top to bottom
    } else pins = b << 1;
    G.left--;
    if (!G.left) this.gfx = null;
    const x = this.x;
    this.x += G.colW;
    this.stats.gfxCols++;
    if (x + G.colW > this.rmargin + 0.01 || !pins) return G.colW / G.pps;
    const g = this.cur.g, y0 = (this.y * DPI / U) + HEAD_Y + G.jy;
    const xp = MARGIN + x + 1.5;
    // the ribbon: a little lighter or darker along the line
    const band = G.fade * (0.93 + 0.07 * Math.sin(G.ph + x / 57));
    g.fillStyle = INK;
    for (let p = 0; p < 9; p++) {
      if (!((pins >> (8 - p)) & 1)) continue;
      this.dot(g, xp + (Math.random() - 0.5) * 0.25, y0 + p * PIN + (Math.random() - 0.5) * 0.25, 1.02, band * (0.78 + Math.random() * 0.22));
    }
    return G.colW / G.pps;
  }
  /** One ink dot; a band that runs over the perforation continues on the next sheet. */
  dot(g, x, y, r, alpha) {
    this.stats.dots++;
    g.globalAlpha = alpha;
    g.beginPath(); g.arc(x, y, r, 0, Math.PI * 2); g.fill();
    if (y + r > PAGE_H) {
      const n = this.nextSheet().g;
      n.fillStyle = INK; n.globalAlpha = alpha;
      n.beginPath(); n.arc(x, y - PAGE_H, r, 0, Math.PI * 2); n.fill();
      n.globalAlpha = 1;
    }
    g.globalAlpha = 1;
  }

  flushLineSound() {
    if (this.lineChars) { this.sound.printLine(this.lineChars, CPS); this.lineChars = 0; }
  }
  /** Feed n/216". */
  lineFeed(n) {
    this.sound.printFeed(n / 36);
    this.y += n;
    this.moved = true;
    if (this.y >= this.pageLen) {
      const over = this.y - this.pageLen;
      this.newPage();
      this.y = this.perfSkip ? TOP_U : over;
    }
  }
  formFeed() {
    this.sound.printFeed((this.pageLen - this.y) / 36);
    this.newPage(); this.y = this.perfSkip ? TOP_U : 0; this.x = this.lmargin;
    this.moved = true;
  }

  newPage() {
    if (this.next) { this.cur = this.next; this.next = null; this.position(); return; }
    this.cur = this.addSheet();
    this.position();
  }
  nextSheet() {
    if (!this.next) this.next = this.addSheet();
    return this.next;
  }
  addSheet() {
    const c = document.createElement('canvas');
    c.width = PAGE_W; c.height = PAGE_H;
    const g = c.getContext('2d');
    paintPaper(g);
    this.feed.append(c);
    const p = { canvas: c, g };
    this.pages.push(p);
    while (this.pages.length > MAX_PAGES) this.pages.shift().canvas.remove();
    return p;
  }
  glyph(ch, x, ypx) {
    const g = this.cur.g, f = this.font;
    const x0 = MARGIN + x + 1.5, y0 = ypx + (LH - 8 * 2.05) / 2;
    const dx = 1.72, dy = 2.05, r = 0.92;
    g.fillStyle = INK;
    const fade = 0.78 + Math.random() * 0.2;         // ribbon wear
    for (let y = 0; y < 8; y++) {
      const bits = f[ch * 8 + y];
      if (!bits) continue;
      for (let x = 0; x < 8; x++) {
        if (!((bits >> (7 - x)) & 1)) continue;
        g.globalAlpha = fade * (0.8 + Math.random() * 0.2);
        g.beginPath();
        g.arc(x0 + x * dx + (Math.random() - 0.5) * 0.3, y0 + y * dy + (Math.random() - 0.5) * 0.3, r, 0, Math.PI * 2);
        g.fill();
      }
    }
    g.globalAlpha = 1;
  }
  /** A glyph in one of the other type styles (condensed, elite, double width,
   *  emphasized, double-strike, italic, underline, super/subscript). */
  glyphStyled(ch, x, ypx, adv) {
    const g = this.cur.g, f = this.font;
    const wide = this.dwide || this.dwideLine;
    const hs = adv / CW / (wide ? 2 : 1);             // horizontal dot pitch vs pica
    const dx = 1.72 * hs, r = 0.92;
    let dy = 2.05, yoff = 0;
    if (this.script) { dy = 1.03; yoff = this.script === 2 ? 4 * 2.05 : 0; }
    const x0 = MARGIN + x + 1.5, y0 = ypx + HEAD_Y + yoff;
    g.fillStyle = INK;
    const fade = 0.78 + Math.random() * 0.2;
    const put = (px, py) => {
      g.globalAlpha = fade * (0.8 + Math.random() * 0.2);
      g.beginPath(); g.arc(px + (Math.random() - 0.5) * 0.3, py + (Math.random() - 0.5) * 0.3, r, 0, Math.PI * 2); g.fill();
      this.stats.dots++;
    };
    for (let y = 0; y < 8; y++) {
      const bits = f[ch * 8 + y];
      if (!bits) continue;
      const sh = this.italic ? (7 - y) * 0.3 : 0;
      for (let xx = 0; xx < 8; xx++) {
        if (!((bits >> (7 - xx)) & 1)) continue;
        const px = x0 + (wide ? xx * 2 * dx : xx * dx) + sh, py = y0 + y * dy;
        put(px, py);
        if (wide) put(px + dx, py);
        if (this.bold && !this.condensed) put(px + (wide ? 2 * dx : dx) * 0.5, py);
        if (this.dstrike) put(px + 0.15, py + 0.7);
      }
    }
    if (this.under) for (let u = 0; u < adv; u += 1.2) put(MARGIN + x + u + 0.6, ypx + HEAD_Y + 8 * 2.05);
    g.globalAlpha = 1;
  }

  /** Keep the line being printed at the printer's slot (the bottom of the view). */
  position() {
    const w = this.feed.clientWidth || 1;
    const scale = w / PAGE_W;
    // paper below the current line, still inside (and a sheet more when one is already started)
    const below = (PAGE_H - (this.y * DPI / U) - LH) + LH * 0.4 + (this.next ? PAGE_H : 0);
    this.feed.style.transform = `translateY(${(below * scale).toFixed(1)}px)`;
  }
  moveHead() {
    const track = this.head.parentElement.clientWidth - this.head.clientWidth;
    this.head.style.transform = `translateX(${(Math.min(this.x, PRINT_W) / PRINT_W * track).toFixed(1)}px)`;
  }

  tearOff() {
    if (!this.printedAnything) return;
    this.sound.click('latch');
    const sheets = this.pages.slice();
    const used = sheets.length - (this.next ? 2 : 1);
    const lastH = this.next ? PAGE_H : Math.min(PAGE_H, Math.ceil((this.y * DPI / U) + 2 * LH));
    const out = document.createElement('canvas');
    out.width = PAGE_W; out.height = used * PAGE_H + lastH + (this.next ? PAGE_H : 0);
    const g = out.getContext('2d');
    sheets.forEach((p, k) => g.drawImage(p.canvas, 0, k * PAGE_H));
    out.toBlob((b) => b && download(b, 'armdos-printout.png', 'image/png'), 'image/png');
    // a fresh sheet
    for (const p of this.pages) p.canvas.remove();
    this.pages = []; this.next = null; this.newPage(); this.y = TOP_U; this.x = this.lmargin;
    this.printedAnything = false;
    $('tearOff').disabled = true; $('paperEmpty').hidden = false;
    this.position(); this.moveHead();
  }
}

function paintPaper(g) {
    g.fillStyle = '#fbfaf2'; g.fillRect(0, 0, PAGE_W, PAGE_H);
    // green bars: 1/2" bands (3 lines) alternating, starting at the top of form
    g.fillStyle = '#dcebd6';
    for (let y = 0; y < PAGE_H; y += LH * 6) g.fillRect(MARGIN - 6, y + LH * 3, PAGE_W - 2 * MARGIN + 12, LH * 3);
    // tractor strips: holes and perforation
    const strip = Math.round(0.5 * DPI);
    for (const x0 of [0, PAGE_W - strip]) {
      g.fillStyle = 'rgba(0,0,0,.035)'; g.fillRect(x0, 0, strip, PAGE_H);
      for (let y = DPI * 0.25; y < PAGE_H; y += DPI * 0.5) {
        g.beginPath(); g.arc(x0 + strip / 2, y, DPI * 0.078, 0, Math.PI * 2);
        g.fillStyle = '#1c1915'; g.fill();
      }
    }
    g.strokeStyle = 'rgba(0,0,0,.18)'; g.setLineDash([3, 4]); g.lineWidth = 1.2;
    for (const x of [strip, PAGE_W - strip]) { g.beginPath(); g.moveTo(x + 0.5, 0); g.lineTo(x + 0.5, PAGE_H); g.stroke(); }
    // perforation between sheets (at the top of this one)
    g.beginPath(); g.moveTo(0, 1); g.lineTo(PAGE_W, 1); g.strokeStyle = 'rgba(0,0,0,.25)'; g.stroke();
    g.setLineDash([]);
    // faint line numbers on the green bar, like the real forms
    g.fillStyle = 'rgba(40,110,60,.35)'; g.font = `${Math.round(DPI * 0.07)}px "IBM Plex Mono", monospace`;
    for (let l = 0; l < PAGE_LINES; l += 3) g.fillText(String(l + 1), MARGIN - 20, l * LH + LH * 0.72);
}
