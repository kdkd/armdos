#!/usr/bin/env node
// apps/hercules/tests/run.mjs - the Hercules card + mono monitor option ("make hercules-test").
//
//   node apps/hercules/tests/run.mjs [--shots DIR]
//
// Boots build/hd.img with new Machine({ video: 'hercules' }) and checks the POST
// (detection, the configuration box), DOS in mode 7 at B0000h (DIR), MODE,
// the games' "requires a VGA" exits, HERCULES.EXE in 720x348 graphics and back,
// and that a VGA machine still boots as before. With --shots, PNGs of the
// screens go to DIR (to be looked at).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { mdaAttr } from '../../../emu/render-hgc.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const B = (p) => path.join(ROOT, 'build', p);
const args = process.argv.slice(2);
const shots = args.includes('--shots') ? args[args.indexOf('--shots') + 1] : null;
if (shots) fs.mkdirSync(shots, { recursive: true });
let failures = 0;
const check = (ok, what, extra = '') => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${extra ? `  (${extra})` : ''}`); if (!ok) failures++; return ok; };
const png = async (pc, name) => { if (shots) await pc.png(path.join(shots, name + '.png')); };

// ---- the MDA attribute rules (render-hgc.mjs)
{
  const t = (a) => mdaAttr(a, true, false).join(',');
  check(t(0x07) === '1,0,false' && t(0x0F) === '2,0,false' && t(0x01) === '1,0,true' && t(0x09) === '2,0,true' &&
        t(0x70) === '0,1,false' && t(0x00) === '0,0,false' && t(0x88) === '0,0,false' && t(0x17) === '1,0,false',
        'MDA attributes: normal, bright, underline, reverse, non-display; other backgrounds are black');
  check(mdaAttr(0x87, true, true)[0] === 0 && mdaAttr(0x87, true, false)[0] === 1, 'bit 7 blinks with blink enabled');
}

const pc = await boot({ rom: B('rom.bin'), hd: B('hd.img'), video: 'hercules', monitor: 'green' });
const m = pc.machine, m8 = m.cpu.m8;
check(pc.waitText('Monochrome (Hercules)', { timeoutMs: 20000, stepMs: 2 }), 'POST box: Display Type : Monochrome (Hercules)');
await png(pc, 'post');
check(pc.waitText('C:\\>', { timeoutMs: 60000 }), 'DOS boots to C:\\> on the mono card');
check(m8[0x449] === 7 && (m8[0x410] & 0x30) === 0x30 && (m8[0x463] | (m8[0x464] << 8)) === 0x3B4,
      'BIOS: mode 7, equipment word 80x25 mono, CRTC 3B4h');
check(m.hgc.crtc[10] === 0x0B && m.hgc.crtc[11] === 0x0C, 'cursor lines 0Bh-0Ch');
check(m8[0xB8000] === 0xFF && m8[0xA0000] === 0xFF, 'no VGA memory: B8000h/A0000h are an empty bus');
pc.type('DIR\r'); pc.waitIdle();
check(pc.hasText('COMMAND  COM') && pc.hasText('bytes free'), 'DIR lists C:\\ in mode 7');
await png(pc, 'dir');
pc.type('MODE CO80\r'); pc.waitIdle();
check(pc.hasText('Function not supported on this computer - CO80'), 'MODE CO80: not supported without a colour card');
pc.type('MODE MONO\r'); pc.waitIdle();
check(m8[0x449] === 7 && !pc.hasText('- MONO'), 'MODE MONO works');
pc.type('CD \\GAMES\\DOOM\rDOOM\rCD \\\r'); pc.waitIdle({ timeoutMs: 20000 });
check(pc.hasText('DOOM requires a VGA.'), 'DOOM: "DOOM requires a VGA."');
pc.type('CLS\rC:\\DEMO\\HERCULES\r');
check(pc.until(() => m.hgc.graphics, { timeoutMs: 15000 }), 'HERCULES.EXE: the card is in 720x348 graphics');
pc.run(1500);
const img = pc.render();
let lit = 0; for (let i = 0; i < img.data.length; i += 4) if (img.data[i + 1] > 100) lit++;
check(img.width === 720 && img.height === 348 && lit > 20000, 'graphics picture: 720x348 with the chip drawn', `${lit} lit pixels`);
check((m.hgc.config & 3) === 3, 'configuration switch set to "full" (two pages)');
await png(pc, 'hgc-chip');
pc.type(' '); pc.run(1000);
await png(pc, 'hgc-chart');
pc.type('{ESC}');
check(pc.until(() => !m.hgc.graphics && pc.hasText('Hercules Graphics Card demo'), { timeoutMs: 10000 }), 'back to text mode with the closing line');
check(m.hgc.config === 0 && m8[0x449] === 7, 'configuration switch back to 0, mode 7');
await png(pc, 'after');

// ---- the VGA machine is unchanged
const vga = await boot({ rom: B('rom.bin'), hd: B('hd.img') });
check(vga.waitText('VGA/EGA', { timeoutMs: 20000, stepMs: 2 }) && vga.waitText('C:\\>', { timeoutMs: 60000 }) &&
      vga.machine.cpu.m8[0x449] === 3 && (vga.machine.cpu.m8[0x410] & 0x30) === 0x20, 'VGA machine: POST VGA/EGA, mode 3, colour equipment bits');

console.log(failures ? `\n${failures} failure(s)` : '\nall Hercules tests passed');
process.exit(failures ? 1 : 0);
