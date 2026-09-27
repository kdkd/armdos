// apps/drarm/tests/analyse.mjs - speech analysis for DRARM's tests: resampling to
// 11025 Hz, voiced/unvoiced frames, an autocorrelation pitch tracker and LPC
// formant tracks (Levinson-Durbin, peaks of the envelope).
export const FS = 11025;

/** 44100 Hz -> 11025 Hz (box filter of 4) */
export function to11k(x, rate = 44100) {
  const k = Math.round(rate / FS), n = Math.floor(x.length / k), y = new Float32Array(n);
  for (let i = 0; i < n; i++) { let s = 0; for (let j = 0; j < k; j++) s += x[i * k + j]; y[i] = s / k; }
  return y;
}
export const rms = (x) => { let a = 0; for (let i = 0; i < x.length; i++) a += x[i] * x[i]; return Math.sqrt(a / Math.max(1, x.length)); };

/** the span holding sound: [first, last] sample above thr */
export function span(x, thr = 0.01) {
  let a = -1, b = -1;
  for (let i = 0; i < x.length; i++) if (Math.abs(x[i]) > thr) { if (a < 0) a = i; b = i; }
  return [a, b];
}

/** per 10 ms frame (40 ms window): { t, rms, f0 (0 = unvoiced) } */
export function pitchTrack(x) {
  const w = Math.round(0.04 * FS), hop = Math.round(0.01 * FS), out = [];
  const lo = Math.floor(FS / 300), hi = Math.ceil(FS / 60);
  const peak = x.reduce((m, v) => Math.max(m, Math.abs(v)), 1e-9);
  for (let i = 0; i + w < x.length; i += hop) {
    const f = x.subarray(i, i + w);
    let mean = 0; for (const v of f) mean += v; mean /= w;
    const r = rms(f) / peak;
    let f0 = 0;
    if (r > 0.04) {
      const ac = (k) => { let s = 0; for (let j = 0; j + k < w; j++) s += (f[j] - mean) * (f[j + k] - mean); return s; };
      const a0 = ac(0);
      let best = -Infinity, bk = 0;
      for (let k = lo; k <= hi; k++) { const v = ac(k); if (v > best) { best = v; bk = k; } }
      if (best > 0.4 * a0) f0 = FS / bk;
    }
    out.push({ t: i / FS, rms: r, f0 });
  }
  return out;
}

export const median = (a) => { const s = [...a].sort((p, q) => p - q); return s.length ? s[s.length >> 1] : 0; };

/** LPC (order p) of a frame -> envelope peaks (Hz) below 4000 */
export function formants(frame, p = 10) {
  const n = frame.length, x = new Float64Array(n);
  for (let i = 0; i < n; i++) x[i] = (frame[i] - (i ? 0.9 * frame[i - 1] : 0)) * (0.54 - 0.46 * Math.cos(2 * Math.PI * i / (n - 1)));
  const R = new Float64Array(p + 1);
  for (let k = 0; k <= p; k++) { let s = 0; for (let i = k; i < n; i++) s += x[i] * x[i - k]; R[k] = s; }
  if (R[0] <= 0) return [];
  const a = new Float64Array(p + 1); a[0] = 1; let e = R[0];
  for (let i = 1; i <= p; i++) {
    let acc = R[i]; for (let j = 1; j < i; j++) acc += a[j] * R[i - j];
    const k = -acc / e, t = a.slice();
    for (let j = 1; j < i; j++) a[j] = t[j] + k * t[i - j];
    a[i] = k; e *= 1 - k * k;
  }
  const env = [];
  for (let f = 0; f <= 4000; f += 10) {
    const w = 2 * Math.PI * f / FS; let re = 0, im = 0;
    for (let j = 0; j <= p; j++) { re += a[j] * Math.cos(w * j); im -= a[j] * Math.sin(w * j); }
    env.push(-10 * Math.log10(re * re + im * im));
  }
  const peaks = [];
  for (let i = 1; i < env.length - 1; i++) if (env[i] > env[i - 1] && env[i] >= env[i + 1] && i * 10 > 150) peaks.push(i * 10);
  return peaks;
}

/** F1/F2 tracks over the voiced frames */
export function formantTracks(x, track) {
  const w = Math.round(0.025 * FS), F1 = [], F2 = [];
  for (const fr of track) {
    if (!fr.f0) continue;
    const i = Math.round(fr.t * FS);
    const pk = formants(x.subarray(i, i + w));
    if (pk.length >= 2) { F1.push(pk[0]); F2.push(pk[1]); }
  }
  return { F1, F2 };
}
export const stdev = (a) => { const m = a.reduce((s, v) => s + v, 0) / Math.max(1, a.length); return Math.sqrt(a.reduce((s, v) => s + (v - m) ** 2, 0) / Math.max(1, a.length)); };
