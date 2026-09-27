// disc-node.mjs - load a disc built by build-disc.mjs (disc.json + data.iso + tNN.wav)
// as an emulator CdDisc, for node tests: the ISO is read at once, each audio track's
// WAV is decoded when the drive first asks for it (as the page decodes Opus lazily).
//
//   import { loadDisc } from '../tools/disc-node.mjs';
//   const disc = loadDisc('build/cdrom/sampler93', { lazy: true });
//   pc.m.cdrom.insert(disc);   // or boot({ cdrom: disc })
//   disc.requests               // track numbers the drive asked to decode, in order
import fs from 'node:fs';
import path from 'node:path';
import { CdDisc } from '../../../emu/dev/atapi.mjs';

/** 16-bit PCM WAV -> { left, right } Int16Array */
export function readWav(file) {
  const b = fs.readFileSync(file);
  let o = 12, fmt = null;
  while (o + 8 <= b.length) {
    const id = b.toString('latin1', o, o + 4), len = b.readUInt32LE(o + 4);
    if (id === 'fmt ') fmt = { ch: b.readUInt16LE(o + 10), rate: b.readUInt32LE(o + 12), bits: b.readUInt16LE(o + 22) };
    if (id === 'data') {
      if (!fmt || fmt.bits !== 16) throw new Error(`${file}: need 16-bit PCM`);
      const n = Math.floor(len / (2 * fmt.ch));
      const left = new Int16Array(n), right = new Int16Array(n);
      for (let i = 0; i < n; i++) {
        left[i] = b.readInt16LE(o + 8 + i * 2 * fmt.ch);
        right[i] = fmt.ch > 1 ? b.readInt16LE(o + 8 + i * 4 + 2) : left[i];
      }
      return { left, right, rate: fmt.rate };
    }
    o += 8 + len + (len & 1);
  }
  throw new Error(`${file}: no data chunk`);
}

export function loadDisc(dir, { lazy = true, delayMs = 0 } = {}) {
  const manifest = JSON.parse(fs.readFileSync(path.join(dir, 'disc.json'), 'utf8'));
  const data = new Uint8Array(fs.readFileSync(path.join(dir, manifest.tracks[0].file)));
  const decoded = new Map();
  const requests = [];
  const decode = (no) => {
    const t = manifest.tracks.find((x) => x.number === no);
    if (!t || t.type !== 'audio') return null;
    if (!decoded.has(no)) decoded.set(no, readWav(path.join(dir, t.files.wav)));
    return decoded.get(no);
  };
  const audio = {
    get: (no) => (lazy ? decoded.get(no) || null : decode(no)),
    request: (no) => { requests.push(no); return new Promise((res) => setTimeout(() => { decode(no); res(); }, delayMs)); },
  };
  const disc = new CdDisc(manifest, { data, audio });
  disc.requests = requests;
  return disc;
}
