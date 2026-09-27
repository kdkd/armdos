#!/usr/bin/env node
// apps/debug/tests/mkref.mjs - run scripts through the GENUINE MS-DOS 4.00 DEBUG.COM
// in DOSBox-X (the set-up in $ARMDOS_REFS/dos400-verify, as apps/edlin/tests/mkref.mjs)
// and save what it printed to apps/debug/tests/ref/<NAME>.OUT: the reference for
// the formats tests/run.mjs checks (E fields, S/C/H/W lines, T's blank line...).
// Developer tool (needs dosbox-x and mtools); the references are checked in.
//
//   node apps/debug/tests/mkref.mjs [--seconds N]
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const argv = process.argv.slice(2);
const SECONDS = +(argv[argv.indexOf('--seconds') + 1] || 60);
const WORK = path.resolve(HERE, '../../../build/debug-ref');
// ARMDOS_REFS: a directory with dos400-verify/tools/hd-after-tests.img (a 32 MB hard disk
// image with MS-DOS 4.00 installed in C:\DOS) and msdos400-pcjs/MSDOS400-DISK1.img (the
// original MS-DOS 4.00 disk 1). Not distributed: MS-DOS 4.00 binaries are not open source.
const REFS = process.env.ARMDOS_REFS || '';
const VERIFY = path.join(REFS, 'dos400-verify');
const DISK1 = path.join(REFS, 'msdos400-pcjs/MSDOS400-DISK1.img');
const REF = path.join(HERE, 'ref');

export const scripts = {
  // byte lists, dump, search, compare, hex, fill, move, write, errors
  FORMATS: ['E 200 41 42 43 0D', 'D 200 L4', 'S 200 L4 42', 'C 200 L3 201', 'H 1234 34', 'H 5 FFFF',
    'F 300 L8 AA BB', 'D 300 L8', 'M 300 L4 310', 'D 30E L6', 'D 1FC L8', 'N T.BIN', 'R CX', '4', 'W 200',
    'N T.EXE', 'W', 'N', 'W', 'N NOPE.TXT', 'L', 'zz', 'D 100 XYZ', 'R F', 'XX', 'R F', 'ZR NZ', 'RQQ',
    'G 1 2 3 4 5 6 7 8 9 A B', 'T 0', 'Q'],
  // E in its interactive mode: space, '-', 1-2 digits, Enter
  EINTER: ['E 100', '41 42 4-5', 'D 100 L3', 'E 110', '  -', 'Q'],
  // a tiny program: A, U, T, G to the end, G after the end
  PROG: ['A 100', 'MOV DL,41', 'MOV AH,2', 'INT 21', 'INT 20', '', 'T', 'G', 'G', 'Q'],
  // names and errors
  NAMES: ['W', 'L', 'N NOPE.COM', 'L', 'N NOPE.EXE', 'L', 'H 1', 'D 200 100', 'R CX', 'XYZ', 'E 200', '', 'D 100 10F',
    'U 100 L2', 'U', 'A 100', 'MOV AX,', 'JMP 200', '', 'L 100 2 0 1', 'D 2FE L2', 'Q'],

};

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  if (!REFS || !fs.existsSync(path.join(VERIFY, 'tools/hd-after-tests.img')) || !fs.existsSync(DISK1)) {
    console.log('mkref: skipped - set ARMDOS_REFS to the MS-DOS 4.00 reference images (see the comment at the top)');
    process.exit(0);
  }
  fs.rmSync(WORK, { recursive: true, force: true });
  fs.mkdirSync(WORK, { recursive: true });
  const m = (cmd, ...a) => execFileSync(cmd, a, { stdio: ['ignore', 'pipe', 'pipe'] }).toString();
  const hd = path.join(WORK, 'hd.img'), fd = path.join(WORK, 'fd.img');
  fs.copyFileSync(path.join(VERIFY, 'tools/hd-after-tests.img'), hd);
  fs.copyFileSync(DISK1, fd);
  const C = `${hd}@@32256`;
  const put = (img, data, dst) => {
    const t = path.join(WORK, 'tmp.bin');
    fs.writeFileSync(t, Buffer.from(data, 'latin1'));
    m('mcopy', '-o', '-i', img, t, dst);
  };
  try { m('mmd', '-i', C, '::/D'); } catch { /* exists */ }
  let bat = '@ECHO OFF\r\nCD \\D\r\n';
  for (const [name, lines] of Object.entries(scripts)) {
    put(C, lines.join('\r\n') + '\r\n', `::/D/${name}.SCR`);
    bat += `DEBUG < ${name}.SCR > ${name}.OUT\r\n`;
  }
  bat += 'ECHO done> DONE.TXT\r\n';
  put(C, bat, '::/D/RUN.BAT');
  put(fd, '@ECHO OFF\r\nPATH C:\\DOS\r\nC:\r\nCALL \\D\\RUN.BAT\r\n', '::/AUTOEXEC.BAT');
  fs.writeFileSync(path.join(WORK, 'dosbox.conf'), `[sdl]\noutput=surface\n[dosbox]\nmachine=svga_s3\nmemsize=4\nquit warning=false\n[cpu]\ncycles=max\n[autoexec]\nIMGMOUNT 2 ${hd} -t hdd -fs none -size 512,63,16,65\nIMGMOUNT A ${fd} -t floppy\nBOOT -L A\n`);
  try {
    execFileSync('timeout', [String(SECONDS), 'dosbox-x', '-conf', path.join(WORK, 'dosbox.conf'), '-nopromptfolder', '-fastlaunch'],
      { stdio: 'ignore', env: { ...process.env, SDL_VIDEODRIVER: 'dummy', SDL_AUDIODRIVER: 'dummy' } });
  } catch { /* timeout ends it */ }
  m('mcopy', '-s', '-n', '-o', '-i', C, '::/D', WORK);
  if (!fs.existsSync(path.join(WORK, 'D', 'DONE.TXT'))) { console.error('the batch did not finish'); process.exit(1); }
  fs.mkdirSync(REF, { recursive: true });
  for (const name of Object.keys(scripts)) {
    fs.copyFileSync(path.join(WORK, 'D', `${name}.OUT`), path.join(REF, `${name}.OUT`));
    console.log(`=== ${name}\n` + fs.readFileSync(path.join(REF, `${name}.OUT`), 'latin1'));
  }
}
