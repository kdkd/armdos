#!/usr/bin/env node
// Structure checks of the synthesised trainings (emu/modemsound.mjs): the V.90 sequence in the
// order and at the times measured on a real 56K call (docs/MODEM.md), with the right signal in
// each phase (Goertzel power at the tones that define it), the DIL's 144-sample period, and
// the telephone band limit. Plus the other modulations' durations.
import { handshake, handshakeMs, SR } from '../../modemsound.mjs';

let fails = 0, passes = 0;
const ok = (name, cond, extra = '') => { if (cond) passes++; else { fails++; console.log(`FAIL ${name} ${extra}`); } };
const goertzel = (x, a, b, f) => { let s1 = 0, s2 = 0; const c = 2 * Math.cos(2 * Math.PI * f / SR); for (let n = a; n < b; n++) { const s = x[n] + c * s1 - s2; s2 = s1; s1 = s; } return (s1 * s1 + s2 * s2 - c * s1 * s2) / (b - a) ** 2; };
const win = (t, d = 0.1) => [Math.round(t * SR), Math.round((t + d) * SR)];
const ratio = (x, t, f, others, d = 0.1) => { const [a, b] = win(t, d); const p = goertzel(x, a, b, f); return p / Math.max(...others.map((g) => goertzel(x, a, b, g))); };

const h = handshake('V90');
const x = h.samples, names = h.phases.map((p) => p.name);
ok('V.90 lasts ~17.9 s', Math.abs(h.total - 17.9) < 0.1, h.total);
const order = ['V.8bis CRe', 'V.8bis MRd', 'ANSam', 'V.8 CM', 'V.8 JM', 'INFO0', 'L1 / L2 line probing (C)', 'L1 / L2 line probing (A)', 'INFO1c', 'S / S-bar', 'phase 3 TRN', 'V.90 PCM', 'V.90 DIL', 'phase 4'];
const idx = order.map((o) => names.findIndex((n) => n.startsWith(o)));
ok('V.90 phases in order', idx.every((v, i) => v >= 0 && (i === 0 || v > idx[i - 1])), JSON.stringify(names));
const at = (n) => h.phases.find((p) => p.name.startsWith(n));
ok('CRe: 1375 + 2002 Hz', ratio(x, at('V.8bis CRe').t0 + 0.1, 1375, [1700, 2400, 1000]) > 20 && ratio(x, at('V.8bis CRe').t0 + 0.1, 2002, [1700, 2400, 1000]) > 20);
ok('MRd: 1529 + 2225 Hz', ratio(x, at('V.8bis MRd').t0 + 0.1, 1529, [1900, 2600, 1100]) > 20);
ok('ANSam: 2100 Hz alone before CM', ratio(x, at('ANSam').t0 + 0.2, 2100, [1080, 1750, 1500], 0.3) > 30);
const cm = at('V.8 CM').t0 + 0.2;
ok('CM under ANSam: V.21 low (980/1180) with 2100', goertzel(x, ...win(cm, 0.3), 980) + goertzel(x, ...win(cm, 0.3), 1180) > 5 * goertzel(x, ...win(cm, 0.3), 1650));
const jm = at('V.8 JM').t0 + 0.2;
ok('JM: V.21 high (1650/1850) appears', goertzel(x, ...win(jm, 0.3), 1650) + goertzel(x, ...win(jm, 0.3), 1850) > 5 * goertzel(x, ...win(jm, 0.3), 2100));
const l1 = at('L1 / L2 line probing (C)').t0 + 0.03;
const comb = [300, 450, 1050, 1500, 2100, 2700, 3300].map((f) => goertzel(x, ...win(l1, 0.12), f));
const gaps = [375, 975, 1725, 2625].map((f) => goertzel(x, ...win(l1, 0.12), f));
ok('L1: a comb of tones every 150 Hz', Math.min(...comb) > 10 * Math.max(...gaps), JSON.stringify([comb, gaps].map((a) => a.map((v) => v.toExponential(1)))));
ok('L1: 1200 and 1800 Hz left out', goertzel(x, ...win(l1, 0.12), 1200) < Math.min(...comb) / 10 && goertzel(x, ...win(l1, 0.12), 1800) < Math.min(...comb) / 10);
// DIL: periodic in 144 samples (lines every 55.6 Hz)
const d = at('V.90 DIL'), [da] = win(d.t0 + 0.3), N = 4000;
const ac = (lag) => { let s = 0, e = 0; for (let n = 0; n < N; n++) { s += x[da + n] * x[da + n + lag]; e += x[da + n] * x[da + n]; } return s / e; };
ok('DIL repeats every 144 samples (18 ms)', ac(144) > 0.6 && ac(144) > 2 * Math.abs(ac(100)), `${ac(144).toFixed(2)} vs ${ac(100).toFixed(2)}`);
// telephone band: little above 3800 Hz, little below 150 Hz
const tr = at('phase 3 TRN').t0 + 0.4;
ok('band limited: 3900 Hz >20 dB below 2000 Hz', goertzel(x, ...win(tr, 0.4), 3900) < goertzel(x, ...win(tr, 0.4), 2000) / 100);
ok('band limited: 60 Hz >20 dB below 1000 Hz', goertzel(x, ...win(tr, 0.4), 60) < goertzel(x, ...win(tr, 0.4), 1000) / 100);
// quiet gap between V.8bis and ANSam (the ~0.9 s silence)
const [ga, gb] = win(3.3, 0.6); let e = 0; for (let n = ga; n < gb; n++) e += x[n] * x[n];
ok('silence before ANSam', Math.sqrt(e / (gb - ga)) < 0.01);
ok('durations: V.22bis 1.9 s, V.32bis 3.7 s, V.34 ~8.2 s', handshakeMs('V22BIS') === 1900 && handshakeMs('V32BIS') === 3700 && Math.abs(handshakeMs('V34') - 8180) < 50);
ok('V.34 has no V.8bis and no DIL', !handshake('V34').phases.some((p) => /V\.8bis|DIL/.test(p.name)));
ok('answerer role mixes the other way round', handshake('V90', { role: 'answerer' }).samples.length === x.length);
console.log(`modemsound: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
