// apps/term/tests/lib.mjs - shared by the TERM and BBS tests: private disks,
// booting machines on one phone exchange (emu/phone.mjs, the COM2 modem of
// docs/MODEM.md), lock-step running of several machines, the byte stream a
// machine's UART receives.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';
import { PhoneExchange } from '../../../emu/phone.mjs';
export { PhoneExchange };

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const B = (p) => path.join(ROOT, 'build', p);
export const OUT = B('term-test');
fs.mkdirSync(OUT, { recursive: true });

let failures = 0;
export const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; return ok; };
export const failed = () => failures;

/** texts: {dst: content}, files: [{src, dst}] (src relative to ROOT or absolute) */
export function makeDisk(name, { files = [], texts = {}, dirs = [], autoexec = '' } = {}) {
  const dir = path.join(OUT, name + '.tmp');
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const all = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM', dst: 'COMMAND.COM' },
    ...files.map((f) => ({ ...f, src: path.isAbsolute(f.src) ? path.relative(ROOT, f.src) : f.src })),
  ];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, typeof text === 'string' ? text.replace(/\r?\n/g, '\r\n') : text);
    all.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', 'FILES=20\nBUFFERS=20\nSHELL=C:\\COMMAND.COM C:\\ /P\n');
  put('AUTOEXEC.BAT', '@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\n' + autoexec);
  for (const [dst, t] of Object.entries(texts)) put(dst, t);
  const { img } = buildImage({ format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: name.toUpperCase().slice(0, 11),
    date: '1989-11-04 21:00:00', boot: { src: 'build/bootsect.bin' }, files: all, dirs: ['DOS', ...dirs] }, ROOT);
  fs.rmSync(dir, { recursive: true, force: true });
  return img;
}

export async function startPC(img, opts = {}) {
  const pc = await boot({ rom: B('rom.bin'), hd: img, ...opts });
  pc.shot = async (file) => { const p = path.join(OUT, file); await pc.png(p); console.log(`     screenshot ${p}`); return p; };
  return pc;
}

/** Record everything the machine's COM2 receiver gets (modem results + line data). */
export function tapCom2(pc) {
  const u = pc.m.com2, rec = { text: '' };
  const receive = u.receive.bind(u);
  u.receive = (b, ...rest) => { rec.text += String.fromCharCode(b); return receive(b, ...rest); };
  return rec;
}

/** Run several machines in lock-step for ms of emulated time (ticks: extra
 *  endpoints like the Host Link, called with the first machine's time). */
export async function runAll(pcs, ticks, ms, step = 4, pred = null) {
  const end = pcs[0].m.timeMs() + ms;
  let n = 0;
  while (pcs[0].m.timeMs() < end) {
    for (const pc of pcs) pc.m.runFor(step);
    for (const t of ticks || []) t(pcs[0].m.timeMs());
    if (pred && pred()) return true;
    if (++n % 50 === 0) await new Promise((r) => setImmediate(r));
  }
  return pred ? !!pred() : true;
}

export function readFile(pc, p) {
  try {
    const img = pc.m.ata.img;
    const r = new FatReader(Buffer.from(img.buffer, img.byteOffset, img.length));
    return Buffer.from(r.readFile(r.lookup(p)));
  } catch { return null; }
}
export const screen = (pc) => pc.lines().join('\n');
export const has = (pc, t) => pc.lines().join('\n').includes(t);

/** press a key combo on a machine (KeyboardEvent codes) */
export function keys(pc, codes) {
  for (const c of codes) pc.m.keyDown(c);
  for (const c of [...codes].reverse()) pc.m.keyUp(c);
}
