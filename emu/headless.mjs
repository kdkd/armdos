#!/usr/bin/env node
// ARM-DOS headless runner (ARCH.md §13).
//
// node emu/headless.mjs --rom build/rom.bin [--hd hd.img] [--fd a.img] [--fd-wp]
//      [--keys "DIR\r{F1}"] [--wait-text "C:\>"] [--until-idle] [--max-seconds 60]
//      [--screen] [--png out.png] [--serial] [--exit-on-port] [--mips]
//      [--trace N] [--mhz 100] [--no-jit] [--rtc 2026-01-01T12:00:00] [--save-hd out.img]
//      [--preload-font]   (copy emu/fonts/vga8x16.bin into the font RAM at power-on)
//      [--wav out.wav] [--wav-rate 44100]   (record the machine's audio: SB16, OPL3, PC speaker)
//      [--sf2 bank.sf2]   (the MPU-401's GM synth sound set; with --wav, 3rdparty/midi/ARMGS.SFA by default)
//      [--ram MB] [--sound sb16|adlib|none] [--video vga|hercules] [--no-mouse] [--no-modem] [--no-cdrom] [--no-midi] [--no-joystick] [--stick]
//                         (the cards and SIMMs: Machine.setHardware)
//
// Keys are typed after --wait-text appears, or (without --wait-text) as soon as
// the machine first goes idle. --until-idle stops once the keys are consumed and
// the CPU sits in WFI. Exit status: the 0xF4 exit code with --exit-on-port,
// 3 on timeout while waiting, 0 otherwise.

import { writeFileSync, readFileSync, existsSync } from 'node:fs';
import { boot, formatTrace, formatRegs } from './testkit.mjs';

const argv = process.argv.slice(2);
const flags = new Set(), val = {};
const takesValue = new Set(['rom', 'hd', 'fd', 'keys', 'wait-text', 'max-seconds', 'png', 'trace', 'mhz', 'rtc', 'save-hd', 'save-fd', 'wav', 'wav-rate', 'sf2', 'ram', 'sound', 'video']);
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (!a.startsWith('--')) { console.error('unexpected argument ' + a); process.exit(2); }
  const k = a.slice(2);
  if (takesValue.has(k)) val[k] = argv[++i]; else flags.add(k);
}
if (flags.has('help') || !val.rom) {
  console.log('usage: node emu/headless.mjs --rom rom.bin [--hd img] [--fd img] [--keys text] [--wait-text text] [--until-idle]\n' +
    '       [--max-seconds s] [--screen] [--png file] [--serial] [--exit-on-port] [--mips] [--trace N] [--mhz n] [--no-jit] [--rtc iso]\n' +
    '       [--save-hd file] [--save-fd file] [--fd-wp] [--preload-font] [--wav file] [--wav-rate hz] [--sf2 file]\n' +
    '       [--ram MB] [--sound sb16|adlib|none] [--video vga|hercules] [--no-mouse] [--no-modem] [--no-cdrom] [--no-midi] [--no-joystick] [--stick]');
  process.exit(flags.has('help') ? 0 : 2);
}
const unescape = (s) => s.replace(/\\r/g, '\r').replace(/\\n/g, '\r').replace(/\\t/g, '\t').replace(/\\e/g, '\x1b');
const echo = flags.has('serial');
const traceN = val.trace ? +val.trace : 0;
let faultsShown = 0;
const wavChunks = [], wavRate = val['wav-rate'] ? +val['wav-rate'] : 44100;

// the MPU-401's General MIDI synth (dev/mpu401.mjs, dev/gmsynth.mjs) when audio is recorded
let mpu;
{
  const sfPath = val.sf2 || new URL('../3rdparty/midi/ARMGS.SFA', import.meta.url).pathname;
  if (val.wav && existsSync(sfPath)) {
    const { parseSf2 } = await import('./dev/sf2.mjs'), { GmSynth } = await import('./dev/gmsynth.mjs');
    const synth = new GmSynth(wavRate); synth.loadSoundFont(parseSf2(readFileSync(sfPath)));
    mpu = { synth };
  }
}
if (flags.has('no-midi')) mpu = { ...mpu, present: false };
const pc = await boot({
  mpu,
  ram: val.ram ? +val.ram : undefined, sound: val.sound, video: val.video,
  mouse: flags.has('no-mouse') ? false : undefined, modem: flags.has('no-modem') ? false : undefined, cdrom: flags.has('no-cdrom') ? false : undefined,
  joystick: flags.has('no-joystick') ? false : undefined,
  rom: val.rom, hd: val.hd, fd: val.fd, fdWriteProtected: flags.has('fd-wp'),
  mhz: val.mhz ? +val.mhz : undefined, jit: !flags.has('no-jit'), trace: traceN, preloadFont: flags.has('preload-font'),
  rtcBaseMs: val.rtc ? new Date(val.rtc).getTime() : Date.now(),
  onSerial: (b) => { if (echo) process.stdout.write(String.fromCharCode(b)); },
  onDebug: (b) => { if (echo) process.stdout.write(String.fromCharCode(b)); },
  audio: val.wav ? { rate: wavRate, speaker: true, onAudio: (l, r) => wavChunks.push([l, r]) } : undefined,
  onFault: (kind, at, addr) => {
    if (!traceN || faultsShown >= 3) return;
    faultsShown++;
    process.stderr.write(`\n*** ${kind} ${kind === 'data' ? 'abort' : kind === 'prefetch' ? 'abort' : 'instruction'} at pc=${(at >>> 0).toString(16)} addr=${(addr >>> 0).toString(16)}\n`);
    process.stderr.write(formatTrace(pc.cpu, traceN).join('\n') + '\n' + formatRegs(pc.cpu) + '\n');
  },
});
const m = pc.machine;
if (flags.has('stick')) m.joy.plug(0);          // a joystick in the game port, centred
const maxMs = (val['max-seconds'] ? +val['max-seconds'] : 60) * 1000;
const keys = val.keys !== undefined ? unescape(val.keys) : null;
const waitText = val['wait-text'] !== undefined ? unescape(val['wait-text']) : null;
const t0 = performance.now();
let status = 0, typed = false;

// phase 1: wait for text or first idle, then type
if (keys !== null || waitText !== null) {
  let ok;
  if (waitText !== null) ok = pc.waitText(waitText, { timeoutMs: maxMs });
  else ok = pc.waitIdle({ timeoutMs: maxMs, quietMs: 200 });
  if (!ok && !m.stopped) { process.stderr.write(`timeout waiting for ${waitText !== null ? JSON.stringify(waitText) : 'idle'}\n`); status = 3; }
  if (keys !== null && ok) { pc.type(keys); typed = true; }
}
// phase 2: run until idle / exit / time limit
const remaining = () => Math.max(0, maxMs - m.timeMs());
if (!m.stopped && status === 0) {
  if (flags.has('until-idle')) {
    if (!pc.waitIdle({ timeoutMs: remaining() })) { if (!m.stopped) { process.stderr.write('timeout waiting for idle\n'); status = 3; } }
  } else if (flags.has('exit-on-port')) {
    pc.waitExit({ timeoutMs: remaining() });
    if (!m.stopped) { process.stderr.write('timeout waiting for exit port\n'); status = 3; }
  } else if (!typed && keys === null && waitText === null) {
    pc.run(remaining());
  } else if (typed) {
    pc.until(() => m.typingDone(), { timeoutMs: remaining() });
    pc.run(Math.min(200, remaining()));
  }
}
const t1 = performance.now();
if (status === 3 && traceN) process.stderr.write('last instructions:\n' + formatTrace(pc.cpu, traceN).join('\n') + '\n' + formatRegs(pc.cpu) + '\n');

if (flags.has('screen')) process.stdout.write((echo ? '\n' : '') + pc.lines().map((l) => l.replace(/\s+$/, '')).join('\n').replace(/\n+$/, '') + '\n');
if (val.png) await pc.png(val.png);
if (val['save-hd'] && m.ata.img) writeFileSync(val['save-hd'], m.ata.img);
if (val['save-fd'] && m.fdc.img) writeFileSync(val['save-fd'], m.fdc.img);
if (val.wav) { m.audio.pump(); writeFileSync(val.wav, wavFile(wavChunks, wavRate)); }
if (flags.has('mips')) {
  const insns = m.cpu.icount, ms = t1 - t0;
  process.stderr.write(`emulated ${(m.timeMs() / 1000).toFixed(3)} s, ${(insns / 1e6).toFixed(1)}M instructions, host ${ms.toFixed(0)} ms, ` +
    `${(insns / ms / 1000).toFixed(1)} MIPS (halted ${(100 * m.haltedNs / Math.max(1, m.timeNs())).toFixed(1)}% of emulated time)\n`);
}
if (flags.has('exit-on-port') && m.stopped) status = m.exitCode;
process.exit(status);

/** 16-bit stereo PCM WAV from [left, right] Float32Array chunks. */
function wavFile(chunks, rate) {
  const n = chunks.reduce((a, [l]) => a + l.length, 0);
  const b = Buffer.alloc(44 + n * 4);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 4, 4); b.write('WAVEfmt ', 8);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22); b.writeUInt32LE(rate, 24);
  b.writeUInt32LE(rate * 4, 28); b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 4, 40);
  let o = 44;
  const s16 = (v) => Math.max(-32768, Math.min(32767, Math.round(v * 32767)));
  for (const [l, r] of chunks) for (let i = 0; i < l.length; i++) { b.writeInt16LE(s16(l[i]), o); b.writeInt16LE(s16(r[i]), o + 2); o += 4; }
  return b;
}
