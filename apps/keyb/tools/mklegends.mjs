#!/usr/bin/env node
// apps/keyb/tools/mklegends.mjs - writes web/js/kbdlayouts.js, the key legends
// of every KEYB layout for the page's on-screen keyboard (web/js/pckeys.js) and
// its "follow my computer's layout" typing (web/js/input.js).
//
//   node apps/keyb/tools/mklegends.mjs [OUT.js]      (default web/js/kbdlayouts.js)
//
// The legends are what KEYB.COM itself would produce: KEYBOARD.SYS (built by
// tools/kdfasm.mjs) run through the model of KEYB's state processor
// (tools/kbdsys.mjs) for every key, unshifted, with Shift and with AltGr, in
// every code page the layout has; a key that sets a dead-key flag shows the
// accent it stands for. Keys the tables leave to the BIOS show the US legend.
// apps/keyb/tests/run.mjs checks that the committed file is up to date.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { buildKeyboardSys } from './kdfasm.mjs';
import * as K from './kbdsys.mjs';
import { CP_HIGH, CP_LOW, cpChar, cpByte } from './codepages.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');

// the layout ids KEYB publishes on port F6h (ARCH.md 4.6; keyb.c LAYOUT_IDS)
export const LAYOUT_IDS = [
  [1, 'GR', 0, 'German'], [2, 'SP', 0, 'Spanish'], [3, 'PO', 0, 'Portuguese'], [4, 'FR', 189, 'French'],
  [5, 'DK', 0, 'Danish'], [6, 'SG', 0, 'Swiss German'], [7, 'IT', 141, 'Italian'], [8, 'UK', 166, 'United Kingdom'],
  [9, 'SF', 0, 'Swiss French'], [10, 'BE', 0, 'Belgian'], [11, 'NL', 0, 'Dutch'], [12, 'NO', 0, 'Norwegian'],
  [13, 'CF', 0, 'Canadian French'], [14, 'SV', 0, 'Swedish'], [15, 'SU', 0, 'Finnish'], [16, 'LA', 0, 'Latin American'],
  [17, 'DV', 0, 'Dvorak'], [18, 'DL', 0, 'Dvorak (left hand)'], [19, 'DR', 0, 'Dvorak (right hand)'],
  [20, 'FR', 120, 'French (120)'], [21, 'IT', 142, 'Italian (142)'], [22, 'UK', 168, 'United Kingdom (168)'],
];
export const CP_INDEX = [437, 850, 860, 863, 865];

// the keys that carry characters (KeyboardEvent.code -> scan code, set 1)
export const KEYS = {
  Backquote: 0x29, Digit1: 0x02, Digit2: 0x03, Digit3: 0x04, Digit4: 0x05, Digit5: 0x06, Digit6: 0x07, Digit7: 0x08,
  Digit8: 0x09, Digit9: 0x0A, Digit0: 0x0B, Minus: 0x0C, Equal: 0x0D,
  KeyQ: 0x10, KeyW: 0x11, KeyE: 0x12, KeyR: 0x13, KeyT: 0x14, KeyY: 0x15, KeyU: 0x16, KeyI: 0x17, KeyO: 0x18, KeyP: 0x19,
  BracketLeft: 0x1A, BracketRight: 0x1B, Backslash: 0x2B,
  KeyA: 0x1E, KeyS: 0x1F, KeyD: 0x20, KeyF: 0x21, KeyG: 0x22, KeyH: 0x23, KeyJ: 0x24, KeyK: 0x25, KeyL: 0x26,
  Semicolon: 0x27, Quote: 0x28,
  IntlBackslash: 0x56, KeyZ: 0x2C, KeyX: 0x2D, KeyC: 0x2E, KeyV: 0x2F, KeyB: 0x30, KeyN: 0x31, KeyM: 0x32,
  Comma: 0x33, Period: 0x34, Slash: 0x35,
};
// the US BIOS's characters (bios/kbd.c k_normal / k_shift)
const US_LO = { 0x29: '`', 0x02: '1', 0x03: '2', 0x04: '3', 0x05: '4', 0x06: '5', 0x07: '6', 0x08: '7', 0x09: '8', 0x0A: '9', 0x0B: '0',
  0x0C: '-', 0x0D: '=', 0x1A: '[', 0x1B: ']', 0x2B: '\\', 0x27: ';', 0x28: "'", 0x33: ',', 0x34: '.', 0x35: '/' };
const US_HI = { 0x29: '~', 0x02: '!', 0x03: '@', 0x04: '#', 0x05: '$', 0x06: '%', 0x07: '^', 0x08: '&', 0x09: '*', 0x0A: '(', 0x0B: ')',
  0x0C: '_', 0x0D: '+', 0x1A: '{', 0x1B: '}', 0x2B: '|', 0x27: ':', 0x28: '"', 0x33: '<', 0x34: '>', 0x35: '?' };
const US_LETTERS = { 0x10: 'q', 0x11: 'w', 0x12: 'e', 0x13: 'r', 0x14: 't', 0x15: 'y', 0x16: 'u', 0x17: 'i', 0x18: 'o', 0x19: 'p',
  0x1E: 'a', 0x1F: 's', 0x20: 'd', 0x21: 'f', 0x22: 'g', 0x23: 'h', 0x24: 'j', 0x25: 'k', 0x26: 'l',
  0x2C: 'z', 0x2D: 'x', 0x2E: 'c', 0x2F: 'v', 0x30: 'b', 0x31: 'n', 0x32: 'm' };
function usChar(scan, shift) {
  if (US_LETTERS[scan]) return shift ? US_LETTERS[scan].toUpperCase() : US_LETTERS[scan];
  return (shift ? US_HI : US_LO)[scan] || '';
}

// the dead-key flags (KEYBMAC.INC) and the accents on their key caps
const ACCENTS = { 0x80: '\u00B4', 0x40: '`', 0x20: '\u00A8', 0x10: '^', 0x08: '\u00B8', 0x04: '~' };

/** What one key does in one state: { ch } or { dead: accent } or {}. */
function keyResult(t, scan, flags, cp) {
  const nls = { f1: 0, f2: 0 };
  const r = K.processKey(t, scan, flags, nls);
  if (r.exit) {
    if (r.out.length) return { ch: r.out.map((v) => cpChar(cp, v & 0xFF)).join('') };
    if (nls.f1) return { dead: ACCENTS[nls.f1] || '' };   // a dead key: the accent it stands for
    return {};
  }
  return null;                                      // the BIOS's (US) translation
}

export function makeLegends(kbdsys) {
  const kbd = K.parse(kbdsys);
  const layouts = {};
  for (const [id, code, kid, name] of LAYOUT_IDS) {
    let off = 0;
    if (kid) { const x = kbd.ids.find((x) => x.id === kid && kbd.entry(x.entry).code === code); off = x && x.entry; }
    else off = (kbd.langs.find((l) => l.code === code) || {}).entry;
    if (!off) throw new Error(`layout ${code} ${kid}`);
    const entry = kbd.entry(off);
    const pages = {};
    for (const { cp } of entry.cps) {
      const t = K.load(kbd, entry, cp);
      const keys = {};
      for (const [name, scan] of Object.entries(KEYS)) {
        const base = keyResult(t, scan, [0, 0, 0, 0], cp);
        const shift = keyResult(t, scan, [K.LEFT_SHIFT, 0, 0, 0], cp);
        const altgr = keyResult(t, scan, [K.ALT_SHIFT, 0, 0, K.R_ALT_SHIFT], cp);
        const b = base === null ? usChar(scan, false) : base.ch ?? base.dead ?? '';
        const s = shift === null ? usChar(scan, true) : shift.ch ?? shift.dead ?? '';
        const a = altgr && (altgr.ch ?? altgr.dead) || '';
        const dead = (base && base.dead !== undefined ? 1 : 0) | (shift && shift.dead !== undefined ? 2 : 0) | (altgr && altgr.dead !== undefined ? 4 : 0);
        if (!b && !s && !a) continue;
        keys[name] = dead ? [b, s, a, dead] : a ? [b, s, a] : [b, s];
      }
      pages[cp] = keys;
    }
    layouts[id] = { code, id: kid, name, cps: entry.cps.map((c) => c.cp), keys: pages };
  }
  return layouts;
}

export function legendsModule(kbdsys) {
  const layouts = makeLegends(kbdsys);
  return `// web/js/kbdlayouts.js - GENERATED by apps/keyb/tools/mklegends.mjs from
// KEYBOARD.SYS (do not edit): the key legends of every KEYB layout, as KEYB.COM
// itself produces them, for the on-screen keyboard and "follow my computer's
// layout". LAYOUTS[id] (id = port F6h bits 0-4, ARCH.md 4.6): code, /ID, name,
// code pages, keys[cp][KeyboardEvent.code] = [unshifted, shifted, AltGr, dead]
// (dead: bit 0/1/2 = that legend is a dead accent key).
export const CP_INDEX = ${JSON.stringify(CP_INDEX)};
export const CP_HIGH = ${JSON.stringify(CP_HIGH, null, 1)};
export const CP_LOW = ${JSON.stringify(CP_LOW)};
export ${cpChar.toString()}
export ${cpByte.toString()}
export const LAYOUTS = ${JSON.stringify(layouts)};
`;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const out = process.argv[2] || path.join(ROOT, 'web/js/kbdlayouts.js');
  fs.writeFileSync(out, legendsModule(buildKeyboardSys()));
  console.log(`mklegends: ${out}`);
}
