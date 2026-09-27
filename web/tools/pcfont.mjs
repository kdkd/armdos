// The machine's own text-mode font as a web font: makePcFont(bitmap) turns a raw 8xN
// CP437 bitmap font (emu/fonts/*.bin, one byte per pixel row) into a TrueType file
// whose outlines are the glyphs' pixels, traced into polygons (so no seams between
// squares at any size). Glyph n is CP437 code n; the cmap maps each one's Unicode
// character (emu/render.mjs's CP437 table), so box drawing, blocks and arrows work in
// ordinary HTML text. One font pixel is 1/rows em: at font-size = rows px it is one
// CSS pixel, the size to use it at (or a whole multiple of it).
import { CP437 } from '../../emu/render.mjs';

const U = 64;                                    // font units per pixel

// the outline of one glyph: the boundary edges of its lit pixels, clockwise (TrueType's
// winding for filled area, y up), chained into closed contours with collinear points dropped
function trace(rows, asc) {
  const h = rows.length, on = (x, y) => x >= 0 && x < 8 && y >= 0 && y < h && (rows[y] >> (7 - x)) & 1;
  const edges = new Map();                       // "x,y" start corner -> [end corners] (pixel-grid corners, y down)
  const add = (x0, y0, x1, y1) => { const k = `${x0},${y0}`; (edges.get(k) || edges.set(k, []).get(k)).push([x1, y1]); };
  for (let y = 0; y < h; y++) for (let x = 0; x < 8; x++) {
    if (!on(x, y)) continue;
    if (!on(x, y - 1)) add(x, y, x + 1, y);              // top, left to right
    if (!on(x + 1, y)) add(x + 1, y, x + 1, y + 1);      // right, downwards
    if (!on(x, y + 1)) add(x + 1, y + 1, x, y + 1);      // bottom, right to left
    if (!on(x - 1, y)) add(x, y + 1, x, y);              // left, upwards
  }
  const contours = [];
  for (;;) {
    const first = edges.keys().next().value;
    if (first === undefined) break;
    const pts = [];
    let [cx, cy] = first.split(',').map(Number), dx = 0, dy = 0;
    for (;;) {
      const k = `${cx},${cy}`, outs = edges.get(k);
      if (!outs) break;
      // two ways on at a corner where pixels touch diagonally: turn right, keeping them apart
      const i = Math.max(0, outs.findIndex(([nx, ny]) => nx - cx === -dy && ny - cy === dx));
      const [nx, ny] = outs.splice(i, 1)[0];
      if (!outs.length) edges.delete(k);
      const ndx = nx - cx, ndy = ny - cy;
      if (ndx !== dx || ndy !== dy) pts.push([cx, cy]);  // a corner
      dx = ndx; dy = ndy; cx = nx; cy = ny;
    }
    contours.push(pts.map(([x, y]) => [x * U, (asc - y) * U]));
  }
  return contours;
}

// TrueType simple glyph: header, end points, no instructions, flags (all on-curve), x and y deltas as words
function glyf(contours) {
  if (!contours.length) return Buffer.alloc(0);
  const all = contours.flat(), xs = all.map((p) => p[0]), ys = all.map((p) => p[1]);
  const n = all.length, b = Buffer.alloc(10 + 2 * contours.length + 2 + n + 4 * n);
  let o = 0;
  o = b.writeInt16BE(contours.length, o);
  for (const v of [Math.min(...xs), Math.min(...ys), Math.max(...xs), Math.max(...ys)]) o = b.writeInt16BE(v, o);
  let end = -1; for (const c of contours) o = b.writeUInt16BE(end += c.length, o);
  o = b.writeUInt16BE(0, o);
  for (let k = 0; k < n; k++) b[o++] = 1;
  let px = 0; for (const [x] of all) { o = b.writeInt16BE(x - px, o); px = x; }
  let py = 0; for (const [, y] of all) { o = b.writeInt16BE(y - py, o); py = y; }
  return b;
}

const utf16 = (s) => { const b = Buffer.alloc(s.length * 2); for (let k = 0; k < s.length; k++) b.writeUInt16BE(s.charCodeAt(k), k * 2); return b; };
const sum = (b) => { let s = 0; const p = Buffer.concat([b, Buffer.alloc((4 - b.length % 4) % 4)]); for (let k = 0; k < p.length; k += 4) s = (s + p.readUInt32BE(k)) >>> 0; return s; };
function struct(fields) {                        // [[bytes, value], ...] big-endian, 1/2/4 bytes, negative values allowed
  const b = Buffer.alloc(fields.reduce((a, [n]) => a + n, 0)); let o = 0;
  for (const [n, v] of fields) { if (n === 1) b[o] = v & 255; else if (n === 2) b.writeUInt16BE(v & 0xFFFF, o); else b.writeUInt32BE(v >>> 0, o); o += n; }
  return b;
}

export function makePcFont(bitmap, { family = 'PC VGA', rows = 16, ascent = 12, version = '1.0' } = {}) {
  const em = rows * U, adv = 8 * U, desc = rows - ascent;
  const glyphs = [];
  for (let c = 0; c < 256; c++) glyphs.push(glyf(trace([...bitmap.subarray(c * rows, (c + 1) * rows)], ascent)));
  const loca = Buffer.alloc(257 * 4); let off = 0;
  const glyfT = Buffer.concat(glyphs.map((g, k) => { loca.writeUInt32BE(off, k * 4); const p = Buffer.concat([g, Buffer.alloc(g.length % 4 ? 4 - g.length % 4 : 0)]); off += p.length; return p; }));
  loca.writeUInt32BE(off, 256 * 4);
  let maxPts = 0, maxCt = 0;
  for (const g of glyphs) if (g.length) { const nc = g.readInt16BE(0); maxCt = Math.max(maxCt, nc); maxPts = Math.max(maxPts, g.readUInt16BE(10 + 2 * (nc - 1)) + 1); }

  // cmap format 4, one segment per character, glyph = CP437 code (0x00 stays .notdef)
  const map = new Map();
  for (let c = 1; c < 256; c++) map.set(CP437[c].charCodeAt(0), c);
  // a few characters the page uses that CP437 lacks, drawn with its nearest glyph
  for (const [u, c] of [[0xA0, 0x20], [0x25B8, 0x10], [0x25C2, 0x11], [0x25CF, 0x07], [0x2022, 0x07], [0xD7, 0x78], [0x2013, 0x2D], [0x2014, 0x2D], [0x2026, 0xFA]]) if (!map.has(u)) map.set(u, c);
  const codes = [...map.keys()].sort((a, b) => a - b).concat(0xFFFF), seg = codes.length;
  const lg = Math.floor(Math.log2(seg));
  const f4 = struct([[2, 4], [2, 16 + 8 * seg], [2, 0], [2, seg * 2], [2, 2 << lg], [2, lg], [2, seg * 2 - (2 << lg)],
    ...codes.map((c) => [2, c]), [2, 0], ...codes.map((c) => [2, c]),
    ...codes.map((c) => [2, c === 0xFFFF ? 1 : map.get(c) - c]), ...codes.map(() => [2, 0])]);
  const cmap = Buffer.concat([struct([[2, 0], [2, 1], [2, 3], [2, 1], [4, 12]]), f4]);

  const names = [[1, family], [2, 'Regular'], [3, `${family} ${version}`], [4, family], [5, `Version ${version}`], [6, family.replace(/\s+/g, '')]];
  const strs = names.map(([, s]) => utf16(s)); let so = 0;
  const name = Buffer.concat([struct([[2, 0], [2, names.length], [2, 6 + 12 * names.length]]),
    ...names.map(([id], k) => { const r = struct([[2, 3], [2, 1], [2, 0x409], [2, id], [2, strs[k].length], [2, so]]); so += strs[k].length; return r; }), ...strs]);

  const tables = {
    'OS/2': struct([[2, 4], [2, adv], [2, 400], [2, 5], [2, 0], [2, adv], [2, em / 2], [2, 0], [2, em / 4], [2, adv], [2, em / 2], [2, 0], [2, em / 2],
      [2, U], [2, 4 * U], [2, 0], ...Array(10).fill([1, 0]), [4, 3], [4, 0], [4, 0], [4, 0], [4, 0x20202020], [2, 0xC0],
      [2, 0x20], [2, 0xFFFF], [2, ascent * U], [2, -desc * U], [2, 0], [2, ascent * U], [2, desc * U], [4, 1], [4, 0x80000000],
      [2, 7 * U], [2, 10 * U], [2, 0], [2, 0x20], [2, 1]]),
    cmap,
    glyf: glyfT,
    head: struct([[4, 0x00010000], [4, 0x00010000], [4, 0], [4, 0x5F0F3CF5], [2, 0x000B], [2, em], [4, 0], [4, 0], [4, 0], [4, 0],
      [2, 0], [2, -desc * U], [2, adv], [2, ascent * U], [2, 0], [2, rows], [2, 2], [2, 1], [2, 0]]),
    hhea: struct([[4, 0x00010000], [2, ascent * U], [2, -desc * U], [2, 0], [2, adv], [2, 0], [2, 0], [2, adv], [2, 1], [2, 0], [2, 0],
      [2, 0], [2, 0], [2, 0], [2, 0], [2, 0], [2, 256]]),
    hmtx: Buffer.concat(glyphs.map((g) => struct([[2, adv], [2, g.length ? g.readInt16BE(2) : 0]]))),
    loca,
    maxp: struct([[4, 0x00010000], [2, 256], [2, maxPts], [2, maxCt], [2, 0], [2, 0], [2, 2], [2, 0], [2, 0], [2, 0], [2, 0], [2, 0], [2, 0], [2, 0], [2, 0]]),
    name,
    post: struct([[4, 0x00030000], [4, 0], [2, -2 * U], [2, U], [4, 1], [4, 0], [4, 0], [4, 0], [4, 0]]),
  };
  const tags = Object.keys(tables).sort(), n = tags.length, lg2 = Math.floor(Math.log2(n));
  const header = struct([[4, 0x00010000], [2, n], [2, 16 << lg2], [2, lg2], [2, n * 16 - (16 << lg2)]]);
  const dir = Buffer.alloc(16 * n), body = []; let pos = 12 + 16 * n;
  tags.forEach((t, k) => {
    const d = tables[t];
    dir.write(t, k * 16, 'latin1'); dir.writeUInt32BE(sum(d), k * 16 + 4); dir.writeUInt32BE(pos, k * 16 + 8); dir.writeUInt32BE(d.length, k * 16 + 12);
    const p = Buffer.concat([d, Buffer.alloc((4 - d.length % 4) % 4)]); body.push(p); pos += p.length;
  });
  const font = Buffer.concat([header, dir, ...body]);
  const headAt = dir.readUInt32BE(tags.indexOf('head') * 16 + 8);
  font.writeUInt32BE((0xB1B0AFBA - sum(font)) >>> 0, headAt + 8);
  return font;
}
