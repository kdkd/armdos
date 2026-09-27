#!/usr/bin/env node
// apps/doom/tests/joystick.mjs - DOOM's joystick on the game port (201h).
//
//   node apps/doom/tests/joystick.mjs [--out DIR] [--no-jit]
//
// Vanilla DOS DOOM: with use_joystick 1 in DEFAULT.CFG, I_StartupJoystick reads
// the stick through the BIOS (INT 15h AH=84h) and asks for the centre, the upper
// left and the lower right corner, each confirmed with button 1. In the game the
// stick walks and turns (I_JoystickEvents, ev_joystick) and button 1 fires
// (joyb_fire 0). Checks:
//   * use_joystick 1 + a stick: "joystick found" and the three prompts, answered
//     by moving the emulated stick (m.joy) and pressing button 1
//   * E1M1 (-warp 1 1): standing still the view stays put; the stick pushed
//     forward moves it (screen difference), pushed right turns it
//   * button 1 fires: the ammo count in the status bar changes
//   * use_joystick 1 with no stick plugged in: "joystick not found", game starts
//   * use_joystick 0 (vanilla default): no joystick messages at all
// Screenshots go to build/doom-test/joy/.

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
const OUT = path.resolve(opt('--out', B('doom-test/joy')));
fs.mkdirSync(OUT, { recursive: true });

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; };

function makeImage(name, cfg) {
  const dir = path.join(OUT, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/DOOM.EXE', dst: 'GAMES\\DOOM\\' },
    { src: '3rdparty/doom/DOOM1.WAD', dst: 'GAMES\\DOOM\\' },
    { src: 'build/ktest/TSHELL.EXE', dst: 'T\\TSHELL.EXE' },
  ];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, text.replace(/\n/g, '\r\n'));
    files.push({ src: path.relative(ROOT, host), dst });
  };
  const himem = fs.existsSync(B('HIMEM.SYS'));
  if (himem) files.push({ src: 'build/HIMEM.SYS', dst: 'DOS\\HIMEM.SYS' });
  put('CONFIG.SYS', `FILES=20\n${himem ? 'DEVICE=C:\\DOS\\HIMEM.SYS\n' : ''}SHELL=C:\\T\\TSHELL.EXE C:\\T\\S.TXT\n`);
  put('T\\S.TXT', `cd \\GAMES\\DOOM\nC:\\GAMES\\DOOM\\DOOM.EXE -warp 1 1\nhalt\n`);
  if (cfg !== null) put('GAMES\\DOOM\\DEFAULT.CFG', cfg);
  const { img } = buildImage({
    format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'DOOMJOY',
    date: '1993-12-10 12:00:00', boot: { src: 'build/bootsect.bin' }, files,
    dirs: ['GAMES', 'GAMES\\DOOM', 'DOS', 'T'],
  }, ROOT);
  const p = path.join(OUT, name + '.img');
  fs.writeFileSync(p, img);
  return p;
}
const start = async (name, cfg) => boot({ rom: B('rom.bin'), hd: makeImage(name, cfg), jit: !argv.includes('--no-jit') });

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

// ------------------------------------------------------------ calibrate + play
{
  console.log('== use_joystick 1, a stick in the game port');
  const pc = await start('joy', 'use_joystick 1\n');
  const j = pc.machine.joy;
  j.plug(0);                                   // centred
  const prompt = (t) => pc.waitText(t, { timeoutMs: 60000, stepMs: 20 });
  const press = () => { j.setButton(0, true); pc.run(150); j.setButton(0, false); pc.run(150); };
  check(prompt('joystick found'), '"joystick found"');
  check(prompt('CENTER the JOYSTICK and press button 1:'), 'prompt: CENTER the JOYSTICK and press button 1:');
  pc.run(300); press();
  check(prompt('Push the JOYSTICK to the UPPER LEFT corner and press button 1:'), 'prompt: UPPER LEFT corner');
  j.setAxis(0, -1); j.setAxis(1, -1); pc.run(200); press();
  check(prompt('Push the JOYSTICK to the LOWER RIGHT corner and press button 1:'), 'prompt: LOWER RIGHT corner');
  j.setAxis(0, 1); j.setAxis(1, 1); pc.run(200); press();
  j.setAxis(0, 0); j.setAxis(1, 0);
  const text = pc.screen();
  console.log(text.split('\n').filter((l) => /joystick|JOYSTICK/.test(l)).map((l) => '     ' + l).join('\n'));
  check(pc.until(() => pc.machine.vga.mode === 0x13, { timeoutMs: 60000, stepMs: 50 }), 'E1M1 in mode 13h after the calibration');
  pc.run(3000);                                // the wipe, settle
  const f0 = frame(pc); await shot(pc, 'start.png');
  pc.run(1000);
  const f1 = frame(pc);
  const still = diff(f0, f1, 0, 168);
  check(still < 2000, `stick centred: the view stays put (${still} pixels changed)`);
  j.setAxis(1, -1);                            // push forward
  pc.run(1000);
  j.setAxis(1, 0); pc.run(400);
  const f2 = frame(pc); await shot(pc, 'forward.png');
  const moved = diff(f1, f2, 0, 168);
  check(moved > 10000, `stick forward: the player walked (${moved} pixels changed)`);
  j.setAxis(0, 1);                             // push right: turn
  pc.run(500);
  j.setAxis(0, 0); pc.run(400);
  const f3 = frame(pc); await shot(pc, 'turn.png');
  const turned = diff(f2, f3, 0, 168);
  check(turned > 10000, `stick right: the player turned (${turned} pixels changed)`);
  pc.run(1000);
  const f4 = frame(pc);
  j.setButton(0, true); pc.run(1200); j.setButton(0, false); pc.run(500);  // button 1 = fire
  const f5 = frame(pc); await shot(pc, 'fire.png');
  const ammo = diff(f4, f5, 171, 192, 0, 48);
  check(ammo > 20, `button 1 fires: the ammo count changed (${ammo} pixels)`);
}

// ------------------------------------------------------------ no stick
{
  console.log('== use_joystick 1, nothing plugged in');
  const pc = await start('nostick', 'use_joystick 1\n');
  check(pc.waitText('joystick not found', { timeoutMs: 60000, stepMs: 20 }), '"joystick not found"');
  check(pc.until(() => pc.machine.vga.mode === 0x13, { timeoutMs: 60000, stepMs: 50 }), 'the game starts anyway');
}
{
  console.log('== vanilla default (use_joystick 0), a stick plugged in');
  const pc = await start('off', null);
  pc.machine.joy.plug(0);
  let seen = false;
  const ok = pc.until(() => { if (/joystick/i.test(pc.screen())) seen = true; return pc.machine.vga.mode === 0x13; }, { timeoutMs: 60000, stepMs: 20 });
  check(ok && !seen, 'no joystick messages, the game starts');
}

console.log(failures ? `doom joystick: ${failures} FAILED` : 'doom joystick: all passed');
process.exit(failures ? 1 : 0);
