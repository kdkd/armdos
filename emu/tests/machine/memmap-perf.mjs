#!/usr/bin/env node
// Cost of the memory activity map: DOOM -timedemo demo1 host MIPS with the map
// closed and open (alternating runs, fresh boot each), plus where the activity
// went. The "open" runs also drain and decay the counters every 66 ms of host
// time as the inspector does (web/js/memmap-panel.js), so that cost is included.
//
// usage: node emu/tests/machine/memmap-perf.mjs [--img build/doom-test/mhz33/timedemo.img] [--reps 2]
//          [--period N --window N] [--exact] [--closed-only] [--phases] [--steady [--slot N]]
// --steady: one demo, the map toggled every --slot instructions, open slots paired with
// their closed neighbours (the most drift-robust number). --phases: host time spent in
// the sampling windows. (The image comes from apps/doom/tests/run.mjs timedemo.)
import { existsSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../testkit.mjs';
import { MemActivity, memoryRegions, NCELLS } from '../../memmap.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const argv = process.argv.slice(2);
const opt = (n, d) => { const i = argv.indexOf(n); return i >= 0 ? argv[i + 1] : d; };
const img = resolve(ROOT, opt('--img', 'build/doom-test/mhz33/timedemo.img'));
const reps = +opt('--reps', 2);
const actOpts = {};
if (opt('--period')) actOpts.period = +opt('--period');
if (opt('--window')) actOpts.window = +opt('--window');
if (argv.includes('--exact')) actOpts.exact = true;
if (!existsSync(img)) { console.log(`no ${img}: run apps/doom/tests/run.mjs timedemo first`); process.exit(0); }

async function demo(mapOpen) {
  const pc = await boot({ rom: join(ROOT, 'build/rom.bin'), hd: img });
  if (!pc.until(() => pc.machine.vga.mode === 0x13, { timeoutMs: 60000, stepMs: 50 })) throw new Error('DOOM did not start');
  let act = null, heat = null, lastDrain = 0, drains = 0;
  const regions0 = memoryRegions(pc.machine);
  if (mapOpen) {
    act = new MemActivity(actOpts);
    act.attach(pc.machine);
    if (argv.includes('--phases')) {            // host time and instructions per phase (diagnostics)
      const f0 = act.flip.bind(act); let tp = performance.now(), ip = pc.cpu.icount + pc.cpu._n;
      act.ph = { rec: [0, 0], plain: [0, 0] };
      act.flip = () => { const t = performance.now(), i = pc.cpu.icount + pc.cpu._n; const k = act.rec ? 'rec' : 'plain'; act.ph[k][0] += t - tp; act.ph[k][1] += i - ip; tp = t; ip = i; f0(); };
    }
    heat = [new Float32Array(NCELLS), new Float32Array(NCELLS), new Float32Array(NCELLS), new Float32Array(65536)];
    var tot = [new Float64Array(NCELLS), new Float64Array(NCELLS), new Float64Array(NCELLS)];
  }
  const t0 = performance.now(), i0 = pc.cpu.icount, e0 = pc.timeMs;
  const done = pc.until(() => {
    if (act && performance.now() - lastDrain > 66) {
      lastDrain = performance.now(); drains++;
      act.drain((x, r, w, wp) => {      // what the panel does per frame: decay + add, and memoryRegions once a second
        const src = [x, r, w];
        for (let c = 0; c < 3; c++) { const h = heat[c], s = src[c], t = tot[c]; for (let i = 0; i < NCELLS; i++) { h[i] = h[i] * 0.8 + s[i]; t[i] += s[i]; } }
        for (let i = 0; i < 65536; i++) heat[3][i] = heat[3][i] * 0.8 + (wp[i] > 0 ? 0.2 : 0);
      });
      if (drains % 15 === 0) memoryRegions(pc.machine);
    }
    return pc.debug.includes('timed ');
  }, { timeoutMs: 600000, stepMs: 10 });
  const ms = performance.now() - t0, insns = pc.cpu.icount - i0;
  if (!done) throw new Error('timedemo did not finish');
  const res = { mips: insns / ms / 1000, ms, insns, fps: (pc.debug.match(/timed .*/) || [''])[0].trim(), emuMs: pc.timeMs - e0 };
  if (act) {
    act.detach();
    if (act.ph) for (const k of ['rec', 'plain']) console.log(`  phase ${k}: ${(act.ph[k][1] / 1e6).toFixed(0)}M insns in ${act.ph[k][0].toFixed(0)} ms = ${(act.ph[k][1] / act.ph[k][0] / 1000).toFixed(0)} MIPS`);
    // activity by region (as they were when DOOM had started)
    const regs = regions0;
    for (const r of regs) { r.x = 0; r.r = 0; r.w = 0; }
    const byCell = (i) => {
      const a = i < 65536 ? i << 8 : (0xFFF00000 + ((i - 65536) << 8)) >>> 0;
      let lo = 0, hi = regs.length - 1;
      while (lo <= hi) { const mid = (lo + hi) >> 1; if (a < regs[mid].start) hi = mid - 1; else if (a >= regs[mid].end) lo = mid + 1; else return regs[mid]; }
      return null;
    };
    for (let i = 0; i < 65536 + 4096; i++) {
      const r = byCell(i); if (!r) continue;
      r.x += tot[0][i]; r.r += tot[1][i]; r.w += tot[2][i];
    }
    res.regions = regs.filter((r) => r.x + r.r + r.w > 0).sort((a, b) => (b.x + b.r + b.w) - (a.x + a.r + a.w)).slice(0, 12);
    res.sum = [0, 1, 2].map((c) => tot[c].reduce((s, v) => s + v, 0));
  }
  res.regions0 = regions0;
  return res;
}

// Steady state: one demo; the first `warm` instructions with the map closed (the plain
// JIT warms up), then the map toggles every `slot` instructions (drained as the panel
// does while open); host MIPS per state. The first open slot includes V8 re-optimising
// the interpreter loop for its trace branch (a one-time cost).
async function steady(warm = 1.5e9, slot = +opt('--slot', 1.25e8)) {
  const pc = await boot({ rom: join(ROOT, 'build/rom.bin'), hd: img });
  if (!pc.until(() => pc.machine.vga.mode === 0x13, { timeoutMs: 60000, stepMs: 50 })) throw new Error('DOOM did not start');
  const act = new MemActivity(actOpts);
  const heat = [new Float32Array(NCELLS), new Float32Array(NCELLS), new Float32Array(NCELLS)], hp = new Float32Array(65536);
  const st = { open: [0, 0], closed: [0, 0] }, seq = [];
  const i0 = pc.cpu.icount;
  pc.until(() => pc.cpu.icount - i0 > warm || pc.debug.includes('timed '), { timeoutMs: 600000, stepMs: 10 });
  let on = false, lastDrain = 0;
  while (!pc.debug.includes('timed ')) {
    if (on) act.attach(pc.machine); else act.detach();
    const t = performance.now(), i = pc.cpu.icount;
    pc.until(() => {
      if (on && performance.now() - lastDrain > 66) {
        lastDrain = performance.now();
        act.drain((x, r, w, wp) => {
          const src = [x, r, w]; for (let c = 0; c < 3; c++) { const h = heat[c], s = src[c]; for (let k = 0; k < NCELLS; k++) h[k] = h[k] * 0.8 + s[k]; }
          for (let k = 0; k < wp.length; k++) hp[k] = hp[k] * 0.8 + (wp[k] > 0 ? 0.2 : 0);
        });
      }
      return pc.cpu.icount - i > slot || pc.debug.includes('timed ');
    }, { timeoutMs: 600000, stepMs: 10 });
    const dt = performance.now() - t, di = pc.cpu.icount - i;
    if (di < slot * 0.9) break;                          // (the demo ended inside the slot)
    st[on ? 'open' : 'closed'][0] += dt; st[on ? 'open' : 'closed'][1] += di;
    seq.push(di / dt / 1000);
    on = !on;
  }
  act.detach();
  const mo = st.open[1] / st.open[0] / 1000, mc = st.closed[1] / st.closed[0] / 1000;
  console.log(`steady state (after ${(warm / 1e9).toFixed(1)}G insns warm-up, ${(slot / 1e6).toFixed(0)}M-insn slots): closed ${mc.toFixed(0)} MIPS, open ${mo.toFixed(0)} MIPS: ${((1 - mo / mc) * 100).toFixed(1)}% slower`);
  console.log(`  slots (closed/open alternating): ${seq.map((v) => v.toFixed(0)).join(' ')}`);
  // pair every open slot after the first two with the mean of its closed neighbours (drift-robust)
  const ratios = [];
  for (let k = 5; k < seq.length - 1; k += 2) ratios.push(seq[k] / ((seq[k - 1] + seq[k + 1]) / 2));
  const mean = ratios.reduce((s, v) => s + v, 0) / ratios.length;
  const sd = Math.sqrt(ratios.reduce((s, v) => s + (v - mean) ** 2, 0) / Math.max(1, ratios.length - 1));
  console.log(`  open vs neighbouring closed slots, after the first two open slots: ${((1 - mean) * 100).toFixed(1)}% slower (+-${(sd / Math.sqrt(ratios.length) * 100).toFixed(1)}% s.e., n=${ratios.length})`);
}
if (argv.includes('--steady')) { await steady(); process.exit(0); }

const M = (v) => (v / 1e6).toFixed(1) + 'M';
const results = { off: [], on: [] };
for (let k = 0; k < reps; k++) {
  for (const on of argv.includes('--closed-only') ? [false] : [false, true]) {
    const r = await demo(on);
    results[on ? 'on' : 'off'].push(r.mips);
    console.log(`map ${on ? 'OPEN  ' : 'closed'}: ${r.mips.toFixed(0)} host MIPS (${(r.insns / 1e6).toFixed(0)}M insns, ${r.ms.toFixed(0)} ms) ${r.fps}`);
    if (on && k === reps - 1) {
      console.log(`  counted: exec ~${M(r.sum[0])} (of ${M(r.insns)} run), reads ~${M(r.sum[1])}, writes ~${M(r.sum[2])}`);
      for (const g of r.regions) console.log(`  ${g.start.toString(16).padStart(8, '0')}-${g.end.toString(16).padStart(8, '0')} ${g.name.padEnd(26)} X ${M(g.x).padStart(8)}  R ${M(g.r).padStart(8)}  W ${M(g.w).padStart(8)}`);
    }
  }
}
const avg = (a) => a.reduce((s, v) => s + v, 0) / a.length;
const off = avg(results.off), on = avg(results.on);
if (!results.on.length) { console.log(`closed: ${off.toFixed(0)} MIPS (${results.off.map((v) => v.toFixed(0)).join(' ')})`); process.exit(0); }
console.log(`memmap overhead: closed ${off.toFixed(0)} MIPS, open ${on.toFixed(0)} MIPS: ${((1 - on / off) * 100).toFixed(1)}% slower with the map open`);
