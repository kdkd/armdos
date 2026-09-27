#!/usr/bin/env node
// apps/cdrom/tests/cdex.mjs - ARMCD.SYS + ARMCDEX.EXE on the Multimedia Sampler '93 disc,
// with the real COMMAND.COM: the driver's banner, ARMCDEX's banner and errors, DIR/TYPE/
// COPY/CD on D: through the kernel's redirector callouts, programs and a batch file run from
// the CD, the INT 2Fh AX=15xxh API (CDAPI.EXE), "Not ready" with the tray open, a media change.
//   node apps/cdrom/tests/cdex.mjs          (make cdrom-test)
import fs from 'node:fs';
import { startPC, check, failed, B, readHdFile, lastLine } from './harness.mjs';
import { loadDisc } from '../tools/disc-node.mjs';
import { IsoReader, buildIso } from '../tools/iso9660.mjs';
import { CdDisc } from '../../../emu/dev/atapi.mjs';

const DISC = B('cdrom/sampler93');
const disc = loadDisc(DISC);
const iso = new IsoReader(new Uint8Array(fs.readFileSync(`${DISC}/data.iso`)));
const pc = await startPC({
  name: 'cdex', shell: 'command', lastdrive: 'E', cdrom: disc,
  files: [{ src: 'build/ARMCD.SYS', dst: 'DOS\\' }, { src: 'build/ARMCDEX.EXE', dst: 'DOS\\' },
          { src: 'build/cdrom-test/CDAPI.EXE', dst: 'DOS\\' }, ],
  extraConfig: 'DEVICE=C:\\DOS\\ARMCD.SYS /D:ARMCD001\n',
});
const scr = () => pc.screen();
const cls = () => pc.cmd('CLS');
check(pc.bootOk, 'booted to C:\\>');
let s = scr();
check(s.includes('ARM-PC CD-ROM Device Driver  Version 1.10') && s.includes('Unit 0: ARM-PC CD-ROM DRIVE  1.00  (secondary IDE, master)')
  && s.includes('Device name: ARMCD001'), 'ARMCD.SYS banner: the drive found by IDENTIFY PACKET DEVICE');

// ---------------------------------------------------------------- ARMCDEX
cls(); pc.cmd('DIR D:');
check(pc.hasText('Invalid drive specification'), 'before ARMCDEX: DIR D: -> Invalid drive specification');
cls(); pc.cmd('ARMCDEX /D:NOSUCH');
s = scr();
check(s.includes("Device driver not found: 'NOSUCH'.") && s.includes('No valid CDROM device drivers selected'), 'ARMCDEX /D:NOSUCH: not found, none selected');
cls(); pc.cmd('ARMCDEX /D:ARMCD001 /M:8 /V');
s = scr();
console.log(s.trimEnd());
check(s.includes('ARM CD-ROM Extensions Version 2.21') && s.includes('Copyright (C) Europa Micro Systems 1986-1993. All rights reserved.'), 'ARMCDEX banner');
check(/^        Drive D: = Driver ARMCD001 unit 0$/m.test(s), '"Drive D: = Driver ARMCD001 unit 0" (the next free letter)');
check(/Sector buffers  : 8 x 2048 bytes/.test(s), '/M:8 -> 8 sector buffers');
cls(); pc.cmd('ARMCDEX /D:ARMCD001');
check(pc.hasText('ARM CD-ROM Extensions already installed'), 'a second ARMCDEX refuses');

// ---------------------------------------------------------------- DIR, TYPE, COPY, CD
cls(); pc.cmd('VOL D:');
check(/Volume in drive D is SAMPLER93/.test(scr()), 'VOL D: -> SAMPLER93');
cls(); pc.cmd('DIR D:\\*.TXT');
s = scr();
console.log(s.trimEnd());
check(/Volume in drive D is SAMPLER93/.test(s), 'DIR D: volume label SAMPLER93');
check(/Directory of\s+D:\\/.test(s), 'Directory of D:\\');
const readme = iso.lookup('README.TXT');
check(new RegExp(`README\\s+TXT\\s+${readme.size}\\s+0?6-17-93\\s+12:00p`).test(s), `README.TXT ${readme.size} bytes, 6-17-93 12:00p`);
const nroot = iso.list('\\').length;
cls(); pc.cmd('DIR D:');
s = scr();
check(/ZORK\s+<DIR>/.test(s) && /PICTURES\s+<DIR>/.test(s), 'directories listed');
check(new RegExp(`${nroot} File\\(s\\)\\s+0 bytes free`).test(s), `${nroot} File(s), 0 bytes free`);
check(!/Serial Number/.test(s), 'no volume serial number (as DOS 4 with the CD-ROM extensions)');
await pc.shot('cdex-dir.png');

cls(); pc.cmd('DIR D:\\ZORK');
s = scr();
const z1 = iso.lookup('ZORK\\ZORK1.DAT');
check(/Directory of\s+D:\\ZORK/.test(s) && new RegExp(`ZORK1\\s+DAT\\s+${z1.size}`).test(s) && /\.\s+<DIR>/.test(s), 'DIR D:\\ZORK (subdirectory, . and ..)');
cls(); pc.cmd('DIR D:\\*.BAT /W');
check(/MENU\.BAT|MENU\s+BAT/.test(scr()) && !/README/.test(scr()), 'DIR D:\\*.BAT /W (wildcards)');

cls(); pc.cmd('TYPE D:\\MUSIC\\TRACKS.TXT');
s = scr();
check(s.includes('Track  2  Cipher') && s.includes('Kevin MacLeod'), 'TYPE D:\\MUSIC\\TRACKS.TXT');

cls(); pc.cmd('MD C:\\ZORKCD');
pc.cmd('COPY D:\\ZORK\\*.* C:\\ZORKCD', { timeoutMs: 120000 });
s = scr();
const nz = iso.list('ZORK').length;
check(new RegExp(`${nz} File\\(s\\) copied`).test(s), `COPY D:\\ZORK\\*.* C:\\ZORKCD: ${nz} files copied`);
let same = true;
for (const e of iso.list('ZORK')) {
  const a = readHdFile(pc, `ZORKCD\\${e.name}`), b = Buffer.from(iso.readFile(`ZORK\\${e.name}`)).toString('latin1');
  if (a !== b) { same = false; console.log(`     differs: ${e.name}`); }
}
check(same, 'the copies are byte-identical to the ISO files (multi-sector reads, partial sectors)');

cls(); pc.cmd('D:');
check(lastLine(pc).startsWith('D:\\>'), 'D: -> D:\\>');
pc.cmd('CD TEXTS');
check(lastLine(pc).startsWith('D:\\TEXTS>'), 'CD TEXTS');
cls(); pc.cmd('DIR');
check(/Directory of\s+D:\\TEXTS/.test(scr()) && /RAVEN\s+TXT/.test(scr()) && /Volume in drive D is SAMPLER93/.test(scr()), 'DIR in D:\\TEXTS');
cls(); pc.cmd('TYPE RAVEN.TXT');
check(pc.hasText('Nevermore'), 'TYPE RAVEN.TXT (relative path on D:)');
pc.cmd('CD ..');
check(lastLine(pc).startsWith('D:\\>'), 'CD ..');
pc.cmd('CD NOWHERE');
check(pc.hasText('Invalid directory'), 'CD NOWHERE -> Invalid directory');
cls(); pc.cmd('DEL README.TXT');
check(pc.hasText('Access denied'), 'DEL on the CD -> Access denied');
cls(); pc.cmd('COPY C:\\CONFIG.SYS D:\\');
check(!/1 File\(s\) copied/.test(scr()), 'COPY to the CD fails');

// a batch file and a program from the CD
cls(); pc.type('MENU\r');
check(pc.waitText('MULTIMEDIA  SAMPLER', { timeoutMs: 20000 }), 'MENU.BAT runs from the CD');
pc.until(() => /^D:\\>/.test(lastLine(pc)), { timeoutMs: 20000 });
await pc.shot('cdex-menu.png');
cls(); pc.type('CD ZORK\rZORK1\r');
check(pc.waitText('West of House', { timeoutMs: 60000 }), 'D:\\ZORK\\ZORK1.EXE runs from the CD (EXEC through the redirector)');
await pc.shot('cdex-zork.png');
pc.type('quit\r'); pc.run(1500); pc.type('y\r');
check(pc.until(() => /^D:\\ZORK>/.test(lastLine(pc)), { timeoutMs: 20000 }), 'back from ZORK');
pc.cmd('CD \\'); pc.cmd('C:');

// ---------------------------------------------------------------- the INT 2Fh AX=15xxh API
cls(); pc.cmd('CDAPI > C:\\API.TXT', { timeoutMs: 60000 });
const api = readHdFile(pc, 'API.TXT') || '';
console.log(api.trimEnd());
const has = (re, what) => check(re.test(api), what);
has(/^1500h: BX=1 CX=3 \(D:\)/m, '1500h: one drive, D:');
has(/^150Ch: version 2\.21/m, '150Ch: version 2.21');
has(/^150Bh: AX=5AD8 BX=ADAD/m, '150Bh D: is a CD drive (ADADh signature)');
has(/^150Bh C: AX=0000/m, '150Bh C: is not');
has(/^150Dh: D:/m, '150Dh drive letters');
has(/^1501h: unit 0, device ARMCD001/m, '1501h device list -> the ARMCD001 header');
has(/^1502h: copyright file "COPYRGHT\.TXT;1"/m, '1502h copyright file name (the PVD field as recorded, with ;1)');
has(/^1503h: abstract file "ABSTRACT\.TXT;1"/m, '1503h abstract file name');
has(/^1504h: bibliographic file "BIBLIO\.TXT;1"/m, '1504h bibliographic file name');
has(/^1505h: AX=1 type 1 "CD001" volume "SAMPLER93"/m, '1505h reads the PVD');
has(/^1505h DX=1: AX=255 type 255/m, '1505h DX=1: the terminator');
has(/^1508h: ok "CD001" "CD001"/m, '1508h absolute read of sectors 16-17');
has(/^1509h: CF AX=5/m, '1509h write -> access denied');
has(new RegExp(`^150Fh: ok AX=1 len \\d+ extent ${readme.extent} size ${readme.size} flags 00 name README\\.TXT;1`, 'm'), '150Fh directory entry of \\README.TXT');
has(new RegExp(`^150Fh ZORK: ok size ${z1.size} name ZORK1\\.DAT;1`, 'm'), '150Fh in a subdirectory');
has(/^150Fh missing: CF AX=2/m, '150Fh missing file -> error 2');
has(/^IOCTL 6: status 0100 device status 00000316/m, 'IOCTL 6 device status: closed, unlocked, raw, audio, Red Book, channel control');
has(/^IOCTL 7: 2048/m, 'IOCTL 7 sector size');
has(new RegExp(`^IOCTL 8: ${disc.leadout} sectors`, 'm'), `IOCTL 8 volume size ${disc.leadout}`);
// Red Book MM:SS.FF of frame f (LBA + 150), as a regexp. The lead-out comes from the disc
// as built: the data track's size follows the programs on it, i.e. the compiler.
const msf = (f) => `${String(Math.floor(f / 4500)).padStart(2, '0')}:${String(Math.floor(f / 75) % 60).padStart(2, '0')}\\.${String(f % 75).padStart(2, '0')}`;
has(new RegExp(`^IOCTL 10: tracks 1-5 lead-out ${msf(disc.leadout + 150)}`, 'm'), `IOCTL 10 audio disk info: tracks 1-5, lead-out ${msf(disc.leadout + 150).replace('\\', '')}`);
has(/^IOCTL 11: track 1 at 00:02\.00 ctl 41/m, 'IOCTL 11 track 1: data (control 4)');
const t2 = disc.track(2).start + 150;
has(new RegExp(`^IOCTL 11: track 2 at ${msf(t2)} ctl 01`, 'm'), `IOCTL 11 track 2 audio at ${msf(t2).replace('\\', '')}`);
has(/^IOCTL 14: status 0100 UPC 00199306170000/m, 'IOCTL 14 UPC/EAN (the disc\'s MCN)');
has(/^IOCTL 4: in0 0 vol0 255 in1 1 vol1 255/m, 'IOCTL 4 audio channel info (MODE SENSE page 0Eh)');
has(/^Q: status 0100 busy 0/m, 'Q channel: not playing');

cls(); pc.cmd('CDAPI PLAY 3');
check(pc.hasText('PLAY track 3') && pc.m.cdrom.state().playing, '1510h PLAY AUDIO track 3: the drive plays');
for (let i = 0; i < 40; i++) { pc.run(50); await new Promise((r) => setTimeout(r, 0)); }   // let the WAV "decode"
cls(); pc.cmd('CDAPI Q');
const q = scr().match(/Q: status (\w+) busy (\d) track (\w+) index (\d+) rel (\d+):(\d+)/);
check(q && q[1] === '0300' && q[2] === '1' && q[3] === '03', 'Q channel while playing: BUSY, track 03 (BCD)');
check(q && +q[5] * 60 + +q[6] >= 1 && +q[5] * 60 + +q[6] <= 4, `position advances in emulated time (rel ${q && q[5]}:${q && q[6]})`);

// ---------------------------------------------------------------- resident size
// the MCB owned by ARMCDEX (DOS 4 keeps the program name at +8 of its MCB)
const mcbs = [];
for (let seg = 0x60; seg < 0xA000; seg++) {
  const m8 = pc.m.cpu.m8, a = seg << 4;
  if ((m8[a] === 0x4D || m8[a] === 0x5A) && String.fromCharCode(...m8.subarray(a + 8, a + 15)) === 'ARMCDEX')
    mcbs.push({ seg, owner: m8[a + 1] | (m8[a + 2] << 8), size: (m8[a + 3] | (m8[a + 4] << 8)) * 16 });
}
const res = mcbs.find((x) => x.owner === x.seg + 1);
console.log(`     ARMCDEX MCBs: ${JSON.stringify(mcbs)}`);
check(res && res.size > 8 * 2048 && res.size < 40000, `ARMCDEX resident: ${res && res.size} bytes (code + 8 sector buffers)`);

// ---------------------------------------------------------------- not ready, media change
pc.m.cdrom.eject();
check(pc.m.cdrom.state().trayOpen && !pc.m.cdrom.state().playing, 'eject: tray open, audio stops');
cls(); pc.type('DIR D:\r');
check(pc.waitText('Abort, Retry, Fail?', { timeoutMs: 20000 }), 'DIR D: with the tray open -> Abort, Retry, Fail?');
check(/Not ready reading drive D/.test(scr()), '"Not ready reading drive D"');
await pc.shot('cdex-notready.png');
// a different disc goes in, then Retry
const other = buildIso({ volumeId: 'OTHERDISC', date: '1994-03-01 09:30:00',
  files: [{ path: 'HELLO.TXT', data: new TextEncoder().encode('Hello from the other disc.\r\n') }] });
pc.m.cdrom.setDiscInTray(new CdDisc({ title: 'Other', tracks: [{ number: 1, type: 'data', sectors: other.length / 2048 }] }, { data: other }));
pc.m.cdrom.closeTray();
pc.run(300);
pc.type('R');
const okRetry = pc.until(() => /Volume in drive D is OTHERDISC/.test(scr()) && /^C:\\>/.test(lastLine(pc)), { timeoutMs: 20000 });
s = scr();
if (!okRetry) console.log(s);
check(okRetry, 'Retry after a new disc: DIR shows its volume OTHERDISC (media change seen, cache dropped)');
check(/HELLO\s+TXT\s+28\s+0?3-01-94\s+9:30a/.test(s), 'the new disc\'s file');
cls(); pc.cmd('TYPE D:\\HELLO.TXT');
check(pc.hasText('Hello from the other disc.'), 'TYPE from the new disc');
// and back
pc.m.cdrom.insert(disc);
pc.run(200);
cls(); pc.cmd('VOL D:');
if (!/Volume in drive D is SAMPLER93/.test(scr())) console.log(scr());
check(/Volume in drive D is SAMPLER93/.test(scr()), 'the Sampler back in: SAMPLER93 again');
pc.m.cdrom.eject();
cls(); pc.type('TYPE D:\\README.TXT\r');
check(pc.waitText('Abort, Retry, Fail?', { timeoutMs: 20000 }), 'TYPE with no disc -> Abort, Retry, Fail?');
pc.type('F');
check(pc.until(() => /^C:\\>/.test(lastLine(pc)), { timeoutMs: 10000 }), 'Fail -> back to the prompt');

cls(); pc.cmd('DIR C:\\');
check(/Volume in drive C is ARM-DOS/.test(scr()) && /bytes free/.test(scr()), 'C: unaffected');
check(pc.faults.length === 0, 'no CPU faults');
console.log(failed() ? `${failed()} check(s) FAILED` : 'cdex: all checks passed');
process.exit(failed() ? 1 : 0);
