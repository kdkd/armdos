#!/usr/bin/env node
// The ATA device's streaming sector source (dev/ata.mjs): reads of sectors the
// source doesn't have keep BSY until fetch() resolves; failures give ERR/UNC;
// writes are reported; new commands and SRST cancel a wait.
import { Machine } from '../../machine.mjs';

let fails = 0, passes = 0;
const eq = (name, got, want) => {
  const ok = JSON.stringify(got) === JSON.stringify(want);
  if (ok) passes++; else { fails++; console.log(`FAIL ${name}: got ${JSON.stringify(got)} want ${JSON.stringify(want)}`); }
};
const tick = () => new Promise((r) => setTimeout(r, 0));

function mk() {
  const hd = new Uint8Array(1008 * 512 * 2);
  for (let s = 0; s < hd.length / 512; s++) hd[s * 512] = s & 0xFF, hd[s * 512 + 1] = s >> 8;
  const acts = [];
  const m = new Machine({ jit: false, hd, onDiskActivity: (d, l, n) => acts.push([l, n]) });
  m.cpu.halted = 1; m.cpu.i = 1;
  const src = { have: new Set([0, 1, 2]), calls: [], written: [], pending: null,
    ready(l, n) { for (let k = l; k < l + n; k++) if (!this.have.has(k)) return false; return true; },
    fetch(l, n) { this.calls.push([l, n]); return new Promise((res) => { this.pending = (ok) => { if (ok) for (let k = l; k < l + n; k++) this.have.add(k); res(ok); }; }); },
    wrote(l) { this.written.push(l); } };
  m.ata.source = src;
  return { m, src, acts, hd };
}
const read = (m, lba, n) => { m.out8(0x1F6, 0xE0 | ((lba >> 24) & 15)); m.out8(0x1F2, n); m.out8(0x1F3, lba & 255); m.out8(0x1F4, (lba >> 8) & 255); m.out8(0x1F5, (lba >> 16) & 255); m.out8(0x1F7, 0x20); };
const word0 = (m) => m.read(0x100001F0, 2);

{
  const { m, src, acts } = mk();
  read(m, 1, 1);
  eq('available sector: DRQ at once, no fetch', [m.in8(0x1F7) & 0x88, src.calls.length], [0x08, 0]);
  eq('data', word0(m), 1);
  read(m, 100, 2);
  eq('missing sector: BSY, no DRQ', m.in8(0x1F7) & 0x88, 0x80);
  eq('fetch asked for the command\'s range', src.calls, [[100, 2]]);
  eq('no disk activity (sound) while waiting on the source', acts.length, 1);
  src.pending(true); await tick();
  eq('after fetch: DRQ, data, activity reported', [m.in8(0x1F7) & 0x88, word0(m), acts[1]], [0x08, 100, [100, 2]]);
  for (let k = 1; k < 256; k++) word0(m);
  eq('second sector follows', word0(m), 101);
}
{
  const { m, src } = mk();
  read(m, 500, 1);
  src.pending(false); await tick();
  const st = m.in8(0x1F7);
  eq('failed fetch: ERR, not BSY, UNC', [st & 0x81, m.in8(0x1F1)], [0x01, 0x40]);
}
{
  const { m, src } = mk();
  read(m, 600, 1);
  const p = src.pending;
  m.out8(0x3F6, 0x04); m.out8(0x3F6, 0x00);          // SRST
  p(true); await tick();
  eq('SRST cancels the wait (no late DRQ)', m.in8(0x1F7) & 0x88, 0x00);
  read(m, 700, 1); const p2 = src.pending;
  read(m, 1, 1);                                       // a new command abandons it
  eq('new command after a wait works', [m.in8(0x1F7) & 0x88, word0(m)], [0x08, 1]);
  p2(true); await tick();
  eq('stale completion ignored', m.ata.mode, 1);
}
{
  const { m, src, hd } = mk();
  m.out8(0x1F6, 0xE0); m.out8(0x1F2, 1); m.out8(0x1F3, 900 & 255); m.out8(0x1F4, 900 >> 8); m.out8(0x1F5, 0); m.out8(0x1F7, 0x30);
  for (let k = 0; k < 256; k++) m.write(0x100001F0, 2, 0xBEEF);
  eq('writes go straight to the image and are reported to the source', [hd[900 * 512], src.written], [0xEF, [900]]);
}
console.log(`ata-stream: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
