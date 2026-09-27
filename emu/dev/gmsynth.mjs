// A General MIDI / Roland GS software synthesizer playing SoundFont 2 banks:
// the ARM-PC's "Sound Canvas" (behind the MPU-401, dev/mpu401.mjs). Pure JS,
// no dependencies: runs in node (tests, headless --wav), on the page's main
// thread and inside an AudioWorklet (web/js/audio-gm-worklet.js).
//
//   const syn = new GmSynth(48000);
//   syn.loadSoundFont(parseSf2(bytes));          // dev/sf2.mjs
//   syn.midiByte(0x90); syn.midiByte(60); syn.midiByte(100);   // a raw MIDI byte stream
//   syn.render(L, R, offset, n, gain);           // ADDS n frames into L/R at offset
//
// What it implements (SoundFont 2.04 semantics as FluidSynth has them, which
// is what GeneralUser GS is voiced for):
//  * preset/instrument zones (key/velocity ranges, global zones), every
//    generator that makes sound: sample offsets, loops (modes 0/1/3), root key,
//    tuning, scale tuning, exclusive class, volume envelope (delay, attack,
//    hold, decay, sustain, release, key scaling), modulation envelope and the
//    two LFOs (to pitch, filter and volume), the resonant low-pass filter, pan,
//    reverb and chorus sends, initial attenuation (x0.4, the EMU convention);
//  * modulators: the ten SF2 default modulators plus the bank's own (override
//    and addition rules, all source curves, the amount source);
//  * MIDI: note on/off with velocity, program change, bank select (GS
//    variations with fallback to the capital tone), channel pressure, pitch
//    bend with RPN 0 range, RPN 1/2 fine/coarse tune, CC 1/7/10/11/64/66/67/
//    91/93/120/121/123-127, running status, SysEx GM on, GS reset, XG reset,
//    GS "use for rhythm part", master volume; channel 10 is the drum kit;
//  * 64 voices with stealing (released and quietest first), linear
//    interpolation, a Freeverb-style reverb and a stereo chorus.
// Output level: a single full-velocity piano note is about 0.1-0.2 peak, like an
// OPL3 voice in dev/audio.mjs' calibration.

import { GEN, GEN_DEFAULT, NGEN } from './sf2.mjs';

const MAX_VOICES = 64;
const BLOCK = 32;                 // control-rate block (samples)
const OUT_GAIN = 1.4;             // overall synth level (a mezzo-forte note at CC7 100 peaks near 0.1; full songs stay below 1)
const EMU_ATTEN = 0.4;            // FluidSynth / EMU10K initial attenuation factor

const tc2sec = (tc) => (tc <= -32768 ? 0 : Math.pow(2, tc / 1200));
const abscentHz = (c) => 8.176 * Math.pow(2, c / 1200);

// ---- modulator source curves (index 0..127 of x = v/128)
const CONCAVE = new Float64Array(129), CONVEX = new Float64Array(129);
for (let i = 0; i <= 128; i++) {
  const x = i / 128;
  CONCAVE[i] = x >= 1 ? 1 : Math.min(1, Math.max(0, -(5 / 6) * Math.log10(1 - x)));
  CONVEX[i] = 0;
}
for (let i = 0; i <= 128; i++) CONVEX[i] = 1 - CONCAVE[128 - i];
function curve(x, type) {       // x in [0,1]
  if (type === 0) return x;
  if (type === 3) return x >= 0.5 ? 1 : 0;
  const f = x * 128, i = Math.floor(f), t = f - i, tab = type === 1 ? CONCAVE : CONVEX;
  if (i >= 128) return tab[128];
  return tab[i] + (tab[i + 1] - tab[i]) * t;
}

// default modulators (SF2.04 8.4.1; velocity->filter as FluidSynth, only above velocity 64; CC 7 and
// CC 11 with the General MIDI volume curve of a Sound Canvas, 40 log10(v/127) dB, instead of the
// SF2 default's 80 log10 - DOOM's music at CC 7 = 50 is then -16 dB, as on the SC-55, not -32 dB)
const DEFAULT_MODS = [
  { src: 0x0502, dest: GEN.initialAttenuation, amount: 960, amtSrc: 0x0000, trans: 0 },
  { src: 0x0102, dest: GEN.initialFilterFc, amount: -2400, amtSrc: 0x0C02, trans: 0 },
  { src: 0x000D, dest: GEN.vibLfoToPitch, amount: 50, amtSrc: 0x0000, trans: 0 },
  { src: 0x0081, dest: GEN.vibLfoToPitch, amount: 50, amtSrc: 0x0000, trans: 0 },
  { src: 0x0587, dest: GEN.initialAttenuation, amount: 480, amtSrc: 0x0000, trans: 0 },   // 480: the GM/Sound Canvas curve, 40 log10(v/127) dB
  { src: 0x028A, dest: GEN.pan, amount: 1000, amtSrc: 0x0000, trans: 0 },
  { src: 0x058B, dest: GEN.initialAttenuation, amount: 480, amtSrc: 0x0000, trans: 0 },   // (SF2/FluidSynth: 960, 80 log10)
  { src: 0x00DB, dest: GEN.reverbEffectsSend, amount: 200, amtSrc: 0x0000, trans: 0 },
  { src: 0x00DD, dest: GEN.chorusEffectsSend, amount: 200, amtSrc: 0x0000, trans: 0 },
  { src: 0x020E, dest: GEN.fineTune, amount: 12700, amtSrc: 0x0010, trans: 0 },
];
const sameMod = (a, b) => a.src === b.src && a.dest === b.dest && a.amtSrc === b.amtSrc && a.trans === b.trans;
// A bank's velocity->filter modulator replaces the default one whatever its amount source: banks
// voiced for FluidSynth cancel it with an amount of 0 (GeneralUser GS does, in ~600 zones).
const overrides = (d, m) => sameMod(d, m) || (d === DEFAULT_MODS[1] && m.src === 0x0102 && m.dest === GEN.initialFilterFc);

// generators the preset level may add to (SF2 8.5: not the address offsets, key/vel ranges, keynum, velocity, sample modes, exclusive class, root key)
const PRESET_OK = new Uint8Array(NGEN).fill(1);
for (const g of [0, 1, 2, 3, 4, 12, 45, 50, 46, 47, 54, 57, 58, 43, 44, 41, 53]) PRESET_OK[g] = 0;

// ---------------------------------------------------------------- channels
class Channel {
  constructor(n) { this.n = n; this.reset(true); }
  reset(full) {
    this.cc = new Uint8Array(128);
    const c = this.cc;
    c[7] = 100; c[10] = 64; c[11] = 127; c[91] = 40; c[93] = 0; c[100] = c[101] = c[98] = c[99] = 127;
    this.bend = 8192; this.pressure = 0; this.bendRange = 2; this.fineTune = 0; this.coarseTune = 0;
    this.program = 0; this.bank = 0; this.drum = this.n === 9;
    this.preset = null; this.sustained = [];
    this.rpnMode = 0;         // 1 = RPN selected, 2 = NRPN
  }
  resetControllers() {
    const c = this.cc;
    c[1] = 0; c[11] = 127; c[64] = 0; c[65] = 0; c[66] = 0; c[67] = 0; c[100] = c[101] = c[98] = c[99] = 127;
    this.bend = 8192; this.pressure = 0; this.rpnMode = 0;
  }
}

// ---------------------------------------------------------------- voices
class Voice {
  constructor() {
    this.on = false; this.g = new Float64Array(NGEN); this.base = new Float64Array(NGEN); this.mods = [];
    this.x1 = 0; this.x2 = 0; this.y1 = 0; this.y2 = 0;
  }
}

export class GmSynth {
  constructor(rate = 44100, { voices = MAX_VOICES } = {}) {
    this.rate = rate;
    this.sf = null; this.data = null;
    this.ch = Array.from({ length: 16 }, (_, i) => new Channel(i));
    this.voices = Array.from({ length: voices }, () => new Voice());
    this.masterVol = 1;
    this.noteCount = 0;             // note-ons received (tests, meters)
    this.meter = new Float32Array(16);   // per-channel activity (last velocity x volume, decays)
    this.onNote = null;             // optional hook (ch, key, vel) for tests
    this.stateOnly = false;         // true: update channel state, start no voices
    // parser
    this.status = 0; this.need = 0; this.d = [0, 0]; this.dn = 0; this.sysex = null; this.skip = false;
    this.voiceSerial = 0; this.noteSerial = 0;
    this.initEffects();
  }

  /** Change the output rate (drops sounding voices). */
  setRate(rate) { this.allSoundOff(); this.rate = rate; this.initEffects(); }

  loadSoundFont(sf) {
    this.allSoundOff();
    this.sf = sf; this.data = sf.data;
    this.presetMap = new Map();
    for (const p of sf.presets) { const k = p.bank * 128 + p.program; if (!this.presetMap.has(k)) this.presetMap.set(k, p); }
    for (const c of this.ch) c.preset = null;
  }
  get loaded() { return !!this.sf; }

  findPreset(c) {
    if (!this.sf) return null;
    const m = this.presetMap;
    if (c.drum) return m.get(128 * 128 + c.program) || m.get(128 * 128) || null;
    return m.get(c.bank * 128 + c.program) || m.get(c.program) || m.values().next().value;
  }

  // ------------------------------------------------------------ MIDI input
  /** Feed one byte of a MIDI stream. */
  midiByte(b) {
    b &= 0xFF;
    if (b >= 0xF8) return;                                   // realtime: ignored
    if (this.sysex) {
      if (b === 0xF7) { this.handleSysex(this.sysex); this.sysex = null; return; }
      if (b & 0x80) this.sysex = null;                       // aborted by a status byte
      else { if (this.sysex.length < 512) this.sysex.push(b); return; }
    }
    if (b & 0x80) {
      if (b === 0xF0) { this.sysex = []; this.status = 0; return; }
      if (b >= 0xF0) { this.status = 0; this.need = b === 0xF2 ? 2 : b === 0xF1 || b === 0xF3 ? 1 : 0; this.skip = true; this.dn = 0; return; }
      this.status = b; this.dn = 0; this.skip = false;
      this.need = (b & 0xE0) === 0xC0 ? 1 : 2;
      return;
    }
    if (this.skip) { if (++this.dn >= this.need) { this.dn = 0; this.need = 0; } return; }
    if (!this.status) return;
    this.d[this.dn++] = b;
    if (this.dn >= this.need) { this.dn = 0; this.message(this.status, this.d[0], this.d[1]); }
  }
  midiBytes(arr) { for (const b of arr) this.midiByte(b); }

  message(st, d1, d2) {
    const c = this.ch[st & 15];
    switch (st & 0xF0) {
      case 0x80: this.noteOff(c, d1); break;
      case 0x90: if (d2) this.noteOn(c, d1, d2); else this.noteOff(c, d1); break;
      case 0xA0: break;                                      // poly pressure: not used by GM
      case 0xB0: this.control(c, d1, d2); break;
      case 0xC0: c.program = d1; c.preset = null; break;
      case 0xD0: c.pressure = d1; this.updateChannel(c); break;
      case 0xE0: c.bend = d1 | (d2 << 7); this.updateChannel(c); break;
    }
  }

  control(c, n, v) {
    const cc = c.cc;
    cc[n] = v;
    switch (n) {
      case 0: c.bank = v; c.preset = null; return;           // GS: MSB = variation
      case 6: case 38: this.dataEntry(c); return;
      case 96: case 97: {                                      // data increment/decrement
        if (c.rpnMode === 1 && cc[101] === 0 && cc[100] === 0) { c.bendRange = Math.max(0, Math.min(24, c.bendRange + (n === 96 ? 1 : -1))); this.updateChannel(c); }
        return;
      }
      case 98: case 99: c.rpnMode = 2; return;
      case 100: case 101: c.rpnMode = 1; return;
      case 64: if (v < 64) this.releaseSustained(c); return;
      case 120: this.channelSoundOff(c); return;
      case 121: c.resetControllers(); this.releaseSustained(c); this.updateChannel(c); return;
      case 123: case 124: case 125: case 126: case 127: this.channelNotesOff(c); return;
    }
    this.updateChannel(c);
  }
  dataEntry(c) {
    if (c.rpnMode !== 1) return;
    const cc = c.cc, msb = cc[6], lsb = cc[38];
    if (cc[101] !== 0) return;
    switch (cc[100]) {
      case 0: c.bendRange = msb + lsb / 100; break;
      case 1: c.fineTune = (((msb << 7) | lsb) - 8192) / 8192 * 100; break;
      case 2: c.coarseTune = msb - 64; break;
      default: return;
    }
    this.updateChannel(c);
  }

  handleSysex(s) {
    // universal non-realtime GM system on/off
    if (s[0] === 0x7E && s[2] === 0x09 && (s[3] === 0x01 || s[3] === 0x03)) { this.resetAll(false); return; }
    if (s[0] === 0x7E && s[2] === 0x09 && s[3] === 0x02) { this.resetAll(false); return; }
    // universal realtime master volume
    if (s[0] === 0x7F && s[2] === 0x04 && s[3] === 0x01 && s.length >= 6) { this.masterVol = ((s[5] << 7) | s[4]) / 16383; return; }
    // Roland GS: 41 dev 42 12 addr(3) data... sum
    if (s[0] === 0x41 && s[2] === 0x42 && s[3] === 0x12 && s.length >= 8) {
      const a = (s[4] << 16) | (s[5] << 8) | s[6], v = s[7];
      if (a === 0x40007F) { this.resetAll(true); return; }                // GS reset
      if (a === 0x400004) { this.masterVol = v / 127; return; }            // master volume
      if ((a & 0xFFF0FF) === 0x401015) {                                   // use for rhythm part
        const p = (a >> 8) & 15, n = p === 0 ? 9 : p <= 9 ? p - 1 : p;
        this.ch[n].drum = v !== 0; this.ch[n].preset = null; return;
      }
      return;
    }
    // Yamaha XG system on: treat as a reset
    if (s[0] === 0x43 && (s[1] & 0xF0) === 0x10 && s[2] === 0x4C && s[3] === 0 && s[4] === 0 && s[5] === 0x7E) this.resetAll(false);
  }

  resetAll() {
    this.allSoundOff();
    for (const c of this.ch) c.reset(true);
    this.masterVol = 1;
  }
  /** Power-on / hardware reset. */
  reset() { this.resetAll(); this.status = 0; this.sysex = null; this.dn = 0; this.meter.fill(0); this.clearEffects(); }

  // ------------------------------------------------------------ notes
  noteOn(c, key, vel) {
    this.noteCount++; this.noteSerial++;
    if (this.onNote) this.onNote(c.n, key, vel);
    const lvl = vel / 127 * c.cc[7] / 127 * c.cc[11] / 127;
    if (lvl > this.meter[c.n]) this.meter[c.n] = lvl;
    if (this.stateOnly || !this.sf) return;
    if (!c.preset) c.preset = this.findPreset(c);
    const p = c.preset; if (!p) return;
    const sf = this.sf;
    for (const pz of p.zones) {
      if (key < pz.keyLo || key > pz.keyHi || vel < pz.velLo || vel > pz.velHi) continue;
      const inst = sf.instruments[pz.instrument]; if (!inst) continue;
      for (const iz of inst.zones) {
        if (key < iz.keyLo || key > iz.keyHi || vel < iz.velLo || vel > iz.velHi) continue;
        this.startVoice(c, key, vel, p, pz, inst, iz);
      }
    }
  }

  startVoice(c, key, vel, preset, pz, inst, iz) {
    const s = this.sf.samples[iz.sample]; if (!s || s.end <= s.start) return;
    const v = this.allocVoice();
    const base = v.base;
    base.set(GEN_DEFAULT);
    const ig = inst.global;
    for (let g = 0; g < NGEN; g++) {
      if (iz.set[g]) base[g] = iz.gens[g]; else if (ig && ig.set[g]) base[g] = ig.gens[g];
    }
    const pg = preset.global;
    for (let g = 0; g < NGEN; g++) {
      if (!PRESET_OK[g]) continue;
      if (pz.set[g]) base[g] += pz.gens[g]; else if (pg && pg.set[g]) base[g] += pg.gens[g];
    }
    // modulators: defaults <- instrument global <- instrument zone; then + preset global/zone
    const mods = DEFAULT_MODS.slice();
    const override = (list, add) => {
      for (const m of list) {
        const i = mods.findIndex((x) => overrides(x, m));
        if (i >= 0 && !add) mods[i] = m; else mods.push(m);
      }
    };
    const localOver = (glob, loc) => {             // a zone's modulator replaces the same one of its global zone
      const out = glob ? glob.mods.filter((g) => !loc.mods.some((l) => sameMod(g, l))) : [];
      return out.concat(loc.mods);
    };
    override(localOver(ig, iz), false);
    const pm = localOver(pg, pz);
    v.mods = mods.filter((m) => m.amount !== 0 && m.dest < NGEN);
    v.pmods = pm.filter((m) => m.amount !== 0 && m.dest < NGEN);

    v.on = true; v.ch = c; v.key = key; v.vel = vel; v.sample = s; v.id = ++this.voiceSerial;
    v.keyEff = base[GEN.keynum] >= 0 ? base[GEN.keynum] : key;
    v.velEff = base[GEN.velocity] >= 0 ? base[GEN.velocity] : vel;
    v.released = false; v.sustainHeld = false;
    v.x1 = v.x2 = v.y1 = v.y2 = 0; v.filtFc = -1; v.filtQ = -1;
    v.lfoM = 0; v.lfoV = 0; v.lfoMdir = 1; v.lfoVdir = 1; v.t = 0;
    v.ampCur = 0;
    this.computeMods(v);
    const g = v.g;
    // sample addressing
    v.start = s.start + g[GEN.startAddrsOffset] + 32768 * g[GEN.startAddrsCoarseOffset];
    v.end = s.end + g[GEN.endAddrsOffset] + 32768 * g[GEN.endAddrsCoarseOffset];
    v.loopStart = s.loopStart + g[GEN.startloopAddrsOffset] + 32768 * g[GEN.startloopAddrsCoarseOffset];
    v.loopEnd = s.loopEnd + g[GEN.endloopAddrsOffset] + 32768 * g[GEN.endloopAddrsCoarseOffset];
    const dlen = this.data.length;
    if (v.end > dlen - 1) v.end = dlen - 1;
    if (v.start < 0) v.start = 0; if (v.start >= v.end) v.start = v.end - 1;
    v.mode = g[GEN.sampleModes] & 3;
    if (v.mode === 2) v.mode = 0;
    if (v.loopStart < v.start) v.loopStart = v.start; if (v.loopEnd > v.end) v.loopEnd = v.end;
    if (v.loopEnd - v.loopStart < 2) v.mode = 0;
    v.pos = v.start;
    v.root = g[GEN.overridingRootKey] >= 0 ? g[GEN.overridingRootKey] : s.rootKey;
    v.rateRatio = s.rate / this.rate;
    // exclusive class: cut other voices of the same class on this channel
    const ex = g[GEN.exclusiveClass];
    v.excl = ex;
    if (ex) for (const o of this.voices) if (o !== v && o.on && o.ch === c && o.excl === ex && o.noteStart !== this.noteSerial) this.fastRelease(o);
    v.noteStart = this.noteSerial;
    // envelopes
    this.initEnvelopes(v);
    this.updateVoice(v);
  }

  allocVoice() {
    let best = null, bestScore = Infinity;
    for (const v of this.voices) {
      if (!v.on) return v;
      // prefer: finished release, then released, then quietest, then oldest
      const score = (v.released ? 0 : 2) + v.ampCur + (v.volStage === 5 ? -1 : 0);
      if (score < bestScore) { bestScore = score; best = v; }
    }
    best.on = false;
    return best;
  }

  noteOff(c, key) {
    for (const v of this.voices) {
      if (!v.on || v.ch !== c || v.key !== key || v.released) continue;
      if (c.cc[64] >= 64) { v.sustainHeld = true; continue; }
      this.release(v);
    }
  }
  releaseSustained(c) {
    for (const v of this.voices) if (v.on && v.ch === c && v.sustainHeld && !v.released) { v.sustainHeld = false; this.release(v); }
  }
  channelNotesOff(c) {
    for (const v of this.voices) if (v.on && v.ch === c && !v.released) { if (c.cc[64] >= 64) v.sustainHeld = true; else this.release(v); }
  }
  channelSoundOff(c) { for (const v of this.voices) if (v.on && v.ch === c) v.on = false; }
  allSoundOff() { for (const v of this.voices) v.on = false; }
  get activeVoices() { let n = 0; for (const v of this.voices) if (v.on) n++; return n; }

  // ------------------------------------------------------------ modulation
  srcValue(v, src) {
    const idx = src & 0x7F, cc = (src & 0x80) !== 0, dir = (src >> 8) & 1, bip = (src >> 9) & 1, type = (src >> 10) & 0x3F;
    let x;
    if (cc) x = v.ch.cc[idx] / 128;
    else switch (idx) {
      case 0: return 1;                       // no controller
      case 2: x = v.vel / 128; break;
      case 3: x = v.key / 128; break;
      case 10: x = 0; break;                  // poly pressure
      case 13: x = v.ch.pressure / 128; break;
      case 14: x = v.ch.bend / 16384; break;
      case 16: x = v.ch.bendRange / 127; break;   // (12700 x range/127 = range semitones exactly)
      default: return 0;
    }
    if (dir) x = 1 - x;
    if (bip) {
      if (type === 3) return x >= 0.5 ? 1 : -1;
      // bipolar: -1..1 through the curve on each half
      const y = x * 2 - 1;
      return type === 0 ? y : Math.sign(y) * curve(Math.abs(y), type);
    }
    return curve(x, type);
  }
  computeMods(v) {
    const g = v.g; g.set(v.base);
    g[GEN.initialAttenuation] *= EMU_ATTEN;
    for (const list of [v.mods, v.pmods]) for (const m of list) {
      const a = this.srcValue(v, m.src);
      if (a === 0) continue;
      const b = m.amtSrc ? this.srcValue(v, m.amtSrc) : 1;
      let val = m.amount * a * b;
      if (m.trans === 2) val = Math.abs(val);
      g[m.dest] += val;
    }
  }
  updateChannel(c) { for (const v of this.voices) if (v.on && v.ch === c) { this.computeMods(v); this.updateVoice(v); } }

  /** Derived per-voice parameters from v.g (after a modulator/controller change). */
  updateVoice(v) {
    const g = v.g, c = v.ch;
    const scale = g[GEN.scaleTuning];
    v.pitchBase = (v.keyEff - v.root) * scale + (g[GEN.coarseTune] + c.coarseTune) * 100 + g[GEN.fineTune] + c.fineTune + v.sample.correction;
    let atten = g[GEN.initialAttenuation]; if (atten < 0) atten = 0; if (atten > 1440) atten = 1440;
    v.atten = atten;
    let pan = g[GEN.pan]; if (pan < -500) pan = -500; if (pan > 500) pan = 500;
    const a = (pan + 500) / 1000 * Math.PI / 2;
    v.gl = Math.cos(a); v.gr = Math.sin(a);
    v.rev = Math.max(0, Math.min(1000, g[GEN.reverbEffectsSend])) / 1000;
    v.cho = Math.max(0, Math.min(1000, g[GEN.chorusEffectsSend])) / 1000;
    v.fc0 = Math.max(1500, Math.min(13500, g[GEN.initialFilterFc]));
    let q = g[GEN.initialFilterQ] / 10; if (q < 0) q = 0; if (q > 96) q = 96;
    v.qdB = q;
    v.modLfoToPitch = g[GEN.modLfoToPitch]; v.vibLfoToPitch = g[GEN.vibLfoToPitch]; v.modEnvToPitch = g[GEN.modEnvToPitch];
    v.modLfoToFc = g[GEN.modLfoToFilterFc]; v.modEnvToFc = g[GEN.modEnvToFilterFc]; v.modLfoToVol = g[GEN.modLfoToVolume];
    v.lfoMinc = 4 * abscentHz(g[GEN.freqModLFO]) / this.rate; v.lfoVinc = 4 * abscentHz(g[GEN.freqVibLFO]) / this.rate;
    let sus = g[GEN.sustainVolEnv]; if (sus < 0) sus = 0; if (sus > 1440) sus = 1440;
    v.volSus = sus;
    let msus = g[GEN.sustainModEnv] / 1000; v.modSus = 1 - Math.max(0, Math.min(1, msus));
  }

  initEnvelopes(v) {
    const g = v.g, r = this.rate, k = 60 - v.keyEff;
    const smp = (tc) => tc2sec(tc) * r;
    v.volDelay = smp(g[GEN.delayVolEnv]); v.volAttack = smp(g[GEN.attackVolEnv]);
    v.volHold = smp(g[GEN.holdVolEnv] + g[GEN.keynumToVolEnvHold] * k);
    v.volDecay = smp(g[GEN.decayVolEnv] + g[GEN.keynumToVolEnvDecay] * k);
    v.volRelease = smp(Math.max(-7200, g[GEN.releaseVolEnv]));
    v.modDelay = smp(g[GEN.delayModEnv]); v.modAttack = smp(g[GEN.attackModEnv]);
    v.modHold = smp(g[GEN.holdModEnv] + g[GEN.keynumToModEnvHold] * k);
    v.modDecay = smp(g[GEN.decayModEnv] + g[GEN.keynumToModEnvDecay] * k);
    v.modRelease = smp(Math.max(-7200, g[GEN.releaseModEnv]));
    v.lfoMdelay = smp(g[GEN.delayModLFO]); v.lfoVdelay = smp(g[GEN.delayVibLFO]);
    // stages: 0 delay 1 attack 2 hold 3 decay 4 sustain 5 release
    v.volStage = 0; v.volT = 0; v.volLin = 0; v.volCb = 1440;
    v.modStage = 0; v.modT = 0; v.modVal = 0;
  }

  release(v) {
    if (v.released) return;
    v.released = true;
    // volume: continue from the current level in cB
    const lin = v.volStage === 1 || v.volStage === 0 ? v.volLin : Math.pow(10, -v.volCb / 200);
    v.volCb = lin > 1e-5 ? -200 * Math.log10(lin) : 1000;
    v.volStage = 5; v.volT = 0;
    v.modStage = 5; v.modT = 0; v.modRelFrom = v.modVal;
    if (v.mode === 3) v.mode = 0;               // loop until release, then play to the end
  }
  fastRelease(v) { this.release(v); v.volRelease = 0.005 * this.rate; }

  /** Advance the envelopes by k samples; returns the linear volume-envelope amplitude at the end. */
  advanceEnvelopes(v, k) {
    // volume envelope
    let rem = k;
    while (rem > 0) {
      switch (v.volStage) {
        case 0: { const left = v.volDelay - v.volT; if (left > rem) { v.volT += rem; rem = 0; } else { rem -= Math.max(0, left); v.volStage = 1; v.volT = 0; } v.volLin = 0; break; }
        case 1: { const left = v.volAttack - v.volT; if (left > rem) { v.volT += rem; rem = 0; v.volLin = v.volT / v.volAttack; } else { rem -= Math.max(0, left); v.volStage = 2; v.volT = 0; v.volLin = 1; v.volCb = 0; } break; }
        case 2: { const left = v.volHold - v.volT; v.volCb = 0; if (left > rem) { v.volT += rem; rem = 0; } else { rem -= Math.max(0, left); v.volStage = 3; v.volT = 0; } break; }
        case 3: {
          const rate = 1000 / Math.max(1, v.volDecay);               // cB per sample (100 dB per decay time)
          v.volCb += rate * rem;
          if (v.volCb >= v.volSus) { v.volCb = v.volSus; v.volStage = 4; }
          rem = 0; break;
        }
        case 4: v.volCb = v.volSus; rem = 0; break;
        case 5: {
          const rate = 1000 / Math.max(1, v.volRelease);
          v.volCb += rate * rem; rem = 0;
          if (v.volCb >= 960) { v.volCb = 1440; v.done = true; }
          break;
        }
      }
    }
    // modulation envelope (0..1)
    rem = k;
    while (rem > 0) {
      switch (v.modStage) {
        case 0: { const left = v.modDelay - v.modT; if (left > rem) { v.modT += rem; rem = 0; } else { rem -= Math.max(0, left); v.modStage = 1; v.modT = 0; } v.modVal = 0; break; }
        case 1: { const left = v.modAttack - v.modT; if (left > rem) { v.modT += rem; rem = 0; v.modVal = v.modT / v.modAttack; } else { rem -= Math.max(0, left); v.modStage = 2; v.modT = 0; v.modVal = 1; } break; }
        case 2: { const left = v.modHold - v.modT; v.modVal = 1; if (left > rem) { v.modT += rem; rem = 0; } else { rem -= Math.max(0, left); v.modStage = 3; v.modT = 0; } break; }
        case 3: { v.modVal -= rem / Math.max(1, v.modDecay); if (v.modVal <= v.modSus) { v.modVal = v.modSus; v.modStage = 4; } rem = 0; break; }
        case 4: v.modVal = v.modSus; rem = 0; break;
        case 5: { v.modVal -= rem / Math.max(1, v.modRelease); if (v.modVal < 0) v.modVal = 0; rem = 0; break; }
      }
    }
    if (v.volStage <= 1) return v.volLin;
    return Math.pow(10, -v.volCb / 200);
  }
  advanceLfos(v, k) {
    // triangle LFOs, starting at 0 going up, after their delay
    if (v.t >= v.lfoMdelay) { v.lfoM += v.lfoMdir * v.lfoMinc * k; if (v.lfoM > 1) { v.lfoM = 2 - v.lfoM; v.lfoMdir = -1; } else if (v.lfoM < -1) { v.lfoM = -2 - v.lfoM; v.lfoMdir = 1; } }
    if (v.t >= v.lfoVdelay) { v.lfoV += v.lfoVdir * v.lfoVinc * k; if (v.lfoV > 1) { v.lfoV = 2 - v.lfoV; v.lfoVdir = -1; } else if (v.lfoV < -1) { v.lfoV = -2 - v.lfoV; v.lfoVdir = 1; } }
    v.t += k;
  }

  setFilter(v, fcCents) {
    const qdB = v.qdB;
    if (Math.abs(fcCents - v.filtFc) < 2 && qdB === v.filtQ) return;
    v.filtFc = fcCents; v.filtQ = qdB;
    let f = abscentHz(fcCents);
    const ny = this.rate * 0.45;
    if (f >= ny && qdB <= 3.01) { v.fOff = true; return; }
    if (f > ny) f = ny; if (f < 5) f = 5;
    v.fOff = false;
    const qlin = Math.pow(10, (qdB - 3.01) / 20);
    const w = 2 * Math.PI * f / this.rate, cs = Math.cos(w), sn = Math.sin(w);
    const alpha = sn / (2 * qlin), a0 = 1 + alpha, gain = 1 / Math.sqrt(qlin);
    v.b0 = (1 - cs) / 2 * gain / a0; v.b1 = (1 - cs) * gain / a0; v.b2 = v.b0;
    v.a1 = -2 * cs / a0; v.a2 = (1 - alpha) / a0;
  }

  // ------------------------------------------------------------ rendering
  /** ADD n frames into L/R starting at off. gain scales this synth's output. */
  render(L, R, off, n, gain = 1) {
    if (n <= 0) return;
    this.ensureBus(n);
    const rb = this.revBus, cb = this.choBus;
    rb.fill(0, 0, n); cb.fill(0, 0, n);
    let any = false;
    const g0 = gain * OUT_GAIN * this.masterVol;
    for (const v of this.voices) {
      if (!v.on) continue;
      any = true;
      for (let o = 0; o < n && v.on; o += BLOCK) this.renderVoice(v, L, R, off + o, o, Math.min(BLOCK, n - o), g0);
    }
    this.effects(L, R, off, n, any, gain);
    // meters decay ~12 dB/s
    const dec = Math.pow(0.25, n / this.rate);
    for (let i = 0; i < 16; i++) this.meter[i] *= dec;
  }

  renderVoice(v, L, R, off, bo, k, g0) {
    const env1 = this.advanceEnvelopes(v, k);
    this.advanceLfos(v, k);
    // (pitch bend arrives through the default modulator, in fineTune)
    const cents = v.pitchBase + v.lfoM * v.modLfoToPitch + v.lfoV * v.vibLfoToPitch + v.modVal * v.modEnvToPitch;
    const inc = Math.pow(2, cents / 1200) * v.rateRatio;
    this.setFilter(v, v.fc0 + v.modVal * v.modEnvToFc + v.lfoM * v.modLfoToFc);
    const volCb = v.atten + v.lfoM * v.modLfoToVol;
    const att = Math.pow(10, -Math.max(0, volCb) / 200) * g0;
    const a0 = (v.ampCur || 0), a1 = env1 * att;
    v.ampCur = a1;
    const da = (a1 - a0) / k;
    const d = this.data, loopS = v.loopStart, loopE = v.loopEnd, loopL = loopE - loopS, end = v.end;
    const looping = v.mode === 1 || v.mode === 3;
    let pos = v.pos, amp = a0;
    const gl = v.gl, gr = v.gr, rs = v.rev, cs = v.cho, rb = this.revBus, cb = this.choBus;
    const fOff = v.fOff;
    let x1 = v.x1, x2 = v.x2, y1 = v.y1, y2 = v.y2;
    const b0 = v.b0, b1 = v.b1, b2 = v.b2, fa1 = v.a1, fa2 = v.a2;
    const S = 1 / 32768;
    for (let i = 0; i < k; i++) {
      const ip = pos | 0, fr = pos - ip;
      const s0 = d[ip], s1 = d[ip + 1];
      let s = (s0 + (s1 - s0) * fr) * S;
      if (!fOff) { const y = b0 * s + b1 * x1 + b2 * x2 - fa1 * y1 - fa2 * y2; x2 = x1; x1 = s; y2 = y1; y1 = y; s = y; }
      amp += da;
      const o = s * amp;
      L[off + i] += o * gl; R[off + i] += o * gr;
      rb[bo + i] += o * rs; cb[bo + i] += o * cs;
      pos += inc;
      if (looping) { if (pos >= loopE) pos -= loopL; }
      else if (pos >= end - 1) { v.on = false; break; }
    }
    v.pos = pos; v.x1 = x1; v.x2 = x2; v.y1 = y1; v.y2 = y2;
    if (v.done) { v.on = false; v.done = false; }
  }

  // ------------------------------------------------------------ effects
  initEffects() {
    const sc = this.rate / 44100;
    const combs = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617], aps = [556, 441, 341, 225], spread = 23;
    const mk = (len) => ({ buf: new Float32Array(Math.max(1, Math.round(len * sc))), i: 0, store: 0 });
    this.rv = { cl: combs.map(mk), cr: combs.map((l) => mk(l + spread)), al: aps.map(mk), ar: aps.map((l) => mk(l + spread)) };
    this.rvFeedback = 0.5 * 0.28 + 0.7;          // Freeverb room size 0.5
    this.rvDamp = 0.3 * 0.4; this.rvWet = 0.25; this.rvWidth = 0.8;
    const cl = Math.round(0.05 * this.rate);
    this.cho = { l: new Float32Array(cl), i: 0, ph: 0, len: cl };
    this.revBus = new Float32Array(0); this.choBus = new Float32Array(0);
    this.fxIdle = 0;
  }
  clearEffects() {
    for (const k of ['cl', 'cr', 'al', 'ar']) for (const c of this.rv[k]) { c.buf.fill(0); c.store = 0; }
    this.cho.l.fill(0); this.fxIdle = 1e9;
  }
  ensureBus(n) { if (this.revBus.length < n) { this.revBus = new Float32Array(n); this.choBus = new Float32Array(n); } }

  effects(L, R, off, n, any, gain) {
    // skip the effects once everything has been silent for 3 s (tails done)
    if (!any) { this.fxIdle += n; if (this.fxIdle > 3 * this.rate) return; } else this.fxIdle = 0;
    const rb = this.revBus, cb = this.choBus, rv = this.rv, fb = this.rvFeedback, damp = this.rvDamp, d1 = 1 - damp;
    const wet = this.rvWet, w1 = wet * (this.rvWidth / 2 + 0.5), w2 = wet * ((1 - this.rvWidth) / 2);
    const ch = this.cho, clen = ch.len, cl = ch.l;
    const phInc = 2 * Math.PI * 0.4 / this.rate, base = 0.012 * this.rate, depth = 0.003 * this.rate;
    for (let i = 0; i < n; i++) {
      // reverb (Freeverb): input mono, 8 parallel combs + 4 allpasses per side
      const inp = rb[i] * 0.03;
      let ol = 0, or = 0;
      for (let c = 0; c < 8; c++) {
        let cm = rv.cl[c], y = cm.buf[cm.i];
        cm.store = y * d1 + cm.store * damp; cm.buf[cm.i] = inp + cm.store * fb; if (++cm.i >= cm.buf.length) cm.i = 0; ol += y;
        cm = rv.cr[c]; y = cm.buf[cm.i];
        cm.store = y * d1 + cm.store * damp; cm.buf[cm.i] = inp + cm.store * fb; if (++cm.i >= cm.buf.length) cm.i = 0; or += y;
      }
      for (let a = 0; a < 4; a++) {
        let ap = rv.al[a], b = ap.buf[ap.i]; ap.buf[ap.i] = ol + b * 0.5; if (++ap.i >= ap.buf.length) ap.i = 0; ol = b - ol;
        ap = rv.ar[a]; b = ap.buf[ap.i]; ap.buf[ap.i] = or + b * 0.5; if (++ap.i >= ap.buf.length) ap.i = 0; or = b - or;
      }
      // chorus: two modulated taps (quadrature LFO) of the chorus bus
      const x = cb[i];
      cl[ch.i] = x;
      ch.ph += phInc; if (ch.ph > 6.283185307179586) ch.ph -= 6.283185307179586;
      const tl = base + depth * Math.sin(ch.ph), tr = base + depth * Math.cos(ch.ph);
      const read = (t) => { let p = ch.i - t; if (p < 0) p += clen; const ip = p | 0, f = p - ip, j = ip + 1 >= clen ? 0 : ip + 1; return cl[ip] + (cl[j] - cl[ip]) * f; };
      const chl = read(tl), chr = read(tr);
      if (++ch.i >= clen) ch.i = 0;
      L[off + i] += (ol * w1 + or * w2) * gain + chl * 0.7 * gain;
      R[off + i] += (or * w1 + ol * w2) * gain + chr * 0.7 * gain;
    }
  }
}
