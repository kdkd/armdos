#!/usr/bin/env node
// apps/gem/tests/run.mjs - GEM for ARM-DOS headless tests ("make gem-test").
//
//   node apps/gem/tests/run.mjs [--only vdi,boot,open,drag,menu,launch,perf,acc,exit,gemapp,floppy,format,vga,hgc]
//
// Boots ARM-DOS (COMMAND.COM, HIMEM, MOUSE.COM) with the GEM files laid out as
// apps/gem/hd.json says, types GEM, drives the desktop with the emulated PS/2
// mouse and reads the graphics screen back: strings are found by matching the
// glyphs of GEM's own system fonts. Screenshots go to build/gem-test/.
import fs from 'node:fs';
import path from 'node:path';
import { start, keys, shot, mouse, findText, screenInk, readFile } from './lib.mjs';

const args = process.argv.slice(2);
const ONLY = args.includes('--only') ? args[args.indexOf('--only') + 1].split(',') : null;
const want = (n) => !ONLY || ONLY.includes(n);
let pass = 0, fail = 0;
const ok = (c, what, detail) => {
  if (c) pass++; else fail++;
  console.log(`${c ? 'ok  ' : 'FAIL'} ${what}`);
  if (!c && detail !== undefined) console.log('     ' + String(detail).split('\n').join('\n     '));
  return c;
};
const mode = (pc) => pc.cpu.m8[0x449];
const has = (pc, s, font = '8x8', region = null) => findText(pc, s, font, region).length > 0;

async function gem(opts = {}) {
  const pc = await start(opts.files || [], opts);
  const t0 = pc.machine.timeMs();
  pc.type(opts.cmd || 'GEM\r');
  // the desktop is up when its menu bar is drawn
  const font = /\/V/i.test(opts.cmd || '') ? '8x16' : pc.machine.hgc ? '8x14' : '8x8';
  for (let i = 0; i < 200 && !has(pc, 'Arrange', font, [0, 0, 300, 16]); i++) pc.run(50);
  pc.bootMs = pc.machine.timeMs() - t0;
  pc.run(500);
  return pc;
}

// ---------------------------------------------------------------- the VDI
if (want('vdi')) {
  console.log('== the VDI on its own (GEMVDI VDITEST.EXE)');
  const pc = await start([], { extra: [{ src: 'build/gem-test/VDITEST.EXE', dst: 'GEMAPPS\\GEMSYS\\' }] });
  keys(pc, 'CD \\GEMAPPS\\GEMSYS\r');
  pc.type('GEMVDI VDITEST.EXE X\r');
  pc.run(3000);
  ok(mode(pc) === 6, 'GEMVDI opens the workstation in CGA mode 6 (640x200)', mode(pc));
  await shot(pc, 'vdi-test');
  const s = screenInk(pc);
  ok(s.rows[0][320] === 1 && s.rows[199][320] === 1 && s.rows[100][0] === 1 && s.rows[100][639] === 1, 'polyline: the frame around the screen');
  let solid = 0; for (let x = 288; x < 322; x++) solid += s.rows[30][x];
  ok(solid >= 33, 'filled rectangle, pattern 8 (solid) of the dither set', solid);
  ok(has(pc, 'The small system font: 6x6.', '6x6'), 'text in the 6x6 system font');
  ok(has(pc, 'GEM VDI on ARM-DOS', '8x8'), 'text in the 8x8 system font');
  ok(has(pc, 'Bold', '8x8') === false && has(pc, 'Italic', '8x8') === false, 'text effects change the glyphs (bold, italic)');
  pc.type(' ');
  pc.run(1500);
  ok(/VDITEST: 640x200/.test(pc.screen()), 'v_clswk: back to text mode, VDITEST reports 640x200', pc.screen().split('\n').slice(-4).join('\n'));
}

// ------------------------------------------------------------ the desktop
let pc = null, m = null;
if (want('boot') || want('open') || want('drag') || want('menu') || want('launch') || want('acc') || want('exit') || want('perf')) {
  console.log('== GEM from the DOS prompt');
  const t0 = Date.now();
  pc = await gem({ extra: [{ src: 'build/gem-test/XMSTEST.EXE', dst: 'DOS\\' }] });
  m = mouse(pc);
  m.home();
  await shot(pc, 'desktop');
  ok(mode(pc) === 6, 'GEM runs in CGA mode 6', mode(pc));
  ok(has(pc, 'File', '8x8', [0, 0, 100, 10]) && has(pc, 'Options') && has(pc, 'Arrange') && has(pc, 'DESKTOP'),
    'the menu bar: File Options Arrange ... DESKTOP');
  ok(has(pc, 'C:\\GEMAPPS\\') && has(pc, 'Disk Drives:'), 'two windows: C:\\GEMAPPS\\ and Disk Drives:');
  ok(has(pc, 'FLOPPY DISK', '6x6') && has(pc, 'HARD DISK', '6x6'), 'the drive icons A: FLOPPY DISK, C: HARD DISK');
  ok(has(pc, 'GEMSYS', '6x6') && has(pc, 'New Folder', '6x6'), 'the GEMAPPS window shows GEMSYS and New Folder');
  console.log(`     (GEM to the desktop: ${pc.bootMs.toFixed(0)} ms emulated, typing included; ${Date.now() - t0} ms of host time for the whole boot)`);
}

function iconAt(pc, label) {
  m.to(636, 120);                        // park the mouse on the desktop border
  const h = findText(pc, label, '6x6');
  return h.length ? [h[0].x + 3 * label.length, h[0].y - 12] : null;
}

if (want('open') || want('launch') || want('drag')) {
  console.log('== opening the hard disk and a folder with the mouse');
  const hd = iconAt(pc, 'HARD DISK');
  const t0 = pc.machine.timeMs();
  m.dclick(...hd);
  pc.run(1500);
  await shot(pc, 'open-c');
  ok(has(pc, 'C:\\') && has(pc, 'COMMAND.COM', '6x6') && has(pc, 'AUTOEXEC.BAT', '6x6'), 'double-click HARD DISK: a window on C:\\ with its files');
  const dos = iconAt(pc, 'DOS');
  m.dclick(...dos);
  pc.run(1500);
  await shot(pc, 'open-dos');
  ok(has(pc, 'C:\\DOS\\') && has(pc, 'MEM.EXE', '6x6') && has(pc, 'EDIT.EXE', '6x6'), 'double-click the DOS folder: C:\\DOS\\ with MEM.EXE, EDIT.EXE');
}

if (want('drag')) {
  console.log('== dragging an icon: copy GEM.BAT onto the GEMSYS folder');
  const src = iconAt(pc, 'GEM.BAT'), dst = iconAt(pc, 'GEMSYS');
  // press, hold (GEM tells a drag from a click by the button staying down), move, drop
  m.to(...src).down(); pc.run(300);
  m.to(...dst); pc.run(300);
  m.up(); pc.run(1500);
  await shot(pc, 'drag-copy');
  ok(has(pc, 'COPY FOLDERS / ITEMS') && has(pc, 'Items to copy:'), 'drop on a folder: the COPY FOLDERS / ITEMS dialog');
  pc.type('\r');
  pc.run(3000);
  const a = readFile(pc, 'DOS/GEM.BAT'), b = readFile(pc, 'GEMAPPS/GEMSYS/GEM.BAT');
  ok(a && b && a.equals(b), 'OK: the file is copied into C:\\GEMAPPS\\GEMSYS', b ? b.length : 'missing');
}

if (want('menu')) {
  console.log('== menus');
  m.click(28, 4);
  pc.run(600);
  await shot(pc, 'menu-file');
  ok(has(pc, 'Exit to DOS') && has(pc, 'To Output'), 'File drops down: ... To Output, Exit to DOS');
  m.click(400, 190);
  pc.run(400);
  m.click(580, 4);
  pc.run(600);
  await shot(pc, 'menu-desk');
  ok(has(pc, 'Desktop info') && has(pc, 'Calculator') && has(pc, 'Clock'), 'DESKTOP menu: Desktop info... and the CALCLOCK.ACC entries');
  m.click(560, 14);
  pc.run(1200);
  await shot(pc, 'desktop-info');
  ok(has(pc, 'Digital Research Inc.') && has(pc, 'Lowell Webster'), 'Desktop info...: the GEM/3 Desktop box');
  pc.type('\r');
  pc.run(800);
}

if (want('launch')) {
  console.log('== running a DOS program from the desktop');
  const ed = iconAt(pc, 'EDIT.EXE');
  m.dclick(...ed);
  pc.run(1200);
  await shot(pc, 'open-application');
  ok(has(pc, 'OPEN APPLICATION') && has(pc, 'Parameters:'), 'EDIT.EXE: the Open Application dialog');
  pc.type('\r');
  pc.run(3000);
  await shot(pc, 'edit');
  ok(mode(pc) === 3 && /File/.test(pc.screen()), 'GEM steps aside: EDIT runs in text mode', 'mode ' + mode(pc) + '\n' + pc.screen());
  pc.type('{ESC}'); pc.run(400); pc.type('{ALT+F}'); pc.run(400); pc.type('x'); pc.run(5000);
  await shot(pc, 'after-edit');
  ok(mode(pc) === 6 && has(pc, 'C:\\DOS\\') && has(pc, 'Arrange'), 'EDIT exits: back in GEM, the C:\\DOS\\ window restored');
  // GEM, the VDI and the accessory stay resident under a DOS program: the
  // extended memory must still be free for it (XMSTEST waits for a key
  // when it gets a parameter)
  m.dclick(...iconAt(pc, 'XMSTEST.EXE'));
  pc.run(1200);
  pc.type('K\r');
  pc.run(2000);
  const xl = pc.screen().split('\n').filter((l) => /XMSTEST/.test(l)).join('\n');
  ok(/free largest 15296K/.test(xl) && /alloc 1024K ax 1/.test(xl), 'under GEM a DOS program still gets the extended memory (XMS)', xl);
  pc.type(' ');
  pc.run(3000);
  ok(mode(pc) === 6 && has(pc, 'Arrange'), 'and GEM comes back after it');
}

if (want('perf')) {
  console.log('== redraw speed');
  // GEM's dispatcher idles in WFI, so the cost of a redraw is the emulated
  // time the CPU was not halted, and the instructions it retired, between the
  // click on a menu entry and the machine going quiet again.
  const M = pc.machine;
  const measure = (label, act) => {
    pc.waitIdle({ timeoutMs: 5000 });
    const h0 = M.haltedNs, t0 = M.timeNs(), i0 = pc.cpu.icount;
    act();
    pc.waitIdle({ timeoutMs: 20000 });
    const busy = (M.timeNs() - t0 - (M.haltedNs - h0)) / 1e6, n = pc.cpu.icount - i0;
    console.log(`     ${label}: ${busy.toFixed(1)} ms busy, ${(n / 1e6).toFixed(2)}M instructions`);
    return busy;
  };
  // the menu drops under the mouse, which then hides the start of the first entry: look for the tail
  const menuPick = (menuX, item) => {
    m.click(menuX, 4); pc.run(500);
    const tail = item.slice(item.indexOf(' ') + 1), t = findText(pc, tail);
    if (t.length) m.click(t[0].x + 8, t[0].y + 3);
    return t.length > 0;
  };
  let picked = true;
  const idle = measure('idle, the mouse parked (baseline)', () => pc.run(1000));
  const tx = measure('Arrange > Show as text (all windows redrawn)', () => (picked = menuPick(170, 'Show as text') && picked));
  await shot(pc, 'show-as-text');
  ok(has(pc, 'EDIT     EXE') || has(pc, 'EDIT.EXE', '8x8'), 'Show as text: the windows list names, sizes and dates');
  const ic = measure('Arrange > Show as icons', () => (picked = menuPick(170, 'Show as icons') && picked));
  // a whole-screen redraw: close and reopen nothing; ask for a full form_dial redraw via Desktop info
  const inf = measure('Desktop info... box, drawn and closed', () => { menuPick(580, 'Desktop info...'); pc.run(800); pc.type('\r'); });
  ok(picked && tx < 2000 && ic < 2000, `window redraws take well under 2 s of emulated time (${tx.toFixed(0)} / ${ic.toFixed(0)} / ${inf.toFixed(0)} ms; idle ${idle.toFixed(0)} ms)`);
}

if (want('acc')) {
  console.log('== desk accessories (CALCLOCK.ACC)');
  m.click(580, 4); pc.run(600);
  const c = findText(pc, 'Calculator');
  m.click(c[0].x + 20, c[0].y + 3); pc.run(1500);
  await shot(pc, 'calculator');
  ok(has(pc, 'Calculator'), 'the Calculator window opens');
  // the keypad (the Calculator opens at a fixed place on the 640x200 desktop)
  const K = { '7': [296, 87], '8': [321, 87], '9': [345, 87], '/': [370, 87], '4': [296, 103], '5': [321, 103], '6': [345, 103], '+': [370, 103],
    '1': [296, 119], '2': [321, 119], '3': [345, 119], '-': [370, 119], '0': [296, 135], '.': [321, 135], '%': [345, 135], '=': [370, 135],
    'E': [268, 71], 'C': [296, 71], 'N': [321, 71], '*': [370, 71] };
  const cx = { x: 260, y: 40 };
  const DISP = [250, 36, 400, 64];
  const calc = (seq) => { for (const ch of seq) { m.click(...K[ch]); pc.run(200); } pc.run(400); };
  calc('12*3=');
  ok(has(pc, '36', '8x8', DISP), 'Calculator: 12 x 3 = 36');
  calc('C1/8=');
  ok(has(pc, '.125', '8x8', DISP), 'Calculator: 1 / 8 = .125');
  calc('C7-10=');
  ok(has(pc, '-3', '8x8', DISP), 'Calculator: 7 - 10 = -3');
  await shot(pc, 'calculator-result');
  m.click(580, 4); pc.run(600);
  const k = findText(pc, 'Clock');
  m.click(k[0].x + 10, k[0].y + 3); pc.run(1500);
  await shot(pc, 'clock');
  ok(has(pc, 'Clock') && has(pc, 'pm'), 'the Clock window shows the time');
}

if (want('exit')) {
  console.log('== back to DOS');
  m.click(28, 4); pc.run(600);
  const e = findText(pc, 'Exit to DOS');
  m.click(e[0].x + 20, e[0].y + 3);
  pc.run(3000);
  ok(mode(pc) === 3 && /C:\\.*>\s*$/.test(pc.screen().trimEnd()), 'File > Exit to DOS: the DOS prompt again', pc.screen());
}

// ------------------------------------------------ a GEM application: DEMO
// the GEM Programmer's Toolkit sample (C:\\GEMAPPS\\DEMO.APP): launched from the
// Desktop in graphics mode, and GEM brings the Desktop back when it quits
async function demoRun(vga) {
  const pc2 = await gem(vga ? { cmd: 'GEM /V\r' } : {});
  const m2 = mouse(pc2);
  const small = vga ? '8x8' : '6x6', big = vga ? '8x16' : '8x8';
  const H = vga ? 480 : 200, sy = (y) => Math.round(y * H / 200);
  const tag = vga ? ' (VGA)' : '';
  m2.home();
  const icon = (l) => { m2.to(636, sy(120)); const h = findText(pc2, l, small); return h.length ? [h[0].x + l.length * (vga ? 4 : 3), h[0].y - (vga ? 14 : 12)] : null; };
  const pick = (menuX, tail) => { m2.click(menuX, 4); pc2.run(600); const t = findText(pc2, tail, big); if (t.length) m2.click(t[0].x + 8, t[0].y + 3); pc2.run(1500); return t.length > 0; };
  m2.dclick(...icon('DEMO.APP'));
  pc2.run(4000);
  await shot(pc2, 'demo' + (vga ? '-vga' : ''));
  ok(has(pc2, 'GEM Demo Window', big) && has(pc2, 'Options', big) && !has(pc2, 'Arrange', big), 'double-click DEMO.APP: GEM runs it, with its menu bar and window' + tag);
  // the pen dialog: PROGDEF brushes drawn by DEMO's own code, the colour selector
  ok(pick(75, 'Eraser Selection'), 'Options > Pen / Eraser Selection...' + tag);
  await shot(pc2, 'demo-pens' + (vga ? '-vga' : ''));
  ok(has(pc2, 'GEM Demo Pen / Eraser Selection', big) && has(pc2, 'Pen Colors:', big), 'the pen dialog' + tag);
  const pens = findText(pc2, 'Pens:', big)[0];
  // the third (broad) pen brush, drawn by DEMO's dr_code through the AES
  const bx = pens.x + 184, by = pens.y + (vga ? 7 : 3);
  const inkAt = () => { let n = 0; const sc = screenInk(pc2); for (let y = by - 4; y <= by + 4; y++) for (let x = bx - 8; x <= bx + 8; x++) n += sc.rows[y][x]; return n; };
  ok(inkAt() > 10, 'the brush pictures (PROGDEF objects drawn by the application)' + tag, inkAt());
  m2.click(bx, by); pc2.run(400);
  if (vga) {                    // colour 2 of the selector: GEM red
    const pcl = findText(pc2, 'Pen Colors:', big)[0];
    const two = findText(pc2, '2', big).filter((h) => Math.abs(h.y - pcl.y) < 4 && h.x > pcl.x);
    if (two.length) m2.click(two[0].x + 3, two[0].y + 4);
    pc2.run(600);
  }
  const okb = findText(pc2, 'Ok', big);
  m2.click(okb[0].x + 5, okb[0].y + 3);
  pc2.run(1500);
  // draw: a broad stroke across the window
  m2.to(120, sy(150)).down(); pc2.run(200);
  m2.to(560, sy(150)); pc2.run(300);
  m2.up(); pc2.run(800);
  await shot(pc2, 'demo-drawn' + (vga ? '-vga' : ''));
  const stroke = () => { const sc = screenInk(pc2); let n = 0; for (let x = 150; x < 530; x++) n += sc.rows[sy(150)][x]; return n; };
  ok(stroke() > 370, 'dragging the mouse draws a broad line' + tag, stroke());
  if (vga) {
    const px = pc2.cpu.m8[0xA0000 + sy(150) * 640 + 300];
    ok(px === 1, 'in the chosen colour: pixel value 1 = GEM colour 2 (red)', px);
  }
  // Save As -> DOS file of the picture
  ok(pick(28, 'ave As...'), 'File > Save As...' + tag);
  ok(has(pc2, 'Save GEM Demo picture as', big), 'the name dialog' + tag);
  pc2.type('STROKE\r');
  pc2.run(3000);
  const f = readFile(pc2, 'GEMAPPS/STROKE.DOO');
  const want = vga ? 640 * 480 / 2 : 640 * 200 / 8;
  ok(f && f.length === want, `the picture is saved as C:\\GEMAPPS\\STROKE.DOO (${f ? f.length : 0} bytes)` + tag);
  // Quit: back to the Desktop, which shows the new file
  ok(pick(28, 'Quit'), 'File > Quit' + tag);
  pc2.run(2000);
  await shot(pc2, 'demo-quit' + (vga ? '-vga' : ''));
  ok(has(pc2, 'Arrange', big) && has(pc2, 'STROKE.DOO', small), 'GEM runs the Desktop again, and it shows STROKE.DOO' + tag);
  if (vga) return;
  // again, and load the picture back through the AES's file selector
  m2.dclick(...icon('DEMO.APP'));
  pc2.run(4000);
  ok(stroke() < 10, 'a new DEMO starts with an empty window', stroke());
  ok(pick(28, 'oad...'), 'File > Load...');
  await shot(pc2, 'demo-fsel');
  ok(has(pc2, 'ITEM SELECTOR') && has(pc2, 'C:\\GEMAPPS\\*.DOO') && has(pc2, 'STROKE'), 'the ITEM SELECTOR lists C:\\GEMAPPS\\*.DOO');
  const it = findText(pc2, 'STROKE');
  m2.dclick(it[0].x + 10, it[0].y + 3);
  pc2.run(3000);
  await shot(pc2, 'demo-loaded');
  ok(stroke() > 370, 'double-click STROKE.DOO: the picture is back', stroke());
  pick(28, 'Quit');
  pc2.run(2000);
  ok(has(pc2, 'Arrange', big), 'and back to the Desktop');
}
if (want('gemapp')) {
  console.log('== a GEM application: the Toolkit DEMO (drawing program)');
  await demoRun(false);
  await demoRun(true);
}

// ------------------------------------------------- a critical error (INT 24h)
if (want('floppy')) {
  console.log('== no disk in drive A: (INT 24h) and the Options menu; no MOUSE in AUTOEXEC (GEM.BAT loads it)');
  const pc2 = await gem({ mouse: false });
  const m2 = mouse(pc2);
  m2.home();
  m2.to(636, 120);
  const f = findText(pc2, 'FLOPPY DISK', '6x6');
  m2.dclick(f[0].x + 33, f[0].y - 12);
  pc2.run(3000);
  await shot(pc2, 'floppy-alert');
  ok(has(pc2, 'Drive A: is not responding.') && has(pc2, 'Retry'), 'double-click FLOPPY DISK with no disk: GEM\'s alert "Drive A: is not responding"');
  const c = findText(pc2, 'Cancel');
  m2.click(c[0].x + 10, c[0].y + 3);
  pc2.run(2000);
  await shot(pc2, 'floppy-cancel');
  ok(!has(pc2, 'Drive A: is not responding.') && !has(pc2, 'Directory name'), 'Cancel: no second alert');
  m2.click(75, 4);
  pc2.run(600);
  await shot(pc2, 'menu-options');
  ok(has(pc2, 'Set preferences...') && has(pc2, 'Enter DOS commands'), 'the Options menu drops down');
  const sp = findText(pc2, 'Set preferences...');
  m2.click(sp[0].x + 20, sp[0].y + 3);
  pc2.run(1500);
  await shot(pc2, 'set-preferences');
  ok(has(pc2, 'SET PREFERENCES'), 'Set preferences...: the dialog');
}

// ---------------------------------------------------- formatting a floppy
if (want('format')) {
  console.log('== File > Format... runs FORMAT with its questions answered (DESKOSIF hooks)');
  const fd = new Uint8Array(1474560);
  const pc2 = await gem({ extra: [{ src: 'build/FORMAT.COM', dst: 'DOS\\' }], machine: { fd } });
  const m2 = mouse(pc2);
  m2.home();
  m2.to(636, 120);
  const f = findText(pc2, 'FLOPPY DISK', '6x6');
  m2.click(f[0].x + 33, f[0].y - 12);
  pc2.run(500);
  m2.click(28, 4);
  pc2.run(600);
  const fm = findText(pc2, 'Format...');
  m2.click(fm[0].x + 10, fm[0].y + 3);
  pc2.run(1500);
  ok(has(pc2, 'Formatting will ERASE all'), 'the Desktop asks before formatting');
  const b = findText(pc2, 'OK');
  m2.click(b[0].x + 6, b[0].y + 3);
  const t0 = pc2.machine.timeMs();
  const running = () => { const k = pc2.cpu.m8; for (let s = 0x50; s < 0xA000; s++) { const a = s * 16; if ((k[a] === 0x4D || k[a] === 0x5A) && String.fromCharCode(...k.slice(a + 8, a + 14)) === 'FORMAT' && (k[a + 1] | k[a + 2] << 8)) return true; } return false; };
  pc2.run(2000);
  const wasRunning = running();
  for (let i = 0; i < 120 && running(); i++) pc2.run(1000);
  pc2.run(1000);
  await shot(pc2, 'format-done');
  const img = pc2.machine.fdc.img;
  ok(wasRunning && !running() && img[510] === 0x55 && img[511] === 0xAA && img[21] === 0xF0,
    `FORMAT ran (${((pc2.machine.timeMs() - t0) / 1000).toFixed(0)} s emulated) and wrote a 1.44 MB boot sector`);
  ok(String.fromCharCode(...img.slice(0x2B, 0x36)) === 'NO NAME    ', 'no volume label (the label question got Enter), "Format another?" got N');
  ok(mode(pc2) === 6 && has(pc2, 'Disk Drives:') && !has(pc2, 'Format complete') && !has(pc2, 'percent'), 'its screen output was swallowed; the Desktop is intact');
  m2.to(636, 120);
  const f2 = findText(pc2, 'FLOPPY DISK', '6x6');
  m2.dclick(f2[0].x + 33, f2[0].y - 12);
  pc2.run(3000);
  await shot(pc2, 'format-open');
  ok(has(pc2, 'A:\\') && !has(pc2, 'not responding'), 'the new disk opens: A:\\');
}

// ------------------------------------------------ the colour mode (VGA 62h)
if (want('vga')) {
  console.log('== mode 62h: 640x480 in 256 colours (BIOS + emulator)');
  let pc2 = await start([], { extra: [{ src: 'build/gem-test/MODETEST.EXE', dst: 'DOS\\' }, { src: 'build/gem-test/VDITEST.EXE', dst: 'GEMAPPS\\GEMSYS\\' }] });
  keys(pc2, 'MODETEST\r', 1500);
  const mt = pc2.screen().split('\n').filter((l) => /MODETEST/.test(l)).join('\n');
  ok(/mode 62 rows 30 cell 16/.test(mt), 'INT 10h AX=0062h: mode 62h, 80x30 text cells of 8x16', mt);
  ok(/pixels bios 14 direct 14 bar 200 scrolled 200/.test(mt), 'AH=0Ch/0Dh and direct pixels at A0000h + 640*y + x (to EAFFFh), teletype scrolls', mt);
  ok(/1Bh al 1B mode 62 colours 256 scanlines 3/.test(mt), 'AH=1Bh: 256 colours, 480 lines', mt);
  keys(pc2, 'CD \\GEMAPPS\\GEMSYS\r');
  pc2.type('GEMVDI /V VDITEST.EXE X\r');
  pc2.run(3000);
  await shot(pc2, 'vdi-vga');
  ok(mode(pc2) === 0x62, 'GEMVDI /V opens the workstation in mode 62h', mode(pc2));
  const fb = (x, y) => pc2.cpu.m8[0xA0000 + y * 640 + x];
  // the 16 colour swatches: GEM index i -> pixel value (white 0, black 15, ...)
  const sw = []; for (let i = 0; i < 16; i++) sw.push(fb(8 + i * 12 + 5, 188));
  ok(JSON.stringify(sw) === JSON.stringify([0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13]), 'the 16 GEM colours map to the pixel values of the DRI VGA drivers', sw);
  const dac = pc2.machine.vga.dac, rgb = (p) => [dac[p * 3], dac[p * 3 + 1], dac[p * 3 + 2]];
  ok(JSON.stringify([rgb(0), rgb(15), rgb(1), rgb(4)]) === JSON.stringify([[63, 63, 63], [0, 0, 0], [63, 0, 0], [0, 0, 63]]), 'and the DAC shows them as white, black, red, blue', JSON.stringify([rgb(0), rgb(15), rgb(1), rgb(4)]));
  ok(has(pc2, 'GEM VDI on ARM-DOS', '8x8'), 'text in the 8x8 font in 640x480');
  pc2.type(' ');
  pc2.run(1500);
  ok(/VDITEST: 640x480/.test(pc2.screen()) && mode(pc2) === 3, 'v_clswk: back to text mode', pc2.screen().split('\n').slice(-3).join('\n'));

  console.log('== GEM /V: the Desktop in colour');
  pc2 = await gem({ cmd: 'GEM /V\r' });
  const m2 = mouse(pc2);
  m2.home();
  await shot(pc2, 'desktop-vga');
  ok(mode(pc2) === 0x62 && has(pc2, 'Arrange', '8x16') && has(pc2, 'Disk Drives:', '8x16'), 'GEM /V: the desktop in 640x480 with the 8x16 system font');
  const px2 = (x, y) => pc2.cpu.m8[0xA0000 + y * 640 + x];
  ok(px2(4, 470) !== 0 && px2(4, 470) !== 15, 'the desktop background is in colour', px2(4, 470));
  const icon = (l) => { m2.to(636, 470); const h = findText(pc2, l, '8x8'); return h.length ? [h[0].x + 4 * l.length, h[0].y - 14] : null; };
  m2.dclick(...icon('HARD DISK'));
  pc2.run(1500);
  await shot(pc2, 'vga-open-c');
  ok(has(pc2, 'C:\\', '8x16') && has(pc2, 'COMMAND.COM', '8x8'), 'the mouse reaches the lower half: double-click HARD DISK opens C:\\');
  // redraw speed in colour: the same Arrange > Show as text
  const M = pc2.machine;
  pc2.waitIdle({ timeoutMs: 5000 });
  m2.click(170, 6); pc2.run(500);
  const t = findText(pc2, 'as text', '8x16');
  const h0 = M.haltedNs, t0 = M.timeNs(), i0 = pc2.cpu.icount;
  if (t.length) m2.click(t[0].x + 8, t[0].y + 4);
  pc2.waitIdle({ timeoutMs: 20000 });
  const busy = (M.timeNs() - t0 - (M.haltedNs - h0)) / 1e6;
  console.log(`     640x480x256 Arrange > Show as text: ${busy.toFixed(1)} ms busy, ${((pc2.cpu.icount - i0) / 1e6).toFixed(2)}M instructions`);
  await shot(pc2, 'vga-show-as-text');
  ok(t.length > 0 && has(pc2, 'COMMAND  COM', '8x16'), 'Show as text in colour');
  m2.click(28, 6); pc2.run(600);
  const e = findText(pc2, 'Exit to DOS', '8x16');
  if (e.length) m2.click(e[0].x + 20, e[0].y + 4);
  pc2.run(3000);
  ok(mode(pc2) === 3 && pc2.cpu.m8[0xD0000] === 0xFF, 'Exit to DOS: text mode, and the adapter hole is empty again');
}

// ---------------------------------------------------------- the Hercules card
if (want('hgc')) {
  console.log('== GEM on the Hercules card (720x348)');
  const pc2 = await gem({ machine: { video: 'hercules', monitor: 'amber' } });
  await shot(pc2, 'desktop-hercules');
  ok(has(pc2, 'Arrange', '8x14') && has(pc2, 'Disk Drives:', '8x14'), 'the desktop in 720x348 with the 8x14 system font');
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
