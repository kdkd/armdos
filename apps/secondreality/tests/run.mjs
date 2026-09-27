#!/usr/bin/env node
// apps/secondreality/tests/run.mjs - Second Reality under ELBOW, headless:
// the setup screen, the Sound Blaster Pro choice, the intro's texts in the
// unchained 320x400 mode (drawn plane by plane through the VGA's planar
// window), music from EMS through the SB16, the first landscape picture, and
// Ctrl ending the demo cleanly (text mode, the prompt).  "make secondreality-test".
import fs from 'node:fs';
import { session, Checker } from '../../dosutil/tests/harness.mjs';
import { renderScreen } from '../../../emu/render.mjs';

const t = new Checker('SECOND REALITY');
const SR = '3rdparty/secondreality';
const files = [{ src: 'build/ELBOW.EXE', dst: 'DOS\\' }, { src: 'build/HIMEM.SYS', dst: 'DOS\\' }];
for (const f of fs.readdirSync(SR)) files.push({ src: `${SR}/${f}`, dst: 'ELBOW\\DEMOS\\SECOND\\' });
const s = await session({ name: 'secondreality', files, dirs: ['ELBOW', 'ELBOW\\DEMOS', 'ELBOW\\DEMOS\\SECOND'], sizeMB: 64,
  config: 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=30\nSHELL=C:\\T\\TSHELL.EXE\n' });
const m = s.pc.machine;
const chunks = [];
m.audio.start(22050, (l) => chunks.push(Float32Array.from(l)));
const rms = (from) => { let n = 0, q = 0; for (const c of chunks.slice(from)) for (const v of c) { n++; q += v * v; } return n ? Math.sqrt(q / n) : 0; };
// what the picture looks like: count of distinct colours, fraction of bright pixels in a box
const look = (x0 = 0, y0 = 0, x1 = 1, y1 = 1) => {
  const img = renderScreen(m);
  const cols = new Set(); let bright = 0, n = 0;
  for (let y = Math.floor(y0 * img.height); y < y1 * img.height; y++) for (let x = Math.floor(x0 * img.width); x < x1 * img.width; x++) {
    const k = (y * img.width + x) * 4, r = img.data[k], g = img.data[k + 1], b = img.data[k + 2];
    cols.add((r << 16) | (g << 8) | b); n++; if (r + g + b > 400) bright++;
  }
  return { w: img.width, h: img.height, colours: cols.size, bright: bright / n };
};

s.pc.type('CD \\ELBOW\\DEMOS\\SECOND\r'); s.waitPrompt();
s.pc.type('\\DOS\\ELBOW SECOND\r');
t.ok(s.pc.waitText('Soundcard: Gravis Ultrasound', { timeoutMs: 20000 }), 'the SETUP screen (START.EXE, a PKLITE part loaded by the demo\'s own EXE loader)', s.pc.screen());
t.ok(s.pc.screen().includes('C∙O∙N∙D') || /S.E.C.O.N.D/.test(s.pc.screen()), 'its title line', s.pc.screen());
s.pc.run(500);
s.pc.type('{LEFT}');
t.ok(s.pc.waitText('SoundBlaster Pro', { timeoutMs: 5000 }), 'Left arrow: Sound Blaster Pro', s.pc.screen());
s.pc.run(300);
const a0 = chunks.length;
s.pc.type('{ENTER}');
// the intro: "A Future Crew Production" fades in over the unchained 320x400 mode
let text = null;
for (let i = 0; i < 40 && !text; i++) {
  s.pc.run(500);
  const l = look(0.2, 0.25, 0.8, 0.6);
  if (l.bright > 0.02 && l.w === 320 && l.h === 400) text = l;
}
t.ok(!!text, 'intro text in the 320x400 unchained mode (Mode Y: chain-4 off, CRTC byte mode, max scan line 0)', JSON.stringify(look()));
t.ok(m.vga.window && !m.vga.chain4 && m.vga.displayKind() === 'vga', 'the VGA\'s planar window is open (A0000h = MMIO onto the four planes)');
t.ok(rms(a0) > 0.002, 'music: the SB16 plays STMIK\'s mix (from EMS, through ELBOW\'s DMA page translation)', `rms ${rms(a0)}`);
// the landscape with the stars (ALKU, after the texts)
let land = null;
for (let i = 0; i < 90 && !land; i++) {
  s.pc.run(1000);
  const l = look(0, 0.6, 1, 0.9);
  if (l.colours > 20) land = l;
}
t.ok(!!land, 'the landscape picture (a 640-wide virtual screen, scrolled by the start address and pixel panning)', JSON.stringify(look()));
// Esc skips a part; holding Ctrl ends the whole demo (U2's ctrldown): text mode and the prompt come back
m.keyDown('ControlLeft'); s.pc.run(1500); m.keyUp('ControlLeft');
let back = false;
for (let i = 0; i < 40 && !back; i++) { s.pc.run(500); back = m.vga.displayKind() === 'text' && s.atPrompt(); }
t.ok(back, 'Ctrl ends the demo: text mode, back at the prompt', s.pc.screen());
t.ok(/Copyright \(C\) 1993 Future Crew/.test(s.pc.screen()) && /This demonstration can not be sold/.test(s.pc.screen()), 'the end screen (U2END.BIN copied to B800h with REP MOVSD)', s.pc.screen());
t.ok(!m.vga.window && m.cpu.mmioSeg > 0xFF, 'the window is closed again (A0000h is RAM)');
t.ok(!s.pc.faults.length, 'no CPU faults', JSON.stringify(s.pc.faults));
t.done();
