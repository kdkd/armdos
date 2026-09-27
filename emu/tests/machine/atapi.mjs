#!/usr/bin/env node
// The ATAPI CD-ROM (dev/atapi.mjs), port level, no CPU: signature and IDENTIFY PACKET,
// INQUIRY, spin-up / unit attention / sense data, READ CAPACITY, READ(10/12) incl.
// streamed data, READ TOC (0/1, LBA/MSF), PLAY AUDIO MSF/(10)/(12), sub-channel position in
// emulated time, pause/resume/stop, completion status, MODE SENSE/SELECT page 0Eh volume,
// GET EVENT STATUS media events, tray eject/load and the lock, READ CD audio, and the audio
// rendered through dev/audio.mjs (level, pitch, SB16 CD volume, holds for undecoded audio).
import { Machine } from '../../machine.mjs';
import { CdDisc, lba2msf, msf2lba } from '../../dev/atapi.mjs';

let fails = 0, passes = 0;
const eq = (name, got, want) => {
  const ok = JSON.stringify(got) === JSON.stringify(want);
  if (ok) passes++; else { fails++; console.log(`FAIL ${name}: got ${JSON.stringify(got)} want ${JSON.stringify(want)}`); }
};
const ok = (name, cond, info = '') => { if (cond) passes++; else { fails++; console.log(`FAIL ${name} ${info}`); } };
const tick = () => new Promise((r) => setTimeout(r, 0));

// a disc: 400 data sectors, two audio tracks (3 s of 1 kHz at -6 dB left / 500 Hz right; 2 s of 440 Hz)
const DATA = 400;
const tone = (secs, fl, fr, amp = 0.5) => {
  const n = Math.round(secs * 44100), left = new Float32Array(n), right = new Float32Array(n);
  for (let i = 0; i < n; i++) { left[i] = amp * Math.sin(2 * Math.PI * fl * i / 44100); right[i] = amp * Math.sin(2 * Math.PI * fr * i / 44100); }
  return { left, right };
};
const pcm = { 2: tone(3, 1000, 500), 3: tone(2, 440, 440) };
const manifest = { title: 'Test Disc', mcn: '0012345678905', tracks: [
  { number: 1, type: 'data', sectors: DATA },
  { number: 2, type: 'audio', pregap: 150, sectors: Math.ceil(pcm[2].left.length / 588), frames: pcm[2].left.length },
  { number: 3, type: 'audio', pregap: 150, sectors: Math.ceil(pcm[3].left.length / 588), frames: pcm[3].left.length, isrc: 'USABC9300001' },
] };
const iso = new Uint8Array(DATA * 2048);
for (let s = 0; s < DATA; s++) { iso[s * 2048] = s & 255; iso[s * 2048 + 1] = s >> 8; iso[s * 2048 + 2047] = 0xA5; }

function mk(opts = {}) {
  const changes = [], acts = [];
  const m = new Machine({ jit: false, onCdrom: (s) => changes.push(s), onCdActivity: (l, n) => acts.push([l, n]) });
  m.cpu.halted = 1; m.cpu.i = 1;
  const disc = new CdDisc(manifest, { data: opts.data || iso, audio: opts.audio || ((n) => pcm[n] || null) });
  return { m, disc, changes, acts };
}
const st = (m) => m.in8(0x177);
/** issue a packet; returns { data: Uint8Array, status, err, sense? } */
function packet(m, bytes, { limit = 0xFFFE, out = null } = {}) {
  const cdb = new Uint8Array(12); cdb.set(bytes);
  m.out8(0x176, 0xA0); m.out8(0x171, 0); m.out8(0x174, limit & 255); m.out8(0x175, limit >> 8);
  m.out8(0x177, 0xA0);
  if ((st(m) & 0x88) !== 0x08 || m.in8(0x172) !== 1) return { error: 'no CDB phase' };
  for (let i = 0; i < 12; i += 2) m.write(0x10000170, 2, cdb[i] | (cdb[i + 1] << 8));
  return finish(m, out);
}
function finish(m, out) {
  const data = [];
  for (let guard = 0; guard < 100000; guard++) {
    const s = st(m);
    if (s & 0x80) return { busy: true, data: new Uint8Array(data) };
    if (!(s & 0x08)) break;
    const n = m.in8(0x174) | (m.in8(0x175) << 8), reason = m.in8(0x172);
    if (reason === 0) { for (let i = 0; i < n; i += 2) m.write(0x10000170, 2, out[i] | ((out[i + 1] || 0) << 8)); continue; }
    for (let i = 0; i < n; i += 2) { const w = m.read(0x10000170, 2); data.push(w & 255); if (i + 1 < n) data.push(w >> 8); }
  }
  const s = st(m);
  const r = { data: new Uint8Array(data), status: s, err: m.in8(0x171), reason: m.in8(0x172) };
  if (s & 1) { const q = packet(m, [0x03, 0, 0, 0, 18]); r.sense = [q.data[2] & 15, q.data[12], q.data[13]]; }
  return r;
}
const be32 = (b, o) => ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0;
const ascii = (b, a, n) => String.fromCharCode(...b.subarray(a, a + n));
const ready = (m) => { m.runFor(1300); for (let i = 0; i < 3; i++) if (!(packet(m, [0x00]).status & 1)) break; };
const subq = (m, msf = 1) => { const r = packet(m, [0x42, msf ? 2 : 0, 0x40, 1, 0, 0, 0, 0, 16]); return { status: r.data[1], track: r.data[6], index: r.data[7], abs: msf ? [r.data[9], r.data[10], r.data[11]] : be32(r.data, 8), rel: msf ? [r.data[13], r.data[14], r.data[15]] : be32(r.data, 12) }; };
const msfOf = (lba) => lba2msf(lba);

// ------------------------------------------------------------------ basics
{
  const { m, disc } = mk();
  eq('signature after reset', [m.in8(0x174), m.in8(0x175), m.in8(0x172), m.in8(0x173)], [0x14, 0xEB, 1, 1]);
  m.out8(0x176, 0xB0);
  eq('no slave', m.in8(0x177), 0);
  m.out8(0x176, 0xA0);
  m.out8(0x177, 0xEC);
  eq('IDENTIFY DEVICE aborts with the signature', [st(m) & 1, m.in8(0x171) & 4, m.in8(0x174), m.in8(0x175)], [1, 4, 0x14, 0xEB]);
  m.out8(0x177, 0xA1);
  const id = finish(m).data;
  const w = (i) => id[2 * i] | (id[2 * i + 1] << 8);
  const idstr = (a, n) => { let s = ''; for (let i = 0; i < n; i++) s += String.fromCharCode(w(a + i) >> 8, w(a + i) & 255); return s.trim(); };
  eq('IDENTIFY PACKET: ATAPI CD-ROM, 12-byte packets', [w(0) >> 14, (w(0) >> 8) & 31, w(0) & 3], [2, 5, 0]);
  eq('IDENTIFY PACKET: model', idstr(27, 20), 'ARM-PC CD-ROM DRIVE');

  const inq = packet(m, [0x12, 0, 0, 0, 36]);
  eq('INQUIRY: CD-ROM, removable', [inq.data[0], inq.data[1]], [5, 0x80]);
  eq('INQUIRY: "ARM-PC CD-ROM DRIVE"', [ascii(inq.data, 8, 8).trim(), ascii(inq.data, 16, 16).trim(), ascii(inq.data, 32, 4)], ['ARM-PC', 'CD-ROM DRIVE', '1.00']);
  let r = packet(m, [0x00]);
  eq('first TUR: unit attention power on (06/29)', r.sense, [6, 0x29, 0]);
  eq('ERR, sense key in the error register', [r.status & 1, r.err >> 4], [1, 6]);
  eq('empty drive: 02/3A', packet(m, [0x00]).sense, [2, 0x3A, 1]);
  m.cdrom.insert(disc);
  eq('spinning up: 02/04/01', packet(m, [0x00]).sense, [2, 0x04, 1]);
  const ev = packet(m, [0x4A, 1, 0, 0, 0x10, 0, 0, 0, 8]);
  eq('GESN: new media, present', [ev.data[2], ev.data[4], ev.data[5]], [4, 2, 2]);
  eq('GESN: event consumed', packet(m, [0x4A, 1, 0, 0, 0x10, 0, 0, 0, 8]).data[4], 0);
  m.runFor(1300);
  eq('ready: unit attention medium changed (06/28) once', packet(m, [0x00]).sense, [6, 0x28, 0]);
  eq('then ready', packet(m, [0x00]).status & 1, 0);
  const cap = packet(m, [0x25]);
  eq('READ CAPACITY: last LBA, 2048', [be32(cap.data, 0), be32(cap.data, 4)], [disc.leadout - 1, 2048]);
  r = packet(m, [0x28, 0, 0, 0, 0, 16, 0, 0, 3]);
  eq('READ(10) 16..18', [r.data.length, r.data[0], r.data[2048], r.data[4096], r.data[2047], r.status & 1], [6144, 16, 17, 18, 0xA5, 0]);
  r = packet(m, [0x28, 0, 0, 0, 1, 0x00, 0, 0, 40], { limit: 0x8000 });
  eq('READ(10) 40 sectors with a 32 KB byte count limit', [r.data.length, r.data[39 * 2048] | (r.data[39 * 2048 + 1] << 8)], [40 * 2048, 256 + 39]);
  r = packet(m, [0xA8, 0, 0, 0, 0, 5, 0, 0, 0, 2]);
  eq('READ(12)', [r.data.length, r.data[2048]], [4096, 6]);
  eq('READ past the data track: 05/64', packet(m, [0x28, 0, 0, 0, 1, 0x8F, 0, 0, 2]).sense, [5, 0x64, 0]);
  eq('READ past the lead-out: 05/21', packet(m, [0x28, 0, 0, 1, 0, 0, 0, 0, 1]).sense, [5, 0x21, 0]);
  eq('unknown opcode: 05/20', packet(m, [0xD8]).sense, [5, 0x20, 0]);

  // TOC
  const t2 = disc.track(2), t3 = disc.track(3);
  eq('layout: track 2 after data + 150 pregap', [t2.start, t3.start, disc.leadout], [DATA + 150, t2.end + 150, t3.end]);
  let toc = packet(m, [0x43, 0, 0, 0, 0, 0, 0, 0x03, 0x24]).data;
  eq('TOC LBA: header', [(toc[0] << 8) | toc[1], toc[2], toc[3]], [2 + 4 * 8, 1, 3]);
  eq('TOC LBA: entries', [[toc[5], toc[6], be32(toc, 8)], [toc[13], toc[14], be32(toc, 16)], [toc[21], toc[22], be32(toc, 24)], [toc[30], be32(toc, 32)]],
    [[0x14, 1, 0], [0x10, 2, t2.start], [0x10, 3, t3.start], [0xAA, disc.leadout]]);
  toc = packet(m, [0x43, 2, 0, 0, 0, 0, 2, 0x03, 0x24]).data;
  eq('TOC MSF from track 2', [toc[3 + 0], toc[6], toc[9], toc[10], toc[11]], [3, 2, ...msfOf(t2.start)]);
  eq('TOC MSF: track 1 at 00:02:00', msfOf(0), [0, 2, 0]);
  toc = packet(m, [0x43, 0, 1, 0, 0, 0, 0, 0, 12]).data;
  eq('TOC format 1 (sessions)', [toc[1], toc[2], toc[3], toc[6], be32(toc, 8)], [10, 1, 1, 1, 0]);
  eq('TOC bad start track', packet(m, [0x43, 0, 0, 0, 0, 0, 9, 0, 12]).sense, [5, 0x24, 0]);
  eq('TOC allocation length honoured', packet(m, [0x43, 0, 0, 0, 0, 0, 0, 0, 4]).data.length, 4);

  // MCN / ISRC
  let q = packet(m, [0x42, 0, 0x40, 2, 0, 0, 0, 0, 24]).data;
  eq('MCN', [q[8] & 0x80, ascii(q, 9, 13)], [0x80, '0012345678905']);
  q = packet(m, [0x42, 0, 0x40, 3, 0, 0, 3, 0, 24]).data;
  eq('ISRC of track 3', ascii(q, 9, 12), 'USABC9300001');

  // ---------------------------------------------------------------- audio
  eq('PLAY on the data track: 05/64', packet(m, [0x47, 0, 0, 0, 2, 0, 0, 3, 0]).sense, [5, 0x64, 0]);
  eq('no audio status before play', subq(m).status, 0x15);
  const [sm, ss, sf] = msfOf(t2.start), [em, es, ef] = msfOf(t2.end);
  r = packet(m, [0x47, 0, 0, sm, ss, sf, em, es, ef]);
  eq('PLAY AUDIO MSF track 2 accepted', r.status & 1, 0);
  let s = subq(m);
  eq('playing, track 2 index 1 at its start', [s.status, s.track, s.index, s.abs], [0x11, 2, 1, msfOf(t2.start)]);
  m.runFor(1000);
  s = subq(m, 0);
  ok('position advances in emulated time: +75 sectors after 1 s', Math.abs(s.abs - (t2.start + 75)) <= 1, JSON.stringify(s));
  eq('relative address counts from the track start', s.rel, s.abs - t2.start);
  const rs = packet(m, [0x03, 0, 0, 0, 18]).data;
  eq('REQUEST SENSE while playing: 00/00/11', [rs[2], rs[12], rs[13]], [0, 0, 0x11]);
  packet(m, [0x4B, 0, 0, 0, 0, 0, 0, 0, 0]);
  const p0 = subq(m, 0).abs;
  m.runFor(500);
  eq('paused: status 12h, position held', [subq(m, 0).status, subq(m, 0).abs], [0x12, p0]);
  packet(m, [0x4B, 0, 0, 0, 0, 0, 0, 0, 1]);
  m.runFor(400);
  s = subq(m, 0);
  ok('resumed: moves on from where it paused', s.status === 0x11 && Math.abs(s.abs - (p0 + 30)) <= 1, JSON.stringify(s));
  m.runFor(3000);
  eq('completed (13h) reported once, then 15h', [subq(m).status, subq(m).status], [0x13, 0x15]);
  eq('RESUME without a play: 05/2C', packet(m, [0x4B, 0, 0, 0, 0, 0, 0, 0, 1]).sense, [5, 0x2C, 0]);

  // PLAY AUDIO(10) from the pregap of track 3 into the track, then STOP
  r = packet(m, [0x45, 0, ...[t3.start - 75].flatMap((v) => [v >>> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255]), 0, 0, 150]);
  s = subq(m, 0);
  eq('PLAY(10) in the pregap: track 3 index 0, relative counts down', [s.status, s.track, s.index, s.rel >> 0], [0x11, 3, 0, -75]);
  m.runFor(1200);
  s = subq(m, 0);
  eq('... then index 1', [s.track, s.index], [3, 1]);
  packet(m, [0x4E]);
  eq('STOP PLAY: no status', subq(m).status, 0x15);
  // PLAY(12) with a length of 0 does nothing
  eq('PLAY(12) length 0: accepted, nothing plays', [packet(m, [0xA5, 0, 0, 0, 3, 0, 0, 0, 0, 0]).status & 1, subq(m).status], [0, 0x15]);

  // MODE SENSE / SELECT page 0Eh
  let ms = packet(m, [0x5A, 0, 0x0E, 0, 0, 0, 0, 0, 24]).data;
  eq('MODE SENSE 0Eh: header + page, default volumes', [(ms[0] << 8) | ms[1], ms[2], ms[8], ms[9], ms[16], ms[17], ms[18], ms[19]], [22, 3, 0x0E, 0x0E, 1, 255, 2, 255]);
  const sel = new Uint8Array(24); sel.set(ms.subarray(0, 24)); sel[0] = sel[1] = 0; sel[17] = 0x40; sel[19] = 0x80;
  r = packet(m, [0x55, 0x10, 0, 0, 0, 0, 0, 0, 24], { out: sel });
  eq('MODE SELECT accepted', r.status & 1, 0);
  ms = packet(m, [0x5A, 0, 0x0E, 0, 0, 0, 0, 0, 24]).data;
  eq('volumes changed', [ms[17], ms[19]], [0x40, 0x80]);
  ms = packet(m, [0x5A, 0, 0x3F, 0, 0, 0, 0, 0x01, 0]).data;
  eq('MODE SENSE all pages', [ms[8], ms[16], ms[24], ms[40]], [1, 0x0D, 0x0E, 0x2A]);
  eq('MODE SENSE unknown page: 05/24', packet(m, [0x5A, 0, 0x05, 0, 0, 0, 0, 0, 24]).sense, [5, 0x24, 0]);

  // READ CD of audio sectors: raw PCM
  r = packet(m, [0xBE, 0x04, ...[t2.start + 1].flatMap((v) => [v >>> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255]), 0, 0, 1, 0x10]);
  const dv = new DataView(r.data.buffer);
  const f0 = 588;                                   // sector 1 of track 2 = frame 588
  eq('READ CD audio: 2352 bytes of the track PCM', [r.data.length, dv.getInt16(0, true), dv.getInt16(2, true)],
    [2352, Math.round(pcm[2].left[f0] * 32767), Math.round(pcm[2].right[f0] * 32767)]);
  eq('READ(10) of an audio sector: 05/64', packet(m, [0x28, 0, ...[t2.start].flatMap((v) => [v >>> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255]), 0, 0, 1]).sense, [5, 0x64, 0]);

  // tray: lock, eject, load
  packet(m, [0x1E, 0, 0, 0, 1]);
  eq('locked: eject refused 05/53/02', packet(m, [0x1B, 0, 0, 0, 2]).sense, [5, 0x53, 2]);
  eq('locked: tray button refused, eject request event', [m.cdrom.trayButton(), packet(m, [0x4A, 1, 0, 0, 0x10, 0, 0, 0, 8]).data[4]], [false, 1]);
  packet(m, [0x1E, 0, 0, 0, 0]);
  eq('START STOP UNIT eject', [packet(m, [0x1B, 0, 0, 0, 2]).status & 1, m.cdrom.trayOpen], [0, true]);
  eq('tray open: 02/3A/02', packet(m, [0x00]).sense, [2, 0x3A, 2]);
  eq('GESN: removal, tray open, no media', (() => { const d = packet(m, [0x4A, 1, 0, 0, 0x10, 0, 0, 0, 8]).data; return [d[4], d[5]]; })(), [3, 1]);
  packet(m, [0x1B, 0, 0, 0, 3]);
  eq('START STOP UNIT load: tray closed, spinning up', [m.cdrom.trayOpen, packet(m, [0x00]).sense], [false, [2, 4, 1]]);
  ready(m);
  eq('ready again', packet(m, [0x00]).status & 1, 0);
}

// ------------------------------------------------------------------ rendered audio
{
  const { m, disc } = mk();
  m.cdrom.insert(disc); ready(m);
  // SB16 mixer: master 0 dB, CD 0 dB
  const mix = (r, v) => { m.out8(0x224, r); m.out8(0x225, v); };
  mix(0x30, 0xF8); mix(0x31, 0xF8); mix(0x36, 0xF8); mix(0x37, 0xF8);
  const L = [], R = [];
  m.audio.start(44100, (l, r) => { L.push(...l); R.push(...r); });
  m.runFor(200);
  const t2 = disc.track(2), [sm, ss, sf] = msfOf(t2.start), [em, es, ef] = msfOf(t2.end);
  const at = L.length;
  packet(m, [0x47, 0, 0, sm, ss, sf, em, es, ef]);
  m.runFor(1000);
  const rms = (a, from, n) => { let s = 0; for (let i = from; i < from + n; i++) s += a[i] * a[i]; return Math.sqrt(s / n); };
  const zc = (a, from, n) => { let c = 0; for (let i = from + 1; i < from + n; i++) if ((a[i - 1] < 0) !== (a[i] < 0)) c++; return c / 2 / (n / 44100); };
  ok('silence before PLAY', rms(L, 0, at) < 1e-6);
  ok('playing: left 1 kHz at 0.5 amplitude (rms 0.354)', Math.abs(rms(L, at + 4410, 22050) - 0.3536) < 0.01, rms(L, at + 4410, 22050));
  ok('left pitch 1000 Hz', Math.abs(zc(L, at + 4410, 22050) - 1000) < 5, zc(L, at + 4410, 22050));
  ok('right pitch 500 Hz', Math.abs(zc(R, at + 4410, 22050) - 500) < 5, zc(R, at + 4410, 22050));
  ok('sound starts at the PLAY command (within 1 ms)', rms(L, at, 44) > 0.2 || rms(L, at + 44, 44) > 0.2);
  mix(0x36, 0xB8); mix(0x37, 0xB8);                 // CD -16 dB
  const a2 = L.length; m.runFor(500);
  ok('SB16 CD volume -16 dB applies', Math.abs(rms(L, a2 + 441, 11025) / 0.3536 - Math.pow(10, -16 / 20)) < 0.01, rms(L, a2 + 441, 11025));
  mix(0x36, 0xF8); mix(0x37, 0xF8);
  const sel = new Uint8Array(24); sel.set([0, 0, 0, 0, 0, 0, 0, 0, 0x0E, 0x0E, 4, 0, 0, 0, 0, 0, 0x02, 0x80, 0x01, 0xFF]);
  packet(m, [0x55, 0x10, 0, 0, 0, 0, 0, 0, 24], { out: sel });
  const a3 = L.length; m.runFor(500);
  ok('MODE SELECT: left port <- channel 1 at half volume (500 Hz)', Math.abs(zc(L, a3 + 441, 11025) - 500) < 5 && Math.abs(rms(L, a3 + 441, 11025) - 0.3536 * 128 / 255) < 0.01, [zc(L, a3 + 441, 11025), rms(L, a3 + 441, 11025)]);
  ok('... right port <- channel 0 (1 kHz)', Math.abs(zc(R, a3 + 441, 11025) - 1000) < 5);
  packet(m, [0x4B, 0, 0, 0, 0, 0, 0, 0, 0]);
  const a4 = L.length; m.runFor(300);
  ok('paused: silence', rms(L, a4 + 100, 10000) < 1e-6);
  m.cdrom.headphones = {}; m.cdrom.knob = 0.5;
  packet(m, [0x4B, 0, 0, 0, 0, 0, 0, 0, 1]);
  mix(0x36, 0); mix(0x37, 0);
  const a5 = L.length; m.runFor(300);
  ok('headphones: bypass the SB16 mixer, knob volume', Math.abs(rms(R, a5 + 441, 8820) - 0.3536 * 0.5) < 0.01, rms(R, a5 + 441, 8820));
  m.audio.stop();
}

// ------------------------------------------------------------------ streaming data + held audio
{
  const have = new Set([0, 1, 2]);
  let pendingData = null, pendingAudio = null, audioReq = [];
  const data = { img: iso, ready: (l, n) => { for (let k = l; k < l + n; k++) if (!have.has(k)) return false; return true; },
    fetch: (l, n) => new Promise((res) => { pendingData = (okk) => { if (okk) for (let k = l; k < l + n; k++) have.add(k); res(okk); }; }) };
  const decoded = {};
  const audio = { get: (n) => decoded[n] || null, request: (n) => { audioReq.push(n); return new Promise((res) => { pendingAudio = () => { decoded[n] = pcm[n]; res(); }; }); } };
  const { m, disc, acts } = mk({ data, audio });
  m.cdrom.insert(disc); ready(m);
  let r = packet(m, [0x28, 0, 0, 0, 0, 1, 0, 0, 1]);
  eq('available sector: data at once', r.data[0], 1);
  r = packet(m, [0x28, 0, 0, 0, 0, 100, 0, 0, 2]);
  eq('streamed sector: BSY while fetching', [!!r.busy, st(m) & 0x88], [true, 0x80]);
  pendingData(true); await tick();
  r = finish(m);
  eq('after the fetch: the data', [r.data.length, r.data[0], r.data[2048]], [4096, 100, 101]);
  r = packet(m, [0x28, 0, 0, 0, 0, 200, 0, 0, 1]);
  pendingData(false); await tick();
  eq('failed fetch: 03/11 (unrecovered read error)', finish(m).sense, [3, 0x11, 0]);

  const t2 = disc.track(2), [sm, ss, sf] = msfOf(t2.start), [em, es, ef] = msfOf(t2.end);
  packet(m, [0x47, 0, 0, sm, ss, sf, em, es, ef]);
  eq('PLAY of undecoded audio: requested, status playing', [audioReq, subq(m).status], [[2], 0x11]);
  m.runFor(700);
  eq('position held at the start while decoding (the drive seeks)', subq(m, 0).abs, t2.start);
  pendingAudio(); await tick();
  m.runFor(1000);
  ok('after decoding: plays from the start', Math.abs(subq(m, 0).abs - (t2.start + 75)) <= 1, subq(m, 0).abs);
  m.runFor(1500);
  eq('track 3 requested ahead of the track boundary (plays to end of 2 only: no)', audioReq, [2]);
}
{
  // a play across two tracks decodes the next one ahead
  const decoded = { 2: pcm[2] }; const req = [];
  const audio = { get: (n) => decoded[n] || null, request: (n) => { req.push(n); decoded[n] = pcm[n]; return Promise.resolve(); } };
  const { m, disc } = mk({ audio });
  m.cdrom.insert(disc); ready(m);
  const t2 = disc.track(2), [sm, ss, sf] = msfOf(t2.start), [em, es, ef] = msfOf(disc.leadout);
  packet(m, [0x47, 0, 0, sm, ss, sf, em, es, ef]);
  m.runFor(500); subq(m);
  eq('next track requested while playing the one before it', req, [3]);
  m.runFor(5000);
  const s = subq(m, 0);
  eq('played on into track 3', [s.status, s.track], [0x11, 3]);
  m.runFor(3000);
  eq('completed at the lead-out', subq(m).status, 0x13);
}

eq('msf2lba(lba2msf(x))', [0, 150, 4499, 333000].map((x) => msf2lba(...lba2msf(x))), [0, 150, 4499, 333000]);
console.log(`${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
