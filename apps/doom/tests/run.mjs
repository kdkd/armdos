#!/usr/bin/env node
// apps/doom/tests/run.mjs - boot ARM-DOS headless and run DOOM.EXE.
//
//   node apps/doom/tests/run.mjs [play] [timedemo] [--out DIR] [--mhz N]
//
// Builds its own hard disk image (IO.SYS, ARMDOS.SYS, CONFIG.SYS, the
// kernel's test shell or COMMAND.COM, C:\GAMES\DOOM\DOOM.EXE + DOOM1.WAD),
// boots it and drives DOOM with real scan codes through the 8042:
//
//   play      title screen -> Enter x4 (new game, E1, skill) -> walk (Up),
//             turn, fire (Ctrl), open a door (Space) -> F10, Y -> ENDOOM.
//             Screenshots: title.png, menu.png, game*.png, endoom.png.
//   timedemo  DOOM -timedemo demo1: fps in emulated time (the machine's
//             100 MHz) plus host MIPS.
//
// Needs build/rom.bin, build/IO.SYS, build/ARMDOS.SYS, build/bootsect.bin,
// build/DOOM.EXE and a shell (build/ktest/TSHELL.EXE from "make kernel-test"
// or build/COMMAND.COM).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage } from '../../../disk/mkimage.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const B = (p) => path.join(ROOT, 'build', p);

const argv = process.argv.slice(2);
const opt = (name, def) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : def; };
const OUT = path.resolve(opt('--out', B('doom-test')));
const MHZ = +opt('--mhz', 0) || undefined;
const EXTRA = opt('--args', '');
const which = argv.filter((a, i) => !a.startsWith('--') && !(i > 0 && argv[i - 1].startsWith('--')));
fs.mkdirSync(OUT, { recursive: true });

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; };

// ------------------------------------------------------------ the disk
function makeImage(name, doomArgs, script) {
  const dir = path.join(OUT, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/DOOM.EXE', dst: 'GAMES\\DOOM\\' },
    { src: '3rdparty/doom/DOOM1.WAD', dst: 'GAMES\\DOOM\\' },
  ];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, text.replace(/\n/g, '\r\n'));
    files.push({ src: path.relative(ROOT, host), dst });
  };
  if (fs.existsSync(B('HIMEM.SYS'))) files.push({ src: 'build/HIMEM.SYS', dst: 'DOS\\HIMEM.SYS' });
  const himem = fs.existsSync(B('HIMEM.SYS')) ? 'DEVICE=C:\\DOS\\HIMEM.SYS\n' : '';
  // The kernel's test shell runs a script: MEM, then DOOM, then halts with
  // whatever DOOM left on the screen (ENDOOM). Its log goes to COM1.
  if (!fs.existsSync(B('ktest/TSHELL.EXE'))) throw new Error('build/ktest/TSHELL.EXE missing (make kernel-test)');
  files.push({ src: 'build/ktest/TSHELL.EXE', dst: 'T\\TSHELL.EXE' });
  put('CONFIG.SYS', `FILES=20\n${himem}SHELL=C:\\T\\TSHELL.EXE C:\\T\\S.TXT\n`);
  if (fs.existsSync(B('doom-test/WADCHECK.EXE'))) files.push({ src: 'build/doom-test/WADCHECK.EXE', dst: 'T\\' });
  put('T\\S.TXT', script ?? `mem\ncd \\GAMES\\DOOM\nC:\\GAMES\\DOOM\\DOOM.EXE ${doomArgs}\nmem\nhalt\n`);
  const m = {
    format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'DOOMTEST',
    date: '1993-12-10 12:00:00', boot: { src: 'build/bootsect.bin' }, files,
    dirs: ['GAMES', 'GAMES\\DOOM', 'DOS', 'T'],
  };
  const { img } = buildImage(m, ROOT);
  const p = path.join(OUT, name + '.img');
  fs.writeFileSync(p, img);
  return p;
}

async function start(name, doomArgs, script) {
  const hd = makeImage(name, doomArgs, script);
  const speaker = [];
  const pc = await boot({ rom: B('rom.bin'), hd, mhz: MHZ, jit: !argv.includes('--no-jit'),
    onSpeaker: (on, hz) => speaker.push([on, hz]) });
  pc.speaker = speaker;
  return pc;
}

const mode = (pc) => pc.machine.vga.mode;
const hold = (pc, code, ms) => { pc.machine.keyDown(code); pc.run(ms); pc.machine.keyUp(code); pc.run(60); };
const tap = (pc, code, after = 400) => { pc.machine.keyDown(code); pc.run(80); pc.machine.keyUp(code); pc.run(after); };
const shot = async (pc, file) => { await pc.png(path.join(OUT, file)); console.log(`     screenshot ${path.join(OUT, file)}`); };

function mips(pc, t0, i0) {
  const ms = performance.now() - t0, insns = pc.cpu.icount - i0;
  return { ms, insns, mips: insns / ms / 1000 };
}

// -------------------------------------------------------------- play
async function play() {
  console.log('== play');
  const pc = await start('play', EXTRA);
  const t0 = performance.now();
  const ivt = (n) => pc.cpu.m32[n];
  pc.until(() => pc.serial.includes('T:MEM'), { timeoutMs: 30000 });
  const vec0 = [ivt(8), ivt(9)];
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 60000, stepMs: 50 }), 'DOOM set mode 13h');
  check(ivt(8) !== vec0[0] && ivt(9) !== vec0[1], 'INT 08h and INT 09h hooked while DOOM runs');
  if (mode(pc) !== 0x13) { console.log(pc.screen()); console.log(pc.serial); console.log(pc.debug); return; }
  console.log(`     mode 13h at ${pc.timeMs.toFixed(0)} ms emulated; serial log:\n` + pc.serial.split('\n').map((l) => '       ' + l).join('\n'));
  pc.run(3000);
  await shot(pc, 'title.png');

  // the BIOS tick keeps running at 18.2 Hz (chained from DOOM's 140 Hz INT 08h)
  const bda = () => pc.cpu.m32[0x46C >> 2];
  const tk0 = bda(), tm0 = pc.timeMs;
  pc.run(5000);
  const hz = (bda() - tk0) / ((pc.timeMs - tm0) / 1000);
  check(Math.abs(hz - 18.2065) < 0.5, `BIOS tick at ${hz.toFixed(2)} Hz while DOOM owns the PIT`);
  tap(pc, 'Enter', 600);                 // main menu
  await shot(pc, 'menu.png');
  tap(pc, 'Enter', 600);                 // New Game -> episode
  tap(pc, 'Enter', 600);                 // Knee-Deep in the Dead -> skill
  tap(pc, 'Enter', 2500);                // Hurt me plenty -> E1M1 (after the wipe)
  await shot(pc, 'game1.png');

  hold(pc, 'ArrowUp', 1200);             // walk forward
  hold(pc, 'ArrowRight', 350);           // turn
  await shot(pc, 'game2.png');
  hold(pc, 'ControlLeft', 700);          // fire the pistol
  pc.run(100);
  pc.machine.keyDown('ControlLeft'); pc.run(150);
  await shot(pc, 'game3-fire.png');
  pc.machine.keyUp('ControlLeft'); pc.run(300);
  hold(pc, 'ArrowLeft', 350);
  hold(pc, 'ArrowUp', 900);
  tap(pc, 'Space', 800);                 // use
  tap(pc, 'Tab', 400);                   // automap
  await shot(pc, 'game4-automap.png');
  tap(pc, 'Tab', 300);

  // Measure the frame rate while playing: DOOM draws at most one frame per
  // 35 Hz tic; count mode 13h frame-buffer copies via the emulated time the
  // CPU is busy vs halted.
  const h0 = pc.machine.haltedNs, e0 = pc.machine.timeNs();
  pc.machine.keyDown('ArrowUp'); pc.run(2000); pc.machine.keyUp('ArrowUp');
  const busy = 1 - (pc.machine.haltedNs - h0) / (pc.machine.timeNs() - e0);
  console.log(`     while walking: CPU busy ${(busy * 100).toFixed(1)}% of emulated time (rest in WFI)`);
  await shot(pc, 'game5.png');

  tap(pc, 'F10', 600);                   // quit?
  await shot(pc, 'quit-prompt.png');
  tap(pc, 'KeyY', 3000);
  check(pc.until(() => mode(pc) === 0x03 && pc.serial.includes('T:EXIT'), { timeoutMs: 20000 }), 'back in text mode after quit');
  check(ivt(8) === vec0[0] && ivt(9) === vec0[1], 'INT 08h and INT 09h restored on exit');
  const tones = pc.speaker.filter(([on]) => on);
  check(tones.length > 20, `PC speaker played ${tones.length} tones (e.g. ${[...new Set(tones.slice(0, 8).map(([, f]) => Math.round(f)))].join(', ')} Hz)`);
  check(!pc.speaker.length || !pc.speaker[pc.speaker.length - 1][0], 'speaker silent after exit');
  const t1 = bda(); pc.run(1000);
  check(Math.abs((bda() - t1) - 18.2) < 2, 'BIOS 18.2 Hz tick restored after exit');
  pc.run(500);
  await shot(pc, 'endoom.png');
  const scr = pc.screen();
  console.log(scr);
  check(/DOOM|id Software|Doom|shareware/i.test(scr), 'ENDOOM text on screen');
  const r = mips(pc, t0, 0);
  console.log(`     ${pc.timeMs.toFixed(0)} ms emulated in ${r.ms.toFixed(0)} ms host, ${(pc.cpu.icount / 1e6).toFixed(0)}M instructions`);
  console.log('     serial:\n' + pc.serial.split('\n').map((l) => '       ' + l).join('\n'));
  if (pc.debug) console.log('     debug port:\n' + pc.debug);
  if (pc.faults.length) console.log('     faults:', pc.faults);
}

// ---------------------------------------------------------- timedemo
async function timedemo() {
  console.log('== timedemo');
  const pc = await start('timedemo', `-timedemo demo1 ${EXTRA}`);
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 60000, stepMs: 50 }), 'DOOM set mode 13h');
  const t0 = performance.now(), i0 = pc.cpu.icount, e0 = pc.timeMs;
  const done = pc.until(() => pc.debug.includes('timed ') || pc.serial.includes('T:EXIT'), { timeoutMs: 600000, stepMs: 100 });
  const r = mips(pc, t0, i0);
  check(done && pc.debug.includes('timed '), 'timedemo finished');
  console.log(`     ${pc.debug.trim()}`);
  console.log(`     demo: ${(pc.timeMs - e0).toFixed(0)} ms emulated, ${(r.insns / 1e6).toFixed(0)}M instructions, ` +
    `host ${r.ms.toFixed(0)} ms = ${r.mips.toFixed(0)} host MIPS (${((pc.timeMs - e0) / r.ms).toFixed(2)}x real time)`);
  pc.run(300);
  await shot(pc, 'timedemo-end.png');
  if (pc.faults.length) console.log('     faults:', pc.faults);
}

// ------------------------------------------------ file system check
async function wadcheck() {
  console.log('== wadcheck');
  const pc = await start('wadcheck', '', 'cd \\GAMES\\DOOM\nC:\\T\\WADCHECK.EXE DOOM1.WAD\nhalt\n');
  pc.waitSerial('T:EXIT', { timeoutMs: 120000, stepMs: 100 });
  console.log(pc.screen());
  check(pc.hasText('WADCHECK ok'), 'DOOM1.WAD reads back identically through fseek/fread');
  // Compare the sequential read (left in guest memory) with the host file.
  const mm = /buf (?:0x)?([0-9a-f]+)/i.exec(pc.screen());
  if (mm) {
    const base = parseInt(mm[1], 16), wad = fs.readFileSync(path.join(ROOT, '3rdparty/doom/DOOM1.WAD'));
    const m8 = pc.cpu.m8;
    let runs = [], inBad = -1;
    for (let i = 0; i <= wad.length; i++) {
      const bad = i < wad.length && m8[base + i] !== wad[i];
      if (bad && inBad < 0) inBad = i;
      if (!bad && inBad >= 0) { runs.push([inBad, i]); inBad = -1; }
    }
    console.log(`     sequential read: ${runs.length} bad ranges` + runs.slice(0, 12).map(([a, b]) => ` [0x${a.toString(16)}-0x${b.toString(16)})`).join(''));
    if (runs.length) {
      const a = runs[0][0];
      const find = (off) => { const pat = m8.subarray(base + off, base + off + 32); const j = wad.indexOf(Buffer.from(pat)); return j; };
      for (const [x] of runs.slice(0, 10)) console.log(`     at 0x${x.toString(16)}: got ${m8[base + x].toString(16)} want ${wad[x].toString(16)}`);
    }
  }
}

// ---------------------------- started from C:\ (IWAD next to DOOM.EXE)
async function fromroot() {
  console.log('== fromroot');
  const pc = await start('fromroot', '', 'C:\\GAMES\\DOOM\\DOOM.EXE\nhalt\n');
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 60000, stepMs: 50 }), 'C:\\> GAMES\\DOOM\\DOOM finds DOOM1.WAD next to DOOM.EXE');
}

const all = { play, timedemo, wadcheck, fromroot };
for (const n of (which.length ? which : ['play', 'timedemo'])) await all[n]();
console.log(failures ? `${failures} FAILED` : 'all ok');
process.exit(failures ? 1 : 0);
