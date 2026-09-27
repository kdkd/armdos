#!/usr/bin/env node
// apps/paint/samples/mksamples.mjs - draws ARM Paint's sample pictures
// (C:\PAINT\SAMPLES) with code and writes them as 320x200 256-colour PCX:
//
//   SPLASH.PCX   the ARM-DOS 4.00 splash: chrome letters over a synthwave grid
//   SUNSET.PCX   a sunset over the sea with a palm tree
//   ARMAT.PCX    a pixel-art portrait of the ARM/AT computer
//
//   node apps/paint/samples/mksamples.mjs      (writes next to this script)
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const FONT = fs.readFileSync(path.join(HERE, '../../../emu/fonts/cga8x8.bin'));
const W = 320, H = 200;

// the VGA's power-on palette (bios/video.c dac_default256), 8-bit
export function vgaDefault() {
  const out = [];
  for (let i = 0; i < 16; i++) {
    let b = (i & 1) ? 42 : 0, g = (i & 2) ? 42 : 0, r = (i & 4) ? 42 : 0;
    if (i === 6) g = 21;
    if (i & 8) { r += 21; g += 21; b += 21; }
    out.push(r, g, b);
  }
  for (const v of [0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63]) out.push(v, v, v);
  const ramps = [[0, 16, 31, 47, 63], [31, 39, 47, 55, 63], [45, 49, 54, 58, 63], [0, 7, 14, 21, 28], [14, 17, 21, 24, 28],
    [20, 22, 24, 26, 28], [0, 4, 8, 12, 16], [8, 10, 12, 14, 16], [11, 12, 13, 15, 16]];
  for (const v of ramps) {
    const lo = v[0], hi = v[4];
    for (let k = 0; k < 24; k++) {
      const seg = Math.floor(k / 4), pos = k % 4;
      const rgb = [[v[pos], lo, hi], [hi, lo, v[4 - pos]], [hi, v[pos], lo], [v[4 - pos], hi, lo], [lo, hi, v[pos]], [lo, v[4 - pos], hi]][seg];
      out.push(...rgb);
    }
  }
  while (out.length < 768) out.push(0);
  return out.map((v) => (v << 2) | (v >> 4));
}

// ------------------------------------------------------------------ helpers
class Pic {
  constructor() { this.px = new Uint8Array(W * H); this.pal = new Uint8Array(768); this.n = 32; }
  // (0-31 stay the VGA's 16 colours and 16 greys: ARM Paint draws its tool box with them)
  /** add a colour (0-255 per channel), returns its index */
  col(r, g, b) { const i = this.n++; this.pal.set([r, g, b].map((v) => Math.max(0, Math.min(255, Math.round(v)))), i * 3); return i; }
  ramp(n, a, b) { const first = this.n; for (let i = 0; i < n; i++) { const t = n > 1 ? i / (n - 1) : 0; this.col(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t); } return first; }
  set(x, y, c) { x |= 0; y |= 0; if (x >= 0 && y >= 0 && x < W && y < H) this.px[y * W + x] = c; }
  get(x, y) { return this.px[y * W + x]; }
  rect(x0, y0, x1, y1, c) { for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) this.set(x, y, c); }
  frame(x0, y0, x1, y1, c) { for (let x = x0; x <= x1; x++) { this.set(x, y0, c); this.set(x, y1, c); } for (let y = y0; y <= y1; y++) { this.set(x0, y, c); this.set(x1, y, c); } }
  disc(cx, cy, r, c) { for (let y = -r; y <= r; y++) for (let x = -r; x <= r; x++) if (x * x + y * y <= r * r + r) this.set(cx + x, cy + y, c); }
  line(x0, y0, x1, y1, c) {
    const n = Math.max(Math.abs(x1 - x0), Math.abs(y1 - y0), 1);
    for (let i = 0; i <= n; i++) this.set(Math.round(x0 + (x1 - x0) * i / n), Math.round(y0 + (y1 - y0) * i / n), c);
  }
  /** text in the ROM font, scaled; colour may be a function (x, y, row) */
  text(s, x, y, c, sx = 1, sy = sx) {
    for (const ch of s) {
      const g = FONT.subarray(ch.charCodeAt(0) * 8, ch.charCodeAt(0) * 8 + 8);
      for (let r = 0; r < 8; r++) for (let b = 0; b < 8; b++) if (g[r] & (0x80 >> b))
        for (let j = 0; j < sy; j++) for (let i = 0; i < sx; i++) {
          const px = x + b * sx + i, py = y + r * sy + j;
          this.set(px, py, typeof c === 'function' ? c(px, py, r * sy + j) : c);
        }
      x += 8 * sx;
    }
    return x;
  }
}

const BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
const dith = (x, y) => (BAYER[(y & 3) * 4 + (x & 3)] + 0.5) / 16;
let seed = 1988;
const rand = () => ((seed = (seed * 1103515245 + 12345) >>> 0) >>> 8) / 16777216;

export function pcx(pic) {
  const h = Buffer.alloc(128);
  h[0] = 0x0A; h[1] = 5; h[2] = 1; h[3] = 8;
  h.writeUInt16LE(W - 1, 8); h.writeUInt16LE(H - 1, 10); h.writeUInt16LE(320, 12); h.writeUInt16LE(200, 14);
  for (let i = 0; i < 48; i++) h[16 + i] = pic.pal[i];
  h[65] = 1; h.writeUInt16LE(W, 66); h.writeUInt16LE(1, 68); h.writeUInt16LE(320, 70); h.writeUInt16LE(200, 72);
  const out = [h];
  for (let y = 0; y < H; y++) {
    const line = []; const s = pic.px.subarray(y * W, y * W + W);
    for (let i = 0; i < W;) {
      let run = 1;
      while (i + run < W && run < 63 && s[i + run] === s[i]) run++;
      if (run > 1 || s[i] >= 0xC0) line.push(0xC0 | run);
      line.push(s[i]); i += run;
    }
    out.push(Buffer.from(line));
  }
  // PCX palettes are 8-bit; the VGA DAC takes the top 6 bits
  out.push(Buffer.from([0x0C]), Buffer.from(pic.pal));
  return Buffer.concat(out);
}

// ------------------------------------------------------------------ SPLASH
function splash() {
  const p = new Pic();
  const black = p.col(0, 0, 0);
  const sky = p.ramp(32, [4, 0, 24], [120, 20, 110]);          // night to magenta at the horizon
  const chrome = p.ramp(24, [255, 255, 255], [40, 60, 150]);    // letter faces
  const chrome2 = p.ramp(16, [255, 190, 60], [150, 30, 40]);    // lower half: warm reflection
  const gridc = p.ramp(8, [40, 0, 70], [255, 60, 220]);
  const floor = p.col(16, 0, 30);
  const sun = p.ramp(16, [255, 240, 90], [255, 40, 120]);
  const white = p.col(255, 255, 255), grey = p.col(150, 150, 170), cyan = p.col(90, 230, 255);
  const hz = 128;
  for (let y = 0; y < hz; y++) for (let x = 0; x < W; x++) p.set(x, y, sky + Math.min(31, Math.floor((y / hz) * 31 + dith(x, y))));
  // stars
  for (let i = 0; i < 90; i++) { const x = Math.floor(rand() * W), y = Math.floor(rand() * 80); p.set(x, y, rand() < 0.2 ? white : grey); }
  // the sun, with the classic slices
  for (let y = -44; y <= 0; y++) for (let x = -44; x <= 44; x++) {
    if (x * x + y * y > 44 * 44) continue;
    const yy = hz + y - 2;
    const band = -y;
    if (band < 22 && (band % 6) < (6 - Math.floor(band / 4))) continue;
    p.set(160 + x, yy, sun + Math.min(15, Math.floor((44 + y) / 44 * 15)));
  }
  // the floor grid in perspective
  for (let y = hz; y < H; y++) for (let x = 0; x < W; x++) p.set(x, y, floor);
  for (let k = 1; k < 14; k++) {
    const y = hz + Math.round(Math.pow(k / 13, 2.2) * (H - hz));
    for (let x = 0; x < W; x++) p.set(x, y, gridc + Math.min(7, 2 + (k >> 1)));
  }
  for (let k = -12; k <= 12; k++) p.line(160 + k * 6, hz, 160 + k * 60, H - 1, gridc + 5);
  for (let x = 0; x < W; x++) p.set(x, hz, gridc + 7);
  // ARM-DOS in chrome, 5x
  const tx = 160 - 7 * 20, ty = 34;
  const face = (x, y, r) => r < 20 ? chrome + Math.min(23, Math.floor(r * 23 / 20)) : chrome2 + Math.min(15, Math.floor((r - 20) * 15 / 19));
  p.text('ARM-DOS', tx + 3, ty + 3, black, 5, 5);            // drop shadow
  p.text('ARM-DOS', tx, ty, face, 5, 5);
  const v = '4.00';
  p.text(v, 160 - v.length * 8 + 2, ty + 48, black, 2, 2);
  p.text(v, 160 - v.length * 8, ty + 46, cyan, 2, 2);
  const e = 'Europa Micro Systems';
  p.text(e, 160 - e.length * 4, 186, white);
  return p;
}

// ------------------------------------------------------------------ SUNSET
function sunset() {
  const p = new Pic();
  const sky = p.ramp(48, [30, 10, 70], [255, 170, 60]);        // violet to orange
  const sea = p.ramp(32, [120, 40, 70], [10, 10, 40]);         // near the horizon to the foreground
  const sun = p.ramp(12, [255, 250, 180], [255, 120, 40]);
  const glint = p.ramp(8, [255, 230, 150], [230, 110, 60]);
  const hills = p.col(70, 20, 60), hills2 = p.col(45, 12, 45);
  const black = p.col(12, 4, 16), trunk = p.col(24, 8, 22);
  const cloud = p.ramp(6, [255, 140, 120], [140, 60, 110]);
  const hz = 128;
  for (let y = 0; y < hz; y++) for (let x = 0; x < W; x++) {
    const t = Math.pow(y / hz, 1.3) * 47;
    p.set(x, y, sky + Math.min(47, Math.floor(t + dith(x, y))));
  }
  // thin clouds
  for (const [cy, cx, len] of [[40, 60, 90], [52, 190, 110], [70, 20, 70], [84, 230, 80], [96, 120, 60]]) {
    for (let x = cx; x < cx + len; x++) {
      const w = Math.sin((x - cx) / len * Math.PI) * 2.2;
      for (let d = -w; d <= w; d++) p.set(x, cy + Math.round(d * 0.8), cloud + Math.min(5, Math.floor(Math.abs(d) * 2 + (cy - 40) / 20)));
    }
  }
  // the sun, half set
  const sx = 205, sy = hz - 4;
  for (let y = -30; y <= 30; y++) for (let x = -30; x <= 30; x++) {
    if (x * x + y * y > 900 || sy + y >= hz) continue;
    p.set(sx + x, sy + y, sun + Math.min(11, Math.floor((y + 30) / 60 * 11 + dith(x, y) * 1.5)));
  }
  // distant hills on the horizon
  for (let x = 0; x < W; x++) {
    const h1 = Math.round(6 + 5 * Math.sin(x / 23) + 3 * Math.sin(x / 7.3 + 1));
    const h2 = Math.round(3 + 3 * Math.sin(x / 31 + 2));
    if (x < 120) for (let y = hz - h1; y < hz; y++) p.set(x, y, hills);
    if (x > 260) for (let y = hz - h2 - 2; y < hz; y++) p.set(x, y, hills2);
  }
  // the sea
  for (let y = hz; y < H; y++) for (let x = 0; x < W; x++) p.set(x, y, sea + Math.min(31, Math.floor((y - hz) / (H - hz) * 31 + dith(x, y))));
  // the sun's path on the water
  for (let y = hz + 1; y < H; y += 2) {
    const d = (y - hz) / (H - hz);
    const half = 26 - d * 10 + rand() * 6;
    for (let x = sx - half; x < sx + half; x++) {
      if (rand() < 0.45 + d * 0.3) continue;
      const len = 2 + Math.floor(rand() * 7);
      for (let i = 0; i < len; i++) p.set(x + i, y, glint + Math.min(7, Math.floor(d * 7 + rand() * 2)));
      x += len + 1;
    }
  }
  // a palm tree
  const pts = [];
  for (let i = 0; i <= 40; i++) { const t = i / 40; pts.push([50 + Math.sin(t * 1.6) * 26, H - 1 - t * 120]); }
  pts.forEach(([x, y], i) => { const w = 5 - i / 12; p.rect(Math.round(x - w), Math.round(y), Math.round(x + w), Math.round(y) + 3, trunk); });
  const [px, py] = pts[pts.length - 1];
  for (const [ang, len] of [[-2.8, 62], [-2.3, 55], [-1.7, 40], [-1.1, 48], [-0.5, 60], [0.1, 58], [0.6, 50], [2.4, 45], [3.0, 50]]) {
    for (let i = 0; i < len; i++) {
      const t = i / len;
      const x = px + Math.cos(ang) * i, y = py + Math.sin(ang) * i + t * t * 30;
      const w = Math.sin(t * Math.PI) * 5;
      p.set(Math.round(x), Math.round(y), black);
      for (let k = 1; k <= w; k++) if ((i + k) % 3) { p.set(Math.round(x - k * 0.5), Math.round(y + k), black); p.set(Math.round(x + k * 0.3), Math.round(y - k * 0.6), black); }
    }
  }
  p.disc(Math.round(px), Math.round(py) + 2, 4, black);
  // birds
  for (const [bx, by, s] of [[150, 50, 3], [165, 44, 2], [176, 56, 2], [120, 64, 2]]) {
    p.line(bx - s * 2, by - s, bx, by, black); p.line(bx, by, bx + s * 2, by - s, black);
  }
  return p;
}

// ------------------------------------------------------------------ ARM/AT
function armat() {
  const p = new Pic();
  const wall = p.ramp(16, [80, 110, 140], [50, 70, 100]);
  const desk = p.ramp(8, [140, 95, 55], [95, 60, 35]);
  const beige = p.col(222, 214, 190), beigeL = p.col(242, 236, 218), beigeD = p.col(170, 160, 136), beigeDD = p.col(120, 112, 96);
  const glass = p.col(18, 24, 20), glassL = p.col(40, 52, 44), phos = p.col(170, 240, 170), phosD = p.col(80, 160, 90);
  const black = p.col(10, 10, 10), slot = p.col(40, 38, 34), green = p.col(40, 255, 60), amber = p.col(255, 170, 30);
  const keyc = p.col(200, 192, 170), keyD = p.col(150, 142, 122), keyS = p.col(110, 104, 90), red = p.col(200, 40, 30);
  const badge = p.col(30, 60, 140), white = p.col(255, 255, 255);
  for (let y = 0; y < 150; y++) for (let x = 0; x < W; x++) p.set(x, y, wall + Math.min(15, Math.floor(y / 150 * 15 + dith(x, y))));
  for (let y = 150; y < H; y++) for (let x = 0; x < W; x++) p.set(x, y, desk + Math.min(7, Math.floor((y - 150) / 50 * 7 + dith(x, y) + (Math.sin(x / 9 + y / 3) > 0.9 ? 1 : 0))));
  // system unit
  const ux0 = 60, ux1 = 260, uy0 = 110, uy1 = 150;
  p.rect(ux0, uy0, ux1, uy1, beige); p.frame(ux0, uy0, ux1, uy1, beigeDD);
  for (let x = ux0 + 1; x < ux1; x++) p.set(x, uy0 + 1, beigeL);
  for (let x = ux0 + 1; x < ux1; x++) p.set(x, uy1 - 1, beigeD);
  // drives: 5.25" and 3.5"
  p.rect(176, 117, 250, 128, beigeD); p.rect(178, 121, 248, 123, slot); p.rect(208, 124, 216, 126, beigeDD);
  p.rect(176, 132, 250, 143, beigeD); p.rect(186, 136, 232, 138, slot); p.rect(236, 139, 244, 141, beigeDD);
  // badge, LEDs, turbo, power
  p.rect(70, 118, 118, 128, badge); p.text('ARM/AT', 71, 119, white);
  p.rect(70, 136, 73, 138, green); p.rect(80, 136, 83, 138, amber); p.rect(90, 136, 93, 138, red);
  p.rect(130, 133, 146, 143, beigeD); p.frame(130, 133, 146, 143, beigeDD);
  p.rect(150, 133, 166, 143, beigeD); p.frame(150, 133, 166, 143, beigeDD);
  for (let x = ux0 + 4; x < 165; x += 4) p.set(x, uy1 - 4, beigeDD);
  // monitor
  const mx0 = 90, mx1 = 230, my0 = 8, my1 = 104;
  p.rect(130, my1, 190, uy0 - 1, beigeD); p.rect(115, uy0 - 4, 205, uy0 - 1, beigeDD);
  p.rect(mx0, my0, mx1, my1, beige); p.frame(mx0, my0, mx1, my1, beigeDD);
  for (let x = mx0 + 1; x < mx1; x++) p.set(x, my0 + 1, beigeL);
  for (let y = my0 + 1; y < my1; y++) p.set(mx0 + 1, y, beigeL);
  p.rect(mx0 + 8, my0 + 8, mx1 - 8, my1 - 14, beigeDD);
  // the tube, slightly rounded
  const sx0 = mx0 + 11, sx1 = mx1 - 11, sy0 = my0 + 11, sy1 = my1 - 17;
  for (let y = sy0; y <= sy1; y++) for (let x = sx0; x <= sx1; x++) {
    const cx = (x - (sx0 + sx1) / 2) / ((sx1 - sx0) / 2), cy = (y - (sy0 + sy1) / 2) / ((sy1 - sy0) / 2);
    if (Math.pow(Math.abs(cx), 8) + Math.pow(Math.abs(cy), 8) > 1) continue;
    p.set(x, y, (cx < -0.4 && cy < -0.3 && (x + y) % 5 === 0) ? glassL : glass);
  }
  const lines = ['ARM/AT BIOS', '640K OK', '', 'C:\\>ver', 'ARM-DOS 4.00', '', 'C:\\>'];
  lines.forEach((s, i) => p.text(s.slice(0, 14), sx0 + 5, sy0 + 3 + i * 9, i < 2 ? phosD : phos));
  p.rect(sx0 + 5 + 4 * 8, sy0 + 3 + 6 * 9 + 6, sx0 + 5 + 4 * 8 + 6, sy0 + 3 + 6 * 9 + 7, phos);
  p.rect(mx1 - 30, my1 - 10, mx1 - 22, my1 - 6, beigeD); p.set(mx1 - 14, my1 - 8, green); p.set(mx1 - 13, my1 - 8, green);
  // keyboard
  const kx0 = 70, kx1 = 250, ky0 = 160, ky1 = 192;
  p.rect(kx0, ky0, kx1, ky1, beigeD); p.frame(kx0, ky0, kx1, ky1, beigeDD);
  for (let x = kx0 + 1; x < kx1; x++) p.set(x, ky0 + 1, beige);
  for (let r = 0; r < 5; r++) {
    const off = [0, 3, 5, 7, 0][r];
    for (let k = 0; k < 14 - (r === 4 ? 0 : 0); k++) {
      const x = kx0 + 5 + off + k * 12, y = ky0 + 4 + r * 6;
      if (r === 4) { if (k === 0) { p.rect(kx0 + 40, y, kx0 + 130, y + 4, keyc); p.rect(kx0 + 40, y + 4, kx0 + 130, y + 4, keyS); } continue; }
      if (x + 10 > kx1 - 36) continue;
      p.rect(x, y, x + 9, y + 4, keyc); p.rect(x, y + 4, x + 9, y + 4, keyS); p.set(x + 1, y + 1, keyD);
    }
  }
  for (let r = 0; r < 4; r++) for (let k = 0; k < 3; k++) { const x = kx1 - 33 + k * 10, y = ky0 + 4 + r * 6; p.rect(x, y, x + 7, y + 4, keyc); p.rect(x, y + 4, x + 7, y + 4, keyS); }
  // the mouse and its cord
  p.rect(272, 170, 290, 190, beige); p.frame(272, 170, 290, 190, beigeDD);
  p.rect(272, 170, 290, 176, beigeL); p.line(281, 170, 281, 176, beigeDD); p.frame(272, 170, 290, 176, beigeDD);
  for (let i = 0; i < 40; i++) p.set(281 + Math.round(Math.sin(i / 6) * 4) - i / 5, 169 - i, black);
  // shadow under the unit
  for (let x = ux0 + 2; x <= ux1 + 2; x++) p.set(x, uy1 + 1, desk + 7);
  return p;
}

const pics = { 'SPLASH.PCX': splash(), 'SUNSET.PCX': sunset(), 'ARMAT.PCX': armat() };
for (const [name, pic] of Object.entries(pics)) {
  if (pic.n > 256) throw new Error(`${name}: ${pic.n} colours`);
  const def = vgaDefault();
  for (let i = 0; i < 768; i++) if (i < 96 || i >= pic.n * 3) pic.pal[i] = def[i];   // the rest: the default palette
  fs.writeFileSync(path.join(HERE, name), pcx(pic));
  console.log(`${name}: ${pic.n} colours`);
}
