// Display renderer: produces an RGBA image of the current display from guest
// memory and the VGA registers (ARCH.md §6).
//   text modes 00h-03h: 720x400 (9x16 cells; 40-column modes are pixel-doubled)
//   CGA 04h/05h: 320x200, 06h: 640x200, mode 13h: 320x200,
//   ARM-PC mode 62h: 640x480 256 colours, linear from A0000h
// Graphics modes are output at native size; the page scales them.
// A machine with the Hercules card (machine.hgc) is drawn by render-hgc.mjs
// (720x350 MDA text / 720x348 graphics, phosphor-coloured).

import { FRAME_NS, SNAP_CRTC, SNAP_ATTR, SNAP_SEQ, SNAP_PEL, SNAP_DAC, SNAP_SIZE, EV_CRTC, EV_ATTR, EV_DAC, EV_SEQ } from './dev/vga.mjs';
import { renderHercules, hgcScreenLines } from './render-hgc.mjs';

export const CP437 = (
  '\u0000☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼' +
  ' !"#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~⌂' +
  'ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀' +
  'αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ').split('');

const LE = new Uint8Array(new Uint32Array([1]).buffer)[0] === 1;
function rgba(r, g, b) { return LE ? ((255 << 24) | (b << 16) | (g << 8) | r) >>> 0 : ((r << 24) | (g << 16) | (b << 8) | 255) >>> 0; }

// DAC entry -> packed RGBA
function dacColor(vga, i) {
  i &= vga.pelMask;
  const d = vga.dac, k = i * 3;
  const c6 = (v) => (v << 2) | (v >> 4);
  return rgba(c6(d[k]), c6(d[k + 1]), c6(d[k + 2]));
}
// 16-colour palette index (text/CGA) -> DAC index via the attribute controller
function attrToDac(vga, p) {
  const a = vga.attr, cs = a[0x14];
  const v = a[p & 15];
  return (a[0x10] & 0x80) ? ((v & 0x0F) | ((cs & 3) << 4) | ((cs & 0x0C) << 4)) : ((v & 0x3F) | ((cs & 0x0C) << 4));
}

export function displaySize(vga) {
  const kind = vga.displayKind ? vga.displayKind() : 'legacy';
  if (kind === 'vga') { const g = vgaGeometry(vga.crtc, 0, vga.attr, 0); return [g.w, g.outH]; }
  if (kind === 'text' && vga.mode >= 4 && vga.mode !== 0x62) { const ch = charHeight(vga); return [720, Math.floor(400 / ch) * ch]; }
  switch (vga.mode) {
    case 0x04: case 0x05: case 0x13: return [320, 200];
    case 0x06: return [640, 200];
    case 0x62: return [640, 480];
    default: {
      const ch = charHeight(vga);
      return [720, Math.floor(400 / ch) * ch];
    }
  }
}
// text cell height from CRTC max scan line (reg 9); 400 scan lines per screen
export function charHeight(vga) { return Math.max(4, Math.min(32, (vga.crtc[9] & 31) + 1)); }
export function textRows(vga) { return Math.floor(400 / charHeight(vga)); }

/**
 * Render into `out` ({width,height,data}) or a new image. timeMs selects the
 * blink phase (default: emulated time).
 */
export function renderScreen(machine, out = null, timeMs = undefined) {
  if (machine.hgc) return renderHercules(machine, out, timeMs);    // Hercules card + mono monitor
  const vga = machine.vga, m8 = machine.cpu.m8;
  const [w, h] = displaySize(vga);
  if (!out || out.width !== w || out.height !== h) {
    const data = new Uint8ClampedArray(w * h * 4);
    out = { width: w, height: h, data, u32: new Uint32Array(data.buffer) };
  }
  if (!out.u32) out.u32 = new Uint32Array(out.data.buffer, out.data.byteOffset, w * h);
  const px = out.u32;
  const tNs = timeMs === undefined ? machine.timeNs() : timeMs * 1e6;
  const frame = Math.floor(tNs / FRAME_NS);
  const kind = vga.displayKind();
  if (kind === 'vga') { renderVga(machine, px, w, h); return out; }
  if (kind === 'text' && vga.mode >= 4 && vga.mode !== 0x62) { renderText(machine, px, w, h, frame); return out; }
  switch (vga.mode) {
    case 0x13: {
      const pal = new Uint32Array(256);
      for (let i = 0; i < 256; i++) pal[i] = dacColor(vga, i);
      for (let i = 0; i < 64000; i++) px[i] = pal[m8[0xA0000 + i]];
      break;
    }
    case 0x62: {
      const pal = new Uint32Array(256);
      for (let i = 0; i < 256; i++) pal[i] = dacColor(vga, i);
      for (let i = 0; i < 640 * 480; i++) px[i] = pal[m8[0xA0000 + i]];
      break;
    }
    case 0x04: case 0x05: {
      const cs = vga.cgaSel, bright = (cs & 0x10) ? 8 : 0;
      let map;
      if (vga.mode === 0x05) map = [cs & 15, 3 + bright, 4 + bright, 7 + bright];
      else map = (cs & 0x20) ? [cs & 15, 3 + bright, 5 + bright, 7 + bright] : [cs & 15, 2 + bright, 4 + bright, 6 + bright];
      const pal = map.map((p) => dacColor(vga, attrToDac(vga, p)));
      for (let y = 0; y < 200; y++) {
        const base = 0xB8000 + ((y & 1) << 13) + (y >> 1) * 80;
        let o = y * 320;
        for (let x = 0; x < 80; x++) {
          const b = m8[base + x];
          px[o++] = pal[b >> 6]; px[o++] = pal[(b >> 4) & 3]; px[o++] = pal[(b >> 2) & 3]; px[o++] = pal[b & 3];
        }
      }
      break;
    }
    case 0x06: {
      const fg = dacColor(vga, attrToDac(vga, vga.cgaSel & 15)), bg = dacColor(vga, attrToDac(vga, 0));
      for (let y = 0; y < 200; y++) {
        const base = 0xB8000 + ((y & 1) << 13) + (y >> 1) * 80;
        let o = y * 640;
        for (let x = 0; x < 80; x++) {
          const b = m8[base + x];
          for (let k = 7; k >= 0; k--) px[o++] = (b >> k) & 1 ? fg : bg;
        }
      }
      break;
    }
    default:
      renderText(machine, px, w, h, frame);
  }
  return out;
}

function renderText(machine, px, w, h, frame) {
  const vga = machine.vga, m8 = machine.cpu.m8, font = machine.font;
  const cols = vga.mode <= 1 ? 40 : 80, dbl = cols === 40 ? 2 : 1;
  const ch = charHeight(vga), rows = textRows(vga);
  const pal = new Uint32Array(16);
  for (let i = 0; i < 16; i++) pal[i] = dacColor(vga, attrToDac(vga, i));
  const blinkOn = vga.blinkEnabled, blinkPhase = (frame >> 4) & 1;   // 32-frame cycle
  const lg = vga.lineGraphics;
  const start = vga.startAddr;
  const base = 0xB8000;
  for (let row = 0; row < rows; row++) {
    for (let col = 0; col < cols; col++) {
      const cell = start + row * cols + col;
      const a0 = base + ((cell * 2) & 0x7FFF);
      const c = m8[a0], at = m8[a0 + 1];
      let fg = pal[at & 15], bg;
      if (blinkOn) { bg = pal[(at >> 4) & 7]; if ((at & 0x80) && !blinkPhase) fg = bg; }
      else bg = pal[at >> 4];
      const g = c * 32, ninth = lg && c >= 0xC0 && c <= 0xDF;
      let o = row * ch * w + col * 9 * dbl;
      for (let y = 0; y < ch; y++, o += w) {
        const bits = font[g + y];
        let p = o;
        for (let k = 7; k >= 0; k--) { const v = (bits >> k) & 1 ? fg : bg; px[p++] = v; if (dbl === 2) px[p++] = v; }
        const v9 = ninth && (bits & 1) ? fg : bg;
        px[p++] = v9; if (dbl === 2) px[p++] = v9;
      }
    }
  }
  // cursor
  const cs = vga.crtc[0x0A], ce = vga.crtc[0x0B] & 31;
  const pos = vga.cursorPos - start;
  if (!(cs & 0x20) && pos >= 0 && pos < cols * rows && ((frame >> 3) & 1)) {
    const row = Math.floor(pos / cols), col = pos % cols;
    const at = m8[base + ((vga.cursorPos * 2) & 0x7FFF) + 1];
    const fg = pal[at & 15];
    for (let y = cs & 31; y <= ce && y < ch; y++) {
      let o = (row * ch + y) * w + col * 9 * dbl;
      for (let k = 0; k < 9 * dbl; k++) px[o++] = fg;
    }
  }
}

/** The text screen as an array of strings (CP437 -> Unicode). */
export function screenLines(machine) {
  if (machine.hgc) return hgcScreenLines(machine, CP437);
  const vga = machine.vga, m8 = machine.cpu.m8;
  const cols = vga.mode <= 1 ? 40 : 80, rows = textRows(vga);
  const lines = [];
  for (let row = 0; row < rows; row++) {
    let s = '';
    for (let col = 0; col < cols; col++) {
      const c = m8[0xB8000 + (((vga.startAddr + row * cols + col) * 2) & 0x7FFF)];
      s += c === 0 ? ' ' : CP437[c];
    }
    lines.push(s);
  }
  return lines;
}
export function screenText(machine) { return screenLines(machine).map((l) => l.replace(/\s+$/, '')).join('\n'); }

// ------------------------------------------------------------------ VGA graphics
// The register-driven display (dev/vga.mjs): 256-colour (chain-4 RAM or the
// planes: Mode X/Y and friends) and 16-colour planar modes, with the CRTC's
// start address (latched per frame), offset, byte/word/doubleword addressing,
// maximum scan line / double scan, line compare (split screen, with the
// attribute controller's pan reset), vertical display end, horizontal pixel
// and byte panning, and the attribute controller's palette / plane enable /
// colour select. The last complete frame is drawn from its starting register
// state plus the timeline of register writes during it (per scan line).

/** geometry from CRTC/attribute registers (arrays + base offsets) */
export function vgaGeometry(c, cb, a, ab) {
  const c256 = (a[ab + 0x10] & 0x40) !== 0;
  let hde = c[cb + 1] + 1;
  const w = Math.min(1024, hde * (c256 ? 4 : 8));
  let vde = (c[cb + 0x12] | ((c[cb + 7] & 2) << 7) | ((c[cb + 7] & 0x40) << 3)) + 1;
  if (vde > 1024) vde = 1024;
  const rep = ((c[cb + 9] & 31) + 1) * ((c[cb + 9] & 0x80) ? 2 : 1);
  const half = rep % 2 === 0;
  return { c256, w, vde, rep, half, outH: half ? vde >> 1 : vde };
}

const scratch = { st: new Uint8Array(SNAP_SIZE), pal: new Uint32Array(256), line: new Uint8Array(2048 + 64) };

function isStd13(v) {
  const c = v.crtc;
  return v.chain4 && !v.window && (v.attr[0x10] & 0x40) && c[1] === 0x4F && c[0x12] === 0x8F && (c[7] & 0x52) === 0x12 && (c[9] & 0xDF) === 0x41 && c[0x18] === 0xFF && (c[7] & 0x10) &&
    c[0x13] === 0x28 && (c[0x14] & 0x40) && c[0x0C] === 0 && c[0x0D] === 0 && (c[8] & 0x7F) === 0 && (v.attr[0x13] & 15) === 0 &&
    !(v.seq[1] & 0x20);
}

function renderVga(machine, px, w, h) {
  const vga = machine.vga;
  vga.roll(machine.timeNs());
  const n = vga.doneValid ? vga.doneN : 0;
  const ev = vga.doneEv;
  // mode 13h with its standard registers and no raster effects in the last frame: the fast path
  let raster = false;
  for (let i = 0; i < n; i++) if (ev[i * 3] >= 0) { raster = true; break; }
  if (!raster && isStd13(vga) && w === 320 && h === 200) {
    const pal = scratch.pal, m8 = machine.cpu.m8;
    for (let i = 0; i < 256; i++) pal[i] = dacColor(vga, i);
    for (let i = 0; i < 64000; i++) px[i] = pal[m8[0xA0000 + i]];
    return;
  }
  // the register state the frame started with (or the current one)
  const st = scratch.st;
  if (vga.doneValid && raster) st.set(vga.doneStart);
  else { vga.snapshot(st); }
  const events = raster ? n : 0;
  const pal = scratch.pal;
  const c6 = (v) => (v << 2) | (v >> 4);
  const dacAt = (i) => { i &= st[SNAP_PEL]; const k = SNAP_DAC + i * 3; return rgba(c6(st[k]), c6(st[k + 1]), c6(st[k + 2])); };
  const g = vgaGeometry(st, SNAP_CRTC, st, SNAP_ATTR);
  const c256 = g.c256;
  // 16-colour: attribute palette -> DAC index
  const pal16 = new Uint32Array(16);
  const rebuild = () => {
    if (c256) for (let i = 0; i < 256; i++) pal[i] = dacAt(i);
    else {
      const a10 = st[SNAP_ATTR + 0x10], cs = st[SNAP_ATTR + 0x14];
      for (let i = 0; i < 16; i++) {
        const v = st[SNAP_ATTR + i];
        const d = (a10 & 0x80) ? ((v & 0x0F) | ((cs & 3) << 4) | ((cs & 0x0C) << 4)) : ((v & 0x3F) | ((cs & 0x0C) << 4));
        pal16[i] = dacAt(d);
      }
    }
  };
  rebuild();
  const m8 = machine.cpu.m8, vr = vga.vram, vr32 = vga.vram32;
  const chained = !vga.window && vga.chain4;
  let ei = 0, dirty = false;
  const crtc = (i) => st[SNAP_CRTC + i];
  // the start address is latched once per frame
  const ashift = (crtc(0x14) & 0x40) ? 2 : (crtc(0x17) & 0x40) ? 0 : 1;
  const start = ((crtc(0x0C) << 8) | crtc(0x0D)) + ((crtc(8) >> 5) & 3);
  const pitch = (crtc(0x13) * 2) << ashift;
  const ppc = c256 ? 4 : 8;
  const line = scratch.line, black = rgba(0, 0, 0);
  let rowBase = (start << ashift) & 0xFFFF, rowCount = 0, split = false;
  const vde = g.vde, rep = g.rep;
  for (let s = 0; s < vde; s++) {
    // apply the writes that happened before this scan line
    while (ei < events && ev[ei * 3] < s) {
      const code = ev[ei * 3 + 1], v = ev[ei * 3 + 2], kind = code >> 12, idx = code & 0xFFF;
      if (kind === EV_DAC) { st[SNAP_DAC + idx] = v; dirty = true; }
      else if (kind === EV_ATTR) { st[SNAP_ATTR + idx] = v; if (idx <= 0x14) dirty = true; }
      else if (kind === EV_CRTC) { if (idx !== 0x0C && idx !== 0x0D) st[SNAP_CRTC + idx] = v; }
      else if (kind === EV_SEQ) st[SNAP_SEQ + idx] = v;
      else st[SNAP_PEL] = v, dirty = true;
      ei++;
    }
    if (dirty) { rebuild(); dirty = false; }
    const lc = crtc(0x18) | ((crtc(7) & 0x10) << 4) | ((crtc(9) & 0x40) << 3);
    if (s === lc + 1 && s > 0 && !split) { split = true; rowBase = 0; rowCount = 0; }
    else if (s > 0 && rowCount === 0) rowBase = (rowBase + pitch) & 0xFFFF;
    const draw = !g.half || (s & 1) === 0;
    if (draw) {
      const y = g.half ? s >> 1 : s;
      if (y >= h) break;
      let o = y * w;
      if (st[SNAP_SEQ + 1] & 0x20) { for (let x = 0; x < w; x++) px[o + x] = black; }
      else {
        const panReg = (split && (st[SNAP_ATTR + 0x10] & 0x20)) ? 0 : st[SNAP_ATTR + 0x13] & 15;
        const nclk = (w / ppc | 0) + 2;
        if (c256) {
          const pan = (panReg >> 1) & 3;
          let k = 0;
          if (chained && ashift === 2 && (rowBase & 3) === 0) {
            for (let cc = 0; cc < nclk; cc++) {
              const a = 0xA0000 + ((rowBase + (cc << 2)) & 0xFFFF);
              line[k++] = m8[a]; line[k++] = m8[a + 1]; line[k++] = m8[a + 2]; line[k++] = m8[a + 3];
            }
          } else if (chained) {
            for (let cc = 0; cc < nclk; cc++) {
              const off = (rowBase + (cc << ashift)) & 0xFFFF;
              for (let p = 0; p < 4; p++) line[k++] = vga.planeByte(off, p);
            }
          } else {
            for (let cc = 0; cc < nclk; cc++) {
              const q = ((rowBase + (cc << ashift)) & 0xFFFF) << 2;
              line[k++] = vr[q]; line[k++] = vr[q + 1]; line[k++] = vr[q + 2]; line[k++] = vr[q + 3];
            }
          }
          for (let x = 0; x < w; x++) px[o++] = pal[line[x + pan]];
        } else {
          const pan = panReg < 8 ? panReg : 0;
          const en = st[SNAP_ATTR + 0x12] & 15;
          let k = 0;
          for (let cc = 0; cc < nclk; cc++) {
            const l = vr32[(rowBase + (cc << ashift)) & 0xFFFF];
            const p0 = l & 0xFF, p1 = (l >>> 8) & 0xFF, p2 = (l >>> 16) & 0xFF, p3 = l >>> 24;
            for (let b = 7; b >= 0; b--) line[k++] = (((p0 >> b) & 1) | (((p1 >> b) & 1) << 1) | (((p2 >> b) & 1) << 2) | (((p3 >> b) & 1) << 3)) & en;
          }
          for (let x = 0; x < w; x++) px[o++] = pal16[line[x + pan]];
        }
      }
    }
    if (++rowCount >= rep) rowCount = 0;
  }
}
