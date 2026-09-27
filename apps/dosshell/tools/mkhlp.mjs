#!/usr/bin/env node
// tools/mkhlp.mjs - build SHELL.HLP from data/help.txt.
//
// help.txt: "@H_NAME Title" starts a topic (H_NAME from the enum in
// shellc/shell.h, which fixes the order); the text follows. Lines are joined
// with blanks; an empty line is a paragraph break; a line that starts with a
// blank starts a new line (lists, tables). '&' is SHELL.HLP's line break.
//
// SHELL.HLP: "ADSHHLP", 0, u16 count, then count x {u16 offset, u16 length},
// then the records "Title\0text".
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const [src, out] = process.argv.slice(2);
const header = fs.readFileSync(path.join(here, '../shellc/shell.h'), 'latin1');
const en = header.match(/enum \{\s*\/\* topics in SHELL\.HLP[^]*?\*\/([^}]*)\}/);
if (!en) throw new Error('topic enum not found in shell.h');
const names = en[1].split(',').map((s) => s.trim()).filter((s) => s && s !== 'H_COUNT');

const topics = {};
let cur = null;
for (const raw of fs.readFileSync(src, 'latin1').split(/\r?\n/)) {
  if (raw.startsWith('#')) continue;
  const m = raw.match(/^@(H_\w+)\s+(.*)$/);
  if (m) { cur = { title: m[2].trim(), lines: [] }; topics[m[1]] = cur; continue; }
  if (cur) cur.lines.push(raw.replace(/\s+$/, ''));
}

function join(lines) {
  while (lines.length && !lines[lines.length - 1]) lines.pop();
  while (lines.length && !lines[0]) lines.shift();
  let s = '', para = false;
  for (const l of lines) {
    if (!l) { para = true; continue; }
    if (!s) s = l;
    else if (para) s += '&&' + l;
    else if (l.startsWith(' ')) s += '&' + l;
    else s += ' ' + l;
    para = false;
  }
  return s;
}

const recs = names.map((n) => {
  const t = topics[n];
  if (!t) throw new Error(`help.txt has no topic ${n}`);
  const text = join(t.lines);
  if (text.length > 2500) throw new Error(`${n} is too long (${text.length})`);
  return Buffer.from(t.title + '\0' + text, 'latin1');
});
for (const k of Object.keys(topics)) if (!names.includes(k)) throw new Error(`unknown topic ${k}`);
const hdr = Buffer.alloc(10 + 4 * recs.length);
hdr.write('ADSHHLP', 0, 'latin1');
hdr.writeUInt16LE(recs.length, 8);
let off = hdr.length;
recs.forEach((r, i) => {
  if (off + r.length > 0xFFFF) throw new Error('SHELL.HLP over 64 KB');
  hdr.writeUInt16LE(off, 10 + i * 4);
  hdr.writeUInt16LE(r.length, 12 + i * 4);
  off += r.length;
});
fs.writeFileSync(out, Buffer.concat([hdr, ...recs]));
