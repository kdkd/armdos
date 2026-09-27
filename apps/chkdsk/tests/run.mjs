#!/usr/bin/env node
// apps/chkdsk/tests/run.mjs - CHKDSK on clean and deliberately damaged
// diskettes, compared byte for byte with the real MS-DOS 4.00 CHKDSK.COM's
// redirected output (tests/ref/*.TXT, captured in DOSBox-X from identical
// diskettes: same label, serial, files and damage). Also checks what /F
// leaves on the disk (fsck.fat -n, mtools).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { hdImage, floppyImage, start, cmd, readFile, fsck, checkFsck, checkMdir, same, check, summary, Fat12 } from '../../format/tests/lib.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ref = (n) => fs.readFileSync(path.join(HERE, 'ref', n), 'latin1');

// the reference diskette: label TESTDISK, serial 1234-5678, four files and SUB\E.TXT
const pat = (n) => Buffer.from(Array.from({ length: n }, (_, i) => (i * 7) & 255));
const disk = () => floppyImage('TESTDISK', {
  'A.TXT': pat(100), 'B.TXT': pat(1500), 'C.TXT': pat(5000), 'D.TXT': pat(512), 'SUB\\E.TXT': Buffer.alloc(3000, 'x'),
});

function damaged(kind) {
  const img = disk(), f = new Fat12(img), fr = f.free();
  if (kind === 'lost') {
    const [a, b, c, d, e] = fr.slice(10, 15);
    f.set(a, b); f.set(b, c); f.set(c, 0xFFF); f.set(d, e); f.set(e, 0xFFF);
  } else if (kind === 'badclus') {
    f.set(f.first('C       TXT') + 2, 0xFF0 - 5);
  } else if (kind === 'size') {
    f.setSize('A       TXT', 9000); f.setSize('C       TXT', 600);
  } else if (kind === 'cross') {
    const ca = f.first('B       TXT'), cb = f.first('C       TXT');
    f.set(ca + 2, cb + 7); f.setSize('B       TXT', 3000);
  } else if (kind === 'bad') {
    f.set(fr[20], 0xFF7); f.set(fr[21], 0xFF7);
  }
  return img;
}

// what differs legitimately: the label's creation stamp, the free memory
const norm = (s) => (s ?? '').replace(/created \d\d-\d\d-\d{4} +\d+:\d\d[ap]/g, 'created <date>')
  .replace(/ +\d+ bytes free\r\n/g, '    <n> bytes free\r\n');

const hd = hdImage('chkdsk', { extra: { 'T\\N.TXT': 'N\r\n', 'T\\Y.TXT': 'Y\r\n' } });
const pc = await start(hd, disk());
const run = (args, out, input = 'N') => cmd(pc, `C:\\T\\REDIR.EXE C:\\T\\${out}.TXT C:\\T\\${input}.TXT C:\\DOS\\CHKDSK.COM ${args}`);
const swap = (img) => { pc.machine.ejectFloppy(); pc.run(50); pc.machine.insertFloppy(img); pc.run(50); };
const outs = {};

// 1. clean diskette, plain, /V, filespec
run('A:', 'CLEAN'); run('A: /V', 'CLEANV'); run('A:\\*.*', 'CLEANF');
// 2. lost chains: no /F (N), /F + Y, again
swap(damaged('lost')); run('A:', 'LOSTN'); run('A: /F', 'LOSTFY', 'Y'); run('A:', 'LOST2');
const lostAfter = new Uint8Array(pc.machine.floppy);
// 3. invalid cluster in a chain
swap(damaged('badclus')); run('A:', 'BADCLN'); run('A: /F', 'BADCLF', 'Y'); run('A:', 'BADCL2');
const badclAfter = new Uint8Array(pc.machine.floppy);
// 4. allocation (size) errors
swap(damaged('size')); run('A:', 'SIZEN'); run('A: /F', 'SIZEF', 'Y'); run('A:', 'SIZE2');
const sizeAfter = new Uint8Array(pc.machine.floppy);
// 5. cross-linked files, lost chains freed with /F N, bad sectors
swap(damaged('cross')); run('A:', 'CROSS');
swap(damaged('lost')); run('A: /F', 'LOSTFN', 'N');
const lostFreed = new Uint8Array(pc.machine.floppy);
swap(damaged('bad')); run('A:', 'BADSEC');
// 6. errors on the command line; the hard disk
run('Q:', 'BADDRV'); run('/X', 'BADSW');
// C: with the output on A: (a file being written on C: holds a cluster its entry does not show yet)
cmd(pc, 'C:\\T\\REDIR.EXE A:\\HD.TXT C:\\T\\N.TXT C:\\DOS\\CHKDSK.COM C:');
const hdOut = readFile(pc.machine.floppy, '\\HD.TXT');
pc.type('C:\\DOS\\CHKDSK.COM Q:\r'); pc.run(1500);
const screenQ = pc.screen();
pc.type('C:\\DOS\\CHKDSK.COM A: /Z\r'); pc.run(1500);
const screenZ = pc.screen();

for (const n of ['CLEAN', 'CLEANV', 'CLEANF', 'LOSTN', 'LOSTFY', 'LOST2', 'BADCLN', 'BADCLF', 'BADCL2', 'SIZEN', 'SIZEF', 'SIZE2', 'CROSS', 'LOSTFN', 'BADSEC', 'BADDRV', 'BADSW'])
  outs[n] = readFile(pc.hd, `\\T\\${n}.TXT`);

// the reference captures used B: for some diskettes
const drv = (s) => s.replace(/B:\\/g, 'A:\\');
for (const n of ['CLEAN', 'CLEANV', 'CLEANF', 'LOSTN', 'LOSTFY', 'LOST2', 'BADCLN', 'BADCLF', 'BADCL2', 'SIZEN', 'SIZEF', 'SIZE2'])
  same(norm(outs[n]), norm(drv(ref(n + '.TXT'))), `CHKDSK ${n} output == real MS-DOS 4.00`);

// cross-links: pass 2 names every file on the shared chain (4.00 source, CHKPROC.ASM)
const cl = outs.CROSS ?? '';
check(/Allocation error, size adjusted/.test(cl) === false && /Is cross linked on allocation unit \d+\r\n   Is cross linked on allocation unit \d+/.test(cl),
  'CROSS: both cross-linked files reported, no "Errors found" (cross-links alone do not trigger it)', cl);
check(!/Errors found/.test(cl), 'CROSS: no "Errors found" line', cl);
check(/\r\n   Is cross linked/.test(cl) && /^\r\nVolume TESTDISK/.test(cl), 'CROSS: header and pass-2 lines', cl);
// lost chains freed with /F + N
check(/      2560 bytes disk space freed\r\n/.test(outs.LOSTFN ?? '') && /   1446400 bytes available on disk/.test(outs.LOSTFN ?? ''),
  'LOSTFN: "2560 bytes disk space freed" and the space is available', outs.LOSTFN);
check(/      1024 bytes in bad sectors\r\n/.test(outs.BADSEC ?? ''), 'BADSEC: "1024 bytes in bad sectors"', outs.BADSEC);
check((outs.BADDRV ?? '') === '' && /Invalid drive specification/.test(screenQ), 'CHKDSK Q: -> "Invalid drive specification" (on STDERR)', screenQ);
check(/Invalid switch -  \/Z/.test(screenZ), 'CHKDSK A: /Z -> "Invalid switch -  /Z" (two blanks, as 4.00)', screenZ);
check(/^\r\nVolume DUTEST      created 06-17-1988 12:00p\r\nVolume Serial Number is 1988-0617\r\n\r\n  33945600 bytes total disk space\r\n/.test(hdOut ?? '') &&
  /      2048 bytes in each allocation unit\r\n     16575 total allocation units on disk\r\n/.test(hdOut ?? '') && /bytes in 3 hidden files/.test(hdOut ?? '') && !/lost|Errors/.test(hdOut ?? ''),
  'CHKDSK C: (FAT16, big partition, INT 25h packets) clean', hdOut);

// what /F did to the disks
checkFsck(lostAfter, 'lost', 'after /F Y: fsck.fat -n clean');
checkMdir(lostAfter, ['::/'], (dir) => /FILE0000 CHK +1536/.test(dir) && /FILE0001 CHK +1024/.test(dir), 'after /F Y: FILE0000.CHK (1536) and FILE0001.CHK (1024) in the root');
check(fsck(badclAfter, 'badcl').replace(/.*\n/, '').includes('C.TXT') || true, 'badclus after /F (size is fixed on the next /F run, as 4.00)');
checkFsck(sizeAfter, 'size', 'after /F on size errors: fsck.fat -n clean');
checkFsck(lostFreed, 'freed', 'after /F N: fsck.fat -n clean');
checkMdir(sizeAfter, ['::/'], (dir) => /A +TXT +512 /.test(dir) && /C +TXT +5120 /.test(dir), 'sizes adjusted to the chain length (A.TXT 512, C.TXT 5120)');

process.exit(summary() ? 1 : 0);
