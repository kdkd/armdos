// apps/arminfo/tests/harness.mjs - shared by the showcase programs' tests
// (ARMINFO, POPUP, CRASH, DEMO).
//
// Builds a private hard disk (IO.SYS, ARMDOS.SYS, HIMEM.SYS, a shell, the
// programs under test), boots it headless and hands back the testkit PC.
// The shell is build/COMMAND.COM when it exists, else the kernel's test shell
// (build/ktest/TSHELL.EXE) in interactive mode (prompt "T>"); pc.prompt says
// which, pc.cmd(line) types a command line and waits for the next prompt.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const B = (p) => path.join(ROOT, 'build', p);

let failures = 0;
export const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; return ok; };
export const failed = () => failures;

/**
 * files: [{src (relative to ROOT), dst}], dirs: [...]
 * opts: { name, outDir, mhz, shell: 'auto'|'tshell'|'command', himem: true, extraConfig }
 */
export async function startPC(opts) {
  const out = opts.outDir;
  const dir = path.join(out, opts.name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    ...(opts.files || []),
  ];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, text.replace(/\r?\n/g, '\r\n'));
    files.push({ src: path.relative(ROOT, host), dst });
  };
  const useCommand = opts.shell === 'command' || (opts.shell !== 'tshell' && fs.existsSync(B('COMMAND.COM')));
  let config = 'FILES=20\nBUFFERS=20\n';
  if (opts.himem !== false && fs.existsSync(B('HIMEM.SYS'))) {
    files.push({ src: 'build/HIMEM.SYS', dst: 'DOS\\HIMEM.SYS' });
    config += 'DEVICE=C:\\DOS\\HIMEM.SYS\n';
  }
  if (useCommand) {
    files.push({ src: 'build/COMMAND.COM', dst: 'COMMAND.COM' });
    config += 'SHELL=C:\\COMMAND.COM C:\\ /P\n';
    put('AUTOEXEC.BAT', '@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\n');
  } else {
    files.push({ src: 'build/ktest/TSHELL.EXE', dst: 'T\\TSHELL.EXE' });
    config += 'SHELL=C:\\T\\TSHELL.EXE\n';
  }
  config += opts.extraConfig || '';
  put('CONFIG.SYS', config);
  for (const [dst, text] of Object.entries(opts.texts || {})) put(dst, text);
  const m = {
    format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'ARM-DOS',
    date: '1989-06-01 12:00:00', boot: { src: 'build/bootsect.bin' }, files,
    dirs: ['DOS', 'T', ...(opts.dirs || [])],
  };
  const { img } = buildImage(m, ROOT);
  const hd = path.join(out, opts.name + '.img');
  fs.writeFileSync(hd, img);
  const speaker = [];
  const pc = await boot({ rom: B('rom.bin'), hd, mhz: opts.mhz, jit: opts.jit !== false,
    onSpeaker: (on, hz) => speaker.push([pc.timeMs, on, hz]) });
  pc.speaker = speaker;
  fs.rmSync(hd, { force: true });          // the machine has its own copy (34 MB each)
  fs.rmSync(dir, { recursive: true, force: true });
  pc.prompt = useCommand ? 'C:\\>' : 'T>';
  pc.hdPath = hd;
  pc.outDir = out;
  /** type a command, wait until a new prompt shows at the bottom */
  pc.cmd = (line, { timeoutMs = 60000 } = {}) => {
    pc.type(line + '\r');
    pc.until(() => pc.m.typingDone(), { timeoutMs: 10000 });
    pc.run(50);
    return pc.until(() => lastLine(pc).startsWith(pc.prompt) && pc.m.biosKeyBufferEmpty(), { timeoutMs });
  };
  pc.shot = async (file) => { const p = path.join(out, file); await pc.png(p); console.log(`     screenshot ${p}`); return p; };
  pc.bootOk = pc.until(() => lastLine(pc).startsWith(pc.prompt), { timeoutMs: 30000 });
  return pc;
}

export function lastLine(pc) {
  const l = pc.lines();
  const row = pc.m.cpu.m8[0x451];     // cursor row of page 0 (BDA 0x450/0x451)
  return (l[row] || '').trimEnd();
}

export const mode = (pc) => pc.m.vga.mode;
export const tap = (pc, code, after = 150) => { pc.m.keyDown(code); pc.run(60); pc.m.keyUp(code); pc.run(after); };
export const combo = (pc, codes, after = 200) => {
  for (const c of codes) { pc.m.keyDown(c); pc.run(30); }
  for (const c of [...codes].reverse()) { pc.m.keyUp(c); pc.run(30); }
  pc.run(after);
};

/** read a file from the running machine's hard disk (null if missing) */
export function readHdFile(pc, p) {
  try {
    const img = pc.m.ata.img;
    const r = new FatReader(Buffer.from(img.buffer, img.byteOffset, img.length));
    return Buffer.from(r.readFile(r.lookup(p))).toString('latin1');
  } catch { return null; }
}
