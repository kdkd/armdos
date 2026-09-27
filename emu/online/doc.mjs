// Documents for ARM-DOS Online: the service lays text out for the client's 76-column
// reader (word wrap, indents, styles, numbered links) and sends finished lines, so the
// DOS program only has to paint them. Encoding of a line (CP437 bytes):
//
//   0x0A          end of line
//   0x03 s        style s from here on: n normal, h heading, H subheading, s minor heading,
//                 b bold, i italic, k dim, y yellow, g green, c cyan, r red, m magenta,
//                 w bright white, t title, p picture link, l link (the client colours them)
//   0x01 a b      start of link number (a-32)*96 + (b-32)   (1 .. 9215)
//   0x02          end of link
//
// Every line starts in style n outside any link; a link or style that continues onto the
// next line is opened again there, so each line stands on its own.

import { toCP437 } from './cp437.mjs';

export const WIDTH = 76;

export class Doc {
  constructor({ title = '', channel = '', kind = 'E', width = WIDTH, maxBytes = 48 * 1024 } = {}) {
    Object.assign(this, { title, channel, kind, width, maxBytes });
    this.lines = [];          // encoded lines (without the LF)
    this.plain = [];          // the same as plain text (tests, printing, logs)
    this.links = [null];      // 1-based
    this.bytes = 0;
    this.cells = [];          // the line being filled: { c, s, l }
    this.indent = 0;          // continuation indent of the current paragraph
    this.lastBlank = true;
    this.truncated = false;
  }
  get full() { return this.bytes >= this.maxBytes; }

  /** Register a link target; returns its number. */
  addLink(target) { this.links.push(target); return this.links.length - 1; }

  // ------------------------------------------------------------ inline text
  /** Words (whitespace collapses, as in HTML). */
  text(s, style = 'n', link = 0) {
    s = toCP437(s).replace(/[\s\n]+/g, ' ');
    for (const ch of s) this.cell(ch, style, link);
  }
  /** Text exactly as given (spaces kept; still wraps). */
  raw(s, style = 'n', link = 0) { for (const ch of toCP437(s)) if (ch !== '\n') this.cellRaw(ch, style, link); else this.br(); }
  link(s, target, style = 'l') { const n = this.addLink(target); this.text(s, style, n); return n; }
  cell(ch, s, l) {
    if (ch === ' ') {
      const last = this.cells[this.cells.length - 1];
      if (this.cells.length <= this.indent || (last && last.c === ' ')) return;
    }
    this.cellRaw(ch, s, l);
  }
  cellRaw(ch, s, l) {
    this.cells.push({ c: ch, s, l });
    if (this.cells.length > this.width) this.wrap();
  }
  wrap() {
    const cells = this.cells;
    let brk = -1;
    for (let i = cells.length - 1; i > this.indent; i--) if (cells[i].c === ' ') { brk = i; break; }
    let rest;
    if (brk < 0 || brk < this.indent + 8) { rest = cells.splice(this.width); }     // one long word: cut it
    else { rest = cells.splice(brk + 1); cells.pop(); }
    this.emit(cells);
    this.cells = [];
    for (let i = 0; i < this.indent; i++) this.cells.push({ c: ' ', s: 'n', l: 0 });
    while (rest.length && rest[0].c === ' ') rest.shift();
    for (const c of rest) this.cells.push(c);
  }
  /** End the current line (a hard break); an empty line stays empty. */
  br() {
    const hasText = this.cells.length > this.indent;
    this.emit(hasText ? this.cells : []);
    this.cells = [];
    for (let i = 0; i < this.indent; i++) this.cells.push({ c: ' ', s: 'n', l: 0 });
  }
  emit(cells) {
    while (cells.length && cells[cells.length - 1].c === ' ' && !cells[cells.length - 1].l) cells.pop();
    let out = '', plain = '', s = 'n', l = 0;
    for (const x of cells) {
      if (x.l !== l) {
        if (l) out += '\x02';
        if (x.l) out += '\x01' + String.fromCharCode(32 + Math.floor(x.l / 96), 32 + (x.l % 96));
        l = x.l;
      }
      if (x.s !== s) { out += '\x03' + x.s; s = x.s; }
      out += x.c; plain += x.c;
    }
    if (l) out += '\x02';
    this.lines.push(out); this.plain.push(plain);
    this.bytes += out.length + 1;
    this.lastBlank = plain.length === 0;
  }

  // ------------------------------------------------------------ blocks
  /** Start a new block: finish the current line; blank line before unless tight. */
  block({ indent = 0, first = indent, tight = false } = {}) {
    if (this.cells.length > this.indentCur()) this.br();
    if (!tight && !this.lastBlank && this.lines.length) this.blank();
    this.indent = indent;
    this.cells = [];
    for (let i = 0; i < first; i++) this.cells.push({ c: ' ', s: 'n', l: 0 });
  }
  indentCur() { let n = 0; while (n < this.cells.length && this.cells[n].c === ' ' && !this.cells[n].l) n++; return Math.max(n, this.indent); }
  /** Finish the current block. */
  end() { if (this.cells.length > this.indentCur()) this.br(); this.cells = []; this.indent = 0; }
  blank() { if (this.cells.length > this.indentCur()) this.br(); this.emit([]); }
  heading(s, level = 2) {
    this.end();
    if (!this.lastBlank && this.lines.length) this.blank();
    const st = level <= 2 ? 'h' : level === 3 ? 'H' : 's';
    this.text(level <= 2 ? s.toUpperCase() : s, st);
    this.end();
    if (level <= 2) {
      const len = Math.min(this.width, this.plain[this.plain.length - 1].length);
      this.cells = []; this.raw('═'.repeat(len), 'h'); this.end();
    }
    this.lastBlank = false;
  }
  rule(style = 'k') { this.end(); this.raw('─'.repeat(this.width), style); this.end(); }
  /** One line, not wrapped (cut at the width). */
  line(s, style = 'n') { this.end(); const t = toCP437(s).slice(0, this.width); for (const ch of t) this.cells.push({ c: ch, s: style, l: 0 }); this.end(); }
  /** A line built from [text, style, link] parts, cut at the width. */
  parts(parts) {
    this.end();
    for (const [t, s = 'n', l = 0] of parts) for (const ch of toCP437(t)) if (this.cells.length < this.width) this.cells.push({ c: ch, s, l });
    this.end();
  }

  /** The whole document as one binary string (CP437 + codes). */
  encode() { this.end(); while (this.lines.length && this.plain[this.plain.length - 1] === '') { this.lines.pop(); this.plain.pop(); } return this.lines.map((l) => l + '\n').join(''); }
  text_() { return this.plain.join('\n'); }
}
