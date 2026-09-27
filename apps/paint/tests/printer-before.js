// web/js/printer.js as it was before the FX-80 graphics (the text-only
// printer), kept verbatim so apps/paint/tests/test_printer.py can check that
// plain text still prints exactly as it did (both pages with the same seeded
// Math.random).

import { $, download } from './util.js';

const DPI = 144;                      // canvas pixels per inch
const COLS = 80, LPI = 6, CPI = 10;
const PAGE_LINES = 66;                // 11" at 6 lpi
const PAGE_W = Math.round(9.5 * DPI), PAGE_H = Math.round(11 * DPI);
const MARGIN = Math.round(0.75 * DPI);  // 0.5" tractor strip + 0.25" gap
const CW = DPI / CPI, LH = DPI / LPI;
const TOP_LINE = 3;                   // first line printed on a fresh page
const CPS = 220;                      // characters per second
const MAX_PAGES = 12;

export class Printer {
  constructor(sound) {
    this.sound = sound;
    this.font = null;
    this.queue = [];
    this.pages = [];
    this.col = 0; this.line = TOP_LINE;
    this.esc = 0;
    this.lineChars = 0; this.lineStart = 0;
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
    window.armdosPrinter = this;      // (added for the comparison test)
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
    if (!this.raf) { this.last = performance.now(); this.raf = requestAnimationFrame((t) => this.pump(t)); }
  }

  pump(now) {
    this.raf = 0;
    if (!this.font) { this.loadFont().then(() => { this.raf = requestAnimationFrame((t) => this.pump(t)); }); return; }
    let budget = Math.max(1, Math.round(((now - this.last) / 1000) * CPS));
    this.last = now;
    let moved = false;
    while (budget-- > 0 && this.queue.length) moved = this.byte(this.queue.shift()) || moved;
    if (moved) this.position();
    this.moveHead();
    if (this.queue.length) this.raf = requestAnimationFrame((t) => this.pump(t));
    else { this.flushLineSound(); $('prData').classList.remove('on'); }
  }

  byte(b) {
    if (this.esc) { this.esc--; return false; }
    switch (b) {
      case 0x1B: this.esc = 1; return false;                  // ESC x: skip the parameter
      case 0x0D: this.flushLineSound(); if (this.col > 0) this.sound.printReturn(this.col); this.col = 0; return false;
      case 0x0A: this.flushLineSound(); this.lineFeed(1); return true;
      case 0x0C: this.flushLineSound(); this.formFeed(); return true;
      case 0x08: if (this.col > 0) this.col--; return false;
      case 0x09: this.col = Math.min(COLS - 1, (this.col + 8) & ~7); return false;
      case 0x07: case 0x00: case 0x11: case 0x13: case 0x0E: case 0x0F: case 0x12: case 0x14: return false;
    }
    if (this.col >= COLS) { this.col = 0; this.lineFeed(1); }
    this.glyph(b, this.col, this.line);
    if (!this.lineChars) this.lineStart = this.col;
    this.lineChars++;
    this.col++;
    this.printedAnything = true;
    $('tearOff').disabled = false; $('paperEmpty').hidden = true;
    return false;
  }
  flushLineSound() {
    if (this.lineChars) { this.sound.printLine(this.lineChars, CPS); this.lineChars = 0; }
  }
  lineFeed(n) {
    this.sound.printFeed(n);
    this.line += n;
    if (this.line >= PAGE_LINES) { this.newPage(); this.line = TOP_LINE; }
  }
  formFeed() {
    this.sound.printFeed(PAGE_LINES - this.line);
    this.newPage(); this.line = TOP_LINE; this.col = 0;
  }

  newPage() {
    const c = document.createElement('canvas');
    c.width = PAGE_W; c.height = PAGE_H;
    const g = c.getContext('2d');
    paintPaper(g);
    this.feed.append(c);
    this.pages.push({ canvas: c, g });
    while (this.pages.length > MAX_PAGES) this.pages.shift().canvas.remove();
    this.cur = this.pages[this.pages.length - 1];
    this.position();
  }
  glyph(ch, col, line) {
    const g = this.cur.g, f = this.font;
    const x0 = MARGIN + col * CW + 1.5, y0 = line * LH + (LH - 8 * 2.05) / 2;
    const dx = 1.72, dy = 2.05, r = 0.92;
    g.fillStyle = '#23242e';
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

  /** Keep the line being printed at the printer's slot (the bottom of the view). */
  position() {
    const w = this.feed.clientWidth || 1;
    const scale = w / PAGE_W;
    const below = (PAGE_LINES - this.line - 1) * LH + LH * 0.4;   // paper below the current line, still inside
    this.feed.style.transform = `translateY(${(below * scale).toFixed(1)}px)`;
  }
  moveHead() {
    const track = this.head.parentElement.clientWidth - this.head.clientWidth;
    this.head.style.transform = `translateX(${(Math.min(this.col, COLS) / COLS * track).toFixed(1)}px)`;
  }

  tearOff() {
    if (!this.printedAnything) return;
    this.sound.click('latch');
    const used = this.pages.length - 1;
    const lastH = Math.min(PAGE_H, Math.ceil((this.line + 2) * LH));
    const out = document.createElement('canvas');
    out.width = PAGE_W; out.height = used * PAGE_H + lastH;
    const g = out.getContext('2d');
    this.pages.forEach((p, k) => g.drawImage(p.canvas, 0, k * PAGE_H));
    out.toBlob((b) => b && download(b, 'armdos-printout.png', 'image/png'), 'image/png');
    // a fresh sheet
    for (const p of this.pages) p.canvas.remove();
    this.pages = []; this.newPage(); this.line = TOP_LINE; this.col = 0;
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
