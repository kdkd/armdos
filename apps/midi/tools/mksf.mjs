#!/usr/bin/env node
// apps/midi/tools/mksf.mjs - make the ARM-PC's General MIDI sound set
// (3rdparty/midi/ARMGS.SFA) from GeneralUser GS 2.0.3 (S. Christian Collins,
// licence in apps/midi/sf/GeneralUser-GS-LICENSE.txt, which permits modifying
// and redistributing it).
//
//   node apps/midi/tools/mksf.mjs GeneralUser-GS.sf2 3rdparty/midi/ARMGS.SFA
//
// What it does to the bank (every preset, every GS variation and drum kit is kept):
//  * samples above 22050 Hz are resampled to about 22050 Hz (windowed-sinc low
//    pass, loops resampled as loops: the new rate is chosen so that each loop is
//    a whole number of points long and plays at exactly the original pitch);
//  * continuously looped samples lose the tail after their loop (never played);
//  * the sample points are stored as 4.5-bit block ADPCM (chunk 'smpB' in place
//    of 'smpl', format in emu/dev/sf2.mjs, which decodes it at load).
// The preset/instrument data (pdta) is copied unchanged apart from the sample headers.
import fs from 'node:fs';
import { parseSf2, adpcmEncode } from '../../../emu/dev/sf2.mjs';

const [src, dst, capArg] = process.argv.slice(2);
if (!src || !dst) { console.error('usage: mksf.mjs in.sf2 out.sfa [maxRate]'); process.exit(2); }
const CAP = +(capArg || 22050);
const raw = fs.readFileSync(src);
const sf = parseSf2(raw);

// sample modes used per sample: 1 = only continuous loops (tail can go), else keep the tail
const modes = new Map();
for (const ins of sf.instruments) for (const z of ins.zones) {
  const m = z.set[54] ? z.gens[54] : (ins.global && ins.global.set[54] ? ins.global.gens[54] : 0);
  modes.set(z.sample, (modes.get(z.sample) || 0) | (1 << (m & 3)));
}

// ---- windowed-sinc resampler
const TAPS = 16;            // half width in source points (at ratio 1)
function resample(get, n0, k, nOut, periodic) {
  // out[i] = sum src[j] * h((i*k - j) / scale), scale = max(1, k) for the anti-alias low pass
  const out = new Float32Array(nOut), scale = Math.max(1, k), fc = 0.95 / scale, W = Math.ceil(TAPS * scale);
  for (let i = 0; i < nOut; i++) {
    const x = i * k, j0 = Math.floor(x);
    let acc = 0, wsum = 0;
    for (let j = j0 - W + 1; j <= j0 + W; j++) {
      const d = x - j;
      const t = d / W;
      if (t <= -1 || t >= 1) continue;
      const win = 0.42 + 0.5 * Math.cos(Math.PI * t) + 0.08 * Math.cos(2 * Math.PI * t);   // Blackman
      const a = Math.PI * d * fc, s = a === 0 ? 1 : Math.sin(a) / a;
      const h = fc * s * win;
      acc += h * get(j, periodic); wsum += h;
    }
    out[i] = wsum ? acc / wsum : 0;
  }
  return out;
}

const outSamples = [];   // { hdr, pcm: Int16Array }
let resampled = 0, cut = 0;
for (let si = 0; si < sf.samples.length; si++) {
  const s = sf.samples[si];
  const m = modes.get(si) || 1;                          // unused samples: keep as they are
  const loopOnly = m === (1 << 1);                       // used only with sampleModes 1
  const src16 = sf.data.subarray(s.start, s.end);
  const n = s.end - s.start, ls = s.loopStart - s.start, le = s.loopEnd - s.start;
  const looped = (m & (1 << 1 | 1 << 3)) && le > ls + 1 && ls >= 0 && le <= n;
  let rate = s.rate, pcm, nls = ls, nle = le;
  if (s.rate > CAP * 1.02 && n > 32) {
    // new loop length a whole number of points: rate' = rate * L'/L
    let k = s.rate / CAP;
    if (looped) { const L = le - ls, L2 = Math.max(2, Math.round(L / k)); k = L / L2; }
    rate = Math.round(s.rate / k);
    const get = (j, periodic) => {
      if (periodic && j >= le) j = ls + ((j - ls) % (le - ls));
      return j < 0 || j >= n ? 0 : src16[j];
    };
    nls = looped ? Math.round(ls / k) : Math.round(ls / k);
    nle = looped ? nls + Math.round((le - ls) / k) : Math.round(le / k);
    const nOut = looped && loopOnly ? nle + 8 : Math.round(n / k);
    // the loop part must be periodic: positions >= loop start are computed relative to it
    const body = resample(get, n, k, Math.min(nOut, nle + 8), looped);
    let tail = null;
    if (nOut > nle + 8) {
      // (sampleModes 3) the release tail after the loop end, not periodic
      const t2 = resample((j) => (j < 0 || j >= n ? 0 : src16[j]), n, k, nOut, false);
      tail = t2.subarray(nle + 8);
    }
    pcm = new Int16Array(nOut);
    for (let i = 0; i < body.length; i++) pcm[i] = Math.max(-32768, Math.min(32767, Math.round(body[i])));
    if (tail) for (let i = 0; i < tail.length; i++) pcm[nle + 8 + i] = Math.max(-32768, Math.min(32767, Math.round(tail[i])));
    if (looped) for (let i = 0; i < 8 && nle + i < pcm.length; i++) pcm[nle + i] = pcm[nls + i];   // guard points = loop start
    resampled++;
  } else {
    let nOut = n;
    if (looped && loopOnly && le + 8 < n) { nOut = le + 8; cut++; }
    pcm = Int16Array.from(src16.subarray(0, nOut));
    if (looped) for (let i = 0; i < 8 && le + i < pcm.length; i++) pcm[le + i] = pcm[ls + i];
  }
  outSamples.push({ s, rate, pcm, ls: nls, le: nle });
}

// ---- layout: 46 zero points after each sample (SF2 rule), ADPCM runs
let pos = 0;
const shdr = Buffer.alloc(46 * (outSamples.length + 1));
const runs = [];
let errSum = 0, sigSum = 0;
outSamples.forEach((o, i) => {
  const b = i * 46, s = o.s;
  shdr.write(s.name.padEnd(20, '\0').slice(0, 20), b, 'latin1');
  const start = pos, end = pos + o.pcm.length;
  shdr.writeUInt32LE(start, b + 20); shdr.writeUInt32LE(end, b + 24);
  shdr.writeUInt32LE(start + Math.max(0, o.ls), b + 28); shdr.writeUInt32LE(start + Math.max(0, o.le), b + 32);
  shdr.writeUInt32LE(o.rate, b + 36); shdr[b + 40] = s.rootKey; shdr.writeInt8(s.correction, b + 41);
  shdr.writeUInt16LE(s.link, b + 42); shdr.writeUInt16LE(s.type, b + 44);
  const enc = adpcmEncode(o.pcm);
  runs.push(enc.bytes); errSum += enc.err;
  for (let k = 0; k < o.pcm.length; k++) sigSum += o.pcm[k] * o.pcm[k];
  pos = end + 46;
});
shdr.write('EOS', outSamples.length * 46, 'latin1');
const totalPoints = pos;

// ---- write the RIFF
const u8 = new Uint8Array(raw.buffer, raw.byteOffset, raw.byteLength), dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
const cc = (o) => String.fromCharCode(u8[o], u8[o + 1], u8[o + 2], u8[o + 3]);
function list(off, end) { const out = []; while (off + 8 <= end) { const id = cc(off), len = dv.getUint32(off + 4, true); out.push({ id, off, len, sub: id === 'LIST' ? cc(off + 8) : null }); off += 8 + len + (len & 1); } return out; }
const chunk = (id, body) => { const h = Buffer.alloc(8); h.write(id, 0, 'latin1'); h.writeUInt32LE(body.length, 4); return Buffer.concat([h, body, body.length & 1 ? Buffer.alloc(1) : Buffer.alloc(0)]); };
const LIST = (type, parts) => chunk('LIST', Buffer.concat([Buffer.from(type, 'latin1'), ...parts]));
const top = list(12, 8 + dv.getUint32(4, true));
const infoC = top.find((c) => c.sub === 'INFO'), pdtaC = top.find((c) => c.sub === 'pdta');
const zstr = (t) => { const b = Buffer.from(t + '\0', 'latin1'); return b.length & 1 ? Buffer.concat([b, Buffer.alloc(1)]) : b; };
const infoParts = list(infoC.off + 12, infoC.off + 8 + infoC.len).map((c) => {
  const orig = raw.subarray(c.off + 8, c.off + 8 + c.len).toString('latin1').replace(/\0+$/, '');
  if (c.id === 'INAM') return chunk('INAM', zstr('ARM-PC GS (GeneralUser GS 2.0.3, ADPCM)'));
  if (c.id === 'ICMT') return chunk('ICMT', zstr('Modified for the ARM-PC (ARM-DOS project): samples resampled to <= 22050 Hz, continuous-loop tails removed, 4.5-bit block ADPCM sample chunk smpB (see emu/dev/sf2.mjs). Original: GeneralUser GS 2.0.3 by S. Christian Collins.\n\n' + orig));
  return Buffer.from(raw.subarray(c.off, c.off + 8 + c.len + (c.len & 1)));
});
const pdtaParts = list(pdtaC.off + 12, pdtaC.off + 8 + pdtaC.len).map((c) => c.id === 'shdr' ? chunk('shdr', shdr) : Buffer.from(raw.subarray(c.off, c.off + 8 + c.len + (c.len & 1))));
const hdr = Buffer.alloc(8); hdr.write('SMPB', 0, 'latin1'); hdr.writeUInt32LE(totalPoints, 4);
const smpA = chunk('smpB', Buffer.concat([hdr, ...runs.map((r) => Buffer.from(r.buffer, r.byteOffset, r.byteLength))]));
const body = Buffer.concat([Buffer.from('sfbk', 'latin1'), LIST('INFO', infoParts), LIST('sdta', [smpA]), LIST('pdta', pdtaParts)]);
fs.writeFileSync(dst, chunk('RIFF', body));
// verify it parses
const back = parseSf2(fs.readFileSync(dst));
console.log(`mksf: ${sf.samples.length} samples (${resampled} resampled to <= ${CAP} Hz, ${cut} loop tails cut), ${(totalPoints / 1e6).toFixed(2)} M points,`
  + ` ${(fs.statSync(dst).size / 1048576).toFixed(2)} MB; ADPCM SNR ${(10 * Math.log10(sigSum / errSum)).toFixed(1)} dB; ${back.presets.length} presets`);
