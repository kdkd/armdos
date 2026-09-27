#!/usr/bin/env node
// Floating-point benchmark: emu/tests/cpu/c/fbench.c (n-body and mandelbrot in
// double, a float 3D-transform kernel) built for soft-float and for the VFP
// (-mfpu=vfp -mfloat-abi=softfp), run on the bare CPU with the JIT.
// usage: node emu/tests/cpu/fpbench.mjs [--rounds N] [--no-jit]
import { execFileSync } from 'node:child_process';
import { readFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { runBare } from './bare.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const rounds = args.includes('--rounds') ? +args[args.indexOf('--rounds') + 1] : 4;
const jit = !args.includes('--no-jit');
const res = {};
for (const [name, flags] of [['vfp', ['-mfpu=vfp', '-mfloat-abi=softfp', '-fno-math-errno']], ['soft', []]]) {
  const out = join(here, '..', '..', '..', 'build', 'emu-tests', `fbench-${name}-r${rounds}`);   // (build/: make clean removes it)
  mkdirSync(out, { recursive: true });
  execFileSync(join(here, 'c', 'build.sh'), [out, 'fbench', '-O2', '-marm', ...flags, `-DROUNDS=${rounds}`], { stdio: 'inherit' });
  const r = await runBare(readFileSync(join(out, 'fbench.bin')), { jit, maxInsns: 1e12 });
  if (!r.out.includes('fbench done')) { console.log(`${name}: WRONG OUTPUT\n${r.out}`); process.exit(1); }
  res[name] = r;
  console.log(`${name.padEnd(4)}: ${(r.insns / rounds / 1e6).toFixed(1)}M instructions/round, ${(r.ms / rounds).toFixed(1)} ms/round host, ${r.mips.toFixed(0)} MIPS` +
    ` -> ${(r.insns / rounds / 1e5).toFixed(1)} ms/round emulated at 100 MHz`);
}
console.log(`VFP speedup: ${(res.soft.ms / res.vfp.ms).toFixed(1)}x host time, ${(res.soft.insns / res.vfp.insns).toFixed(1)}x fewer instructions${jit ? '' : ' (interpreter)'}`);
