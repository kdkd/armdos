// A small Standard MIDI File reader for the tests (host side, independent of the SDK's smf.c).
export function readSmf(buf) {
  const u = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
  const be32 = (o) => ((u[o] << 24) | (u[o + 1] << 16) | (u[o + 2] << 8) | u[o + 3]) >>> 0, be16 = (o) => (u[o] << 8) | u[o + 1];
  if (String.fromCharCode(...u.subarray(0, 4)) !== 'MThd') throw new Error('not a MIDI file');
  const format = be16(8), ntr = be16(10), div = be16(12);
  let o = 8 + be32(4); const tracks = [];
  while (o + 8 <= u.length && tracks.length < ntr) {
    const len = be32(o + 4);
    if (String.fromCharCode(...u.subarray(o, o + 4)) === 'MTrk') tracks.push(u.subarray(o + 8, o + 8 + len));
    o += 8 + len;
  }
  const events = [];
  tracks.forEach((t, ti) => {
    let p = 0, tick = 0, run = 0;
    const vl = () => { let v = 0, b; do { b = t[p++]; v = (v << 7) | (b & 0x7F); } while (b & 0x80 && p < t.length); return v; };
    while (p < t.length) {
      tick += vl();
      let st = t[p];
      if (st & 0x80) p++; else st = run;
      if (st < 0xF0) { run = st; const d1 = t[p++]; const d2 = (st & 0xE0) === 0xC0 ? 0 : t[p++]; events.push({ tick, track: ti, st, d1, d2 }); }
      else if (st === 0xF0 || st === 0xF7) { const n = vl(); events.push({ tick, track: ti, st, data: t.subarray(p, p + n) }); p += n; }
      else if (st === 0xFF) { const type = t[p++]; const n = vl(); events.push({ tick, track: ti, st, type, data: t.subarray(p, p + n) }); p += n; if (type === 0x2F) break; }
      else break;
    }
  });
  events.sort((a, b) => a.tick - b.tick || a.track - b.track);
  // tempo map -> seconds
  let tempo = 500000, refTick = 0, refUs = 0;
  for (const e of events) {
    e.us = refUs + (e.tick - refTick) * tempo / div;
    if (e.st === 0xFF && e.type === 0x51) { refUs = e.us; refTick = e.tick; tempo = (e.data[0] << 16) | (e.data[1] << 8) | e.data[2]; }
  }
  const text = (type) => { const e = events.find((x) => x.st === 0xFF && x.type === type && x.track === 0); return e ? String.fromCharCode(...e.data) : ''; };
  return { format, ntracks: tracks.length, division: div, events, lengthUs: events.length ? events[events.length - 1].us : 0, title: text(3), notes: events.filter((e) => (e.st & 0xF0) === 0x90 && e.d2) };
}
