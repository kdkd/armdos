#!/usr/bin/env node
// apps/quake/tests/joystick.mjs - DOS Quake's joystick (in_dos.c's code in src/in_armdos.c)
// on the game port at 201h (emu/dev/gameport.mjs).
//
//   node apps/quake/tests/joystick.mjs [--out DIR] [--mhz N] [--no-jit]
//
//   nostick   no joystick plugged in: "joystick not found" (the 201h count loop times out)
//   stick     a stick in the port: "joystick found", the CENTER / UPPER LEFT / LOWER RIGHT
//             prompts on the DOS text screen answered with the stick and button 1, then
//             E1M1 (+map e1m1): the stick pushed forward moves the player ("edict 1" origin),
//             to the left turns him (angles), button 2 is K_JOY2 (bound to an echo)
//   nojoy     QUAKE -nojoy: no probe, no prompts
//
// Screenshots go to build/quake-test/joystick/.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage } from '../../../disk/mkimage.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const B = (p) => path.join(ROOT, 'build', p);
const argv = process.argv.slice(2);
const opt = (name, def) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : def; };
const OUT = path.resolve(opt('--out', B('quake-test/joystick')));
const MHZ = +opt('--mhz', 0) || undefined;
fs.mkdirSync(OUT, { recursive: true });

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; };

function makeImage(name) {
  const dir = path.join(OUT, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const frag = JSON.parse(fs.readFileSync(path.join(ROOT, 'apps/quake/hd.json'), 'utf8'));
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM' },
    { src: 'build/HIMEM.SYS', dst: 'DOS\\' },
    ...frag.files,
  ];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, text.replace(/\n/g, '\r\n'));
    files.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=20\n');
  put('AUTOEXEC.BAT', '@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\n');
  const { img } = buildImage({
    format: 'hd', sizeMB: 64, heads: 16, sectorsPerTrack: 63, label: 'QUAKEJOY',
    date: '1996-06-22 12:00:00', boot: { src: 'build/bootsect.bin' }, files, dirs: [...frag.dirs, 'DOS'],
  }, ROOT);
  const p = path.join(OUT, name + '.img');
  fs.writeFileSync(p, img);
  return p;
}
async function start(name) {
  const pc = await boot({ rom: B('rom.bin'), hd: makeImage(name), mhz: MHZ, jit: !argv.includes('--no-jit') });
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), `${name}: DOS prompt`);
  return pc;
}
const tap = (pc, code, after = 400) => { pc.machine.keyDown(code); pc.run(80); pc.machine.keyUp(code); pc.run(after); };
const typeKeys = (pc, text, after = 300) => {
  for (const ch of text) {
    if (ch === '"') { pc.machine.keyDown('ShiftLeft'); tap(pc, 'Quote', 30); pc.machine.keyUp('ShiftLeft'); continue; }
    tap(pc, ch === ' ' ? 'Space' : ch === '\r' ? 'Enter' : /[0-9]/.test(ch) ? 'Digit' + ch : 'Key' + ch.toUpperCase(), 30);
  }
  pc.run(after);
};
const button = (pc, n) => { pc.machine.joy.setButton(n, true); pc.run(150); pc.machine.joy.setButton(n, false); pc.run(150); };
const shot = async (pc, file) => { await pc.png(path.join(OUT, file)); console.log(`     screenshot ${path.join(OUT, file)}`); };
// the player's edict: the console's "edict 1" prints its fields to E9 (Sys_Printf)
function player(pc) {
  const d0 = pc.debug.length;
  tap(pc, 'Backquote', 500);
  typeKeys(pc, 'edict 1\r', 600);
  tap(pc, 'Backquote', 500);
  const out = pc.debug.slice(d0);
  const v = (k) => { const m = out.match(new RegExp(`\\b${k}\\s+'\\s*([-\\d.]+)\\s+([-\\d.]+)\\s+([-\\d.]+)\\s*'`)); return m ? m.slice(1).map(Number) : null; };
  if (!v('origin')) console.log(out.slice(0, 600).replace(/^/gm, '     | '));
  return { origin: v('origin'), angles: v('v_angle') || v('angles') };
}

async function nostick() {
  console.log('== nostick');
  const pc = await start('nostick');
  pc.type('CD \\GAMES\\QUAKE\rQUAKE -nosound\r');
  check(pc.until(() => pc.debug.includes('joystick not found'), { timeoutMs: 30000 }), 'no stick plugged in: "joystick not found"');
  check(!pc.debug.includes('CENTER the joystick'), 'no calibration prompts');
}

async function nojoy() {
  console.log('== nojoy');
  const pc = await start('nojoy');
  pc.machine.joy.plug(0);
  pc.type('CD \\GAMES\\QUAKE\rQUAKE -nosound -nojoy\r');
  check(pc.until(() => pc.debug.includes('execing quake.rc'), { timeoutMs: 30000 }), 'QUAKE -nojoy starts');
  check(!pc.debug.includes('joystick found') && !pc.debug.includes('CENTER the joystick'), '-nojoy: no probe, no prompts');
}

async function stick() {
  console.log('== stick');
  const pc = await start('stick');
  const joy = pc.machine.joy;
  joy.plug(0);
  pc.type('CD \\GAMES\\QUAKE\rQUAKE -nosound +map e1m1\r');
  check(pc.until(() => pc.debug.includes('CENTER the joystick'), { timeoutMs: 30000 }), '"joystick found", CENTER prompt');
  check(pc.debug.includes('joystick found'), 'probe: joystick found');
  pc.run(300);
  check(pc.hasText('CENTER the joystick') && pc.hasText('press button 1 (ESC to skip):'), 'the prompt is on the DOS text screen');
  await shot(pc, 'calibrate.png');
  button(pc, 0);
  check(pc.until(() => pc.debug.includes('UPPER LEFT'), { timeoutMs: 5000 }), 'button 1 -> UPPER LEFT prompt');
  joy.setAxis(0, -1); joy.setAxis(1, -1); pc.run(200);
  button(pc, 0);
  check(pc.until(() => pc.debug.includes('LOWER RIGHT'), { timeoutMs: 5000 }), 'button 1 -> LOWER RIGHT prompt');
  joy.setAxis(0, 1); joy.setAxis(1, 1); pc.run(200);
  button(pc, 0);
  joy.setAxis(0, 0); joy.setAxis(1, 0);
  check(pc.until(() => pc.debug.includes('joystick configured'), { timeoutMs: 5000 }), '"joystick configured."');
  check(pc.until(() => pc.debug.includes('the Slipgate Complex'), { timeoutMs: 60000 }), 'E1M1 loaded');
  pc.run(3000);
  await shot(pc, 'e1m1.png');
  const p0 = player(pc);
  check(p0.origin && p0.angles, `"edict 1": origin ${p0.origin} angles ${p0.angles}`);
  pc.run(1000);
  const p1 = player(pc);
  check(p1.origin && p0.origin && p1.origin.every((v, i) => Math.abs(v - p0.origin[i]) < 1), 'stick centred: the player stays put');
  joy.setAxis(1, -1); pc.run(1500); joy.setAxis(1, 0); pc.run(500);
  const p2 = player(pc);
  const moved = p2.origin && Math.hypot(p2.origin[0] - p1.origin[0], p2.origin[1] - p1.origin[1]);
  check(moved > 100, `stick forward: the player walked ${moved && moved.toFixed(0)} units`);
  await shot(pc, 'e1m1-forward.png');
  joy.setAxis(0, -1); pc.run(700); joy.setAxis(0, 0); pc.run(300);
  const p3 = player(pc);
  const yaw = p3.angles && ((p3.angles[1] - p2.angles[1] + 540) % 360) - 180;
  check(yaw > 20, `stick left: the player turned left ${yaw && yaw.toFixed(0)} degrees`);
  await shot(pc, 'e1m1-left.png');
  // button 2 = K_JOY2
  tap(pc, 'Backquote', 500);
  typeKeys(pc, 'bind joy2 "echo joytwo"\r', 300);
  tap(pc, 'Backquote', 500);
  const d0 = pc.debug.length;
  button(pc, 1); pc.run(300);
  check(pc.debug.slice(d0).includes('joytwo'), 'button 2 -> K_JOY2 (its binding runs)');
  if (pc.faults.length) console.log('     faults:', pc.faults.slice(0, 5));
}

for (const t of (argv.includes("stick") ? [stick] : [nostick, nojoy, stick])) await t();
console.log(failures ? `${failures} FAILED` : 'all passed');
process.exit(failures ? 1 : 0);
