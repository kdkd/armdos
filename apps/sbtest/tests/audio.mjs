// Helpers for the Sound Blaster tests (apps/sbtest, apps/doom): a disk image
// with COMMAND.COM + AUTOEXEC.BAT (SET BLASTER) + programs, audio capture from
// the emulated machine, WAV output and a little spectral analysis.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage } from '../../../disk/mkimage.mjs';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const B = (p) => path.join(ROOT, 'build', p);
export const RATE = 44100;

/** Build a C: image in dir: files = [{ src, dst }], plus the kernel, COMMAND.COM, CONFIG.SYS, AUTOEXEC.BAT. */
export function makeImage(dir, name, files, { autoexec, config } = {}) {
  fs.mkdirSync(dir, { recursive: true });
  const all = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM' },
    ...files,
  ];
  const put = (dst, text) => {
    const host = path.join(dir, name + '-' + dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, text.replace(/\r?\n/g, '\r\n'));
    all.push({ src: path.relative(ROOT, host), dst });
  };
  if (fs.existsSync(B('SBMIX.EXE'))) all.push({ src: 'build/SBMIX.EXE', dst: 'DOS\\SBMIX.EXE' });   // AUTOEXEC.BAT runs SBMIX /INIT /Q
  const himem = fs.existsSync(B('HIMEM.SYS'));
  if (himem) all.push({ src: 'build/HIMEM.SYS', dst: 'DOS\\HIMEM.SYS' });
  put('CONFIG.SYS', config ?? `FILES=20\nBUFFERS=20\n${himem ? 'DEVICE=C:\\DOS\\HIMEM.SYS\n' : ''}`);
  put('AUTOEXEC.BAT', autoexec ?? fs.readFileSync(path.join(ROOT, 'disk/c/AUTOEXEC.BAT'), 'latin1'));
  const dirs = [...new Set(all.filter((f) => f.dst && f.dst.includes('\\')).flatMap((f) => {
    const parts = f.dst.split('\\').slice(0, -1); return parts.map((_, i) => parts.slice(0, i + 1).join('\\'));
  }))];
  const { img } = buildImage({ format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'SBTEST',
    date: '1992-06-01 12:00:00', boot: { src: 'build/bootsect.bin' }, files: all, dirs }, ROOT);
  const p = path.join(dir, name + '.img');
  fs.writeFileSync(p, img);
  return p;
}

/** Boot with audio capture: pc.audio() -> { L, R } Float32Arrays from power-on. */
export async function bootWithAudio(hd, opts = {}) {
  const chunks = [];
  const pc = await boot({ rom: B('rom.bin'), hd, audio: { rate: RATE, speaker: true, onAudio: (l, r) => chunks.push([l, r]) }, ...opts });
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

export function writeWav(file, L, R, rate = RATE) {
  const n = L.length, b = Buffer.alloc(44 + n * 4);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 4, 4); b.write('WAVEfmt ', 8);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22); b.writeUInt32LE(rate, 24);
  b.writeUInt32LE(rate * 4, 28); b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 4, 40);
  const s = (v) => Math.max(-32768, Math.min(32767, Math.round(v * 32767)));
  for (let i = 0; i < n; i++) { b.writeInt16LE(s(L[i]), 44 + i * 4); b.writeInt16LE(s(R[i]), 46 + i * 4); }
  fs.writeFileSync(file, b);
}

export const rms = (x) => { let a = 0; for (let i = 0; i < x.length; i++) a += x[i] * x[i]; return Math.sqrt(a / Math.max(1, x.length)); };
/** Power of frequency f in x (Hann-windowed DFT bin at an arbitrary frequency). */
export function power(x, f, rate = RATE) {
  let re = 0, im = 0; const w = 2 * Math.PI * f / rate, n = x.length;
  for (let i = 0; i < n; i++) { const h = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / n); re += h * x[i] * Math.cos(w * i); im += h * x[i] * Math.sin(w * i); }
  return (re * re + im * im) / (n * n);
}
/** Frequency of the strongest component between lo and hi Hz (0.5 Hz resolution around the peak). */
export function peakHz(x, lo, hi, rate = RATE) {
  let best = 0, bf = lo;
  for (let f = lo; f <= hi; f += 4) { const p = power(x, f, rate); if (p > best) { best = p; bf = f; } }
  for (let f = bf - 4; f <= bf + 4; f += 0.5) { const p = power(x, f, rate); if (p > best) { best = p; bf = f; } }
  return bf;
}
export const window = (x, t0, t1, rate = RATE) => x.subarray(Math.round(t0 * rate), Math.round(t1 * rate));
