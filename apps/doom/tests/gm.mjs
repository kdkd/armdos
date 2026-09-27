#!/usr/bin/env node
// apps/doom/tests/gm.mjs - DOOM's music as General MIDI on the MPU-401 (i_mpumus_armdos.c):
// AUTOEXEC.BAT with SET BLASTER=A220 I7 D1 H5 P330 T6, the MPU-401 wired to the GM synth
// (emu/dev/gmsynth.mjs with 3rdparty/midi/ARMGS.SFA), audio recorded to build/doom-test/gm/.
//  * DOOM says "I_InitMusic: General MIDI, MPU-401 at 330h" and plays D_INTRO (the GM
//    version of the title music, not D_INTROA);
//  * E1M1: every note-on sent to the MPU matches a host interpretation of the D_E1M1 MUS
//    lump (channel mapping 15 -> 10, key, velocity) at DMX's 140 Hz tick times, and the
//    audio is non-silent with the riff's E2 bass;
//  * the menu's music volume slider scales CC 7; quitting silences all channels;
//  * DMXOPTION=-fm keeps the OPL3 (no MIDI bytes).
import fs from 'node:fs';
import path from 'node:path';
import { makeImage } from '../../sbtest/tests/audio.mjs';
import { bootMidi, parseStream, AUTOEXEC, check, done, writeWav, rms, peakHz, power, win, B, ROOT } from '../../midi/tests/lib.mjs';

const OUT = B('doom-test/gm');
fs.mkdirSync(OUT, { recursive: true });
for (const f of ['rom.bin', 'IO.SYS', 'ARMDOS.SYS', 'COMMAND.COM', 'DOOM.EXE', 'bootsect.bin'])
  if (!fs.existsSync(B(f))) { console.log(`build/${f} missing`); process.exit(2); }
const WAD = fs.readFileSync(path.join(ROOT, '3rdparty/doom/DOOM1.WAD'));
const files = [{ src: 'build/DOOM.EXE', dst: 'GAMES\\DOOM\\DOOM.EXE' }, { src: '3rdparty/doom/DOOM1.WAD', dst: 'GAMES\\DOOM\\DOOM1.WAD' }];
const tap = (pc, code, after = 400) => { pc.machine.keyDown(code); pc.run(80); pc.machine.keyUp(code); pc.run(after); };

function lump(name) {
  const n = WAD.readInt32LE(4), dir = WAD.readInt32LE(8);
  for (let i = 0; i < n; i++) {
    const o = dir + i * 16;
    if (WAD.toString('latin1', o + 8, o + 16).replace(/\0.*$/, '') === name) return WAD.subarray(WAD.readInt32LE(o), WAD.readInt32LE(o) + WAD.readInt32LE(o + 4));
  }
  return null;
}
/** MUS -> note-ons [{ tick, ch (MIDI), key, vel }] (the MUS format; independent of the C code). */
function musNotes(mus, maxTicks) {
  const start = mus.readUInt16LE(6), len = mus.readUInt16LE(4), end = start + len, out = [], vel = new Array(16).fill(127);
  let p = start, tick = 0;
  const mch = (c) => (c === 15 ? 9 : c < 9 ? c : c + 1);
  while (p < end && tick <= maxTicks) {
    const ev = mus[p++], c = ev & 15, type = (ev >> 4) & 7;
    if (type === 0 || type === 2 || type === 3) p++;
    else if (type === 1) { let k = mus[p++]; if (k & 0x80) vel[c] = mus[p++] & 0x7F; out.push({ tick, ch: mch(c), key: k & 0x7F, vel: vel[c] }); }
    else if (type === 4) p += 2;
    else if (type === 6) break;
    if (ev & 0x80) { let d = 0, b; do { b = mus[p++]; d = (d << 7) | (b & 0x7F); } while (b & 0x80); tick += d; }
  }
  return out;
}

async function doom(name, autoexec) {
  const hd = makeImage(OUT, name, files, { autoexec });
  const pc = await bootMidi(hd);
  pc.waitText('C:\\>', { timeoutMs: 30000 });
  pc.type('CD \\GAMES\\DOOM\rDOOM\r');
  let initLine = '';
  pc.until(() => { const t = pc.screen(); const m = t.match(/I_InitMusic: [^\n]*/); if (m) initLine = m[0].trim(); return pc.machine.vga.mode === 0x13; }, { timeoutMs: 60000, stepMs: 20 });
  pc.initLine = initLine;
  return pc;
}

{
  const pc = await doom('gm', AUTOEXEC);
  check(pc.machine.vga.mode === 0x13, 'DOOM started (mode 13h)');
  check(pc.initLine === 'I_InitMusic: General MIDI, MPU-401 at 330h', 'DOOM chose General MIDI on the MPU-401', pc.initLine);
  const tTitle = pc.timeMs;
  pc.run(8000);
  const titleOns = parseStream(pc.midi).filter((m) => (m.st & 0xF0) === 0x90 && m.d2 && m.ms >= tTitle);
  const intro = musNotes(lump('D_INTRO'), 140 * 8);
  check(titleOns.length > 10 && titleOns.slice(0, 10).every((m, i) => m.d1 === intro[i].key), 'title music is D_INTRO (the GM version)', `${titleOns.length} notes`);
  const tMenu = pc.timeMs;
  tap(pc, 'Enter', 600); tap(pc, 'Enter', 600); tap(pc, 'Enter', 600);
  const tE1M1 = pc.timeMs;
  tap(pc, 'Enter', 200);
  pc.run(14000);
  const tEnd = pc.timeMs;
  const { L, R } = pc.audio();
  writeWav(path.join(OUT, 'doom-gm.wav'), L, R);
  await pc.png(path.join(OUT, 'gm-e1m1.png'));
  // E1M1 note for note
  const msgs = parseStream(pc.midi);
  const e1Start = msgs.findIndex((m) => m.ms > tE1M1 && (m.st & 0xF0) === 0xB0 && m.d1 === 121);   // RestartSong
  const ons = msgs.slice(e1Start).filter((m) => (m.st & 0xF0) === 0x90 && m.d2);
  const ref = musNotes(lump('D_E1M1'), 140 * 12);
  const t0 = ons.length ? ons[0].ms - ref[0].tick * 1000 / 140 : 0;
  let matched = 0, maxDt = 0;
  const n = ons.filter((m) => m.ms - t0 < 12000).length;
  for (let i = 0; i < n; i++) {
    const o = ons[i], e = ref[i];
    const dt = Math.abs(o.ms - t0 - e.tick * 1000 / 140);
    if ((o.st & 15) === e.ch && o.d1 === e.key && o.d2 === e.vel && dt < 1.5) matched++;
    maxDt = Math.max(maxDt, dt);
  }
  check(n > 100 && matched === n, 'E1M1: note-ons match the MUS score (channel, key, velocity, 140 Hz ticks)', `${matched}/${n} in 12 s, max |dt| ${maxDt.toFixed(2)} ms`);
  check(ons.some((m) => (m.st & 15) === 9), 'percussion on MIDI channel 10');
  const progs = [...new Set(msgs.slice(e1Start).filter((m) => (m.st & 0xF0) === 0xC0).map((m) => m.d1))];
  check(progs.includes(30) || progs.includes(29), 'E1M1 programs include the distortion/overdrive guitar', progs.join(','));
  const e1 = win(L, tE1M1 / 1000 + 1.5, tEnd / 1000);
  check(rms(e1) > 0.01, 'E1M1 music audible through the synth', `rms ${rms(e1).toFixed(3)}`);
  const bass = peakHz(win(L, tE1M1 / 1000 + 1.5, tE1M1 / 1000 + 7.5), 70, 100);
  check(Math.abs(bass - 82.4) < 3, 'E1M1 bass on E2 (82.4 Hz, as the OPL test)', `${bass.toFixed(1)} Hz`);
  void R; void tMenu;
  // music volume: Esc -> Options -> Sound Volume -> Music Volume left x3
  const n0 = pc.midi.length;
  tap(pc, 'Escape', 400); tap(pc, 'ArrowDown', 200); tap(pc, 'Enter', 400);    // Options
  for (let i = 0; i < 5; i++) tap(pc, 'ArrowDown', 150);                          // Sound Volume
  tap(pc, 'Enter', 400); tap(pc, 'ArrowDown', 150);                             // Music Volume
  const vol = parseStream(pc.midi.slice(n0)).filter((m) => (m.st & 0xF0) === 0xB0 && m.d1 === 7);
  tap(pc, 'ArrowLeft', 200); tap(pc, 'ArrowLeft', 200); tap(pc, 'ArrowLeft', 200);
  const vol2 = parseStream(pc.midi.slice(n0)).filter((m) => (m.st & 0xF0) === 0xB0 && m.d1 === 7).slice(vol.length);
  check(vol2.length >= 16, 'music volume slider sends CC 7 on the channels', `${vol2.length} CC 7 messages, last ${vol2.length ? vol2[vol2.length - 1].d2 : '-'}`);
  tap(pc, 'Escape', 500);                                  // (Esc closes the whole menu)
  // quit
  tap(pc, 'F10', 500); tap(pc, 'KeyY', 4000);
  check(pc.machine.vga.mode !== 0x13, 'DOOM quit to text mode');
  pc.run(3000);
  check(!pc.machine.mpu.uart && pc.synth.activeVoices === 0, 'after quitting: MPU reset, no voice sounding', `${pc.synth.activeVoices} voices`);
  check(pc.faults.length === 0, 'no CPU faults');
}
{
  const pc = await doom('fm', AUTOEXEC + 'SET DMXOPTION=-fm\n');
  pc.run(3000);
  check(pc.midi.length === 0 && !pc.initLine, 'DMXOPTION=-fm: OPL music, nothing sent to the MPU', `${pc.midi.length} bytes`);
}
done();
