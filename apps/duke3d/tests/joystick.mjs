#!/usr/bin/env node
// apps/duke3d/tests/joystick.mjs - Duke Nukem 3D with a joystick on the game port (201h).
//
//   node apps/duke3d/tests/joystick.mjs [--out DIR] [--no-jit]
//
//   stick     DUKE3D.CFG says ControllerType = 2 (keyboard and joystick, as SETUP.EXE
//             wrote it); a stick is plugged in (m.joy). At start-up, in text mode, the
//             MACT calibration asks "Center the joystick and press a button", then the
//             upper-left and lower-right corners; the test moves the stick there and
//             presses button 1 each time. Then E1L1: stick forward moves the player
//             (ps[0].posx/posy), stick right turns him (ps[0].ang), button 1 fires the
//             pistol (ammo 48 -> 47), centred = standing still.
//   nostick   ControllerType = 2 but nothing in the game port: no calibration prompt,
//             the game starts on the keyboard.
//
// Screenshots go to build/duke3d-test/joystick-*.png.

import fs from 'node:fs';
import path from 'node:path';
import { B, start, mode, tap, frameHash, elfSymbols, structOffsets } from './lib.mjs';

const argv = process.argv.slice(2);
const opt = (name, def) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : def; };
const OUT = path.resolve(opt('--out', B('duke3d-test')));
const JIT = !argv.includes('--no-jit');
fs.mkdirSync(OUT, { recursive: true });

let failures = 0;
const check = (ok, what, extra = '') => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${extra ? '  ' + extra : ''}`); if (!ok) failures++; };
const shot = async (pc, file) => { await pc.png(path.join(OUT, file)); console.log(`     screenshot ${path.join(OUT, file)}`); };

// ps[0] (struct player_struct) in the game image: offsets from the ELF's DWARF
const ELF = B('obj/D3DGAME/D3DGAME.elf');
const SYMS = elfSymbols(ELF);
const PS = structOffsets(ELF, 'player_struct') || {};
const OFF = { posx: PS.posx, posy: PS.posy, ang: PS.ang, ammo: PS.ammo_amount + 2 };   // ammo_amount[PISTOL_WEAPON = 1], int16_t
if (!SYMS.has('ps') || !Object.values(OFF).every(Number.isInteger)) {
  console.log(`FAIL ps[0] and struct player_struct not found in ${ELF}'s symbols/debug info: ${JSON.stringify(OFF)}`);
  process.exit(1);
}
const base = (pc) => parseInt((pc.debug.match(/image at ([0-9A-F]+)/) || [0, '0'])[1], 16);
const peek32 = (pc, o) => pc.cpu.m32[(base(pc) + SYMS.get('ps') + o) >>> 2] | 0;
const peek16 = (pc, o) => { const a = base(pc) + SYMS.get('ps') + o; return (pc.cpu.m32[a >>> 2] >>> ((a & 2) * 8)) << 16 >> 16; };

function cfgFile(name) {
  const p = path.join(OUT, name + '-DUKE3D.CFG');
  fs.writeFileSync(p, '[Controls]\r\nControllerType = 2\r\n');
  return { src: path.relative(path.resolve(B('..')), p), dst: 'GAMES\\DUKE3D\\DUKE3D.CFG' };
}

async function toGame(pc) {
  pc.until(() => /demo1\.dmo/i.test(pc.debug), { timeoutMs: 30000, stepMs: 100 });
  pc.run(500);
  tap(pc, 'Escape', 1200);
  tap(pc, 'Enter', 1000);
  tap(pc, 'Enter', 1000);
  tap(pc, 'ArrowDown', 400);
  tap(pc, 'Enter', 200);
  return pc.until(() => /Entering E1L1/.test(pc.debug), { timeoutMs: 20000 });
}
const press = (pc, n = 0) => { pc.machine.joy.setButton(n, true); pc.run(150); pc.machine.joy.setButton(n, false); pc.run(300); };

async function stick() {
  console.log('== stick');
  const pc = await start(OUT, 'joystick', { jit: JIT, extraFiles: [cfgFile('joystick')] });
  const joy = pc.machine.joy;
  joy.plug(0);
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('CD \\GAMES\\DUKE3D\rDUKE3D\r');
  check(pc.waitText('Center the joystick and press a button', { timeoutMs: 20000 }), 'calibration: "Center the joystick and press a button"');
  pc.run(500);
  check(mode(pc) !== 0x13, 'the game waits in text mode for the button');
  press(pc);
  check(pc.waitText('Move joystick to upper-left corner and press a button', { timeoutMs: 5000 }), 'calibration: upper-left');
  joy.setAxis(0, -1); joy.setAxis(1, -1); pc.run(200); press(pc);
  check(pc.waitText('Move joystick to lower-right corner and press a button', { timeoutMs: 5000 }), 'calibration: lower-right');
  joy.setAxis(0, 1); joy.setAxis(1, 1); pc.run(200); press(pc);
  joy.setAxis(0, 0); joy.setAxis(1, 0);
  console.log(pc.screen().split('\n').filter((l) => /joystick/i.test(l)).map((l) => '     | ' + l).join('\n'));
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 30000 }), 'then the game starts (mode 13h)');
  check(await toGame(pc), 'New Game -> E1L1');
  pc.run(3000);
  const P = () => ({ x: peek32(pc, OFF.posx), y: peek32(pc, OFF.posy), a: peek16(pc, OFF.ang) & 2047 });
  const p0 = P(); pc.run(1000); const p1 = P();
  check(p0.x === p1.x && p0.y === p1.y && p0.a === p1.a, 'stick centred: the player stands still', JSON.stringify([p0, p1]));
  const h0 = frameHash(pc);
  joy.setAxis(1, -1); pc.run(1500); joy.setAxis(1, 0); pc.run(400);
  const p2 = P();
  const d = Math.hypot(p2.x - p1.x, p2.y - p1.y);
  check(d > 500 && p2.a === p1.a, `stick forward walks forward (${d.toFixed(0)} units)`, JSON.stringify([p1, p2]));
  check(frameHash(pc) !== h0, 'the view changed');
  await shot(pc, 'joystick-walk.png');
  joy.setAxis(0, 1); pc.run(600); joy.setAxis(0, 0); pc.run(400);
  const p3 = P();
  check(p3.a !== p2.a && Math.hypot(p3.x - p2.x, p3.y - p2.y) < 50, `stick right turns (ang ${p2.a} -> ${p3.a})`);
  await shot(pc, 'joystick-turn.png');
  const a0 = peek16(pc, OFF.ammo);
  press(pc, 0); pc.run(700);
  check(peek16(pc, OFF.ammo) === a0 - 1, `button 1 fires (pistol ammo ${a0} -> ${peek16(pc, OFF.ammo)})`);
  await shot(pc, 'joystick-fire.png');
}

async function nostick() {
  console.log('== nostick');
  const pc = await start(OUT, 'joystick-none', { jit: JIT, extraFiles: [cfgFile('joystick-none')] });
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('CD \\GAMES\\DUKE3D\rDUKE3D\r');
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 30000 }), 'no stick: the game starts (mode 13h)', mode(pc) === 0x13 ? '' : '\n' + pc.screen());
  check(!/Center the joystick/.test(pc.screen() + pc.debug), 'no calibration prompt');
}

if (!process.env.ONLY || process.env.ONLY === 'stick') await stick();
if (!process.env.ONLY || process.env.ONLY === 'nostick') await nostick();
console.log(failures ? `${failures} check(s) FAILED` : 'all checks passed');
process.exit(failures ? 1 : 0);
