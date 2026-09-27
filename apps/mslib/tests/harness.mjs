// apps/mslib/tests/harness.mjs - shared test harness for the MS-DOS 4.0
// utilities compiled for ARM (FC, MEM, ATTRIB, FDISK, SUBST, JOIN).
//
// A session builds a 32 MB hard disk image (IO.SYS, ARMDOS.SYS, the
// kernel's test shell as C:\T\TSHELL.EXE running C:\T\S.TXT, the programs
// under test in C:\T, the \WORK files of the DOS 4.00 verification disk),
// boots it headlessly, lets the script run to its end and returns the disk
// afterwards, so the redirected outputs can be compared byte for byte with
// what the real MS-DOS 4.00 binaries wrote (apps/*/tests/expected/, made
// with apps/mslib/tools/dos400run.sh).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const B = (p) => path.join(ROOT, 'build', p);

export function need(files) {
  const missing = files.filter((f) => !fs.existsSync(path.join(ROOT, f)));
  if (missing.length) throw new Error('missing: ' + missing.join(', ') + ' (make them first)');
}

// opts: name, outDir, programs [build/X.EXE ...] (to C:\T), files [{src,dst}],
// texts {dst: string (LF -> CR LF)}, bytes {dst: Buffer}, script (TSHELL lines),
// config (CONFIG.SYS lines before SHELL=), dirs, work (default true), sizeMB,
// cylinders; floppy: true puts all that on a 1.44 MB diskette in A: (the
// machine boots from it) and hd (a Buffer) is the hard disk, e.g. a blank one
export function makeImage(o) {
  const dir = path.join(o.outDir, o.name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/ktest/TSHELL.EXE', dst: 'T\\TSHELL.EXE' },
  ];
  let n = 0;
  const put = (dst, buf) => {
    const host = path.join(dir, `f${n++}_` + dst.replace(/[\\/:]/g, '_'));
    fs.writeFileSync(host, buf);
    files.push({ src: path.relative(ROOT, host), dst });
  };
  for (const p of o.programs || []) files.push({ src: p, dst: 'T\\' });
  if (o.work !== false)
    for (const f of fs.readdirSync(path.join(ROOT, 'apps/mslib/tests/work')))
      files.push({ src: `apps/mslib/tests/work/${f}`, dst: `WORK\\${f}` });
  for (const f of o.files || []) files.push(f);
  for (const [d, t] of Object.entries(o.texts || {})) put(d, Buffer.from(t.replace(/\r?\n/g, '\r\n'), 'latin1'));
  for (const [d, b] of Object.entries(o.bytes || {})) put(d, b);
  const X = o.floppy ? 'A' : 'C';
  const config = [...(o.config || ['FILES=20', 'BUFFERS=20']), `SHELL=${X}:\\T\\TSHELL.EXE ${X}:\\T\\S.TXT`];
  put('CONFIG.SYS', Buffer.from(config.join('\r\n') + '\r\n'));
  put('T\\S.TXT', Buffer.from([...o.script, 'exit 0'].join('\r\n') + '\r\n', 'latin1'));
  const m = {
    ...(o.floppy ? { format: 'fd1440' } : { format: 'hd', heads: 16, sectorsPerTrack: 63, ...(o.cylinders ? { cylinders: o.cylinders } : { sizeMB: o.sizeMB || 32 }) }),
    label: 'ARMDOS',
    date: '1988-06-17 12:00:00', boot: { src: 'build/bootsect.bin' }, files,
    dirs: ['T', 'OUT', ...(o.work !== false ? ['WORK'] : []), ...(o.dirs || [])],
  };
  const { img } = buildImage(m, ROOT);
  fs.writeFileSync(path.join(dir, 'before.img'), img);
  return { dir, img };
}

export async function session(o) {
  const { dir, img } = makeImage(o);
  const pc = o.floppy
    ? await boot({ rom: B('rom.bin'), fd: new Uint8Array(img), hd: new Uint8Array(o.hd), ...(o.boot || {}) })
    : await boot({ rom: B('rom.bin'), hd: new Uint8Array(img), ...(o.boot || {}) });
  let finished = true;
  if (o.drive) finished = await o.drive(pc);
  else finished = pc.waitExit({ timeoutMs: (o.timeout || 60) * 1000 });
  const after = Buffer.from(pc.machine.ata.img);
  // the images stay only with KEEP_IMAGES=1 (32 MB each; keep build/ lean)
  if (process.env.KEEP_IMAGES) fs.writeFileSync(path.join(dir, 'after.img'), after);
  else fs.rmSync(path.join(dir, 'before.img'), { force: true });
  let reader = null;
  try { reader = new FatReader(after); } catch { /* the test may have repartitioned the disk */ }
  const read = (p) => {
    const e = reader && reader.lookup(p);
    return e ? Buffer.from(reader.readFile(e)) : null;
  };
  const exits = [...pc.serial.matchAll(/T:EXIT (\S+) (\d+) (\d+)/g)].map((m) => ({ prog: m[1], code: +m[2], type: +m[3] }));
  return { pc, dir, after, read, reader, finished, exits, serial: pc.serial };
}

// ------------------------------------------------------------ checking
export class Checker {
  constructor(title) { this.title = title; this.pass = 0; this.fail = 0; }
  ok(cond, what, detail) {
    if (cond) this.pass++; else this.fail++;
    console.log(`${cond ? 'ok  ' : 'FAIL'} ${what}`);
    if (!cond && detail) console.log('     ' + String(detail).replace(/\n/g, '\n     '));
    return cond;
  }
  // byte-exact comparison, with a readable diff on failure
  same(actual, expected, what) {
    const a = actual == null ? null : Buffer.from(actual), e = Buffer.from(expected);
    if (a && a.equals(e)) return this.ok(true, what);
    const show = (b) => b == null ? '(missing)' : b.toString('latin1').replace(/\r\n/g, '\u21b5\n').replace(/\r/g, '\u240d').replace(/\t/g, '\u2192');
    return this.ok(false, what, `expected:\n${show(e)}\nactual:\n${show(a)}`);
  }
  done() {
    console.log(`\n${this.title}: ${this.pass} passed, ${this.fail} failed`);
    process.exit(this.fail ? 1 : 0);
  }
}
