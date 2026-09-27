#!/usr/bin/env node
// Runs the emulator's test suites. Needs arm-none-eabi-* and qemu-system-arm
// for the differential tests (those are skipped with --quick).
// usage: node emu/tests/run-all.mjs [--quick] [--browser]
import { spawnSync } from 'node:child_process';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const T = dirname(fileURLToPath(import.meta.url));
const quick = process.argv.includes('--quick');
const suites = [
  ['cpu unit (interpreter)', ['cpu/unit.mjs']],
  ['cpu unit (jit)', ['cpu/unit.mjs', '--jit']],
  ['jit: program reloaded at the same address', ['cpu/reload.mjs']],
  ['disassembler vs objdump', ['disasm/test-disasm.mjs']],
  ['devices (ports, no CPU)', ['machine/devices.mjs']],
  ['VGA mode 62h: 640x480x256 linear frame buffer', ['machine/vga62.mjs']],
  ['VGA planar/unchained model, window, renderer', ['machine/vga-planar.mjs']],
  ['ATA streaming sector source', ['machine/ata-stream.mjs']],
  ['ATAPI CD-ROM + CD audio', ['machine/atapi.mjs']],
  ['Sound Blaster 16 / DMA / OPL3', ['machine/sb16.mjs']],
  ['COM2 16550A + modem + phone exchange', ['machine/modem.mjs']],
  ['modem from ARM-DOS (INT 14h, COM2)', ['machine/modem-dos.mjs']],
  ['open the box: cards, SIMMs, clock + ARM-DOS', ['machine/hardware.mjs']],
  ['ZMODEM vs lrzsz sz/rz', ['zmodem/lrzsz.mjs']],
  ['Host Link 555-0100 + lrzsz over the modem', ['zmodem/hostlink.mjs']],
  ['modem training sounds (V.90 structure)', ['modemsound/check.mjs']],
  ['the scripted numbers (WOPR, KREMVAX, ...)', ['machine/phonelines.mjs']],
  ['machine / device test ROM', ['machine/rom-test.mjs']],
  ['spin-loop idle skip is exact', ['machine/spin.mjs']],
  ['jit: many programs loaded at one address with a resident TSR', ['machine/tsr-reload.mjs']],
  ['memory activity map (counters, regions)', ['machine/memmap.mjs']],
  ['game port 201h: timing, ISA cycle, polling loops', ['machine/gameport.mjs']],
  ['machine performance', ['machine/perf.mjs']],
  ['floating point: VFP vs soft-float', ['cpu/fpbench.mjs', '--rounds', '2']],
];
// the differential checks need qemu-system-arm (and the ARM toolchain): without it, say so and skip them
const haveQemu = spawnSync('sh', ['-c', 'command -v qemu-system-arm'], { encoding: 'utf8' }).status === 0;
if (!quick && !haveQemu) console.log('skip the differential checks vs QEMU: qemu-system-arm is not installed (apt install qemu-system-arm / brew install qemu)');
if (!quick && haveQemu) {
  for (const seed of [1, 2, 3]) {
    suites.push([`cpu vs qemu, seed ${seed} (interpreter)`, ['cpu/difftest.mjs', '--seed', String(seed), '--per', '1000']]);
    suites.push([`cpu vs qemu, seed ${seed + 100} (jit)`, ['cpu/difftest.mjs', '--seed', String(seed + 100), '--per', '1000', '--jit']]);
  }
  for (const seed of [1, 2]) {
    suites.push([`vfp vs qemu, seed ${seed} (interpreter)`, ['cpu/vfpdiff.mjs', '--seed', String(seed), '--per', '1000']]);
    suites.push([`vfp vs qemu, seed ${seed + 100} (jit)`, ['cpu/vfpdiff.mjs', '--seed', String(seed + 100), '--per', '400', '--jit']]);
  }
  suites.push(['vfp vs qemu, exact flags (interpreter)', ['cpu/vfpdiff.mjs', '--seed', '7', '--per', '1000', '--exact']]);
  suites.push(['vfp vs qemu, exact flags (jit)', ['cpu/vfpdiff.mjs', '--seed', '8', '--per', '300', '--exact', '--jit']]);
  suites.push(['jit: native libgcc division is exact', ['cpu/divtest.mjs']]);
  suites.push(['C programs vs qemu (interpreter)', ['cpu/ctest.mjs']]);
  suites.push(['C programs vs qemu (jit)', ['cpu/ctest.mjs', '--jit']]);
}
let failed = 0;
for (const [name, args] of suites) {
  const t0 = Date.now();
  const r = spawnSync(process.execPath, [join(T, args[0]), ...args.slice(1)], { encoding: 'utf8' });
  const ok = r.status === 0;
  if (!ok) failed++;
  const last = (r.stdout || '').trim().split('\n').pop();
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name.padEnd(40)} ${((Date.now() - t0) / 1000).toFixed(1).padStart(6)} s  ${last}`);
  if (!ok) console.log((r.stdout || '').split('\n').slice(-20).join('\n') + (r.stderr || ''));
}
if (process.argv.includes('--browser')) {
  const r = spawnSync(process.env.PYTHON || 'python3', [join(T, 'browser', 'run.py')], { encoding: 'utf8' });
  console.log(`${r.status === 0 ? 'ok  ' : 'FAIL'} browser (playwright)`);
  console.log(r.stdout.trim());
  if (r.status !== 0) { failed++; console.log(r.stderr); }
}
console.log(failed ? `${failed} suite(s) failed` : 'all suites passed');
process.exit(failed ? 1 : 0);
