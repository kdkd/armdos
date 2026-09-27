#!/usr/bin/env node
// apps/tcc/tools/mksys.mjs - assemble TCC's system directory:
//
//   node apps/tcc/tools/mksys.mjs --host OUTDIR       include/ with the original
//        (long, lower-case) header names, for the Linux cross compiler
//   node apps/tcc/tools/mksys.mjs --dos OUTDIR        INCLUDE\ with DOS 8.3 names
//        (what ARM-DOS itself makes of the long names: the name part is cut to
//        8 characters, as DOS does when a program opens "_default_types.h")
//
// Headers: TinyCC's own (stddef.h, stdarg.h, float.h, ...), apps/tcc/include
// (iso646.h), newlib's
// (newlib-nano's newlib.h), and the ARM-DOS SDK's (dos.h, conio.h, ...);
// fcntl.h and malloc.h exist in both newlib and the SDK (which uses
// #include_next): they are merged into one file. A few newlib headers get a
// small fix so they work without GCC (see PATCHES).
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');
const [mode, out] = process.argv.slice(2);
if (!['--host', '--dos'].includes(mode) || !out) {
  console.error('usage: mksys.mjs --host|--dos OUTDIR');
  process.exit(2);
}
const DOS = mode === '--dos';

// newlib's include directory (the one arm-none-eabi-gcc uses)
const sysroot = execFileSync('arm-none-eabi-gcc', ['-print-sysroot']).toString().trim();
let NEWLIB = path.join(sysroot || '/usr/lib/arm-none-eabi', 'include');
if (!fs.existsSync(path.join(NEWLIB, 'stdio.h'))) NEWLIB = '/usr/lib/arm-none-eabi/include';
const NANO = [path.join(NEWLIB, 'newlib-nano'), path.join(NEWLIB, 'nano')].find((d) => fs.existsSync(path.join(d, 'newlib.h')));
const TCCINC = path.join(ROOT, 'apps/tcc/src/include');
const OURINC = path.join(ROOT, 'apps/tcc/include');      // our additions (iso646.h)
const SDKINC = path.join(ROOT, 'sdk/include');

// headers that make no sense on ARM-DOS (or need things TCC lacks)
const SKIP = new Set(['elf.h', 'ar.h', 'cpio.h', 'tar.h', 'regdef.h', 'ndbm.h', 'pthread.h', 'threads.h',
  'spawn.h', 'complex.h', 'fenv.h', 'devctl.h', 'termios.h', 'utmp.h', 'grp.h', 'pwd.h',
  'glob.h', 'wordexp.h', 'iconv.h', 'langinfo.h', 'envlock.h', 'stdatomic.h', 'tgmath.h', 'dirent.h']);
const SKIPSUB = new Set(['sys/tree.h', 'sys/queue.h', 'sys/iconvnls.h',
  'sys/fenv.h', 'machine/termios.h', 'sys/custom_file.h', 'sys/resource.h', 'sys/wait.h', 'sys/dir.h',
  'sys/dirent.h']);

// small fixes for a non-GCC compiler: [file, from, to]
const PATCHES = [
  // no __GNUC__: <limits.h> would be included in the middle of itself (and
  // print "ARG_MAX redefined"). TCC predefines __SCHAR_MAX__ etc. for us.
  ['machine/_default_types.h', '#if __GNUC_PREREQ (3, 3)', '#if __GNUC_PREREQ (3, 3) || defined(__TINYC__)'],
];

const files = new Map();   // dst relative path (with '/') -> Buffer
const add = (rel, buf) => files.set(rel, buf);
const readdir = (d) => fs.readdirSync(d).filter((f) => f.endsWith('.h')).sort();

for (const f of readdir(NEWLIB)) if (!SKIP.has(f)) add(f, fs.readFileSync(path.join(NEWLIB, f)));
for (const sub of ['sys', 'machine'])
  for (const f of readdir(path.join(NEWLIB, sub)))
    if (!SKIPSUB.has(`${sub}/${f}`)) add(`${sub}/${f}`, fs.readFileSync(path.join(NEWLIB, sub, f)));
add('newlib.h', fs.readFileSync(path.join(NANO, 'newlib.h')));        // the nano configuration
for (const f of readdir(TCCINC)) if (f !== 'tccdefs.h' && !SKIP.has(f)) add(f, fs.readFileSync(path.join(TCCINC, f)));
for (const f of readdir(OURINC)) add(f, fs.readFileSync(path.join(OURINC, f)));
for (const f of readdir(SDKINC)) {
  let text = fs.readFileSync(path.join(SDKINC, f), 'latin1');
  const m = text.match(/#include_next <([^>]+)>/);
  if (m) {
    const base = files.get(m[1]);
    if (!base) throw new Error(`${f}: #include_next <${m[1]}>: no such newlib header`);
    text = text.replace(m[0], `/* --- newlib's <${m[1]}> --- */\n${base.toString('latin1')}\n/* --- ARM-DOS additions --- */`);
  }
  add(f, Buffer.from(text, 'latin1'));
}
for (const [f, from, to] of PATCHES) {
  const t = files.get(f).toString('latin1');
  if (!t.includes(from)) throw new Error(`patch for ${f} does not apply`);
  add(f, Buffer.from(t.replace(from, to), 'latin1'));
}

// DOS names: 8.3, upper case; collisions are an error
const dosName = (rel) => rel.split('/').map((c) => {
  const i = c.lastIndexOf('.');
  const [n, e] = i > 0 ? [c.slice(0, i), c.slice(i + 1)] : [c, ''];
  return (n.slice(0, 8) + (e ? '.' + e.slice(0, 3) : '')).toUpperCase();
}).join('/');

const incDir = path.join(out, DOS ? 'INCLUDE' : 'include');
fs.rmSync(incDir, { recursive: true, force: true });
const seen = new Map();
let bytes = 0;
for (const [rel, buf] of [...files].sort()) {
  const dst = DOS ? dosName(rel) : rel;
  if (seen.has(dst)) throw new Error(`8.3 name collision: ${rel} and ${seen.get(dst)} -> ${dst}`);
  seen.set(dst, rel);
  const p = path.join(incDir, dst);
  fs.mkdirSync(path.dirname(p), { recursive: true });
  // DOS text files: CRLF line ends
  fs.writeFileSync(p, DOS ? Buffer.from(buf.toString('latin1').replace(/\r?\n/g, '\r\n'), 'latin1') : buf);
  bytes += buf.length;
}
console.log(`mksys: ${files.size} headers (${Math.round(bytes / 1024)} KB) -> ${incDir}`);
