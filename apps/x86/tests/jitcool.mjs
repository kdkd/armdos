// Assert that a modified hot routine is translated again during cpu_run,
// rather than merely checking that the interpreter returns the right value.
import assert from 'node:assert/strict';
import { session } from '../../dosutil/tests/harness.mjs';
import { readElbow } from '../../../web/js/elbow-probe.js';

const s = await session({ name: 'x86-jitcool', files: [
  { src: process.env.ELBOW || 'build/ELBOW.EXE', dst: 'DOS\\ELBOW.EXE' },
  { src: 'build/HIMEM.SYS', dst: 'DOS\\' },
  { src: 'build/x86-test/jitcool.com', dst: 'WORK\\JITCOOL.COM' },
], config: 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=20\nSHELL=C:\\T\\TSHELL.EXE\n' });
s.pc.type('\\DOS\\ELBOW /JIT \\WORK\\JITCOOL.COM\r');
assert(s.pc.waitText('COOL WARM', { timeoutMs: 30000 }), s.pc.screen());
let E = readElbow(s.pc.machine);
assert(E?.jit, 'ELBOW JIT active');
const worker = E.blocks().find((b) => !b.dead && b.lin - (b.cs << 4) === 0x200);
assert(worker, 'routine was hot and translated before modification');
s.pc.type(' ');
assert(s.pc.waitText('COOL DONE', { timeoutMs: 30000 }), s.pc.screen());
E = readElbow(s.pc.machine);
const blocks = E.blocks().filter((b) => b.lin === worker.lin && b.cs === worker.cs);
assert(blocks.some((b) => b.dead && b.code === worker.code), 'original block invalidated');
assert(blocks.some((b) => !b.dead && b.code !== worker.code), 'cooldown expired and routine was translated again');
s.pc.type(' ');
assert(s.waitPrompt(), s.pc.screen());
assert.equal(s.pc.faults.length, 0, JSON.stringify(s.pc.faults));
console.log('ok SMC cooldown expires during cpu_run and the patched routine is translated again');
