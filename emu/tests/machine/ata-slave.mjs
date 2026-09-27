#!/usr/bin/env node
// Two hard disks on the primary IDE channel (dev/ata.mjs ATAChannel): drive/head bit 4 picks
// the slave, which reads and writes its own image; the master is untouched; IDENTIFY answers
// for each; with no slave, selecting it reads status 0 (no drive), as a real channel does.
import { Machine } from '../../machine.mjs';

let fails = 0, passes = 0;
const eq = (name, got, want) => {
  const ok = JSON.stringify(got) === JSON.stringify(want);
  if (ok) passes++; else { fails++; console.log(`FAIL ${name}: got ${JSON.stringify(got)} want ${JSON.stringify(want)}`); }
};
const disk = (tag, n = 1008 * 16) => { const d = new Uint8Array(n * 512); for (let s = 0; s < n; s++) { d[s * 512] = tag; d[s * 512 + 1] = s & 0xFF; } return d; };
const sel = (m, unit, lba, n) => { m.out8(0x1F6, 0xE0 | (unit << 4) | ((lba >> 24) & 15)); m.out8(0x1F2, n); m.out8(0x1F3, lba & 255); m.out8(0x1F4, (lba >> 8) & 255); m.out8(0x1F5, (lba >> 16) & 255); };
const word0 = (m) => m.read(0x100001F0, 2);
const drain = (m, words) => { for (let i = 0; i < words; i++) m.read(0x100001F0, 2); };

{
  const hd = disk(0xC0), hd2 = disk(0xD0, 1008 * 32), writes = [], acts = [];
  const m = new Machine({ jit: false, hd, hd2, onDiskWrite: (d, l) => writes.push([d, l]), onDiskActivity: (d) => acts.push(d) });
  m.cpu.halted = 1; m.cpu.i = 1;
  sel(m, 1, 7, 1); m.out8(0x1F7, 0x20);
  eq('slave: read sector 7 of its own image', word0(m), 0xD0 | (7 << 8));
  drain(m, 255);
  sel(m, 0, 7, 1); m.out8(0x1F7, 0x20);
  eq('master: sector 7 of C:', word0(m), 0xC0 | (7 << 8));
  drain(m, 255);
  sel(m, 1, 9, 1); m.out8(0x1F7, 0x30);
  for (let i = 0; i < 256; i++) m.write(0x100001F0, 2, 0xABCD);
  eq('slave: write lands on D:', [hd2[9 * 512], hd2[9 * 512 + 1]], [0xCD, 0xAB]);
  eq('master untouched', [hd[9 * 512], hd[9 * 512 + 1]], [0xC0, 9]);
  eq('disk activity and writes reported as drive 81h', [acts.slice(-1)[0], writes.slice(-1)[0]], [0x81, [0x81, 9]]);
  sel(m, 1, 0, 1); m.out8(0x1F7, 0xEC);
  const id = []; for (let i = 0; i < 256; i++) id.push(m.read(0x100001F0, 2));
  eq('slave IDENTIFY: its own size', id[60] | (id[61] << 16), 1008 * 32);
  sel(m, 0, 0, 1); m.out8(0x1F7, 0xEC);
  const id0 = []; for (let i = 0; i < 256; i++) id0.push(m.read(0x100001F0, 2));
  eq('master IDENTIFY: its own size', id0[60] | (id0[61] << 16), 1008 * 16);
}
{
  const m = new Machine({ jit: false, hd: disk(0xC0) });
  m.cpu.halted = 1; m.cpu.i = 1;
  m.out8(0x1F6, 0xB0);
  eq('no slave: its status reads 0', m.in8(0x1F7), 0);
  m.out8(0x1F7, 0xEC);
  eq('no slave: IDENTIFY does nothing', m.in8(0x1F7), 0);
  m.out8(0x1F6, 0xA0);
  eq('the master still answers', m.in8(0x1F7) & 0x40, 0x40);
}
console.log(`ata-slave: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
