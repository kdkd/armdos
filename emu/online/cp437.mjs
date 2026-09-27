// Unicode -> code page 437, for text that goes to an ARM-PC screen (ARM-DOS Online).
// Characters CP437 has map to themselves; accented letters it lacks lose the accent
// (NFD), typographic punctuation becomes its ASCII look-alike, anything else is '?'.

const HIGH = 'ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ';
const MAP = new Map();
for (let i = 0; i < HIGH.length; i++) if (!MAP.has(HIGH[i])) MAP.set(HIGH[i], 0x80 + i);
MAP.set(' ', 0x20);    // no-break space: a plain space on screen (wrapping treats it as a letter upstream)
const ALIAS = {
  '‘': "'", '’': "'", '‚': "'", '‛': "'", '′': "'", '“': '"', '”': '"', '„': '"', '″': '"', '–': '-', '—': '-', '―': '-', '‐': '-', '‑': '-', '−': '-', '‒': '-', '…': '...', '•': '\x07', '‣': '\x07', '◦': 'o',
  '×': 'x', '⋅': '\xFA', '∗': '*', '→': '->', '←': '<-', '↔': '<->', '⇒': '=>', '≠': '/=', '™': '(TM)', '©': '(C)', '®': '(R)',
  '€': 'EUR', '‰': '0/00', '§': '\x15', '¶': '\x14', '¹': '1', '³': '3', '¾': '3/4', '⅓': '1/3', '⅔': '2/3',
  'β': '\xE1', 'μ': '\xE6', 'ϕ': '\xED', 'Δ': '\x7F', 'λ': 'l', 'ν': 'v', 'ρ': 'p', 'ω': 'w',
  'η': 'n', 'θ': '\xE9', 'κ': 'k', 'ψ': 'psi', 'χ': 'x', 'ζ': 'z', 'ξ': 'xi', 'γ': 'y', 'ι': 'i', 'ο': 'o', 'υ': 'u',
  'Π': '\xE3', 'Λ': 'L', 'Ψ': 'Psi', 'Ξ': 'Xi', 'ł': 'l', 'Ł': 'L', 'đ': 'd', 'Đ': 'D', 'ø': 'o', 'Ø': 'O', 'œ': 'oe', 'Œ': 'OE',
  'þ': 'th', 'Þ': 'Th', 'ð': 'd', 'ı': 'i', '​': '', '‎': '', '‏': '', '­': '', '﻿': '', ' ': ' ',
  ' ': ' ', ' ': ' ', ' ': ' ', '⁠': '', '−': '-', '∼': '~', '≃': '\xF7', '≅': '\xF7', '∑': '\xE4',
  '∏': '\xE3', '∫': 'S', '∂': 'd', '∈': 'in', '∅': '\xED', '∧': '^', '∨': 'v', '¦': '|', '¯': '-', '´': "'", '¸': ',', '¨': '"',
  '•': '\x07', '★': '*', '☆': '*', '✓': '\xFB', '✔': '\xFB', '✗': 'x', '♠': '\x06', '♣': '\x05', '♥': '<3', '♦': '\x04',
  '☺': ':)', '☻': ':)', '♪': '\x0D', '♫': '\x0E', '☼': '\x0F', '►': '\x10', '◄': '\x11', '↕': '\x12', '‼': '\x13',
  '▬': '\x16', '↨': '\x17', '↑': '\x18', '↓': '\x19', '∟': '\x1C', '▲': '\x1E', '▼': '\x1F', '⌂': '\x7F',
};

/** One Unicode code point (as a string) -> CP437 byte string ('' to drop it). */
function one(ch) {
  const c = ch.codePointAt(0);
  if (c < 0x80) return c >= 0x20 || c === 10 ? ch : '';
  const m = MAP.get(ch);
  if (m !== undefined) return String.fromCharCode(m);
  const a = ALIAS[ch];
  if (a) return a;
  if (a === '') return '';
  const base = ch.normalize('NFD').replace(/[̀-ͯ]/g, '');
  if (base && base !== ch) return [...base].map(one).join('');
  if (c >= 0x2000 && c <= 0x200A) return ' ';
  return '?';
}

const cache = new Map();
/** A JS string -> a "binary" string whose char codes are CP437 bytes. */
export function toCP437(s) {
  let out = '';
  for (const ch of String(s)) {
    if (ch.charCodeAt(0) < 0x80) { out += ch.charCodeAt(0) >= 0x20 || ch === '\n' ? ch : ch === '\t' ? ' ' : ''; continue; }
    let r = cache.get(ch);
    if (r === undefined) { r = one(ch); cache.set(ch, r); }
    out += r;
  }
  return out;
}

/** CP437 byte -> Unicode (for tests and logs). */
export function fromCP437(b) {
  if (b >= 0x80) return HIGH[b - 0x80];
  return String.fromCharCode(b);
}
