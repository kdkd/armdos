// quick manual runner: node apps/x86/tests/try.mjs "X86 \X\HELLO.COM" [more commands...]
import fs from 'node:fs';
import { session } from '../../dosutil/tests/harness.mjs';
import { x86state } from './x86state.mjs';
const files = [{ src: 'build/ELBOW.EXE', dst: 'DOS\\' }, { src: 'build/HIMEM.SYS', dst: 'DOS\\' }];
for (const f of fs.readdirSync('build/x86-test')) files.push({ src: 'build/x86-test/' + f, dst: 'X\\' });
if (fs.existsSync('build/x86-ms4')) for (const f of fs.readdirSync('build/x86-ms4')) files.push({ src: 'build/x86-ms4/' + f, dst: 'M\\' });
const himem = !process.env.NOHIMEM;
const s = await session({ name: 'x86try', files, dirs: ['X', 'M'],
  config: (himem ? 'DEVICE=C:\\DOS\\HIMEM.SYS\n' : '') + 'FILES=20\nSHELL=C:\\T\\TSHELL.EXE\n' });
for (const cmd of process.argv.slice(2)) {
  const t0 = s.pc.timeMs;
  let out;
  try { out = s.run(cmd, { timeoutMs: +(process.env.TMO || 60000) }); } catch (e) { console.log('ERR', e.message); console.log(x86state(s.pc)); s.pc.run(50); console.log(x86state(s.pc)); break; }
  console.log(`--- ${cmd}  (${((s.pc.timeMs - t0) / 1000).toFixed(2)} s emulated)`);
  console.log(out.join('\n'));
}
if (s.pc.faults.length) console.log('faults', s.pc.faults);
if (s.pc.debug) console.log('E9:', s.pc.debug);
