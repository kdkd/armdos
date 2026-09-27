#!/usr/bin/env node
// apps/duke3d/tests/gm.mjs - Duke Nukem 3D's music as General MIDI on the MPU-401: Jim Dose's
// MPU401.C (the Apogee Sound System's own MPU-401 driver, from the 3D Realms AudioLib
// release) behind MUSIC.C's "General MIDI" device, chosen by CONFIG.C when an MPU-401 answers
// at BLASTER's P port. The MPU is wired to the GM synth (emu/dev/gmsynth.mjs).
//  * the MPU gets reset + UART mode (3Fh) at start-up;
//  * the title music: the note-ons sent to the MPU follow GRABBAG.MID from DUKE3D.GRP
//    (channel and key, in order; tempo within 2%) and the synth makes it audible;
//  * the sound effects still come from the SB16 (DSP programmed);
//  * no MPU-401 (the page setting "off"): FM music on the OPL3 as before.
import fs from 'node:fs';
import path from 'node:path';
import { makeImage, B, ROOT, mode } from './lib.mjs';
import { newSynth, parseStream, check, done, writeWav, rms, win, RATE } from '../../midi/tests/lib.mjs';
import { readSmf } from '../../midi/tests/smf.mjs';
import { boot } from '../../../emu/testkit.mjs';

const OUT = B('duke3d-test/gm');
fs.mkdirSync(OUT, { recursive: true });
const grp = fs.readFileSync(path.join(ROOT, '3rdparty/duke3d/DUKE3D.GRP'));
function grpFile(name) {
  const n = grp.readUInt32LE(12); let off = 16 + n * 16;
  for (let i = 0; i < n; i++) {
    const nm = grp.toString('latin1', 16 + i * 16, 28 + i * 16).replace(/\0+$/, ''), sz = grp.readUInt32LE(28 + i * 16);
    if (nm === name) return grp.subarray(off, off + sz);
    off += sz;
  }
  return null;
}

async function duke(name, withMpu) {
  const hd = makeImage(OUT, name, { autoexec: 'SET BLASTER=A220 I7 D1 H5 P330 T6' });
  const chunks = [], midi = [], cmds = [];
  const synth = withMpu ? newSynth(RATE) : null;
  const pc = await boot({ rom: B('rom.bin'), hd, audio: { rate: RATE, speaker: false, onAudio: (l, r) => chunks.push([l, r]) },
    mpu: withMpu ? { synth, onMidi: (t, b) => midi.push([t / 1e6, b]) } : { present: false } });
  const w = pc.machine.mpu.write.bind(pc.machine.mpu);
  pc.machine.mpu.write = (port, v) => { if (port & 1) cmds.push(v); return w(port, v); };
  const sbw = pc.machine.sb.write.bind(pc.machine.sb); let dsp = 0;
  pc.machine.sb.write = (port, v) => { if ((port & 0xF) === 0xC) dsp++; return sbw(port, v); };
  const opl = pc.machine.sb.opl, ow = opl.write.bind(opl); let keyOns = 0;
  opl.write = (off, v) => { if ((off & 1) && ((opl.addr[off >> 1] & 0xF0) === 0xB0) && opl.addr[off >> 1] !== 0xBD && (v & 0x20)) keyOns++; return ow(off, v); };
  pc.waitText('C:\\>', { timeoutMs: 20000 });
  pc.type('CD \\GAMES\\DUKE3D\rDUKE3D\r');
  pc.until(() => mode(pc) === 0x13, { timeoutMs: 30000 });
  const t0 = pc.timeMs;
  pc.run(12000);
  const audio = () => { pc.machine.audio.pump(); const n = chunks.reduce((a, [l]) => a + l.length, 0), L = new Float32Array(n), R = new Float32Array(n); let o = 0; for (const [l, r] of chunks) { L.set(l, o); R.set(r, o); o += l.length; } return { L, R }; };
  return { pc, midi, cmds, synth, t0, audio, dsp: () => dsp, keyOns: () => keyOns };
}

{
  const d = await duke('gm', true);
  check(mode(d.pc) === 0x13, 'DUKE3D running (mode 13h)');
  check(d.cmds.includes(0xFF) && d.cmds.includes(0x3F) && d.pc.machine.mpu.uart, 'MPU401.C: reset, then UART mode', d.cmds.map((c) => c.toString(16)).join(' '));
  const ons = parseStream(d.midi).filter((m) => (m.st & 0xF0) === 0x90 && m.d2);
  const ref = readSmf(grpFile('GRABBAG.MID')).notes;
  // the first notes of the song, in order (channel + key), and the tempo (time span of the first 40)
  const k = Math.min(40, ons.length);
  let same = 0;
  for (let i = 0; i < k; i++) if ((ons[i].st & 15) === (ref[i].st & 15) && ons[i].d1 === ref[i].d1) same++;
  const span = ons[k - 1].ms - ons[0].ms, refSpan = (ref[k - 1].us - ref[0].us) / 1000;
  check(k === 40 && same === k, 'title music GRABBAG.MID: the first 40 note-ons in order (channel, key)', `${same}/${k}`);
  check(Math.abs(span / refSpan - 1) < 0.02, 'title music tempo as in the file', `${span.toFixed(0)} ms vs ${refSpan.toFixed(0)} ms`);
  check(d.keyOns() === 0, 'no OPL notes (music on the MPU)');
  check(d.dsp() > 10, 'sound effects on the SB16 (DSP programmed)', `${d.dsp()} DSP writes`);
  const { L, R } = d.audio();
  writeWav(path.join(OUT, 'duke3d-gm.wav'), L, R);
  const music = win(L, ons[0].ms / 1000 + 0.5, d.pc.timeMs / 1000);
  check(rms(music) > 0.01, 'the title music is audible through the GM synth', `rms ${rms(music).toFixed(3)}`);
  check(d.pc.faults.length === 0, 'no CPU faults');
}
{
  const d = await duke('nompu', false);
  check(d.keyOns() > 20 && d.midi.length === 0, 'no MPU-401: FM music on the OPL3', `${d.keyOns()} OPL key-ons`);
}
done();
