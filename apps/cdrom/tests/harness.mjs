// apps/cdrom/tests/harness.mjs - boots a private C: for the CD-ROM tests.
// Mirrors apps/arminfo/tests/harness.mjs (startPC) with a few more options:
//
//   const pc = await startPC({
//     name: 'dir',                 // image / temp file name
//     outDir: 'build/cdrom-test',  // where screenshots (pc.shot) go
//     armdos: 'build/ARMDOS.SYS',   // the ARMDOS.SYS to boot (default build/ARMDOS.SYS)
//     files: [{ src, dst }],       // extra files (src relative to the project root)
//     dirs: ['CD'],                // extra directories
//     texts: { 'T\\X.TXT': 'text' },   // extra text files (LF -> CRLF)
//     extraConfig: 'DEVICE=C:\\DOS\\ARMCD.SYS /D:ARMCD001\n',   // appended to CONFIG.SYS
//     autoexec: 'SBMIX /INIT /Q\n',    // appended to AUTOEXEC.BAT (COMMAND.COM only)
//     lastdrive: 'E',              // LASTDRIVE= (omitted if not given)
//     shell: 'auto'|'command'|'tshell', himem: true, mhz, jit,
//     cdrom: {...},                // passed through to boot() -> the Machine option `cdrom`
//     machine: {...},              // any other Machine/boot() options
//   });
//   pc.cmd('DIR D:')  pc.hasText(s)  pc.shot('x.png')  pc.bootOk  pc.prompt  lastLine(pc)
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

export async function startPC(opts) {
  const out = path.resolve(ROOT, opts.outDir || 'build/cdrom-test');
  fs.mkdirSync(out, { recursive: true });
  const dir = path.join(out, opts.name + '.files');
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: opts.armdos || 'build/ARMDOS.SYS', dst: 'ARMDOS.SYS', attr: 'HSR', first: 2 },
    ...(opts.files || []),
  ];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, text.replace(/\r?\n/g, '\r\n'));
    files.push({ src: path.relative(ROOT, host), dst });
  };
  const useCommand = opts.shell === 'command' || (opts.shell !== 'tshell' && fs.existsSync(B('COMMAND.COM')));
  let config = 'FILES=20\nBUFFERS=20\n';
  if (opts.lastdrive) config += `LASTDRIVE=${opts.lastdrive}\n`;
  if (opts.himem !== false && fs.existsSync(B('HIMEM.SYS'))) {
    files.push({ src: 'build/HIMEM.SYS', dst: 'DOS\\HIMEM.SYS' });
    config += 'DEVICE=C:\\DOS\\HIMEM.SYS\n';
  }
  if (useCommand) {
    files.push({ src: 'build/COMMAND.COM', dst: 'COMMAND.COM' });
    config += 'SHELL=C:\\COMMAND.COM C:\\ /P\n';
    put('AUTOEXEC.BAT', '@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\n' + (opts.autoexec || ''));
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
  fs.rmSync(dir, { recursive: true, force: true });
  const speaker = [];
  const pc = await boot({ rom: B('rom.bin'), hd: new Uint8Array(img), mhz: opts.mhz, jit: opts.jit !== false,
    ...(opts.machine || {}), ...(opts.cdrom ? { cdrom: opts.cdrom } : {}),
    onSpeaker: (on, hz) => speaker.push([pc.timeMs, on, hz]) });
  pc.speaker = speaker;
  pc.prompt = useCommand ? 'C:\\>' : 'T>';
  pc.outDir = out;
  /** type a command, wait until a prompt shows on the cursor's line */
  pc.cmd = (line, { timeoutMs = 60000, prompt = /^[A-Z]:\\[^>]*>/ } = {}) => {
    pc.type(line + '\r');
    pc.until(() => pc.m.typingDone(), { timeoutMs: 10000 });
    pc.run(50);
    return pc.until(() => prompt.test(lastLine(pc)) && pc.m.biosKeyBufferEmpty(), { timeoutMs });
  };
  pc.hasText = pc.hasText || ((s) => pc.screen().includes(s));
  pc.shot = async (file) => { const p = path.join(out, file); await pc.png(p); console.log(`     screenshot ${p}`); return p; };
  pc.bootOk = pc.until(() => lastLine(pc).startsWith(pc.prompt), { timeoutMs: 30000 });
  return pc;
}

export function lastLine(pc) {
  const l = pc.lines();
  const row = pc.m.cpu.m8[0x451];     // cursor row of page 0
  return (l[row] || '').trimEnd();
}

export const tap = (pc, code, after = 150) => { pc.m.keyDown(code); pc.run(60); pc.m.keyUp(code); pc.run(after); };
export const combo = (pc, codes, after = 200) => {
  for (const c of codes) { pc.m.keyDown(c); pc.run(30); }
  for (const c of [...codes].reverse()) { pc.m.keyUp(c); pc.run(30); }
  pc.run(after);
};

/** read a file from the running machine's C: (null if missing) */
export function readHdFile(pc, p) {
  try {
    const img = pc.m.ata.img;
    const r = new FatReader(Buffer.from(img.buffer, img.byteOffset, img.length));
    return Buffer.from(r.readFile(r.lookup(p))).toString('latin1');
  } catch { return null; }
}
