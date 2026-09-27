#!/usr/bin/env node
// Renders the modem training sounds (emu/modemsound.mjs) to 16-bit 8 kHz WAV files, for
// listening and for the spectrogram comparison in docs/MODEM.md.
//   node emu/tests/modemsound/render.mjs <outdir> [V90 V34 V32BIS V22BIS ...]
import { writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { handshake } from '../../modemsound.mjs';

export function wav(samples, sr) {
  const n = samples.length, b = Buffer.alloc(44 + n * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 2, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16);
  b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * 2, 28);
  b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 2, 40);
  let peak = 0; for (const v of samples) peak = Math.max(peak, Math.abs(v));
  const g = peak > 0 ? 0.89 / peak : 1;
  for (let i = 0; i < n; i++) b.writeInt16LE(Math.round(Math.max(-1, Math.min(1, samples[i] * g)) * 32767), 44 + i * 2);
  return b;
}
if (import.meta.url === `file://${process.argv[1]}`) {
  const out = process.argv[2] || '.';
  const mods = process.argv.slice(3).length ? process.argv.slice(3) : ['V90', 'V34', 'V32BIS', 'V22BIS'];
  mkdirSync(out, { recursive: true });
  for (const m of mods) {
    const h = handshake(m);
    writeFileSync(join(out, `${m}.wav`), wav(h.samples, h.sr));
    writeFileSync(join(out, `${m}.phases.json`), JSON.stringify(h.phases, null, 1));
    console.log(`${m}: ${h.total.toFixed(2)} s, ${h.phases.length} phases`);
  }
}
