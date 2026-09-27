#!/usr/bin/env node
// apps/keyb/tools/kdfasm.mjs - builds KEYBOARD.SYS from MS-DOS 4.00's keyboard
// definition sources (DEV/KEYBOARD/KDF*.ASM, Microsoft, MIT licence; copies in
// apps/keyb/kdf/) plus ARM-DOS's own additions (KDFDV.ASM: Dvorak).
//
//   node apps/keyb/tools/kdfasm.mjs OUT.SYS [--link kdfarm,KDFSP,...] [--kdf DIR]
//
// It is a small assembler for exactly the MASM subset those files use:
// labels, EQU/=, DB/DW/DD with DUP, strings, OFFSET, $, + - * / ( ), the
// include files' equates, and KEYBMAC.INC's state-logic macros (IFF ANDF ELSEF
// ENDIFF XLATT OPTION SET_FLAG PUT_ERROR_CHAR IFKBD GOTO BEEP RESET_NLS
// EXIT_INT_9 EXIT_STATE_LOGIC CHECK_FOR_CORE_KEY FLAG), implemented here with
// the byte encodings KEYBMAC.INC gives them. The modules are "linked" the way
// LINK + EXE2BIN did it (KEYBOARD.LNK): one public CODE segment, each module's
// part paragraph aligned, OFFSETs relative to the start of the file.
//
// The default link list is MS-DOS 4.00's (KEYBOARD.LNK) with ARM-DOS's header
// (KDFARM.ASM = KDFNOW.ASM + the Dvorak layouts) and KDFDV.ASM before KDFEOF.
// With --link kdfnow,... and the original list the output is byte-for-byte the
// KEYBOARD.SYS of MS-DOS 4.00 (checked by apps/keyb/tests/run.mjs when the
// original is available).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));

export const MS_LINK = ['KDFNOW', 'KDFSP', 'KDFPO', 'KDFFR120', 'KDFFR189', 'KDFDK', 'KDFSG', 'KDFGE',
  'KDFIT141', 'KDFIT142', 'KDFUK166', 'KDFUK168', 'KDFSF', 'KDFBE', 'KDFNL', 'KDFNO', 'KDFCF', 'KDFSV',
  'KDFLA', 'KDFEOF'];
export const ARM_LINK = ['KDFARM', ...MS_LINK.slice(1, -1), 'KDFDV', 'KDFEOF'];

// ------------------------------------------------------------ constants

// KEYBMAC.INC FIND_FLAG: which flag byte each mask lives in
const FLAG_IDS = {
  SCAN_MATCH: 'EXT_KB_FLAG_ID', EITHER_SHIFT: 'EXT_KB_FLAG_ID', CAPS_STATE: 'KB_FLAG_ID',
  NUM_STATE: 'KB_FLAG_ID', EITHER_CTL: 'EXT_KB_FLAG_ID', EITHER_ALT: 'EXT_KB_FLAG_ID',
  LEFT_SHIFT: 'KB_FLAG_ID', RIGHT_SHIFT: 'KB_FLAG_ID', ALT_SHIFT: 'KB_FLAG_ID', CTL_SHIFT: 'KB_FLAG_ID',
  R_ALT_SHIFT: 'KB_FLAG_3_ID', R_CTL_SHIFT: 'KB_FLAG_3_ID', TILDE: 'NLS_FLAG_1_ID', ACUTE: 'NLS_FLAG_1_ID',
  GRAVE: 'NLS_FLAG_1_ID', DIARESIS: 'NLS_FLAG_1_ID', CEDILLA: 'NLS_FLAG_1_ID', CIRCUMFLEX: 'NLS_FLAG_1_ID',
  LC_E0: 'KB_FLAG_3_ID',
};
const CMD = { IFF: 0x00, ANDF: 0x10, ELSEF: 0x20, ENDIFF: 0x30, XLATT: 0x40, OPTION: 0x50, SET_FLAG: 0x60,
  PUT_ERROR: 0x70, IFKBD: 0x80, GOTO: 0x90, BEEP: 0xA0, RESET_NLS: 0xB0, CHECK_CORE: 0xC0 };

// ------------------------------------------------------------ source handling

function readLines(file) {
  // the sources are code page 437 bytes: keep every byte as one char
  return fs.readFileSync(file).toString('latin1').split('\n').map((l) => l.replace(/\r$/, ''));
}

function stripComment(line) {
  let q = null;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (q) { if (c === q) q = null; }
    else if (c === '\'' || c === '"') q = c;
    else if (c === ';') return line.slice(0, i);
  }
  return line;
}

function splitArgs(s) {
  const out = [];
  let cur = '', q = null, depth = 0;
  for (const c of s) {
    if (q) { cur += c; if (c === q) q = null; continue; }
    if (c === '\'' || c === '"') { q = c; cur += c; continue; }
    if (c === '(') depth++;
    if (c === ')') depth--;
    if (c === ',' && depth === 0) { out.push(cur.trim()); cur = ''; continue; }
    cur += c;
  }
  out.push(cur.trim());
  return out;
}

// ------------------------------------------------------------ the assembler

class Asm {
  constructor() {
    this.sym = new Map();      // upper-case name -> number (equates and labels)
    this.bytes = [];
    this.pc = 0;               // offset in the linked image
    this.pass = 1;
    this.undefined = new Set();
  }

  // equates from an include file (macros and structures are skipped)
  include(file) {
    let skip = null;
    for (const raw of readLines(file)) {
      const line = stripComment(raw).trim();
      if (!line) continue;
      const w = line.split(/\s+/);
      if (skip) { if ((skip === 'ENDM' && w[0].toUpperCase() === 'ENDM') || (skip === 'ENDS' && w[1] && w[1].toUpperCase() === 'ENDS')) skip = null; continue; }
      if (w[1] && w[1].toUpperCase() === 'MACRO') { skip = 'ENDM'; continue; }
      if (w[1] && w[1].toUpperCase() === 'STRUC') { skip = 'ENDS'; continue; }
      const m = line.match(/^([A-Za-z_@$?][\w@$?]*)\s+(EQU|=)\s+(.+)$/i) || line.match(/^([A-Za-z_@$?][\w@$?]*)\s*(=)\s*(.+)$/);
      if (m) {
        try { this.sym.set(m[1].toUpperCase(), this.expr(m[3])); } catch { /* an equate we don't need */ }
      }
    }
  }

  expr(s) {
    s = s.trim();
    let i = 0;
    const self = this;
    const peek = () => { while (s[i] === ' ' || s[i] === '\t') i++; return s[i]; };
    function atom() {
      const c = peek();
      if (c === '(') { i++; const v = sum(); peek(); if (s[i] !== ')') throw new Error(`) expected in ${s}`); i++; return v; }
      if (c === '-') { i++; return -atom(); }
      if (c === '+') { i++; return atom(); }
      if (c === '$') { i++; return self.pc; }
      if (c === '\'' || c === '"') {
        const q = c; const j = s.indexOf(q, i + 1); const str = s.slice(i + 1, j); i = j + 1;
        let v = 0; for (const ch of str) v = (v << 8) | ch.charCodeAt(0);
        return v;
      }
      const m = s.slice(i).match(/^[A-Za-z_@$?0-9][\w@$?]*/);
      if (!m) throw new Error(`bad expression ${s}`);
      const tok = m[0]; i += tok.length;
      const up = tok.toUpperCase();
      if (/^[0-9]/.test(tok)) {
        if (/^[0-9][0-9A-F]*H$/i.test(tok)) return parseInt(tok.slice(0, -1), 16);
        if (/^[01]+B$/i.test(tok)) return parseInt(tok.slice(0, -1), 2);
        if (/^[0-9]+D?$/i.test(tok)) return parseInt(tok, 10);
        throw new Error(`bad number ${tok}`);
      }
      if (up === 'OFFSET') return atom();
      if (up === 'NOT') return ~atom() & 0xFFFF;
      if (self.sym.has(up)) return self.sym.get(up);
      if (self.globals.has(up)) return self.globals.get(up);
      if (self.pass === 2) throw new Error(`undefined symbol ${tok}`);
      self.undefined.add(up);
      return 0;
    }
    function prod() {
      let v = atom();
      for (;;) {
        const c = peek();
        if (c === '*') { i++; v *= atom(); } else if (c === '/') { i++; v = Math.trunc(v / atom()); } else return v;
      }
    }
    function sum() {
      let v = prod();
      for (;;) {
        const c = peek();
        if (c === '+') { i++; v += prod(); } else if (c === '-') { i++; v -= prod(); } else return v;
      }
    }
    const v = sum();
    if (peek() !== undefined) throw new Error(`junk in expression "${s}"`);
    return v;
  }

  emit(v, n) {
    for (let k = 0; k < n; k++) {
      if (this.pass === 2) this.bytes[this.pc] = (v >> (8 * k)) & 0xFF;
      this.pc++;
    }
  }

  data(kind, list) {
    const n = kind === 'DB' ? 1 : kind === 'DW' ? 2 : 4;
    for (const item of splitArgs(list)) {
      if (item === '') { this.emit(0, n); continue; }
      const dup = item.match(/^(.+?)\s+DUP\s*\((.*)\)$/i);
      if (dup) {
        const count = this.expr(dup[1]);
        for (let k = 0; k < count; k++) this.data(kind, dup[2]);
        continue;
      }
      if (n === 1 && (item[0] === '\'' || item[0] === '"') && item[item.length - 1] === item[0] && item.length > 3) {
        for (const ch of item.slice(1, -1)) this.emit(ch.charCodeAt(0), 1);
        continue;
      }
      if (n === 1 && (item[0] === '\'' || item[0] === '"') && item.length === 3) { this.emit(item.charCodeAt(1), 1); continue; }
      this.emit(this.expr(item), n);
    }
  }

  flag(name) {
    const up = name.trim().toUpperCase();
    const id = FLAG_IDS[up];
    if (!id) throw new Error(`unknown flag ${name}`);
    return [this.sym.get(id), this.expr(up)];
  }

  macro(name, args) {
    const a = args ? splitArgs(args) : [];
    const not = a[1] && a[1].toUpperCase() === 'NOT' ? 0x08 : 0;
    switch (name) {
      case 'IFF': case 'ANDF': {
        const [id, mask] = this.flag(a[0]);
        this.emit(CMD[name] + not + id, 1); this.emit(mask, 1); return true;
      }
      case 'FLAG': { const [id, mask] = this.flag(a[0]); this.emit(id, 1); this.emit(mask, 1); return true; }
      case 'ELSEF': this.emit(CMD.ELSEF, 1); return true;
      case 'ENDIFF': this.emit(CMD.ENDIFF, 1); return true;
      case 'XLATT': this.emit(CMD.XLATT, 1); this.emit(this.expr(a[0]), 1); return true;
      case 'PUT_ERROR_CHAR': this.emit(CMD.PUT_ERROR, 1); this.emit(this.expr(a[0]), 1); return true;
      case 'OPTION': this.emit(CMD.OPTION + not, 1); this.emit(this.expr(a[0]), 1); return true;
      case 'SET_FLAG': this.emit(CMD.SET_FLAG, 1); this.emit(this.expr(a[0]), 1); return true;
      case 'RESET_NLS': this.emit(CMD.RESET_NLS, 1); return true;
      case 'BEEP': this.emit(CMD.BEEP, 1); return true;
      case 'IFKBD': this.emit(CMD.IFKBD, 1); this.emit(this.expr(a[0]), 2); return true;
      case 'GOTO': { this.emit(CMD.GOTO, 1); const t = this.expr(a[0]); this.emit(t - this.pc - 2, 2); return true; }
      case 'EXIT_INT_9': this.emit(CMD.GOTO + 1, 1); this.emit(0, 2); return true;
      case 'EXIT_STATE_LOGIC': this.emit(CMD.GOTO + 2, 1); this.emit(0, 2); return true;
      case 'CHECK_FOR_CORE_KEY': this.emit(CMD.CHECK_CORE, 1); return true;
    }
    return false;
  }

  statement(line, file, no) {
    let s = stripComment(line).trim();
    if (!s) return;
    // label:
    let m = s.match(/^([A-Za-z_@$?][\w@$?]*):\s*(.*)$/);
    if (m) {
      this.define(m[1], this.pc, file, no);
      s = m[2].trim();
      if (!s) return;
    }
    const words = s.split(/\s+/);
    const w0 = words[0].toUpperCase(), w1 = (words[1] || '').toUpperCase();
    const rest = (n) => s.replace(/^\S+/, '').replace(n === 2 ? /^\s*\S+/ : /^/, '').trim();
    if (w0 === 'PUBLIC') { for (const n of splitArgs(rest(1))) this.publics.add(n.toUpperCase()); return; }
    if (['PAGE', 'TITLE', '.XLIST', '.LIST', 'EXTRN', 'ASSUME', 'END', 'SUBTTL', 'NAME'].includes(w0)) return;
    if (w1 === 'SEGMENT') { this.pc = (this.pc + 15) & ~15; return; }     // paragraph aligned (LINK)
    if (w1 === 'ENDS') return;
    if (w0 === 'INCLUDE') {
      const f = findFile(path.dirname(file), words[1]);
      // copyrigh.inc is data; the others are equates and macros
      if (/COPYRIGH/i.test(words[1])) { for (const [k, l] of readLines(f).entries()) this.statement(l, f, k + 1); }
      else this.include(f);
      return;
    }
    if (w1 === 'EQU' || w1 === '=') { this.sym.set(w0, this.expr(rest(2))); return; }
    if (/^([A-Za-z_@$?][\w@$?]*)\s*=/.test(s)) { const [n, v] = s.split('='); this.sym.set(n.trim().toUpperCase(), this.expr(v)); return; }
    if (w1 === 'LABEL') { this.define(words[0], this.pc, file, no); return; }
    if (['DB', 'DW', 'DD'].includes(w0)) { this.data(w0, rest(1)); return; }
    if (['DB', 'DW', 'DD'].includes(w1)) { this.define(words[0], this.pc, file, no); this.data(w1, rest(2)); return; }
    if (w0 === 'EVEN') { if (this.pc & 1) this.emit(0x90, 1); return; }
    if (this.macro(w0, rest(1))) return;
    throw new Error(`${path.basename(file)}:${no}: cannot assemble "${s}"`);
  }

  define(name, v, file, no) {
    const up = name.toUpperCase();
    if (this.pass === 1 && this.sym.has(up) && this.labels.has(up)) throw new Error(`${path.basename(file)}:${no}: ${name} defined twice`);
    this.labels.add(up);
    this.sym.set(up, v);
  }

  assemble(files) {
    this.globals = new Map();
    for (this.pass = 1; this.pass <= 2; this.pass++) {
      this.pc = 0;
      for (const [fi, f] of files.entries()) {
        // module-local symbols; PUBLIC ones are exported to the other modules.
        // Pass 2 starts from pass 1's labels (forward references).
        this.sym = new Map(this.pass === 2 ? this.pass1[fi] : []); this.labels = new Set(); this.publics = new Set();
        const lines = readLines(f);
        lines.forEach((l, k) => this.statement(l, f, k + 1));
        for (const n of this.publics) if (this.sym.has(n)) this.globals.set(n, this.sym.get(n));
        if (this.pass === 1) (this.pass1 ||= [])[fi] = new Map(this.sym);
      }
    }
    const out = Buffer.alloc(this.pc);
    for (let k = 0; k < this.pc; k++) out[k] = this.bytes[k] || 0;
    return out;
  }
}

function findFile(dir, name) {
  const want = name.toUpperCase();
  for (const d of [dir]) {
    for (const f of fs.readdirSync(d)) if (f.toUpperCase() === want) return path.join(d, f);
  }
  throw new Error(`include file ${name} not found in ${dir}`);
}

/** Assemble and link the modules (names without .ASM, looked up case-insensitively in dir). */
export function buildKeyboardSys(dir = path.join(HERE, '..', 'kdf'), link = ARM_LINK) {
  const files = link.map((n) => findFile(dir, n + '.ASM'));
  const img = new Asm().assemble(files);
  if (link !== MS_LINK && link.join() !== MS_LINK.join()) fixMaxima(img);
  return img;
}

// ARM-DOS: the header's "maximum size" words (+10h common, +12h specific,
// +14h state logic), which KEYB sizes its resident table area with, are
// MS-DOS 4.00's figures and smaller than several of its own sections (FR's
// common section is 798 bytes, the header says 650). Set them to the real
// maxima so any layout fits wherever KEYB made room for another.
function fixMaxima(b) {
  const u16 = (o) => b[o] | (b[o + 1] << 8);
  const nId = u16(0x18), nLang = u16(0x1A);
  let ml = 0, mc = 0, ms = 0;
  const entries = [];
  for (let i = 0; i < nLang; i++) entries.push(u16(0x1C + 6 * i + 2));
  for (let i = 0; i < nId; i++) entries.push(u16(0x1C + 6 * nLang + 6 * i + 2));
  for (const e of entries) {
    const logic = u16(e + 4), ll = u16(logic);
    ml = Math.max(ml, ll); mc = Math.max(mc, u16(logic + ll));
    for (let k = 0; k < b[e + 9]; k++) ms = Math.max(ms, u16(u16(e + 10 + 6 * k + 2)));
  }
  b.writeUInt16LE(mc, 0x10); b.writeUInt16LE(ms, 0x12); b.writeUInt16LE(ml, 0x14);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  const out = args.find((a) => !a.startsWith('--'));
  const li = args.indexOf('--link');
  const di = args.indexOf('--kdf');
  const link = li >= 0 ? args[li + 1].split(',') : ARM_LINK;
  const dir = di >= 0 ? args[di + 1] : undefined;
  const img = buildKeyboardSys(dir, link);
  if (out) fs.writeFileSync(out, img);
  console.log(`kdfasm: ${out || '(no output)'}: ${img.length} bytes`);
}
