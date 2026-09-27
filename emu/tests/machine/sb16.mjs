#!/usr/bin/env node
// Sound Blaster 16 / 8237 DMA / OPL3 tests at the port level (no guest code):
// DSP reset and identification, DMA register behaviour, transfers paced by
// emulated time with IRQs at block ends, auto-init/pause/exit, 16-bit stereo,
// the mixer, OPL timers (AdLib detection), and rendered audio: an OPL note and
// a DMA sine checked by their spectral peak (ARCH.md §4.2, §4.3).
import { Machine } from '../../machine.mjs';
import { OPL3 } from '../../dev/opl3.mjs';

let fails = 0, passes = 0;
const eq = (name, got, want) => {
  const ok = JSON.stringify(got) === JSON.stringify(want);
  if (ok) passes++; else { fails++; console.log(`FAIL ${name}: got ${JSON.stringify(got)} want ${JSON.stringify(want)}`); }
};
const near = (name, got, want, tol) => {
  if (Math.abs(got - want) <= tol) passes++; else { fails++; console.log(`FAIL ${name}: got ${got} want ${want} ±${tol}`); }
};
function mk(o = {}) {
  const m = new Machine({ jit: false, rtcBaseMs: 0, ...o });
  m.cpu.halted = 1; m.cpu.i = 1;      // the CPU sleeps; we drive the ports
  m.out8(0x21, 0x00);                 // unmask IRQ 0-7 (IRQ7 = the SB)
  return m;
}
const dsp = (m, ...bytes) => { for (const b of bytes) m.out8(0x22C, b); };
const rd = (m) => m.in8(0x22A);
function dmaSetup8(m, ch, addr, count, mode) {           // count = bytes
  m.out8(0x0A, 4 | ch);                                  // mask
  m.out8(0x0C, 0);                                       // flip-flop
  m.out8(0x0B, mode | ch);
  m.out8(ch * 2, addr & 0xFF); m.out8(ch * 2, (addr >> 8) & 0xFF);
  m.out8({ 0: 0x87, 1: 0x83, 2: 0x81, 3: 0x82 }[ch], (addr >> 16) & 0xFF);
  m.out8(ch * 2 + 1, (count - 1) & 0xFF); m.out8(ch * 2 + 1, ((count - 1) >> 8) & 0xFF);
  m.out8(0x0A, ch);                                      // unmask
}
function dmaSetup16(m, ch, addr, words, mode) {
  const c = ch - 4;
  m.out8(0xD4, 4 | c); m.out8(0xD8, 0); m.out8(0xD6, mode | c);
  const w = addr >> 1;
  m.out8(0xC0 + c * 4, w & 0xFF); m.out8(0xC0 + c * 4, (w >> 8) & 0xFF);
  m.out8({ 5: 0x8B, 6: 0x89, 7: 0x8A }[ch], (addr >> 16) & 0xFE);
  m.out8(0xC2 + c * 4, (words - 1) & 0xFF); m.out8(0xC2 + c * 4, ((words - 1) >> 8) & 0xFF);
  m.out8(0xD4, c);
}
const dmaCount8 = (m, ch) => { m.out8(0x0C, 0); const lo = m.in8(ch * 2 + 1); return lo | (m.in8(ch * 2 + 1) << 8); };
const dmaAddr8 = (m, ch) => { m.out8(0x0C, 0); const lo = m.in8(ch * 2); return lo | (m.in8(ch * 2) << 8); };
// IRQ7 pending at the PIC?
const irq7 = (m) => (m.pic.irr & 0x80) !== 0;
// run until the IRQ comes, return emulated ms
function waitIrq(m, maxMs = 2000) {
  const t0 = m.timeMs();
  while (!irq7(m) && m.timeMs() - t0 < maxMs) m.runFor(0.05);
  return m.timeMs() - t0;
}
function peakHz(x, rate, lo = 50, hi = 5000) {           // brute-force DFT peak (1 Hz resolution near the top)
  let best = 0, bestF = 0;
  const pow = (f) => { let re = 0, im = 0; const w = 2 * Math.PI * f / rate; for (let i = 0; i < x.length; i++) { re += x[i] * Math.cos(w * i); im += x[i] * Math.sin(w * i); } return re * re + im * im; };
  for (let f = lo; f <= hi; f += 5) { const p = pow(f); if (p > best) { best = p; bestF = f; } }
  for (let f = bestF - 5; f <= bestF + 5; f += 0.5) { const p = pow(f); if (p > best) { best = p; bestF = f; } }
  return bestF;
}
const rms = (x) => Math.sqrt(x.reduce((a, v) => a + v * v, 0) / x.length);

// ---------------- DSP reset / identification
{
  const m = mk();
  m.out8(0x226, 1); m.out8(0x226, 0);
  eq('reset: no data before 20 us', m.in8(0x22E) & 0x80, 0);
  m.runFor(0.03);
  eq('reset: data ready', m.in8(0x22E) & 0x80, 0x80);
  eq('reset: AA', rd(m), 0xAA);
  eq('write status ready', m.in8(0x22C) & 0x80, 0);
  dsp(m, 0xE1); eq('DSP version 4.05', [rd(m), rd(m)], [4, 5]);
  dsp(m, 0xE0, 0x5A); eq('E0 invert', rd(m), 0xA5);
  dsp(m, 0xE3); let s = ''; for (let k = 0; k < 60; k++) { const c = rd(m); if (!c) break; s += String.fromCharCode(c); }
  eq('E3 copyright', s, 'COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.');
  dsp(m, 0xD1, 0xD8); eq('speaker on', rd(m), 0xFF);
  dsp(m, 0xD3, 0xD8); eq('speaker off', rd(m), 0x00);
  dsp(m, 0xE4, 0x3C, 0xE8); eq('test register', rd(m), 0x3C);
  dsp(m, 0xF2);
  eq('F2 raises IRQ 7', irq7(m), true);
  eq('mixer 82h: 8-bit IRQ pending', (() => { m.out8(0x224, 0x82); return m.in8(0x225) & 3; })(), 1);
  m.in8(0x22E);
  eq('22Eh read acknowledges', [irq7(m), (() => { m.out8(0x224, 0x82); return m.in8(0x225) & 3; })()], [false, 0]);
}

// ---------------- mixer
{
  const m = mk();
  const mx = (i, v) => { m.out8(0x224, i); if (v !== undefined) m.out8(0x225, v); return m.in8(0x225); };
  eq('mixer 80h = IRQ7', mx(0x80), 0x04);
  eq('mixer 81h = DMA 1 + 5', mx(0x81), 0x22);
  eq('master power-on -14 dB', [mx(0x30), mx(0x31)], [0xC0, 0xC0]);
  mx(0x22, 0xFF); eq('SB Pro master alias', [mx(0x30), mx(0x31), mx(0x22)], [0xF8, 0xF8, 0xFF]);
  mx(0x04, 0x9B); eq('SB Pro voice alias read back', mx(0x04), 0x9B);
  mx(0x00, 0); eq('mixer reset', mx(0x30), 0xC0);
  mx(0x80, 0x02); dsp(m, 0xF2);
  eq('IRQ select 5', (m.pic.irr & 0x20) !== 0, true);
  m.in8(0x22E);
}

// ---------------- 8237 registers
{
  const m = mk();
  dmaSetup8(m, 1, 0x12345, 0x1000, 0x48);
  eq('dma ch1 address readback', dmaAddr8(m, 1), 0x2345);
  eq('dma ch1 count readback', dmaCount8(m, 1), 0x0FFF);
  eq('page register readback', m.in8(0x83), 0x01);
  eq('dma ch1 physical', m.dma.physAddr(1).toString(16), '12345');
  m.out8(0x483, 0x02);
  eq('EISA high page', m.dma.physAddr(1).toString(16), '2012345');
  m.out8(0x83, 0x01);
  eq('low page write clears high page', m.dma.physAddr(1).toString(16), '12345');
  dmaSetup16(m, 5, 0x40000, 0x100, 0x58);
  eq('dma ch5 word address', m.dma.physAddr(5).toString(16), '40000');
  m.out8(0x0F, 0x0F); eq('write-all-mask', m.dma.ready(1), false);
  m.out8(0x0E, 0); eq('clear-mask', m.dma.ready(1), true);
}

// ---------------- 8-bit single-cycle DMA: pacing, IRQ time, address/count progress
{
  const m = mk();
  const buf = 0x20000;
  for (let i = 0; i < 4000; i++) m.cpu.m8[buf + i] = 128 + Math.round(100 * Math.sin(i / 3));
  dmaSetup8(m, 1, buf, 4000, 0x48);                      // single, read memory
  m.out8(0x226, 1); m.out8(0x226, 0); m.runFor(0.05); rd(m);
  dsp(m, 0x40, 256 - 100);                               // time constant: 10 kHz
  dsp(m, 0xD1);
  const t0 = m.timeMs();
  dsp(m, 0x14, (2000 - 1) & 0xFF, (2000 - 1) >> 8);      // 2000 bytes = 200 ms
  m.runFor(100);
  near('half-way DMA progress (bytes consumed)', 4000 - 1 - dmaCount8(m, 1), 1000, 2);
  eq('no IRQ at half way', irq7(m), false);
  const ms = waitIrq(m) + (m.timeMs() - t0 - waitIrq(m, 0));
  near('IRQ after 2000 frames at 10 kHz (ms)', m.timeMs() - t0, 200, 0.1);
  eq('address advanced by 2000', dmaAddr8(m, 1), (buf + 2000) & 0xFFFF);
  m.in8(0x22E);
  m.runFor(300);
  eq('single-cycle: no second IRQ', irq7(m), false);
  void ms;
}

// ---------------- 8-bit auto-init, pause/continue, exit auto-init
{
  const m = mk();
  const buf = 0x30000;
  dmaSetup8(m, 1, buf, 2048, 0x58);                      // auto-init, read
  m.out8(0x226, 1); m.out8(0x226, 0); m.runFor(0.05); rd(m);
  dsp(m, 0x40, 256 - 45);                                // 22222 Hz
  dsp(m, 0x48, 1023 & 0xFF, 1023 >> 8);                  // block = 1024
  const period = 1024 / (1e6 / 45) * 1000;               // ms
  const t0 = m.timeMs();
  dsp(m, 0x1C);
  const times = [];
  for (let k = 0; k < 4; k++) { waitIrq(m); times.push(m.timeMs() - t0); m.in8(0x22E); }
  near('auto-init IRQ 1', times[0], period, 0.1);
  near('auto-init IRQ 4', times[3], 4 * period, 0.2);
  near('DMA wrapped (auto-init reload): count', dmaCount8(m, 1), 2047, 2);
  dsp(m, 0xD0);
  m.runFor(3 * period);
  eq('paused: no IRQ', irq7(m), false);
  dsp(m, 0xD4);
  const t1 = m.timeMs(); waitIrq(m); m.in8(0x22E);
  eq('continue: IRQ within a block', m.timeMs() - t1 <= period + 0.1, true);
  dsp(m, 0xDA);
  waitIrq(m); m.in8(0x22E);
  m.runFor(3 * period);
  eq('exit auto-init: stops after the block', irq7(m), false);
}

// ---------------- SB16 16-bit stereo on DMA 5, rendered audio (1 kHz sine)
{
  const chunks = [];
  const m = mk({ audio: { rate: 44100, onAudio: (l, r) => chunks.push([l, r]) } });
  m.out8(0x224, 0x30); m.out8(0x225, 0xF8); m.out8(0x224, 0x31); m.out8(0x225, 0xF8);   // master 0 dB
  m.out8(0x224, 0x32); m.out8(0x225, 0xF8); m.out8(0x224, 0x33); m.out8(0x225, 0xF8);   // voice 0 dB
  const buf = 0x40000, frames = 2205;                    // 100 ms at 22050 Hz, stereo 16-bit
  for (let i = 0; i < frames; i++) {
    const v = Math.round(16000 * Math.sin(2 * Math.PI * 1000 * i / 22050));
    const w = (v) => v & 0xFFFF;
    m.cpu.m8[buf + i * 4] = w(v) & 0xFF; m.cpu.m8[buf + i * 4 + 1] = w(v) >> 8;
    m.cpu.m8[buf + i * 4 + 2] = w(-v) & 0xFF; m.cpu.m8[buf + i * 4 + 3] = w(-v) >> 8;   // right = inverted
  }
  dmaSetup16(m, 5, buf, frames * 2, 0x58);
  dsp(m, 0x41, 22050 >> 8, 22050 & 0xFF);
  const half = frames;                                   // samples (words) per block: two blocks per buffer
  const t0 = m.timeMs();
  dsp(m, 0xB6, 0x30, (half - 1) & 0xFF, (half - 1) >> 8); // 16-bit, auto-init, stereo signed; block = half the buffer
  waitIrq(m);
  near('16-bit stereo block IRQ (ms)', m.timeMs() - t0, 50, 0.1);
  m.out8(0x224, 0x82); eq('mixer 82h: 16-bit IRQ', m.in8(0x225) & 3, 2);
  m.in8(0x22E); eq('22Eh does not ack a 16-bit IRQ', irq7(m), true);
  m.in8(0x22F); eq('22Fh acks it', irq7(m), false);
  for (let k = 0; k < 12; k++) { waitIrq(m); m.in8(0x22F); }
  dsp(m, 0xD9); waitIrq(m); m.in8(0x22F);
  m.runFor(50);
  const L = Float32Array.from(chunks.flatMap(([l]) => [...l])), R = Float32Array.from(chunks.flatMap(([, r]) => [...r]));
  const mid = L.subarray(4410, 4410 + 8820), midR = R.subarray(4410, 4410 + 8820);
  near('PCM peak frequency (Hz)', peakHz(mid, 44100, 200, 4000), 1000, 2);
  near('PCM level (0 dB mixer, 16000/32768 sine)', rms(mid), 16000 / 32768 / Math.SQRT2, 0.02);
  let corr = 0; for (let i = 0; i < mid.length; i++) corr += mid[i] * midR[i];
  eq('right channel is the inverted left', corr < 0, true);
  const tail = L.subarray(L.length - 1000);
  eq('silent after exit', rms(tail) < 1e-3, true);
  // no gaps: every 10 ms window in the playing part has signal
  let gaps = 0; for (let w = 4410; w + 441 < 4410 + 22050 * 2 * 0.6; w += 441) if (rms(L.subarray(w, w + 441)) < 0.1) gaps++;
  eq('continuous output across auto-init blocks', gaps, 0);
}

// ---------------- OPL: timers (AdLib detection) and status
{
  const m = mk();
  const w = (r, v) => { m.out8(0x388, r); m.out8(0x389, v); };
  w(4, 0x60); w(4, 0x80);
  eq('opl status after reset', m.in8(0x388) & 0xE0, 0);
  w(2, 0xFF); w(4, 0x21);
  m.runFor(0.05);
  eq('timer 1 not yet (50 us)', m.in8(0x388) & 0xE0, 0);
  m.runFor(0.04);
  eq('timer 1 overflow (80 us): C0h', m.in8(0x388) & 0xE0, 0xC0);
  eq('OPL3 (status bits 1-2 clear)', m.in8(0x388) & 0x06, 0);
  w(4, 0x60); w(4, 0x80);
  w(3, 0xFE); w(4, 0x02 | 0x40);                         // timer 2, 640 us, timer 1 masked
  m.runFor(0.6); eq('timer 2 not yet', m.in8(0x388) & 0xE0, 0);
  m.runFor(0.1); eq('timer 2 overflow: A0h', m.in8(0x388) & 0xE0, 0xA0);
  w(4, 0x80); eq('IRQ reset clears flags', m.in8(0x388) & 0xE0, 0);
  eq('SB base+8 is the OPL status too', m.in8(0x228) & 0x06, 0);
}

// ---------------- OPL3 core: a sine note at a known pitch
function oplNote(o, ch, hz, block = 4) {
  const fnum = Math.round(hz * (1 << (20 - block)) / 49716);
  const op1 = [0, 1, 2, 8, 9, 10, 16, 17, 18][ch], op2 = op1 + 3;
  o(0x20 + op1, 0x21); o(0x20 + op2, 0x21);               // sustained (EGT), multiplier 1
  o(0x40 + op1, 0x3F); o(0x40 + op2, 0x00);               // modulator silent, carrier loud
  o(0x60 + op1, 0xF0); o(0x60 + op2, 0xF0);               // fast attack
  o(0x80 + op1, 0x05); o(0x80 + op2, 0x05);               // sustain level 0, release rate 5
  o(0xE0 + op1, 0); o(0xE0 + op2, 0);                     // sine
  o(0xC0 + ch, 0x30);                                     // FM, both speakers
  o(0xA0 + ch, fnum & 0xFF); o(0xB0 + ch, 0x20 | (block << 2) | (fnum >> 8));
}
{
  const chip = new OPL3();
  oplNote((r, v) => chip.writeReg(r, v), 0, 440);
  const x = new Float32Array(49716 / 2);
  for (let i = 0; i < 2000; i++) chip.generate();
  for (let i = 0; i < x.length; i++) { chip.generate(); x[i] = chip.outL; }
  near('Nuked OPL3: 440 Hz note peak', peakHz(x, 49716, 100, 2000), 440, 1.5);
  eq('Nuked OPL3: full-scale sine amplitude', Math.max(...x) > 3500 && Math.max(...x) < 4100, true);
  // 440*2^(20-4)/49716 = 580: the chip's own frequency is 580*49716/65536 = 439.99 Hz
  const chip2 = new OPL3(); let i = 0;
  const x2 = new Float32Array(20000);
  for (; i < x2.length; i++) { chip2.generate(); x2[i] = chip2.outL; }
  eq('Nuked OPL3: silent after reset', Math.max(...x2.map(Math.abs)), 0);
  eq('isSilent after reset', chip2.isSilent(), true);
}
// bit-exactness: a random register stream (rhythm, 4-op, OPL3 mode, vibrato,
// waveforms ...), 60000 samples, checksum of the C Nuked-OPL3's output
// (chocolate-doom opl/opl3.c driven by the same stream; measured 2026-09-24)
{
  let seed = 3; const rnd = () => ((seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
  const ri = (n) => Math.floor(rnd() * n);
  const ev = []; let t = 0;
  const w = (r, v) => ev.push([t, r, v]);
  w(0x105, seed & 1); w(0x104, ri(64));
  for (let k = 0; k < 400; k++) {
    t += ri(600);
    const hi = (seed & 1) && rnd() < 0.5 ? 0x100 : 0;
    const x = rnd();
    if (x < 0.5) { const base = [0x20, 0x40, 0x60, 0x80, 0xE0][ri(5)]; w(hi | (base + ri(0x16)), ri(256)); }
    else if (x < 0.75) { const ch = ri(9); w(hi | (0xA0 + ch), ri(256)); w(hi | (0xB0 + ch), ri(64)); }
    else if (x < 0.9) w(hi | (0xC0 + ri(9)), ri(256));
    else if (x < 0.97) w(0xBD, ri(256));
    else w(0x08, ri(256));
  }
  const c = new OPL3(); let e = 0, h = 2166136261;
  for (let i = 0; i < 60000; i++) {
    while (e < ev.length && ev[e][0] <= i) { c.writeReg(ev[e][1], ev[e][2]); e++; }
    c.generate();
    for (const ch of `${c.outL} ${c.outR}\n`) { h ^= ch.charCodeAt(0); h = Math.imul(h, 16777619) >>> 0; }
  }
  eq('Nuked OPL3 bit-exact vs the C original (checksum)', h.toString(16), '3c1cfe62');
}
// the machine renders FM at 44.1 kHz through the SB mixer, on time
{
  const chunks = [];
  const m = mk({ audio: { rate: 44100, onAudio: (l, r) => chunks.push([l, r]) } });
  m.out8(0x224, 0x22); m.out8(0x225, 0xFF); m.out8(0x224, 0x26); m.out8(0x225, 0xFF);
  const o = (r, v) => { m.out8(0x388, r); m.out8(0x389, v); };
  m.runFor(100);                                          // 100 ms of silence first
  oplNote(o, 3, 523.25, 5);                               // C5 on channel 3
  m.runFor(400);
  o(0xB3, 0);                                             // key off
  m.runFor(1500);
  const L = Float32Array.from(chunks.flatMap(([l]) => [...l]));
  eq('silence before the note', rms(L.subarray(0, 4000)) < 1e-4, true);
  const onset = L.findIndex((v) => Math.abs(v) > 0.01);
  near('note starts at 100 ms (samples)', onset, 4410, 30);
  near('FM peak through the machine (Hz)', peakHz(L.subarray(6000, 6000 + 8820), 44100, 100, 2000), 523.25, 1.5);
  eq('released note decays', rms(L.subarray(L.length - 4410)) < 0.1 * rms(L.subarray(6000, 10000)), true);
  // OPL3 mode: the second register array (0x100+) plays too
  const m2 = mk({ audio: { rate: 44100, onAudio: () => {} } });
  const o3 = (r, v) => { m2.out8(0x38A - (r < 0x100 ? 2 : 0), r & 0xFF); m2.out8(0x38B - (r < 0x100 ? 2 : 0), v); };
  o3(0x105, 1); m2.runFor(1);
  eq('OPL3 NEW bit', m2.sb.opl.chip.newm, 1);
}

console.log(`${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
