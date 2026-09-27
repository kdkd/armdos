#!/usr/bin/env node
// apps/wolf3d/tests/run.mjs - boot ARM-DOS headless and play Wolfenstein 3D.
//
//   node apps/wolf3d/tests/run.mjs [play] [fps] [fromroot] [--out DIR] [--mhz N] [--no-jit]
//
// Builds its own hard disk image (IO.SYS, ARMDOS.SYS, CONFIG.SYS whose SHELL=
// is the kernel's test shell, C:\GAMES\WOLF3D\ as apps/wolf3d/hd.json has
// it), boots it and drives the game with real scan codes through the 8042:
//
//   play      signon -> PG-13 -> title (AdLib music) -> main menu -> New
//             Game, episode 1, "Bring 'em on" -> E1M1: walk, open a door
//             (Space; Sound Blaster door sound), fire (Ctrl; digitized
//             pistol), knife (1), pistol (2), turn, strafe (Alt), run (Shift)
//             -> F2 Save Game (slot 1, typed name) -> F10 quit -> ORDERSCREEN
//             text page. Then WOLF3D again: Load Game finds the save on C:.
//             Checks mode 13h, INT 08h/09h hooked and restored, the BIOS
//             tick at 18.2 Hz while the PIT runs at 700 Hz, OPL writes and
//             SB DMA sounds (audio captured to play.wav), SAVEGAM0.WL1 +
//             CONFIG.WL1 written to C:, text mode and PIT restored on exit.
//   fps       "WOLF3D FPS" (frame counter on port E9h) while walking in E1M1:
//             frames per second (capped at 70 like the original) and the
//             share of emulated time the CPU is busy (the rest is WFI).
//   speaker   WOLF3D NOAL NOSB: the PC speaker sound effects (PIT channel 2).
//   fromroot  C:\>GAMES\WOLF3D\WOLF3D.EXE finds its data from another directory.
//   nodata    no WL1 files: WOLF3D's error message in text mode, exit code 1.
//
// Screenshots go to build/wolf3d-test/.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const B = (p) => path.join(ROOT, 'build', p);

const argv = process.argv.slice(2);
const opt = (name, def) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : def; };
const OUT = path.resolve(opt('--out', B('wolf3d-test')));
const MHZ = +opt('--mhz', 0) || undefined;
const which = argv.filter((a, i) => !a.startsWith('--') && !(i > 0 && argv[i - 1].startsWith('--')));
fs.mkdirSync(OUT, { recursive: true });

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; };

// ------------------------------------------------------------ the disk
function makeImage(name, script, { nodata = false } = {}) {
  const dir = path.join(OUT, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const frag = JSON.parse(fs.readFileSync(path.join(ROOT, 'apps/wolf3d/hd.json'), 'utf8'));
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    ...frag.files.filter((f) => !nodata || f.src.endsWith('.EXE')),
  ];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, text.replace(/\n/g, '\r\n'));
    files.push({ src: path.relative(ROOT, host), dst });
  };
  if (!fs.existsSync(B('ktest/TSHELL.EXE'))) throw new Error('build/ktest/TSHELL.EXE missing (make kernel-test)');
  files.push({ src: 'build/ktest/TSHELL.EXE', dst: 'T\\TSHELL.EXE' });
  put('CONFIG.SYS', `FILES=20\nSHELL=C:\\T\\TSHELL.EXE C:\\T\\S.TXT\n`);
  put('T\\S.TXT', script);
  const m = {
    format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'WOLFTEST',
    date: '1993-01-01 13:40:00', boot: { src: 'build/bootsect.bin' }, files,
    dirs: [...frag.dirs, 'T'],
  };
  const { img } = buildImage(m, ROOT);
  const p = path.join(OUT, name + '.img');
  fs.writeFileSync(p, img);
  return p;
}

async function start(name, script, opts) {
  const hd = makeImage(name, script, opts);
  const speaker = [];
  const pc = await boot({ rom: B('rom.bin'), hd, mhz: MHZ, jit: !argv.includes('--no-jit'),
    onSpeaker: (on, hz) => speaker.push([on, hz]) });
  pc.speaker = speaker;
  return pc;
}

const mode = (pc) => pc.machine.vga.mode;
const ivt = (pc, n) => pc.cpu.m32[n];
const bda = (pc) => pc.cpu.m32[0x46C >> 2];
const hold = (pc, code, ms) => { pc.machine.keyDown(code); pc.run(ms); pc.machine.keyUp(code); pc.run(60); };
const tap = (pc, code, after = 400) => { pc.machine.keyDown(code); pc.run(80); pc.machine.keyUp(code); pc.run(after); };
const shot = async (pc, file) => { await pc.png(path.join(OUT, file)); console.log(`     screenshot ${path.join(OUT, file)}`); };
const busy = (pc, ms) => {
  const h0 = pc.machine.haltedNs, e0 = pc.machine.timeNs();
  pc.run(ms);
  return 1 - (pc.machine.haltedNs - h0) / (pc.machine.timeNs() - e0);
};
// the palette-mapped frame buffer: how many distinct colours are on screen
const colours = (pc) => { const img = pc.render(); const s = new Set(); for (let i = 0; i < img.data.length; i += 4 * 7) s.add(img.data[i] << 16 | img.data[i + 1] << 8 | img.data[i + 2]); return s.size; };
const frameHash = (pc) => { const img = pc.render(); let h = 0; for (let i = 0; i < img.data.length; i += 3) h = (h * 31 + img.data[i]) | 0; return h; };

function readHostFile(pc, dosPath) {
  try {
    const fr = new FatReader(Buffer.from(pc.machine.ata.img.buffer, pc.machine.ata.img.byteOffset, pc.machine.ata.img.length));
    const ent = fr.lookup(dosPath);
    return ent ? fr.readFile(ent) : null;
  } catch (e) { return null; }
}

// Count what the game does with the sound hardware, and keep the audio.
function soundProbe(pc) {
  const p = { opl: 0, oplTimes: [], dsp: 0, dspCmds: new Map(), mixer4: [] };
  const sb = pc.machine.sb, opl = sb.opl;
  const ow = opl.write.bind(opl);
  opl.write = (off, v) => { if (off & 1) { p.opl++; p.oplTimes.push(pc.timeMs); } return ow(off, v); };
  const sw = sb.write.bind(sb);
  let mixIdx = 0;
  sb.write = (port, v) => {
    const off = port & 0xF;
    if (off === 0xC) { p.dsp++; p.dspCmds.set(v, (p.dspCmds.get(v) || 0) + 1); }
    if (off === 4) mixIdx = v;
    if (off === 5 && mixIdx === 4) p.mixer4.push(v);
    return sw(port, v);
  };
  // audio output: RMS per 100 ms window, and a WAV of the session
  p.rate = 22050; p.chunks = []; p.windows = [];
  let acc = 0, n = 0;
  pc.machine.audio.start(p.rate, (l, r) => {
    const buf = new Int16Array(l.length * 2);
    for (let i = 0; i < l.length; i++) {
      buf[2 * i] = Math.max(-1, Math.min(1, l[i])) * 32767;
      buf[2 * i + 1] = Math.max(-1, Math.min(1, r[i])) * 32767;
      acc += l[i] * l[i] + r[i] * r[i]; n += 2;
      if (n >= p.rate / 10) { p.windows.push([pc.timeMs, Math.sqrt(acc / n)]); acc = 0; n = 0; }
    }
    p.chunks.push(buf);
  });
  p.loudSince = (t) => Math.max(0, ...p.windows.filter(([tm]) => tm >= t).map(([, v]) => v));
  p.saveWav = (file) => {
    const data = Buffer.concat(p.chunks.map((c) => Buffer.from(c.buffer)));
    const h = Buffer.alloc(44);
    h.write('RIFF', 0); h.writeUInt32LE(36 + data.length, 4); h.write('WAVEfmt ', 8);
    h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(2, 22); h.writeUInt32LE(p.rate, 24);
    h.writeUInt32LE(p.rate * 4, 28); h.writeUInt16LE(4, 32); h.writeUInt16LE(16, 34); h.write('data', 36); h.writeUInt32LE(data.length, 40);
    fs.writeFileSync(file, Buffer.concat([h, data]));
    console.log(`     audio ${file} (${(data.length / 4 / p.rate).toFixed(1)} s)`);
  };
  return p;
}

// -------------------------------------------------------------- play
async function play() {
  console.log('== play');
  const pc = await start('play', 'mem\ncd \\GAMES\\WOLF3D\nWOLF3D\nmem\nWOLF3D\nmem\nhalt\n');
  const snd = soundProbe(pc);
  pc.until(() => pc.serial.includes('T:MEM'), { timeoutMs: 30000 });
  const vec0 = [ivt(pc, 8), ivt(pc, 9)];
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 60000, stepMs: 50 }), 'WOLF3D set mode 13h');
  if (mode(pc) !== 0x13) { console.log(pc.screen()); console.log(pc.serial); console.log(pc.debug); return; }
  pc.run(600);
  await shot(pc, 'signon.png');
  pc.run(3500);
  check(ivt(pc, 8) !== vec0[0] && ivt(pc, 9) !== vec0[1], 'INT 08h and INT 09h hooked while the game runs');
  await shot(pc, 'signon-ready.png');

  // BIOS tick keeps running at 18.2 Hz though the PIT now runs at 700 Hz
  const tk0 = bda(pc), tm0 = pc.timeMs;
  pc.run(3000);
  const hz = (bda(pc) - tk0) / ((pc.timeMs - tm0) / 1000);
  check(Math.abs(hz - 18.2065) < 0.5, `BIOS tick at ${hz.toFixed(2)} Hz while the game owns the PIT`);

  tap(pc, 'Space', 1500);                // signon -> PG-13
  await shot(pc, 'pg13.png');
  const o0 = snd.opl, tmusic = pc.timeMs;
  tap(pc, 'Space', 2500);                // -> title
  await shot(pc, 'title.png');
  check(snd.opl - o0 > 50 && snd.loudSince(tmusic) > 0.001, `AdLib music on the title (${snd.opl - o0} OPL writes, level ${snd.loudSince(tmusic).toFixed(3)})`);
  tap(pc, 'Space', 1500);                // -> main menu (on "Read This!" without a config)
  await shot(pc, 'menu.png');
  for (let i = 0; i < 5; i++) tap(pc, 'ArrowUp', 250);  // -> New Game
  await shot(pc, 'menu-newgame.png');
  tap(pc, 'ArrowDown', 250);             // Sound
  tap(pc, 'Enter', 800);
  await shot(pc, 'sound-menu.png');
  tap(pc, 'Escape', 800);
  tap(pc, 'ArrowUp', 250);               // New Game

  tap(pc, 'Enter', 800);                 // New Game -> episode
  await shot(pc, 'episode.png');
  tap(pc, 'Enter', 800);                 // Escape from Wolfenstein -> skill
  await shot(pc, 'skill.png');
  tap(pc, 'Enter', 3500);                // Bring 'em on -> Get Psyched -> E1M1
  await shot(pc, 'e1m1-start.png');

  hold(pc, 'ArrowUp', 700);              // walk towards the door ahead
  await shot(pc, 'e1m1-walk.png');
  const d0 = snd.dsp;
  tap(pc, 'Space', 1500);                // open it
  await shot(pc, 'e1m1-door.png');
  check(snd.dsp > d0, `door sound on the Sound Blaster (${snd.dsp - d0} DSP writes)`);
  hold(pc, 'ArrowUp', 900);              // through the door
  const d1 = snd.dspCmds.get(0xC6) || 0, tfire = pc.timeMs;
  pc.machine.keyDown('ControlLeft'); pc.run(250);
  await shot(pc, 'e1m1-fire.png');       // pistol shot
  pc.machine.keyUp('ControlLeft'); pc.run(400);
  check((snd.dspCmds.get(0xC6) || 0) > d1 && snd.loudSince(tfire) > 0.002, `pistol shot digitized on the SB (DMA sound started, level ${snd.loudSince(tfire).toFixed(3)})`);
  tap(pc, 'Digit1', 300);                // knife
  pc.machine.keyDown('ControlLeft'); pc.run(200);
  await shot(pc, 'e1m1-knife.png');
  pc.machine.keyUp('ControlLeft'); pc.run(300);
  tap(pc, 'Digit2', 300);                // pistol again
  hold(pc, 'ArrowLeft', 500);            // turn
  pc.machine.keyDown('AltLeft'); hold(pc, 'ArrowRight', 400); pc.machine.keyUp('AltLeft'); pc.run(100); // strafe
  pc.machine.keyDown('ShiftLeft'); hold(pc, 'ArrowDown', 400); pc.machine.keyUp('ShiftLeft'); pc.run(100); // run back
  await shot(pc, 'e1m1-move.png');

  // fps while walking (70 fps cap, rest in WFI)
  pc.machine.keyDown('ArrowUp');
  const b = busy(pc, 1500);
  pc.machine.keyUp('ArrowUp'); pc.run(100);
  console.log(`     walking: CPU busy ${(b * 100).toFixed(1)}% of emulated time`);

  // save the game: F2 -> save menu, slot 1, a name, Enter
  tap(pc, 'F2', 1200);
  await shot(pc, 'save-menu.png');
  tap(pc, 'Enter', 500);                 // slot 1
  for (const k of ['KeyA', 'KeyR', 'KeyM', 'Space', 'KeyD', 'KeyO', 'KeyS']) tap(pc, k, 120);
  await shot(pc, 'save-name.png');
  tap(pc, 'Enter', 1500);
  await shot(pc, 'saved.png');
  pc.run(500);
  // F10: quit to DOS
  tap(pc, 'F10', 800);
  await shot(pc, 'quit-prompt.png');
  pc.machine.keyDown('KeyY'); pc.run(80); pc.machine.keyUp('KeyY');
  pc.until(() => pc.serial.includes('T:EXIT WOLF3D'), { timeoutMs: 20000, stepMs: 2 });
  check(mode(pc) === 0x03, 'back in text mode after quit');
  await shot(pc, 'orderscreen.png');
  const scr = pc.screen();
  console.log(scr);
  check(/Apogee|order|ORDER/i.test(scr), 'ORDERSCREEN text page on screen');
  check(ivt(pc, 8) === vec0[0] && ivt(pc, 9) === vec0[1], 'INT 08h and INT 09h restored on exit');
  check(!pc.speaker.length || !pc.speaker[pc.speaker.length - 1][0], 'speaker silent after exit');
  check(pc.machine.pit.ch[0].reload === 0x10000, `PIT channel 0 back to divisor 65536 (18.2 Hz) after exit (${pc.machine.pit.ch[0].reload})`);

  const save = readHostFile(pc, 'GAMES\\WOLF3D\\SAVEGAM0.WL1');
  check(save && save.length > 1000 && save.subarray(0, 8).toString('latin1').toLowerCase() === 'arm dos\0', `SAVEGAM0.WL1 on C: (${save ? save.length : 0} bytes, "${save ? save.subarray(0, 32).toString('latin1').replace(/\0.*/, '') : ''}")`);
  const cfg = readHostFile(pc, 'GAMES\\WOLF3D\\CONFIG.WL1');
  check(cfg && cfg.length > 100, `CONFIG.WL1 on C: (${cfg ? cfg.length : 0} bytes)`);

  // second run: load the saved game from the disk
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 60000, stepMs: 20 }), 'WOLF3D started again');
  pc.run(4000);
  tap(pc, 'Space', 1500); tap(pc, 'Space', 2500); tap(pc, 'Space', 1500);
  await shot(pc, 'menu2.png');
  tap(pc, 'ArrowDown', 250); tap(pc, 'ArrowDown', 250); tap(pc, 'ArrowDown', 250);  // Load Game
  tap(pc, 'Enter', 800);
  await shot(pc, 'load-menu.png');
  tap(pc, 'Enter', 3000);                // slot 1
  await shot(pc, 'loaded.png');
  tap(pc, 'F10', 800);
  tap(pc, 'KeyY', 3000);
  check(pc.until(() => mode(pc) === 0x03 && /T:MEM[^]*T:MEM/.test(pc.serial), { timeoutMs: 20000 }), 'second run quit to text mode');
  snd.saveWav(path.join(OUT, 'play.wav'));
  console.log(`     DSP commands: ${[...snd.dspCmds].map(([c, n]) => `${c.toString(16)}h x${n}`).join(', ')}; mixer voice volume writes: ${snd.mixer4.length}`);

  console.log(`     ${pc.timeMs.toFixed(0)} ms emulated, ${(pc.cpu.icount / 1e6).toFixed(0)}M instructions`);
  console.log('     serial:\n' + pc.serial.split('\n').map((l) => '       ' + l).join('\n'));
  if (pc.debug) console.log('     debug port:\n' + pc.debug);
  if (pc.faults.length) console.log('     faults:', pc.faults);
}

// ------------------------------------------ PC speaker (no AdLib, no SB)
async function speaker() {
  console.log('== speaker');
  const pc = await start('speaker', 'cd \\GAMES\\WOLF3D\nWOLF3D NOAL NOSB NOWAIT TEDLEVEL 0\nhalt\n');
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 60000, stepMs: 50 }), 'WOLF3D NOAL NOSB set mode 13h');
  pc.run(3000);
  const t0 = pc.speaker.length;
  pc.machine.keyDown('ControlLeft'); pc.run(1200); pc.machine.keyUp('ControlLeft');
  hold(pc, 'ArrowUp', 600);
  tap(pc, 'Space', 1500);
  await shot(pc, 'speaker-e1m1.png');
  const tones = pc.speaker.slice(t0).filter(([on]) => on);
  check(tones.length > 5, `PC speaker played ${tones.length} tones (e.g. ${[...new Set(tones.slice(0, 8).map(([, f]) => Math.round(f)))].join(', ')} Hz)`);
  tap(pc, 'F10', 800);
  tap(pc, 'KeyY', 3000);
  check(pc.until(() => mode(pc) === 0x03, { timeoutMs: 20000 }), 'quit to text mode');
  check(!pc.speaker.length || !pc.speaker[pc.speaker.length - 1][0], 'speaker silent after exit');
}

// ---------------------------------------------------------------- fps
async function fps() {
  console.log('== fps');
  const pc = await start('fps', 'cd \\GAMES\\WOLF3D\nWOLF3D NOWAIT FPS TEDLEVEL 0\nhalt\n');
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 60000, stepMs: 50 }), 'WOLF3D set mode 13h');
  pc.until(() => pc.debug.includes('fps '), { timeoutMs: 30000, stepMs: 50 });
  pc.run(1500);
  await shot(pc, 'fps-e1m1.png');
  pc.debug = '';
  const t0 = performance.now(), i0 = pc.cpu.icount;
  pc.machine.keyDown('ArrowLeft');
  const b = busy(pc, 5000);
  pc.machine.keyUp('ArrowLeft');
  const ms = performance.now() - t0;
  const vals = [...pc.debug.matchAll(/fps (\d+)/g)].map((m) => +m[1]);
  const avg = vals.reduce((a, v) => a + v, 0) / (vals.length || 1);
  console.log(`     frames/s while turning (E1M1): ${vals.join(' ')}`);
  console.log(`     CPU busy ${(b * 100).toFixed(1)}% -> ~${Math.round(avg / b)} fps of headroom uncapped`);
  console.log(`     host: ${((pc.cpu.icount - i0) / ms / 1000).toFixed(0)} MIPS, ${(5000 / ms).toFixed(2)}x real time`);
  check(vals.length >= 3 && avg >= 69, `70 fps (average ${avg.toFixed(1)})`);
  await shot(pc, 'fps-turned.png');
}

// ---------------------------- started from C:\ (data next to WOLF3D.EXE)
async function fromroot() {
  console.log('== fromroot');
  const pc = await start('fromroot', 'C:\\GAMES\\WOLF3D\\WOLF3D.EXE\nhalt\n');
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 60000, stepMs: 50 }), 'C:\\> GAMES\\WOLF3D\\WOLF3D finds its data next to WOLF3D.EXE');
  pc.run(4000);
  await shot(pc, 'fromroot.png');
  if (pc.debug) console.log('     debug port:\n' + pc.debug);
}

// ------------------------------------------------ palette fades are idle
// VL_FadeOut/VL_FadeIn wait for the vertical retrace once per step; those
// waits must sleep in WFI (not spin on port 3DAh), and the SB / AdLib timer
// work must go on during them: a door sound started just before Esc (fade to
// the menu) plays without gaps and the music keeps going.
async function fades() {
  for (const noal of [false, true]) {
    console.log(`== fades${noal ? ' (NOAL: Sound Blaster only)' : ''}`);
    const pc = await start('fades', `cd \\GAMES\\WOLF3D\nWOLF3D NOWAIT TEDLEVEL 0${noal ? ' NOAL' : ''}\nhalt\n`);
    const snd = soundProbe(pc);
    check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 60000, stepMs: 50 }), 'WOLF3D set mode 13h');
    pc.run(3000);
    hold(pc, 'ArrowUp', 700);              // to the door
    const measure = (label, keys, ms) => {
      const h0 = pc.machine.haltedNs, e0 = pc.machine.timeNs(), i0 = pc.cpu.icount, t0 = performance.now(), o0 = snd.opl, w0 = snd.windows.length, tm0 = pc.timeMs;
      keys();
      pc.run(ms);
      const busyf = 1 - (pc.machine.haltedNs - h0) / (pc.machine.timeNs() - e0);
      const hostms = performance.now() - t0;
      const ot = snd.oplTimes.filter((t) => t >= tm0);
      let gap = 0; for (let k = 1; k < ot.length; k++) gap = Math.max(gap, ot[k] - ot[k - 1]);
      console.log(`     ${label}: CPU busy ${(busyf * 100).toFixed(1)}% of ${ms} ms emulated, ${((pc.cpu.icount - i0) / 1e6).toFixed(1)}M instructions, ${hostms.toFixed(0)} ms host` +
        (noal ? '' : `, ${snd.opl - o0} OPL writes (longest pause ${gap.toFixed(0)} ms)`));
      return { busy: busyf, windows: snd.windows.slice(w0), opl: snd.opl - o0, gap };
    };
    const press = (k) => { pc.machine.keyDown(k); pc.run(60); pc.machine.keyUp(k); };
    if (noal) {
      // the door sound (1423 ms at 7042 Hz) plays whole and unbroken
      const c0 = snd.dspCmds.get(0xC6) || 0;
      const d = measure('door opening (digitized)', () => tap(pc, 'Space', 30), 2000);
      const loud = d.windows.map(([, v]) => v > 0.001);
      const first = loud.indexOf(true), last = loud.lastIndexOf(true);
      const gaps = first < 0 ? -1 : loud.slice(first, last + 1).filter((x) => !x).length;
      check((snd.dspCmds.get(0xC6) || 0) > c0 && first >= 0 && gaps === 0 && (last - first + 1) * 50 >= 1300,
        `door sound on the SB, unbroken (${first < 0 ? 'no sound' : `${(last - first + 1) * 50} ms, ${gaps} silent 50 ms windows inside`})`);
      pc.run(1500);                        // the door closes again
    }
    const base = measure('in game, standing', () => {}, 1000);
    const out = measure('Esc: fade out to the menu', () => press('Escape'), 1200);
    await shot(pc, `fade-menu${noal ? '-noal' : ''}.png`);
    // "Back to Game" (two below "Read This!"): fade back into the game
    tap(pc, 'ArrowDown', 200); tap(pc, 'ArrowDown', 200);
    const inn = measure('Back to Game: fade into the game', () => press('Enter'), 1200);
    await shot(pc, `fade-game${noal ? '-noal' : ''}.png`);
    check(out.busy < 0.3 && inn.busy < 0.3, `fades sleep in WFI (busy ${(out.busy * 100).toFixed(0)}% / ${(inn.busy * 100).toFixed(0)}%)`);
    if (!noal)
      check(inn.opl > 50 && inn.gap < 300, `AdLib music (700 Hz timer) keeps going through the fade (${inn.opl} OPL writes, longest pause ${inn.gap.toFixed(0)} ms)`);
    snd.saveWav(path.join(OUT, `fades${noal ? '-noal' : ''}.wav`));
  }
}

// ------------------------------------ no data files: a clean text-mode error
async function nodata() {
  console.log('== nodata');
  const pc = await start('nodata', 'cd \\GAMES\\WOLF3D\nWOLF3D\nhalt\n', { nodata: true });
  pc.until(() => pc.serial.includes('T:EXIT WOLF3D'), { timeoutMs: 30000, stepMs: 20 });
  console.log(pc.screen().split('\n').filter((l) => l.trim()).slice(-4).join('\n'));
  check(/T:EXIT WOLF3D 1/.test(pc.serial) && pc.hasText('NO WOLFENSTEIN 3-D DATA FILES to be found!') && mode(pc) === 0x03,
    'without the WL1 files: "NO WOLFENSTEIN 3-D DATA FILES to be found!", exit code 1, text mode');
}

const all = { play, speaker, fps, fades, fromroot, nodata };
for (const n of (which.length ? which : ['play', 'speaker', 'fps', 'fades', 'fromroot', 'nodata'])) await all[n]();
console.log(failures ? `${failures} FAILED` : 'all ok');
process.exit(failures ? 1 : 0);
