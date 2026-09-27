// apps/dosutil/tests/harness.mjs - boot ARM-DOS headless with the kernel's
// test shell (interactive, "T>" prompt) and run DOS 4 utilities like a user:
// type a command, wait for the prompt, read the screen.  Shared by the tests
// of FIND, SORT, MORE, TREE, COMP, XCOPY, LABEL, REPLACE and PRINT.
//
//   const s = await session({ name: 'find', files: [...], dirs: [...] });
//   const out = s.run('FIND "fox" \\WORK\\A.TXT');   // screen lines after the command
//   s.file('OUT\\X.TXT')                             // a file from the live disk
//
// The disk: IO.SYS, ARMDOS.SYS, \T\TSHELL.EXE, \T\CLS.EXE, the utilities in
// \DOS (build/<NAME>.EXE|COM), \WORK with the same A.TXT/B.TXT/C.TXT/README.TXT
// as the real-DOS 4.00 reference disk (label ARMDOS, serial 4069-12FF).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const B = (p) => path.join(ROOT, 'build', p);
const DATA = 'apps/dosutil/tests/data';

export const UTILS = ['FIND.EXE', 'SORT.EXE', 'MORE.COM', 'TREE.COM', 'COMP.COM', 'XCOPY.EXE',
  'LABEL.COM', 'REPLACE.EXE', 'PRINT.COM'];

export function makeImage({ name, files = [], text = {}, dirs = [], label = 'ARMDOS', serial = '4069-12FF',
                            config, work = true, sizeMB = 32 }) {
  const dir = B(path.join('u4test', name));
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const all = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/ktest/TSHELL.EXE', dst: 'T\\' },
    { src: 'build/u4test/CLS.EXE', dst: 'T\\' },
    { src: 'build/u4test/REDIR.EXE', dst: 'T\\' },
  ];
  for (const u of UTILS) if (fs.existsSync(B(u))) all.push({ src: 'build/' + u, dst: 'DOS\\' });
  const put = (dst, content) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, content);
    all.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', (config ?? 'FILES=20\nSHELL=C:\\T\\TSHELL.EXE\n').replace(/\n/g, '\r\n'));
  const d = ['DOS', 'T', ...(work ? ['WORK'] : []), 'OUT', ...dirs];
  if (work) for (const f of ['README.TXT', 'A.TXT', 'B.TXT', 'C.TXT']) all.push({ src: `${DATA}/WORK/${f}`, dst: 'WORK\\' });
  for (const [dst, content] of Object.entries(text)) put(dst, content);
  all.push(...files);
  // byte for byte: the fixtures are the reference disk's exact bytes (N.TXT & co.
  // have bare LFs on purpose), so mkimage's LF -> CR LF for text files is off
  const m = {
    format: 'hd', sizeMB, heads: 16, sectorsPerTrack: 63, label, serial,
    date: '1988-06-17 12:00:00', boot: { src: 'build/bootsect.bin' },
    files: all.map((f) => ({ binary: true, ...f })), dirs: d,
  };
  return { img: buildImage(m, ROOT).img, dir };
}

export class Session {
  constructor(pc, dir, command = false) {
    this.pc = pc; this.dir = dir; this.command = command;
    // the test shell's "T>", or COMMAND.COM's "C:\...>" (PROMPT $P$G)
    this.promptRe = command ? /[A-Z]:\\[^>]*>$/ : /T>$/;
  }

  /** screen lines (trimmed right), without trailing empty lines */
  lines() {
    const l = this.pc.screen().split('\n').map((x) => x.replace(/\s+$/, ''));
    while (l.length && !l[l.length - 1]) l.pop();
    return l;
  }
  atPrompt() {
    const l = this.lines();
    return l.length > 0 && this.promptRe.test(l[l.length - 1]);
  }
  idle(ms = 300) { this.pc.waitIdle({ quietMs: ms, timeoutMs: 30000 }); }
  waitPrompt(timeoutMs = 30000) {
    const end = this.pc.timeMs + timeoutMs;
    while (this.pc.timeMs < end) {
      this.pc.waitIdle({ quietMs: 200, timeoutMs: 5000 });
      if (this.atPrompt()) return true;
    }
    return false;
  }
  /** type a line at the T> prompt; returns the screen lines after the
   *  command line, with the final "T>" prompt removed.  steps: [[waitText,
   *  typeText], ...] answer prompts on the way. */
  run(cmd, { steps = [], cls = true, timeoutMs = 60000 } = {}) {
    if (cls) {
      this.pc.type('CLS\r');
      this.waitPrompt();
    }
    this.pc.type(cmd + '\r');
    for (const [wait, keys] of steps) {
      if (wait && !this.pc.waitText(wait, { timeoutMs })) throw new Error(`"${wait}" did not appear for ${cmd}:\n${this.pc.screen()}`);
      this.idle(150);
      this.pc.type(keys);
    }
    if (!this.waitPrompt(timeoutMs)) throw new Error(`no prompt after ${cmd}:\n${this.pc.screen()}`);
    const l = this.lines();
    l[l.length - 1] = l[l.length - 1].replace(this.promptRe, '');
    let i = -1;
    for (let k = 0; k < l.length; k++) if (l[k].includes('>' + cmd.slice(0, 40))) { i = k; break; }
    const out = l.slice(i >= 0 ? i + 1 : 0);
    while (out.length && !out[out.length - 1]) out.pop();
    return out;
  }
  reader() { return new FatReader(Buffer.from(this.pc.machine.ata.img)); }
  file(p) {
    const r = this.reader();
    const e = r.lookup(p);
    return e ? Buffer.from(r.readFile(e)) : null;
  }
  exists(p) { return !!this.reader().lookup(p); }
  save(p) { fs.writeFileSync(p, this.pc.machine.ata.img); }
}

export async function session(opts) {
  if (opts.command) {                  // boot COMMAND.COM instead of the test shell
    if (!fs.existsSync(B('COMMAND.COM'))) { console.log('skip (no build/COMMAND.COM): ' + opts.name); return null; }
    opts = { ...opts, files: [...(opts.files || []), { src: 'build/COMMAND.COM' }],
             config: opts.config ?? 'FILES=20\nSHELL=C:\\COMMAND.COM /P\n',
             text: { 'AUTOEXEC.BAT': '@ECHO OFF\r\nPATH C:\\DOS;C:\\T\r\nPROMPT $P$G\r\n', ...(opts.text || {}) } };
  }
  const { img, dir } = makeImage(opts);
  const pc = await boot({ rom: B('rom.bin'), hd: new Uint8Array(img), ...(opts.boot || {}) });
  const s = new Session(pc, dir, !!opts.command);
  if (!pc.waitText(opts.command ? 'C:\\>' : 'T>', { timeoutMs: 30000 })) throw new Error('no prompt:\n' + pc.screen());
  s.idle();
  return s;
}

// -------------------------------------------------------------- checking
export class Checker {
  constructor(title) { this.title = title; this.fails = 0; this.n = 0; }
  ok(cond, what, detail) {
    this.n++;
    console.log(`${cond ? 'ok  ' : 'FAIL'} ${what}`);
    if (!cond) { this.fails++; if (detail) console.log('     ' + String(detail).replace(/\n/g, '\n     ')); }
    return cond;
  }
  /** compare screen lines with the expected text (lines joined by \n) */
  lines(got, want, what) {
    const g = got.join('\n'), w = Array.isArray(want) ? want.join('\n') : want;
    return this.ok(g === w, what, `got:\n${g}\n--- want:\n${w}`);
  }
  bytes(got, want, what) {
    const ok = got && Buffer.compare(Buffer.from(got), Buffer.from(want)) === 0;
    return this.ok(ok, what, ok ? '' : `got:  ${JSON.stringify(got && got.toString('latin1'))}\nwant: ${JSON.stringify(Buffer.from(want).toString('latin1'))}`);
  }
  done() {
    console.log(`\n${this.title}: ${this.n - this.fails} passed, ${this.fails} failed`);
    process.exit(this.fails ? 1 : 0);
  }
}

/** the expected-output files captured from the real MS-DOS 4.00 (tests/expected) */
export function expected(app, name) {
  return fs.readFileSync(path.join(ROOT, 'apps', app, 'tests', 'expected', name));
}
