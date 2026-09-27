// What a modem's speaker plays during training (docs/MODEM.md), generated from the actual
// line signals — no recordings. Pure JS, used by the page (web/js/modem-audio.js) and by the
// node renderer (emu/tests/modemsound/render.mjs) that draws the spectrograms in docs/.
//
// Everything is synthesised at 8 kHz, as on a telephone line, then band limited (~300-3400 Hz
// with gentle skirts), with the calling side's own signals a little louder than the far end's
// (what you hear is the CALLING modem's speaker: both directions mixed, near end louder), a
// faint line hiss and a short hybrid echo.
//
//   handshake(mod, { role }) -> { sr: 8000, samples: Float32Array, phases: [{ name, t0, t1 }] }
//   mod: 'V21' | 'V22' | 'V22BIS' | 'V32BIS' | 'V34' | 'V90'
//   handshakeMs(mod) -> duration (the modem's training time; the modem uses it for CONNECT)
//
// The V.90 timeline follows a real 56K call measured phase by phase (see docs/MODEM.md):
// V.8bis CRe/MRd tones and V.21 messages, silence, ANSam (2100 Hz, 15 Hz AM, a phase reversal
// every 450 ms) with CM under it, JM, INFO0, tones A/B, the two L1/L2 line-probing combs,
// INFO1, S, the long TRN hiss, the full-band PCM training, the DIL comb, the final hiss.

export const SR = 8000;

// ------------------------------------------------------------------ building blocks
function rng(seed) { let s = seed >>> 0 || 1; return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; }; }
const env = (n, n0, n1, fade) => Math.min(1, (n - n0) / fade, (n1 - n) / fade);

class Line {
  constructor(secs, seed = 1989) { this.near = new Float32Array(Math.ceil(secs * SR) + 1); this.far = new Float32Array(this.near.length); this.r = rng(seed); this.phases = []; }
  buf(side) { return side === 'near' ? this.near : this.far; }
  span(t0, t1) { return [Math.max(0, Math.round(t0 * SR)), Math.min(this.near.length, Math.round(t1 * SR))]; }
  mark(name, t0, t1) { this.phases.push({ name, t0: +t0.toFixed(3), t1: +t1.toFixed(3) }); }

  /** Sine tone; am = [rate Hz, depth]; rev = phase reversal period (s). */
  tone(side, t0, t1, f, amp, { am = null, rev = 0, fade = 0.006 } = {}) {
    const o = this.buf(side), [n0, n1] = this.span(t0, t1), fn = fade * SR;
    let ph = 0, sign = 1, nextRev = rev ? n0 + rev * SR : Infinity;
    for (let n = n0; n < n1; n++) {
      if (n >= nextRev) { sign = -sign; nextRev += rev * SR; }
      ph += 2 * Math.PI * f / SR;
      const a = am ? 1 + am[1] * Math.sin(2 * Math.PI * am[0] * (n - n0) / SR) : 1;
      o[n] += amp * a * sign * Math.sin(ph) * env(n, n0, n1, fn);
    }
  }
  /** Continuous-phase FSK (V.21: 300 bit/s); bits() -> 0/1. */
  fsk(side, t0, t1, mark, space, amp, bits, baud = 300) {
    const o = this.buf(side), [n0, n1] = this.span(t0, t1), fn = 0.005 * SR;
    let ph = 0, bit = 1, next = n0, i = 0;
    for (let n = n0; n < n1; n++) {
      if (n >= next) { bit = bits(i++); next += SR / baud; }
      ph += 2 * Math.PI * (bit ? mark : space) / SR;
      o[n] += amp * Math.sin(ph) * env(n, n0, n1, fn);
    }
  }
  /** A V.8/V.8bis-style message: ten 1s of preamble, then framed random octets, repeated. */
  v21(side, t0, t1, high, amp) {
    const r = this.r, frame = [];
    for (let k = 0; k < 10; k++) frame.push(1);
    for (let b = 0; b < 8; b++) { frame.push(0); let v = (r() * 256) | 0; for (let k = 0; k < 8; k++) { frame.push(v & 1); v >>= 1; } frame.push(1); }
    this.fsk(side, t0, t1, high ? 1650 : 980, high ? 1850 : 1180, amp, (i) => frame[i % frame.length]);
  }
  /** Linear modulation: symbols sym(i) -> [I, Q], smoothed half-cosine transitions. */
  carrier(side, t0, t1, fc, baud, sym, amp, fade = 0.006) {
    const o = this.buf(side), [n0, n1] = this.span(t0, t1), sps = SR / baud, fn = fade * SR;
    let prev = sym(0), cur = sym(1), k = 1;
    for (let n = n0; n < n1; n++) {
      const tl = (n - n0) / sps;
      while (tl >= k) { prev = cur; cur = sym(++k); }
      const fr = tl - (k - 1), w = fr < 0.5 ? 0.5 - 0.5 * Math.cos(2 * Math.PI * fr) : 1;
      const I = prev[0] + (cur[0] - prev[0]) * w, Q = prev[1] + (cur[1] - prev[1]) * w;
      const ph = 2 * Math.PI * fc * n / SR;
      o[n] += amp * env(n, n0, n1, fn) * (I * Math.cos(ph) - Q * Math.sin(ph));
    }
  }
  dpsk(side, t0, t1, fc, amp, step = null, baud = 600) {
    const r = this.r, pts = []; let p = 0;
    const st = step || (() => (r() * 4) | 0);
    this.carrier(side, t0, t1, fc, baud, (i) => { while (pts.length <= i) { p = (p + st(pts.length)) & 3; pts.push([Math.cos(p * Math.PI / 2), Math.sin(p * Math.PI / 2)]); } return pts[i]; }, amp);
  }
  qam(side, t0, t1, fc, baud, amp, levels = 4) {
    const r = this.r, pts = [], lv = Array.from({ length: levels }, (_, k) => (2 * k - levels + 1) / (levels - 1));
    this.carrier(side, t0, t1, fc, baud, (i) => { while (pts.length <= i) pts.push([lv[(r() * levels) | 0], lv[(r() * levels) | 0]]); return pts[i]; }, amp, 0.004);
  }
  /** V.34 line probing L1/L2: tones every 150 Hz from 150 to 3750 Hz, 900/1200/1800/2400 left out. */
  comb(side, t0, t1, amp) {
    const o = this.buf(side), [n0, n1] = this.span(t0, t1), fn = 0.004 * SR;
    const fs = []; for (let k = 1; k <= 25; k++) { const f = 150 * k; if (![900, 1200, 1800, 2400].includes(f)) fs.push(f); }
    const a = amp / Math.sqrt(fs.length) * 1.6;
    for (let n = n0; n < n1; n++) {
      let v = 0;
      for (let j = 0; j < fs.length; j++) v += Math.cos(2 * Math.PI * fs[j] * (n - n0) / SR + Math.PI * j * j / fs.length);  // quadratic phases: low crest factor
      o[n] += a * v * env(n, n0, n1, fn);
    }
  }
  /** PCM (V.90 downstream / DIL-free training): random mu-law-ish levels at 8 kHz = full-band hiss. */
  pcm(side, t0, t1, amp) {
    const o = this.buf(side), [n0, n1] = this.span(t0, t1), fn = 0.004 * SR, r = this.r;
    for (let n = n0; n < n1; n++) { const u = r() * 2 - 1; o[n] += amp * Math.sign(u) * Math.pow(Math.abs(u), 1.6) * 1.7 * env(n, n0, n1, fn); }
  }
  /**
   * V.90 digital impairment learning: the server sends repeating PCM codeword patterns
   * (a few hundred distinct segments, each a short periodic sequence of chosen levels with
   * sign alternation). On the line: dense comb lines (harmonics of the pattern period) with a
   * fast rhythmic pulsing as the segments change, getting louder as the levels climb.
   */
  dil(side, t0, t1, amp0, amp1, P = 144) {
    // Measured on a real call: the DIL is periodic in 144 samples (18 ms at 8 kHz): lines every
    // 55.6 Hz and a 56 Hz buzz in the envelope. Each 18 ms period is a burst of training codes
    // (random mu-law levels, alternating sign) followed by quieter reference codes; the code set
    // changes every few hundred ms (the lines stay put, their strengths shift) and the levels
    // climb through the sequence.
    const o = this.buf(side), [n0, n1] = this.span(t0, t1), fn = 0.004 * SR, r = this.r;
    const seg = 4 * SR;     // one code set held through the section (the lines stay put)
    const mk = () => {
      const pat = new Float32Array(P);
      for (let k = 0; k < P; k++) { const u = r(); pat[k] = (k < 96 ? 0.3 + 0.7 * u * u : 0.22 * u) * (k % 2 ? 1 : -1) * (r() < 0.2 ? -1 : 1); }
      return pat;
    };
    let a = mk(), b = mk(), segStart = n0;
    for (let n = n0; n < n1; n++) {
      if (n - segStart >= seg) { a = b; b = mk(); segStart = n; }
      const x = (n - segStart) / seg, w = x < 0.8 ? 0 : (x - 0.8) / 0.2;      // hold, then cross-fade to the next code set
      const k = (n - n0) % P, v = a[k] * (1 - w) + b[k] * w;
      const frac = (n - n0) / (n1 - n0), amp = amp0 + (amp1 - amp0) * frac * frac;
      o[n] += amp * v * env(n, n0, n1, fn);
    }
  }

  /** Near end louder, band limit, a faint hybrid echo of the near end and line hiss. */
  mix(nearGain = 1, farGain = 0.72) {
    const N = this.near.length, out = new Float32Array(N), r = rng(77);
    const echo = Math.round(0.018 * SR);
    for (let n = 0; n < N; n++) {
      out[n] = nearGain * this.near[n] + farGain * this.far[n] + (n >= echo ? 0.05 * this.near[n - echo] : 0) + 0.0012 * (r() * 2 - 1);
    }
    // telephone band: 2nd-order high-pass at 280 Hz, two 2nd-order low-passes at 3500 Hz
    biquad(out, 'hp', 280, 0.6); biquad(out, 'lp', 3500, 0.7); biquad(out, 'lp', 3650, 0.6);
    return out;
  }
}
function biquad(x, type, f, q) {
  const w = 2 * Math.PI * f / SR, c = Math.cos(w), a = Math.sin(w) / (2 * q);
  let b0, b1, b2;
  if (type === 'lp') { b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; } else { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; }
  const a0 = 1 + a, a1 = -2 * c, a2 = 1 - a;
  let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  for (let n = 0; n < x.length; n++) {
    const y = (b0 * x[n] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
    x2 = x1; x1 = x[n]; y2 = y1; y1 = y; x[n] = y;
  }
}

// ------------------------------------------------------------------ the sequences
// (seconds from the answering modem going off hook; C = calling side, A = answering side)

function v90(L, C, A, t = 0) {
  const m = (name, a, b) => L.mark(name, a, b);
  // V.8bis: CRe (C) and MRd (A) dual tones, then the capabilities exchange in V.21
  L.tone(C, t + 0.20, t + 0.60, 1375, 0.115); L.tone(C, t + 0.20, t + 0.60, 2002, 0.115); L.tone(C, t + 0.60, t + 0.70, 400, 0.13);
  m('V.8bis CRe 1375+2002 Hz, 400 Hz', t + 0.20, t + 0.70);
  L.tone(A, t + 0.73, t + 1.12, 1529, 0.16); L.tone(A, t + 0.73, t + 1.12, 2225, 0.16); L.tone(A, t + 1.12, t + 1.22, 1900, 0.16);
  m('V.8bis MRd 1529+2225 Hz, 1900 Hz', t + 0.73, t + 1.22);
  L.v21(C, t + 1.28, t + 1.97, false, 0.115); m('V.8bis CL/MS (V.21 low)', t + 1.28, t + 1.97);
  L.v21(A, t + 2.14, t + 2.89, true, 0.18); m('V.8bis MS (V.21 high)', t + 2.14, t + 2.89);
  L.v21(C, t + 2.89, t + 3.19, false, 0.115); m('V.8bis ACK (V.21 low)', t + 2.89, t + 3.19);
  // V.8: ANSam with CM under it, then JM, CJ
  L.tone(A, t + 4.09, t + 6.24, 2100, 0.076, { am: [15, 0.2], rev: 0.45 }); m('ANSam 2100 Hz, 15 Hz AM, reversals / 450 ms', t + 4.09, t + 6.24);
  L.v21(C, t + 5.27, t + 7.31, false, 0.1); m('V.8 CM (V.21 low)', t + 5.27, t + 7.31);
  L.v21(A, t + 6.24, t + 7.31, true, 0.13); m('V.8 JM (V.21 high)', t + 6.24, t + 7.31);
  return v34probe(L, C, A, t + 7.38, true);
}
/** V.34 phase 2 (INFO0, tones A/B, L1/L2 x2, INFO1) and the S sequence of phase 3. */
function v34probe(L, C, A, t, withHum1800) {
  const m = (name, a, b) => L.mark(name, a, b);
  const ab = (a, b) => { L.tone(C, a, b, 1200, 0.12); L.tone(A, a, b, 2400, 0.11, { rev: 0.05 }); if (withHum1800) L.tone(A, a, b, 1800, 0.04); };
  L.dpsk(C, t, t + 0.13, 1200, 0.14); L.dpsk(A, t, t + 0.13, 2400, 0.13); if (withHum1800) L.tone(A, t, t + 0.13, 1800, 0.06);
  m('INFO0c / INFO0a (DPSK 600 bit/s)', t, t + 0.13);
  ab(t + 0.13, t + 0.28); m('tones B 1200 / A 2400 Hz', t + 0.13, t + 0.28);
  L.comb(C, t + 0.28, t + 0.45, 0.17); L.comb(C, t + 0.45, t + 0.63, 0.085); m('L1 / L2 line probing (C)', t + 0.28, t + 0.63);
  ab(t + 0.63, t + 0.74);
  L.comb(A, t + 0.74, t + 0.91, 0.2); L.comb(A, t + 0.91, t + 1.08, 0.1); m('L1 / L2 line probing (A)', t + 0.74, t + 1.08);
  L.tone(A, t + 1.08, t + 1.46, 2400, 0.14); if (withHum1800) L.tone(A, t + 1.08, t + 1.46, 1800, 0.08);
  L.dpsk(C, t + 1.18, t + 1.33, 1200, 0.15); m('INFO1c', t + 1.18, t + 1.33);
  L.dpsk(A, t + 1.33, t + 1.46, 2400, 0.15); m('INFO1a', t + 1.33, t + 1.46);
  // phase 3: S / S-bar (alternating points: line spectra), then TRN
  const s0 = t + 1.55;
  for (const f of [250, 1100, 1800, 2800, 3450]) L.tone(A, s0, s0 + 0.13, f, 0.1);
  L.carrier(A, s0, s0 + 0.13, 1959, 3429 / 4, (i) => (i & 1 ? [1, 0] : [0, 1]), 0.25);
  m('S / S-bar', s0, s0 + 0.13);
  return s0 + 0.13;
}

function build(mod, role) {
  const C = role === 'answerer' ? 'far' : 'near', A = role === 'answerer' ? 'near' : 'far';
  let L;
  switch (mod) {
    case 'V90': {
      L = new Line(20);
      let t = v90(L, C, A);                                               // -> 9.06
      L.qam(A, t, t + 1.7, 1959, 3429, 0.34); L.qam(C, t + 0.4, t + 1.7, 1959, 3429, 0.2);
      L.mark('phase 3 TRN (scrambled QAM, 3429 baud)', t, t + 1.7); t += 1.7;
      L.pcm(A, t, t + 2.9, 0.32); L.qam(C, t, t + 2.9, 1829, 3200, 0.2);
      L.mark('V.90 PCM training / upstream TRN', t, t + 2.9); t += 2.9;
      L.dil(A, t, t + 1.8, 0.5, 1.3); L.qam(C, t, t + 1.8, 1829, 3200, 0.06);
      L.dil(A, t + 1.8, t + 1.97, 1.2, 1.2, 72); L.qam(C, t + 1.8, t + 1.97, 1829, 3200, 0.06);
      L.mark('V.90 DIL (digital impairment learning)', t, t + 1.97); t += 1.97;
      L.pcm(A, t, t + 0.16, 0.3); L.qam(C, t, t + 0.16, 1829, 3200, 0.2);
      L.qam(A, t + 0.16, t + 0.42, 2600, 1800, 0.22); L.tone(C, t + 0.17, t + 0.38, 1335, 0.3);
      L.mark('Jd / Ja, 1335 Hz tone', t, t + 0.42); t += 0.42;
      L.pcm(A, t, t + 1.85, 0.42); L.qam(C, t, t + 1.85, 1829, 3200, 0.4);
      L.mark('phase 4: MP, E, scrambled data', t, t + 1.85); t += 1.85;
      L.total = t;
      break;
    }
    case 'V34': {
      L = new Line(10);
      const m = (name, a, b) => L.mark(name, a, b);
      L.tone(A, 0.3, 2.45, 2100, 0.076, { am: [15, 0.2], rev: 0.45 }); m('ANSam 2100 Hz, 15 Hz AM, reversals / 450 ms', 0.3, 2.45);
      L.v21(C, 1.48, 3.52, false, 0.1); m('V.8 CM (V.21 low)', 1.48, 3.52);
      L.v21(A, 2.45, 3.52, true, 0.13); m('V.8 JM (V.21 high)', 2.45, 3.52);
      let t = v34probe(L, C, A, 3.6, false);
      L.qam(A, t, t + 1.5, 1959, 3429, 0.34); L.qam(C, t + 0.5, t + 1.5, 1959, 3429, 0.22); m('phase 3 TRN', t, t + 1.5); t += 1.5;
      L.qam(A, t, t + 1.4, 1959, 3429, 0.3); L.qam(C, t, t + 1.4, 1959, 3429, 0.3); m('phase 4: MP, E, scrambled data', t, t + 1.4); t += 1.4;
      L.total = t;
      break;
    }
    case 'V32BIS': {
      L = new Line(3.8); const m = (name, a, b) => L.mark(name, a, b);
      L.tone(C, 0.0, 0.35, 1800, 0.22); m('AA 1800 Hz', 0, 0.35);
      for (let i = 0; i < 6; i++) { const a = 0.35 + i * 0.07; L.tone(A, a, a + 0.066, i & 1 ? 3000 : 600, 0.16); L.tone(A, a, a + 0.066, i & 1 ? 600 : 3000, 0.16); }
      m('AC / CA 600 + 3000 Hz', 0.35, 0.8);
      L.qam(A, 0.9, 2.1, 1800, 2400, 0.25); m('S / TRN (echo canceller training)', 0.9, 2.1);
      L.dpsk(C, 2.2, 2.45, 1800, 0.25, (i) => (i & 1 ? 2 : 0), 2400); m('R rate sequence', 2.2, 2.45);
      L.qam(A, 2.5, 3.7, 1800, 2400, 0.26); L.qam(C, 2.9, 3.7, 1800, 2400, 0.16); m('TRN, both directions', 2.5, 3.7);
      L.total = 3.7;
      break;
    }
    case 'V21': {
      L = new Line(0.75);
      L.fsk(A, 0.05, 0.7, 2225, 2025, 0.24, () => 1); L.fsk(C, 0.25, 0.7, 1270, 1070, 0.24, () => (L.r() < 0.5 ? 1 : 0));
      L.mark('Bell 103 FSK', 0.05, 0.7); L.total = 0.7;
      break;
    }
    default: {   // V22 / V22BIS: USB1 on 2400 Hz, the caller's S1 on 1200 Hz, scrambled DPSK, 16-QAM
      const T = mod === 'V22' ? 1.3 : 1.9, k = T / 1.9;
      L = new Line(T + 0.05); const m = (name, a, b) => L.mark(name, a, b);
      L.dpsk(A, 0.075, T, 2400, 0.3, () => 3); m('unscrambled binary 1 (2250 Hz)', 0.075, T);
      L.dpsk(C, 0.62 * k, 0.72 * k, 1200, 0.28, (i) => (i & 1 ? 3 : 1)); m('S1 (C)', 0.62 * k, 0.72 * k);
      L.dpsk(C, 0.72 * k, T, 1200, 0.28); m('scrambled 1s, 1200 bit/s', 0.72 * k, T);
      if (mod === 'V22BIS') {
        L.dpsk(A, 0.78 * k, 0.88 * k, 2400, 0.08, (i) => (i & 1 ? 3 : 1));
        L.qam(A, 1.35 * k, T, 2400, 600, 0.26); L.qam(C, 1.42 * k, T, 1200, 600, 0.26); m('16-QAM 2400 bit/s', 1.35 * k, T);
      }
      L.total = T;
    }
  }
  return L;
}

/** Training time of a modulation in ms (the modem's CONNECT comes at the end of it). */
const DUR = {};
export function handshakeMs(mod) {
  if (!(mod in DUR)) DUR[mod] = Math.round(build(mod, 'caller').total * 1000);
  return DUR[mod];
}
/** The training as heard through the modem's speaker (8 kHz). */
export function handshake(mod, { role = 'caller' } = {}) {
  const L = build(mod, role);
  const samples = L.mix().subarray(0, Math.round(L.total * SR));
  return { sr: SR, samples, phases: L.phases, total: L.total };
}

/** Steady data carriers (what ATM2 lets you hear while connected), a loopable buffer. */
export function dataSignal(mod, secs = 2) {
  const L = new Line(secs);
  if (mod === 'V21') { L.fsk('far', 0, secs, 2225, 2025, 0.2, () => (L.r() < 0.5 ? 1 : 0)); L.fsk('near', 0, secs, 1270, 1070, 0.2, () => (L.r() < 0.5 ? 1 : 0)); }
  else if (mod === 'V22' || mod === 'V22BIS') { L.qam('far', 0, secs, 2400, 600, 0.22); L.qam('near', 0, secs, 1200, 600, 0.22); }
  else if (mod === 'V90') { L.pcm('far', 0, secs, 0.26); L.qam('near', 0, secs, 1829, 3200, 0.22); }
  else L.qam('far', 0, secs, 1959, 3429, 0.3);
  return { sr: SR, samples: L.mix() };
}

// ------------------------------------------------------------------ what other lines play
// (heard in the calling modem's speaker before any carrier: a PBX, a fax machine, a trunk)
const NOTE = (n) => 440 * Math.pow(2, n / 12);           // semitones from A4
// "Greensleeves" (traditional, public domain) - the hold music of every switchboard
const GREENSLEEVES = [[0, 1], [3, 2], [5, 1], [7, 1.5], [8, 0.5], [7, 1], [5, 2], [2, 1], [-2, 1.5], [0, 0.5], [2, 1],
  [3, 2], [0, 1], [0, 1.5], [-1, 0.5], [0, 1], [2, 2], [-1, 1], [-5, 2], [0, 1],
  [3, 2], [5, 1], [7, 1.5], [8, 0.5], [7, 1], [5, 2], [2, 1], [-2, 1.5], [0, 0.5], [2, 1],
  [3, 1.5], [2, 0.5], [0, 1], [-1, 1.5], [-3, 0.5], [-1, 1], [0, 3], [0, 2], [null, 1]];

/** kind: 'hold' | 'fax' | 'routing' | 'pickup'. Returns { sr, samples }. */
export function lineSignal(kind, ms = 3000) {
  const L = new Line(ms / 1000 + 0.1, 42);
  const T = ms / 1000;
  if (kind === 'hold') {
    // a cheap music-on-hold chip: a buzzy square-ish melody with a little vibrato, a bass note
    let t = 0.1;
    const beat = 0.3;
    while (t < T) {
      for (const [n, b] of GREENSLEEVES) {
        if (t >= T) break;
        const d = b * beat;
        if (n !== null) {
          const f = NOTE(n), o = L.buf('far'), [n0, n1] = L.span(t, Math.min(T, t + d * 0.92));
          let ph = 0;
          for (let i = n0; i < n1; i++) {
            ph += 2 * Math.PI * f * (1 + 0.004 * Math.sin(2 * Math.PI * 5.5 * i / SR)) / SR;
            const e = Math.min(1, (i - n0) / 80, (n1 - i) / 300);
            o[i] += 0.16 * e * (Math.sin(ph) + 0.35 * Math.sin(3 * ph) + 0.2 * Math.sin(5 * ph));
          }
          L.tone('far', t, Math.min(T, t + d * 0.9), NOTE(n - 24 + (n > 2 ? 0 : 5)), 0.05);
        }
        t += d;
      }
    }
  } else if (kind === 'fax') {
    // an answering fax: CED 2100 Hz, a short gap, then the V.21 channel 2 HDLC flags and DIS
    // frame (1650/1850 Hz, 300 bit/s) - the screech - repeated until the caller gives up
    L.tone('far', 0.25, 3.25, 2100, 0.3);
    const flags = [0, 1, 1, 1, 1, 1, 1, 0];
    const frame = [...Array(12)].flatMap(() => flags).concat([...Array(160)].map(() => (L.r() < 0.5 ? 1 : 0)), flags, flags);
    for (let t = 3.35; t < T; t += 3.0) L.fsk('far', t, Math.min(T, t + 1.3), 1650, 1850, 0.3, (i) => frame[i % frame.length]);
  } else if (kind === 'routing') {
    // an international trunk being set up: faint hiss, a few clicks and a far-away tone burst
    L.pcm('far', 0, T, 0.012);
    for (const at of [0.2, 0.9, 1.1, 2.3]) if (at < T) L.tone('far', at, at + 0.012, 1000 + 700 * L.r(), 0.25);
    if (T > 2.6) L.tone('far', 1.6, 1.75, 2400, 0.06);
  } else if (kind === 'pickup') {
    L.tone('far', 0.0, 0.01, 800, 0.3); L.pcm('far', 0.01, 0.12, 0.05);
  }
  return { sr: SR, samples: L.mix(1, 0.8).subarray(0, Math.round(T * SR)) };
}
