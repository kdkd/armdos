// App icons for the home screen, drawn in code and written as PNGs (no canvas
// library): a beige VGA monitor with an amber "ARM" and the C:\> prompt in the
// real IBM VGA font. makeIcons(outDir, font) writes apple-touch-icon.png
// (180), icon-192.png, icon-512.png and icon-maskable-512.png.
import { writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { deflateSync } from 'node:zlib';

const CRC = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c; });
const crc32 = (buf) => { let c = -1; for (const b of buf) c = CRC[(c ^ b) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; };
function png(w, h, rgba) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 4 + 1)] = 0; rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4); }
  const chunk = (type, data) => { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const c = Buffer.alloc(4); c.writeUInt32BE(crc32(td)); return Buffer.concat([len, td, c]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

const hex = (s) => [parseInt(s.slice(1, 3), 16), parseInt(s.slice(3, 5), 16), parseInt(s.slice(5, 7), 16)];
const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
// signed distance to a rounded box centred at (cx,cy) with half size (hx,hy), radius r
function rbox(x, y, cx, cy, hx, hy, r) {
  const dx = Math.abs(x - cx) - (hx - r), dy = Math.abs(y - cy) - (hy - r);
  return Math.hypot(Math.max(dx, 0), Math.max(dy, 0)) + Math.min(Math.max(dx, dy), 0) - r;
}

function draw(size, font, { maskable = false } = {}) {
  const SS = 4, out = Buffer.alloc(size * size * 4);
  const k = maskable ? 0.74 : 0.9;                    // content scale (maskable keeps to the safe circle)
  const bgTop = hex('#2a231b'), bgBot = hex('#0e0c0a');
  const beige = hex('#ddd5c1'), beigeHi = hex('#f1ecdf'), beigeLo = hex('#b9af97'), bezel = hex('#c6bda6');
  const glass = hex('#0d1211'), amber = hex('#ffb340'), grey = hex('#d4d8d4');
  // text on the screen: "ARM" (amber) and "C:\>_" (grey), 8x16 glyphs
  const lines = [['ARM', amber], ['C:\\>_', grey]];
  const scr = { x0: 0.235, x1: 0.765, y0: 0.215, y1: 0.585 };
  const cols = 5.5, cellW = (scr.x1 - scr.x0 - 0.07) / (cols * 8), cellH = cellW * 1.0;   // square pixels, 8x16 glyph cell
  const tx0 = scr.x0 + 0.035, ty0 = scr.y0 + ((scr.y1 - scr.y0) - 2 * 16 * cellH - 0.02) / 2;
  function sample(u, v) {
    // back to the unscaled design space
    const x = (u - 0.5) / k + 0.5, y = (v - 0.5) / k + 0.5 + (maskable ? 0.0 : 0.02);
    let c = mix(bgTop, bgBot, v);
    // soft shadow under the monitor
    const sh = rbox(x, y, 0.5, 0.86, 0.33, 0.035, 0.035);
    if (sh < 0.04) c = mix(c, [0, 0, 0], 0.45 * (1 - Math.max(0, sh) / 0.04));
    // foot and neck
    if (rbox(x, y, 0.5, 0.835, 0.24, 0.028, 0.02) < 0) c = mix(beigeLo, beige, 0.3);
    if (rbox(x, y, 0.5, 0.79, 0.12, 0.04, 0.01) < 0) c = beigeLo;
    // the case
    const body = rbox(x, y, 0.5, 0.435, 0.37, 0.33, 0.07);
    if (body < 0) {
      c = mix(beigeHi, beige, Math.min(1, (y - 0.105) / 0.12));
      if (y > 0.68) c = mix(c, beigeLo, (y - 0.68) / 0.09 * 0.6);
      // bezel recess
      const bz = rbox(x, y, 0.5, 0.4, 0.3, 0.215, 0.05);
      if (bz < 0) c = mix(bezel, beigeLo, Math.max(0, Math.min(1, (0.2 - (y - 0.19)) * 2)) * 0.4);
      // the glass
      const gl = rbox(x, y, 0.5, 0.4, (scr.x1 - scr.x0) / 2, (scr.y1 - scr.y0) / 2, 0.045);
      if (gl < 0) {
        c = mix(glass, [0, 0, 0], Math.min(1, Math.hypot(x - 0.5, y - 0.4) * 1.6) * 0.6);
        // glyphs
        lines.forEach(([text, col], row) => {
          const gy = (y - (ty0 + row * 16 * cellH + row * 0.02)) / cellH;
          if (gy < 0 || gy >= 16) return;
          const gx = (x - tx0) / cellW;
          const ci = Math.floor(gx / 8);
          if (ci < 0 || ci >= text.length) return;
          const ch = text.charCodeAt(ci), px = Math.floor(gx - ci * 8), py = Math.floor(gy);
          if ((font[ch * 16 + py] >> (7 - px)) & 1) c = col;
        });
        // a little glass sheen
        if (y < 0.32 && x < 0.55) c = mix(c, [255, 255, 255], 0.06 * (1 - (y - scr.y0) / 0.1));
      }
      // power LED
      if (Math.hypot(x - 0.79, y - 0.69) < 0.016) c = hex('#6dff8e');
    }
    return c;
  }
  for (let py = 0; py < size; py++) for (let px = 0; px < size; px++) {
    let r = 0, g = 0, b = 0;
    for (let sy = 0; sy < SS; sy++) for (let sx = 0; sx < SS; sx++) {
      const c = sample((px + (sx + 0.5) / SS) / size, (py + (sy + 0.5) / SS) / size);
      r += c[0]; g += c[1]; b += c[2];
    }
    const o = (py * size + px) * 4, n = SS * SS;
    out[o] = r / n; out[o + 1] = g / n; out[o + 2] = b / n; out[o + 3] = 255;
  }
  return png(size, size, out);
}

export function makeIcons(dir, font) {
  mkdirSync(dir, { recursive: true });
  writeFileSync(join(dir, 'apple-touch-icon.png'), draw(180, font));
  writeFileSync(join(dir, 'icon-192.png'), draw(192, font));
  writeFileSync(join(dir, 'icon-512.png'), draw(512, font));
  writeFileSync(join(dir, 'icon-maskable-512.png'), draw(512, font, { maskable: true }));
}
