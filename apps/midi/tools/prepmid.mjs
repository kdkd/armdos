#!/usr/bin/env node
// apps/midi/tools/prepmid.mjs - the C:\MIDI\ files from the Mutopia Project
// downloads in apps/midi/data/src/ (all public domain; see apps/midi/README.md).
// Only the track-0 name ("control track", as LilyPond writes it) becomes the
// piece's title, a copyright meta event records the provenance, and where the
// LilyPond file set no tempo (its default: quarter = 60) a tempo is set.
// The notes are untouched.
//
//   node apps/midi/tools/prepmid.mjs        -> apps/midi/data/*.MID
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const DIR = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'data');
export const PIECES = [
  { src: 'bwv846.mid', dst: 'BACH846.MID', title: 'J.S. Bach: Prelude in C major, BWV 846', mutopia: 'Mutopia-2011/09/12-5', by: 'Tobias Erbsland' },
  { src: 'toccata.mid', dst: 'TOCCATA.MID', title: 'J.S. Bach: Toccata and Fugue in D minor, BWV 565', mutopia: 'Mutopia-2011/09/11-1780', by: 'Anonymous', bpm: 72 },
  { src: 'brandenburg_2-score.mid', dst: 'BRANDBG2.MID', title: 'J.S. Bach: Brandenburg Concerto No. 2, I. Allegro', mutopia: 'Mutopia-2009/06/13-1680', by: 'Andy Vaught', bpm: 96 },
  { src: 'nacht.mid', dst: 'NACHTMUS.MID', title: 'W.A. Mozart: Eine kleine Nachtmusik, I. Allegro', mutopia: 'Mutopia-2007/01/01-900', by: 'Anonymous' },
  { src: 'elise.mid', dst: 'FURELISE.MID', title: 'L. van Beethoven: Fur Elise, WoO 59', mutopia: 'Mutopia-2015/08/18-931', by: 'Stelios Samelis' },
  { src: 'mtnking.mid', dst: 'MTNKING.MID', title: 'E. Grieg: In the Hall of the Mountain King', mutopia: 'Mutopia-2013/12/07-1888', by: 'Coyau' },
  { src: 'entertainer.mid', dst: 'ENTERTNR.MID', title: 'S. Joplin: The Entertainer', mutopia: 'Mutopia-2016/11/25-263', by: 'Chris Sawer' },
  { src: 'maple.mid', dst: 'MAPLELF.MID', title: 'S. Joplin: Maple Leaf Rag', mutopia: 'Mutopia-2011/11/13-23', by: 'Chris Sawer' },
];

const vlq = (n) => { const b = [n & 0x7F]; while ((n >>= 7)) b.unshift(0x80 | (n & 0x7F)); return b; };
const meta = (type, bytes) => [0x00, 0xFF, type, ...vlq(bytes.length), ...bytes];
const ascii = (s) => [...s].map((c) => c.charCodeAt(0) & 0x7F);

function prep(buf, p) {
  const u = new Uint8Array(buf);
  const be32 = (o) => ((u[o] << 24) | (u[o + 1] << 16) | (u[o + 2] << 8) | u[o + 3]) >>> 0;
  const hdrEnd = 8 + be32(4);
  const out = [...u.subarray(0, hdrEnd)];
  let o = hdrEnd, first = true;
  while (o + 8 <= u.length) {
    const len = be32(o + 4), id = String.fromCharCode(...u.subarray(o, o + 4));
    let body = [...u.subarray(o + 8, o + 8 + len)];
    if (id === 'MTrk' && first) {
      first = false;
      // walk track 0: drop its track name, rewrite tempos; prepend title and copyright
      const t = body, res = [];
      let q = 0, run = 0, pendingDelta = 0;
      const readVl = () => { let v = 0, b; do { b = t[q++]; v = (v << 7) | (b & 0x7F); } while (b & 0x80); return v; };
      while (q < t.length) {
        const delta = readVl();
        let st = t[q];
        const evStart = q;
        if (st & 0x80) q++; else st = run;
        if (st === 0xFF) {
          const type = t[q++], n = readVl(), data = t.slice(q, q + n); q += n;
          if (type === 0x03 || type === 0x02) { pendingDelta += delta; continue; }
          if (type === 0x51 && p.bpm) { const us = Math.round(60e6 / p.bpm); data[0] = us >> 16; data[1] = (us >> 8) & 255; data[2] = us & 255; }
          res.push(...vlq(delta + pendingDelta), 0xFF, type, ...vlq(n), ...data); pendingDelta = 0;
          continue;
        }
        if (st === 0xF0 || st === 0xF7) { const n = readVl(); q += n; res.push(...vlq(delta + pendingDelta), ...t.slice(evStart, q)); pendingDelta = 0; continue; }
        run = st;
        q += (st & 0xE0) === 0xC0 ? 1 : 2;
        res.push(...vlq(delta + pendingDelta), ...t.slice(evStart, q)); pendingDelta = 0;
      }
      body = [...meta(0x03, ascii(p.title)), ...meta(0x02, ascii(`Public domain. Mutopia Project ${p.mutopia}, typeset by ${p.by}`)), ...res];
    }
    const L = body.length;
    out.push(...u.subarray(o, o + 4), L >>> 24, (L >> 16) & 255, (L >> 8) & 255, L & 255, ...body);
    o += 8 + len;
  }
  return Uint8Array.from(out);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  for (const p of PIECES) {
    const b = prep(fs.readFileSync(path.join(DIR, 'src', p.src)), p);
    fs.writeFileSync(path.join(DIR, p.dst), b);
    console.log(`prepmid: ${p.dst} ${b.length} bytes  ${p.title}`);
  }
}
