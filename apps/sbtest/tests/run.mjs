#!/usr/bin/env node
// apps/sbtest/tests/run.mjs - SBTEST.EXE on ARM-DOS with the machine's audio
// recorded: the detection line, then the PCM chime (E6 then C6, 16-bit DMA on
// channel 5) and the FM chord (C4 E4 G4 through the OPL3) are checked in the
// rendered audio by their spectral peaks. Writes build/sbtest-test/sbtest.wav.
import fs from 'node:fs';
import { B, makeImage, bootWithAudio, writeWav, rms, power, peakHz, window, RATE } from './audio.mjs';

const OUT = B('sbtest-test');
let failures = 0;
const check = (ok, what, extra = '') => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${extra ? '  ' + extra : ''}`); if (!ok) failures++; };
for (const f of ['rom.bin', 'IO.SYS', 'ARMDOS.SYS', 'COMMAND.COM', 'SBTEST.EXE', 'bootsect.bin'])
  if (!fs.existsSync(B(f))) { console.log(`build/${f} missing`); process.exit(2); }

const hd = makeImage(OUT, 'sbtest', [{ src: 'build/SBTEST.EXE', dst: 'DOS\\SBTEST.EXE' }]);
const pc = await bootWithAudio(hd);
check(pc.waitText('C:\\>', { timeoutMs: 30000 }), 'booted to C:\\>');
pc.type('SET\r'); pc.waitIdle();
check(pc.hasText('BLASTER=A220 I7 D1 H5 P330 T6'), 'AUTOEXEC.BAT set BLASTER=A220 I7 D1 H5 P330 T6');
const mx = pc.machine.sb.mixer;
check([0x30, 0x31, 0x32, 0x33, 0x34, 0x35].every((r) => mx[r] === 0xF8), 'AUTOEXEC.BAT SBMIX /INIT: master, voice, FM at 0 dB', [...mx.subarray(0x30, 0x36)].map((v) => v.toString(16)).join(' '));
pc.type('CLS\rSBMIX FM=24 CD=0\r'); pc.waitIdle();
check(pc.hasText('FM      ') && /FM +\u2588{24}\u2591{7}  24  -14 dB/.test(pc.lines().join('\n')), 'SBMIX FM=24 shows the level bar', pc.lines().find((l) => l.startsWith('  FM')));
check(mx[0x34] === 0xC0 && mx[0x36] === 0, 'SBMIX set the registers');
pc.type('SBMIX /INIT /Q\rCLS\r'); pc.waitIdle();
pc.type('SBTEST\r');
let tChime = 0, tFm = 0;
pc.until(() => { if (!tChime && pc.hasText('Playing a PCM chime')) tChime = pc.timeMs / 1000; if (!tFm && pc.hasText('Playing an FM chord')) tFm = pc.timeMs / 1000; return pc.hasText('Done.'); },
  { timeoutMs: 20000, stepMs: 5 });
pc.run(300);
const screen = pc.screen();
console.log(screen.split('\n').filter((l) => l.trim()).slice(-9).map((l) => '     ' + l).join('\n'));
check(screen.includes('Sound Blaster 16 found at 220h, IRQ 7, DMA 1/5, DSP 4.05'), 'detection line');
check(screen.includes('OPL3 (YMF262) FM synthesizer found at 388h'), 'OPL3 detected by the timer test');
check(/  [1-9]\d* blocks played/.test(screen), 'the stream took SB IRQs', (screen.match(/\d+ blocks played/) || [''])[0]);
check(screen.includes('Done.'), 'SBTEST finished');

const { L, R } = pc.audio();
fs.mkdirSync(OUT, { recursive: true });
writeWav(`${OUT}/sbtest.wav`, L, R);
console.log(`     ${(L.length / RATE).toFixed(2)} s of audio -> build/sbtest-test/sbtest.wav; chime at ${tChime.toFixed(2)} s, chord at ${tFm.toFixed(2)} s`);
// the chime: two notes of 0.55 s each, starting right after the message
const ding = window(L, tChime + 0.05, tChime + 0.45), dong = window(L, tChime + 0.62, tChime + 1.0);
const f1 = peakHz(ding, 300, 3000), f2 = peakHz(dong, 300, 3000);
check(Math.abs(f1 - 1318.5) < 3, `chime note 1 = E6 1318.5 Hz`, `${f1} Hz, rms ${rms(ding).toFixed(3)}`);
check(Math.abs(f2 - 1046.5) < 3, `chime note 2 = C6 1046.5 Hz`, `${f2} Hz, rms ${rms(dong).toFixed(3)}`);
check(rms(window(L, tChime - 0.3, tChime - 0.05)) < 0.002, 'silence before the chime');
// the chord: C4 E4 G4 - each fundamental well above the spectrum between the notes
const chord = window(L, tFm + 0.3, tFm + 1.3);
const fund = [261.63, 329.63, 392.0], between = [295, 360, 430];
const pf = fund.map((f) => power(chord, f)), pb = between.map((f) => power(chord, f));
check(rms(chord) > 0.01, 'FM chord is audible', `rms ${rms(chord).toFixed(3)}`);
fund.forEach((f, i) => check(pf[i] > 30 * pb[i], `FM chord contains ${f} Hz`, `${(10 * Math.log10(pf[i] / pb[i])).toFixed(1)} dB above ${between[i]} Hz`));
const after = window(L, tFm + 2.2, tFm + 2.3);
check(rms(after) < 0.01, 'chord released', `rms ${rms(after).toFixed(4)}`);
let corr = 0; for (let i = 0; i < chord.length; i++) corr += chord[i] * window(R, tFm + 0.3, tFm + 1.3)[i];
check(corr > 0, 'FM on both channels');
console.log(failures ? `${failures} FAILED` : 'all passed');
process.exit(failures ? 1 : 0);
