#!/usr/bin/env node
// apps/nlsfunc/tools/mkcountry.mjs - builds COUNTRY.SYS from MS-DOS 4.00's
// source for it (DEV/COUNTRY/MKCNTRY.ASM + MKCNTRY.INC, Microsoft, MIT licence;
// copies in apps/nlsfunc/country/), in DOS 4.00's COUNTRY.SYS format, plus
// ARM-DOS's additions (Brazil, 055).
//
//   node apps/nlsfunc/tools/mkcountry.mjs OUT.SYS [--ms]
//
// MKCNTRY.ASM is a program that writes its data segment to a file; this tool
// assembles just that data (from "cdinfo" to "cdiend"): labels, DB/DW/DD with
// DUP and OFFSET, label arithmetic, the equates of MKCNTRY.INC, and instances
// of its structures (ctryent/CTRYSTR, ctrydat/CTRYDAT, ctable/CTABLE, cinfo/
// CINFO). With --ms the output is byte-for-byte MS-DOS 4.00's COUNTRY.SYS
// (checked by apps/nlsfunc/tests/run.mjs when the original is at hand).
//
// The format: FFh "COUNTRY", 8 reserved, 1 pointer (type 1) to the entry list:
// a count, then per (country, code page) 12-byte entries (size 12, country,
// code page, 2 reserved words, offset of its data). The data: a count (6),
// then 8-byte items (size 6, subfunction 1/2/4/5/6/7, 0, offset, 0) pointing
// at tables that begin with FFh and a 7-character name ("CTYINFO", "UCASE  ",
// "FCHAR  ", "COLLATE", "DBCS   ") and a length word.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const SRC = path.join(HERE, '..', 'country');

const read = (f) => fs.readFileSync(f).toString('latin1').split('\n').map((l) => l.replace(/\r$/, ''));
function stripComment(line) {
  let q = null;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (q) { if (c === q) q = null; } else if (c === '\'' || c === '"') q = c; else if (c === ';') return line.slice(0, i);
  }
  return line;
}
function splitArgs(s) {
  const out = []; let cur = '', q = null, depth = 0;
  for (const c of s) {
    if (q) { cur += c; if (c === q) q = null; continue; }
    if (c === '\'' || c === '"') { q = c; cur += c; continue; }
    if (c === '(' || c === '<') depth++;
    if (c === ')' || c === '>') depth--;
    if (c === ',' && depth === 0) { out.push(cur.trim()); cur = ''; continue; }
    cur += c;
  }
  out.push(cur.trim());
  return out;
}

// the structures of MKCNTRY.INC: field sizes and defaults
const STRUCTS = {
  CTRYENT: { fields: [2, 2, 2, 2, 2, 2, 2] },
  CTRYDAT: { fields: [2, 1, 1, 2, 2] },
  CTABLE: { fields: [1, 7, 2], defaults: ['0FFh', "'CTYINFO'", 'CINFOSIZE'] },
  CINFO: { fields: [2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4, 1, 1, 10] },
};

export function buildCountrySys({ ms = false } = {}) {
  const sym = new Map();
  for (const l of read(path.join(SRC, 'MKCNTRY.INC'))) {
    const m = stripComment(l).trim().match(/^(\w+)\s+EQU\s+(.+)$/i);
    if (m && !/TYPE/i.test(m[2])) sym.set(m[1].toUpperCase(), m[2].trim());
  }
  const fixed = { CENTRYSIZE: 12, CDATASIZE: 6, CINFOSIZE: 38 };
  const lines = read(path.join(SRC, 'MKCNTRY.ASM'));
  const start = lines.findIndex((l) => /^cdinfo\s+label/i.test(l));
  const end = lines.findIndex((l) => /^cdiend\s+label/i.test(l));
  const body = lines.slice(start, end);
  // include copyrigh.inc in place (the notice the MIT licence keeps)
  const ci = body.findIndex((l) => /^\s*include\s+copyrigh\.inc/i.test(l));
  if (ci >= 0) body.splice(ci, 1, '\tdb\t"MS DOS Version 4.00 (C)Copyright 1988 Microsoft Corp"', '\tdb\t"Licensed Material - Property of Microsoft  "');
  const nEntries = body.filter((l) => /^\s*ctryent\s/i.test(l)).length;

  let pass, pc, out;
  const labels = new Map();
  const value = (s) => {
    s = s.trim();
    let i = 0;
    const peek = () => { while (s[i] === ' ' || s[i] === '\t') i++; return s[i]; };
    const atom = () => {
      const c = peek();
      if (c === '(') { i++; const v = sum(); peek(); i++; return v; }
      if (c === '-') { i++; return -atom(); }
      if (c === '$') { i++; return pc; }
      if (c === '\'' || c === '"') { const j = s.indexOf(c, i + 1); const t = s.slice(i + 1, j); i = j + 1; let v = 0; for (const ch of t) v = (v << 8) | ch.charCodeAt(0); return v; }
      const m = s.slice(i).match(/^\w+/); if (!m) throw new Error(`bad expression "${s}"`);
      i += m[0].length;
      const t = m[0], up = t.toUpperCase();
      if (/^[0-9]/.test(t)) {
        if (/H$/i.test(t)) return parseInt(t.slice(0, -1), 16);
        return parseInt(t, 10);
      }
      if (up === 'OFFSET') return atom();
      if (up === 'FINALCNT') return nEntries;
      if (up in fixed) return fixed[up];
      if (labels.has(up)) return labels.get(up);
      if (sym.has(up)) return value(sym.get(up));
      if (pass === 2) throw new Error(`undefined ${t}`);
      return 0;
    };
    const prod = () => { let v = atom(); for (;;) { const c = peek(); if (c === '*') { i++; v *= atom(); } else return v; } };
    const sum = () => { let v = prod(); for (;;) { const c = peek(); if (c === '+') { i++; v += prod(); } else if (c === '-') { i++; v -= prod(); } else return v; } };
    return sum();
  };
  const emit = (v, n) => { for (let k = 0; k < n; k++) { if (pass === 2) out.push((v >> (8 * k)) & 0xFF); pc++; } };
  const data = (n, list) => {
    for (const item of splitArgs(list)) {
      if (item === '') { emit(0, n); continue; }
      const d = item.match(/^(.+?)\s+dup\s*\((.*)\)$/i);
      if (d) { const c = value(d[1]); for (let k = 0; k < c; k++) data(n, d[2]); continue; }
      if (n === 1 && (item[0] === '\'' || item[0] === '"') && item.length > 3) { for (const ch of item.slice(1, -1)) emit(ch.charCodeAt(0), 1); continue; }
      emit(value(item), n);
    }
  };
  const instance = (name, args) => {
    const st = STRUCTS[name];
    const a = splitArgs(args);
    st.fields.forEach((size, k) => {
      const v = a[k] !== undefined && a[k] !== '' ? a[k] : (st.defaults && st.defaults[k]) || '';
      if (size === 7 || size === 10) {
        if (v === '') { for (let j = 0; j < size; j++) emit(0, 1); return; }
        const t = v.slice(1, -1).padEnd(size, ' ');
        if (t.length !== size) throw new Error(`field ${v}`);
        for (const ch of t) emit(ch.charCodeAt(0), 1);
        return;
      }
      emit(v === '' ? 0 : value(v), size);
    });
  };
  for (pass = 1; pass <= 2; pass++) {
    pc = 0; out = [];
    for (const raw of body) {
      const s = stripComment(raw).trim();
      if (!s || /^page\b/i.test(s) || /^cntrycnt\s*=/i.test(s) || /^dummy\b/i.test(s) || /^(finalCNT|endm)\b/i.test(s)) continue;
      let m;
      if ((m = s.match(/^(\w+)\s+label\s+\w+$/i))) { labels.set(m[1].toUpperCase(), pc); continue; }
      if ((m = s.match(/^(\w+)\s+equ\s+\$$/i))) { labels.set(m[1].toUpperCase(), pc); continue; }
      if ((m = s.match(/^(\w+)\s+equ\s+(.+)$/i))) { sym.set(m[1].toUpperCase(), m[2]); continue; }
      if ((m = s.match(/^(\w+)\s+macro\b/i))) continue;
      if ((m = s.match(/^(db|dw|dd)\s+(.*)$/i))) { data({ db: 1, dw: 2, dd: 4 }[m[1].toLowerCase()], m[2]); continue; }
      if ((m = s.match(/^(\w+)\s+(db|dw|dd)\s+(.*)$/i))) { labels.set(m[1].toUpperCase(), pc); data({ db: 1, dw: 2, dd: 4 }[m[2].toLowerCase()], m[3]); continue; }
      if ((m = s.match(/^(ctryent|ctrydat|ctable|cinfo)\s*<(.*)>$/i))) { instance(m[1].toUpperCase(), m[2]); continue; }
      throw new Error(`mkcountry: cannot assemble "${s}"`);
    }
  }
  let img = Buffer.from(out);
  if (!ms) img = addArm(img);
  return img;
}

// ------------------------------------------------------------ the model

const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;

/** The file as { country, cp, items: { subfn: Buffer (the table with its FFh/name/length header) } }. */
export function parseCountrySys(b) {
  if (b[0] !== 0xFF || b.toString('latin1', 1, 8) !== 'COUNTRY') throw new Error('not a COUNTRY.SYS');
  const list = u32(b, 0x13);
  const n = u16(b, list);
  const entries = [];
  for (let i = 0, p = list + 2; i < n; i++, p += 14) {
    const e = { country: u16(b, p + 2), cp: u16(b, p + 4), items: {} };
    const d = u32(b, p + 10);
    const ni = u16(b, d);
    for (let k = 0, q = d + 2; k < ni; k++, q += 8) {
      const id = b[q + 2], off = u32(b, q + 4);
      const len = u16(b, off + 8);
      e.items[id] = b.subarray(off, off + 10 + len);
    }
    entries.push(e);
  }
  return entries;
}

/** Write a COUNTRY.SYS for the entries (identical tables are stored once). */
export function writeCountrySys(entries, tail) {
  const parts = [];
  let off = 0;
  const push = (buf) => { parts.push(buf); off += buf.length; return off - buf.length; };
  const hdr = Buffer.alloc(0x17);
  hdr[0] = 0xFF; hdr.write('COUNTRY', 1, 'latin1'); hdr.writeUInt16LE(1, 0x10); hdr[0x12] = 1; hdr.writeUInt32LE(0x17, 0x13);
  push(hdr);
  const list = Buffer.alloc(2 + 14 * entries.length);
  list.writeUInt16LE(entries.length, 0);
  const listOff = push(list);
  const tables = new Map();
  const dataOffs = [];
  for (const e of entries) {
    const ids = Object.keys(e.items).map(Number);
    const d = Buffer.alloc(2 + 8 * ids.length);
    dataOffs.push(push(d));
    d.writeUInt16LE(ids.length, 0);
    e._d = d; e._ids = ids;
  }
  for (const e of entries) {
    e._ids.forEach((id, k) => {
      const t = e.items[id], key = t.toString('latin1');
      if (!tables.has(key)) tables.set(key, push(Buffer.from(t)));
      const q = 2 + 8 * k;
      e._d.writeUInt16LE(6, q); e._d[q + 2] = id; e._d.writeUInt32LE(tables.get(key), q + 4);
    });
  }
  entries.forEach((e, i) => {
    const p = listOff + 2 + 14 * i - listOff;
    list.writeUInt16LE(12, p); list.writeUInt16LE(e.country, p + 2); list.writeUInt16LE(e.cp, p + 4);
    list.writeUInt32LE(dataOffs[i], p + 10);
  });
  if (tail) push(tail);
  return Buffer.concat(parts);
}

// ARM-DOS additions: Brazil (055, code pages 850 and 437 - DOS 5 and later had it):
// dd/mm/yyyy with "/", "Cr$" before the amount with a space, "." thousands,
// "," decimals, 24-hour ":" time; Portugal's upper-case and collating tables.
function addArm(img) {
  const entries = parseCountrySys(img);
  const po850 = entries.find((e) => e.country === 351 && e.cp === 850);
  const la437 = entries.find((e) => e.country === 3 && e.cp === 437);
  const br = (base, cp) => {
    const items = { ...base.items };
    const info = Buffer.from(base.items[1]);
    const c = 10;                       // CINFO after the 10-byte table header
    info.writeUInt16LE(55, c); info.writeUInt16LE(cp, c + 2); info.writeUInt16LE(1, c + 4);   // DMY
    Buffer.from('Cr$\0\0', 'latin1').copy(info, c + 6);
    info[c + 11] = 0x2E; info[c + 12] = 0; info[c + 13] = 0x2C; info[c + 14] = 0;
    info[c + 15] = 0x2F; info[c + 16] = 0; info[c + 17] = 0x3A; info[c + 18] = 0;
    info[c + 19] = 2; info[c + 20] = 2; info[c + 21] = 1;       // "Cr$ 1.234,56", 2 decimals, 24 h
    info[c + 26] = 0x3B; info[c + 27] = 0;
    items[1] = info;
    return { country: 55, cp, items };
  };
  entries.push(br(po850, 850), br(la437, 437));
  const tailAt = img.lastIndexOf(Buffer.from('MS DOS Version 4.00', 'latin1'));
  return writeCountrySys(entries, tailAt >= 0 ? img.subarray(tailAt) : null);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const out = process.argv[2];
  const img = buildCountrySys({ ms: process.argv.includes('--ms') });
  if (out) fs.writeFileSync(out, img);
  console.log(`mkcountry: ${out}: ${img.length} bytes`);
}
