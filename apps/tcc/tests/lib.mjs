// apps/tcc/tests/lib.mjs - shared helpers for the TCC tests: build a C: image
// with the real COMMAND.COM (+ HIMEM.SYS) and whatever files a test needs,
// boot it headless, type commands, read files back.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const B = (p) => path.join(ROOT, 'build', p);

/**
 * files: [{ dst: 'T\\X.C', data: string|Buffer } | { dst, src: 'build/..' }]
 * trees: [{ src: 'build/tcc/disk/TC', dst: 'TC' }]
 * autoexec: text of AUTOEXEC.BAT (CRLF added)
 */
export function makeImage(work, { files = [], trees = [], dirs = [], autoexec = '@ECHO OFF\nPROMPT $P$G\nPATH C:\\DOS;C:\\TC\n', config } = {}) {
  fs.mkdirSync(work, { recursive: true });
  const list = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM' },
    { src: 'build/HIMEM.SYS', dst: 'DOS\\' },
  ];
  let n = 0;
  const put = (dst, data) => {
    const host = path.join(work, `f${n++}_` + dst.replace(/[\\/:]/g, '_'));
    fs.writeFileSync(host, data);
    list.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', (config ?? 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=20\nBUFFERS=20\n').replace(/\r?\n/g, '\r\n'));
  put('AUTOEXEC.BAT', autoexec.replace(/\r?\n/g, '\r\n'));
  for (const f of files) {
    if (f.src) list.push({ src: f.src, dst: f.dst });
    else put(f.dst, typeof f.data === 'string' ? f.data : f.data);
  }
  const m = {
    format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'TCCTEST',
    date: '1988-06-17 12:00:00', boot: { src: 'build/bootsect.bin' }, files: list,
    tree: trees, dirs: ['DOS', ...dirs],
  };
  return buildImage(m, ROOT).img;
}

export async function start(img, opts = {}) {
  const pc = await boot({ rom: B('rom.bin'), hd: img, ...opts });
  if (!pc.waitText('C:\\>', { timeoutMs: 30000 })) throw new Error('no C:\\> prompt:\n' + pc.screen());
  pc.waitIdle({ timeoutMs: 5000 });
  return pc;
}

/** the text of the line the cursor is on (BIOS data area 0x450: column, row) */
export function cursorLine(pc) {
  const row = pc.cpu.m32 ? (pc.machine.cpu.m32[0x450 >> 2] >>> 8) & 0xFF : 0;
  return (pc.lines()[row] ?? '').trimEnd();
}

/**
 * Type a command line and wait until COMMAND.COM prompts again (cursor on a
 * "C:\...>" line and the CPU idle). Returns the emulated milliseconds from
 * the Enter key to the new prompt.
 */
export function cmd(pc, line, timeoutMs = 120000) {
  pc.type(line + '\r');
  pc.until(() => pc.machine.typingDone() && pc.machine.biosKeyBufferEmpty(), { timeoutMs: 20000, stepMs: 5 });
  const t0 = pc.timeMs;
  let t1 = 0;
  pc.run(5);
  pc.until(() => {
    if (!/^[A-Z]:\\[^>]*>$/.test(cursorLine(pc))) return false;
    t1 = pc.timeMs;
    return pc.waitIdle({ timeoutMs: 2000, quietMs: 150 });
  }, { timeoutMs, stepMs: 5 });
  return (t1 || pc.timeMs) - t0;
}

export function readFile(pc, p) {
  const r = new FatReader(pc.machine.ata.img);
  try { return Buffer.from(r.readFile(r.lookup(p))); } catch { return null; }
}
