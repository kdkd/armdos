// apps/keyb/tests/nlskit.mjs - the harness the tests of KEYB, DISPLAY.SYS and
// NLSFUNC share: a C: with the kernel, COMMAND.COM and the NLS programs, a
// CONFIG.SYS and an AUTOEXEC.BAT whose commands write their standard output to
// C:\R\Onn.TXT (read back from the disk image afterwards), and key presses for
// KTEST (tests/ktest.c), which prints what INT 16h returns.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const ROOT = path.resolve(HERE, '../../..');
export const B = (p) => path.join(ROOT, 'build', p);

export function checker() {
  const st = { failures: 0 };
  st.check = (ok, what, detail = '') => {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${!ok && detail ? '\n       ' + detail : ''}`);
    if (!ok) st.failures++;
    return ok;
  };
  st.eq = (got, want, what) => st.check(got === want, what, `got  ${JSON.stringify(got)}\n       want ${JSON.stringify(want)}`);
  return st;
}

const NLS_FILES = ['KEYB.COM', 'KEYBOARD.SYS', 'DISPLAY.SYS', 'EGA.CPI', 'NLSFUNC.EXE', 'COUNTRY.SYS', 'MODE.COM', 'ANSI.SYS'];

/** A C: image. extra: [{ dst, text } | { dst, src }]. */
export function makeImage(name, { config = '', autoexec = '', extra = [] } = {}) {
  const dir = B(`nls-test/${name}`);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM' },
    { src: 'build/keyb-test/KEYTEST.EXE', dst: 'DOS\\KTEST.EXE' },
  ];
  for (const f of NLS_FILES) files.push({ src: 'build/' + f, dst: 'DOS\\' });
  const put = (dst, data) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, data);
    files.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', config.replace(/\n/g, '\r\n'));
  put('AUTOEXEC.BAT', ('@ECHO OFF\nPATH C:\\DOS\n' + autoexec + 'ECHO ALL-DONE\n').replace(/\n/g, '\r\n'));
  for (const e of extra) {
    if (e.text !== undefined) put(e.dst, typeof e.text === 'string' ? e.text.replace(/\n/g, '\r\n') : e.text);
    else files.push({ src: e.src, dst: e.dst });
  }
  const { img } = buildImage({
    format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'NLSTEST',
    date: '1988-06-17 12:00:00', boot: { src: 'build/bootsect.bin' }, files, dirs: ['DOS', 'R'],
  }, ROOT);
  return img;
}

/** "cmd > \R\Onn.TXT" lines for a list of commands (index = file number). */
export function redirected(cmds) {
  return cmds.map((c, i) => `${c} > \\R\\O${String(i).padStart(2, '0')}.TXT\n`).join('');
}

export async function run(name, opts, drive) {
  const pc = await boot({ rom: B('rom.bin'), hd: makeImage(name, opts), ...(opts.boot || {}) });
  if (drive) await drive(pc);
  const done = pc.until(() => pc.hasText('ALL-DONE'), { timeoutMs: opts.timeoutMs || 120000 });
  pc.run(300);
  const fat = new FatReader(pc.machine.ata.img);
  pc.file = (p) => { try { return Buffer.from(fat.readFile(fat.lookup(p))).toString('latin1'); } catch { return null; } };
  pc.out = (i) => pc.file(`R\\O${String(i).padStart(2, '0')}.TXT`);
  pc.done = done;
  return pc;
}

/** Press a key combination ("ShiftLeft+KeyA") on the machine's keyboard. */
export function press(pc, combo) {
  const parts = combo.split('+');
  for (const p of parts) { pc.machine.keyDown(p); pc.run(15); }
  for (const p of parts.reverse()) { pc.machine.keyUp(p); pc.run(15); }
}

/** Wait for KTEST, press the keys (then Esc), return the words it printed. */
export function ktest(pc, keys, file) {
  if (!pc.until(() => pc.hasText('KTEST READY'), { timeoutMs: 60000 })) return null;
  for (const k of keys) press(pc, k);
  press(pc, 'Escape');
  pc.until(() => pc.hasText('KTEST DONE'), { timeoutMs: 20000 });
  // retire this run's markers (the next KTEST may already have printed its
  // READY below them): everything up to the DONE line
  const m8 = pc.cpu.m8;
  let done = -1;
  for (let a = 0xB8000; a < 0xB8000 + 4000; a += 2)
    if (m8[a] === 0x4B && m8[a + 2] === 0x54 && m8[a + 12] === 0x44 && m8[a + 14] === 0x4F) done = a;
  for (let a = 0xB8000; a <= done; a += 2) if (m8[a] === 0x4B && m8[a + 2] === 0x54 && m8[a + 4] === 0x45) m8[a] = 0x6B;
  return file;
}
