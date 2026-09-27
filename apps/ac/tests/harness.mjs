// apps/ac/tests/harness.mjs - boot a private C: with COMMAND.COM, the ARM
// Commander (AC.EXE + ACMAIN.EXE in C:\DOS) and some scratch files.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const B = (p) => path.join(ROOT, 'build', p);
export const OUT = B('ac-test');

let failures = 0, passes = 0;
export const check = (ok, what, detail) => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${!ok && detail !== undefined ? '\n     ' + String(detail).split('\n').join('\n     ') : ''}`);
  if (ok) passes++; else failures++;
  return ok;
};
export const summary = () => { console.log(`\n${passes} passed, ${failures} failed`); return failures; };

/** opts: { name, texts: {dst: content}, dirs: [], files: [], mouse, autoexecExtra, mhz } */
export async function startPC(opts = {}) {
  fs.mkdirSync(OUT, { recursive: true });
  const dir = path.join(OUT, (opts.name || 'ac') + '-files');
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM', dst: 'COMMAND.COM' },
    { src: 'build/AC.EXE', dst: 'DOS\\AC.EXE' },
    { src: 'build/ACMAIN.EXE', dst: 'DOS\\ACMAIN.EXE' },
    { src: 'build/MEM.EXE', dst: 'DOS\\MEM.EXE' },
    { src: 'build/MOUSE.COM', dst: 'DOS\\MOUSE.COM' },
    { src: 'build/HIMEM.SYS', dst: 'DOS\\HIMEM.SYS' },
    { src: 'apps/ac/data/AC.MNU', dst: 'DOS\\AC.MNU' },
    ...(opts.files || []),
  ];
  let k = 0;
  const put = (dst, text) => {
    const host = path.join(dir, `f${k++}`);
    fs.writeFileSync(host, typeof text === 'string' ? text.replace(/\r?\n/g, '\r\n') : text);
    files.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', 'FILES=20\nBUFFERS=20\nDEVICE=C:\\DOS\\HIMEM.SYS\nSHELL=C:\\COMMAND.COM C:\\ /P\n');
  put('AUTOEXEC.BAT', '@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\n' + (opts.mouse ? 'MOUSE\n' : '') + (opts.autoexecExtra || ''));
  for (const [dst, text] of Object.entries(opts.texts || {})) put(dst, text);
  const m = {
    format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'ARM-DOS',
    date: '1989-06-01 12:00:00', boot: { src: 'build/bootsect.bin' }, files,
    dirs: ['DOS', ...(opts.dirs || [])],
  };
  const { img } = buildImage(m, ROOT);
  const hd = path.join(OUT, (opts.name || 'ac') + '.img');
  fs.writeFileSync(hd, img);
  const pc = await boot({ rom: B('rom.bin'), hd, mhz: opts.mhz, jit: true });
  fs.rmSync(hd, { force: true });
  fs.rmSync(dir, { recursive: true, force: true });
  pc.bootOk = pc.waitText('C:\\>', { timeoutMs: 30000 });
  pc.waitIdle();
  pc.shot = async (file) => { const p = path.join(OUT, file); await pc.png(p); return p; };
  pc.keys = (s, idle = true) => { pc.type(s); if (idle) pc.waitIdle({ timeoutMs: 60000 }); };
  pc.row = (r) => (pc.lines()[r] || '').trimEnd();
  pc.attr = (x, y) => pc.m.cpu.m8[0xB8000 + (y * 80 + x) * 2 + 1];
  pc.readFile = (p) => readHdFile(pc, p);
  return pc;
}

export function readHdFile(pc, p) {
  try {
    const img = pc.m.ata.img;
    const r = new FatReader(Buffer.from(img.buffer, img.byteOffset, img.length));
    return Buffer.from(r.readFile(r.lookup(p))).toString('latin1');
  } catch { return null; }
}

export function hdEntries(pc, p) {
  try {
    const img = pc.m.ata.img;
    const r = new FatReader(Buffer.from(img.buffer, img.byteOffset, img.length));
    const e = r.lookup(p);
    return r.readDir(p ? e : null).filter((x) => !(x.attr & 8)).map((x) => x.name);
  } catch (e) { return null; }
}
