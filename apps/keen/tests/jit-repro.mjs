// apps/keen/tests/jit-repro.mjs - repro for a past emulator JIT regression:
// a batch file runs TERM.EXE (a script that just exits) and then UNZIP.EXE, which is
// loaded where TERM's code was. With the JIT, UNZIP then dies ("Exception 0Dh: data
// abort ... in UNZIP.EXE"); with `nojit` (or the JIT of the site deployed at 15:01) it
// unzips normally. Needs build/TERM.EXE, build/UNZIP.EXE, build/HIMEM.SYS.
//   node apps/keen/tests/jit-repro.mjs [nojit]
import fs from 'node:fs';
import path from 'node:path';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage } from '../../../disk/mkimage.mjs';
import { ROOT, B } from '../../term/tests/lib.mjs';

const tmp = path.join(B('keen-test'), 'jit.tmp');
fs.mkdirSync(tmp, { recursive: true });
const put = (n, s) => { const p = path.join(tmp, n); fs.writeFileSync(p, s.replace(/\r?\n/g, '\r\n')); return p; };
const files = [
  { src: 'build/IO.SYS', attr: 'HSR', first: 1 }, { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 }, { src: 'build/COMMAND.COM' },
  { src: 'build/HIMEM.SYS', dst: 'DOS\\' }, { src: 'build/UNZIP.EXE', dst: 'DOS\\' }, { src: 'build/TERM.EXE', dst: 'TERM\\' },
  { src: put('CONFIG.SYS', 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=20\nBUFFERS=20\nSHELL=C:\\COMMAND.COM C:\\ /P\n'), dst: 'CONFIG.SYS' },
  { src: put('AUTOEXEC.BAT', '@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\n'), dst: 'AUTOEXEC.BAT' },
  { src: put('X.SCR', 'MESSAGE "hi"\nPAUSE 1\nEXIT 0\n'), dst: 'X.SCR' },
  { src: put('T.BAT', '@ECHO OFF\nC:\\TERM\\TERM.EXE /S:X.SCR\nCLS\nUNZIP -o KEENDRMS.ZIP README.TXT\n'), dst: 'T.BAT' },
  { src: '3rdparty/keen/KEENDRMS.ZIP', dst: 'KEENDRMS.ZIP' },
];
const { img } = buildImage({ format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'JIT', boot: { src: 'build/bootsect.bin' }, files, dirs: ['DOS', 'TERM'] }, ROOT);
fs.rmSync(tmp, { recursive: true, force: true });
const pc = await boot({ rom: B('rom.bin'), hd: img, jit: !process.argv.includes('nojit') });
pc.waitText('C:\\>');
pc.type('T\r');
pc.run(6000);
const s = pc.screen();
console.log(s);
const ok = s.includes('Inflating: README.TXT');
console.log(ok ? 'OK: UNZIP ran normally after TERM' : 'FAIL: UNZIP did not run normally after TERM', pc.faults);
process.exit(ok ? 0 : 1);
