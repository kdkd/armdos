#!/usr/bin/env node
// apps/cdrom/tests/cdplay.mjs - CDPLAY.EXE on the Multimedia Sampler '93 (and a short test disc):
// the full-screen player (track list with CDPLAY.INI titles, LCD, play/pause/stop/next/prev/
// eject/shuffle/repeat, a disc change), the audio the machine renders (silent/non-silent at
// the right times, the right samples: cross-correlated with the track's WAV), and the pop-up
// (CDPLAY /R: Ctrl+Alt+C at the prompt through INT 28h and over ZORK, the screen restored
// exactly, refused in graphics with a beep, shuffle/repeat carrying on while it is closed, /U).
//   node apps/cdrom/tests/cdplay.mjs          (make cdrom-test)
// Writes build/cdrom-test/cdplay.wav (the first seconds of track 2 as the machine played it).
import fs from 'node:fs';
import path from 'node:path';
import { startPC, check, failed, B, lastLine, combo, tap } from './harness.mjs';
import { loadDisc, readWav } from '../tools/disc-node.mjs';
import { buildIso } from '../tools/iso9660.mjs';
import { CdDisc } from '../../../emu/dev/atapi.mjs';

const DISC = B('cdrom/sampler93');
const disc = loadDisc(DISC);
const RATE = 44100;
const pc = await startPC({
  name: 'cdplay', shell: 'command', lastdrive: 'E', cdrom: disc,
  files: [{ src: 'build/ARMCD.SYS', dst: 'DOS\\' }, { src: 'build/ARMCDEX.EXE', dst: 'DOS\\' },
          { src: 'build/CDPLAY.EXE', dst: 'DOS\\' }, { src: 'build/cdrom/sampler93/CDPLAY.INI', dst: 'DOS\\' },
          { src: 'build/SBMIX.EXE', dst: 'DOS\\' }, { src: 'build/MOUSE.COM', dst: 'DOS\\' }],
  extraConfig: 'DEVICE=C:\\DOS\\ARMCD.SYS /D:ARMCD001\n', autoexec: 'SBMIX /INIT /Q\nMOUSE\nC:\\DOS\\ARMCDEX /D:ARMCD001\n',   // SBMIX: the SB16's CD input at its standard level
});
check(pc.bootOk && pc.hasText('Drive D: = Driver ARMCD001 unit 0'), 'booted, ARMCDEX loaded from AUTOEXEC.BAT');
const scr = () => pc.screen();
const tick = () => new Promise((r) => setTimeout(r, 0));
/** run ms of emulated time, letting the (lazy) audio "decode" in between, as the page does */
const go = async (ms) => { for (let t = 0; t < ms; t += 20) { pc.run(20); await tick(); } };
const vram = () => Array.from(pc.m.cpu.m16.subarray(0xB8000 / 2, 0xB8000 / 2 + 2000));
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const attrOf = (word) => { const l = pc.lines(); for (let y = 0; y < l.length; y++) { const x = l[y].indexOf(word); if (x >= 0) return pc.m.cpu.m8[0xB8000 + (y * 80 + x) * 2 + 1]; } return -1; };
const lit = (word) => attrOf(word) === 0x0A;
const st = () => pc.m.cdrom.state();
const key = async (k, ms = 300) => { pc.type(k); pc.until(() => pc.m.typingDone(), { timeoutMs: 5000 }); await go(ms); };
const hotkey = async () => { combo(pc, ['ControlLeft', 'AltLeft', 'KeyC'], 300); await go(200); };

// ---------------------------------------------------------------- the audio capture
const blocks = [];            // [emulated ms at the block's start, rms, peak]
let cap = null;               // { from, left: [], right: [] } a raw window, for the WAV and correlation
let sampleNo = 0;
const t0ms = pc.timeMs;
pc.m.audio.start(RATE, (L, R) => {
  let s = 0, p = 0;
  for (let i = 0; i < L.length; i++) { s += L[i] * L[i] + R[i] * R[i]; p = Math.max(p, Math.abs(L[i]), Math.abs(R[i])); }
  blocks.push([t0ms + sampleNo / RATE * 1000, Math.sqrt(s / (2 * L.length)), p, L.length]);
  if (cap && cap.left.length < cap.max) { cap.left.push(...L); cap.right.push(...R); }
  sampleNo += L.length;
}, { speaker: false });
const level = (from, to) => {   // rms over emulated ms [from, to)
  let s = 0, n = 0;
  for (const [t, rms, , len] of blocks) if (t >= from && t < to) { s += rms * rms * len; n += len; }
  return n ? Math.sqrt(s / n) : -1;
};

// ---------------------------------------------------------------- /? and the full-screen player
pc.cmd('CLS'); pc.cmd('CDPLAY /?');
check(pc.hasText('Plays audio compact discs in the CD-ROM drive.') && pc.hasText('CDPLAY [/R | /U]'), 'CDPLAY /? help');
pc.cmd('CLS');
pc.type('CDPLAY\r');
check(pc.waitText('ARM-DOS CD Player', { timeoutMs: 10000 }), 'CDPLAY: the full-screen player');
await go(500);
let s = scr();
console.log(s);
// the disc's playing time is its lead-out (Red Book frames / 75); the data track's size, and
// so the lead-out, follows the programs on the disc (the compiler)
const total = Math.floor((disc.leadout + 150) / 75), totalMmss = `${Math.floor(total / 60)}:${String(total % 60).padStart(2, '0')}`;
check(s.includes("ARM-DOS Multimedia Sampler '93 - Kevin MacLeod") && new RegExp(`5 tracks\\s+\\S\\s+${totalMmss}`).test(s), `disc title and artist from CDPLAY.INI, 5 tracks, ${totalMmss}`);
check(/01\s+Data \(the files on this disc\)/.test(s) && /02\s+Cipher\s+3:53/.test(s) && /03\s+Local Forecast - Elevator\s+3:11/.test(s) &&
  /04\s+Funkorama/.test(s) && /05\s+Eighties Action\s+2:50/.test(s), 'track list: titles from CDPLAY.INI, lengths from the TOC');
check(lit('STOP') && !lit('PLAY') && s.includes('4 audio tracks.  Press P or Enter to play.'), 'LCD: STOP lit, 4 audio tracks');
check(['Play', 'Pause', 'Stop', 'Prev', 'Next', 'Eject', 'Shuffle', 'Repeat'].every((b) => s.includes(b)), 'buttons: Play Pause Stop Prev Next Eject Shuffle Repeat');
await pc.shot('cdplay-stopped.png');

// ---------------------------------------------------------------- play track 2
const tPress = pc.timeMs;
await key('2', 3000);
s = scr();
check(st().playing && st().track === 2, 'key 2: the drive plays track 2');
check(lit('PLAY') && !lit('STOP') && /Cipher\s+0:0[234] \/ 3:53/.test(s), `LCD: PLAY, "Cipher 0:0x / 3:53" (${(s.match(/Cipher\s+\S+ \/ 3:53/) || [''])[0]})`);
check(/\x10 02\s+Cipher/.test(s) || /► 02\s+Cipher/.test(s), 'the playing track is marked in the list');
await pc.shot('cdplay-playing.png');
const quiet = level(tPress - 1000, tPress), loud = level(tPress + 1000, tPress + 2900);
console.log(`     audio rms: ${quiet.toFixed(5)} before, ${loud.toFixed(4)} while playing`);
check(quiet >= 0 && quiet < 1e-4, 'silent before track 2 starts');
check(loud > 0.004, 'sound while track 2 plays (CD input at SBMIX /INIT\'s -14 dB)');
const onset = blocks.find(([t, rms]) => t >= tPress && rms > 1e-4);
// Cipher begins with half a second of silence: its first sound is ~0.55 s in
check(onset && onset[0] - tPress > 300 && onset[0] - tPress < 1000, `the first sound comes ${onset ? Math.round(onset[0] - tPress) : '?'} ms after the key press (the track's own lead-in)`);

// the right samples at the right time: capture 1 s now, find it in t02.wav
{
  const posLba = st().lba - disc.track(2).start;          // where the drive says it is
  cap = { left: [], right: [], max: RATE };
  await go(1100);
  const got = Float32Array.from(cap.left.slice(0, RATE / 2));
  const wav = readWav(path.join(DISC, 't02.wav'));
  const expectAt = posLba * 588;
  let best = -2, bestOff = 0;
  const probe = got.subarray(0, 4096);
  for (let off = Math.max(0, expectAt - RATE / 2); off < expectAt + RATE; off += 7) {
    let sxy = 0, sxx = 0, syy = 0;
    for (let i = 0; i < probe.length; i += 2) { const x = probe[i], y = wav.left[off + i] / 32768; sxy += x * y; sxx += x * x; syy += y * y; }
    const c = sxy / Math.sqrt(sxx * syy + 1e-12);
    if (c > best) { best = c; bestOff = off; }
  }
  const dt = (bestOff - expectAt) / RATE;
  console.log(`     correlation ${best.toFixed(3)} at ${(bestOff / RATE).toFixed(3)} s into track 2 (drive position ${(expectAt / RATE).toFixed(3)} s, delta ${dt.toFixed(3)} s)`);
  check(best > 0.95, 'the machine plays track 2\'s own samples (correlation with t02.wav)');
  check(Math.abs(dt) < 0.1, 'at the position the drive reports (within 0.1 s)');
  // WAV of what the machine played
  const n = cap.left.length, b = Buffer.alloc(44 + n * 4);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 4, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22);
  b.writeUInt32LE(RATE, 24); b.writeUInt32LE(RATE * 4, 28); b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 4, 40);
  for (let i = 0; i < n; i++) { b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(cap.left[i] * 32767))), 44 + i * 4); b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(cap.right[i] * 32767))), 46 + i * 4); }
  fs.writeFileSync(B('cdrom-test/cdplay.wav'), b);
  console.log(`     wrote ${B('cdrom-test/cdplay.wav')}`);
  cap = null;
}

// ---------------------------------------------------------------- pause, resume, next, prev, stop
await key(' ', 400);
const lbaP = st().lba, tP = pc.timeMs;
check(st().paused && lit('PAUSE') && !lit('PLAY'), 'Space: paused (PAUSE lit)');
await go(1500);
check(st().lba === lbaP, 'the position holds while paused');
check(level(tP + 200, pc.timeMs) < 1e-4, 'silent while paused');
await key(' ', 1000);
check(st().playing && st().lba > lbaP && level(pc.timeMs - 600, pc.timeMs) > 0.004, 'Space again: resumes where it paused, sound again');
tap(pc, 'ArrowRight', 1000); await go(500);
check(st().playing && st().track === 3 && /Local Forecast - Elevator\s+0:0[01] \/ 3:11/.test(scr()), 'Right: next track (3)');
tap(pc, 'ArrowLeft', 500); await go(300);
check(st().track === 2, 'Left within 3 s of the start: previous track (2)');
await go(3500);
tap(pc, 'ArrowLeft', 500); await go(300);
check(st().track === 2 && st().lba - disc.track(2).start < 75 * 2, 'Left later in a track: back to its start');
await key('s', 500);
const tS = pc.timeMs;
await go(800);
check(!st().playing && !st().paused && lit('STOP'), 'S: stopped');
check(level(tS + 100, pc.timeMs) < 1e-4, 'silent when stopped');

// the mouse: click Next (the button at columns 40-48 of row 13), then a row of the list
const clickCell = async (x, y) => {
  for (let i = 0; i < 10; i++) { pc.m.mouseMove(-200, -200); pc.run(20); }        // to the top left corner
  let px = x * 8 + 4, py = (y * 8 + 4) * 2;                                        // 8 mickeys = 8 pixels across, 16 = 8 down
  while (px > 0 || py > 0) { const dx = Math.min(px, 100), dy = Math.min(py, 100); pc.m.mouseMove(dx, dy); px -= dx; py -= dy; pc.run(20); }
  pc.m.mouseButtons(1); pc.run(100); pc.m.mouseButtons(0); await go(600);
};
await key('2', 1000);
await clickCell(43, 13);
check(st().playing && st().track === 3, 'mouse: a click on Next plays track 3');
await clickCell(20, 16 + 3);
check(st().playing && st().track === 4, 'mouse: a click on the list\'s track 04 row plays it');
await key('s', 300);

// list selection + Enter
tap(pc, 'ArrowUp'); tap(pc, 'ArrowUp');
await key('\r', 800);
check(st().playing && st().track === 2 && /Cipher/.test(scr()), 'Up, Up (from track 4), Enter: plays the selected track (2)');
tap(pc, 'ArrowDown'); tap(pc, 'ArrowDown');
await key('\r', 800);
check(st().playing && st().track === 4 && /Funkorama/.test(scr()), 'Down, Down, Enter: plays the selected track (4)');
await pc.shot('cdplay-track4.png');

// shuffle / repeat indicators
await key('h', 300);
check(lit('SHUFFLE') && scr().includes('Shuffle on'), 'H: SHUFFLE lit (and a status line)');
await key('r', 300);
check(lit('REPEAT'), 'R: REPEAT lit');
await pc.shot('cdplay-shuffle.png');
await key('h', 100); await key('r', 300);
check(!lit('SHUFFLE') && !lit('REPEAT'), 'H, R again: both off');

// eject and load from the player
await key('e', 800);
check(st().trayOpen && !st().playing && scr().includes('The drive is open.'), 'E: the tray opens, "The drive is open."');
await pc.shot('cdplay-open.png');
await key('e', 2500);
check(!st().trayOpen && /5 tracks/.test(scr()), 'E again: the tray closes, the disc is read again');

// ---------------------------------------------------------------- a short disc: repeat / shuffle go on at track ends
const tone = (secs, hz) => { const n = Math.ceil(secs * RATE / 588) * 588, l = new Float32Array(n); for (let i = 0; i < n; i++) l[i] = 0.4 * Math.sin(2 * Math.PI * hz * i / RATE); return { left: l, right: l }; };
const pcm = { 2: tone(3, 440), 3: tone(3, 660) };
const iso = buildIso({ volumeId: 'SHORT', files: [{ path: 'X.TXT', data: new Uint8Array([65, 13, 10]) }] });
const shortDisc = new CdDisc({ title: 'Short', tracks: [{ number: 1, type: 'data', sectors: iso.length / 2048 },
  { number: 2, type: 'audio', pregap: 150, sectors: pcm[2].left.length / 588 }, { number: 3, type: 'audio', pregap: 150, sectors: pcm[3].left.length / 588 }] },
  { data: iso, audio: (n) => pcm[n] || null });
pc.m.cdrom.insert(shortDisc);
await go(2500);
s = scr();
check(/Audio CD\s+\S\s+3 tracks/.test(s) && /02\s+Track 2\s+0:05/.test(s) && /03\s+Track 3\s+0:03/.test(s), 'a new disc: CDPLAY re-reads the TOC (no titles: "Track n")');
await key('r', 200);
await key('3', 500);
check(st().track === 3, 'repeat on, track 3 (the last) plays');
await go(4000);
check(st().playing && st().track === 2, 'at the end of the disc, repeat starts again at track 2');
await key('s', 300); await key('r', 200);
await key('h', 200);
await key('2', 500);
await go(9500);
check(!st().playing && lit('STOP'), 'shuffle without repeat: each track once, then stop');
await key('h', 200);

// leave the player with music playing
pc.m.cdrom.insert(disc);
await go(2500);
await key('5', 1500);
check(st().playing && st().track === 5, 'track 5 playing');
tap(pc, 'Escape', 500);
pc.until(() => /^C:\\>/.test(lastLine(pc)), { timeoutMs: 5000 });
check(/^C:\\>/.test(lastLine(pc)) && pc.lines().some((l) => l.startsWith('C:\\>CDPLAY')) && !pc.hasText('Tracks'),
  'Esc: back to the prompt, the screen as it was');
await go(1000);
if (!/^C:\\>/.test(lastLine(pc))) console.log(scr());
check(st().playing && st().track === 5 && level(pc.timeMs - 800, pc.timeMs) > 0.004, 'the music plays on after CDPLAY exits');

// ---------------------------------------------------------------- the pop-up
const HOOKS = [8, 9, 0x10, 0x13, 0x15, 0x21, 0x28, 0x2F];
const ivt = (n) => pc.m.cpu.m32[n] >>> 0;
const vec0 = HOOKS.map(ivt);
pc.cmd('CLS');
pc.cmd('CDPLAY /R');
check(pc.hasText('ARM-DOS CD Player installed. Press Ctrl+Alt+C to pop it up.'), 'CDPLAY /R: installed');
pc.cmd('CDPLAY /R');
check(pc.hasText('The CD Player pop-up is already installed.'), 'a second /R refuses');
{
  let res = null;
  for (let seg = 0x60; seg < 0xA000 && !res; seg++) {
    const m8 = pc.m.cpu.m8, a = seg << 4;
    if ((m8[a] === 0x4D || m8[a] === 0x5A) && String.fromCharCode(...m8.subarray(a + 8, a + 14)) === 'CDPLAY' && (m8[a + 1] | (m8[a + 2] << 8)) === seg + 1)
      res = (m8[a + 3] | (m8[a + 4] << 8)) * 16;
  }
  console.log(`     CDPLAY /R resident: ${res} bytes`);
  check(res && res < 30000, 'the pop-up is resident (MCB named CDPLAY), under 30 KB');
}
pc.cmd('ECHO The quick brown fox');
const before = vram();
await hotkey();
check(pc.waitText(' CD Player ', { timeoutMs: 3000 }) && pc.hasText('Track 5: Eighties Action') && pc.hasText('PLAY'),
  'Ctrl+Alt+C at the prompt (INT 28h): the pop-up shows track 5 playing');
await pc.shot('cdplay-popup-prompt.png');
await key('3', 1500);
check(st().track === 3 && pc.hasText('Track 3: Local Forecast - Elevator'), 'pop-up: 3 plays track 3');
tap(pc, 'Escape', 300);
check(same(vram(), before), 'Esc: the screen is restored exactly');
check(pc.until(() => /^C:\\>/.test(lastLine(pc)), { timeoutMs: 3000 }), 'back at the prompt');

// over a program: ZORK from the CD
pc.cmd('CLS');
pc.type('D:\\ZORK\\ZORK1\r');
check(pc.waitText('West of House', { timeoutMs: 60000 }), 'ZORK I runs');
await go(500);
const zorkScreen = vram();
await hotkey();
check(pc.waitText(' CD Player ', { timeoutMs: 3000 }), 'Ctrl+Alt+C over ZORK, waiting for a command in INT 21h AH=3Fh: the pop-up');
await key('4', 1500);
check(st().track === 4, 'pop-up over ZORK: 4 plays track 4');
await pc.shot('cdplay-popup-zork.png');
tap(pc, 'Escape', 300);
check(same(vram(), zorkScreen), 'ZORK\'s screen restored exactly');
pc.type('open mailbox\r');
check(pc.waitText('leaflet', { timeoutMs: 10000 }), 'ZORK carries on ("open mailbox")');
check(st().playing && st().track === 4, 'and the music too');
pc.type('quit\r'); await go(1500); pc.type('y\r');
pc.until(() => /^C:\\>/.test(lastLine(pc)), { timeoutMs: 20000 });

// repeat while the pop-up is closed (the short disc)
pc.m.cdrom.insert(shortDisc);
await go(2500);
await hotkey();
await key('r', 200);
await key('3', 500);
tap(pc, 'Escape', 300);
check(st().track === 3, 'pop-up: repeat on, track 3 of the short disc');
await go(5000);
check(st().playing && st().track === 2, 'with the pop-up closed, repeat wraps to track 2 at the end (checked at safe moments)');
await hotkey(); await key('s', 300); await key('r', 200); tap(pc, 'Escape', 300);
pc.m.cdrom.insert(disc);
await go(2000);

// graphics mode: refused with a beep
pc.type('D:\\DEMO\\DEMO.EXE\r');
check(pc.until(() => pc.m.vga.mode === 0x13, { timeoutMs: 30000 }), 'DEMO (from the CD) in mode 13h');
await go(1000);
const n0 = pc.speaker.length;
await hotkey();
await go(300);
const beeped = pc.speaker.slice(n0).some(([, on, hz]) => on && Math.abs(hz - 880) < 5);
check(beeped && pc.m.vga.mode === 0x13, 'Ctrl+Alt+C in mode 13h: 880 Hz beep, no pop-up');
tap(pc, 'Escape', 1500);
pc.until(() => pc.m.vga.mode === 3, { timeoutMs: 10000 });
pc.until(() => /^C:\\>/.test(lastLine(pc)), { timeoutMs: 10000 });

// removal
pc.cmd('CLS');
pc.cmd('CDPLAY /U');
check(pc.hasText('The CD Player pop-up has been removed from memory.'), 'CDPLAY /U');
check(HOOKS.every((n, i) => ivt(n) === vec0[i]), 'all eight vectors restored');
pc.cmd('CDPLAY /U');
check(pc.hasText('The CD Player pop-up is not installed.'), 'CDPLAY /U again: not installed');
const v0 = vram();
await hotkey();
check(same(vram(), v0), 'the hot key does nothing after removal');

check(pc.faults.length === 0, 'no CPU faults');
console.log(failed() ? `${failed()} check(s) FAILED` : 'cdplay: all checks passed');
process.exit(failed() ? 1 : 0);
