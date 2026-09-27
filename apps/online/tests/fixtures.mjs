// Canned responses for the ARM-DOS Online service in node tests: no network.
// fixtures/index.json maps each URL the service asks for to a file (recorded from the
// real services with tests/record.mjs). Pictures are stored as gzipped PPM (P6), which
// decodePPM turns into RGBA the way a browser's decoder would.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

export const FIXTURES = path.join(path.dirname(fileURLToPath(import.meta.url)), 'fixtures');
// the moment the fixtures were recorded ("3 hours ago" in the news is relative to this)
export const RECORDED_AT = Date.parse('2026-09-24T22:30:00Z');

export function fixtureFetch({ dir = FIXTURES, missing = [], latencyMs = 0, fail = () => false } = {}) {
  const index = JSON.parse(fs.readFileSync(path.join(dir, 'index.json'), 'utf8'));
  return async (url) => {
    if (latencyMs) await new Promise((r) => setTimeout(r, latencyMs));
    if (fail(url)) throw new TypeError('Failed to fetch');
    const e = index[url];
    if (!e) { missing.push(url); return { ok: false, status: 404, json: async () => ({}), text: async () => '', arrayBuffer: async () => new ArrayBuffer(0) }; }
    let data = fs.readFileSync(path.join(dir, e.file));
    if (e.file.endsWith('.gz')) data = zlib.gunzipSync(data);
    return {
      ok: e.status === 200, status: e.status,
      json: async () => JSON.parse(data.toString('utf8')),
      text: async () => data.toString('utf8'),
      arrayBuffer: async () => data.buffer.slice(data.byteOffset, data.byteOffset + data.length),
    };
  };
}

/** P6 PPM bytes -> { width, height, data: RGBA }, scaled down (nearest) to fit maxW x maxH. */
export function decodePPM(bytes) {
  const buf = Buffer.from(bytes);
  const m = /^P6\s+(\d+)\s+(\d+)\s+255\s/.exec(buf.toString('latin1', 0, 64));
  if (!m) throw new Error('not a PPM');
  const w = +m[1], h = +m[2], off = m[0].length;
  const data = new Uint8ClampedArray(w * h * 4);
  for (let i = 0; i < w * h; i++) { data[i * 4] = buf[off + i * 3]; data[i * 4 + 1] = buf[off + i * 3 + 1]; data[i * 4 + 2] = buf[off + i * 3 + 2]; data[i * 4 + 3] = 255; }
  return { width: w, height: h, data };
}
