// apps/keyb/tools/kbdsys.mjs - KEYBOARD.SYS (MS-DOS 4.00 format) as a JS module:
// the file's structure, the tables KEYB builds from it for one layout and code
// page, and KEYB's state-logic interpreter (MS-DOS 4.00 CMD/KEYB/KEYBI9.ASM
// KEYB_STATE_PROCESSOR), step for step. KEYB.COM (keyb.c) is the ARM version of
// the same thing; this model is what the tests compare it with and what
// tools/mklegends.mjs uses to draw the page's key caps.

export const G_KB = 0x1000;           // the enhanced (101/102-key) keyboard
// BIOS flag bits (KB_FLAG 40:17, KB_FLAG_1 40:18, KB_FLAG_3 40:96)
export const RIGHT_SHIFT = 0x01, LEFT_SHIFT = 0x02, CTL_SHIFT = 0x04, ALT_SHIFT = 0x08, CAPS_STATE = 0x40, NUM_STATE = 0x20;
export const LC_E0 = 0x02, R_CTL_SHIFT = 0x04, R_ALT_SHIFT = 0x08;
const EITHER_SHIFT = 0x80, EITHER_CTL = 0x40, EITHER_ALT = 0x20, SCAN_MATCH = 0x08;
const TYPE_2_TAB = 0x40, ASCII_ONLY = 0x80, ZERO_SCAN = 0x20, EXIT_IF_FOUND = 0x80;
// dead-key flags in NLS_FLAG_1 (KEYBMAC.INC)
export const DEAD = { ACUTE: 0x80, GRAVE: 0x40, DIARESIS: 0x20, CIRCUMFLEX: 0x10, CEDILLA: 0x08, TILDE: 0x04 };
// state ids whose "space" table gives the accent itself
export const ACCENT_SPACE = { 0x80: 10, 0x40: 13, 0x20: 16, 0x10: 19, 0x08: 22, 0x04: 33 };

const u16 = (b, o) => b[o] | (b[o + 1] << 8);

/** The header: languages (code -> entry offset), IDs, and each language entry. */
export function parse(buf) {
  const b = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
  if (b[0] !== 0xFF || String.fromCharCode(...b.slice(1, 8)) !== 'KEYB   ') throw new Error('not a KEYBOARD.SYS');
  const numId = u16(b, 0x18), numLang = u16(b, 0x1A);
  const langs = [], ids = [];
  let o = 0x1C;
  for (let i = 0; i < numLang; i++, o += 6) langs.push({ code: String.fromCharCode(b[o], b[o + 1]), entry: u16(b, o + 2) });
  for (let i = 0; i < numId; i++, o += 6) ids.push({ id: u16(b, o), entry: u16(b, o + 2) });
  const entry = (off) => {
    const e = { code: String.fromCharCode(b[off], b[off + 1]), id: u16(b, off + 2), logic: u16(b, off + 4),
      numId: b[off + 8], cps: [] };
    for (let i = 0, p = off + 10; i < b[off + 9]; i++, p += 6) e.cps.push({ cp: u16(b, p), table: u16(b, p + 2) });
    return e;
  };
  return { b, numId, numLang, langs, ids, entry,
    maxCommon: u16(b, 0x10), maxSpecific: u16(b, 0x12), maxLogic: u16(b, 0x14) };
}

/** The states of a translate section that apply to kbType (STATE_BUILD's filter). */
const FLAG_STATES = [1, 2, 24, 30];     // DEAD_LOWER DEAD_UPPER DEAD_THIRD DEAD_FOURTH
function readSection(b, off, kbType) {
  const len = u16(b, off), cp = u16(b, off + 2);
  const states = [];
  let p = off + 4;
  for (;;) {
    const sl = u16(b, p);
    if (!sl) break;
    const id = b[p + 2], type = u16(b, p + 3), err = u16(b, p + 5);
    if ((type & kbType) && FLAG_STATES.includes(id)) {
      // a flag table (SET_FLAG): DW count, then (scan, flag id, mask) triples
      const n = u16(b, p + 7), flags = [];
      for (let i = 0, q = p + 9; i < n; i++, q += 3) flags.push([b[q], b[q + 1], b[q + 2]]);
      states.push({ id, err, tabs: [], flags });
    } else if (type & kbType) {
      const tabs = [];
      let t = p + 7;
      for (;;) {
        const tl = u16(b, t);
        if (!tl) break;
        tabs.push(b.slice(t, t + tl));
        t += tl;
      }
      states.push({ id, err, tabs });
    }
    p += sl;
  }
  return { cp, len, states };
}

/** What KEYB loads for one language entry and code page. */
export function load(kbd, entry, cp, kbType = G_KB) {
  const b = kbd.b;
  const logicLen = u16(b, entry.logic);
  const special = u16(b, entry.logic + 2);
  const logic = b.slice(entry.logic, entry.logic + logicLen);
  const common = readSection(b, entry.logic + logicLen, kbType);
  const c = entry.cps.find((x) => x.cp === cp);
  const specific = c ? readSection(b, c.table, kbType) : null;
  return { code: entry.code, cp, special, logic, common, specific };
}

function findState(sec, id) { return sec ? sec.states.find((s) => s.id === id) : undefined; }

/** XLATT: search the state's tables for the scan code. Returns the buffer entry or null. */
function translate(st, scan) {
  for (const t of st.tabs) {
    const opts = t[2];
    if (opts & TYPE_2_TAB) {
      const n = t[3], sz = (opts & (ASCII_ONLY | ZERO_SCAN)) ? 2 : 3;
      for (let i = 0, p = 4; i < n; i++, p += sz) {
        if (t[p] !== scan) continue;
        let ah = scan, al = t[p + 1];
        if (!(opts & (ASCII_ONLY | ZERO_SCAN))) ah = t[p + 2];
        if (opts & ZERO_SCAN) ah = 0;
        return (ah << 8) | al;
      }
    } else {
      const lo = t[3], hi = t[4];
      if (scan < lo || scan > hi) continue;
      const i = scan - lo;
      let v = (opts & (ASCII_ONLY | ZERO_SCAN)) ? (scan << 8) | t[5 + i] : u16(t, 5 + 2 * i);
      if (opts & ZERO_SCAN) v &= 0xFF;
      return v;
    }
  }
  return null;
}

/**
 * KEYB_STATE_PROCESSOR for one make code. flags = [KB_FLAG, KB_FLAG_1, KB_FLAG_2,
 * KB_FLAG_3]; nls = { f1, f2 } (the dead-key state, kept between calls).
 * Returns { exit, out: [buffer entries], beep }: exit = true means the key was
 * handled (INT 9 ends), false = continue with the US (BIOS) translation.
 */
export function processKey(t, scan, flags, nls, kbType = G_KB) {
  const f = [...flags, 0, nls.f1, nls.f2];
  let ext = 0;
  if (f[0] & (RIGHT_SHIFT | LEFT_SHIFT)) ext |= EITHER_SHIFT;
  if ((f[0] & CTL_SHIFT) || (f[3] & R_CTL_SHIFT)) ext |= EITHER_CTL;
  if ((f[0] & ALT_SHIFT) || (f[3] & R_ALT_SHIFT)) ext |= EITHER_ALT;
  f[4] = ext;
  const out = [];
  let beep = false, option = 0, nest = 0, level = 0, takeElse = false;
  const L = t.logic;
  let si = 4;
  const fill = (v) => { if ((v & 0xFF) === 0xFF || (v >> 8) === 0xFF) return; out.push(v & 0xFFFF); };
  const done = (exit) => { nls.f1 = f[5]; nls.f2 = f[6]; return { exit, out, beep }; };
  for (let guard = 0; guard < 100000; guard++) {
    const op = L[si], cmd = op >> 4;
    switch (cmd) {
      case 0x0: case 0x1: {            // IFF / ANDF
        if (cmd === 0x0) {
          if (nest === level) {
            const v = f[op & 7], m = L[si + 1];
            const hit = (op & 8) ? !(v & m) : !!(v & m);
            if (hit) { level++; takeElse = false; } else takeElse = true;
          }
          nest++;
        } else if (nest === level) {
          const v = f[op & 7], m = L[si + 1];
          const hit = (op & 8) ? !(v & m) : !!(v & m);
          if (!hit) { takeElse = true; level--; }
        }
        si += 2; break;
      }
      case 0x2:                        // ELSEF
        if (level === nest) level--;
        else if (takeElse) { nest--; if (level === nest) { level++; takeElse = false; } nest++; }
        si += 1; break;
      case 0x3:                        // ENDIFF
        if (level === nest) level--;
        nest--; si += 1; break;
      case 0x4: {                      // XLATT
        if (level === nest) {
          const id = L[si + 1];
          let v = null;
          const a = findState(t.specific, id);
          if (a) { f[4] &= ~SCAN_MATCH; v = translate(a, scan); }
          if (v === null) { const c = findState(t.common, id); if (c) { f[4] &= ~SCAN_MATCH; v = translate(c, scan); } }
          if (v !== null) {
            fill(v);
            f[4] |= SCAN_MATCH;
            if (option & EXIT_IF_FOUND) return done(true);
          }
        }
        si += 2; break;
      }
      case 0x5:                        // OPTION
        if (level === nest) { if (op & 8) option &= ~L[si + 1]; else option |= L[si + 1]; }
        si += 2; break;
      case 0x6: {                      // SET_FLAG (flag tables live in the common section)
        if (nest === level) {
          const st = findState(t.common, L[si + 1]);
          if (st) {
            f[4] &= ~SCAN_MATCH;
            const ent = (st.flags || []).find((e) => e[0] === scan);
            if (ent) {
              f[5] = 0; f[6] = 0;
              f[ent[1]] |= ent[2];
              f[4] |= SCAN_MATCH;
              if (option & EXIT_IF_FOUND) return done(true);
            }
          }
        }
        si += 2; break;
      }
      case 0x7: {                      // PUT_ERROR_CHAR
        if (nest === level) {
          const id = L[si + 1];
          const st = findState(t.specific, id) || findState(t.common, id);
          if (st) fill(st.err);
        }
        si += 2; break;
      }
      case 0x8:                        // IFKBD
        if (nest === level) { if (u16(L, si + 1) & kbType) { level++; takeElse = false; } else takeElse = true; }
        nest++; si += 3; break;
      case 0x9: {                      // GOTO, EXIT_INT_9, EXIT_STATE_LOGIC
        if (nest === level) {
          const sub = op & 0x0F;
          if (sub === 1) return done(true);
          if (sub === 2) return done(false);
          if (sub !== 0) return done(true);
          const rel = (u16(L, si + 1) << 16) >> 16;
          si += rel; level = 0; nest = 0;
        }
        si += 3; break;
      }
      case 0xA: if (nest === level) beep = true; si += 1; break;    // BEEP
      case 0xB: if (nest === level) { f[5] = 0; f[6] = 0; } si += 1; break;   // RESET_NLS
      default: return done(true);      // FATAL_ERROR
    }
  }
  throw new Error('state logic does not terminate');
}
