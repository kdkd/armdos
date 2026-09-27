#!/usr/bin/env node
// The VGA's register model and planar memory (emu/dev/vga.mjs, render.mjs):
// the mode register's IBM register sets, chain-4 RAM vs the planar MMIO window
// at A0000h (and moving the picture between them), the graphics controller's
// write modes 0-3 (rotate, set/reset, AND/OR/XOR, bit mask, map mask) and read
// modes 0/1 (map select, colour compare / don't care) with latches, guest ARM
// code on the window (interpreter and JIT, which recompiles its loads when the
// window opens), and the renderer: mode 13h fast path, Mode X/Y, 16-colour
// planar modes, start address, offset, double scan, horizontal pixel/byte
// panning, line compare split screen with the pan reset, the attribute
// palette, and per-scan-line register changes from the frame timeline.
// Expected values follow the IBM VGA Technical Reference.
import { Machine } from '../../machine.mjs';
import { renderScreen, displaySize } from '../../render.mjs';
import { FRAME_NS, VRETRACE_NS, ACTIVE_NS } from '../../dev/vga.mjs';

let fails = 0, passes = 0;
const eq = (name, got, want) => {
  const ok = JSON.stringify(got) === JSON.stringify(want);
  if (ok) passes++; else { fails++; console.log(`FAIL ${name}: got ${JSON.stringify(got)} want ${JSON.stringify(want)}`); }
};
const hex = (v) => v.toString(16);
function mk(o = {}) {
  const m = new Machine({ jit: false, ...o });
  m.cpu.halted = 1; m.cpu.i = 1;
  return m;
}
const seq = (m, i, v) => { m.out8(0x3C4, i); m.out8(0x3C5, v); };
const gc = (m, i, v) => { m.out8(0x3CE, i); m.out8(0x3CF, v); };
const crtc = (m, i, v) => { m.out8(0x3D4, i); m.out8(0x3D5, v); };
const attr = (m, i, v) => { m.in8(0x3DA); m.out8(0x3C0, i); m.out8(0x3C0, v); m.out8(0x3C0, 0x20); };
const dac = (m, i, r, g, b) => { m.out8(0x3C8, i); m.out8(0x3C9, r); m.out8(0x3C9, g); m.out8(0x3C9, b); };
const wr = (m, a, v) => m.write(0xA0000 + a, 1, v);
const rd = (m, a) => m.read(0xA0000 + a, 1);
const plane = (m, o, p) => m.vga.vram[(o << 2) | p];
const planes = (m, o) => [0, 1, 2, 3].map((p) => hex(plane(m, o, p)));
const c6 = (v) => (v << 2) | (v >> 4);
const pix = (img, x, y) => Array.from(img.data.slice((y * img.width + x) * 4, (y * img.width + x) * 4 + 3));
function modeX(m) {             // mode 13h, then "unchain": Mode X 320x240 is not needed, 320x200 unchained
  m.out8(0x3E0, 0x13);
  seq(m, 4, 0x06);
  crtc(m, 0x14, 0x00); crtc(m, 0x17, 0xE3);
}

// ------------------------------------------------ register sets, window, chain-4
{
  const m = mk();
  eq('power-on: text registers', [m.vga.displayKind(), hex(m.vga.gc[6]), hex(m.vga.seq[4]), hex(m.vga.crtc[9])], ['text', 'e', '2', '4f']);
  eq('text renders 720x400', displaySize(m.vga), [720, 400]);
  m.out8(0x3E0, 0x13);
  eq('mode 13h registers', [hex(m.vga.misc), hex(m.vga.seq[4]), hex(m.vga.crtc[0x14]), hex(m.vga.gc[5]), hex(m.vga.attr[0x10])], ['63', 'e', '40', '40', '41']);
  eq('mode 13h: no window, RAM', [m.vga.window, m.cpu.pflags[0xA0000 >>> 7] & 8, m.cpu.mmioSeg], [false, 0, 0x10000]);
  eq('mode 13h size', displaySize(m.vga), [320, 200]);
  // a chain-4 picture in RAM
  for (let i = 0; i < 64000; i++) m.cpu.m8[0xA0000 + i] = (i * 7) & 0xFF;
  const img = renderScreen(m);
  const d = m.vga.dac;
  eq('mode 13h fast path pixel', pix(img, 5, 3), [c6(d[((3 * 320 + 5) * 7 & 0xFF) * 3]), c6(d[((3 * 320 + 5) * 7 & 0xFF) * 3 + 1]), c6(d[((3 * 320 + 5) * 7 & 0xFF) * 3 + 2])]);
  // chain-4 off: the window opens and the picture moves into the planes (offset i & ~3, plane i & 3)
  seq(m, 4, 0x06);
  eq('unchained: window open', [m.vga.window, m.cpu.pflags[0xA0000 >>> 7] & 8, m.cpu.pflags[0xAFF80 >>> 7] & 8, m.cpu.pflags[0xB0000 >>> 7] & 8, m.cpu.mmioSeg], [true, 8, 8, 0, 0xA]);
  eq('chain-4 picture in the planes', [plane(m, 4, 1), plane(m, 320, 3)], [(5 * 7) & 0xFF, (323 * 7) & 0xFF]);
  // the same registers otherwise (doubleword mode): the picture looks the same
  const img2 = renderScreen(m);
  eq('unchained with DW addressing shows the same picture', [pix(img2, 5, 3), pix(img2, 319, 199)], [pix(img, 5, 3), pix(img, 319, 199)]);
  // back to chain-4: planes -> RAM
  wr(m, 0, 0); // (map mask 0F after 13h: all planes; offset 0 -> 0)
  seq(m, 2, 0x02); wr(m, 4, 0x99);        // plane 1, offset 4 = chain-4 address 5
  seq(m, 4, 0x0E);
  eq('chained again: window closed, planes -> RAM', [m.vga.window, m.cpu.mmioSeg, m.cpu.m8[0xA0005], m.cpu.m8[0xA0000]], [false, 0x10000, 0x99, 0]);
  m.out8(0x3E0, 0x03);
  eq('back to text', [m.vga.displayKind(), displaySize(m.vga)], ['text', [720, 400]]);
  m.out8(0x3E0, 0x12);
  eq('mode 12h: planar window, 640x480', [m.vga.window, displaySize(m.vga)], [true, [640, 480]]);
  m.reset();
  eq('reset closes the window', [m.vga.window, m.cpu.mmioSeg, m.cpu.pflags[0xA0000 >>> 7] & 8], [false, 0x10000, 0]);
  m.out8(0x3E0, 0x0D);
  eq('mode 0Dh 320x200', displaySize(m.vga), [320, 200]);
  m.out8(0x3E0, 0x10);
  eq('mode 10h 640x350', displaySize(m.vga), [640, 350]);
  m.out8(0x3E0, 0x04);
  eq('CGA mode 4 stays CGA', [m.vga.displayKind(), m.vga.window, displaySize(m.vga)], ['cga', false, [320, 200]]);
}

// ------------------------------------------------ write modes, read modes, latches
{
  const m = mk();
  m.out8(0x3E0, 0x12);                     // planar, write mode 0, map mask 0F, bit mask FF
  wr(m, 10, 0xA5);
  eq('write mode 0: all planes', planes(m, 10), ['a5', 'a5', 'a5', 'a5']);
  seq(m, 2, 0x05); wr(m, 11, 0x3C);
  eq('map mask 0101', planes(m, 11), ['3c', '0', '3c', '0']);
  seq(m, 2, 0x0F);
  // set/reset: enable planes 0,1; values 1,0 -> plane0 FF, plane1 00, planes 2,3 CPU data
  gc(m, 0, 0x01); gc(m, 1, 0x03); wr(m, 12, 0x81);
  eq('set/reset + enable', planes(m, 12), ['ff', '0', '81', '81']);
  gc(m, 1, 0x00);
  // rotate 3: 0x81 -> 0x30
  gc(m, 3, 0x03); wr(m, 13, 0x81);
  eq('data rotate', planes(m, 13), ['30', '30', '30', '30']);
  // latches + ALU: read offset 10 (A5 x4) then write 0F with AND / OR / XOR
  gc(m, 3, 0x08); rd(m, 10); wr(m, 14, 0x0F);
  eq('AND with latches', planes(m, 14), ['5', '5', '5', '5']);
  gc(m, 3, 0x10); rd(m, 10); wr(m, 15, 0x0F);
  eq('OR with latches', planes(m, 15), ['af', 'af', 'af', 'af']);
  gc(m, 3, 0x18); rd(m, 10); wr(m, 16, 0x0F);
  eq('XOR with latches', planes(m, 16), ['aa', 'aa', 'aa', 'aa']);
  gc(m, 3, 0x00);
  // bit mask: bits from the latches where the mask is 0
  gc(m, 8, 0xF0); rd(m, 10); wr(m, 17, 0x00);
  eq('bit mask keeps latched bits', planes(m, 17), ['5', '5', '5', '5']);
  gc(m, 8, 0xFF);
  // write mode 1: the latches of offset 11 copied to 18 (whatever the data)
  gc(m, 5, 0x01); rd(m, 11); wr(m, 18, 0x77);
  eq('write mode 1 copies the latches', planes(m, 18), ['3c', '0', '3c', '0']);
  // write mode 2: colour 1010b in each bit selected by the bit mask
  gc(m, 5, 0x02); gc(m, 8, 0x80); rd(m, 20); wr(m, 20, 0x0A);
  eq('write mode 2 + bit mask', planes(m, 20), ['0', '80', '0', '80']);
  gc(m, 8, 0x01); rd(m, 20); wr(m, 20, 0x05);
  eq('write mode 2, second pixel', planes(m, 20), ['1', '80', '1', '80']);
  // write mode 3: set/reset colour, (rotated data AND bit mask) as the mask
  gc(m, 5, 0x03); gc(m, 8, 0x3C); gc(m, 0, 0x0C); rd(m, 21); wr(m, 21, 0xF0);
  eq('write mode 3', planes(m, 21), ['0', '0', '30', '30']);
  gc(m, 5, 0x00); gc(m, 8, 0xFF); gc(m, 0, 0);
  // read mode 0: read map select
  gc(m, 4, 2); eq('read map select 2', rd(m, 12), 0x81);
  gc(m, 4, 1); eq('read map select 1', rd(m, 12), 0x00);
  gc(m, 4, 0);
  // read mode 1: colour compare. offset 20: pixel 0 = 1010b, pixel 7 = 0101b, others 0
  gc(m, 5, 0x08); gc(m, 7, 0x0F); gc(m, 2, 0x0A);
  eq('read mode 1: compare 1010', rd(m, 20), 0x80);
  gc(m, 2, 0x05); eq('read mode 1: compare 0101', rd(m, 20), 0x01);
  gc(m, 2, 0x00); eq('read mode 1: compare 0000', rd(m, 20), 0x7E);
  gc(m, 7, 0x05); gc(m, 2, 0x00);          // only planes 0 and 2 matter: 1010b -> x0x0 matches 0
  eq('read mode 1: colour don\'t care', rd(m, 20), 0xFE);
  gc(m, 7, 0x00); eq('read mode 1: don\'t care everything', rd(m, 20), 0xFF);
  gc(m, 5, 0x00);
  // 16/32-bit bus accesses are bytes in order (the last read's latches remain)
  m.write(0xA0000 + 40, 4, 0x44332211);
  eq('32-bit store = 4 byte writes', [plane(m, 40, 0), plane(m, 41, 1), plane(m, 42, 2), plane(m, 43, 3)], [0x11, 0x22, 0x33, 0x44]);
  eq('32-bit load = 4 byte reads', hex(m.read(0xA0000 + 40, 4)), '44332211');
  eq('latches from the last byte', hex(m.vga.latch >>> 0), '44444444');
}

// ------------------------------------------------ guest code on the window (interpreter + JIT)
for (const jit of [false, true]) {
  const m = new Machine({ jit });
  m.cpu.i = 1;
  // a Mode X "copy through the latches" loop: r0 = A0000h (src), r1 = A0100h (dst), 64 bytes
  //   loop: ldrb r2,[r0],#1 ; strb r2,[r1],#1 ; subs r3,r3,#1 ; bne loop ; b .
  const code = [0xE3A0080A, 0xE2801C01, 0xE3A03040, 0xE4D02001, 0xE4C12001, 0xE2533001, 0x1AFFFFFB, 0xEAFFFFFE];
  const put = () => code.forEach((w, i) => { const a = 0x8000 + i * 4; m.cpu.m8[a] = w & 255; m.cpu.m8[a + 1] = (w >>> 8) & 255; m.cpu.m8[a + 2] = (w >>> 16) & 255; m.cpu.m8[a + 3] = w >>> 24; });
  put();
  // warm the JIT on plain RAM first (mode 13h: A0000h is RAM), so the window has to flush it
  m.out8(0x3E0, 0x13);
  for (let k = 0; k < 20; k++) { m.cpu.pc = 0x8000; m.cpu.halted = 0; m.runFor(0.05); }
  modeX(m);
  for (let o = 0; o < 64; o++) for (let p = 0; p < 4; p++) m.vga.vram[(o << 2) | p] = (o * 4 + p) & 0xFF;
  gc(m, 5, 0x41);                          // write mode 1
  m.cpu.pc = 0x8000; m.cpu.halted = 0;
  m.runFor(1);
  gc(m, 5, 0x40);
  const ok = [0, 1, 17, 63].map((o) => planes(m, 0x100 + o).join(','));
  eq(`guest latch copy (jit ${jit})`, ok, [0, 1, 17, 63].map((o) => [0, 1, 2, 3].map((p) => hex((o * 4 + p) & 0xFF)).join(',')));
  // read mode 0 through guest LDRB: plane 2 of offset 5
  gc(m, 4, 2);
  // ldr r0,=0xA0005 via mov/orr ; ldrb r2,[r0] ; b .
  const c2 = [0xE3A0080A, 0xE3800005, 0xE5D02000, 0xEAFFFFFE];
  c2.forEach((w, i) => { const a = 0x9000 + i * 4; m.cpu.m8[a] = w & 255; m.cpu.m8[a + 1] = (w >>> 8) & 255; m.cpu.m8[a + 2] = (w >>> 16) & 255; m.cpu.m8[a + 3] = w >>> 24; });
  m.cpu.pc = 0x9000; m.cpu.halted = 0; m.runFor(0.1);
  eq(`guest read map select (jit ${jit})`, m.cpu.r[2], (5 * 4 + 2) & 0xFF);
}

// ------------------------------------------------ rendering
{
  // Mode X 320x200: pixel (x, y) = plane x & 3, offset y * 80 + x / 4
  const m = mk();
  modeX(m);
  for (let i = 0; i < 256; i++) dac(m, i, i & 63, (i >> 2) & 63, 63 - (i & 63));
  seq(m, 2, 1 << (7 & 3)); wr(m, 10 * 80 + (7 >> 2), 200);         // (7, 10)
  seq(m, 2, 1 << (318 & 3)); wr(m, 199 * 80 + (318 >> 2), 77);     // (318, 199)
  let img = renderScreen(m);
  const col = (i) => [c6(i & 63), c6((i >> 2) & 63), c6(63 - (i & 63))];
  eq('Mode X size', [img.width, img.height], [320, 200]);
  eq('Mode X pixels', [pix(img, 7, 10), pix(img, 318, 199), pix(img, 6, 10)], [col(200), col(77), col(0)]);
  // start address (in bytes of a plane) scrolls by 4 pixels per unit: start 1 -> pixel 7 at x = 3
  crtc(m, 0x0C, 0); crtc(m, 0x0D, 1);
  m.runFor(40);             // (the start address is latched per frame)
  img = renderScreen(m);
  eq('start address', pix(img, 3, 10), col(200));
  // horizontal pixel panning in 256 colours: register value 2 = 1 pixel
  attr(m, 0x13, 2);
  m.runFor(40);
  img = renderScreen(m);
  eq('pixel panning (256 colours)', pix(img, 2, 10), col(200));
  attr(m, 0x13, 0); crtc(m, 0x0D, 0);
  // offset register: 0x50 -> 160 bytes per line (640 virtual pixels)
  crtc(m, 0x13, 0x50);
  seq(m, 2, 1 << (5 & 3)); wr(m, 3 * 160 + (5 >> 2), 99);
  m.runFor(40);
  img = renderScreen(m);
  eq('offset register (virtual width 640)', pix(img, 5, 3), col(99));
  crtc(m, 0x13, 0x28);
  // Mode Y: max scan line 0 -> 400 lines
  crtc(m, 9, 0x40);
  m.runFor(40);
  eq('320x400 (max scan line 0)', displaySize(m.vga), [320, 400]);
  img = renderScreen(m);
  eq('320x400 row 10', pix(img, 7, 10), col(200));
  crtc(m, 9, 0x41);
  // line compare split: from line compare + 1 on, the display restarts at offset 0
  // (in scan lines: 400 per frame, double-scanned) - split at scan line 99 -> output row 50
  seq(m, 2, 0x0F);
  for (let o = 0; o < 80; o++) wr(m, 20 * 80 + o, 150);           // row 20 all colour 150
  crtc(m, 0x0C, (20 * 80) >> 8); crtc(m, 0x0D, (20 * 80) & 0xFF);   // top starts at row 20
  crtc(m, 0x18, 99); crtc(m, 7, m.vga.crtc[7] & ~0x10); crtc(m, 9, m.vga.crtc[9] & ~0x40);
  m.runFor(40);
  img = renderScreen(m);
  eq('split: top shows row 20 at y 0', pix(img, 100, 0), col(150));
  eq('split: y 50 shows row 0 (pixel (7,10) of row 0 at y 60)', [pix(img, 7, 60), pix(img, 100, 49)], [col(200), col(150 * 0)]);
  // pan reset below the split (attribute mode control bit 5)
  attr(m, 0x13, 2); attr(m, 0x10, 0x41 | 0x20);
  m.runFor(40);
  img = renderScreen(m);
  eq('split: PPM keeps the bottom unpanned', pix(img, 7, 60), col(200));
  attr(m, 0x10, 0x41);
  m.runFor(40);
  img = renderScreen(m);
  eq('split: without PPM the bottom pans too', pix(img, 6, 60), col(200));
}
{
  // 16-colour planar, mode 12h: attribute palette -> DAC; colour plane enable; panning
  const m = mk();
  m.out8(0x3E0, 0x12);
  for (let i = 0; i < 16; i++) attr(m, i, i);
  for (let i = 0; i < 16; i++) dac(m, i, i * 4, 0, 63 - i * 4);
  const col = (i) => [c6(i * 4), 0, c6(63 - i * 4)];
  gc(m, 5, 2);
  gc(m, 8, 0x80 >> (13 & 7)); rd(m, 100 * 80 + (13 >> 3)); wr(m, 100 * 80 + (13 >> 3), 9);   // (13, 100) = 9
  gc(m, 8, 0x80 >> (639 & 7)); rd(m, 479 * 80 + 79); wr(m, 479 * 80 + 79, 14);                // (639, 479) = 14
  gc(m, 5, 0); gc(m, 8, 0xFF);
  let img = renderScreen(m);
  eq('mode 12h size', [img.width, img.height], [640, 480]);
  eq('mode 12h pixels', [pix(img, 13, 100), pix(img, 639, 479), pix(img, 12, 100)], [col(9), col(14), col(0)]);
  attr(m, 0x12, 0x07);                      // plane 3 disabled: 9 -> 1
  img = renderScreen(m);
  eq('colour plane enable', pix(img, 13, 100), col(1));
  attr(m, 0x12, 0x0F);
  attr(m, 5, 9); attr(m, 9, 5);             // palette register 9 -> DAC 5
  img = renderScreen(m);
  eq('attribute palette', pix(img, 13, 100), col(5));
  attr(m, 9, 9);
  attr(m, 0x13, 3);                         // pan 3 pixels
  m.runFor(40);
  img = renderScreen(m);
  eq('pixel panning (16 colours)', pix(img, 10, 100), col(9));
  // mode 0Dh: 320x200, 40 bytes per line, double scanned
  m.out8(0x3E0, 0x0D);
  for (let i = 0; i < 16; i++) attr(m, i, i);
  gc(m, 5, 2); gc(m, 8, 0x80 >> (3 & 7)); rd(m, 199 * 40); wr(m, 199 * 40, 12); gc(m, 5, 0); gc(m, 8, 0xFF);
  img = renderScreen(m);
  eq('mode 0Dh size and pixel', [img.width, img.height, pix(img, 3, 199)], [320, 200, col(12)]);
}
{
  // raster effects: a DAC change in the middle of the displayed frame
  const m = mk();
  modeX(m);
  seq(m, 2, 0x0F);
  m.runFor(20);
  // go to the start of the next frame, then half way down its display
  const t0 = Math.ceil(m.timeNs() / FRAME_NS) * FRAME_NS;
  m.runUntil(t0 + 1000);
  dac(m, 0, 63, 0, 0);                      // red during the retrace: the whole frame
  m.runUntil(t0 + VRETRACE_NS + ACTIVE_NS / 2);
  dac(m, 0, 0, 0, 63);                      // blue from the middle on
  m.runUntil(t0 + FRAME_NS + 1000);         // that frame is complete
  const img = renderScreen(m);
  eq('raster: top half red', pix(img, 10, 20), [255, 0, 0]);
  eq('raster: bottom half blue', pix(img, 10, 180), [0, 0, 255]);
  const img2 = (m.runFor(40), renderScreen(m));
  eq('raster: next frame all blue', [pix(img2, 10, 20), pix(img2, 10, 180)], [[0, 0, 255], [0, 0, 255]]);
}
{
  // text mode, odd/even off with A0000h mapped: plane 2 is the character generator
  const m = mk();
  seq(m, 2, 0x04); seq(m, 4, 0x06); gc(m, 4, 2); gc(m, 5, 0); gc(m, 6, 0x04);
  eq('font access window', m.vga.window, true);
  wr(m, 65 * 32 + 3, 0x81);
  eq('plane 2 write reaches the font RAM', m.font[65 * 32 + 3], 0x81);
  seq(m, 2, 0x03); seq(m, 4, 0x02); gc(m, 4, 0); gc(m, 5, 0x10); gc(m, 6, 0x0E);
  eq('font access window closed', [m.vga.window, m.vga.displayKind()], [false, 'text']);
}
{
  // CRTC write protection (register 11h bit 7): 0-7 read-only, bit 4 of 7 stays writable
  const m = mk();
  crtc(m, 0x11, 0x8E); crtc(m, 1, 0x10); crtc(m, 7, 0x00);
  eq('CRTC protect', [hex(m.vga.crtc[1]), hex(m.vga.crtc[7])], ['4f', 'f']);
  crtc(m, 0x11, 0x0E); crtc(m, 1, 0x10);
  eq('CRTC unprotected', hex(m.vga.crtc[1]), '10');
}
{
  // render speed: Mode Y 320x400 and mode 12h (host ms per frame)
  const m = mk();
  modeX(m); crtc(m, 9, 0x40);
  for (let i = 0; i < 0x40000; i++) m.vga.vram[i] = i * 13;
  let img = renderScreen(m);
  let t = performance.now();
  for (let k = 0; k < 50; k++) img = renderScreen(m, img);
  const my = (performance.now() - t) / 50;
  m.out8(0x3E0, 0x12);
  for (let i = 0; i < 0x40000; i++) m.vga.vram[i] = i * 13;
  img = renderScreen(m);
  t = performance.now();
  for (let k = 0; k < 50; k++) img = renderScreen(m, img);
  const m12 = (performance.now() - t) / 50;
  m.out8(0x3E0, 0x13);
  img = renderScreen(m);
  t = performance.now();
  for (let k = 0; k < 50; k++) img = renderScreen(m, img);
  const m13 = (performance.now() - t) / 50;
  console.log(`render: 320x400x256 unchained ${my.toFixed(2)} ms, 640x480x16 ${m12.toFixed(2)} ms, mode 13h ${m13.toFixed(2)} ms per frame`);
  eq('render fast enough (< 12 ms a frame)', my < 12 && m12 < 12, true);
}

console.log(`${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
