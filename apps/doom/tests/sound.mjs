#!/usr/bin/env node
// apps/doom/tests/sound.mjs - DOOM with the Sound Blaster 16 (ARM-DOS with
// COMMAND.COM, AUTOEXEC.BAT's SET BLASTER=A220 I7 D1 H5 T6), the machine's
// audio recorded to build/doom-test/sound/*.wav and checked:
//  * DOOM picks the SB (its I_InitSound line) and hooks IRQ 7 (INT 0Fh);
//  * the title music (D_INTROA) and E1M1's (D_E1M1, "At Doom's Gate") play on
//    the OPL3: the OPL register stream is logged and compared note for note
//    with the reference (Chocolate Doom's OPL player run natively on the same
//    lump: build/doom-test/sound/e1m1-ref.txt, see tests/oplref/), and the
//    audio is analysed (non-silent, the bass line's pitch);
//  * the pistol (DSPISTOL) is heard when firing: 8-bit PCM mixed by DOOM into
//    the 16-bit auto-init DMA stream on DMA 5;
//  * without BLASTER, DOOM falls back to the PC speaker.
// usage: node apps/doom/tests/sound.mjs [--keep-going]
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { compareNotes, parseLog } from './oplref/notes.mjs';
import { B, makeImage, bootWithAudio, writeWav, rms, power, peakHz, window, RATE } from '../../sbtest/tests/audio.mjs';

const OUT = B('doom-test/sound');
fs.mkdirSync(OUT, { recursive: true });
let failures = 0;
const check = (ok, what, extra = '') => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${extra ? '  ' + extra : ''}`); if (!ok) failures++; };
for (const f of ['rom.bin', 'IO.SYS', 'ARMDOS.SYS', 'COMMAND.COM', 'DOOM.EXE', 'bootsect.bin'])
  if (!fs.existsSync(B(f))) { console.log(`build/${f} missing`); process.exit(2); }

const files = [{ src: 'build/DOOM.EXE', dst: 'GAMES\\DOOM\\DOOM.EXE' }, { src: '3rdparty/doom/DOOM1.WAD', dst: 'GAMES\\DOOM\\DOOM1.WAD' }];
const tap = (pc, code, after = 400) => { pc.machine.keyDown(code); pc.run(80); pc.machine.keyUp(code); pc.run(after); };

async function doom(name, autoexec) {
  const hd = makeImage(OUT, name, files, autoexec !== undefined ? { autoexec } : {});
  const pc = await bootWithAudio(hd, { mpu: { present: false } });   // the OPL path: no MPU-401 (else DOOM plays General MIDI, tests/gm.mjs)
  // log OPL register writes (emulated time, register, value) as the chip sees them
  const opl = pc.machine.sb.opl, w = opl.write.bind(opl), log = [];
  opl.write = (off, v) => { if (off & 1) log.push([pc.machine.timeMs(), ((off >> 1) << 8) | opl.addr[off >> 1], v]); return w(off, v); };
  pc.oplLog = log;
  pc.waitText('C:\\>', { timeoutMs: 30000 });
  pc.type('CD \\GAMES\\DOOM\rDOOM\r');
  pc.until(() => pc.machine.vga.mode === 0x13, { timeoutMs: 60000, stepMs: 50 });
  return pc;
}

// ------------------------------------------------------------ with the SB
{
  const pc = await doom('sb');
  check(pc.machine.vga.mode === 0x13, 'DOOM started (mode 13h)');
  const text = pc.debug + pc.serial;
  const ivt = (n) => pc.cpu.m32[n];
  check(ivt(0x0F) !== 0 && (pc.machine.pic.imr & 0x80) === 0, 'IRQ 7 hooked (INT 0Fh) and unmasked');
  const tTitle = pc.timeMs / 1000;
  pc.run(8000);                                        // the title: D_INTROA on the OPL
  const tGame0 = pc.timeMs / 1000;
  tap(pc, 'Enter', 600); tap(pc, 'Enter', 600); tap(pc, 'Enter', 600);
  const tE1M1 = pc.timeMs / 1000;
  tap(pc, 'Enter', 200);                               // Hurt Me Plenty -> E1M1
  pc.run(14000);                                       // listen to "At Doom's Gate"
  const tFire = pc.timeMs / 1000;
  pc.machine.keyDown('ControlLeft'); pc.run(1500); pc.machine.keyUp('ControlLeft');
  pc.run(1500);
  await pc.png(path.join(OUT, 'sb-e1m1.png'));
  const { L, R } = pc.audio();
  writeWav(path.join(OUT, 'doom-sb.wav'), L, R);
  console.log(`     ${(L.length / RATE).toFixed(1)} s of audio -> ${path.relative(process.cwd(), OUT)}/doom-sb.wav (title at ${tTitle.toFixed(1)} s, E1M1 at ${tE1M1.toFixed(1)} s, firing at ${tFire.toFixed(1)} s)`);
  check(pc.oplLog.some(([, r]) => (r & 0xF0) === 0xB0 && r !== 0xBD), 'DOOM keyed OPL voices');
  const title = window(L, tTitle + 1, tGame0), e1m1 = window(L, tE1M1 + 1.5, tFire);
  check(rms(title) > 0.01, 'title music audible', `rms ${rms(title).toFixed(3)}`);
  check(rms(e1m1) > 0.01, 'E1M1 music audible', `rms ${rms(e1m1).toFixed(3)}`);
  fs.writeFileSync(path.join(OUT, 'e1m1-opl.txt'), pc.oplLog.filter(([t]) => t / 1000 >= tE1M1).map(([t, r, v]) => `${t.toFixed(3)} ${r.toString(16)} ${v.toString(16)}`).join('\n') + '\n');
  pc.oplE1M1 = pc.oplLog.filter(([t]) => t / 1000 >= tE1M1 && t / 1000 < tFire);
  // "At Doom's Gate" is an E power-chord riff: the bass E2 (82.4 Hz) under B3
  const bass = peakHz(window(L, tE1M1 + 1.5, tE1M1 + 7.5), 50, 140), fifth = peakHz(window(L, tE1M1 + 1.5, tE1M1 + 7.5), 140, 300);
  check(Math.abs(bass - 82.4) < 3, 'E1M1 bass line on E2 (82.4 Hz)', `${bass} Hz`);
  check(Math.abs(fifth - 246.9) < 5, 'E1M1 riff fifth B3 (246.9 Hz)', `${fifth} Hz`);
  // note for note against Chocolate Doom's OPL player (native build of tests/oplref)
  const refExe = path.join(OUT, 'oplref');
  const srcDir = path.join(path.dirname(new URL(import.meta.url).pathname), 'oplref');
  const cc = spawnSync('gcc', ['-O1', '-w', '-I', srcDir, '-o', refExe, ...['ref.c', 'i_oplmusic.c', 'midifile.c', 'mus2mid.c', 'memio.c'].map((f) => path.join(srcDir, f))], { encoding: 'utf8' });
  if (cc.status !== 0) console.log('     (no host C compiler: skipping the Chocolate Doom reference comparison)\n' + (cc.stderr || ''));
  else {
    const r = spawnSync(refExe, [path.join(B('..'), '3rdparty/doom/DOOM1.WAD'), 'D_E1M1', '64', '16'], { cwd: OUT, encoding: 'utf8', maxBuffer: 1 << 26 });
    fs.writeFileSync(path.join(OUT, 'e1m1-ref.txt'), r.stdout);
    const dur = Math.min(12000, (tFire - tE1M1 - 1) * 1000);
    const c = compareNotes(parseLog(r.stdout), pc.oplE1M1, dur);
    check(c.ref > 100 && c.matched >= c.ref - 1 && c.maxDt <= 8, `E1M1 note for note as Chocolate Doom's OPL player (${(dur / 1000).toFixed(1)} s)`,
      `${c.matched}/${c.ref} notes, same F-numbers, max |dt| ${c.maxDt.toFixed(1)} ms` + (c.unmatched.length ? `; unmatched ${JSON.stringify(c.unmatched.slice(0, 4))}` : ''));
  }
  // the pistol: a burst of broadband PCM on top of the music right after Ctrl
  const shot = window(L, tFire + 0.1, tFire + 1.4), before = window(L, tFire - 1.4, tFire - 0.1);
  const hi = (x) => { let a = 0; for (let f = 2000; f < 5000; f += 250) a += power(x, f); return a; };
  check(hi(shot) > 5 * hi(before), 'pistol shot heard (PCM burst over the music)', `${(10 * Math.log10(hi(shot) / hi(before))).toFixed(1)} dB at 2-5 kHz`);
  check(pc.machine.sb.pb && pc.machine.sb.pb.bits === 16 && pc.machine.sb.pb.stereo && pc.machine.sb.pb.ch === 5, 'SFX stream: 16-bit stereo auto-init on DMA 5');
  globalThis.sbRun = { pc, L, tE1M1, tFire };
  // quit cleanly: vectors restored
  tap(pc, 'F10', 500);                                  // quit prompt
  tap(pc, 'KeyY', 4000);
  check(pc.machine.vga.mode !== 0x13, 'DOOM quit to text mode');
  pc.run(500);
  check((pc.machine.pic.imr & 0x80) !== 0, 'IRQ 7 masked again after exit');
  check(!pc.machine.sb.pb, 'SB stream stopped after exit');
  // every OPL operator keyed off with its envelope run out (not just quiet: a note
  // frozen at -47 dB by a zeroed release rate measures under 0.001 rms sometimes)
  check(pc.machine.sb.opl.chip.isSilent(), 'music silent after exit', `rms ${rms(pc.audio().L.subarray(-4410)).toFixed(5)}`);
}

// ------------------------------------------------------------ without BLASTER
{
  const auto = fs.readFileSync(path.join(B('..'), 'disk/c/AUTOEXEC.BAT'), 'latin1').replace(/SET BLASTER[^\r\n]*\r?\n/i, '');
  const pc = await doom('nosb', auto);
  const spk = [];
  const s0 = pc.machine.speaker.bind(pc.machine);
  pc.machine.speaker = (on, f) => { spk.push([on, f]); s0(on, f); };
  pc.run(4000);
  tap(pc, 'Enter', 600); tap(pc, 'Enter', 600); tap(pc, 'Enter', 600); tap(pc, 'Enter', 2500);
  pc.machine.keyDown('ControlLeft'); pc.run(800); pc.machine.keyUp('ControlLeft'); pc.run(500);
  check(spk.some(([on]) => on), 'no BLASTER: PC speaker sound effects');
  check(!pc.oplLog.some(([, r]) => (r & 0xF0) === 0xB0 && r !== 0xBD), 'no BLASTER: no OPL music');
  check((pc.machine.pic.imr & 0x80) !== 0, 'no BLASTER: IRQ 7 left masked');
}

console.log(failures ? `${failures} FAILED` : 'all passed');
process.exit(failures ? 1 : 0);
