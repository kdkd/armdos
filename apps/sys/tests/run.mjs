#!/usr/bin/env node
// apps/sys/tests/run.mjs - SYS: onto a used data diskette (4.00 makes room:
// root slots 0/1 and the clusters IO.SYS needs are moved), the result
// (fsck.fat -n, files intact, boot record, BPB/serial/label kept) and that
// it boots; "No room", parameter errors.
import fs from 'node:fs';
import { boot } from '../../../emu/testkit.mjs';
import { B, hdImage, floppyImage, start, cmd, readFile, checkFsck, checkMdir, check, summary } from '../../format/tests/lib.mjs';

const pat = (n, k) => Buffer.from(Array.from({ length: n }, (_, i) => (i * 7 + k) & 255));
const files = {
  'A.TXT': pat(100, 1), 'B.TXT': pat(1500, 2), 'C.TXT': pat(5000, 3), 'D.TXT': pat(512, 4),
  'SUB\\E.TXT': pat(3000, 5), 'SUB\\DEEP\\F.TXT': pat(700, 6), 'COMMAND.COM': fs.readFileSync(B('ktest/TSHELL.EXE')),
};
const used = floppyImage('DATADISK', files, { dirs: ['SUB', 'SUB\\DEEP'] });

const hd = hdImage('sys');
const pc = await start(hd, used);
const screenAfter = (line) => { pc.type(line + '\r'); pc.run(1500); return pc.screen(); };

// 1. SYS A: from C: (the current drive)
cmd(pc, 'C:\\DOS\\SYS.COM A:');
let scr = pc.screen();
check(/SYS.COM A:\nSystem transferred\n/.test(scr), 'SYS A: -> "System transferred"', scr);
const img = new Uint8Array(pc.machine.floppy);
checkFsck(img, 'sys1', 'fsck.fat -n: clean');
const root = Buffer.from(img.subarray(19 * 512, 20 * 512));
check(root.toString('latin1', 0, 11) === 'IO      SYS' && root.readUInt16LE(26) === 2 && (root[11] & 7) === 7 &&
  root.toString('latin1', 32, 43) === 'ARMDOS  SYS' && (root[43] & 7) === 7, 'root slots 0/1: IO.SYS (cluster 2, RHS), ARMDOS.SYS (RHS)');
{
  const io = fs.readFileSync(B('IO.SYS')), n = Math.ceil(io.length / 512);
  let contiguous = true;
  const fat = (c) => { const o = 512 + ((c * 3) >> 1), v = img[o] | (img[o + 1] << 8); return c & 1 ? v >> 4 : v & 0xFFF; };
  for (let c = 2; c < 1 + n; c++) if (fat(c) !== c + 1) contiguous = false;
  check(contiguous && fat(1 + n) >= 0xFF8, `IO.SYS contiguous from cluster 2 (${n} clusters), as the ARM boot sector needs`);
  check(Buffer.from(readFile(img, '\\IO.SYS'), 'latin1').equals(io) && Buffer.from(readFile(img, '\\ARMDOS.SYS'), 'latin1').equals(fs.readFileSync(B('ARMDOS.SYS'))),
    'IO.SYS and ARMDOS.SYS copied intact');
}
let intact = true;
for (const [n, c] of Object.entries(files)) { const got = readFile(img, '\\' + n); if (!got || !Buffer.from(got, 'latin1').equals(c)) { intact = false; console.log('     differs: ' + n); } }
check(intact, 'every file (moved out of the way) is intact, subdirectories too');
checkMdir(img, ['::/'], (dir) => /Volume in drive : is DATADISK/.test(dir) && /Serial Number is 1234-5678/.test(dir), 'label and serial number kept');
const b = Buffer.from(img.subarray(0, 512));
check(b.readUInt32LE(0) === 0xEA00000E && Buffer.compare(b.subarray(0x40, 0x1FE), fs.readFileSync(B('bootsect.bin')).subarray(0x40, 0x1FE)) === 0 &&
  b.toString('latin1', 0x2B, 0x36) === 'DATADISK   ' && b.readUInt16LE(0x13) === 2880, 'boot record: ARM code, BPB and label kept');
{
  const pc2 = await boot({ rom: B('rom.bin'), fd: img });
  check(pc2.until(() => pc2.serial.includes('T:SHELL start'), { timeoutMs: 30000 }), 'the diskette boots (IO.SYS, ARMDOS.SYS, its COMMAND.COM)', pc2.screen());
}

// 2. SYS again onto the system diskette (replaces the files in place)
cmd(pc, 'C:\\DOS\\SYS.COM C:\\ A:');
check(/System transferred\n/.test(pc.screen().split('SYS.COM C:\\ A:').pop()), 'SYS C:\\ A: (explicit source) onto a system diskette', pc.screen());
checkFsck(pc.machine.floppy, 'sys2', 'fsck.fat -n: clean');

// 3. a full diskette
{
  const big = {};
  for (let i = 0; i < 7; i++) big[`BIG${i}.DAT`] = Buffer.alloc(200 * 1024, i);
  pc.machine.ejectFloppy(); pc.run(50); pc.machine.insertFloppy(floppyImage('FULL', big)); pc.run(50);
  cmd(pc, 'C:\\DOS\\SYS.COM A:');
  check(/SYS.COM A:\nNo room for system on destination disk\n/.test(pc.screen()), 'full diskette -> "No room for system on destination disk"', pc.screen());
  checkFsck(pc.machine.floppy, 'sys3', 'and it is left intact (fsck.fat -n)');
}

// 4. parameters
scr = screenAfter('C:\\DOS\\SYS.COM');
check(/SYS.COM\nRequired parameter missing\n/.test(scr), 'SYS -> "Required parameter missing"', scr);
scr = screenAfter('C:\\DOS\\SYS.COM C:');
check(/SYS.COM C:\nCannot specify default drive\n/.test(scr), 'SYS C: (the current drive) -> "Cannot specify default drive"', scr);
scr = screenAfter('C:\\DOS\\SYS.COM C:\\NOPE A:');
check(/Invalid path or System files not found\n/.test(scr), 'SYS C:\\NOPE A: -> "Invalid path or System files not found"', scr);
scr = screenAfter('C:\\DOS\\SYS.COM Q:');
check(/SYS.COM Q:\nInvalid drive specification\n/.test(scr), 'SYS Q: -> "Invalid drive specification"', scr);
scr = screenAfter('C:\\DOS\\SYS.COM A: B: C:');
check(/Too many parameters - C:\n/.test(scr), 'SYS A: B: C: -> "Too many parameters - C:"', scr);

process.exit(summary() ? 1 : 0);
