#!/usr/bin/env node
// apps/doom/tests/mouse.mjs - DOOM's mouse through the INT 33h driver (MOUSE.COM).
//
//   node apps/doom/tests/mouse.mjs [--out DIR] [--no-jit]
//
// Vanilla DOS DOOM: I_StartupMouse resets the driver (INT 33h AX=0000h) and
// says "Mouse: detected"; every tic AX=0003h (buttons) and AX=000Bh (mickeys)
// become an ev_mouse. Checks, with PS/2 motion injected into the 8042:
//   * MOUSE.COM loaded: "Mouse: detected"; in E1M1 (-warp 1 1) the view stays
//     put with the mouse still, turns when it moves right, button 1 fires
//     (ammo changes), and mouse_sensitivity scales the turn
//   * no driver: "Mouse: not present", the game starts
//   * use_mouse 0 in DEFAULT.CFG: no mouse messages
// Screenshots go to build/doom-test/mouse/.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage } from '../../../disk/mkimage.mjs';
import { renderScreen } from '../../../emu/render.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const B = (p) => path.join(ROOT, 'build', p);
const argv = process.argv.slice(2);
const opt = (name, def) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : def; };
const OUT = path.resolve(opt('--out', B('doom-test/mouse')));
fs.mkdirSync(OUT, { recursive: true });

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; };

function makeImage(name, cfg, driver = true) {
  const dir = path.join(OUT, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/DOOM.EXE', dst: 'GAMES\\DOOM\\' },
    { src: '3rdparty/doom/DOOM1.WAD', dst: 'GAMES\\DOOM\\' },
    { src: 'build/ktest/TSHELL.EXE', dst: 'T\\TSHELL.EXE' },
    { src: 'build/MOUSE.COM', dst: 'DOS\\' },
  ];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, text.replace(/\n/g, '\r\n'));
    files.push({ src: path.relative(ROOT, host), dst });
  };
  const himem = fs.existsSync(B('HIMEM.SYS'));
  if (himem) files.push({ src: 'build/HIMEM.SYS', dst: 'DOS\\HIMEM.SYS' });
  put('CONFIG.SYS', `FILES=20\n${himem ? 'DEVICE=C:\\DOS\\HIMEM.SYS\n' : ''}SHELL=C:\\T\\TSHELL.EXE C:\\T\\S.TXT\n`);
  put('T\\S.TXT', `${driver ? 'C:\\DOS\\MOUSE.COM\n' : ''}cd \\GAMES\\DOOM\nC:\\GAMES\\DOOM\\DOOM.EXE -warp 1 1\nhalt\n`);
  if (cfg !== null) put('GAMES\\DOOM\\DEFAULT.CFG', cfg);
  const { img } = buildImage({
    format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'DOOMMOUSE',
    date: '1993-12-10 12:00:00', boot: { src: 'build/bootsect.bin' }, files,
    dirs: ['GAMES', 'GAMES\\DOOM', 'DOS', 'T'],
  }, ROOT);
  const p = path.join(OUT, name + '.img');
  fs.writeFileSync(p, img);
  return p;
}
const start = async (name, cfg, driver) => boot({ rom: B('rom.bin'), hd: makeImage(name, cfg, driver), jit: !argv.includes('--no-jit') });

// the 3D view (above the status bar) and the status bar's ammo digits
const frame = (pc) => renderScreen(pc.machine).data;
function diff(a, b, y0, y1, x0 = 0, x1 = 320) {
  let n = 0;
  for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
    const i = (y * 320 + x) * 4;
    if (a[i] !== b[i] || a[i + 1] !== b[i + 1] || a[i + 2] !== b[i + 2]) n++;
  }
  return n;
}
const shot = async (pc, f) => { await pc.png(path.join(OUT, f)); };


// move the mouse in small steps (as a hand would), `ms` of emulated time
function sweep(pc, dx, dy, ms) {
  const steps = Math.max(1, Math.round(ms / 20));
  for (let i = 0; i < steps; i++) { pc.machine.mouseMove(dx, dy); pc.run(20); }
}

async function play(name, cfg) {
  const pc = await start(name, cfg, true);
  check(pc.waitText('Mouse: detected', { timeoutMs: 60000, stepMs: 20 }), `${name}: "Mouse: detected"`);
  check(pc.until(() => pc.machine.vga.mode === 0x13, { timeoutMs: 60000, stepMs: 50 }), `${name}: E1M1 in mode 13h`);
  pc.run(3000);
  const f0 = frame(pc); await shot(pc, `${name}-start.png`);
  pc.run(1000);
  const f1 = frame(pc);
  const still = diff(f0, f1, 0, 168);
  check(still < 2000, `${name}: mouse still: the view stays put (${still} pixels changed)`);
  return { pc, f1 };
}

{
  console.log('== MOUSE.COM loaded (default sensitivity 5)');
  const { pc, f1 } = await play('mouse', null);
  sweep(pc, 4, 0, 300); pc.run(300);            // right: turn right
  const f2 = frame(pc); await shot(pc, 'mouse-turn.png');
  const turned = diff(f1, f2, 0, 168);
  check(turned > 10000, `mouse right: the player turned (${turned} pixels changed)`);
  pc.run(1000);
  const f3 = frame(pc);
  pc.machine.mouseButtons(1); pc.run(1200); pc.machine.mouseButtons(0); pc.run(500);
  const f4 = frame(pc); await shot(pc, 'mouse-fire.png');
  const ammo = diff(f3, f4, 171, 192, 0, 48);
  check(ammo > 20, `button 1 fires: the ammo count changed (${ammo} pixels)`);
  pc.machine.mouseButtons(4); pc.run(800); pc.machine.mouseButtons(0); pc.run(400);   // button 3 = forward
  const f5 = frame(pc);
  check(diff(f4, f5, 0, 168) > 10000, 'button 3 walks forward');
}

// mouse_sensitivity: the same small motion turns further at 9 than at 0
async function turnAt(sens) {
  const { pc, f1 } = await play(`sens${sens}`, `mouse_sensitivity ${sens}\n`);
  // a fixed motion; measure the view change against the still frame
  sweep(pc, 2, 0, 200); pc.run(400);
  const d = diff(f1, frame(pc), 0, 168);
  await shot(pc, `sens${sens}.png`);
  return d;
}
{
  console.log('== mouse_sensitivity');
  const lo = await turnAt(0), hi = await turnAt(9);
  console.log(`     view change: sensitivity 0 -> ${lo} pixels, 9 -> ${hi} pixels`);
  check(hi > lo, 'higher mouse_sensitivity turns further for the same motion');
}

{
  console.log('== no mouse driver');
  const pc = await start('nodriver', null, false);
  check(pc.waitText('Mouse: not present', { timeoutMs: 60000, stepMs: 20 }), '"Mouse: not present"');
  check(pc.until(() => pc.machine.vga.mode === 0x13, { timeoutMs: 60000, stepMs: 50 }), 'the game starts anyway');
}
{
  console.log('== use_mouse 0');
  const pc = await start('off', 'use_mouse 0\n', true);
  let seen = false;
  const ok = pc.until(() => { if (/I_StartupMouse|Mouse:/.test(pc.screen())) seen = true; return pc.machine.vga.mode === 0x13; }, { timeoutMs: 60000, stepMs: 20 });
  check(ok && !seen, 'no mouse messages, the game starts');
}

console.log(failures ? `doom mouse: ${failures} FAILED` : 'doom mouse: all passed');
process.exit(failures ? 1 : 0);
