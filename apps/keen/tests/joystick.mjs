// apps/keen/tests/joystick.mjs - Keen Dreams with a joystick in the game port (201h),
// the way KDREAMS.EXE did it: ID_IN.C's IN_GetJoyAbs polls the port (be_armdos.c),
// IN_Startup finds the stick, the control panel's "Use / Configure Joystick 1" and
// "Configure Joystick" calibrate it (upper-left + button, lower-right + button), and
// the game is played with it.
//
//   node apps/keen/tests/joystick.mjs
//
// The unmodified shareware files (3rdparty/keen/KEENDRMS.ZIP, unpacked on the host)
// go straight onto a private C:, so no BBS call is needed. Screenshots: build/keen-joy-test/.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { startPC, check, failed, has, B, ROOT } from '../../term/tests/lib.mjs';
import { build as buildImage } from '../../../disk/mkimage.mjs';
import { execFileSync } from 'node:child_process';

const OUT = B('keen-joy-test');
fs.mkdirSync(OUT, { recursive: true });
const shot = async (pc, name) => { const p = path.join(OUT, name + '.png'); await pc.png(p); console.log(`     screenshot ${p}`); };

function unzipHost(buf) {
  const eocd = buf.lastIndexOf(Buffer.from([0x50, 0x4b, 0x05, 0x06]));
  const n = buf.readUInt16LE(eocd + 10);
  let p = buf.readUInt32LE(eocd + 16);
  const files = {};
  for (let i = 0; i < n; i++) {
    const method = buf.readUInt16LE(p + 10), csize = buf.readUInt32LE(p + 20);
    const nl = buf.readUInt16LE(p + 28), xl = buf.readUInt16LE(p + 30), cl = buf.readUInt16LE(p + 32);
    const lho = buf.readUInt32LE(p + 42), name = buf.toString('latin1', p + 46, p + 46 + nl);
    const d = lho + 30 + buf.readUInt16LE(lho + 26) + buf.readUInt16LE(lho + 28);
    const raw = buf.subarray(d, d + csize);
    files[name.toUpperCase()] = method === 0 ? Buffer.from(raw) : zlib.inflateRawSync(raw);
    p += 46 + nl + xl + cl;
  }
  return files;
}

// ---- C: with KEEN.EXE and the game data in C:\GAMES\KEEN
const tmp = path.join(OUT, 'disk.tmp');
fs.rmSync(tmp, { recursive: true, force: true });
fs.mkdirSync(tmp, { recursive: true });
const put = (name, data) => { const p = path.join(tmp, name); fs.writeFileSync(p, data); return path.relative(ROOT, p); };
const text = (s) => s.replace(/\r?\n/g, '\r\n');
const files = [
  { src: 'build/IO.SYS', attr: 'HSR', first: 1 }, { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
  { src: 'build/COMMAND.COM' }, { src: 'build/HIMEM.SYS', dst: 'DOS\\' },
  { src: put('CONFIG.SYS', text('DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=20\nBUFFERS=20\nSHELL=C:\\COMMAND.COM C:\\ /P\n')), dst: 'CONFIG.SYS' },
  { src: put('AUTOEXEC.BAT', text('@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\nSET BLASTER=A220 I7 D1 H5 T6\n')), dst: 'AUTOEXEC.BAT' },
  { src: 'build/KEEN.EXE', dst: 'GAMES\\KEEN\\KEEN.EXE' },
];
for (const [name, data] of Object.entries(unzipHost(fs.readFileSync(path.join(ROOT, '3rdparty/keen/KEENDRMS.ZIP')))))
  files.push({ src: put(name, data), dst: 'GAMES\\KEEN\\' + name });
const { img } = buildImage({ format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'KEENJOY', date: '1992-10-01 20:00:00',
  boot: { src: 'build/bootsect.bin' }, files, dirs: ['DOS', 'GAMES', 'GAMES\\KEEN'] }, ROOT);
fs.rmSync(tmp, { recursive: true, force: true });

const v = await startPC(img, { rtcBaseMs: new Date(1992, 9, 1, 20, 0, 0).getTime() });
const j = v.m.joy;
const run = async (ms) => { const end = v.m.timeMs() + ms; while (v.m.timeMs() < end) { v.m.runFor(10); } };
const until = async (pred, ms) => { const end = v.m.timeMs() + ms; while (v.m.timeMs() < end) { v.m.runFor(10); if (pred()) return true; } return !!pred(); };
const typed = async (keys, pause = 300) => { v.type(keys); await run(pause); await until(() => v.m.typingDone(), 20000); };
const tap = async (code, ms = 120) => { v.m.keyDown(code); await run(ms); v.m.keyUp(code); await run(250); };
const press = async (b, ms = 200) => { j.setButton(b, true); await run(ms); j.setButton(b, false); await run(300); };
const frame = () => Buffer.from(v.m.cpu.m8.subarray(0xA0000, 0xA0000 + 64000));
const diff = (a, b) => { let n = 0; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) n++; return n; };

check(await until(() => has(v, 'C:\\>'), 30000), 'ARM-DOS boots to C:\\>');
j.plug(0);                                                   // a stick in the game port, centred, before KEEN starts
await typed('CD \\GAMES\\KEEN\rKEEN\r', 500);
check(await until(() => has(v, 'Ready - Press a Key'), 20000), 'KEEN: the loading screen');
await typed('{ENTER}', 200);
check(await until(() => v.m.vga.mode === 0x13, 5000), 'graphics mode');
await run(2500);
await typed(' ', 200); await run(2500);
await shot(v, 'controlpanel');
// "Choose Controls" (hotkey C), then Joystick 1 (right of Keyboard) and Enter
await tap('KeyC'); await run(500);
await shot(v, 'controls');
await tap('ArrowRight'); await tap('Enter'); await run(800);
await shot(v, 'joystick1');
// "Configure Joystick" (below), Enter -> "Move Joystick to the Upper-Left"
await tap('ArrowDown'); await tap('Enter'); await run(800);
await shot(v, 'calibrate-ul');
j.setAxis(0, -1); j.setAxis(1, -1); await run(300); await press(0);
await shot(v, 'calibrate-lr');
j.setAxis(0, 1); j.setAxis(1, 1); await run(300); await press(0);
j.setAxis(0, 0); j.setAxis(1, 0); await run(800);
await shot(v, 'calibrated');
// KEEN's variables, from the ELF's symbols: the program is loaded at its PSP + 100h
const sym = {};
for (const l of execFileSync('arm-none-eabi-nm', [B('obj/KEEN/KEEN.elf')], { encoding: 'utf8' }).split('\n')) { const [a, , n] = l.split(' '); if (n) sym[n] = parseInt(a, 16); }
const m8 = v.m.cpu.m8, dv = new DataView(m8.buffer);
function loadBase() {
  for (let a = 0x500; a < 0xA0000; a += 16) {
    if ((m8[a] === 0x4D || m8[a] === 0x5A) && String.fromCharCode(...m8.subarray(a + 8, a + 12)) === 'KEEN' && m8[a + 12] === 0 &&
        dv.getUint16(a + 1, true) === (a + 16) >> 4) return a + 16 + 0x100;
  }
  return -1;
}
const base = loadBase();
check(base > 0, `KEEN.EXE found in memory (load base ${base.toString(16)}h)`);
check(m8[base + sym.JoysPresent] === 1 && m8[base + sym.JoysPresent + 1] === 0, 'IN_Startup found joystick 1, not joystick 2 (INL_StartJoy: the port answered)');
const jd = base + sym.JoyDefs, w = (o) => dv.getUint16(jd + o, true);
const [minX, minY, tminX, tminY, tmaxX, tmaxY, maxX, maxY] = [0, 2, 4, 6, 8, 10, 12, 14].map(w);
console.log(`     JoyDefs[0]: min ${minX},${minY} thresholds ${tminX}-${tmaxX}, ${tminY}-${tmaxY} max ${maxX},${maxY}`);
check(minX > 10 && minX < 60 && maxX > 900 && maxX < 1300 && minY > 10 && minY < 60 && maxY > 900 && maxY < 1300,
  'calibration: upper-left ~25, lower-right ~1100 counts (24.2 us + 0.011 us/ohm, 1 us per poll)');
check(tminX < 560 && tmaxX > 560 && tminY < 560 && tmaxY > 560, 'the centre (~560) lies inside the dead zone');

// ---- start a game: the Start button, New Game, a difficulty
for (let k = 0; k < 3; k++) await tap('ArrowLeft');
for (let k = 0; k < 6; k++) await tap('ArrowUp');
await tap('Enter'); await run(1500);
await tap('ArrowRight'); await tap('ArrowDown'); await tap('Enter');
await run(3000);
await shot(v, 'worldmap');
const pl = () => { const p = dv.getUint32(base + sym.player, true); return p ? { x: dv.getUint16(p + 6, true), y: dv.getUint16(p + 8, true) } : null; };
const p0 = pl();
check(p0 !== null, `on the world map: Keen at ${p0 && p0.x},${p0 && p0.y}`);
check(m8[base + sym.Controls] === 2, `playing with Controls[0] = ctrl_Joystick1 (${m8[base + sym.Controls]})`);
await run(1000);
const p1 = pl();
check(p1.x === p0.x && p1.y === p0.y, 'stick centred: Keen stands still');
j.setAxis(0, -1); await run(900); j.setAxis(0, 0); await run(200);
const p2 = pl();
check(p2.x < p1.x - 100, `stick pushed left: Keen walks left on the map (x ${p1.x} -> ${p2.x})`);
j.setAxis(1, -1); await run(300); j.setAxis(1, 0); await run(200);
const p3 = pl();
check(p3.y < p2.y, `stick pushed up: Keen walks up (y ${p2.y} -> ${p3.y})`);
await press(0, 200);
await run(3000);
await shot(v, 'level');
const p4 = pl();
check(p4 && (p4.x !== p3.x || p4.y !== p3.y), `button 1 on the level's flag: into the level (Keen at ${p4 && p4.x},${p4 && p4.y})`);
await run(1500);
const q0 = pl();
j.setAxis(0, 1); await run(1500); j.setAxis(0, 0); await run(300);
const q1 = pl();
await shot(v, 'level-right');
check(q1.x > q0.x + 100, `in the level, stick right: Keen runs right (x ${q0.x} -> ${q1.x})`);
await run(800);
const y0 = pl().y;
let ymin = y0;
j.setButton(0, true);
for (let k = 0; k < 40; k++) { await run(10); ymin = Math.min(ymin, pl().y); }
await shot(v, 'level-jump');
j.setButton(0, false); await run(1500);
check(ymin < y0 - 64, `button 1: Keen jumps (y ${y0} -> ${ymin} at the top)`);
check(Math.abs(pl().y - y0) < 16, 'and lands again');

// ---- quit (Esc, Enter), the configuration keeps the joystick
await typed('{ESC}', 200); await run(2000); await typed('{ENTER}', 200);
check(await until(() => has(v, 'C:\\GAMES\\KEEN>'), 10000), 'quit to DOS');

const f = failed();
console.log(f ? `\n${f} check(s) FAILED` : '\nall keen joystick checks passed');
process.exit(f ? 1 : 0);
