#!/usr/bin/env node
// mkhelp.mjs - a help text (tvhelp.h: ".topic Name,Alias", "{Text:Topic}",
// ".ex"/".endex"; lines starting with ';' are comments) -> a C source file
// with the text as one string in code page 437:
//   node mkhelp.mjs IN.txt OUT.c SYMBOL
// Checks that every cross reference names an existing topic.
import fs from 'node:fs';
const [inp, out, sym] = process.argv.slice(2);
const cp437 = { '•': 0x07, '≡': 0xF0, '→': 0x1A, '←': 0x1B, '↑': 0x18, '↓': 0x19, '─': 0xC4, '│': 0xB3,
  '┌': 0xDA, '┐': 0xBF, '└': 0xC0, '┘': 0xD9, 'é': 0x82, 'è': 0x8A, 'ü': 0x81, 'ö': 0x94, '°': 0xF8,
  '◄': 0x11, '►': 0x10, '▲': 0x1E, '▼': 0x1F, '█': 0xDB, '░': 0xB0, '±': 0xF1, '÷': 0xF6, '≤': 0xF3, '≥': 0xF2 };
const lines = fs.readFileSync(inp, 'utf8').split(/\r?\n/).filter((l) => !l.startsWith(';'));
const names = new Set(['index']);
for (const l of lines) if (l.startsWith('.topic ')) for (const n of l.slice(7).split(',')) names.add(n.trim().toLowerCase());
let bad = 0;
for (const l of lines) {
  if (l.startsWith('.')) continue;
  for (const m of l.matchAll(/\{([^}]*)\}/g)) {
    const t = (m[1].includes(':') ? m[1].slice(m[1].indexOf(':') + 1) : m[1]).trim().toLowerCase();
    if (!names.has(t)) { console.error(`${inp}: no topic "${t}" (${l.trim()})`); bad++; }
  }
}
if (bad) process.exit(1);
const text = lines.join('\n').replace(/\\x([0-9A-Fa-f]{2})/g, (_, h) => String.fromCharCode(parseInt(h, 16)));
const bytes = [];
for (const ch of text) {
  const c = ch.codePointAt(0);
  if (c < 0x80) bytes.push(c);
  else if (cp437[ch] !== undefined) bytes.push(cp437[ch]);
  else if (c < 0x100 && /\\x/.test('')) bytes.push(c);
  else if (c >= 0x80 && c < 0x100) bytes.push(c);   // from a \xNN escape
  else throw new Error(`${inp}: no code page 437 character for ${ch}`);
}
let s = `/* generated from ${inp.split('/').pop()} by apps/tvlib/tools/mkhelp.mjs - do not edit */\n`;
s += `extern const char ${sym}[];\nconst char ${sym}[] =\n`;
let line = '"';
let prevOct = false;
for (const b of bytes) {
  if (b === 0x0A) { s += line + '\\n"\n'; line = '"'; prevOct = false; continue; }
  let e;
  if (b === 0x22) e = '\\"'; else if (b === 0x5C) e = '\\\\'; else if (b === 0x3F) e = '?';
  else if (b < 0x20 || b >= 0x7F) e = '\\' + b.toString(8).padStart(3, '0');
  else e = String.fromCharCode(b);
  line += e;
}
s += line + '";\n';
fs.writeFileSync(out, s);
