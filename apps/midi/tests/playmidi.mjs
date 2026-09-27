#!/usr/bin/env node
// apps/midi/tests/playmidi.mjs - PLAYMIDI.EXE on ARM-DOS (COMMAND.COM, AUTOEXEC.BAT with
// BLASTER ... P330) with the MPU-401 wired to the GM synthesizer and the audio recorded:
//  * the player screen (title, device line, channel rows, meters, time);
//  * every note-on that reaches the MPU matches the file (host SMF parse) in order,
//    key, velocity and time (within 2 ms of the tempo map);
//  * the audio: non-silent, the Bach prelude's opening C major arpeggio in the spectrum;
//  * Space pauses (no MIDI, notes off), N skips, Esc stops and restores the DOS screen;
//  * /Q (one line per file, redirectable), a multi-instrument file's program changes;
//  * without the MPU (page setting "off"): "MPU-401 not found at 330h."
// Writes build/midi-test/playmidi.wav and screenshots.
import fs from 'node:fs';
import { makeImage } from '../../sbtest/tests/audio.mjs';
import { bootMidi, parseStream, AUTOEXEC, check, done, writeWav, rms, power, win, B, ROOT } from './lib.mjs';
import { readSmf } from './smf.mjs';

const OUT = B('midi-test');
for (const f of ['rom.bin', 'IO.SYS', 'ARMDOS.SYS', 'COMMAND.COM', 'PLAYMIDI.EXE', 'bootsect.bin'])
  if (!fs.existsSync(B(f))) { console.log(`build/${f} missing`); process.exit(2); }
const songs = ['BACH846', 'NACHTMUS', 'FURELISE'];
const hd = makeImage(OUT, 'playmidi', [{ src: 'build/PLAYMIDI.EXE', dst: 'DOS\\PLAYMIDI.EXE' },
  ...songs.map((s) => ({ src: `apps/midi/data/${s}.MID`, dst: `MIDI\\${s}.MID` }))], { autoexec: AUTOEXEC });

const pc = await bootMidi(hd);
check(pc.waitText('C:\\>', { timeoutMs: 30000 }), 'booted to C:\\>');
pc.type('CLS\rECHO before\r'); pc.waitIdle();
const before = pc.screen();
pc.type('PLAYMIDI C:\\MIDI\\BACH846\r');
check(pc.waitText('ARM-PC MIDI Player', { timeoutMs: 10000 }), 'player screen');
const tStart = pc.timeMs / 1000;
pc.run(12000);
const scr = pc.screen();
await pc.png(`${OUT}/playmidi.png`);
console.log(scr.split('\n').slice(0, 10).map((l) => '     ' + l).join('\n'));
check(scr.includes('Title:  J.S. Bach: Prelude in C major, BWV 846'), 'title from the file');
check(scr.includes('MPU-401 at 330h, UART mode, IRQ 9'), 'device line: MPU-401 at 330h (BLASTER P330), UART mode');
check(/Time 00:1[12] \/ 02:20/.test(scr), 'song time and length', (scr.match(/Time [^║]*/) || [''])[0].trim());
check(/ 1 Acoustic Grand Piano +100 +C /.test(scr) && /10 Standard Kit/.test(scr), 'channel rows (instrument, volume, pan)');
check(/ 2 Acoustic Grand Piano .*\u2588/.test(scr), 'channel 2 meter moving');

// the MIDI stream against the file
const smf = readSmf(fs.readFileSync(`${ROOT}/apps/midi/data/BACH846.MID`));
const msgs = parseStream(pc.midi);
const ons = msgs.filter((m) => (m.st & 0xF0) === 0x90 && m.d2);
const exp = smf.notes;
let match = 0, worst = 0;
const t0 = ons.length ? ons[0].ms - exp[0].us / 1000 : 0;
for (let i = 0; i < ons.length; i++) {
  const e = exp[i], o = ons[i];
  if (!e) break;
  const dt = Math.abs(o.ms - t0 - e.us / 1000);
  if ((o.st & 15) === (e.st & 15) && o.d1 === e.d1 && o.d2 === e.d2 && dt <= 2) match++;
  worst = Math.max(worst, dt);
}
check(ons.length >= 40 && match === ons.length, `note-ons match the file in order, key, velocity and time`, `${match}/${ons.length} in 12 s, worst ${worst.toFixed(2)} ms`);
check(pc.midi.slice(0, 6).map((x) => x[1]).join() === '240,126,127,9,1,247', 'GM System On first');

// audio
const { L, R } = pc.audio();
writeWav(`${OUT}/playmidi.wav`, L, R);
const song = win(L, tStart + 0.3, tStart + 11);
check(rms(song) > 0.005, 'audible', `rms ${rms(song).toFixed(4)}`);
const first = win(L, ons[0].ms / 1000, ons[0].ms / 1000 + 1.9);      // bar 1: C4 E4 G4 C5 E5 G4 C5 E5 x2
const tones = [261.63, 329.63, 392.0, 523.25], off = [293.66, 349.23, 440.0, 466.16];
const pt = tones.map((f) => power(first, f)), po = off.map((f) => power(first, f));
check(Math.min(...pt) > 10 * Math.max(...po), 'bar 1 sounds C4 E4 G4 C5 (not D4 F4 A4 A#4)',
  tones.map((f, i) => `${f}:${(10 * Math.log10(pt[i] / Math.max(...po))).toFixed(0)}dB`).join(' '));

// pause, next file, Esc
const n0 = pc.midi.length;
pc.type(' '); pc.run(1500);
const pausedScr = pc.screen();
const during = pc.midi.slice(n0).map((x) => x[1]);
pc.run(2000);
check(pausedScr.includes('PAUSED') && pc.midi.length === n0 + during.length, 'Space pauses (no MIDI while paused)');
check(during.filter((b, i) => b === 123 && (during[i - 1] & 0xF0) === 0xB0 || b === 123).length >= 16, 'pause sends All Notes Off on every channel');
check(rms(win(pc.audio().L, pc.timeMs / 1000 - 1.0, pc.timeMs / 1000)) < 0.003, 'silent while paused');
pc.type(' '); pc.run(1500);
check(pc.midi.length > n0 + during.length + 10, 'Space again resumes');
pc.type('{ESC}'); pc.waitIdle();
{ const b = before.split('\n'), a = pc.screen().split('\n');
  const k = b.findIndex((l) => l.startsWith('before'));
  check(k >= 0 && a[k] === b[k] && a[k + 1] === '' && a[k + 2].startsWith('C:\\>PLAYMIDI') && a[k + 3] === '' && a[k + 4].startsWith('C:\\>'), 'Esc stops and restores the DOS screen'); }
check(pc.synth.activeVoices === 0 || pc.run(3000) && pc.synth.activeVoices === 0, 'no hanging notes after Esc');

// /Q with two files, N skips; program changes of the string quartet
pc.type('CLS\rPLAYMIDI /Q C:\\MIDI\\NACHTMUS C:\\MIDI\\FURELISE > C:\\PLAY.LOG\r');
pc.run(4000);
const m1 = pc.midi.length;
const progs = parseStream(pc.midi).filter((m) => (m.st & 0xF0) === 0xC0).map((m) => `${(m.st & 15) + 1}:${m.d1}`);
check(['2:40', '3:40', '4:41', '5:42'].every((p) => progs.includes(p)), 'Eine kleine Nachtmusik: violins, viola, cello (programs 40, 41, 42)', progs.slice(-4).join(' '));
pc.type('N'); pc.run(3000);
pc.type('{ESC}'); pc.waitIdle();
pc.type('TYPE C:\\PLAY.LOG\r'); pc.waitIdle();
const log = pc.screen();
console.log(log.split('\n').filter((l) => l.trim()).slice(-5).map((l) => '     ' + l).join('\n'));
check(log.includes('Playing C:\\MIDI\\NACHTMUS.MID (05:19)\n  W.A. Mozart: Eine kleine Nachtmusik, I. Allegro')
  && log.includes('Playing C:\\MIDI\\FURELISE.MID (02:10)\n  L. van Beethoven: Fur Elise, WoO 59') && log.includes('Stopped.'),
  '/Q: one line per file through DOS (redirected), N skipped to the next, Esc stopped');
check(pc.midi.length > m1, 'the second file played');

// wildcard + missing file, then the MPU switched off
pc.type('CLS\rPLAYMIDI /Q C:\\MIDI\\NOSUCH\r'); pc.waitIdle();
check(pc.hasText('C:\\MIDI\\NOSUCH.MID: File not found'), 'missing file reported');
pc.type('CLS\rPLAYMIDI\r'); pc.waitIdle();
check(pc.hasText('PLAYMIDI [/Q] [/L] [drive:][path]filename[.MID] [...]'), 'usage without arguments');
pc.machine.mpu.present = false;
pc.type('CLS\rPLAYMIDI C:\\MIDI\\BACH846\r'); pc.waitText('MPU-401 not found', { timeoutMs: 5000 });
check(pc.hasText('MPU-401 not found at 330h.'), 'no MPU: "MPU-401 not found at 330h."');
check(pc.faults.length === 0, 'no CPU faults');
done();
