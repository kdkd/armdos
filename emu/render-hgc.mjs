// Display renderer for the Hercules Graphics Card (dev/hercules.mjs), used by
// render.mjs when the machine has one (machine.hgc).
//   text (MDA):          720x350, 80x25 cells of 9x14, attributes the MDA way
//   Hercules graphics:   720x348, 1 bpp, 4-way interleave:
//                        offset = 0x2000*(y&3) + 90*(y>>2) + x/8 from the page base
// The phosphor colour (machine.hgc.monitor) is applied here, so every consumer
// (the page's WebGL/2D display, PNG screenshots) sees a green/amber/white screen.

import { HGC_FRAME_NS, PHOSPHORS } from './dev/hercules.mjs';
import { MDA_FONT } from './dev/mdafont.mjs';

const LE = new Uint8Array(new Uint32Array([1]).buffer)[0] === 1;
function rgba(r, g, b) { return LE ? ((255 << 24) | (b << 16) | (g << 8) | r) >>> 0 : ((r << 24) | (g << 16) | (b << 8) | 255) >>> 0; }

export function hgcDisplaySize(hgc) { return hgc.graphics ? [720, 348] : [720, 350]; }

function levels(hgc) {
  const p = (PHOSPHORS[hgc.monitor] || PHOSPHORS.green).rgb;
  return p.map(([r, g, b]) => rgba(r, g, b));
}

/**
 * MDA attribute byte -> [fg level, bg level, underline] (0 off, 1 normal, 2 bright).
 * 00h/08h/80h/88h: nothing shown; x0h with background 7 (70h, 78h, F0h, F8h): reverse
 * video; foreground 1 (01h, 09h, 81h, 89h): underlined; bit 3: intensity; bit 7: blink
 * (or, with blink disabled, a bright background for reverse video). Any other
 * background colour shows as black.
 */
export function mdaAttr(at, blinkEnabled, blinkOff) {
  let fg, bg, ul = false;
  const a = at & 0x77;
  if (a === 0x00) { fg = 0; bg = 0; }
  else if (a === 0x70) { fg = 0; bg = (!blinkEnabled && (at & 0x80)) ? 2 : 1; }
  else { fg = (at & 8) ? 2 : 1; bg = 0; ul = (at & 7) === 1; }
  if (blinkEnabled && (at & 0x80) && blinkOff) { fg = bg; ul = false; }
  return [fg, bg, ul];
}

export function renderHercules(machine, out = null, timeMs = undefined) {
  const hgc = machine.hgc, m8 = machine.cpu.m8;
  const [w, h] = hgcDisplaySize(hgc);
  if (!out || out.width !== w || out.height !== h) {
    const data = new Uint8ClampedArray(w * h * 4);
    out = { width: w, height: h, data, u32: new Uint32Array(data.buffer) };
  }
  if (!out.u32) out.u32 = new Uint32Array(out.data.buffer, out.data.byteOffset, w * h);
  const px = out.u32;
  const tNs = timeMs === undefined ? machine.timeNs() : timeMs * 1e6;
  const frame = Math.floor(tNs / HGC_FRAME_NS);
  const lv = levels(hgc);
  if (!hgc.videoEnabled) { px.fill(lv[0]); return out; }
  const base = hgc.pageBase;
  if (hgc.graphics) {
    const on = lv[1], off = lv[0];
    const start = (hgc.startAddr * 2) & 0x1FFF;
    for (let y = 0; y < 348; y++) {
      const row = base + ((y & 3) << 13);
      const o0 = (start + (y >> 2) * 90) & 0x1FFF;
      let o = y * 720;
      for (let x = 0; x < 90; x++) {
        const b = m8[row + ((o0 + x) & 0x1FFF)];
        for (let k = 7; k >= 0; k--) px[o++] = (b >> k) & 1 ? on : off;
      }
    }
    return out;
  }
  // text
  const blinkEn = hgc.blinkEnabled, blinkOff = ((frame >> 4) & 1) === 0;
  const start = hgc.startAddr;
  for (let row = 0; row < 25; row++) {
    for (let col = 0; col < 80; col++) {
      const a0 = base + (((start + row * 80 + col) * 2) & 0x7FFF);
      const c = m8[a0], at = m8[a0 + 1];
      const [f, b, ul] = mdaAttr(at, blinkEn, blinkOff);
      const fg = lv[f], bg = lv[b];
      const g = c * 14, ninth = c >= 0xC0 && c <= 0xDF;
      let o = row * 14 * 720 + col * 9;
      for (let y = 0; y < 14; y++, o += 720) {
        let p = o;
        if (ul && y === 12) { for (let k = 0; k < 9; k++) px[p++] = fg; continue; }
        const bits = MDA_FONT[g + y];
        for (let k = 7; k >= 0; k--) px[p++] = (bits >> k) & 1 ? fg : bg;
        px[p] = ninth && (bits & 1) ? fg : bg;
      }
    }
  }
  // cursor: R10 bits 5-6 = 01 hides it; otherwise it blinks every 8 frames (MDA)
  const r10 = hgc.crtc[10], cs = r10 & 31, ce = hgc.crtc[11] & 31;
  const pos = hgc.cursorPos - start;
  if (((r10 >> 5) & 3) !== 1 && pos >= 0 && pos < 2000 && ((frame >> 3) & 1)) {
    const row = Math.floor(pos / 80), col = pos % 80;
    const at = m8[base + ((hgc.cursorPos * 2) & 0x7FFF) + 1];
    const f = mdaAttr(at, false, false)[0];
    const fg = lv[f || 1];
    for (let y = 0; y < 14; y++) {
      if (cs <= ce ? (y < cs || y > ce) : (y < cs && y > ce)) continue;
      let o = (row * 14 + y) * 720 + col * 9;
      for (let k = 0; k < 9; k++) px[o++] = fg;
    }
  }
  return out;
}

/** The text screen as strings (CP437 table from render.mjs); graphics: blank lines. */
export function hgcScreenLines(machine, cp437) {
  const hgc = machine.hgc, m8 = machine.cpu.m8;
  const lines = [];
  for (let row = 0; row < 25; row++) {
    let s = '';
    if (!hgc.graphics) {
      for (let col = 0; col < 80; col++) {
        const c = m8[hgc.pageBase + (((hgc.startAddr + row * 80 + col) * 2) & 0x7FFF)];
        s += c === 0 ? ' ' : cp437[c];
      }
    } else s = ' '.repeat(80);
    lines.push(s);
  }
  return lines;
}
