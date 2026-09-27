#!/usr/bin/env node
// Mode 62h, the ARM-PC's 640x480 256-colour linear mode (ARCH.md §6): the
// mode register opens video memory from A0000h to A0000h + 640*480 (through
// the adapter hole), the renderer shows it through the DAC, and leaving the
// mode (or a reset) closes the hole again. Also through guest code: stores
// by the CPU (interpreter and JIT) land in the frame buffer.
import { Machine, LFB_END } from '../../machine.mjs';
import { renderScreen } from '../../render.mjs';

let fails = 0, passes = 0;
const eq = (name, got, want) => {
  const ok = JSON.stringify(got) === JSON.stringify(want);
  if (ok) passes++; else { fails++; console.log(`FAIL ${name}: got ${JSON.stringify(got)} want ${JSON.stringify(want)}`); }
};
function mk(o = {}) {
  const m = new Machine({ jit: false, ...o });
  m.cpu.halted = 1; m.cpu.i = 1;
  return m;
}

{
  const m = mk();
  eq('LFB_END', LFB_END, 0xEB000);
  eq('hole reads FF before', m.cpu.m8[0xD0000], 0xFF);
  eq('hole protected before', m.cpu.pflags[0xD0000 >>> 7], 1);
  m.out8(0x3E0, 0x62);
  eq('mode register reads 62h', m.in8(0x3E0), 0x62);
  eq('frame buffer cleared', [m.cpu.m8[0xA0000], m.cpu.m8[0xC0000], m.cpu.m8[LFB_END - 1]], [0, 0, 0]);
  eq('hole is RAM up to LFB_END', [m.cpu.pflags[0xC0000 >>> 7], m.cpu.pflags[(LFB_END - 1) >>> 7]], [0, 0]);
  eq('rest of the hole stays empty', [m.cpu.pflags[LFB_END >>> 7], m.cpu.m8[LFB_END]], [1, 0xFF]);
  // a pixel at (639, 479) and one at (0, 0); DAC entry 1 = blue (0,0,42), 4 = red
  m.cpu.m8[0xA0000] = 4;
  m.cpu.m8[0xA0000 + 479 * 640 + 639] = 1;
  const img = renderScreen(m);
  eq('render size', [img.width, img.height], [640, 480]);
  const px = (x, y) => Array.from(img.data.slice((y * 640 + x) * 4, (y * 640 + x) * 4 + 3));
  eq('render (0,0) red', px(0, 0), [170, 0, 0]);
  eq('render (639,479) blue', px(639, 479), [0, 0, 170]);
  eq('render background black', px(320, 240), [0, 0, 0]);
  // DAC change shows
  m.out8(0x3C8, 4); m.out8(0x3C9, 63); m.out8(0x3C9, 63); m.out8(0x3C9, 0);
  eq('render follows the DAC', Array.from(renderScreen(m).data.slice(0, 3)), [255, 255, 0]);
  // back to text: hole closed again
  m.out8(0x3E0, 0x03);
  eq('hole closed', [m.cpu.m8[0xD0000], m.cpu.pflags[0xD0000 >>> 7]], [0xFF, 1]);
  eq('text size', renderScreen(m).width, 720);
  // reset closes it too
  m.out8(0x3E0, 0x62);
  m.reset();
  eq('reset closes the hole', [m.cpu.m8[0xD0000], m.cpu.pflags[0xD0000 >>> 7]], [0xFF, 1]);
}

// guest stores: STR/STRB into the hole part of the frame buffer, interpreter and JIT
for (const jit of [false, true]) {
  const m = new Machine({ jit });
  m.out8(0x3E0, 0x62);
  // at 0x8000: r0 = 0xD0000; r1 = 0x5A; strb r1,[r0]; str r1,[r0,#4]; ldr r2,[r0,#4]; b .
  const code = [0xE3A0080D, 0xE3A0105A, 0xE5C01000, 0xE5801004, 0xE5902004, 0xEAFFFFFE];
  code.forEach((w, i) => { const a = 0x8000 + i * 4; m.cpu.m8[a] = w & 255; m.cpu.m8[a + 1] = (w >>> 8) & 255; m.cpu.m8[a + 2] = (w >>> 16) & 255; m.cpu.m8[a + 3] = w >>> 24; });
  m.cpu.pc = 0x8000; m.cpu.halted = 0;
  m.runFor(1);
  eq(`guest stores reach the frame buffer (jit ${jit})`, [m.cpu.m8[0xD0000], m.cpu.m8[0xD0004], m.cpu.r[2]], [0x5A, 0x5A, 0x5A]);
}

console.log(`${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
