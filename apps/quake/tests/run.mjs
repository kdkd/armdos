#!/usr/bin/env node
// apps/quake/tests/run.mjs - boot ARM-DOS headless and play Quake.
//
//   node apps/quake/tests/run.mjs [play] [timedemo] [fromroot] [nomem] [--out DIR] [--mhz N] [--no-jit]
//
// Builds its own hard disk image: IO.SYS, ARMDOS.SYS, COMMAND.COM, HIMEM.SYS
// (CONFIG.SYS), SET BLASTER (AUTOEXEC.BAT), MOUSE.COM, and C:\GAMES\QUAKE\ as
// apps/quake/hd.json has it; boots it and types at the real DOS prompt, then
// drives the game with scan codes through the 8042.
//
//   play      QUAKE: startup text, mode 13h, the console and the start map
//             (demo loop), the main menu, Single Player -> New Game (the
//             start map), the console (~): "map e1m1" ("the Slipgate
//             Complex"), walk, turn, jump, fire (the shotgun's sound on the
//             SB16), mouse look (MOUSE.COM), "version", quit with Y -> the
//             END1.BIN sell screen in text mode. MEM first: QUAKE.EXE fits
//             in conventional memory with MOUSE.COM loaded. Checks INT 08h/09h hooked and restored, the BIOS tick
//             kept at 18.2 Hz, the SB16 DMA stream, PIT/text mode on exit.
//   timedemo  QUAKE +timedemo demo1: fps at the machine's clock (100 MHz
//             unless --mhz), instructions, host MIPS.
//   fromroot  C:\\>GAMES\\QUAKE\\QUAKE: the data is found next to QUAKE.EXE.
//   nomem     QUAKE with HIMEM.SYS left out and -mem 20: "Not enough memory".
//
// Screenshots go to build/quake-test/.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const B = (p) => path.join(ROOT, 'build', p);

const argv = process.argv.slice(2);
const opt = (name, def) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : def; };
const OUT = path.resolve(opt('--out', B('quake-test')));
const MHZ = +opt('--mhz', 0) || undefined;
const which = argv.filter((a, i) => !a.startsWith('--') && !(i > 0 && argv[i - 1].startsWith('--') && argv[i - 1] !== '--no-jit'));
fs.mkdirSync(OUT, { recursive: true });

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; };

// ------------------------------------------------------------ the disk
function makeImage(name, { autoexec, himem = true }) {
  const dir = path.join(OUT, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const frag = JSON.parse(fs.readFileSync(path.join(ROOT, 'apps/quake/hd.json'), 'utf8'));
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM' },
    { src: 'build/HIMEM.SYS', dst: 'DOS\\' },
    { src: 'build/MOUSE.COM', dst: 'DOS\\' },
    { src: 'build/MEM.EXE', dst: 'DOS\\' },
    ...frag.files,
  ];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, text.replace(/\n/g, '\r\n'));
    files.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', (himem ? 'DEVICE=C:\\DOS\\HIMEM.SYS\n' : '') + 'FILES=20\n');
  put('AUTOEXEC.BAT', `@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\nSET BLASTER=A220 I7 D1 H5 T6\n${autoexec}\n`);
  const m = {
    format: 'hd', sizeMB: 64, heads: 16, sectorsPerTrack: 63, label: 'QUAKETEST',
    date: '1996-06-22 12:00:00', boot: { src: 'build/bootsect.bin' }, files,
    dirs: [...frag.dirs, 'DOS'],
  };
  const { img } = buildImage(m, ROOT);
  const p = path.join(OUT, name + '.img');
  fs.writeFileSync(p, img);
  return p;
}

async function start(name, opts) {
  const hd = makeImage(name, opts);
  const pc = await boot({ rom: B('rom.bin'), hd, mhz: MHZ, jit: !argv.includes('--no-jit') });
  return pc;
}

const mode = (pc) => pc.machine.vga.mode;
const ivt = (pc, n) => pc.cpu.m32[n];
const bdaTicks = (pc) => pc.cpu.m32[0x46C >> 2];
const hold = (pc, code, ms) => { pc.machine.keyDown(code); pc.run(ms); pc.machine.keyUp(code); pc.run(60); };
const tap = (pc, code, after = 400) => { pc.machine.keyDown(code); pc.run(80); pc.machine.keyUp(code); pc.run(after); };
const typeKeys = (pc, text, after = 300) => {
  for (const ch of text) {
    const code = ch === ' ' ? 'Space' : ch === '\r' ? 'Enter' : /[0-9]/.test(ch) ? 'Digit' + ch : ch === '_' ? null : 'Key' + ch.toUpperCase();
    if (ch === '_') { pc.machine.keyDown('ShiftLeft'); tap(pc, 'Minus', 30); pc.machine.keyUp('ShiftLeft'); continue; }
    tap(pc, code, 30);
  }
  pc.run(after);
};
const shot = async (pc, file) => { await pc.png(path.join(OUT, file)); console.log(`     screenshot ${path.join(OUT, file)}`); };
const colours = (pc) => { const img = pc.render(); const s = new Set(); for (let i = 0; i < img.data.length; i += 4 * 7) s.add(img.data[i] << 16 | img.data[i + 1] << 8 | img.data[i + 2]); return s.size; };
const frameHash = (pc) => { const img = pc.render(); let h = 0; for (let i = 0; i < img.data.length; i += 3) h = (h * 31 + img.data[i]) | 0; return h; };
const busy = (pc, ms) => {
  const h0 = pc.machine.haltedNs, e0 = pc.machine.timeNs();
  pc.run(ms);
  return 1 - (pc.machine.haltedNs - h0) / (pc.machine.timeNs() - e0);
};
function mips(pc, t0, i0) {
  const ms = performance.now() - t0, insns = pc.cpu.icount - i0;
  return { ms, insns, mips: insns / ms / 1000 };
}
const dbgTail = (pc, n = 12) => pc.debug.split('\n').slice(-n).join('\n');

function readHostFile(pc, dosPath) {
  try {
    const fr = new FatReader(Buffer.from(pc.machine.ata.img.buffer, pc.machine.ata.img.byteOffset, pc.machine.ata.img.length));
    const ent = fr.lookup(dosPath);
    return ent ? fr.readFile(ent) : null;
  } catch (e) { return null; }
}

// Sound Blaster activity (DSP commands) and the audio it produces.
function soundProbe(pc) {
  const p = { dsp: 0, windows: [] };
  const sb = pc.machine.sb;
  if (!sb) return p;
  const sw = sb.write.bind(sb);
  sb.write = (port, v) => { if ((port & 0xF) === 0xC) p.dsp++; return sw(port, v); };
  let acc = 0, n = 0;
  pc.machine.audio.start(22050, (l, r) => {
    for (let i = 0; i < l.length; i++) {
      acc += l[i] * l[i] + r[i] * r[i]; n += 2;
      if (n >= 22050 / 5) { p.windows.push([pc.timeMs, Math.sqrt(acc / n)]); acc = 0; n = 0; }
    }
  });
  p.loudSince = (t) => Math.max(0, ...p.windows.filter(([tm]) => tm >= t).map(([, v]) => v));
  return p;
}

// ---------------------------------------------------------------- play
async function play() {
  console.log('== play');
  const pc = await start('play', { autoexec: 'MOUSE' });
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('MEM\r');
  pc.waitText('largest executable program size', { timeoutMs: 10000 });
  const free = +(pc.screen().match(/(\d+) largest executable/) || [0, 0])[1];
  const need = (() => { const e = fs.readFileSync(B('QUAKE.EXE')); const h = e.readUInt32LE(0x3C);
    return 256 + e.readUInt32LE(h + 12) + e.readUInt32LE(h + 16) + e.readUInt32LE(h + 20); })();
  check(free >= need, `conventional memory with MOUSE.COM: ${free} bytes free, QUAKE.EXE needs ${need}`);
  pc.type('CLS\r'); pc.run(300);
  const snd = soundProbe(pc);
  const old08 = ivt(pc, 8), old09 = ivt(pc, 9);
  const t0 = performance.now();
  pc.type('CD \\GAMES\\QUAKE\rQUAKE\r');
  check(pc.waitText('Quake v1.09', { timeoutMs: 10000 }), 'startup banner "Quake v1.09"');
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 30000 }), 'mode 13h set');
  check(ivt(pc, 8) !== old08 && ivt(pc, 9) !== old09, 'INT 08h and 09h hooked');
  const tk0 = bdaTicks(pc), tm0 = pc.timeMs;
  pc.run(3000);
  await shot(pc, 'startup.png');
  pc.until(() => pc.debug.includes('execing quake.rc'), { timeoutMs: 20000 });
  pc.run(4000);
  const tk1 = bdaTicks(pc), tm1 = pc.timeMs;
  const hz = (tk1 - tk0) / ((tm1 - tm0) / 1000);
  check(Math.abs(hz - 18.2) < 1, `BIOS tick at ${hz.toFixed(2)} Hz while Quake owns the PIT`);
  console.log(dbgTail(pc, 30).replace(/^/gm, '     | '));
  await shot(pc, 'demo1.png');
  check(colours(pc) > 40, `the demo is on the screen (${colours(pc)} colours)`);
  check(snd.dsp > 0, `Sound Blaster programmed (${snd.dsp} DSP writes)`);
  check(snd.loudSince(tm0) > 0.001, `sound playing (RMS ${snd.loudSince(tm0).toFixed(4)})`);

  // main menu -> single player -> new game
  tap(pc, 'Escape', 800);
  await shot(pc, 'menu.png');
  tap(pc, 'Enter', 800);           // Single Player
  await shot(pc, 'menu-sp.png');
  tap(pc, 'Enter', 200);           // New Game
  check(pc.until(() => pc.debug.includes('maps/start.bsp'), { timeoutMs: 30000 }), 'New Game loads the start map');
  pc.run(4000);
  await shot(pc, 'start.png');
  // the episode 1 slipgate is a walk away; take the console's way there
  tap(pc, 'Backquote', 600);
  typeKeys(pc, 'map e1m1\r', 200);
  check(pc.until(() => pc.debug.includes('maps/e1m1.bsp'), { timeoutMs: 30000 }), 'console "map e1m1" loads E1M1');
  pc.run(4000);
  check(pc.debug.includes('the Slipgate Complex'), 'E1M1 "the Slipgate Complex" entered');
  await shot(pc, 'e1m1.png');
  const h0 = frameHash(pc);
  hold(pc, 'ArrowUp', 1500);
  await shot(pc, 'e1m1-walk.png');
  check(frameHash(pc) !== h0, 'walking changes the view');
  hold(pc, 'ArrowLeft', 700);
  const tf = pc.timeMs;
  tap(pc, 'ControlLeft', 200);
  await shot(pc, 'e1m1-fire.png');
  pc.run(600);
  check(snd.loudSince(tf) > 0.001, `shot audible (RMS ${snd.loudSince(tf).toFixed(4)})`);
  tap(pc, 'Space', 800);           // jump
  hold(pc, 'ArrowRight', 400);
  // mouse look (MOUSE.COM, INT 33h function 0Bh)
  const hm = frameHash(pc);
  for (let i = 0; i < 10; i++) { pc.machine.mouseMove(40, 0); pc.run(50); }
  pc.run(300);
  await shot(pc, 'e1m1-mouse.png');
  check(frameHash(pc) !== hm && pc.debug.includes('mouse available'), 'the mouse turns the view');
  const b = busy(pc, 2000);
  console.log(`     CPU busy ${(b * 100).toFixed(0)}% while standing in E1M1`);
  await shot(pc, 'e1m1-turn.png');

  // the console
  tap(pc, 'Backquote', 600);
  typeKeys(pc, 'version\r', 800);
  await shot(pc, 'console.png');
  tap(pc, 'Backquote', 600);

  // quit: Esc menu -> Quit -> Y
  tap(pc, 'Escape', 600);
  for (let i = 0; i < 4; i++) tap(pc, 'ArrowDown', 150);
  tap(pc, 'Enter', 800);
  await shot(pc, 'quit-prompt.png');
  tap(pc, 'KeyY', 200);
  check(pc.until(() => mode(pc) === 3 && pc.hasText('C:\\GAMES\\QUAKE>'), { timeoutMs: 20000 }), 'back at the DOS prompt in text mode');
  await shot(pc, 'end1.png');
  console.log(pc.screen().split('\n').map((l) => '     | ' + l).join('\n'));
  check(ivt(pc, 8) === old08 && ivt(pc, 9) === old09, 'INT 08h and 09h restored');
  const cfg = readHostFile(pc, 'GAMES\\QUAKE\\ID1\\CONFIG.CFG');
  check(cfg && /bind "w" "\+forward"|bind "UPARROW" "\+forward"/.test(cfg.toString()), `ID1\\CONFIG.CFG written on quit (${cfg ? cfg.length : 0} bytes)`);
  const r = mips(pc, t0, 0);
  console.log(`     ${pc.timeMs.toFixed(0)} ms emulated in ${r.ms.toFixed(0)} ms host`);
  if (pc.faults.length) console.log('     faults:', pc.faults.slice(0, 5));
}

// ------------------------------------------------------------ timedemo
async function timedemo() {
  console.log('== timedemo');
  const pc = await start('timedemo', { autoexec: '' });
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('CD \\GAMES\\QUAKE\rQUAKE -nosound +timedemo demo1\r');
  check(pc.until(() => pc.debug.includes('Playing demo') || pc.debug.includes('demo1'), { timeoutMs: 60000 }), 'demo started');
  const t0 = performance.now(), i0 = pc.cpu.icount, e0 = pc.timeMs;
  const done = pc.until(() => /frames .* seconds .* fps/.test(pc.debug), { timeoutMs: 600000, stepMs: 100 });
  const r = mips(pc, t0, i0);
  const line = (pc.debug.match(/\d+ frames .*fps/) || [''])[0];
  check(done, `timedemo finished: ${line}`);
  console.log(`     ${MHZ || 100} MHz: ${(pc.timeMs - e0).toFixed(0)} ms emulated, ${(r.insns / 1e6).toFixed(0)}M instructions, ` +
    `host ${r.ms.toFixed(0)} ms = ${r.mips.toFixed(0)} host MIPS`);
  await shot(pc, 'timedemo-end.png');
  if (!done) console.log(dbgTail(pc, 20));
}

// ------------------------------------------------------------ fromroot
async function fromroot() {
  console.log('== fromroot');
  const pc = await start('fromroot', { autoexec: '' });
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('GAMES\\QUAKE\\QUAKE -nosound\r');
  check(pc.until(() => mode(pc) === 0x13 && pc.debug.includes('execing quake.rc'), { timeoutMs: 30000 }), 'C:\\>GAMES\\QUAKE\\QUAKE finds ID1 next to QUAKE.EXE');
  pc.run(2000);
  tap(pc, 'Escape', 500);
  for (let i = 0; i < 4; i++) tap(pc, 'ArrowDown', 150);
  tap(pc, 'Enter', 600);
  tap(pc, 'KeyY', 200);
  check(pc.until(() => mode(pc) === 3 && pc.hasText('C:\\>'), { timeoutMs: 20000 }), 'quit back to C:\\>');
}

// --------------------------------------------------------------- nomem
async function nomem() {
  console.log('== nomem');
  const pc = await start('nomem', { autoexec: '', himem: false });
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('CD \\GAMES\\QUAKE\rQUAKE -mem 20\r');
  check(pc.until(() => pc.hasText('Not enough memory'), { timeoutMs: 20000 }), 'error message for -mem 20');
  check(pc.until(() => pc.hasText('C:\\GAMES\\QUAKE>'), { timeoutMs: 5000 }), 'back at the prompt');
  console.log(pc.screen().split('\n').filter((l) => l.trim()).map((l) => '     | ' + l).join('\n'));
}

const all = { play, timedemo, fromroot, nomem };
for (const n of (which.length ? which : ['play', 'timedemo'])) await all[n]();
console.log(failures ? `${failures} FAILED` : 'all passed');
process.exit(failures ? 1 : 0);
