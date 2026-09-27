#!/usr/bin/env node
// apps/edit/tools/mkhlp.mjs - data/help.txt (UTF-8) -> EDIT.HLP (code page 437,
// CR LF). Topics start with ".topic Name"; cross references are ◄Name►.
// Checks that every cross reference names an existing topic.
import fs from 'node:fs';

const cp437 = [
  '\0', '☺', '☻', '♥', '♦', '♣', '♠', '•', '◘', '○', '◙', '♂', '♀', '♪', '♫', '☼',
  '►', '◄', '↕', '‼', '¶', '§', '▬', '↨', '↑', '↓', '→', '←', '∟', '↔', '▲', '▼',
];
const hi = 'ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ';
const map = new Map();
for (let i = 1; i < 32; i++) map.set(cp437[i], i);
for (let i = 0; i < 128; i++) map.set(hi[i], 128 + i);

const [src, dst] = process.argv.slice(2);
const text = fs.readFileSync(src, 'utf8').replace(/\r\n/g, '\n');
const lines = text.split('\n');
const topics = new Set(lines.filter((l) => l.startsWith('.topic ')).map((l) => l.slice(7).toLowerCase()));
let bad = 0;
lines.forEach((l, i) => {
  if (l.startsWith('#')) return;
  for (const m of l.matchAll(/◄([^►]*)►/g))
    if (!topics.has(m[1].toLowerCase())) { console.error(`${src}:${i + 1}: no topic "${m[1]}"`); bad++; }
  if ([...l].length > 76) { console.error(`${src}:${i + 1}: line longer than 76`); bad++; }
});
if (bad) process.exit(1);
const out = [];
for (const ch of lines.filter((l) => !l.startsWith('#')).join('\r\n')) {
  const c = ch.codePointAt(0);
  if (c >= 32 && c < 127 || c === 13 || c === 10) out.push(c);
  else if (map.has(ch)) out.push(map.get(ch));
  else { console.error(`no CP437 character for ${JSON.stringify(ch)}`); process.exit(1); }
}
fs.writeFileSync(dst, Buffer.from(out));
