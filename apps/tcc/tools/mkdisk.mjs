#!/usr/bin/env node
// apps/tcc/tools/mkdisk.mjs - build C:\TC for the hard disk image:
//
//   node apps/tcc/tools/mkdisk.mjs BUILD/tcc     -> BUILD/tcc/disk/TC
//
//   TC\INCLUDE\...    headers with DOS 8.3 names (tools/mksys.mjs --dos)
//   TC\LIB\CRT0.O     the SDK's start-up code
//   TC\LIB\LIBC.A     libdos + newlib-nano libc and libm + libgcc
//   TC\SAMPLES\*      apps/tcc/samples (CRLF line ends)
//   TC\README.TXT     apps/tcc/README.TXT
//   TC\COPYING.TXT    TinyCC's licence (LGPL 2.1)
//
// TCC.EXE itself goes to C:\DOS (on the PATH) with the other programs.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');
const APP = path.resolve(HERE, '..');
const OUT = path.resolve(process.argv[2] ?? path.join(ROOT, 'build/tcc'));
const TC = path.join(OUT, 'disk/TC');
const crlf = (b) => Buffer.from(b.toString('latin1').replace(/\r?\n/g, '\r\n'), 'latin1');
const is83 = (n) => /^[A-Z0-9_$~!#%&@^'(){}-]{1,8}(\.[A-Z0-9_$~!#%&@^'(){}-]{1,3})?$/.test(n);

fs.rmSync(TC, { recursive: true, force: true });
fs.mkdirSync(TC, { recursive: true });
execFileSync(process.execPath, [path.join(HERE, 'mksys.mjs'), '--dos', TC], { stdio: 'inherit' });

fs.mkdirSync(path.join(TC, 'LIB'));
fs.copyFileSync(path.join(OUT, 'libc.a'), path.join(TC, 'LIB/LIBC.A'));
fs.copyFileSync(path.join(ROOT, 'build/sdk/crt0.o'), path.join(TC, 'LIB/CRT0.O'));

fs.mkdirSync(path.join(TC, 'SAMPLES'));
for (const f of fs.readdirSync(path.join(APP, 'samples')).sort()) {
  if (!is83(f)) throw new Error(`samples/${f}: not an 8.3 name`);
  fs.writeFileSync(path.join(TC, 'SAMPLES', f), crlf(fs.readFileSync(path.join(APP, 'samples', f))));
}
fs.writeFileSync(path.join(TC, 'README.TXT'), crlf(fs.readFileSync(path.join(APP, 'README.TXT'))));
fs.writeFileSync(path.join(TC, 'COPYING.TXT'), crlf(fs.readFileSync(path.join(APP, 'src/COPYING'))));

let total = 0, count = 0;
const walk = (d) => { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, e.name); if (e.isDirectory()) walk(p); else { total += fs.statSync(p).size; count++; } } };
walk(TC);
console.log(`mkdisk: ${TC}: ${count} files, ${Math.round(total / 1024)} KB`);
