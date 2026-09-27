// Helpers for the General MIDI tests: the sound set, a synth, a machine with
// the MPU-401 wired to it and audio recorded, and a little DSP.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseSf2 } from '../../../emu/dev/sf2.mjs';
import { GmSynth } from '../../../emu/dev/gmsynth.mjs';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const B = (p) => path.join(ROOT, 'build', p);
export const RATE = 44100;
export const SF_PATH = path.join(ROOT, '3rdparty/midi/ARMGS.SFA');
export const AUTOEXEC = '@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\nSET BLASTER=A220 I7 D1 H5 P330 T6\nSBMIX /INIT /Q\n';

let sfCache = null;
export function soundFont() { return sfCache || (sfCache = parseSf2(fs.readFileSync(SF_PATH))); }
export function newSynth(rate = RATE) { const s = new GmSynth(rate); s.loadSoundFont(soundFont()); return s; }

let failures = 0;
export const check = (ok, what, extra = '') => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${extra ? '  ' + extra : ''}`); if (!ok) failures++; return ok; };
export const done = () => { console.log(failures ? `${failures} FAILED` : 'all passed'); process.exit(failures ? 1 : 0); };

/** Boot a C: image with the MPU-401 wired to a local synth, recording audio and the MIDI byte stream. */
export async function bootMidi(hd, opts = {}) {
  const { boot } = await import('../../../emu/testkit.mjs');
  const chunks = [], midi = [];
  const synth = opts.synth === null ? null : newSynth(RATE);
  const pc = await boot({ rom: B('rom.bin'), hd, audio: { rate: RATE, speaker: false, onAudio: (l, r) => chunks.push([l, r]) },
    mpu: { synth, onMidi: (t, b) => midi.push([t / 1e6, b]) }, ...opts });
  pc.synth = synth; pc.midi = midi;
  pc.audio = () => {
    pc.machine.audio.pump();
    const n = chunks.reduce((a, [l]) => a + l.length, 0);
    const L = new Float32Array(n), R = new Float32Array(n);
    let o = 0;
    for (const [l, r] of chunks) { L.set(l, o); R.set(r, o); o += l.length; }
    return { L, R };
  };
  return pc;
}

/** Decode a raw MIDI byte stream [[ms, byte]...] into messages [{ ms, st, d1, d2 }] (running status, SysEx skipped). */
export function parseStream(bytes) {
  const out = []; let st = 0, need = 0, d = [], sysex = false;
  for (const [ms, b] of bytes) {
    if (b >= 0xF8) continue;
    if (sysex) { if (b === 0xF7) sysex = false; if (!(b & 0x80) || b === 0xF7) continue; sysex = false; }
    if (b === 0xF0) { sysex = true; continue; }
    if (b & 0x80) { st = b; need = (b & 0xE0) === 0xC0 ? 1 : 2; d = []; continue; }
    if (!st) continue;
    d.push(b);
    if (d.length === need) { out.push({ ms, st, d1: d[0], d2: d[1] ?? 0 }); d = []; }
  }
  return out;
}

export function writeWav(file, L, R, rate = RATE) {
  const n = L.length, b = Buffer.alloc(44 + n * 4);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 4, 4); b.write('WAVEfmt ', 8);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22); b.writeUInt32LE(rate, 24);
  b.writeUInt32LE(rate * 4, 28); b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 4, 40);
  const s = (v) => Math.max(-32768, Math.min(32767, Math.round(v * 32767)));
  for (let i = 0; i < n; i++) { b.writeInt16LE(s(L[i]), 44 + i * 4); b.writeInt16LE(s(R[i]), 46 + i * 4); }
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, b);
}

export const rms = (x) => { let a = 0; for (let i = 0; i < x.length; i++) a += x[i] * x[i]; return Math.sqrt(a / Math.max(1, x.length)); };
export const peak = (x) => { let a = 0; for (let i = 0; i < x.length; i++) { const v = Math.abs(x[i]); if (v > a) a = v; } return a; };
export function power(x, f, rate = RATE) {
  let re = 0, im = 0; const w = 2 * Math.PI * f / rate, n = x.length;
  for (let i = 0; i < n; i++) { const h = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / n); re += h * x[i] * Math.cos(w * i); im += h * x[i] * Math.sin(w * i); }
  return (re * re + im * im) / (n * n);
}
/** Strongest frequency between lo and hi (1% steps, then 0.25 Hz). */
export function peakHz(x, lo, hi, rate = RATE) {
  let best = 0, bf = lo;
  for (let f = lo; f <= hi; f *= 1.01) { const p = power(x, f, rate); if (p > best) { best = p; bf = f; } }
  const b0 = bf;
  for (let f = b0 * 0.99; f <= b0 * 1.01; f += 0.25) { const p = power(x, f, rate); if (p > best) { best = p; bf = f; } }
  return bf;
}
/** Fundamental: the lowest strong harmonic-series candidate (for notes whose 2nd harmonic dominates). */
export function fundamentalHz(x, expect, rate = RATE) {
  // compare the power at expect/2, expect, 2*expect neighbourhoods; return the best peak near expect
  return peakHz(x, expect * 0.94, expect * 1.06, rate);
}
export const win = (x, t0, t1, rate = RATE) => x.subarray(Math.round(t0 * rate), Math.round(t1 * rate));
export const midiHz = (k) => 440 * Math.pow(2, (k - 69) / 12);
