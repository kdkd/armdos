// frame rate of Second Reality under ELBOW: runs the demo (setup keys given),
// then at each of the given emulated times samples the display once per
// 1/70 s for two seconds and counts the distinct pictures (= frames drawn).
//   MHZ=133 node apps/secondreality/tests/fps.mjs "<keys>" "<command>" t1,t2,...   (seconds after the setup)
import fs from 'node:fs';
import crypto from 'node:crypto';
import { session } from '../../dosutil/tests/harness.mjs';
import { renderScreen } from '../../../emu/render.mjs';
const SR = '3rdparty/secondreality';
const files = [{ src: 'build/ELBOW.EXE', dst: 'DOS\\' }, { src: 'build/HIMEM.SYS', dst: 'DOS\\' }];
for (const f of fs.readdirSync(SR)) files.push({ src: `${SR}/${f}`, dst: 'SR\\' });
const s = await session({ name: 'srfps' + (process.env.TAG || ''), files, dirs: ['SR'], sizeMB: 64, boot: process.env.MHZ ? { mhz: +process.env.MHZ } : undefined,
  config: 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=30\nSHELL=C:\\T\\TSHELL.EXE\n' });
s.pc.type('CD \\SR\r'); s.waitPrompt();
s.pc.type(process.argv[3] + '\r');
s.pc.run(3000); s.pc.type(process.argv[2]);
const t0 = s.pc.timeMs;
let img = null;
for (const t of process.argv[4].split(',').map(Number)) {
  const now = s.pc.timeMs - t0;
  if (t * 1000 > now) s.pc.run(t * 1000 - now);
  const seen = new Set(); let prev = '', changes = 0;
  for (let i = 0; i < 140; i++) {
    s.pc.run(1000 / 70);
    img = renderScreen(s.pc.machine, img);
    const h = crypto.createHash('md5').update(img.data).digest('hex');
    if (h !== prev) changes++;
    prev = h; seen.add(h);
  }
  await s.pc.png(`${process.env.OUT || '/tmp'}/fps${t}.png`);
  console.log(`t=${t}s: ${(changes / 2).toFixed(1)} picture changes/s, ${seen.size} distinct in 2 s  (${img.width}x${img.height})`);
}
