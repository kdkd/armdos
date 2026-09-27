// Optional fixed-work benchmark. Build mathbench.com and bench.com with make,
// then pass one or more ELBOW builds to compare, for example:
// node apps/x86/tests/bench.mjs build/before/ELBOW.EXE build/ELBOW.EXE
import assert from 'node:assert/strict';
import { session } from '../../dosutil/tests/harness.mjs';

const builds = process.argv.slice(2);
if (!builds.length) builds.push('build/ELBOW.EXE');
for (let i = 0; i < builds.length; i++) {
  const bin = builds[i];
  const s = await session({ name: `x86-bench-${i}`, files: [
    { src: bin, dst: 'DOS\\ELBOW.EXE' }, { src: 'build/HIMEM.SYS', dst: 'DOS\\' },
    { src: 'build/x86-test/mathbench.com', dst: 'WORK\\MATH.COM' },
    { src: 'build/x86-test/bench.com', dst: 'WORK\\BENCH.COM' },
  ], config: 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=20\nSHELL=C:\\T\\TSHELL.EXE\n' });
  for (const test of ['MATH', 'BENCH']) {
    const arm = s.pc.cpu.icount, host = performance.now();
    const output = s.run(`\\DOS\\ELBOW /JIT \\WORK\\${test}.COM`, { cls: false, timeoutMs: 120000 });
    assert(output.includes(test === 'MATH' ? 'MATH OK' : '72E6C03B'), output.join('\n'));
    assert.equal(s.pc.faults.length, 0, JSON.stringify(s.pc.faults));
    console.log(JSON.stringify({ bin, test, armInstructions: s.pc.cpu.icount - arm, hostMs: performance.now() - host, output }));
  }
}
