// Run a bare-metal test binary (loaded at 0x10000) on our CPU with a minimal
// bus: port 0xE9 = console out, port 0xF4 = exit. Used by the C CPU tests and
// the benchmark. No devices, no timers.
import { CPU } from '../../cpu.mjs';

export async function runBare(bin, { jit = false, maxInsns = 5e9, trace = 0 } = {}) {
  let out = '', exitCode = null;
  const cpu = new CPU({
    read: () => -1,
    write: (a, size, v) => {
      a >>>= 0;
      if (a === 0x100000E9) { out += String.fromCharCode(v & 0xFF); return true; }
      if (a === 0x100000F4) { exitCode = v & 0xFF; cpu.halted = 1; cpu.brk = 1; return true; }
      return false;
    },
  });
  if (jit) { const { JIT } = await import('../../jit.mjs'); cpu.jit = new JIT(cpu, { threshold: +(globalThis.process?.env?.JIT_THRESHOLD || 16) }); }
  if (trace) { cpu.trace = new Int32Array(trace * 2); }
  cpu.m8.set(bin, 0x10000);
  cpu.r[1] = 0; cpu.pc = 0x10000;
  const t0 = performance.now();
  let n = 0;
  while (exitCode === null && n < maxInsns) {
    const k = cpu.run(1e7);
    n += k;
    if (k === 0) break;
  }
  const ms = performance.now() - t0;
  return { out, exitCode, insns: cpu.icount, ms, mips: cpu.icount / ms / 1000, cpu };
}
