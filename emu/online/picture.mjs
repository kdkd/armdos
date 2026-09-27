// Pictures for ARM-DOS Online: any image the browser can decode -> a 256-colour
// interlaced GIF87a... er, GIF89a, sized for VGA mode 13h, the way CompuServe's GIFs
// were: at most 320x184 displayed pixels (the viewer keeps the bottom 16 lines for its
// status line), corrected for the mode's tall pixels (320x200 on a 4:3 tube: 1.2:1),
// 240 colours from a median-cut palette with Floyd-Steinberg dithering, and palette
// entries 240-255 left for the viewer's own colours.
//
//   const gif = await makeGif(bytes, { decode })   // decode(bytes, maxW, maxH) -> {width, height, data}
//
// decode defaults to the browser (createImageBitmap + a canvas). Node tests pass their own.

export const MAX_W = 320, MAX_H = 184, ASPECT = 1.2, COLOURS = 240;
// the viewer's colours (indexes 240-255): black, dark grey, grey, white, blue, bright blue, cyan, yellow, red, green
export const UI_PALETTE = [[0, 0, 0], [85, 85, 85], [170, 170, 170], [255, 255, 255], [0, 0, 170], [85, 85, 255], [0, 170, 170], [255, 255, 85],
  [170, 0, 0], [0, 170, 0], [255, 85, 85], [85, 255, 85], [170, 85, 0], [170, 0, 170], [85, 255, 255], [255, 255, 255]];

/** Fit w x h (square pixels) into the mode 13h picture area. */
export function fitSize(w, h, maxW = MAX_W, maxH = MAX_H) {
  const s = Math.min(maxW / w, (maxH * ASPECT) / h, 1);
  let W = Math.max(1, Math.round(w * s)), H = Math.max(1, Math.round((h * s) / ASPECT));
  if (s === 1) { W = w; H = Math.max(1, Math.round(h / ASPECT)); }
  return { width: Math.min(W, maxW), height: Math.min(H, maxH) };
}

/** Area-average resample of RGBA (alpha composited over white). */
export function resample(src, W, H) {
  const { width: w, height: h, data } = src;
  const out = new Float32Array(W * H * 3);
  const sx = w / W, sy = h / H;
  for (let Y = 0; Y < H; Y++) {
    const y0 = Y * sy, y1 = y0 + sy;
    for (let X = 0; X < W; X++) {
      const x0 = X * sx, x1 = x0 + sx;
      let r = 0, g = 0, b = 0, a = 0;
      for (let y = Math.floor(y0); y < Math.min(h, Math.ceil(y1)); y++) {
        const wy = Math.min(y + 1, y1) - Math.max(y, y0);
        for (let x = Math.floor(x0); x < Math.min(w, Math.ceil(x1)); x++) {
          const wt = wy * (Math.min(x + 1, x1) - Math.max(x, x0));
          const i = (y * w + x) * 4, al = data[i + 3] / 255;
          r += wt * (data[i] * al + 255 * (1 - al)); g += wt * (data[i + 1] * al + 255 * (1 - al)); b += wt * (data[i + 2] * al + 255 * (1 - al));
          a += wt;
        }
      }
      const o = (Y * W + X) * 3;
      out[o] = r / a; out[o + 1] = g / a; out[o + 2] = b / a;
    }
  }
  return out;
}

/** Median cut on a 5-bit-per-channel histogram -> up to n colours [[r,g,b], ...]. */
export function medianCut(rgb, n = COLOURS) {
  const hist = new Map();
  for (let i = 0; i < rgb.length; i += 3) {
    const k = ((rgb[i] >> 3) << 10) | ((rgb[i + 1] >> 3) << 5) | (rgb[i + 2] >> 3);
    hist.set(k, (hist.get(k) || 0) + 1);
  }
  const all = [...hist].map(([k, c]) => [(k >> 10) & 31, (k >> 5) & 31, k & 31, c]);
  if (all.length <= n) return all.map(([r, g, b]) => [r * 8 + 4, g * 8 + 4, b * 8 + 4]);
  const boxes = [all];
  const range = (box) => {
    let best = 0, axis = 0;
    for (let a = 0; a < 3; a++) { let lo = 99, hi = -1; for (const p of box) { if (p[a] < lo) lo = p[a]; if (p[a] > hi) hi = p[a]; } if (hi - lo > best) { best = hi - lo; axis = a; } }
    return [best, axis];
  };
  while (boxes.length < n) {
    // split the box with the most pixels x range (weights detail where the picture is)
    let bi = -1, score = 0, ax = 0;
    boxes.forEach((b, i) => { if (b.length < 2) return; const [r, a] = range(b); const cnt = b.reduce((s, p) => s + p[3], 0); const sc = r * Math.sqrt(cnt); if (sc > score) { score = sc; bi = i; ax = a; } });
    if (bi < 0) break;
    const box = boxes[bi].sort((p, q) => p[ax] - q[ax]);
    const total = box.reduce((s, p) => s + p[3], 0);
    let acc = 0, cut = 1;
    for (let i = 0; i < box.length - 1; i++) { acc += box[i][3]; if (acc >= total / 2) { cut = i + 1; break; } }
    boxes.splice(bi, 1, box.slice(0, cut), box.slice(cut));
  }
  return boxes.map((box) => {
    let r = 0, g = 0, b = 0, c = 0;
    for (const p of box) { r += p[0] * p[3]; g += p[1] * p[3]; b += p[2] * p[3]; c += p[3]; }
    return [Math.round((r / c) * 8 + 4), Math.round((g / c) * 8 + 4), Math.round((b / c) * 8 + 4)].map((v) => Math.min(255, v));
  });
}

/** Floyd-Steinberg (serpentine, 7/8 strength) onto the palette -> indexes. */
export function dither(rgb, W, H, pal) {
  const px = new Uint8Array(W * H);
  const buf = Float32Array.from(rgb);
  const cache = new Map();
  const nearest = (r, g, b) => {
    const k = ((r >> 2) << 12) | ((g >> 2) << 6) | (b >> 2);
    let v = cache.get(k);
    if (v !== undefined) return v;
    let best = 1e9;
    for (let i = 0; i < pal.length; i++) {
      const dr = pal[i][0] - r, dg = pal[i][1] - g, db = pal[i][2] - b;
      const d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
      if (d < best) { best = d; v = i; }
    }
    cache.set(k, v);
    return v;
  };
  const k = 7 / 8;
  for (let y = 0; y < H; y++) {
    const rev = y & 1;
    for (let j = 0; j < W; j++) {
      const x = rev ? W - 1 - j : j, i = (y * W + x) * 3;
      const r = Math.max(0, Math.min(255, buf[i])), g = Math.max(0, Math.min(255, buf[i + 1])), b = Math.max(0, Math.min(255, buf[i + 2]));
      const c = nearest(r | 0, g | 0, b | 0);
      px[y * W + x] = c;
      const er = (r - pal[c][0]) * k, eg = (g - pal[c][1]) * k, eb = (b - pal[c][2]) * k;
      const spread = (dx, dy, f) => {
        const xx = x + (rev ? -dx : dx), yy = y + dy;
        if (xx < 0 || xx >= W || yy >= H) return;
        const o = (yy * W + xx) * 3;
        buf[o] += er * f; buf[o + 1] += eg * f; buf[o + 2] += eb * f;
      };
      spread(1, 0, 7 / 16); spread(-1, 1, 3 / 16); spread(0, 1, 5 / 16); spread(1, 1, 1 / 16);
    }
  }
  return px;
}

/** The row order of an interlaced GIF (passes: every 8th from 0, every 8th from 4, every 4th from 2, every 2nd from 1). */
export function interlaceOrder(H) {
  const rows = [];
  for (const [start, step] of [[0, 8], [4, 8], [2, 4], [1, 2]]) for (let y = start; y < H; y += step) rows.push(y);
  return rows;
}

/** GIF89a encoder: 256-entry global table, one interlaced image, LZW. */
export function encodeGif(px, W, H, pal, { comment = '' } = {}) {
  const out = [];
  const u16 = (v) => out.push(v & 255, (v >> 8) & 255);
  for (const c of 'GIF89a') out.push(c.charCodeAt(0));
  u16(W); u16(H);
  out.push(0xF7, 0, 0);                         // global table, 8 bits colour resolution, 256 entries
  for (let i = 0; i < 256; i++) {
    const c = i < pal.length ? pal[i] : i >= 240 ? UI_PALETTE[i - 240] : [0, 0, 0];
    out.push(c[0], c[1], c[2]);
  }
  if (comment) {
    out.push(0x21, 0xFE);
    const b = Array.from(comment, (ch) => ch.charCodeAt(0) & 0x7F);
    for (let i = 0; i < b.length; i += 255) { const s = b.slice(i, i + 255); out.push(s.length, ...s); }
    out.push(0);
  }
  out.push(0x2C); u16(0); u16(0); u16(W); u16(H); out.push(0x40);   // interlaced, no local table
  out.push(8);                                  // LZW minimum code size
  const data = lzw(px, W, H);
  for (let i = 0; i < data.length; i += 255) { const n = Math.min(255, data.length - i); out.push(n); for (let k = 0; k < n; k++) out.push(data[i + k]); }
  out.push(0, 0x3B);
  return Uint8Array.from(out);
}

function lzw(px, W, H) {
  const order = interlaceOrder(H);
  const CLEAR = 256, EOI = 257;
  const out = [];
  let acc = 0, nbits = 0, size = 9;
  const put = (code) => { acc |= code << nbits; nbits += size; while (nbits >= 8) { out.push(acc & 255); acc >>>= 8; nbits -= 8; } };
  let dict = new Map(), next = 258;
  put(CLEAR);
  let prefix = -1;
  for (const y of order) {
    for (let x = 0; x < W; x++) {
      const c = px[y * W + x];
      if (prefix < 0) { prefix = c; continue; }
      const key = (prefix << 8) | c;
      const hit = dict.get(key);
      if (hit !== undefined) { prefix = hit; continue; }
      put(prefix);
      if (next < 4096) {
        dict.set(key, next++);
        if (next > (1 << size) && size < 12) size++;
      } else {
        put(CLEAR); dict = new Map(); next = 258; size = 9;
      }
      prefix = c;
    }
  }
  if (prefix >= 0) put(prefix);
  put(EOI);
  if (nbits > 0) out.push(acc & 255);
  return out;
}

/** Browser image decoding: bytes -> RGBA at most maxW x maxH (keeps the aspect ratio). */
export async function browserDecode(bytes, maxW, maxH) {
  const blob = new Blob([bytes]);
  const probe = await createImageBitmap(blob);
  const s = Math.min(1, maxW / probe.width, maxH / probe.height);
  const w = Math.max(1, Math.round(probe.width * s)), h = Math.max(1, Math.round(probe.height * s));
  let bmp = probe;
  if (s < 1) { try { bmp = await createImageBitmap(blob, { resizeWidth: w, resizeHeight: h, resizeQuality: 'high' }); probe.close?.(); } catch { bmp = probe; } }
  const cv = typeof OffscreenCanvas !== 'undefined' ? new OffscreenCanvas(w, h) : Object.assign(document.createElement('canvas'), { width: w, height: h });
  const ctx = cv.getContext('2d');
  ctx.fillStyle = '#fff'; ctx.fillRect(0, 0, w, h);
  ctx.drawImage(bmp, 0, 0, w, h);
  bmp.close?.();
  const d = ctx.getImageData(0, 0, w, h);
  return { width: w, height: h, data: d.data };
}

/** Encoded image bytes -> { gif, width, height, colours }. */
export async function makeGif(bytes, { decode = browserDecode, comment = '' } = {}) {
  // decode at twice the target size, then area-average down (sharper than one big step)
  const img = await decode(bytes, MAX_W * 2, Math.round(MAX_H * ASPECT * 2));
  if (!img || !img.width || !img.height) throw new Error('undecodable picture');
  const { width: W, height: H } = fitSize(img.width, img.height);
  const rgb = resample(img, W, H);
  const pal = medianCut(rgb, COLOURS);
  const px = dither(rgb, W, H, pal);
  return { gif: encodeGif(px, W, H, pal, { comment }), width: W, height: H, colours: pal.length, pixels: px, palette: pal };
}

/** A GIF decoder (tests: check what the encoder wrote). -> { width, height, pixels (row order), palette } */
export function decodeGif(g) {
  let p = 6;
  const W = g[p] | (g[p + 1] << 8), H = g[p + 2] | (g[p + 3] << 8);
  const flags = g[p + 4]; p += 7;
  const pal = [];
  if (flags & 0x80) { const n = 2 << (flags & 7); for (let i = 0; i < n; i++, p += 3) pal.push([g[p], g[p + 1], g[p + 2]]); }
  let interlaced = false, iw = W, ih = H, ix = 0, iy = 0;
  for (;;) {
    const b = g[p++];
    if (b === 0x21) { p++; while (g[p]) p += g[p] + 1; p++; continue; }
    if (b === 0x2C) { ix = g[p] | (g[p + 1] << 8); iy = g[p + 2] | (g[p + 3] << 8); iw = g[p + 4] | (g[p + 5] << 8); ih = g[p + 6] | (g[p + 7] << 8); interlaced = !!(g[p + 8] & 0x40); p += 9; break; }
    throw new Error('bad GIF block ' + b);
  }
  const min = g[p++];
  const data = [];
  while (g[p]) { for (let i = 1; i <= g[p]; i++) data.push(g[p + i]); p += g[p] + 1; }
  const out = [];
  let size = min + 1, acc = 0, nb = 0, dict = [], prev = null;
  const CLEAR = 1 << min, EOI = CLEAR + 1;
  const reset = () => { dict = []; for (let i = 0; i < CLEAR; i++) dict[i] = [i]; dict[CLEAR] = []; dict[EOI] = []; size = min + 1; prev = null; };
  reset();
  for (const byte of data) {
    acc |= byte << nb; nb += 8;
    while (nb >= size) {
      const code = acc & ((1 << size) - 1); acc >>>= size; nb -= size;
      if (code === CLEAR) { reset(); continue; }
      if (code === EOI) { nb = 0; break; }
      let entry;
      if (code < dict.length) entry = dict[code];
      else if (prev) entry = prev.concat(prev[0]);
      else throw new Error('bad LZW');
      out.push(...entry);
      if (prev && dict.length < 4096) dict.push(prev.concat(entry[0]));
      if (dict.length === (1 << size) && size < 12) size++;
      prev = entry;
    }
  }
  const pixels = new Uint8Array(iw * ih);
  const order = interlaced ? interlaceOrder(ih) : [...Array(ih).keys()];
  order.forEach((y, k) => { for (let x = 0; x < iw; x++) pixels[y * iw + x] = out[k * iw + x] ?? 0; });
  return { width: iw, height: ih, x: ix, y: iy, pixels, palette: pal, interlaced };
}
