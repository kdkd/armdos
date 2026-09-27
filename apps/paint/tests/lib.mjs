// apps/paint/tests/lib.mjs - boot ARM-DOS headless with ARM Paint on C:.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const ROOT = path.resolve(HERE, '../../..');
export const OUT = path.join(ROOT, 'build', 'paint-test');
fs.mkdirSync(OUT, { recursive: true });

let seq = 0;
/** files: [[dosPath, Buffer|string], ...]; opts: { mouse (default true), autoexec, machine } */
export async function start(files = [], opts = {}) {
  const dir = path.join(OUT, `img${seq++}`);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const list = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM' },
    { src: 'build/HIMEM.SYS', dst: 'DOS\\' },
    { src: 'build/MOUSE.COM', dst: 'DOS\\' },
    { src: 'build/PAINT.EXE', dst: 'PAINT\\' },
    { src: 'apps/paint/data/PAINT.BAT', dst: 'DOS\\PAINT.BAT' },
    { src: 'apps/paint/samples/SPLASH.PCX', dst: 'PAINT\\SAMPLES\\' },
    { src: 'apps/paint/samples/SUNSET.PCX', dst: 'PAINT\\SAMPLES\\' },
    { src: 'apps/paint/samples/ARMAT.PCX', dst: 'PAINT\\SAMPLES\\' },
  ];
  if (fs.existsSync(path.join(ROOT, 'build/BANNER.EXE'))) list.push({ src: 'build/BANNER.EXE', dst: 'DOS\\' });
  let n = 0;
  const put = (dst, data) => {
    const host = path.join(dir, `f${n++}`);
    fs.writeFileSync(host, typeof data === 'string' ? Buffer.from(data, 'latin1') : data);
    list.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', 'DEVICE=C:\\DOS\\HIMEM.SYS\r\nFILES=20\r\n');
  put('AUTOEXEC.BAT', '@ECHO OFF\r\nPATH C:\\DOS\r\nPROMPT $P$G\r\n' + (opts.mouse === false ? '' : 'MOUSE\r\n') + (opts.autoexec || ''));
  for (const [dst, data] of files) put(dst, data);
  const { img } = buildImage({ format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'PAINTTEST',
    date: '1988-06-17 12:00:00', boot: { src: 'build/bootsect.bin' }, files: list, dirs: ['DOS', 'PAINT', 'PAINT\\SAMPLES'] }, ROOT);
  fs.rmSync(dir, { recursive: true, force: true });
  const printed = [];
  const pc = await boot({ ...(opts.machine || {}), onPrint: (b) => printed.push(b), rom: path.join(ROOT, 'build/rom.bin'), hd: img });
  pc.printed = printed;
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

/** mode 13h pixel */
export const px = (pc, x, y) => pc.cpu.m8[0xA0000 + y * 320 + x];

/** A mouse in mode 13h screen pixels (the driver's virtual x is 2 per pixel). */
export function mouse(pc) {
  let at = null;
  const m = {
    home() { pc.machine.mouseMove(-3000, -3000); pc.run(250); at = { x: 0, y: 0 }; return m; },
    // the driver: 8 mickeys per 8 virtual pixels horizontally (= 2 mickeys a screen pixel), 16 per 8 vertically
    to(x, y) {
      if (!at) m.home();
      let dx = (x - at.x) * 2, dy = (y - at.y) * 2;
      while (dx || dy) {
        const sx = Math.max(-40, Math.min(40, dx)), sy = Math.max(-40, Math.min(40, dy));
        pc.machine.mouseMove(sx, sy); pc.run(20); dx -= sx; dy -= sy;
      }
      pc.run(60); at = { x, y }; return m;
    },
    down(b = 1) { pc.machine.mouseButtons(b); pc.run(80); return m; },
    up() { pc.machine.mouseButtons(0); pc.run(150); return m; },
    click(x, y, b = 1) { if (x !== undefined) m.to(x, y); m.down(b); m.up(); return m; },
    drag(x0, y0, x1, y1, b = 1) { m.to(x0, y0); m.down(b); m.to(x1, y1); m.up(); return m; },
    get at() { return at; },
  };
  return m;
}

export async function shot(pc, name) {
  const f = path.join(OUT, name + '.png');
  await pc.png(f);
  return f;
}
