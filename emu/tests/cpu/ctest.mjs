#!/usr/bin/env node
// Build the C CPU tests in several variants and compare our output with QEMU's.
// usage: node emu/tests/cpu/ctest.mjs [--jit] [names...]
import { execFileSync, spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';
import { runBare } from './bare.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const jit = args.includes('--jit');
const names = args.filter((a) => !a.startsWith('--'));
const tests = names.length ? names : ['arith', 'excpt', 'bench', 'fp', 'fbench'];
const variants = [['-O2', '-marm'], ['-O2', '-mthumb'], ['-O0', '-marm'], ['-Os', '-mthumb'], ['-O3', '-marm']];
// floating point: VFP (softfp ABI, ARM state: Thumb-1 has no VFP) and soft-float
const VFP = ['-mfpu=vfp', '-mfloat-abi=softfp', '-fno-math-errno'];
const fpVariants = [['-O2', '-marm', ...VFP], ['-O0', '-marm', ...VFP], ['-O3', '-marm', ...VFP], ['-Os', '-mthumb', ...VFP], ['-O2', '-marm']];
const work = join(process.env.EMU_TEST_TMP || join(tmpdir(), 'armdos-emu-ctest'));
mkdirSync(work, { recursive: true });
let fails = 0;
for (const t of tests) for (const v of (t === 'excpt' ? [['-O1', '-marm']] : t.startsWith('f') ? fpVariants : variants)) {
  const out = join(work, t + v.join('').replace(/[^\w-]/g, ''));
  execFileSync(join(here, 'c', 'build.sh'), [out, t, ...v], { stdio: 'inherit' });
  const bin = readFileSync(join(out, t + '.bin'));
  const qr = spawnSync('qemu-system-arm', ['-M', 'versatilepb', '-cpu', 'arm926', '-m', '128M', '-nographic', '-monitor', 'none', '-serial', 'null',
    '-semihosting-config', 'enable=on,target=native', '-kernel', join(out, t + '.bin')], { timeout: 60000, stdio: ['ignore', 'pipe', 'pipe'] });
  let q = qr.stderr.toString() + qr.stdout.toString();
  if (qr.status !== 0) q += `\n<qemu status ${qr.status} ${qr.error || ''}>`;
  const r = await runBare(bin, { jit });
  const ok = r.out === q && r.exitCode === 0;
  if (!ok) {
    fails++;
    const a = q.split('\n'), b = r.out.split('\n');
    let k = 0; while (k < a.length && a[k] === b[k]) k++;
    console.log(`FAIL ${t} ${v.join(' ')}: first difference at line ${k + 1}:\n  qemu: ${a[k]}\n  ours: ${b[k]}  (exit ${r.exitCode}, pc ${(r.cpu.pc >>> 0).toString(16)})`);
  } else console.log(`ok   ${t} ${v.join(' ')}  ${(r.insns / 1e6).toFixed(1)}M insns, ${r.mips.toFixed(1)} MIPS${jit ? ' (jit)' : ''}`);
}
process.exit(fails ? 1 : 0);
