#!/usr/bin/env node
// apps/format/tests/run.mjs - FORMAT: the redirected output compared byte for
// byte with the real MS-DOS 4.00 FORMAT.COM (tests/ref/*.TXT, captured in
// DOSBox-X: same input, percent counter included), the diskettes it makes
// (fsck.fat -n, mtools, boot record), a FORMAT /S diskette that boots,
// errors, and FORMAT C: on a scratch hard disk.
//   node apps/format/tests/run.mjs [--quick]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage } from '../../../disk/mkimage.mjs';
import { B, ROOT, OUT, hdImage, blankFloppy, start, cmd, exitCode, answer, readFile, checkFsck, checkMdir, same, check, summary, save } from './lib.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ref = (n) => fs.readFileSync(path.join(HERE, 'ref', n), 'latin1').replace(/drive B:/g, 'drive A:');
const quick = process.argv.includes('--quick');

// serial numbers come from the clock; the rest must be identical
const serialOf = (s) => (/Volume Serial Number is ([0-9A-F]{4}-[0-9A-F]{4})/.exec(s ?? '') || [])[1];
const norm = (s) => (s ?? '').replace(/Serial Number is [0-9A-F]{4}-[0-9A-F]{4}/g, 'Serial Number is XXXX-XXXX');

const hd = hdImage('format', { extra: {
  'T\\F1.TXT': '\r\nTEST\r\nN\r\n', 'T\\F2.TXT': '\r\nN\r\n', 'T\\F3.TXT': '\r\nbad.lbl\r\nlowcase\r\nN\r\n',
  'T\\F4.TXT': '\r\n\r\nY\r\n\r\n\r\nN\r\n',
} });
const pc = await start(hd, blankFloppy());
const fmt = (args, out, input) => cmd(pc, `C:\\T\\REDIR.EXE C:\\T\\${out}.TXT C:\\T\\${input}.TXT C:\\DOS\\FORMAT.COM ${args}`, { timeoutMs: 300000 });
const fresh = () => { pc.machine.ejectFloppy(); pc.run(50); pc.machine.insertFloppy(blankFloppy()); pc.run(50); };
const out = (n) => readFile(pc.hd, `\\T\\${n}.TXT`);
const bootrec = (img) => Buffer.from(img.subarray(0, 512));

// 1. a blank diskette, label TEST
fmt('A:', 'FMT1', 'F1');
const fmt1 = out('FMT1'), fd1 = new Uint8Array(pc.machine.floppy);
same(norm(fmt1), norm(ref('FMT1.TXT')), 'FORMAT A: output (160 tracks of "nn percent") == real MS-DOS 4.00');
check(exitCode(pc) === 0, 'FORMAT A: exit code 0');
checkFsck(fd1, 'fmt1', 'fsck.fat -n: clean');
{
  const b = bootrec(fd1);
  const ser = (serialOf(fmt1) ?? '').replace('-', '');
  check(b.readUInt32LE(0) === 0xEA00000E && b.toString('latin1', 4, 11) === 'ARMDOS4' && b[0x1FE] === 0x55 && b[0x1FF] === 0xAA,
    'boot record: ARM "b 0x40", OEM ARMDOS4, 55AA');
  check(b.readUInt16LE(0x0B) === 512 && b[0x0D] === 1 && b.readUInt16LE(0x0E) === 1 && b[0x10] === 2 && b.readUInt16LE(0x11) === 224 &&
    b.readUInt16LE(0x13) === 2880 && b[0x15] === 0xF0 && b.readUInt16LE(0x16) === 9 && b.readUInt16LE(0x18) === 18 && b.readUInt16LE(0x1A) === 2,
    'BPB: the DOS 4 1.44 MB layout');
  check(b[0x26] === 0x29 && b.readUInt32LE(0x27).toString(16).toUpperCase().padStart(8, '0') === ser &&
    b.toString('latin1', 0x2B, 0x36) === 'TEST       ' && b.toString('latin1', 0x36, 0x3E) === 'FAT12   ',
    `extended boot record: serial ${serialOf(fmt1)} as printed, label TEST, FAT12`);
  check(Buffer.compare(b.subarray(0x40, 0x1FE), fs.readFileSync(B('bootsect.bin')).subarray(0x40, 0x1FE)) === 0, 'boot code = build/bootsect.bin');
  checkMdir(fd1, ['::/'], (dir) => /Volume in drive : is TEST/.test(dir) && /No files/.test(dir), 'mtools: label TEST, no files');
}

// 2. an invalid label, then a lower-case one (DOS stores it upper case)
fresh(); fmt('A:', 'FMT3', 'F3');
same(norm(out('FMT3')), norm(ref('FMT3.TXT')), 'FORMAT A: "bad.lbl" -> "Invalid Volume ID", re-prompt == real MS-DOS 4.00');
checkMdir(pc.machine.floppy, ['::/'], (dir) => /Volume in drive : is LOWCASE/.test(dir), 'label stored as LOWCASE');

// 3. /V:HELLO (no prompt)
fresh(); fmt('A: /V:hello', 'FMTV', 'F2');
same(norm(out('FMTV')), norm(ref('FMTV.TXT')), 'FORMAT A: /V:hello output == real MS-DOS 4.00');
checkMdir(pc.machine.floppy, ['::/'], (dir) => /Volume in drive : is HELLO/.test(dir), '/V:hello -> label HELLO');

// 4. /S: IO.SYS, ARMDOS.SYS, COMMAND.COM (COMSPEC, here the test shell)
fresh(); fmt('A: /S', 'FMTS', 'F1');
const fdS = new Uint8Array(pc.machine.floppy);
{
  const sizes = ['IO.SYS', 'ARMDOS.SYS', 'ktest/TSHELL.EXE'].map((f) => fs.statSync(B(f)).size);
  const sys = sizes.reduce((a, s) => a + Math.ceil(s / 512) * 512, 0);
  const want = ref('FMTS.TXT').replace('    109056 bytes used by system', String(sys).padStart(10) + ' bytes used by system')
    .replace('   1348608 bytes available on disk', String(1457664 - sys).padStart(10) + ' bytes available on disk')
    .replace('      2634 allocation units', String((1457664 - sys) / 512).padStart(10) + ' allocation units');
  same(norm(out('FMTS')), norm(want), 'FORMAT A: /S output == real MS-DOS 4.00 (with ARM-DOS\'s file sizes)');
  checkFsck(fdS, 'fmts', 'FORMAT /S: fsck.fat -n clean');
  const d = Buffer.from(fdS.subarray(19 * 512, 19 * 512 + 128));
  check(d.toString('latin1', 0, 11) === 'IO      SYS' && (d[11] & 0x1F) === 7 && d.readUInt16LE(26) === 2 &&
    d.toString('latin1', 32, 43) === 'ARMDOS  SYS' && (d[43] & 0x1F) === 7 && d.toString('latin1', 64, 75) === 'COMMAND COM' &&
    d.toString('latin1', 96, 107) === 'TEST       ' && d[107] === 8,
    'root: IO.SYS (RHS, cluster 2), ARMDOS.SYS (RHS), COMMAND.COM, label');
  const io = readFile(fdS, '\\IO.SYS');
  check(io && Buffer.from(io, 'latin1').equals(fs.readFileSync(B('IO.SYS'))), 'IO.SYS copied intact');
}

// 5. errors
pc.type('C:\\DOS\\FORMAT.COM\r'); pc.run(1500);
check(/Required parameter missing - *\n/.test(pc.screen() + '\n'), 'FORMAT (no drive) -> "Required parameter missing -"', pc.screen());
pc.type('C:\\DOS\\FORMAT.COM A: /Q\r'); pc.run(1500);
check(/Invalid switch -  \/Q/.test(pc.screen()), 'FORMAT A: /Q -> "Invalid switch -  /Q"', pc.screen());
pc.type('C:\\DOS\\FORMAT.COM Q:\r'); pc.run(1500);
check(/Invalid drive specification/.test(pc.screen()), 'FORMAT Q: -> "Invalid drive specification"', pc.screen());
pc.type('C:\\DOS\\FORMAT.COM A: /1\r'); pc.run(1500);
check(/Parameters not supported by drive\s+Format terminated/.test(pc.screen()), 'FORMAT A: /1 on a 3.5" drive -> "Parameters not supported by drive"', pc.screen());
// write protected
pc.machine.ejectFloppy(); pc.run(50); pc.machine.insertFloppy(blankFloppy(), true); pc.run(50);
fmt('A:', 'FMTWP', 'F2');
check(/\r\n\r\n\rFormat terminated {51}\r\n/.test(out('FMTWP') ?? '') && exitCode(pc) === 4, 'write-protected: "Format terminated", exit 4', out('FMTWP'));
check(/Write protect error/.test(pc.screen()), '... "Write protect error" on the screen (STDERR)', pc.screen());
// no diskette
pc.machine.ejectFloppy(); pc.run(50);
fmt('A:', 'FMTNR', 'F2');
check(/Format terminated/.test(out('FMTNR') ?? '') && /Not ready/.test(pc.screen()), 'no diskette: "Not ready", "Format terminated"', pc.screen());
// two diskettes in one session: "Format another (Y/N)?" Y
pc.machine.insertFloppy(blankFloppy()); pc.run(50);
fmt('A:', 'FMT2X', 'F4');
const two = out('FMT2X') ?? '';
check((two.match(/Format complete/g) || []).length === 2 && /Format another \(Y\/N\)\?Y\r\r\n\r\nInsert new diskette/.test(two),
  'Format another: Y -> a second diskette', two.slice(-400));

// 6. the /S diskette boots (no hard disk): IO.SYS, ARMDOS.SYS, then its COMMAND.COM
{
  const pc2 = await boot({ rom: B('rom.bin'), fd: fdS });
  const ok = pc2.until(() => pc2.serial.includes('T:SHELL start'), { timeoutMs: 30000 });
  check(ok && /T:ARGV1 \/P/.test(pc2.serial), 'the FORMAT /S diskette boots ARM-DOS and runs its COMMAND.COM /P', pc2.screen() + pc2.serial);
}

// 7. FORMAT C: on the scratch hard disk
if (!quick) {
  pc.type('C:\\DOS\\FORMAT.COM C: /S\r');
  check(answer(pc, 'Enter current volume label for drive C: ', 'WRONG\r', 10000), 'FORMAT C: asks for the current label (DUTEST)');
  pc.until(() => /T:EXIT/.test(pc.serial.split('FORMAT.COM C: /S]').pop()), { timeoutMs: 10000 });
  check(/Invalid Volume ID\s+Format terminated/.test(pc.screen()) && exitCode(pc) === 4, 'wrong label -> "Invalid Volume ID", "Format terminated", exit 4', pc.screen());
  pc.type('C:\\DOS\\FORMAT.COM C: /S\r');
  answer(pc, 'Enter current volume label for drive C: ', 'dutest\r', 10000);
  check(answer(pc, 'Proceed with Format (Y/N)?', 'N\r', 10000), 'the WARNING, ALL DATA ON NON-REMOVABLE DISK prompt');
  pc.waitText('T>', { timeoutMs: 5000 }); pc.run(200);
  check(exitCode(pc) === 5, '"N" -> exit code 5, nothing written');
  const scr = pc.screen();
  check(/dutest\n\nWARNING, ALL DATA ON NON-REMOVABLE DISK\nDRIVE C: WILL BE LOST!\n.*oceed with Format \(Y\/N\)\?N/.test(scr), 'warning text exact', scr);
  pc.type('C:\\DOS\\FORMAT.COM C: /S /V:ARMDOS\r');
  answer(pc, 'Enter current volume label for drive C: ', 'DUTEST\r', 10000);
  answer(pc, 'Proceed with Format (Y/N)?', 'Y\r', 10000);
  let pct = 0;
  pc.until(() => { if (/percent of disk formatted/.test(pc.screen())) pct++; return /T:EXIT \S+ \d+ \d+\n$/.test(pc.serial); }, { timeoutMs: 300000, stepMs: 200 });
  pc.run(300);
  const s2 = pc.screen();
  check(pct > 0 && /Format complete\s*\nSystem transferred\n\n\n +33945600 bytes total disk space\n +\d+ bytes used by system\n +\d+ bytes available on disk\n\n +2048 bytes in each allocation unit\n +\d+ allocation units available on disk\n\nVolume Serial Number is [0-9A-F]{4}-[0-9A-F]{4}\n/.test(s2),
    'FORMAT C: /S /V:ARMDOS: percent counter, report, no "Format another"', s2);
  check(exitCode(pc) === 0, 'FORMAT C: exit code 0');
  const img = pc.hd;
  checkFsck(img, 'fmtc', 'C: after FORMAT: fsck.fat -n clean');
  checkMdir(img, ['-a', '::/'], (dir) => /Volume in drive : is ARMDOS/.test(dir) && /IO +SYS/.test(dir) && /ARMDOS +SYS/.test(dir) && /COMMAND +COM/.test(dir) && !/TSHELL|DOS +<DIR>/.test(dir),
    'C: now holds only the system files and the label');
  const b = Buffer.from(img.subarray(63 * 512, 64 * 512));
  check(b[0x24] === 0x80 && b.readUInt32LE(0x1C) === 63 && b.toString('latin1', 0x36, 0x3E) === 'FAT16   ' && b[0x0D] === 4,
    'C: boot record: drive 80h, 63 hidden sectors, FAT16, 2 KB clusters');
  save(img, 'formatted-c.img');
  const pc3 = await boot({ rom: B('rom.bin'), hd: img });
  check(pc3.until(() => pc3.serial.includes('T:SHELL start'), { timeoutMs: 30000 }), 'the formatted C: boots', pc3.screen());
  fs.rmSync(path.join(B('diskutil-test'), 'formatted-c.img'), { force: true });
}

// 8. with the real COMMAND.COM (when it has been built): FORMAT A: /S at the
//    C:\> prompt, and the diskette boots to COMMAND.COM's date prompt
if (fs.existsSync(B('COMMAND.COM'))) {
  fs.writeFileSync(path.join(OUT, 'AUTOEXEC.BAT'), '@ECHO OFF\r\nPROMPT $P$G\r\nPATH C:\\DOS\r\n');
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 }, { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM' }, { src: path.relative(ROOT, path.join(OUT, 'AUTOEXEC.BAT')) },
    { src: 'build/FORMAT.COM', dst: 'DOS\\' }, { src: 'build/CHKDSK.COM', dst: 'DOS\\' },
  ];
  const hdc = new Uint8Array(buildImage({ format: 'hd', sizeMB: 32, label: 'ARM-DOS', boot: { src: 'build/bootsect.bin' }, files, dirs: ['DOS'] }, ROOT).img);
  const p4 = await boot({ rom: B('rom.bin'), hd: hdc });
  check(p4.waitText('C:\\>', { timeoutMs: 30000 }), 'COMMAND.COM: C:\\> prompt');
  p4.machine.insertFloppy(blankFloppy()); p4.run(50);
  p4.type('FORMAT A: /S\r');
  answer(p4, 'press ENTER', '\r');
  answer(p4, 'Volume label', 'BOOTME\r', 120000);
  check(answer(p4, 'Format another', 'N\r'), 'COMMAND.COM: FORMAT A: /S typed at the prompt (PATH C:\\DOS)');
  p4.run(1500);
  const fd = new Uint8Array(p4.machine.floppy);
  const cc = readFile(fd, '\\COMMAND.COM');
  check(cc !== null && Buffer.from(cc, 'latin1').equals(fs.readFileSync(B('COMMAND.COM'))), 'the diskette holds the real COMMAND.COM');
  checkFsck(fd, 'realcmd', 'that diskette: fsck.fat -n clean');
  const p5 = await boot({ rom: B('rom.bin'), fd });
  check(p5.waitText('Current date is', { timeoutMs: 30000 }), 'booted from the FORMAT /S diskette: COMMAND.COM asks for the date', p5.screen());
}

process.exit(summary() ? 1 : 0);
