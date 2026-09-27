// apps/edit/tests/lib.mjs - boot ARM-DOS headless with EDIT.EXE on C:.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const ROOT = path.resolve(HERE, '../../..');
export const OUT = path.join(ROOT, 'build', 'edit-test');
fs.mkdirSync(OUT, { recursive: true });

let seq = 0;
/**
 * files: [[dosPath, contents(string|Buffer)], ...]
 * opts: { mouse: load MOUSE.COM, himem: true, lines: text for AUTOEXEC }
 */
export async function start(files = [], opts = {}) {
  const dir = path.join(OUT, `img${seq++}`);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const list = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM' },
    { src: 'build/EDIT.EXE', dst: 'DOS\\' },
    { src: 'build/HIMEM.SYS', dst: 'DOS\\' },
    { src: 'build/MOUSE.COM', dst: 'DOS\\' },
    { src: 'build/MEM.EXE', dst: 'DOS\\' },
  ];
  if (fs.existsSync(path.join(ROOT, 'build/edit/EDIT.HLP'))) list.push({ src: 'build/edit/EDIT.HLP', dst: 'DOS\\' });
  let n = 0;
  const put = (dst, data) => {
    const host = path.join(dir, `f${n++}`);
    fs.writeFileSync(host, typeof data === 'string' ? Buffer.from(data, 'latin1') : data);
    list.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', (opts.himem === false ? '' : 'DEVICE=C:\\DOS\\HIMEM.SYS\r\n') + 'FILES=20\r\n');
  put('AUTOEXEC.BAT', '@ECHO OFF\r\nPATH C:\\DOS\r\nPROMPT $P$G\r\n' + (opts.mouse ? 'MOUSE\r\n' : '') + (opts.autoexec || ''));
  for (const [dst, data] of files) put(dst, data);
  const dirs = ['DOS', ...(opts.dirs || [])];
  const { img } = buildImage({ format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'EDITTEST',
    date: '1988-06-17 12:00:00', boot: { src: 'build/bootsect.bin' }, files: list, dirs }, ROOT);
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

/** type keys and let the machine settle */
export function keys(pc, k, ms = 400) {
  pc.type(k);
  pc.waitIdle({ timeoutMs: 20000 });
  pc.run(ms);
}

/** screen text + attributes */
export function grab(pc) {
  const m = pc.cpu.m8;
  const cols = m[0x44A] | (m[0x44B] << 8), rows = m[0x484] + 1;
  const text = [], attrs = [];
  const lines = pc.lines();
  for (let r = 0; r < rows; r++) {
    const a = [];
    for (let c = 0; c < cols; c++) a.push(m[0xB8000 + (r * cols + c) * 2 + 1]);
    attrs.push(a);
    text.push(lines[r]);
  }
  return { text, attrs, rows, cols };
}

export async function shot(pc, name) {
  const f = path.join(OUT, name + '.png');
  await pc.png(f);
  fs.writeFileSync(path.join(OUT, name + '.txt'), pc.lines().join('\n') + '\n');
  return f;
}

/** A mouse that knows where it is (the driver centres the pointer on reset). */
export function mouse(pc, r = 12, c = 40) {
  let at = { r, c };
  const m = {
    home() { pc.machine.mouseMove(-2000, -2000); pc.run(200); at = { r: 0, c: 0 }; return m; },
    to(r, c) { pc.machine.mouseMove((c - at.c) * 8, (r - at.r) * 16); pc.run(150); at = { r, c }; return m; },
    down(b = 1) { pc.machine.mouseButtons(b); pc.run(80); return m; },
    up() { pc.machine.mouseButtons(0); pc.run(250); return m; },
    click(r, c, b = 1) { if (r !== undefined) m.to(r, c); m.down(b); m.up(); return m; },
    dbl(r, c) { if (r !== undefined) m.to(r, c); pc.machine.mouseButtons(1); pc.run(40); pc.machine.mouseButtons(0); pc.run(40);
      pc.machine.mouseButtons(1); pc.run(40); pc.machine.mouseButtons(0); pc.run(400); return m; },
    drag(r, c) { m.down(); m.to(r, c); m.up(); return m; },
    get at() { return at; },
  };
  return m;
}
