#!/usr/bin/env node
// tools/rscdump.mjs FILE.RSC - list a GEM resource file's trees, objects and strings
import fs from 'node:fs';
const b = fs.readFileSync(process.argv[2]);
const w = (o) => b.readUInt16LE(o), l = (o) => b.readUInt32LE(o);
const H = ['vrsn', 'object', 'tedinfo', 'iconblk', 'bitblk', 'frstr', 'string', 'imdata', 'frimg', 'trindex',
  'nobs', 'ntree', 'nted', 'nib', 'nbb', 'nstring', 'nimages', 'rssize'];
const h = {}; H.forEach((k, i) => (h[k] = w(i * 2)));
console.log(h);
const str = (o) => { let s = ''; while (o < b.length && b[o]) s += String.fromCharCode(b[o++]); return s; };
const TYPES = { 20: 'BOX', 21: 'TEXT', 22: 'BOXTEXT', 23: 'IMAGE', 24: 'USERDEF', 25: 'IBOX', 26: 'BUTTON', 27: 'BOXCHAR', 28: 'STRING', 29: 'FTEXT', 30: 'FBOXTEXT', 31: 'ICON', 32: 'TITLE' };
for (let t = 0; t < h.ntree; t++) {
  const root = l(h.trindex + 4 * t);
  console.log(`tree ${t} @${root}`);
  for (let i = 0; ; i++) {
    const o = root + 24 * i;
    const next = w(o) << 16 >> 16, head = w(o + 2) << 16 >> 16, tail = w(o + 4) << 16 >> 16, type = w(o + 6) & 0xff, flags = w(o + 8), state = w(o + 10), spec = l(o + 12);
    let txt = '';
    if ([26, 28, 32].includes(type)) txt = JSON.stringify(str(spec));
    if ([21, 22, 29, 30].includes(type)) txt = JSON.stringify(str(l(spec))) + ' tmpl ' + JSON.stringify(str(l(spec + 4)));
    if (type === 31) txt = 'icon text ' + JSON.stringify(str(l(spec + 8)));
    console.log(`  ${i}: ${next},${head},${tail} ${TYPES[type] || type} f${flags.toString(16)} s${state.toString(16)} spec=${spec.toString(16)} ${w(o + 16)},${w(o + 18)} ${w(o + 20)}x${w(o + 22)} ${txt}`);
    if (flags & 0x20) break;
    if (i > 300) break;
  }
}
for (let i = 0; i < h.nstring; i++) console.log(`str ${i}: ${JSON.stringify(str(l(h.frstr + 4 * i)))}`);
