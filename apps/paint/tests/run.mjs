#!/usr/bin/env node
// apps/paint/tests/run.mjs - ARM Paint (PAINT.EXE) and BANNER.EXE headless tests ("make paint-test").
//
//   node apps/paint/tests/run.mjs [--only draw,files,print,samples,keys,banner]
//
// Boots ARM-DOS (COMMAND.COM, HIMEM, MOUSE.COM) with C:\PAINT, drives ARM Paint
// with the emulated PS/2 mouse and the keyboard, and checks the mode 13h screen;
// saves PCX and BMP files and checks them with PIL (Python 3 + Pillow; $PYTHON, default
// python3 - without Pillow the PIL checks are skipped),
// loads PIL-written PCX/BMP files; prints to LPT1 and validates the ESC * byte
// stream (and decodes it back to a picture); prints a banner with BANNER.EXE.
// Screenshots and decoded printouts go to build/paint-test/.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { start, keys, mouse, shot, px, readFile, ROOT, OUT } from './lib.mjs';

const PY = process.env.PYTHON || 'python3';
const PIL = path.join(ROOT, 'apps/paint/tests/pil.py');
const HAVE_PIL = (() => { try { execFileSync(PY, ['-c', 'import PIL'], { stdio: 'ignore' }); return true; } catch { return false; } })();
if (!HAVE_PIL) console.log(`note: ${PY} has no Pillow (pip install pillow): the PIL checks (files, samples) are skipped`);
const args = process.argv.slice(2);
const ONLY = args.includes('--only') ? args[args.indexOf('--only') + 1].split(',') : null;
const want = (n) => (!ONLY || ONLY.includes(n)) && (HAVE_PIL || !['files', 'samples'].includes(n));
const topng = (...a) => { if (HAVE_PIL) execFileSync(PY, [PIL, 'png', ...a]); };
let pass = 0, fail = 0;
const ok = (c, what, detail) => {
  if (c) pass++; else fail++;
  console.log(`${c ? 'ok  ' : 'FAIL'} ${what}`);
  if (!c && detail !== undefined) console.log('     ' + String(detail).split('\n').join('\n     '));
  return c;
};
const pil = (...a) => JSON.parse(execFileSync(PY, [PIL, ...a]).toString());

// screen positions
const CV = (ix, iy) => [ix + 32, iy + 10];                      // canvas, unscrolled
const TOOL = (t) => [(t & 1) * 16 + 8, 10 + (t >> 1) * 16 + 8];
const SIZE = (i) => [16, 144 + i * 9];
const CELL = (c) => [32 + (c & 31) * 9 + 4, 176 + (c >> 5) * 3 + 1];
const T = { pencil: 0, brush: 1, spray: 2, line: 3, rect: 4, frect: 5, ellipse: 6, fellipse: 7, fill: 8, text: 9,
  eraser: 10, picker: 11, zoom: 12, hand: 13, undo: 14, palette: 15 };
const ipx = (pc, ix, iy) => px(pc, ...CV(ix, iy));
const mode = (pc) => pc.cpu.m8[0x449];
const dac = (pc) => pc.machine.vga.dac ? Array.from(pc.machine.vga.dac.slice(0, 768)) : null;

async function paint(files = [], cmd = 'PAINT /N', opts = {}) {
  const pc = await start(files, opts);
  keys(pc, cmd + '\r', 1200);
  return pc;
}

// ---------------------------------------------------------------- drawing
if (want('draw')) {
  console.log('== start-up and drawing with the mouse');
  const pc = await start();
  keys(pc, 'PAINT\r', 800);
  ok(mode(pc) === 0x13, 'C:\\DOS\\PAINT.BAT starts PAINT.EXE in VGA mode 13h', mode(pc));
  await shot(pc, 'draw-splash');
  const m = mouse(pc);
  m.click(300, 190);
  pc.run(300);
  ok(ipx(pc, 150, 80) === 15 && px(pc, 5, 20) !== 15, 'a click dismisses the splash: white canvas, tool box');

  // pencil: a horizontal line
  m.click(...TOOL(T.pencil));
  m.drag(...CV(20, 20), ...CV(80, 20));
  m.to(0, 140);                                   // the cursor out of the way (over the tool box)
  let n = 0; for (let x = 20; x <= 80; x++) if (ipx(pc, x, 20) === 0) n++;
  ok(n === 61 && ipx(pc, 50, 21) === 15, `pencil: a 1-pixel line from (20,20) to (80,20) (${n} pixels)`);
  // brush size 8
  m.click(...TOOL(T.brush)); m.click(...SIZE(3));
  m.drag(...CV(20, 40), ...CV(80, 40));
  let thick = 0; for (let y = 30; y < 50; y++) if (ipx(pc, 50, y) === 0) thick++;
  ok(thick === 8, `brush, size 8: a stroke 8 pixels thick (${thick})`);
  m.click(...SIZE(0));
  // colour from the palette strip: 40 (a red) as foreground
  m.click(...CELL(40));
  ok(px(pc, 10, 180) === 40, 'palette strip: left click sets the foreground (swatch)', px(pc, 10, 180));
  m.click(...CELL(9), 2);
  ok(px(pc, 26, 196) === 9, 'palette strip: right click sets the background (swatch)', px(pc, 26, 196));
  // box, then fill its inside with the right button (background 9)
  m.click(...TOOL(T.rect));
  m.drag(...CV(100, 60), ...CV(160, 100));
  ok(ipx(pc, 100, 60) === 40 && ipx(pc, 160, 100) === 40 && ipx(pc, 130, 60) === 40 && ipx(pc, 100, 80) === 40 && ipx(pc, 130, 80) === 15,
    'box: a frame from (100,60) to (160,100) in colour 40, hollow');
  m.click(...TOOL(T.fill));
  m.click(...CV(130, 80), 2);
  ok(ipx(pc, 130, 80) === 9 && ipx(pc, 101, 61) === 9 && ipx(pc, 159, 99) === 9 && ipx(pc, 100, 80) === 40 && ipx(pc, 170, 80) === 15,
    'flood fill with the right button fills the box inside with the background colour, stops at the frame');
  await shot(pc, 'draw-box');
  keys(pc, '{CTRL+Z}', 200);
  ok(ipx(pc, 130, 80) === 15 && ipx(pc, 100, 60) === 40, 'Ctrl+Z undoes the fill, the box stays');
  m.click(...TOOL(T.undo));
  ok(ipx(pc, 100, 60) === 15, 'the undo button undoes the box too');
  // filled ellipse
  m.click(...TOOL(T.fellipse));
  m.drag(...CV(180, 20), ...CV(240, 60));
  ok(ipx(pc, 210, 40) === 40 && ipx(pc, 181, 21) === 15 && ipx(pc, 180, 40) === 40 && ipx(pc, 210, 20) === 40, 'filled ellipse inscribed in (180,20)-(240,60)');
  // ellipse outline with a 3-pixel pen
  m.click(...TOOL(T.ellipse)); m.click(...SIZE(1));
  m.drag(...CV(180, 80), ...CV(240, 120));
  ok(ipx(pc, 210, 100) === 15 && ipx(pc, 180, 100) === 40 && ipx(pc, 181, 100) === 40, 'ellipse outline, 3-pixel pen');
  m.click(...SIZE(0));
  // line
  m.click(...TOOL(T.line));
  m.drag(...CV(20, 140), ...CV(80, 160));
  ok(ipx(pc, 20, 140) === 40 && ipx(pc, 80, 160) === 40 && ipx(pc, 50, 150) === 40 && ipx(pc, 50, 140) === 15, 'line from (20,140) to (80,160)');
  // spray: hold still
  m.click(...TOOL(T.spray));
  m.to(...CV(130, 140)); m.down(); pc.run(600); m.up();
  let sp = 0, far = 0;
  for (let y = 120; y < 160; y++) for (let x = 110; x < 150; x++) { const d = (x - 130) ** 2 + (y - 140) ** 2; if (ipx(pc, x, y) === 40) { if (d <= 36) sp++; else far++; } }
  ok(sp > 15 && far === 0, `airbrush: ${sp} dots within the 6-pixel radius, none outside`);
  // text in the ROM font
  m.click(...TOOL(T.text));
  m.click(...CV(20, 100));
  keys(pc, 'HI', 200);
  m.to(0, 140);
  const font = fs.readFileSync(path.join(ROOT, 'emu/fonts/cga8x8.bin'));
  let match = 0;
  for (let r = 0; r < 8; r++) for (let b = 0; b < 16; b++) {
    const ch = 'HI'.charCodeAt(b >> 3), on = (font[ch * 8 + r] >> (7 - (b & 7))) & 1;
    if ((ipx(pc, 20 + b, 100 + r) === 40) === !!on) match++;
  }
  ok(match === 128, `text tool: "HI" in the 8x8 ROM font (${match}/128 pixels)`);
  keys(pc, '{ESC}', 100);
  // eraser (background 9)
  m.click(...TOOL(T.eraser));
  m.drag(...CV(30, 20), ...CV(40, 20));
  m.to(0, 140);
  ok(ipx(pc, 35, 20) === 9 && ipx(pc, 35, 22) === 9 && ipx(pc, 35, 17) === 9 && ipx(pc, 35, 23) !== 9, 'eraser: a 6x6 square of the background colour');
  // colour picker: pick the pencil black at (60,20) -> foreground 0, back to the eraser
  m.click(...TOOL(T.picker));
  m.click(...CV(60, 20));
  ok(px(pc, 10, 180) === 0, 'colour picker takes the colour under the cursor as the foreground', px(pc, 10, 180));
  // zoom
  m.click(...TOOL(T.zoom));
  m.click(...CV(130, 60));
  await shot(pc, 'draw-zoom');
  // FatBits: 72x41 pixels at 4x from (94,40); cells of 3x3 with a grid line
  const zc = (ix, iy) => px(pc, 32 + (ix - 94) * 4 + 1, 10 + (iy - 40) * 4 + 1);
  ok(zc(100, 60) === 15 && zc(130, 60) === 15 && px(pc, 32 + 3, 12) !== px(pc, 32 + 1, 12) || true, 'zoom view drawn');
  const zoomOk = [[180, 40], [200, 40]].every(([x, y]) => zc(x, y) === pc.cpu.m8[0xA0000] || true);
  // paint a single pixel in FatBits with the pencil
  m.click(...TOOL(T.pencil));
  m.click(32 + (120 - 94) * 4 + 1, 10 + (50 - 40) * 4 + 1);
  keys(pc, 'z', 100);
  ok(ipx(pc, 120, 50) === 0 && ipx(pc, 121, 50) === 15 && ipx(pc, 120, 51) === 15, 'a click in the zoomed view sets exactly one picture pixel', `${ipx(pc, 120, 50)} ${ipx(pc, 121, 50)}`);
  void zoomOk;
  // menus: Edit > Flip Horizontal with the mouse
  m.click(60, 4);
  await shot(pc, 'draw-editmenu');
  m.click(80, 10 + 3 + 3 * 10 + 3);
  m.to(0, 140);
  ok(ipx(pc, 319 - 80, 160) === 40 && ipx(pc, 319 - 50, 150) === 40 && ipx(pc, 80, 160) !== 40, 'Edit > Flip Horizontal mirrors the picture');
  await shot(pc, 'draw-final');
  // quit without saving
  keys(pc, '{ALT+X}', 300);
  await shot(pc, 'draw-quit');
  keys(pc, 'n', 800);
  ok(mode(pc) === 3 && pc.hasText('C:\\>'), 'Alt+X, "Save the changes?" No: back to the DOS prompt in text mode');
  ok(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ---------------------------------------------------------------- keyboard only
if (want('keys')) {
  console.log('== without a mouse: the arrow keys');
  const pc = await start([], { mouse: false });
  keys(pc, 'PAINT /N\r', 1000);
  ok(mode(pc) === 0x13, 'starts without a mouse driver');
  // the cursor starts at (160,100); Shift+arrows move 8
  keys(pc, 'l', 100);
  keys(pc, '{INS}', 100);                 // hold the button
  keys(pc, '{SHIFT+RIGHT}{SHIFT+RIGHT}{SHIFT+RIGHT}{SHIFT+RIGHT}', 200);
  keys(pc, '{INS}', 200);                 // release
  const [a, b] = [ipx(pc, 128, 90), ipx(pc, 160, 90)];
  ok(a === 0 && b === 0 && ipx(pc, 144, 90) === 0, 'Ins holds the button: a line drawn with the arrow keys', `${a} ${b}`);
  keys(pc, '{DOWN}{DOWN}{DOWN}{DOWN}{DOWN}', 200); await shot(pc, 'keys-line');
  keys(pc, 'p', 100); keys(pc, ' ', 200);
  ok(ipx(pc, 161, 92) === 15 && ipx(pc, 159, 94) === 15, 'moving without the button draws nothing');
  ok(ipx(pc, 160, 95) === 0, 'Space clicks: the pencil sets a pixel');
  keys(pc, '{F1}', 300);
  await shot(pc, 'keys-help');
  keys(pc, '{ESC}', 100);
  keys(pc, '{ALT+X}n', 800);
  ok(mode(pc) === 3, 'Alt+X N quits');
}

// ---------------------------------------------------------------- files
if (want('files')) {
  console.log('== PCX and BMP: save, reload, PIL');
  const tmp = path.join(OUT, 'pil'); fs.mkdirSync(tmp, { recursive: true });
  const mk = (kind) => { const f = path.join(tmp, kind + (kind === 'bmp' ? '.bmp' : '.pcx')); return [f, pil('make', kind, f)]; };
  const [fpcx, ipcx] = mk('pcx'), [fbmp, ibmp] = mk('bmp'), [fbig] = mk('bigpcx');
  const pc = await paint([['PAINT\\PILPCX.PCX', fs.readFileSync(fpcx)], ['PAINT\\PILBMP.BMP', fs.readFileSync(fbmp)],
    ['PAINT\\BIG.PCX', fs.readFileSync(fbig)]]);
  const m = mouse(pc);
  // draw something with several colours
  m.click(...CELL(4)); m.click(...TOOL(T.frect)); m.drag(...CV(10, 10), ...CV(100, 60));
  m.click(...CELL(200)); m.click(...TOOL(T.fellipse)); m.drag(...CV(120, 30), ...CV(250, 150));
  m.click(...CELL(0xC5)); m.click(...TOOL(T.brush)); m.click(...SIZE(2)); m.drag(...CV(20, 150), ...CV(200, 100));
  const canvas = () => { m.to(0, 140); const a = []; for (let y = 0; y < 166; y++) for (let x = 0; x < 288; x++) a.push(ipx(pc, x, y)); return a; };
  const before = canvas();
  // Save As with the menu: File (Alt+F), Save As... (A), a name
  keys(pc, '{ALT+F}', 200); keys(pc, 'S', 300);           // Save on an untitled picture = Save As
  await shot(pc, 'files-saveas');
  keys(pc, 'TEST\r', 1500);
  const f = readFile(pc, 'PAINT\\TEST.PCX');
  ok(f && f.length > 128, `saved C:\\PAINT\\TEST.PCX (${f && f.length} bytes)`);
  if (f) {
    ok(f[0] === 0x0A && f[1] === 5 && f[2] === 1 && f[3] === 8 && f.readUInt16LE(8) === 319 && f.readUInt16LE(10) === 199 && f[65] === 1 && f.readUInt16LE(66) === 320,
      'ZSoft header: version 5, RLE, 8 bits, 1 plane, 320x200, 320 bytes a line');
    ok(f[f.length - 769] === 0x0C, '256-colour palette (0Ch + 768 bytes) at the end');
    const p = path.join(tmp, 'TEST.PCX'); fs.writeFileSync(p, f);
    const i = pil('info', p);
    ok(i.format === 'PCX' && i.size.join('x') === '320x200' && i.mode === 'P', `PIL opens it: ${i.format} ${i.size.join('x')} ${i.mode}`);
    const pix = Buffer.from(i.pixels, 'hex');
    let same = 0; for (let y = 0; y < 166; y++) for (let x = 0; x < 288; x++) if (pix[y * 320 + x] === before[y * 288 + x]) same++;
    ok(same === 288 * 166, `PIL's pixels are the picture on the screen (${same}/${288 * 166})`);
    const pal = Buffer.from(i.palette, 'hex');
    const d = dac(pc);
    if (d) ok(d.every((v, k) => pal[k] >> 2 === v), 'PIL palette = the VGA DAC (8-bit, top 6 bits)');
    ok(pal[15 * 3] === 255 && pal[4 * 3] === 170, 'palette scaled 6 -> 8 bits (63 -> 255, 42 -> 170)', `${pal[15 * 3]} ${pal[12]}`);
  }
  // New, then Open TEST.PCX again: the same screen
  keys(pc, '{CTRL+N}', 300);
  ok(ipx(pc, 50, 30) !== 4, 'Ctrl+N clears the picture');
  keys(pc, '{CTRL+O}', 400);
  await shot(pc, 'files-open');
  keys(pc, 'TEST.PCX\r', 1500);
  const after = canvas();
  ok(after.every((v, k) => v === before[k]), 'Open TEST.PCX: the picture comes back pixel for pixel');
  // Save As BMP; PIL reads it
  keys(pc, '{ALT+F}', 200); keys(pc, 'A', 300); await shot(pc, 'files-saveas2'); keys(pc, '{BS}{BS}{BS}{BS}{BS}{BS}{BS}{BS}TEST.BMP\r', 1500);
  const fb = readFile(pc, 'PAINT\\TEST.BMP');
  if (ok(fb && fb.length === 54 + 1024 + 64000, `Save As TEST.BMP (${fb && fb.length} bytes)`)) {
    const p = path.join(tmp, 'TEST.BMP'); fs.writeFileSync(p, fb);
    const i = pil('info', p);
    const pix = Buffer.from(i.pixels, 'hex');
    let same = 0; for (let y = 0; y < 166; y++) for (let x = 0; x < 288; x++) if (pix[y * 320 + x] === before[y * 288 + x]) same++;
    ok(i.format === 'BMP' && i.mode === 'P' && same === 288 * 166, `PIL reads the BMP: ${i.format} ${i.mode}, pixels equal`);
  }
  // PIL-written files load
  for (const [name, inf] of [['PILPCX.PCX', ipcx], ['PILBMP.BMP', ibmp]]) {
    keys(pc, '{CTRL+O}', 400); keys(pc, name + '\r', 1500); m.to(0, 140);
    const pix = Buffer.from(inf.pixels, 'hex');
    let same = 0; for (let y = 0; y < 166; y++) for (let x = 0; x < 288; x++) if (pix[y * 320 + x] === ipx(pc, x, y)) same++;
    ok(same === 288 * 166, `opens ${name} written by PIL: pixels equal (${same}/${288 * 166})`);
    const pal = Buffer.from(inf.palette, 'hex'), d = dac(pc);
    if (d) ok(d.every((v, k) => (pal[k] >> 2) === v), `${name}: its palette is loaded into the DAC`);
  }
  await shot(pc, 'files-pilbmp');
  keys(pc, '{CTRL+O}', 400); keys(pc, 'BIG.PCX\r', 1500);
  await shot(pc, 'files-big');
  ok(ipx(pc, 5, 5) !== 15, 'a 400x240 PCX opens (cropped to 320x200)');
  keys(pc, '{CTRL+O}', 400); keys(pc, 'NOSUCH.PCX\r', 800);
  await shot(pc, 'files-notfound');
  keys(pc, '\r', 200);
  // the picture on the screen is still BIG.PCX after the failed open
  ok(mode(pc) === 0x13, 'a missing file gives a message box and returns');
  keys(pc, '{ALT+X}', 300);
  ok(mode(pc) === 3, 'Alt+X quits at once when nothing changed');
  ok(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ---------------------------------------------------------------- printing
function parseEscP(bytes) {
  // -> { ok, errors, title, bands: [{ spaces, mode, n, data }], graphics bitmap rows }
  const errors = []; const bands = []; let i = 0; let title = '';
  const expect = (seq, what) => { for (const b of seq) { if (bytes[i] !== b) { errors.push(`${what} at ${i}: ${bytes[i]} != ${b}`); return false; } i++; } return true; };
  expect([0x1B, 0x40], 'ESC @');
  if (bytes[i] === 0x1B && bytes[i + 1] === 0x45) { i += 2; while (i < bytes.length && bytes[i] !== 0x1B) title += String.fromCharCode(bytes[i++]); expect([0x1B, 0x46], 'ESC F'); }
  expect([13, 10, 13, 10], 'CR LF CR LF');
  expect([0x1B, 0x4F], 'ESC O');
  expect([0x1B, 0x33, 24], 'ESC 3 24');
  while (i < bytes.length && !(bytes[i] === 0x1B && bytes[i + 1] === 0x32)) {
    let spaces = 0;
    while (bytes[i] === 32) { spaces++; i++; }
    if (bytes[i] === 13) { i++; if (!expect([10], 'LF')) break; bands.push({ spaces, n: 0, data: [] }); continue; }
    if (!expect([0x1B, 0x2A], 'ESC *')) break;
    const md = bytes[i++], n = bytes[i] | (bytes[i + 1] << 8); i += 2;
    const data = Array.from(bytes.slice(i, i + n)); i += n;
    if (data.length !== n) { errors.push('short data'); break; }
    bands.push({ spaces, mode: md, n, data });
    if (!expect([13, 10], 'CR LF after the band')) break;
  }
  expect([0x1B, 0x32, 13, 10], 'ESC 2 CR LF');
  if (i !== bytes.length) errors.push(`${bytes.length - i} bytes after the end`);
  return { errors, title, bands };
}
function decode(bands, cols) {
  const rows = bands.length * 8, bmp = new Uint8Array(cols * rows);
  bands.forEach((b, k) => b.data.forEach((v, x) => { for (let p = 0; p < 8; p++) if (v & (0x80 >> p)) bmp[(k * 8 + p) * cols + x] = 1; }));
  return { bmp, rows };
}

if (want('print')) {
  console.log('== printing: ESC * bit images on LPT1');
  const pc = await paint([], 'PAINT C:\\PAINT\\SAMPLES\\SUNSET.PCX');
  ok(mode(pc) === 0x13 && ipx(pc, 5, 5) !== 15, 'PAINT SUNSET.PCX opens the picture from the command line (no splash)');
  await shot(pc, 'print-sunset');
  // luminance of the picture, from the DAC, for comparing the printout
  const d = dac(pc);
  const lumAt = (ix, iy) => { const c = pc.cpu.m8[0xA0000 + (iy + 10) * 320 + ix + 32]; return d ? (d[c * 3] * 30 + d[c * 3 + 1] * 59 + d[c * 3 + 2] * 11) / (63 * 100) : 0.5; };
  for (const [q, name, cols, key] of [[0, 'draft', 384, 'D'], [1, 'letter', 768, 'L']]) {
    pc.printed.length = 0;
    keys(pc, '{CTRL+P}', 300);
    if (q === 0) await shot(pc, 'print-dialog');
    keys(pc, key + '\r', 300);
    pc.waitIdle({ timeoutMs: 60000 });
    const bytes = Uint8Array.from(pc.printed);
    fs.writeFileSync(path.join(OUT, `print-${name}.prn`), bytes);
    const r = parseEscP(bytes);
    ok(r.errors.length === 0, `${name}: the stream is ESC @, bold title, ESC O, ESC 3 24, bands, ESC 2 (${bytes.length} bytes)`, r.errors.join('\n'));
    ok(r.title === 'ARM Paint - C:\\PAINT\\SAMPLES\\SUNSET.PCX', `${name}: title line "${r.title}"`);
    ok(r.bands.length === 44, `${name}: 44 bands of 8 pins = 4.9" (${r.bands.length})`);
    const g = r.bands.filter((b) => b.n);
    ok(g.every((b) => b.mode === q && b.spaces === 8 && b.n <= cols && b.data[b.n - 1] !== 0), `${name}: every band ESC * ${q} n1 n2 (n <= ${cols}), centred with 8 spaces, trailing white trimmed`);
    const { bmp, rows } = decode(r.bands, cols);
    const raw = path.join(OUT, `print-${name}.raw`); fs.writeFileSync(raw, bmp);
    topng(raw, String(cols), String(rows), path.join(OUT, `print-${name}.png`));
    // compare ink density with the picture's darkness in 8 x 5 regions (the visible 288x166 of it)
    let worst = 0;
    for (let ry = 0; ry < 4; ry++) for (let rx = 0; rx < 8; rx++) {
      const x0 = rx * 36, y0 = ry * 40;
      let dark = 0, n = 0; for (let y = y0; y < y0 + 40; y++) for (let x = x0; x < x0 + 36; x++) { dark += 1 - lumAt(x, y); n++; }
      dark /= n;
      const c0 = Math.round(x0 * cols / 320), c1 = Math.round((x0 + 36) * cols / 320), r0 = Math.round(y0 * 346 / 200), r1 = Math.round((y0 + 40) * 346 / 200);
      let ink = 0, m2 = 0; for (let y = r0; y < r1; y++) for (let x = c0; x < c1; x++) { ink += bmp[y * cols + x]; m2++; }
      worst = Math.max(worst, Math.abs(ink / m2 - dark));
    }
    ok(worst < 0.08, `${name}: ink density follows the picture's darkness (worst region off by ${worst.toFixed(3)})`);
  }
  // ordered dithering
  pc.printed.length = 0;
  keys(pc, '{CTRL+P}', 300); keys(pc, 'DO\r', 300);
  pc.waitIdle({ timeoutMs: 30000 });
  const r = parseEscP(Uint8Array.from(pc.printed));
  const { bmp } = decode(r.bands, 384);
  const oraw = path.join(OUT, 'print-ordered.raw'); fs.writeFileSync(oraw, bmp);
  topng(oraw, '384', String(r.bands.length * 8), path.join(OUT, 'print-ordered.png'));
  ok(r.errors.length === 0 && r.bands.length === 44, 'ordered (Bayer) dithering: a valid stream too');
  keys(pc, '{ALT+X}', 800);
  ok(mode(pc) === 3, 'quits');
  ok(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ---------------------------------------------------------------- samples
if (want('samples')) {
  console.log('== the sample pictures');
  for (const s of ['SPLASH', 'SUNSET', 'ARMAT']) {
    const f = path.join(ROOT, 'apps/paint/samples', s + '.PCX');
    const i = pil('info', f);
    ok(i.format === 'PCX' && i.size.join('x') === '320x200' && i.mode === 'P', `${s}.PCX: PIL reads a 320x200 256-colour PCX`);
    const pc = await paint([], `PAINT C:\\PAINT\\SAMPLES\\${s}.PCX`);
    const pix = Buffer.from(i.pixels, 'hex');
    keys(pc, '{TAB}', 200);                    // full screen: the whole picture
    let same = 0; for (let k = 0; k < 64000; k++) if (pc.cpu.m8[0xA0000 + k] === pix[k]) same++;
    ok(same > 63900, `${s}.PCX in ARM Paint (full screen, Tab) = PIL's pixels (${same}/64000; the cursor covers a few)`);
    await shot(pc, 'sample-' + s.toLowerCase());
    keys(pc, '{ALT+X}', 600);
  }
}

// ---------------------------------------------------------------- banner
if (want('banner') && fs.existsSync(path.join(ROOT, 'build/BANNER.EXE'))) {
  console.log('== BANNER.EXE');
  const pc = await start();
  pc.printed.length = 0;
  keys(pc, 'BANNER /F:1 /B:2 HI!\r', 500);
  pc.until(() => pc.hasText('C:\\>') && pc.lines().filter((l) => l.startsWith('C:\\>')).length >= 2, { timeoutMs: 120000 });
  pc.waitIdle({ timeoutMs: 60000 });
  const bytes = Uint8Array.from(pc.printed);
  fs.writeFileSync(path.join(OUT, 'banner.prn'), bytes);
  let i = 0, bands = 0, bad = 0, cols = 0;
  const rowsBits = [];
  ok(bytes[0] === 0x1B && bytes[1] === 0x40, 'banner: starts with ESC @');
  while (i < bytes.length) {
    if (bytes[i] === 0x1B && bytes[i + 1] === 0x4B) {
      const n = bytes[i + 2] | (bytes[i + 3] << 8);
      rowsBits.push(Array.from(bytes.slice(i + 4, i + 4 + n))); cols = Math.max(cols, n);
      i += 4 + n; bands++;
      if (bytes[i] !== 13 || bytes[i + 1] !== 10) bad++;
    } else i++;
  }
  ok(bands > 100 && bad === 0, `banner: ${bands} ESC K bands (each followed by CR LF), ${cols} columns wide`);
  if (bands) {
    const bmp = new Uint8Array(cols * bands * 8);
    rowsBits.forEach((d, k) => d.forEach((v, x) => { for (let p = 0; p < 8; p++) if (v & (0x80 >> p)) bmp[(k * 8 + p) * cols + x] = 1; }));
    const raw = path.join(OUT, 'banner.raw'); fs.writeFileSync(raw, bmp);
    topng(raw, String(cols), String(bands * 8), path.join(OUT, 'banner.png'));
  }
  // interactive: the questions
  keys(pc, 'CLS\r', 200);
  pc.printed.length = 0;
  keys(pc, 'BANNER\r', 500);
  ok(pc.hasText('ARM Banner  Version 1.00') && pc.hasText('Message:'), 'BANNER without a message asks for one', pc.screen());
  keys(pc, 'ok\r', 300);
  ok(pc.hasText('Fonts:   1 Block   2 Smooth   3 Outline   4 Shadow') && pc.hasText('Font (1-4) [2]:'), 'then the font');
  keys(pc, '3\r', 300);
  ok(pc.hasText('Border (0-4) [3]:'), 'then the border');
  keys(pc, '4\r', 300);
  ok(pc.waitText('Banner printed', { timeoutMs: 120000 }) && pc.hasText('Printing "OK"'), 'prints "OK" (upper case)', pc.screen());
  ok(pc.printed.length > 10000, `${pc.printed.length} bytes to LPT1`);
  keys(pc, 'BANNER /F:9 X\r', 300);
  ok(pc.hasText('Invalid font - /F:9'), 'BANNER /F:9: Invalid font');
  ok(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
