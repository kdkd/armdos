#!/usr/bin/env node
// Machine-level performance: runs the CPU benchmark (emu/tests/cpu/c/bench.c)
// inside a full Machine with PIT channel 0 ticking at 1 kHz (IRQs masked), so
// the numbers include the scheduler/device overhead.
// usage: node emu/tests/machine/perf.mjs [--rounds N] [--no-jit]
import { execFileSync } from 'node:child_process';
import { readFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Machine } from '../../machine.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const rounds = args.includes('--rounds') ? +args[args.indexOf('--rounds') + 1] : 20;
const out = join(here, '..', '..', '..', 'build', 'emu-tests', `bench-r${rounds}`);   // (build/: make clean removes it)
mkdirSync(out, { recursive: true });
execFileSync(join(here, '..', 'cpu', 'c', 'build.sh'), [out, 'bench', '-O2', '-marm', `-DROUNDS=${rounds}`], { stdio: 'inherit' });
const bin = readFileSync(join(out, 'bench.bin'));

let text = '';
const m = new Machine({ jit: !args.includes('--no-jit'), onDebug: (b) => { text += String.fromCharCode(b); } });
m.cpu.hostWrite(0x10000, bin);
m.cpu.r[1] = 0; m.cpu.pc = 0x10000;
m.out8(0x43, 0x34); m.out8(0x40, 1193 & 0xFF); m.out8(0x40, 1193 >> 8);   // 1 kHz tick
const t0 = performance.now();
while (!m.stopped) m.runFor(100);
const ms = performance.now() - t0;
const ok = text.includes('bench done');
console.log(`machine bench: ${(m.cpu.icount / 1e6).toFixed(0)}M instructions in ${ms.toFixed(0)} ms host = ${(m.cpu.icount / ms / 1000).toFixed(1)} MIPS; ` +
  `emulated ${(m.timeMs() / 1000).toFixed(2)} s at ${m.mhz} MHz (${(m.timeMs() / ms).toFixed(2)}x real time)${ok ? '' : '  OUTPUT WRONG'}`);
process.exit(ok ? 0 : 1);
