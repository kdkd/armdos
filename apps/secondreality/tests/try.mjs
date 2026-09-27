// manual runner (debugging aid): node apps/secondreality/tests/try.mjs "\\DOS\\ELBOW [/TRACE] SECOND" [ms]
// env: KEYS (typed after KEYWAIT ms, e.g. '{LEFT}{ENTER}' = Sound Blaster Pro), SHOTS (PNGs to OUT),
// MUS (STMIK's position), PIC, STATE (x86 registers), VGADUMP, VRAMOUT, IVT, AUDIO (WAV file),
// SBLOG, CRASHWATCH, WATCHISR, DUMPMEM=seg:off:len,..., MHZ, TAG (parallel runs), E9 (debug tail)
import fs from 'node:fs';
import { session } from '../../dosutil/tests/harness.mjs';
import { x86state, x86mem, x86ring } from '../../x86/tests/x86state.mjs';
// the files: 3rdparty/secondreality (default), or SR=<the source release's MAIN directory>
const files = [{ src: 'build/ELBOW.EXE', dst: 'DOS\\' }, { src: 'build/HIMEM.SYS', dst: 'DOS\\' }];
if (process.env.SR) {
  const SR = process.env.SR, stage = 'build/sr-stage' + (process.env.TAG || '');   // (mkimage wants paths under the project)
  fs.rmSync(stage, { recursive: true, force: true }); fs.mkdirSync(stage + '/DATA', { recursive: true });
  fs.copyFileSync(SR + '/' + (process.env.U2 || 'U2.EXE'), stage + '/SECOND.EXE');
  files.push({ src: stage + '/SECOND.EXE', dst: 'SR\\' });
  for (const f of fs.readdirSync(SR + '/DATA')) { fs.copyFileSync(SR + '/DATA/' + f, stage + '/DATA/' + f); files.push({ src: stage + '/DATA/' + f, dst: 'SR\\' }); }
} else for (const f of fs.readdirSync('3rdparty/secondreality')) files.push({ src: '3rdparty/secondreality/' + f, dst: 'SR\\' });
const s = await session({ name: 'srtry' + (process.env.TAG || ''), files, dirs: ['SR', 'SR\\DATA'], sizeMB: 64, boot: process.env.MHZ ? { mhz: +process.env.MHZ } : undefined,
  config: 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=30\nSHELL=C:\\T\\TSHELL.EXE\n' });
s.pc.type('CD \\SR\r'); s.waitPrompt();
const chunks = [];
if (process.env.SBLOG) { const sb = s.pc.machine.sb, oc = sb.command.bind(sb); let n = 0; sb.command = (c, a) => { if (n++ < +process.env.SBLOG) console.log('SB', (s.pc.machine.timeNs() / 1e6).toFixed(1), c.toString(16), a.map((x) => x.toString(16)).join(' ')); return oc(c, a); };
  const d = s.pc.machine.dma, ow = d.write.bind(d); let k = 0; d.write = (port, v) => { if (k++ < +process.env.SBLOG) console.log('DMA', port.toString(16), v.toString(16)); return ow(port, v); }; }
if (process.env.AUDIO) s.pc.machine.audio.start(22050, (l, r) => chunks.push(Float32Array.from(l), Float32Array.from(r)));
s.pc.type(process.argv[2] + '\r');
const ms = +(process.argv[3] || 5000);
if (process.env.KEYS) { s.pc.run(+(process.env.KEYWAIT || 3000)); s.pc.type(process.env.KEYS); }
const shots = +(process.env.SHOTS || 1);
if (process.env.CRASHWATCH) {
  const from = +process.env.CRASHWATCH;
  s.pc.run(Math.max(0, from - (s.pc.timeMs - 0)));
  for (let t = 0; t < ms; t += 5) {
    s.pc.run(5);
    const st = x86state(s.pc);
    const m = st.match(/CS=([0-9a-f]+)/);
    if (m && parseInt(m[1], 16) < 0x100) {
      console.log('CRASH at', s.pc.timeMs); console.log(st);
      const ss = parseInt(st.match(/SS=([0-9a-f]+)/)[1], 16), sp = parseInt(st.match(/ESP=([0-9a-f]+)/)[1], 16) & 0xFFFF;
      console.log('stack', x86mem(s.pc, ss * 16 + sp, 32).toString('hex'));
      console.log('E9 tail:', s.pc.debug.slice(-6000));
      process.exit(0);
    }
  }
}
if (process.env.WATCHISR) {
  const m = s.pc.machine; let since = -1;
  for (let t = 0; t < ms; t++) {
    s.pc.run(1);
    if (m.pic.isr & 1) { if (since < 0) since = t; else if (t - since > 60) { console.log('ISR stuck since', since, 'now', s.pc.timeMs); console.log(x86state(s.pc)); if (process.env.RING) console.log(x86ring(s.pc)); break; } }
    else since = -1;
  }
}
for (let k = 0; k < shots; k++) {
  s.pc.run(ms / shots);
  console.log('--- t=' + s.pc.timeMs.toFixed(0) + ' mode=' + s.pc.machine.vga.mode.toString(16));
  if (!process.env.NOSCREEN) console.log(s.pc.screen());
  if (process.env.MUS) { const b = x86mem(s.pc, 0x14020 + 0x8f7, 14); console.log('stmik zinfo/zplus/zframe/row/pat/ord', [0,2,4,6,8,10].map((k) => b.readUInt16LE(k).toString(16)).join(' '), 'mode', x86mem(s.pc, 0x14020+0x882, 2).readUInt16LE(0)); }
  if (process.env.PIC) { const m = s.pc.machine, c = m.pit.ch[0]; console.log('pic irr', m.pic.irr.toString(16), 'isr', m.pic.isr.toString(16), 'imr', m.pic.imr.toString(16), 'pit0 mode', c.mode, 'N', c.reload, 'armed', c.armed, 'due', m.pit.due, 'now', m.timeNs(), 'cpsrI', s.pc.cpu.i); }
  if (process.env.IVT) { const v = x86mem(s.pc, 0, 0x400); for (const n of (process.env.IVT).split(',')) { const k = parseInt(n, 16); const off = v.readUInt16LE(k * 4), seg = v.readUInt16LE(k * 4 + 2); console.log('IVT', n, seg.toString(16) + ':' + off.toString(16), x86mem(s.pc, seg * 16 + off, 24).toString('hex')); } }
  if (process.env.VGADUMP) { const v = s.pc.machine.vga; let nz = 0; for (const b of v.vram) if (b) nz++; let dz = 0; for (const b of v.dac) if (b) dz++; let rz = 0; for (let i = 0xA0000; i < 0xB0000; i++) if (s.pc.machine.cpu.m8[i]) rz++;
    console.log('vga mode', v.mode.toString(16), 'kind', v.displayKind(), 'window', v.window, 'seq', [...v.seq].map((x) => x.toString(16)).join(' '), 'gc', [...v.gc].map((x) => x.toString(16)).join(' '), 'crtc', [...v.crtc].map((x) => x.toString(16)).join(' '), 'attr', [...v.attr].map((x) => x.toString(16)).join(' '), 'vram nz', nz, 'dac nz', dz, 'ram nz', rz, 'pel', v.pelMask, 'mmioSeg', s.pc.machine.cpu.mmioSeg); }
  if (process.env.STATE) for (let j = 0; j < 3; j++) { console.log(x86state(s.pc)); s.pc.run(7); }
  await s.pc.png(`${process.env.OUT || 'build'}/sr${k}.png`);
}
if (process.env.VRAMOUT) { fs.writeFileSync(process.env.VRAMOUT, s.pc.machine.vga.vram); fs.writeFileSync(process.env.VRAMOUT + '.snap', Buffer.concat([Buffer.from(s.pc.machine.vga.crtc), Buffer.from(s.pc.machine.vga.dac)])); }
if (process.env.AUDIO) {
  let n = 0, sum = 0, peak = 0;
  for (let i = 0; i < chunks.length; i += 2) for (const v of chunks[i]) { n++; sum += v * v; peak = Math.max(peak, Math.abs(v)); }
  console.log('audio', n, 'samples, rms', Math.sqrt(sum / Math.max(1, n)).toFixed(4), 'peak', peak.toFixed(3));
  const L = [], R = [];
  for (let i = 0; i < chunks.length; i += 2) { L.push(...chunks[i]); R.push(...chunks[i + 1]); }
  const buf = Buffer.alloc(44 + L.length * 4);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + L.length * 4, 4); buf.write('WAVEfmt ', 8); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20); buf.writeUInt16LE(2, 22);
  buf.writeUInt32LE(22050, 24); buf.writeUInt32LE(22050 * 4, 28); buf.writeUInt16LE(4, 32); buf.writeUInt16LE(16, 34); buf.write('data', 36); buf.writeUInt32LE(L.length * 4, 40);
  for (let i = 0; i < L.length; i++) { buf.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(L[i] * 32767))), 44 + i * 4); buf.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(R[i] * 32767))), 46 + i * 4); }
  fs.writeFileSync(process.env.AUDIO, buf);
}
if (process.env.DUMPMEM) for (const d of process.env.DUMPMEM.split(',')) { const [sg, of, n] = d.split(':').map((x) => parseInt(x, 16)); console.log('MEM', d, x86mem(s.pc, sg * 16 + of, n).toString('hex')); }
if (s.pc.faults.length) console.log('faults', s.pc.faults);
if (s.pc.debug) console.log('E9:', s.pc.debug.slice(-(+process.env.E9 || 4000)));
