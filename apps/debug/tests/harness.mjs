// apps/debug/tests/harness.mjs - boot a private ARM-DOS hard disk headless for
// DEBUG's tests.  The shell is the kernel's test shell (build/ktest/TSHELL.EXE,
// prompt "T>") unless opts.shell === 'command' and build/COMMAND.COM exists.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const B = (p) => path.join(ROOT, 'build', p);
export const OUT = B('debug-test');

let failures = 0;
export const check = (ok, what, detail) => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`);
  if (!ok) { failures++; if (detail) console.log(String(detail).split('\n').map((l) => '       | ' + l).join('\n')); }
  return ok;
};
export const failed = () => failures;

export async function startPC(opts) {
  const dir = path.join(OUT, opts.name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/DEBUG.COM', dst: 'DOS\\DEBUG.COM' },
    ...(opts.files || []),
  ];
  const put = (dst, data) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, typeof data === 'string' ? data.replace(/\r?\n/g, '\r\n') : data);
    files.push({ src: path.relative(ROOT, host), dst });
  };
  const useCommand = opts.shell === 'command' && fs.existsSync(B('COMMAND.COM'));
  let config = 'FILES=20\nBUFFERS=20\n';
  if (useCommand) {
    files.push({ src: 'build/COMMAND.COM', dst: 'COMMAND.COM' });
    config += 'SHELL=C:\\COMMAND.COM C:\\ /P\n';
    put('AUTOEXEC.BAT', '@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\n');
  } else {
    files.push({ src: 'build/ktest/TSHELL.EXE', dst: 'T\\TSHELL.EXE' });
    config += 'SHELL=C:\\T\\TSHELL.EXE\n';
  }
  put('CONFIG.SYS', config);
  for (const [dst, data] of Object.entries(opts.texts || {})) put(dst, data);
  const m = {
    format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'ARM-DOS',
    date: '1989-06-01 12:00:00', boot: { src: 'build/bootsect.bin' }, files,
    dirs: ['DOS', 'T', ...(opts.dirs || [])],
  };
  const { img } = buildImage(m, ROOT);
  const pc = await boot({ rom: B('rom.bin'), hd: new Uint8Array(img), jit: opts.jit !== false });

  pc.prompt = useCommand ? 'C:\\>' : 'T>';
  pc.outDir = dir;
  pc.bootOk = pc.until(() => lastLine(pc).startsWith(pc.prompt), { timeoutMs: 30000 });
  return pc;
}

export function lastLine(pc) {
  const l = pc.lines();
  const row = pc.m.cpu.m8[0x451];
  return (l[row] || '').trimEnd();
}

/** the text screen, lines trimmed, from the top down to the cursor row */
export function screenToCursor(pc) {
  const l = pc.lines().map((s) => s.trimEnd());
  return l.slice(0, pc.m.cpu.m8[0x451] + 1).join('\n');
}

/** type a line (or keys) and wait until the machine idles */
export function typeWait(pc, keys, { timeoutMs = 20000 } = {}) {
  pc.type(keys);
  pc.until(() => pc.m.typingDone(), { timeoutMs: 10000 });
  return pc.waitIdle({ timeoutMs });
}

export function readHdFile(pc, p) {
  try {
    const img = pc.m.ata.img;
    const r = new FatReader(Buffer.from(img.buffer, img.byteOffset, img.length));
    return Buffer.from(r.readFile(r.lookup(p)));
  } catch { return null; }
}
