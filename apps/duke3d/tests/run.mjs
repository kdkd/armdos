#!/usr/bin/env node
// apps/duke3d/tests/run.mjs - boot ARM-DOS headless and play Duke Nukem 3D.
//
//   node apps/duke3d/tests/run.mjs [play] [fps] [fromroot] [nomem] [--out DIR] [--mhz N] [--no-jit]
//
// Builds its own hard disk image (IO.SYS, ARMDOS.SYS, COMMAND.COM, HIMEM.SYS
// in CONFIG.SYS, SET BLASTER + SBMIX /INIT + MOUSE in AUTOEXEC.BAT, and
// C:\GAMES\DUKE3D\ as apps/duke3d/hd.json has it), boots it, types at the
// real DOS prompt and presses real keys through the 8042.
//
//   play      MEM (the extender stub fits in conventional memory), DUKE3D:
//             the start-up text (title bars, CON compile, "Checking music/
//             sound inits."), mode 13h, INT 08h/09h and the SB's IRQ 7
//             hooked, the BIOS tick kept at 18.2 Hz, the 3D Realms logo with
//             the title music on the OPL3 (key-ons logged, the audio
//             analysed), the "Duke Nukem 3D" title with its explosions on the
//             SB16, the menu, New Game -> L.A. Meltdown -> Let's Rock ->
//             E1L1 "Hollywood Holocaust", walk, turn, fire (ammo 48 -> 47,
//             the shot audible), jump, mouse turn (MOUSE.COM), Esc -> Quit ->
//             Y -> the DUKESW.BIN shareware screen in text mode, vectors
//             restored, DUKE3D.CFG written. The machine's audio is written
//             to build/duke3d-test/duke3d.wav.
//   fps       E1L1 at the machine's clock: frames/s over 20 s of play (the
//             engine's frame counter and 120 Hz clock read from memory).
//   fromroot  C:\>GAMES\DUKE3D\DUKE3D: the data is found next to DUKE3D.EXE.
//   nomem     no HIMEM.SYS: the extender stub says so and returns to DOS.
//
// Screenshots go to build/duke3d-test/.

import fs from 'node:fs';
import path from 'node:path';
import { B, start, mode, ivt, bdaTicks, hold, tap, colours, frameHash, busy, readHostFile, elfSymbols, structOffsets } from './lib.mjs';
import { writeWav, rms, peakHz, window, RATE } from '../../sbtest/tests/audio.mjs';

const argv = process.argv.slice(2);
const opt = (name, def) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : def; };
const OUT = path.resolve(opt('--out', B('duke3d-test')));
const MHZ = +opt('--mhz', 0) || undefined;
const JIT = !argv.includes('--no-jit');
const which = argv.filter((a, i) => !a.startsWith('--') && !(i > 0 && ['--out', '--mhz'].includes(argv[i - 1])));
fs.mkdirSync(OUT, { recursive: true });

let failures = 0;
const check = (ok, what, extra = '') => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${extra ? '  ' + extra : ''}`); if (!ok) failures++; };
const shot = async (pc, file) => { await pc.png(path.join(OUT, file)); console.log(`     screenshot ${path.join(OUT, file)}`); };
const dbgTail = (pc, n = 12) => pc.debug.split('\n').slice(-n).join('\n');

// the game's own variables, from the ELF (the game image sits in XMS at the
// address D3DLOAD reports on the debug port)
const ELF = B('obj/D3DGAME/D3DGAME.elf');
const SYMS = elfSymbols(ELF);
// ps[0].ammo_amount[PISTOL_WEAPON = 1] (int16_t): the offset from the ELF's DWARF
const AMMO_OFF = (structOffsets(ELF, 'player_struct') || {}).ammo_amount + 2;
const base = (pc) => parseInt((pc.debug.match(/image at ([0-9A-F]+)/) || [0, '0'])[1], 16);
const peek32 = (pc, sym) => pc.cpu.m32[(base(pc) + SYMS.get(sym)) >>> 2] | 0;
const peek16 = (pc, sym, off = 0) => { const a = base(pc) + SYMS.get(sym) + off; return (pc.cpu.m32[a >>> 2] >>> ((a & 2) * 8)) << 16 >> 16; };

// Audio from power-on (like apps/sbtest/tests/audio.mjs bootWithAudio), plus
// the OPL register writes and the Sound Blaster DSP commands.
function audioProbe(pc) {
  const chunks = [];
  pc.machine.audio.start(RATE, (l, r) => chunks.push([l, r]), { speaker: false });
  const opl = pc.machine.sb.opl, w = opl.write.bind(opl), oplLog = [];
  opl.write = (off, v) => { if (off & 1) oplLog.push([pc.timeMs, ((off >> 1) << 8) | opl.addr[off >> 1], v]); return w(off, v); };
  const sb = pc.machine.sb, sw = sb.write.bind(sb); let dsp = 0;
  sb.write = (port, v) => { if ((port & 0xF) === 0xC) dsp++; return sw(port, v); };
  const t0 = pc.timeMs / 1000;
  return {
    oplLog, get dsp() { return dsp; },
    audio() {
      pc.machine.audio.pump();
      const n = chunks.reduce((a, [l]) => a + l.length, 0);
      const L = new Float32Array(n), R = new Float32Array(n);
      let o = 0;
      for (const [l, r] of chunks) { L.set(l, o); R.set(r, o); o += l.length; }
      return { L, R, t0 };
    },
  };
}

async function toGame(pc) {
  // after the logo and the title the game tries its attract-mode demo
  // (DEMO1.DMO, see the README) and shows the main menu
  pc.until(() => /demo1\.dmo/i.test(pc.debug), { timeoutMs: 30000, stepMs: 100 });
  pc.run(500);
  // menu -> New Game -> L.A. Meltdown -> Let's Rock
  tap(pc, 'Escape', 1200);
  tap(pc, 'Enter', 1000);
  tap(pc, 'Enter', 1000);
  tap(pc, 'ArrowDown', 400);
  tap(pc, 'Enter', 200);
  return pc.until(() => /Entering E1L1/.test(pc.debug), { timeoutMs: 20000 });
}

// ---------------------------------------------------------------- play
async function play() {
  console.log('== play');
  const pc = await start(OUT, 'play', { autoexec: 'MOUSE', mhz: MHZ, jit: JIT });
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('MEM\r');
  pc.waitText('largest executable program size', { timeoutMs: 10000 });
  const free = +((pc.screen().match(/([\d,]+) (?:bytes )?largest executable/) || [0, '0'])[1].replace(/,/g, ''));
  console.log(pc.screen().split('\n').filter((l) => /largest|bytes/.test(l)).map((l) => '     | ' + l).join('\n'));
  const need = (() => { const e = fs.readFileSync(B('duke3d/D3DLOAD.EXE')); const h = e.readUInt32LE(0x3C);
    return 256 + e.readUInt32LE(h + 12) + e.readUInt32LE(h + 16) + e.readUInt32LE(h + 20); })();
  check(free >= need, `conventional memory: ${free} bytes free, DUKE3D.EXE's stub needs ${need}`);
  pc.type('CLS\r'); pc.run(300);
  const snd = audioProbe(pc);
  const old08 = ivt(pc, 8), old09 = ivt(pc, 9), old0f = ivt(pc, 0x0F);
  const t0 = performance.now();
  pc.type('CD \\GAMES\\DUKE3D\rDUKE3D\r');
  check(pc.waitText('Duke Nukem 3D Unregistered Shareware v1.3D', { timeoutMs: 10000 }), 'title bar "Duke Nukem 3D Unregistered Shareware v1.3D"');
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 30000 }), 'mode 13h set');
  const startup = pc.screen();
  for (const t of ['Copyright (c) 1996 3D Realms Entertainment', 'Please read LICENSE.DOC', "Compiling: 'GAME.CON'",
    'You have run Duke Nukem 3D 1 times', 'Loading art header.', 'Checking music inits.', 'Checking sound inits.', 'Loading palette/lookups.'])
    check(startup.includes(t) || pc.debug.includes(t), `start-up text "${t}"`);
  console.log(startup.split('\n').filter((l) => l.trim()).map((l) => '     | ' + l).join('\n'));
  check(ivt(pc, 8) !== old08 && ivt(pc, 9) !== old09, 'INT 08h and 09h hooked');
  check(ivt(pc, 0x0F) !== old0f && (pc.machine.pic.imr & 0x80) === 0, 'Sound Blaster IRQ 7 (INT 0Fh) hooked and unmasked');
  const tk0 = bdaTicks(pc), tm0 = pc.timeMs;
  pc.run(2500);
  await shot(pc, 'logo.png');
  check(colours(pc) > 20, `the 3D Realms logo (${colours(pc)} colours)`);
  const tLogo = pc.timeMs / 1000;
  pc.run(3500);
  const tLogoEnd = pc.timeMs / 1000;
  const tk1 = bdaTicks(pc), tm1 = pc.timeMs;
  const hz = (tk1 - tk0) / ((tm1 - tm0) / 1000);
  check(Math.abs(hz - 18.2) < 1, `BIOS tick at ${hz.toFixed(2)} Hz while the game owns the PIT`);
  const keyOns = snd.oplLog.filter(([t, r, v]) => (r & 0xF0) === 0xB0 && r !== 0xBD && (v & 0x20)).length;
  check(keyOns > 20, `title music on the OPL3 (${keyOns} key-ons)`);
  // the title: "DUKE NUKEM" and "3D" fly in with two explosions (SB16)
  pc.until(() => colours(pc) > 60 && frameHash(pc) !== 0, { timeoutMs: 8000, stepMs: 100 });
  pc.run(3000);
  await shot(pc, 'title.png');
  check(snd.dsp > 10, `Sound Blaster programmed (${snd.dsp} DSP writes)`);
  pc.run(1000);

  check(await toGame(pc), 'New Game -> E1L1 "Hollywood Holocaust"', (pc.debug.match(/Entering E1L1: .*/) || [''])[0]);
  pc.run(3000);
  await shot(pc, 'e1l1.png');
  const ammo = () => (Number.isInteger(AMMO_OFF) ? peek16(pc, 'ps', AMMO_OFF) : NaN);
  const ammo0 = ammo();
  const h0 = frameHash(pc);
  hold(pc, 'ArrowUp', 1500);
  await shot(pc, 'e1l1-walk.png');
  check(frameHash(pc) !== h0, 'walking changes the view');
  hold(pc, 'ArrowLeft', 600);
  await shot(pc, 'e1l1-turn.png');
  const tFire = pc.timeMs / 1000;
  tap(pc, 'ControlLeft', 300);
  await shot(pc, 'e1l1-fire.png');
  pc.run(700);
  const tFireEnd = pc.timeMs / 1000;
  check(ammo() === ammo0 - 1, `firing the pistol: ammo ${ammo0} -> ${ammo()}`);
  tap(pc, 'KeyA', 800);                // jump
  const hm = frameHash(pc);
  for (let i = 0; i < 10; i++) { pc.machine.mouseMove(30, 0); pc.run(50); }
  pc.run(300);
  await shot(pc, 'e1l1-mouse.png');
  check(frameHash(pc) !== hm, 'the mouse turns the view (MOUSE.COM, INT 33h)');
  tap(pc, 'F12', 800);                 // screen capture
  const pcx = readHostFile(pc, 'GAMES\\DUKE3D\\DUKE0000.PCX');
  check(pcx && pcx[0] === 10 && pcx.length > 10000, `F12 writes DUKE0000.PCX (${pcx ? pcx.length : 0} bytes)`);
  const b = busy(pc, 2000);
  console.log(`     CPU busy ${(b * 100).toFixed(0)}% while playing E1L1`);

  // the audio
  const { L, R, t0: a0 } = snd.audio();
  writeWav(path.join(OUT, 'duke3d.wav'), L, R);
  console.log(`     ${(L.length / RATE).toFixed(1)} s of audio -> ${path.join(OUT, 'duke3d.wav')}`);
  const logo = window(L, tLogo - a0, tLogoEnd - a0);
  check(rms(logo) > 0.005, 'music audible during the 3D Realms logo (OPL3 only)', `rms ${rms(logo).toFixed(4)}, strongest ${peakHz(logo, 60, 1000)} Hz`);
  const fire = window(L, tFire - a0, tFireEnd - a0);
  check(rms(fire) > rms(logo) * 0.5 && rms(fire) > 0.005, 'the pistol shot audible (SB16 PCM)', `rms ${rms(fire).toFixed(4)}`);

  // quit: Esc -> Quit (the last item) -> Y
  tap(pc, 'Escape', 800);
  tap(pc, 'ArrowUp', 400);
  tap(pc, 'Enter', 800);
  await shot(pc, 'quit-prompt.png');
  tap(pc, 'KeyY', 2500);
  // the shareware version shows its two ordering screens, each until a key
  await shot(pc, 'order1.png');
  const ho = frameHash(pc);
  check(mode(pc) === 0x13 && colours(pc) > 20, 'first ordering screen');
  tap(pc, 'Space', 2500);
  await shot(pc, 'order2.png');
  check(mode(pc) === 0x13 && frameHash(pc) !== ho, 'second ordering screen');
  tap(pc, 'Space', 300);
  check(pc.until(() => mode(pc) === 3 && pc.hasText('C:\\GAMES\\DUKE3D>'), { timeoutMs: 20000 }), 'back at the DOS prompt in text mode');
  await shot(pc, 'exit.png');
  const exitScreen = pc.screen();
  console.log(exitScreen.split('\n').map((l) => '     | ' + l).join('\n'));
  check(/3D REALMS|3D Realms|ORDER/i.test(exitScreen), 'the DUKESW.BIN shareware screen');
  check(ivt(pc, 8) === old08 && ivt(pc, 9) === old09 && ivt(pc, 0x0F) === old0f, 'INT 08h, 09h and 0Fh restored');
  const cfg = readHostFile(pc, 'GAMES\\DUKE3D\\DUKE3D.CFG');
  check(cfg && /FXDevice/.test(cfg.toString()), `DUKE3D.CFG written (${cfg ? cfg.length : 0} bytes)`);
  console.log(`     ${pc.timeMs.toFixed(0)} ms emulated in ${(performance.now() - t0).toFixed(0)} ms host`);
  if (pc.faults.length) console.log('     faults:', pc.faults.slice(0, 5));
}

// ------------------------------------------------------------------ fps
async function fps() {
  console.log('== fps');
  const pc = await start(OUT, 'fps', { autoexec: '', mhz: MHZ, jit: JIT });
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('CD \\GAMES\\DUKE3D\rDUKE3D\r');
  pc.until(() => mode(pc) === 0x13, { timeoutMs: 30000 });
  pc.run(4000);
  check(await toGame(pc), 'E1L1 entered');
  pc.run(2000);
  const f0 = peek32(pc, 'total_rendered_frames'), c0 = peek32(pc, 'totalclock');
  const t0 = performance.now(), i0 = pc.cpu.icount, e0 = pc.timeMs;
  // play: walk around the start area, turning
  for (let i = 0; i < 5; i++) { hold(pc, 'ArrowUp', 1500); hold(pc, 'ArrowLeft', 900); hold(pc, 'ArrowDown', 800); hold(pc, 'ArrowRight', 800); }
  const f1 = peek32(pc, 'total_rendered_frames'), c1 = peek32(pc, 'totalclock');
  const ms = performance.now() - t0, insns = pc.cpu.icount - i0, em = pc.timeMs - e0;
  const rate = (f1 - f0) / ((c1 - c0) / 120);
  await shot(pc, 'fps-end.png');
  check(rate > 5, `E1L1 at ${MHZ || 100} MHz: ${(f1 - f0)} frames in ${((c1 - c0) / 120).toFixed(1)} s = ${rate.toFixed(1)} fps`);
  console.log(`     ${(em / 1000).toFixed(1)} s emulated, ${(insns / 1e6).toFixed(0)}M instructions, host ${ms.toFixed(0)} ms = ${(insns / ms / 1000).toFixed(0)} host MIPS`);
}

// ------------------------------------------------------------- fromroot
async function fromroot() {
  console.log('== fromroot');
  const pc = await start(OUT, 'fromroot', { autoexec: '', mhz: MHZ, jit: JIT });
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('GAMES\\DUKE3D\\DUKE3D\r');
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 30000 }), 'C:\\>GAMES\\DUKE3D\\DUKE3D finds DUKE3D.GRP next to DUKE3D.EXE');
  pc.until(() => /demo1\.dmo/i.test(pc.debug), { timeoutMs: 30000, stepMs: 100 });
  pc.run(1000);
  tap(pc, 'Escape', 1000);
  tap(pc, 'ArrowUp', 400);
  tap(pc, 'Enter', 800);
  tap(pc, 'KeyY', 2500);
  tap(pc, 'Space', 2500);
  tap(pc, 'Space', 300);
  const ok = pc.until(() => mode(pc) === 3 && pc.hasText('C:\\>'), { timeoutMs: 20000 });
  if (!ok) { await shot(pc, 'fromroot-fail.png'); console.log(pc.screen()); }
  check(ok, 'quit back to C:\\> (the current directory restored)');
}

// ---------------------------------------------------------------- nomem
async function nomem() {
  console.log('== nomem');
  const pc = await start(OUT, 'nomem', { autoexec: '', himem: false, mhz: MHZ, jit: JIT });
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('CD \\GAMES\\DUKE3D\rDUKE3D\r');
  check(pc.until(() => pc.hasText('HIMEM.SYS'), { timeoutMs: 20000 }), 'the stub asks for an extended memory manager');
  check(pc.until(() => pc.hasText('C:\\GAMES\\DUKE3D>'), { timeoutMs: 5000 }), 'back at the prompt');
  console.log(pc.screen().split('\n').filter((l) => l.trim()).map((l) => '     | ' + l).join('\n'));
}

const all = { play, fps, fromroot, nomem };
for (const n of (which.length ? which : ['play', 'fps', 'fromroot', 'nomem'])) await all[n]();
console.log(failures ? `${failures} FAILED` : 'all passed');
process.exit(failures ? 1 : 0);
