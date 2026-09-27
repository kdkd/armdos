// Compare two OPL register logs note by note: every key-on (a B0h write that
// turns a voice's key bit on) with its F-number/block, times relative to each
// log's first key-on. Used by ../sound.mjs against the Chocolate Doom reference.
export function notes(lines) {
  const a0 = new Map(), on = new Map(), out = [];
  for (const [t, reg, val] of lines) {
    const hi = reg & 0x100, r = reg & 0xFF;
    if (r >= 0xA0 && r <= 0xA8) a0.set(hi | (r & 15), val);
    else if (r >= 0xB0 && r <= 0xB8) {
      const v = hi | (r & 15), was = on.get(v) || 0, key = val & 0x20;
      if (key && !was) out.push([t, v, ((val & 0x1F) << 8) | (a0.get(v) || 0)]);
      on.set(v, key);
    }
  }
  return out;
}
export const parseLog = (text) => text.split('\n').filter((l) => /^[\d.]+ /.test(l)).map((l) => { const [t, r, v] = l.split(' '); return [+t, parseInt(r, 16), parseInt(v, 16)]; });
/** -> { ref, arm, matched, maxDt, unmatched } over the first `durMs` of both (tolerance tolMs) */
export function compareNotes(refLines, armLines, durMs, tolMs = 10) {
  const ref = notes(refLines), arm = notes(armLines);
  if (!ref.length || !arm.length) return { ref: ref.length, arm: arm.length, matched: 0, maxDt: 0, unmatched: [] };
  const r0 = ref[0][0], a0 = arm[0][0];
  const R = ref.filter(([t]) => t - r0 <= durMs - tolMs), A = arm.filter(([t]) => t - a0 <= durMs);
  const used = new Set(), unmatched = [];
  let matched = 0, maxDt = 0;
  for (const [t, v, f] of R) {
    let best = -1, bd = 1e9;
    A.forEach(([ta, , fa], i) => { if (!used.has(i) && fa === f) { const d = Math.abs((ta - a0) - (t - r0)); if (d < bd) { bd = d; best = i; } } });
    if (best >= 0 && bd <= tolMs) { used.add(best); matched++; maxDt = Math.max(maxDt, bd); } else unmatched.push([t - r0, v, f]);
  }
  return { ref: R.length, arm: A.filter(([t]) => t - a0 <= durMs - tolMs).length, matched, maxDt, unmatched };
}
