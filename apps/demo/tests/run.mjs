#!/usr/bin/env node
// apps/demo/tests/run.mjs - DEMO.EXE headless tests: every part renders (screenshots),
// 70 fps at 100 MHz, the music plays through the PC speaker, Esc restores text mode,
// INT 08h and the PIT.
//   node apps/demo/tests/run.mjs [--out DIR] [--quick]

import fs from 'node:fs';
import path from 'node:path';
import { startPC, check, failed, tap, B, mode } from '../../arminfo/tests/harness.mjs';

const argv = process.argv.slice(2);
const oi = argv.indexOf('--out');
const OUT = path.resolve(oi >= 0 ? argv[oi + 1] : B('showcase-test/demo'));
fs.mkdirSync(OUT, { recursive: true });
const files = [{ src: 'build/DEMO.EXE', dst: 'DEMO\\' }];

// distinct colours in the mode 13h frame buffer (a blank or broken frame has few)
const colours = (pc) => { const s = new Set(pc.m.cpu.m8.subarray(0xA0000, 0xA0000 + 64000)); return s.size; };

async function run(mhz, tag, shots) {
  console.log(`== DEMO at ${mhz} MHz`);
  const pc = await startPC({ name: 'demo' + tag, outDir: OUT, files, dirs: ['DEMO'], mhz });
  check(pc.bootOk, 'booted');
  const vec8 = pc.m.cpu.m32[8] >>> 0;
  pc.cmd('CD \\DEMO');
  pc.type('DEMO\r');
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 30000 }), 'mode 13h');
  const t0 = pc.timeMs, n0 = pc.speaker.length;
  if (shots) {
    for (const [ms, name, minCol] of [[1200, 'intro1', 8], [4200, 'intro2', 4], [7600, 'intro3', 8]]) {
      pc.run(ms - (pc.timeMs - t0));
      await pc.shot(`${name}.png`);
      check(colours(pc) >= minCol, `${name}: ${colours(pc)} colours`);
    }
    for (const name of ['plasma', 'roto', 'tunnel', 'bars']) {
      pc.until(() => false, { timeoutMs: (name === 'plasma' ? 9000 - (pc.timeMs - t0) : 0) + 4000 });
      await pc.shot(`${name}.png`);
      check(colours(pc) >= 40, `${name}: ${colours(pc)} colours`);
      tap(pc, 'Space', 100);
    }
  } else {
    pc.run(15000);
    await pc.shot(`plasma${tag}.png`);
  }
  const notes = pc.speaker.slice(n0).filter(([, on]) => on);
  const freqs = new Set(notes.map(([, , hz]) => Math.round(hz)));
  check(notes.length > 100 && freqs.size > 10, `PC speaker music: ${notes.length} tone changes, ${freqs.size} pitches`);
  const busTick = pc.m.cpu.m32[0x46C >> 2], tm = pc.timeMs;
  pc.run(3000);
  const hz = (pc.m.cpu.m32[0x46C >> 2] - busTick) / ((pc.timeMs - tm) / 1000);
  check(Math.abs(hz - 18.2) < 0.6, `BIOS clock keeps 18.2 Hz while the PIT runs at 1120 Hz (${hz.toFixed(2)})`);
  tap(pc, 'Escape', 500);
  check(pc.until(() => mode(pc) === 3 && pc.hasText('CPU:'), { timeoutMs: 10000 }), 'Esc: back to text mode with the summary');
  const s = pc.screen();
  const fps = /frames per second/.test(s) ? +/([\d.]+) frames per second/.exec(s)[1] : 0;
  const cpu = /CPU: (\d+)% of an ARM926 at (\d+) MHz/.exec(s);
  console.log(s.split('\n').filter((l) => /frames|CPU/.test(l)).map((l) => '     ' + l).join('\n'));
  check(cpu && +cpu[2] === mhz, `CPU line names ${mhz} MHz`);
  check(pc.m.cpu.m32[8] >>> 0 === vec8, 'INT 08h vector restored');
  check(!pc.speaker.length || !pc.speaker[pc.speaker.length - 1][1], 'speaker silent after exit');
  pc.cmd('ECHO back');
  check(pc.hasText('back'), 'DOS prompt works');
  await pc.shot(`end${tag}.png`);
  return { fps, cpu: cpu ? +cpu[1] : 0 };
}

const fast = await run(100, '', true);
check(fast.fps > 68, `smooth at 100 MHz: ${fast.fps} fps, CPU ${fast.cpu}%`);
if (!argv.includes('--quick')) {
  const slow = await run(12, '-12mhz', false);
  check(slow.fps < fast.fps && slow.cpu > fast.cpu * 2, `turbo off (12 MHz): ${slow.fps} fps, CPU ${slow.cpu}%`);
}
console.log(failed() ? `\n${failed()} FAILED` : '\nall DEMO tests passed');
process.exit(failed() ? 1 : 0);
