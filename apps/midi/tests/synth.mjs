#!/usr/bin/env node
// apps/midi/tests/synth.mjs - the MPU-401 at the port level and the GM synthesizer
// rendered through the machine's audio path: pitch, envelope (attack, sustain,
// release), velocity and CC 7/10/11 levels, pitch bend with an RPN 0 range,
// sustain pedal, program change, the drum kit on channel 10, emulated-time
// placement of notes, the SB16 mixer's MIDI volume, the remote (worklet) mode.
import { Machine } from '../../../emu/machine.mjs';
import { newSynth, soundFont, check, done, rms, peak, peakHz, power, win, midiHz, RATE, writeWav, B } from './lib.mjs';

// ---------------------------------------------------------------- the MPU-401 registers
{
  const m = new Machine({});
  const st = () => m.in8(0x331), data = () => m.in8(0x330);
  check(st() === 0xBF, 'status after power-on: ready, nothing to read (BFh)', st().toString(16));
  check(data() === 0xFF, 'data port reads FFh when empty');
  m.out8(0x331, 0xFF);
  check((st() & 0x80) === 0 && data() === 0xFE && st() === 0xBF, 'reset (FFh) -> ACK FEh, then empty');
  check(!!(m.pic.irr & (1 << 9)) === false, 'IRQ 9 lowered once the ACK was read');
  m.out8(0x331, 0xAC);
  check(data() === 0xFE && data() === 0x15, 'ACh (version) -> FEh 15h');
  m.out8(0x331, 0xAD);
  check(data() === 0xFE && data() === 0x01, 'ADh (revision) -> FEh 01h');
  m.out8(0x331, 0xFF); m.in8(0x330);
  let raised = false;
  const oldRaise = m.pic.raise.bind(m.pic);
  m.pic.raise = (n) => { if (n === 9) raised = true; oldRaise(n); };
  m.out8(0x331, 0x3F);
  check(raised && m.mpu.uart && data() === 0xFE, '3Fh -> ACK FEh (IRQ 9 raised), UART mode');
  const bytes = [];
  m.mpu.onMidi = (t, b) => bytes.push(b);
  m.out8(0x330, 0x90); m.out8(0x330, 60); m.out8(0x330, 100);
  check(bytes.join() === '144,60,100', 'UART mode: data bytes go out as MIDI');
  m.out8(0x331, 0x3F);
  check(st() === 0xBF, 'UART mode: commands other than FFh are ignored (no ACK)');
  m.out8(0x331, 0xFF);
  check(st() === 0xBF && !m.mpu.uart, 'UART mode: FFh leaves UART mode without an ACK (as a real MPU-401)');
  m.out8(0x330, 0x90);
  check(bytes.length === 3, 'intelligent mode: data writes are not MIDI');
  m.out8(0x331, 0xFF);
  check(data() === 0xFE, 'second reset -> ACK');
  const m2 = new Machine({ mpu: { present: false } });
  check(m2.in8(0x331) === 0xFF && m2.in8(0x330) === 0xFF, 'MPU switched off: ports float (FFh)');
}

// ---------------------------------------------------------------- the synthesizer through the machine
function machineWithSynth() {
  const chunks = [];
  const syn = newSynth(RATE);
  const m = new Machine({ mpu: { synth: syn } });
  for (const r of [0x30, 0x31, 0x34, 0x35]) m.sb.mixer[r] = 0xF8;     // what SBMIX /INIT does at boot: 0 dB
  m.audio.start(RATE, (l, r) => chunks.push([l, r]));
  m.out8(0x331, 0x3F);
  const send = (...b) => { for (const x of b) m.out8(0x330, x); };
  const run = (ms) => m.runFor(ms);
  const audio = () => { m.audio.pump(); const n = chunks.reduce((a, [l]) => a + l.length, 0), L = new Float32Array(n), R = new Float32Array(n); let o = 0; for (const [l, r] of chunks) { L.set(l, o); R.set(r, o); o += l.length; } return { L, R }; };
  return { m, syn, send, run, audio };
}

// the machine is idle (no ROM): time advances; notes at exact emulated times
{
  const { syn, send, run, audio, m } = machineWithSynth();
  run(100);
  send(0xC0, 0);                       // piano
  send(0x90, 69, 100); run(1000);      // A4 for 1 s from t = 100 ms
  send(0x80, 69, 0); run(1500);
  send(0xC0, 19); send(0x90, 57, 100); run(1000); send(0x90, 57, 0); run(1000);   // church organ A3 (running status note off)
  const { L, R } = audio();
  writeWav(B('midi-test/synth-notes.wav'), L, R);
  // onset
  let onset = -1; for (let i = 0; i < L.length; i++) if (Math.abs(L[i]) > 1e-3) { onset = i / RATE; break; }
  check(Math.abs(onset - 0.100) < 0.004, 'note starts at its emulated time (100 ms)', `${(onset * 1000).toFixed(1)} ms`);
  const f1 = peakHz(win(L, 0.3, 0.9), 200, 1000);
  check(Math.abs(f1 - 440) < 1.5, 'piano A4 = 440 Hz', `${f1.toFixed(2)} Hz`);
  const lv = rms(win(L, 0.15, 0.35)), later = rms(win(L, 0.8, 1.0)), after = rms(win(L, 1.9, 2.4));
  check(lv > 0.01 && lv < 0.5, 'piano level sane', `rms ${lv.toFixed(3)}, peak ${peak(win(L, 0.1, 1.1)).toFixed(3)}`);
  check(later < lv, 'piano decays while held', `${later.toFixed(4)} < ${lv.toFixed(4)}`);
  check(after < 0.002, 'released: silent after the release', `rms ${after.toFixed(5)}`);
  const f2 = peakHz(win(L, 2.9, 3.5), 100, 500);
  check(Math.abs(f2 - 220) < 1, 'church organ A3 = 220 Hz', `${f2.toFixed(2)} Hz`);
  const o1 = rms(win(L, 2.9, 3.1)), o2 = rms(win(L, 3.4, 3.6));
  check(Math.abs(o1 / o2 - 1) < 0.15, 'organ sustains at a steady level', `${o1.toFixed(4)} / ${o2.toFixed(4)}`);
  check(rms(win(L, 3.8, 4.5)) < 0.002, 'organ released by a running-status note-on with velocity 0');
  run(3000);
  check(syn.noteCount === 2 && syn.activeVoices === 0, 'two notes, no voice left after the releases', `${syn.noteCount} notes, ${syn.activeVoices} voices`);
  void R; void m;
}

// pitch across the range, many programs
{
  const syn = newSynth(RATE);
  const cases = [[0, 36], [0, 60], [0, 84], [24, 52], [32, 40], [40, 76], [48, 62], [56, 67], [73, 79], [80, 64], [19, 45], [16, 48]];
  let ok = 0; const bad = [];
  for (const [p, k] of cases) {
    syn.reset();
    const n = RATE * 1.2, L = new Float32Array(n), R = new Float32Array(n);
    syn.midiBytes([0xC0, p, 0x90, k, 100]); syn.render(L, R, 0, n);
    const f0 = midiHz(k);
    const x = win(L, 0.25, 1.1);
    // the fundamental must be a strong spectral line (within 3 cents) - some instruments peak at a harmonic
    const f = peakHz(x, f0 * 0.97, f0 * 1.03);
    const cents = 1200 * Math.log2(f / f0);
    const strong = power(x, f) > 30 * power(x, f0 * 1.06);
    if (Math.abs(cents) < 8 && strong) ok++; else bad.push(`${p}/${k}: ${f.toFixed(2)} Hz (${cents.toFixed(1)} c)`);
  }
  check(ok === cases.length, `pitch of ${cases.length} program/key pairs within 8 cents (vibrato, detuned layers)`, bad.join(', '));
}

// velocity, volume, expression, pan, pitch bend (RPN 0), sustain, drums
{
  const syn = newSynth(RATE);
  const note = (msgs, dur = 0.6) => {
    syn.reset();
    const n = Math.round(RATE * dur), L = new Float32Array(n), R = new Float32Array(n);
    syn.midiBytes(msgs); syn.render(L, R, 0, n);
    return { L, R };
  };
  const base = note([0xC0, 19, 0x90, 60, 100]), soft = note([0xC0, 19, 0x90, 60, 40]);
  const rb = rms(win(base.L, 0.2, 0.5)), rs = rms(win(soft.L, 0.2, 0.5));
  check(rs < rb * 0.6, 'velocity 40 is quieter than 100', `${(20 * Math.log10(rs / rb)).toFixed(1)} dB`);
  const v64 = note([0xC0, 19, 0xB0, 7, 64, 0x90, 60, 100]);
  const dv = 20 * Math.log10(rms(win(v64.L, 0.2, 0.5)) / rb);
  check(Math.abs(dv - 40 * Math.log10(64 / 100)) < 1, 'CC 7 = 64 vs 100: -7.7 dB (the GM curve, 40 log10)', `${dv.toFixed(1)} dB`);
  const e0 = note([0xC0, 19, 0xB0, 11, 0, 0x90, 60, 100]);
  check(rms(win(e0.L, 0.2, 0.5)) < rb * 0.01, 'CC 11 = 0 silences');
  const left = note([0xC0, 19, 0xB0, 10, 0, 0x90, 60, 100]), right = note([0xC0, 19, 0xB0, 10, 127, 0x90, 60, 100]);
  const lr = (x) => rms(win(x.L, 0.2, 0.5)) / Math.max(1e-9, rms(win(x.R, 0.2, 0.5)));
  check(lr(left) > 8 && lr(right) < 0.125, 'CC 10 pans hard left / right', `L/R ${lr(left).toFixed(1)} / ${lr(right).toFixed(3)}`);
  // bend range 12 via RPN 0, bend fully up: A4 -> A5
  const bend = note([0xC0, 19, 0xB0, 101, 0, 0xB0, 100, 0, 0xB0, 6, 12, 0xB0, 38, 0, 0xE0, 0x7F, 0x7F, 0x90, 69, 100], 0.8);
  const fb = peakHz(win(bend.L, 0.3, 0.75), 600, 1100);
  check(Math.abs(1200 * Math.log2(fb / 880)) < 5, 'pitch bend up with RPN 0 range 12 = one octave', `${fb.toFixed(1)} Hz`);
  const bd = note([0xC0, 19, 0xE0, 0, 0x20, 0x90, 69, 100], 0.8);          // bend -> 0x1000 = -2 semitones * 0.5
  const fd = peakHz(win(bd.L, 0.3, 0.75), 300, 500);
  check(Math.abs(1200 * Math.log2(fd / 440) + 100) < 5, 'pitch bend 1000h with the default range = -1 semitone', `${fd.toFixed(1)} Hz`);
  // sustain pedal holds a released note until the pedal goes up
  syn.reset();
  let n = RATE, L = new Float32Array(n * 2), R = new Float32Array(n * 2);
  syn.midiBytes([0xC0, 19, 0xB0, 64, 127, 0x90, 60, 100]); syn.render(L, R, 0, RATE / 4);
  syn.midiBytes([0x80, 60, 0]); syn.render(L, R, RATE / 4, RATE / 2);
  const held = rms(win(L, 0.6, 0.75));
  syn.midiBytes([0xB0, 64, 0]); syn.render(L, R, RATE * 0.75, RATE * 1.25);
  check(held > 0.01 && rms(win(L, 1.5, 2.0)) < 0.002, 'sustain pedal (CC 64) holds the note, pedal up releases it', `held rms ${held.toFixed(3)}`);
  // drums: channel 10, bass drum 36, snare 38, closed hi-hat 42 (exclusive class with the open hi-hat 46)
  const kick = note([0x99, 36, 120], 0.5), hat = note([0x99, 42, 120], 0.5);
  const lowK = power(win(kick.L, 0.0, 0.15), 60) + power(win(kick.L, 0.0, 0.15), 80), highK = power(win(kick.L, 0.0, 0.15), 5000);
  const lowH = power(win(hat.L, 0.0, 0.1), 80), highH = power(win(hat.L, 0.0, 0.1), 7000) + power(win(hat.L, 0.0, 0.1), 5000);
  check(rms(win(kick.L, 0, 0.15)) > 0.01 && lowK > 20 * highK, 'channel 10 note 36 is a bass drum (low)', `rms ${rms(win(kick.L, 0, 0.15)).toFixed(3)}`);
  check(rms(win(hat.L, 0, 0.1)) > 0.003 && highH > lowH, 'channel 10 note 42 is a hi-hat (high)', `rms ${rms(win(hat.L, 0, 0.1)).toFixed(3)}`);
  syn.reset();
  L = new Float32Array(RATE); R = new Float32Array(RATE);
  syn.midiBytes([0x99, 46, 120]); syn.render(L, R, 0, RATE / 5);
  const openVoices = syn.activeVoices;
  syn.midiBytes([0x99, 42, 120]); syn.render(L, R, RATE / 5, RATE / 10);
  const openLeft = syn.voices.filter((v) => v.on && v.key === 46 && !v.released).length;
  check(openVoices > 0 && openLeft === 0, 'closed hi-hat cuts the open hi-hat (exclusive class)');
  // GS drum part via SysEx on channel 11, then GM reset gives channel 11 back to melody
  syn.reset();
  syn.midiBytes([0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x1A, 0x15, 0x01, 0x10, 0xF7]);
  check(syn.ch[10].drum === true, 'GS "use for rhythm part" makes channel 11 a drum part');
  syn.midiBytes([0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7]);
  check(syn.ch[10].drum === false && syn.ch[9].drum === true, 'GM System On resets the parts');
  // bank select: a GS variation falls back to the capital tone
  syn.midiBytes([0xB0, 0, 8, 0xC0, 4]);
  syn.noteOn(syn.ch[0], 60, 100);
  check(syn.ch[0].preset && syn.ch[0].preset.bank === 8 && syn.ch[0].preset.program === 4, 'bank 8 program 4 is a GS variation tone', syn.ch[0].preset && syn.ch[0].preset.name);
  syn.midiBytes([0xB0, 0, 99, 0xC0, 4]); syn.noteOn(syn.ch[0], 60, 100);
  check(syn.ch[0].preset && syn.ch[0].preset.bank === 0 && syn.ch[0].preset.program === 4, 'unknown bank 99 falls back to bank 0');
  // polyphony: 80 notes -> 64 voices, no crash, still sounds
  syn.reset();
  L = new Float32Array(RATE); R = new Float32Array(RATE);
  for (let i = 0; i < 80; i++) syn.midiBytes([0xC0 | (i & 7), 48, 0x90 | (i & 7), 30 + i, 90]);
  const t0 = performance.now(); syn.render(L, R, 0, RATE); const ms = performance.now() - t0;
  check(syn.activeVoices <= 64 && rms(L) > 0.01, 'voice stealing at 64 voices', `${syn.activeVoices} voices, 1 s rendered in ${ms.toFixed(0)} ms`);
  check(ms < 300, 'CPU: 64 voices render faster than 3x real time', `${ms.toFixed(0)} ms per second`);
}

// the SB16 mixer's MIDI (FM) volume scales the synth; remote mode passes the bytes with the chunk
{
  const levels = [];
  for (const fm of [0xF8, 0xB8]) {
    const { m, send, run, audio } = machineWithSynth();
    m.sb.mixer[0x30] = m.sb.mixer[0x31] = 0xF8; m.sb.mixer[0x34] = m.sb.mixer[0x35] = fm;
    send(0xC0, 19, 0x90, 60, 100); run(500);
    levels.push(rms(win(audio().L, 0.2, 0.5)));
  }
  const d = 20 * Math.log10(levels[1] / levels[0]);
  check(Math.abs(d + 16) < 1, 'mixer FM/MIDI volume 23 vs 31 = -16 dB on the synth', `${d.toFixed(1)} dB`);
  const got = [];
  const m = new Machine({ mpu: { remote: true } });
  for (const r of [0x30, 0x31, 0x34, 0x35]) m.sb.mixer[r] = 0xF8;
  m.audio.start(48000, (l, r, midi) => { if (midi) got.push(midi); });
  m.out8(0x331, 0x3F); m.in8(0x330);
  m.runFor(10); m.out8(0x330, 0x90); m.runFor(5); m.out8(0x330, 60); m.out8(0x330, 100); m.runFor(30);
  const ev = got.flatMap((g) => [...g.midi]);
  check(ev.length === 3 && (ev[0] & 511) === 0x90 && (ev[2] & 511) === 100, 'remote mode: the MIDI bytes come with the audio chunk', ev.map((e) => `${e >> 9}:${(e & 511).toString(16)}`).join(' '));
  check(got.length > 0 && got.every((g) => Math.abs(g.gl - 1) < 1e-6 && Math.abs(g.gr - 1) < 1e-6), 'remote mode: the chunk carries the mixer gains (0 dB)');
}
void soundFont;
done();
