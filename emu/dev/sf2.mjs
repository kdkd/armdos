// SoundFont 2 reader (RIFF sfbk: INFO, sdta smpl, pdta phdr/pbag/pmod/pgen/
// inst/ibag/imod/igen/shdr) for the General MIDI synthesizer (gmsynth.mjs).
// Pure JS, no dependencies (runs in node, the page and an AudioWorklet).
//
//   const sf = parseSf2(arrayBufferOrUint8Array);
//   sf.presets: [{ name, program, bank, zones: [zone] , global }]
//   sf.instruments: [{ name, zones, global }]
//   sf.samples: [{ name, start, end, loopStart, loopEnd, rate, rootKey, correction, type, link }]
//   sf.data: Int16Array (all sample points)
// A zone is { gens: Int16Array(61) of set generators, set: Uint8Array(61) (1 = present),
//   mods: [mod], keyLo, keyHi, velLo, velHi, instrument | sample (index) }.
// A modulator is { src, dest, amount, amtSrc, trans }.

export const GEN = {
  startAddrsOffset: 0, endAddrsOffset: 1, startloopAddrsOffset: 2, endloopAddrsOffset: 3,
  startAddrsCoarseOffset: 4, modLfoToPitch: 5, vibLfoToPitch: 6, modEnvToPitch: 7,
  initialFilterFc: 8, initialFilterQ: 9, modLfoToFilterFc: 10, modEnvToFilterFc: 11,
  endAddrsCoarseOffset: 12, modLfoToVolume: 13, chorusEffectsSend: 15, reverbEffectsSend: 16,
  pan: 17, delayModLFO: 21, freqModLFO: 22, delayVibLFO: 23, freqVibLFO: 24,
  delayModEnv: 25, attackModEnv: 26, holdModEnv: 27, decayModEnv: 28, sustainModEnv: 29,
  releaseModEnv: 30, keynumToModEnvHold: 31, keynumToModEnvDecay: 32,
  delayVolEnv: 33, attackVolEnv: 34, holdVolEnv: 35, decayVolEnv: 36, sustainVolEnv: 37,
  releaseVolEnv: 38, keynumToVolEnvHold: 39, keynumToVolEnvDecay: 40,
  instrument: 41, keyRange: 43, velRange: 44, startloopAddrsCoarseOffset: 45, keynum: 46,
  velocity: 47, initialAttenuation: 48, endloopAddrsCoarseOffset: 50, coarseTune: 51,
  fineTune: 52, sampleID: 53, sampleModes: 54, scaleTuning: 56, exclusiveClass: 57,
  overridingRootKey: 58,
};
export const NGEN = 61;

/** Generator defaults (SF2.04 8.1.3). */
export const GEN_DEFAULT = new Int16Array(NGEN);
GEN_DEFAULT[GEN.initialFilterFc] = 13500;
for (const g of [GEN.delayModLFO, GEN.delayVibLFO, GEN.delayModEnv, GEN.attackModEnv, GEN.holdModEnv, GEN.decayModEnv,
  GEN.releaseModEnv, GEN.delayVolEnv, GEN.attackVolEnv, GEN.holdVolEnv, GEN.decayVolEnv, GEN.releaseVolEnv]) GEN_DEFAULT[g] = -12000;
GEN_DEFAULT[GEN.keynum] = -1; GEN_DEFAULT[GEN.velocity] = -1;
GEN_DEFAULT[GEN.scaleTuning] = 100; GEN_DEFAULT[GEN.overridingRootKey] = -1;

function fourcc(u8, o) { return String.fromCharCode(u8[o], u8[o + 1], u8[o + 2], u8[o + 3]); }
function str(u8, o, n) { let s = ''; for (let i = 0; i < n && u8[o + i]; i++) s += String.fromCharCode(u8[o + i]); return s.trim(); }

/** Walk the chunks of a LIST/RIFF body: returns { id: {off, len} } (first of each). */
function chunks(u8, dv, off, end) {
  const out = {};
  while (off + 8 <= end) {
    const id = fourcc(u8, off), len = dv.getUint32(off + 4, true);
    if (id === 'LIST') out['LIST:' + fourcc(u8, off + 8)] = { off: off + 12, len: len - 4 };
    else if (!out[id]) out[id] = { off: off + 8, len };
    off += 8 + len + (len & 1);
  }
  return out;
}

export function parseSf2(buf) {
  const u8 = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  if (fourcc(u8, 0) !== 'RIFF' || fourcc(u8, 8) !== 'sfbk') throw new Error('not a SoundFont 2 file');
  const top = chunks(u8, dv, 12, Math.min(u8.length, 8 + dv.getUint32(4, true)));
  const info = {}, sdta = top['LIST:sdta'], pdta = top['LIST:pdta'];
  if (top['LIST:INFO']) {
    const c = chunks(u8, dv, top['LIST:INFO'].off, top['LIST:INFO'].off + top['LIST:INFO'].len);
    for (const k of Object.keys(c)) if (k !== 'ifil' && k !== 'iver') info[k] = str(u8, c[k].off, c[k].len);
  }
  if (!sdta || !pdta) throw new Error('SoundFont: missing sdta/pdta');
  const sdc = chunks(u8, dv, sdta.off, sdta.off + sdta.len), sd = sdc.smpl, sa = sdc.smpB;
  let data;
  if (sd) {           // copy the sample points (the source may be unaligned)
    data = new Int16Array(sd.len >> 1);
    new Uint8Array(data.buffer).set(u8.subarray(sd.off, sd.off + data.length * 2));
  } else data = new Int16Array(0);
  const p = chunks(u8, dv, pdta.off, pdta.off + pdta.len);
  const rec = (id, size) => { const c = p[id]; if (!c) throw new Error('SoundFont: missing ' + id); return { off: c.off, n: Math.floor(c.len / size) }; };

  const readGens = (id) => { const r = rec(id, 4), g = []; for (let i = 0; i < r.n; i++) { const o = r.off + i * 4; g.push([dv.getUint16(o, true), dv.getInt16(o + 2, true), u8[o + 2], u8[o + 3]]); } return g; };
  const readMods = (id) => { const r = rec(id, 10), m = []; for (let i = 0; i < r.n; i++) { const o = r.off + i * 10; m.push({ src: dv.getUint16(o, true), dest: dv.getUint16(o + 2, true), amount: dv.getInt16(o + 4, true), amtSrc: dv.getUint16(o + 6, true), trans: dv.getUint16(o + 8, true) }); } return m; };
  const readBags = (id) => { const r = rec(id, 4), b = []; for (let i = 0; i < r.n; i++) b.push([dv.getUint16(r.off + i * 4, true), dv.getUint16(r.off + i * 4 + 2, true)]); return b; };

  function zonesOf(bags, gens, mods, first, last, terminal) {
    const zones = []; let global = null;
    for (let b = first; b < last; b++) {
      const z = { gens: new Int16Array(NGEN), set: new Uint8Array(NGEN), mods: [], keyLo: 0, keyHi: 127, velLo: 0, velHi: 127 };
      z[terminal] = -1;
      for (let g = bags[b][0]; g < bags[b + 1][0]; g++) {
        const [op, amt, lo, hi] = gens[g];
        if (op === GEN.keyRange) { z.keyLo = lo; z.keyHi = hi; }
        else if (op === GEN.velRange) { z.velLo = lo; z.velHi = hi; }
        else if (op === (terminal === 'instrument' ? GEN.instrument : GEN.sampleID)) z[terminal] = amt & 0xFFFF;
        else if (op < NGEN) { z.gens[op] = amt; z.set[op] = 1; }
      }
      for (let m = bags[b][1]; m < bags[b + 1][1]; m++) z.mods.push(mods[m]);
      if (z[terminal] < 0) { if (b === first) global = z; continue; }     // only the first zone may be global
      zones.push(z);
    }
    return { zones, global };
  }

  // samples
  const sh = rec('shdr', 46), samples = [];
  for (let i = 0; i < sh.n - 1; i++) {
    const o = sh.off + i * 46;
    samples.push({ name: str(u8, o, 20), start: dv.getUint32(o + 20, true), end: dv.getUint32(o + 24, true),
      loopStart: dv.getUint32(o + 28, true), loopEnd: dv.getUint32(o + 32, true), rate: dv.getUint32(o + 36, true),
      rootKey: u8[o + 40], correction: dv.getInt8(o + 41), link: dv.getUint16(o + 42, true), type: dv.getUint16(o + 44, true) });
  }
  if (sa) data = decodeSmpB(u8, dv, sa, samples);
  // instruments
  const ibag = readBags('ibag'), igen = readGens('igen'), imod = readMods('imod');
  const ih = rec('inst', 22), instruments = [];
  for (let i = 0; i < ih.n - 1; i++) {
    const o = ih.off + i * 22;
    const b0 = dv.getUint16(o + 20, true), b1 = dv.getUint16(o + 42, true);
    instruments.push({ name: str(u8, o, 20), ...zonesOf(ibag, igen, imod, b0, b1, 'sample') });
  }
  // presets
  const pbag = readBags('pbag'), pgen = readGens('pgen'), pmod = readMods('pmod');
  const ph = rec('phdr', 38), presets = [];
  for (let i = 0; i < ph.n - 1; i++) {
    const o = ph.off + i * 38;
    const b0 = dv.getUint16(o + 24, true), b1 = dv.getUint16(o + 38 + 24, true);
    presets.push({ name: str(u8, o, 20), program: dv.getUint16(o + 20, true), bank: dv.getUint16(o + 22, true), ...zonesOf(pbag, pgen, pmod, b0, b1, 'instrument') });
  }
  return { info, presets, instruments, samples, data };
}

// ---- ADPCM sample chunk 'smpB' (written by apps/midi/tools/mksf.mjs): 4.5 bits
// a point. "SMPB", u32 total points, then per sample (shdr order, predictor
// reset at each) blocks of 16 points, 9 bytes each: a header byte (bits 7-5 =
// predictor f, bits 4-0 = step index s), then 16 signed 4-bit codes c, low
// nibble first. y = clamp((C1[f] * y1 + C2[f] * y2 >> 6) + c * STEP[s]).
// (A block-adaptive second-order ADPCM in the style of CD-ROM XA; ~34 dB SNR on
// this bank against ~24 dB for IMA ADPCM at nearly the same size.)
export const ADPCM_C1 = new Int16Array([0, 60, 115, 98, 122, 124, 104, 88]);
export const ADPCM_C2 = new Int16Array([0, 0, -52, -55, -60, -62, -40, -24]);
export const ADPCM_STEP = Int16Array.from({ length: 25 }, (_, k) => Math.round(2 ** (k / 2)));
export const adpcmBytes = (n) => ((n + 15) >> 4) * 9;

/** Decode n points from u8 at byte offset ro into out at o. */
export function adpcmDecode(u8, ro, n, out, o) {
  let y1 = 0, y2 = 0;
  for (let b = 0; b < n; b += 16) {
    const h = u8[ro], c1 = ADPCM_C1[h >> 5], c2 = ADPCM_C2[h >> 5], st = ADPCM_STEP[(h & 31) > 24 ? 24 : h & 31];
    const m = n - b < 16 ? n - b : 16;
    for (let i = 0; i < m; i++) {
      let c = (u8[ro + 1 + (i >> 1)] >> ((i & 1) << 2)) & 15;
      if (c & 8) c -= 16;
      let y = ((c1 * y1 + c2 * y2) >> 6) + c * st;
      if (y > 32767) y = 32767; else if (y < -32768) y = -32768;
      out[o + b + i] = y; y2 = y1; y1 = y;
    }
    ro += 9;
  }
}

/** Encode Int16Array pcm (one sample); returns { bytes: Uint8Array, err } (err = squared error sum). */
export function adpcmEncode(pcm) {
  const n = pcm.length, out = new Uint8Array(adpcmBytes(n)), q = new Int8Array(16), bq = new Int8Array(16);
  let y1 = 0, y2 = 0, err = 0;
  for (let b = 0, ro = 0; b < n; b += 16, ro += 9) {
    let best = Infinity, bh = 0, b1 = 0, b2 = 0;
    for (let f = 0; f < 8; f++) for (let s = 0; s < 25; s++) {
      const c1 = ADPCM_C1[f], c2 = ADPCM_C2[f], st = ADPCM_STEP[s];
      let a1 = y1, a2 = y2, e = 0;
      for (let i = 0; i < 16; i++) {
        const x = b + i < n ? pcm[b + i] : 0, pr = (c1 * a1 + c2 * a2) >> 6;
        let c = Math.round((x - pr) / st); if (c > 7) c = 7; else if (c < -8) c = -8;
        let y = pr + c * st; if (y > 32767) y = 32767; else if (y < -32768) y = -32768;
        if (b + i < n) e += (y - x) * (y - x);
        if (e >= best) break;
        q[i] = c; a2 = a1; a1 = y;
      }
      if (e < best) { best = e; bh = f << 5 | s; b1 = a1; b2 = a2; bq.set(q); }
    }
    out[ro] = bh;
    for (let i = 0; i < 16; i++) out[ro + 1 + (i >> 1)] |= (bq[i] & 15) << ((i & 1) << 2);
    y1 = b1; y2 = b2; err += best;
  }
  return { bytes: out, err };
}

function decodeSmpB(u8, dv, c, samples) {
  const out = new Int16Array(dv.getUint32(c.off + 4, true));
  let ro = c.off + 8;
  for (const s of samples) { const n = s.end - s.start; adpcmDecode(u8, ro, n, out, s.start); ro += adpcmBytes(n); }
  return out;
}
