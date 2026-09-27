// apps/gem/tests/lib.mjs - boot ARM-DOS headless with GEM on C: (for tests).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const ROOT = path.resolve(HERE, '../../..');
export const OUT = path.join(ROOT, 'build', 'gem-test');
fs.mkdirSync(OUT, { recursive: true });

// the GEM files as apps/gem/hd.json lays them out
export function gemFiles() {
  const j = JSON.parse(fs.readFileSync(path.join(ROOT, 'apps/gem/hd.json'), 'utf8'));
  return { files: j.files.filter((f) => fs.existsSync(path.join(ROOT, f.src))), dirs: j.dirs };
}

let seq = 0;
/** files: [[dosPath, Buffer|string], ...]; opts: { mouse (default true), autoexec, machine, extra: [{src,dst}] } */
export async function start(files = [], opts = {}) {
  const dir = path.join(OUT, `img${seq++}`);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const g = gemFiles();
  const list = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM' },
    { src: 'build/HIMEM.SYS', dst: 'DOS\\' },
    { src: 'build/MOUSE.COM', dst: 'DOS\\' },
    ...g.files,
    ...(opts.extra || []),
  ];
  for (const p of ['build/MEM.EXE', 'build/TREE.COM', 'build/EDIT.EXE', 'build/MORE.COM']) if (fs.existsSync(path.join(ROOT, p))) list.push({ src: p, dst: 'DOS\\' });
  let n = 0;
  const put = (dst, data) => {
    const host = path.join(dir, `f${n++}`);
    fs.writeFileSync(host, typeof data === 'string' ? Buffer.from(data, 'latin1') : data);
    list.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', 'DEVICE=C:\\DOS\\HIMEM.SYS\r\nFILES=20\r\n');
  put('AUTOEXEC.BAT', '@ECHO OFF\r\nPATH C:\\DOS\r\nPROMPT $P$G\r\n' + (opts.mouse === false ? '' : 'MOUSE\r\n') + (opts.autoexec || ''));
  for (const [dst, data] of files) put(dst, data);
  const { img } = buildImage({ format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'GEMTEST',
    date: '1988-06-17 12:00:00', boot: { src: 'build/bootsect.bin' }, files: list, dirs: ['DOS', ...(g.dirs || [])] }, ROOT);
  fs.rmSync(dir, { recursive: true, force: true });
  const pc = await boot({ ...(opts.machine || {}), rom: path.join(ROOT, 'build/rom.bin'), hd: img });
  pc.waitText('C:\\>', { timeoutMs: 30000 });
  pc.waitIdle();
  return pc;
}

export function readFile(pc, p) {
  const r = new FatReader(pc.machine.ata.img);
  try { return Buffer.from(r.readFile(r.lookup(p))); } catch { return null; }
}

export function keys(pc, k, ms = 300) {
  pc.type(k);
  pc.waitIdle({ timeoutMs: 20000 });
  pc.run(ms);
}

export async function shot(pc, name) {
  const f = path.join(OUT, name + '.png');
  await pc.png(f);
  return f;
}

/** the mode 6 / Hercules / VGA pixel under (x, y): 1 = ink (black), 0 = paper */
export function ink(pc, x, y) {
  const m = pc.cpu.m8;
  const mode = m[0x449];
  if (mode === 6) {
    const a = 0xB8000 + 0x2000 * (y & 1) + 80 * (y >> 1) + (x >> 3);
    return ((m[a] >> (7 - (x & 7))) & 1) ? 0 : 1;
  }
  return null;
}

/** GEM mouse in screen pixels: the INT 33h driver counts 8 mickeys per 8 pixels
 *  horizontally and 16 per 8 vertically in its virtual 640-wide space */
export function mouse(pc, geo = { mx: 1, my: 2 }) {
  let at = null;
  const m = {
    home() { pc.machine.mouseMove(-3000, -3000); pc.run(250); at = { x: 0, y: 0 }; return m; },
    to(x, y) {
      if (!at) m.home();
      let dx = (x - at.x) * geo.mx, dy = (y - at.y) * geo.my;
      while (dx || dy) {
        const sx = Math.max(-40, Math.min(40, dx)), sy = Math.max(-40, Math.min(40, dy));
        pc.machine.mouseMove(sx, sy); pc.run(20); dx -= sx; dy -= sy;
      }
      pc.run(80); at = { x, y }; return m;
    },
    down(b = 1) { pc.machine.mouseButtons(b); pc.run(100); return m; },
    up() { pc.machine.mouseButtons(0); pc.run(200); return m; },
    click(x, y, b = 1) { if (x !== undefined) m.to(x, y); m.down(b); m.up(); return m; },
    dclick(x, y) { if (x !== undefined) m.to(x, y); pc.machine.mouseButtons(1); pc.run(40); pc.machine.mouseButtons(0); pc.run(60); pc.machine.mouseButtons(1); pc.run(40); pc.machine.mouseButtons(0); pc.run(300); return m; },
    drag(x0, y0, x1, y1, b = 1) { m.to(x0, y0); m.down(b); m.to(x1, y1); m.up(); return m; },
    get at() { return at; },
  };
  return m;
}

// ------------------------------------------------------------ screen text
// GEM draws text in its own system fonts; find a string on the graphics
// screen by matching the glyph bitmaps of the GEM/3 fonts (orig/vdi/fonts).
const FONTDIR = path.join(ROOT, 'apps/gem/orig/vdi/fonts');
const fontCache = {};
export function gemFont(name) {
  if (fontCache[name]) return fontCache[name];
  const L = fs.readFileSync(path.join(FONTDIR, name + '.ful'), 'latin1').split('\n').map((s) => s.trim());
  let i = 0;
  i++;
  const count = +L[i++], height = +L[i++], formPixels = +L[i++];
  const cw = formPixels / count;
  const first = count === 256 ? 0 : 256 - count;
  const glyphs = {};
  for (let c = 0; c < count; c++) {
    i++;
    const rows = [];
    for (let y = 0; y < height; y++) rows.push((L[i++] || '').slice(0, cw).padEnd(cw, '0'));
    glyphs[first + c] = rows;
  }
  return (fontCache[name] = { cw, height, glyphs });
}

/** the screen as rows of 0/1 (1 = ink) for the GEM screen modes */
export function screenInk(pc) {
  const m = pc.cpu.m8, mode = m[0x449];
  let w, h, get;
  if (pc.machine.video === 'hercules' || mode === 7) {
    w = 720; h = 348;
    get = (x, y) => (((m[0xB0000 + 0x2000 * (y & 3) + 90 * (y >> 2) + (x >> 3)] >> (7 - (x & 7))) & 1) ^ 1);
  } else if (mode === 0x62) {
    w = 640; h = 480;
    get = (x, y) => (m[0xA0000 + y * 640 + x] === 0 ? 0 : 1);
  } else {
    w = 640; h = 200;
    get = (x, y) => (((m[0xB8000 + 0x2000 * (y & 1) + 80 * (y >> 1) + (x >> 3)] >> (7 - (x & 7))) & 1) ^ 1);
  }
  const rows = [];
  for (let y = 0; y < h; y++) {
    const r = new Uint8Array(w);
    for (let x = 0; x < w; x++) r[x] = get(x, y);
    rows.push(r);
  }
  return { w, h, rows };
}

/** find str drawn in font (8x8, 6x6, 8x14, 8x16); returns [{x, y, inverse}] */
export function findText(pc, str, font = '8x8', region = null) {
  const f = gemFont(font);
  const scr = screenInk(pc);
  const bm = [];
  for (let y = 0; y < f.height; y++) {
    let row = '';
    for (const ch of str) row += (f.glyphs[ch.charCodeAt(0)] || f.glyphs[32])[y];
    bm.push(row);
  }
  const W = bm[0].length, H = f.height, hits = [];
  const [x0, y0, x1, y1] = region || [0, 0, scr.w - 1, scr.h - 1];
  for (let inv = 0; inv < 2; inv++)
    for (let y = y0; y + H - 1 <= y1 && y + H <= scr.h; y++)
      for (let x = x0; x + W - 1 <= x1 && x + W <= scr.w; x++) {
        let ok = true;
        for (let yy = 0; yy < H && ok; yy++) {
          const r = scr.rows[y + yy], b = bm[yy];
          for (let xx = 0; xx < W; xx++) if (((b.charCodeAt(xx) - 48) ^ inv) !== r[x + xx]) { ok = false; break; }
        }
        if (ok) hits.push({ x, y, inverse: !!inv });
      }
  return hits;
}
