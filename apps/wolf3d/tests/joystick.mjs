#!/usr/bin/env node
// apps/wolf3d/tests/joystick.mjs - Wolfenstein 3D with a joystick in the game port (201h).
//
//   node apps/wolf3d/tests/joystick.mjs [--out DIR] [--mhz N] [--no-jit]
//
// A stick is plugged in (machine.joy, emu/dev/gameport.mjs) before WOLF3D starts, so the
// startup detection (INL_StartJoy, centred) finds it. Then, as a 1992 player would:
// Control -> "Joystick Enabled" off and on again, which runs Calibrate Joystick (stick to
// the upper left + button 0, lower right + button 1) -> New Game -> E1M1. In the game:
// the view stays put with the stick centred; pushed forward it walks (the frame is compared
// with a second run that walks the same time with the Up arrow: they must look alike and
// both unlike the start); pushed right it turns; button 0 fires the pistol (the SB's
// digitized shot, DSP command C6h). F10 quits; CONFIG.WL1 is written.
// Screenshots go to build/wolf3d-test/joy/.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const B = (p) => path.join(ROOT, 'build', p);
const argv = process.argv.slice(2);
const opt = (name, def) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : def; };
const OUT = path.resolve(opt('--out', B('wolf3d-test/joy')));
const MHZ = +opt('--mhz', 0) || undefined;
fs.mkdirSync(OUT, { recursive: true });

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; };

function makeImage(name, script) {
  const dir = path.join(OUT, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const frag = JSON.parse(fs.readFileSync(path.join(ROOT, 'apps/wolf3d/hd.json'), 'utf8'));
  const files = [{ src: 'build/IO.SYS', attr: 'HSR', first: 1 }, { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 }, ...frag.files];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, text.replace(/\n/g, '\r\n'));
    files.push({ src: path.relative(ROOT, host), dst });
  };
  files.push({ src: 'build/ktest/TSHELL.EXE', dst: 'T\\TSHELL.EXE' });
  put('CONFIG.SYS', `FILES=20\nSHELL=C:\\T\\TSHELL.EXE C:\\T\\S.TXT\n`);
  put('T\\S.TXT', script);
  const { img } = buildImage({ format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'WOLFJOY',
    date: '1993-01-01 13:40:00', boot: { src: 'build/bootsect.bin' }, files, dirs: [...frag.dirs, 'T'] }, ROOT);
  const p = path.join(OUT, name + '.img');
  fs.writeFileSync(p, img);
  return p;
}

const mode = (pc) => pc.machine.vga.mode;
const tap = (pc, code, after = 400) => { pc.machine.keyDown(code); pc.run(80); pc.machine.keyUp(code); pc.run(after); };
const shot = async (pc, file) => { await pc.png(path.join(OUT, file)); console.log(`     screenshot ${path.join(OUT, file)}`); };
const frame = (pc) => { const img = pc.render(); return Uint8Array.from(img.data); };
// share of pixels (the 3D view only: the rows above the status bar) that differ
const diff = (a, b, w = 320, rows = 152) => { let n = 0; for (let i = 0; i < w * rows * 4; i += 4) if (a[i] !== b[i] || a[i + 1] !== b[i + 1] || a[i + 2] !== b[i + 2]) n++; return n / (w * rows); };
const dspCounter = (pc) => {
  const c = { c6: 0 }, sb = pc.machine.sb, sw = sb.write.bind(sb);
  sb.write = (port, v) => { if ((port & 0xF) === 0xC && v === 0xC6) c.c6++; return sw(port, v); };
  return c;
};

// boot, run WOLF3D with a stick plugged in, reach the main menu (cursor on New Game)
async function toMenu(name) {
  const hd = makeImage(name, 'cd \\GAMES\\WOLF3D\nWOLF3D\nhalt\n');
  const pc = await boot({ rom: B('rom.bin'), hd, mhz: MHZ, jit: !argv.includes('--no-jit') });
  pc.machine.joy.plug(0);                // a centred stick in port 1
  if (!pc.until(() => mode(pc) === 0x13, { timeoutMs: 60000, stepMs: 50 })) throw new Error('no mode 13h');
  pc.run(4000);
  tap(pc, 'Space', 1500); tap(pc, 'Space', 2500); tap(pc, 'Space', 1500);   // signon, PG-13, title
  for (let i = 0; i < 5; i++) tap(pc, 'ArrowUp', 250);                     // "Read This!" -> New Game
  return pc;
}
// New Game, episode 1, "Bring 'em on" -> E1M1, standing at the start
function newGame(pc) {
  tap(pc, 'Enter', 800); tap(pc, 'Enter', 800); tap(pc, 'Enter', 3500);
  pc.run(1500);
}

async function joystick() {
  console.log('== joystick: calibrate, walk, turn, fire');
  const pc = await toMenu('joy');
  const j = pc.machine.joy;
  // Control ("New Game" + 2)
  tap(pc, 'ArrowDown', 250); tap(pc, 'ArrowDown', 250);
  tap(pc, 'Enter', 1000);
  await shot(pc, 'control.png');
  const ctl = frame(pc);
  // the stick was found at startup and is enabled by default (no CONFIG.WL1): switch it off...
  tap(pc, 'Enter', 800);
  const off = frame(pc);
  check(diff(ctl, off, 320, 200) > 0, '"Joystick Enabled" is active (a stick was detected) and toggles off');
  // ...and on again: Calibrate Joystick
  tap(pc, 'Enter', 800);
  await shot(pc, 'calibrate-1.png');
  const cal1 = frame(pc);
  check(diff(off, cal1, 320, 200) > 0.05, 'the calibration window: "Move joystick to upper left and press button 0"');
  j.setAxis(0, -1); j.setAxis(1, -1); pc.run(200);
  j.setButton(0, true); pc.run(300); j.setButton(0, false); pc.run(500);
  await shot(pc, 'calibrate-2.png');
  const cal2 = frame(pc);
  check(diff(cal1, cal2, 320, 200) > 0.001, 'button 0 in the upper left -> "Move joystick to lower right and press button 1"');
  j.setAxis(0, 1); j.setAxis(1, 1); pc.run(200);
  j.setButton(1, true); pc.run(300); j.setButton(1, false); pc.run(200);
  j.setAxis(0, 0); j.setAxis(1, 0); pc.run(800);
  await shot(pc, 'calibrated.png');
  const done = frame(pc);
  check(diff(done, ctl, 320, 200) < 0.01, 'button 1 in the lower right: back on the Control screen, joystick enabled');
  tap(pc, 'Escape', 1000);               // main menu, on Control
  tap(pc, 'ArrowUp', 250); tap(pc, 'ArrowUp', 250);   // New Game
  newGame(pc);
  await shot(pc, 'e1m1-start.png');
  const start = frame(pc);
  pc.run(500);
  check(diff(start, frame(pc)) === 0, 'stick centred: the view stays put');
  // forward
  j.setAxis(1, -1); pc.run(700); j.setAxis(1, 0); pc.run(300);
  await shot(pc, 'e1m1-joy-forward.png');
  const fwd = frame(pc);
  const moved = diff(start, fwd);
  check(moved > 0.2, `stick forward: the player walks (${(moved * 100).toFixed(0)}% of the view changed)`);
  // right: turn
  j.setAxis(0, 1); pc.run(300); j.setAxis(0, 0); pc.run(300);
  await shot(pc, 'e1m1-joy-turn.png');
  const turned = diff(fwd, frame(pc));
  check(turned > 0.2, `stick right: the player turns (${(turned * 100).toFixed(0)}% of the view changed)`);
  // button 0: fire
  const dsp = dspCounter(pc);
  j.setButton(0, true); pc.run(250);
  await shot(pc, 'e1m1-joy-fire.png');
  j.setButton(0, false); pc.run(400);
  check(dsp.c6 > 0, `button 0 fires the pistol (digitized shot on the SB: ${dsp.c6} DSP C6h)`);
  // quit, CONFIG.WL1
  tap(pc, 'F10', 800);
  pc.machine.keyDown('KeyY'); pc.run(80); pc.machine.keyUp('KeyY');
  check(pc.until(() => pc.serial.includes('T:EXIT WOLF3D'), { timeoutMs: 20000, stepMs: 5 }), 'F10 quits to DOS');
  const fr = new FatReader(Buffer.from(pc.machine.ata.img.buffer, pc.machine.ata.img.byteOffset, pc.machine.ata.img.length));
  let ent = null; try { ent = fr.lookup('GAMES\\WOLF3D\\CONFIG.WL1'); } catch (e) { /* not there */ }
  check(!!ent, 'CONFIG.WL1 written');
  return fwd;
}

async function keyboardReference() {
  console.log('== reference: the same walk with the Up arrow');
  const pc = await toMenu('kbd');
  newGame(pc);
  const start = frame(pc);
  pc.machine.keyDown('ArrowUp'); pc.run(700); pc.machine.keyUp('ArrowUp'); pc.run(300);
  await shot(pc, 'e1m1-kbd-forward.png');
  return { start, fwd: frame(pc) };
}

const fwdJoy = await joystick();
const ref = await keyboardReference();
const d = diff(fwdJoy, ref.fwd), d0 = diff(ref.start, ref.fwd);
check(d < d0 / 3, `the stick walked as far as the Up arrow does (joystick vs keyboard view: ${(d * 100).toFixed(1)}% differ; start vs walked: ${(d0 * 100).toFixed(0)}%)`);
console.log(failures ? `${failures} FAILED` : 'all ok');
process.exit(failures ? 1 : 0);
