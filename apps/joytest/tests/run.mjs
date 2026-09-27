#!/usr/bin/env node
// apps/joytest/tests/run.mjs - the game adapter end to end on ARM-DOS, with a simulated
// joystick (machine.joy, emu/dev/gameport.mjs):
//   * the BIOS (JOYBIOS.EXE): INT 11h bit 12, INT 15h AH=84h DX=0 switches, DX=1 resistive
//     inputs against the 558 timing (AT units, 6.704 us), at 100 and 12 MHz; no stick = 0;
//     no game port = bit 12 clear and CF set / AH=86h
//   * JOYTEST /R (raw counts on stdout, redirectable)
//   * JOYTEST: raw counts, bars, button lamps, the crosshair following the stick, the
//     calibration (centre, upper left, lower right with button 1), JOYSTICK.CFG written
//     and loaded again
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const B = (p) => path.join(ROOT, 'build', p);
const OUT = B('joytest-test');
let failures = 0;
const check = (ok, what, extra = '') => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${extra && !ok ? '\n     ' + extra : ''}`); if (!ok) failures++; };
for (const f of ['rom.bin', 'IO.SYS', 'ARMDOS.SYS', 'COMMAND.COM', 'JOYTEST.EXE', 'joytest-test/JOYBIOS.EXE', 'bootsect.bin'])
  if (!fs.existsSync(B(f))) { console.log(`build/${f} missing`); process.exit(2); }

function makeImage() {
  fs.mkdirSync(OUT, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM' },
    { src: 'build/JOYTEST.EXE', dst: 'DOS\\JOYTEST.EXE' },
    { src: 'build/joytest-test/JOYBIOS.EXE', dst: 'DOS\\JOYBIOS.EXE' },
  ];
  const put = (dst, text) => { const h = path.join(OUT, 'img-' + dst); fs.writeFileSync(h, text.replace(/\n/g, '\r\n')); files.push({ src: path.relative(ROOT, h), dst }); };
  put('CONFIG.SYS', 'FILES=20\n');
  put('AUTOEXEC.BAT', '@ECHO OFF\nPROMPT $p$g\nPATH C:\\DOS\n');
  const { img } = buildImage({ format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'JOYTEST',
    date: '1990-06-01 12:00:00', boot: { src: 'build/bootsect.bin' }, files, dirs: ['DOS'] }, ROOT);
  const p = path.join(OUT, 'joytest.img');
  fs.writeFileSync(p, img);
  return p;
}
const hd = makeImage();
const readFile = (pc, dosPath) => {
  const fr = new FatReader(Buffer.from(pc.machine.ata.img.buffer, pc.machine.ata.img.byteOffset, pc.machine.ata.img.length));
  try { const e = fr.lookup(dosPath); return e ? Buffer.from(fr.readFile(e)).toString('latin1') : null; } catch { return null; }
};
// the AT units INT 15h AH=84h returns for a stick position (-1..1): 558 pulse / 6.704 us
const atUnits = (pos) => (24.2 + 0.011 * (pos + 1) / 2 * 100000) / 6.704;
const near = (v, e, tol) => Math.abs(v - e) <= tol;
const prompted = (pc) => { const L = pc.screen().split('\n').filter((x) => x.trim()); return L.length > 1 && L[L.length - 1].trim() === 'C:\\>'; };
const run = (pc, cmd) => { pc.type('CLS\r'); pc.waitIdle(); pc.type(cmd + '\r'); pc.run(200); pc.until(() => prompted(pc), { timeoutMs: 20000 }); return pc.screen(); };
const bios = (s) => {
  const m = s.match(/84h\/1: CF=(\d) AX=(\d+) BX=(\d+) CX=(\d+) DX=(\d+)/);
  return m ? m.slice(1).map(Number) : null;
};

// ------------------------------------------------------------------ the BIOS
for (const mhz of [100, 12]) {
  const pc = await boot({ rom: B('rom.bin'), hd, mhz });
  check(pc.waitText('C:\\>', { timeoutMs: 30000 }), `@${mhz} MHz: booted`);
  let s = run(pc, 'JOYBIOS');
  check(s.includes('INT 11h: ') && s.includes('game adapter yes'), `@${mhz}: INT 11h bit 12 = game adapter`, s);
  check(s.includes('84h/0: CF=0 AL=F0h'), `@${mhz}: AH=84h DX=0: no buttons = F0h`, s);
  let v = bios(s);
  check(v && v[0] === 0 && v.slice(1).every((x) => x === 0), `@${mhz}: DX=1 with nothing plugged in: 0 0 0 0`, s);
  check(s.includes('84h/2: CF=1 AH=86h'), `@${mhz}: DX=2 is an error (CF, AH=86h)`, s);
  const j = pc.machine.joy;
  j.plug(0); j.setAxis(0, -1); j.setAxis(1, 0.5); j.setButton(0, true); j.setButton(1, true);
  s = run(pc, 'JOYBIOS');
  v = bios(s);
  const ex = [atUnits(-1), atUnits(0.5)];
  check(s.includes('84h/0: CF=0 AL=C0h'), `@${mhz}: buttons 1+2 pressed: AL=C0h`, s);
  check(v && near(v[1], ex[0], 1) && near(v[2], ex[1], 1.5) && v[3] === 0 && v[4] === 0,
    `@${mhz}: DX=1 stick A left (0 ohm) / 3/4 down (75 kOhm): ~${ex[0].toFixed(0)}, ~${ex[1].toFixed(0)}; B absent = 0`, s);
  if (v) console.log(`     @${mhz} MHz: A(x)=${v[1]} A(y)=${v[2]} (expected ${ex[0].toFixed(1)}, ${ex[1].toFixed(1)})`);
  j.setButton(0, false); j.setButton(1, false);
  j.plug(1); j.setAxis(0, 0); j.setAxis(1, 0); j.setAxis(2, 1); j.setAxis(3, -0.5); j.setButton(2, true);
  s = run(pc, 'JOYBIOS');
  v = bios(s);
  check(v && near(v[1], atUnits(0), 1.5) && near(v[2], atUnits(0), 1.5) && near(v[3], atUnits(1), 2) && near(v[4], atUnits(-0.5), 1.5) && s.includes('AL=B0h'),
    `@${mhz}: two sticks: A centred ~${atUnits(0).toFixed(0)}, B ~${atUnits(1).toFixed(0)}/${atUnits(-0.5).toFixed(0)}, button 3: AL=B0h`, s);
  if (mhz === 100) {
    // JOYTEST /R: the raw counts on standard output (redirected to a file)
    j.setButton(2, false); j.plug(1, false); j.setAxis(0, -0.5); j.setAxis(1, 0.5); j.setButton(1, true);
    s = run(pc, 'JOYTEST /R > JOY.TXT');
    const t = readFile(pc, 'JOY.TXT') || '';
    const mx = t.match(/Joystick A: X=(\d+) Y=(\d+)/);
    // the loop: 1 us ISA cycle + ~20 instructions per poll at 100 MHz; 25 kOhm = 299.2 us, 75 kOhm = 849.2 us
    check(t.includes('game adapter at 201h installed') && mx && near(+mx[1], 299.2 / 1.2, 40) && near(+mx[2], 849.2 / 1.2, 110) && +mx[1] < +mx[2],
      'JOYTEST /R: raw counts for X (25 kOhm) < Y (75 kOhm), in the classic range', t);
    check(/button 1 up, button 2 pressed/.test(t) && t.includes('Joystick B: not connected') && /BIOS INT 15h AH=84h: A\(x\)=\d+/.test(t), 'JOYTEST /R: buttons, stick B absent, the BIOS line', t);
    if (mx) console.log(`     JOYTEST /R: ${t.split('\r\n').filter(Boolean).join(' | ')}`);
    j.setButton(1, false);
  }
  pc.machine.stopped = true;
}
{
  const pc = await boot({ rom: B('rom.bin'), hd, joystick: false });
  check(pc.waitText('C:\\>', { timeoutMs: 30000 }), 'no game port: booted');
  const s = run(pc, 'JOYBIOS');
  check(s.includes('game adapter no') && s.includes('84h/0: CF=1') && /84h\/1: CF=1/.test(s), 'no game port: INT 11h bit 12 clear, AH=84h fails', s);
}

// ------------------------------------------------------------------ JOYTEST, full screen
{
  const pc = await boot({ rom: B('rom.bin'), hd });
  pc.waitText('C:\\>', { timeoutMs: 30000 });
  const j = pc.machine.joy;
  const lines = () => pc.lines();
  const findCross = () => { const L = lines(); for (let r = 3; r <= 19; r++) { const c = L[r].indexOf('\u263C'); if (c >= 42) return [c, r]; } return null; };
  const rawOf = (row) => { const m = lines()[row].slice(10, 16).match(/\d+/); return m ? +m[0] : null; };
  pc.type('JOYTEST\r');
  check(pc.waitText('Joystick Test and Calibration', { timeoutMs: 10000 }), 'JOYTEST: the screen');
  pc.run(200);
  check(pc.hasText('No joystick in port A') && lines()[3].includes('----') && pc.hasText('not connected'), 'no stick: "No joystick in port A", ---- counts');
  check(pc.hasText('Game adapter 201h: installed (INT 11h bit 12)') && pc.hasText('Not calibrated: press C'), 'BIOS status line, not calibrated');
  j.plug(0); pc.run(1200);
  const c0 = findCross();
  check(c0 && c0[0] >= 57 && c0[0] <= 62 && c0[1] >= 10 && c0[1] <= 12, 'stick plugged, centred: the crosshair in the middle of the box', JSON.stringify(c0) + '\n' + pc.screen());
  const xc = rawOf(3), yc = rawOf(5);
  check(xc > 400 && xc < 520 && yc > 400 && yc < 520, 'raw counts at the centre (50 kOhm = 574 us)', `${xc} ${yc}`);
  j.setAxis(0, 1); j.setAxis(1, -1); pc.run(300);
  const c1 = findCross(), xr = rawOf(3), yu = rawOf(5);
  check(c1 && c1[0] > c0[0] + 8 && c1[1] < c0[1] - 4, 'stick right and up: the crosshair moves right and up', JSON.stringify([c0, c1]));
  check(xr > xc * 1.7 && yu < yc / 10, 'raw counts: right = ~2x centre, up = ~24 us', `${xr} ${yu}`);
  j.setButton(0, true); j.setButton(3, true); pc.run(200);
  check(lines()[8].slice(12, 20).includes('ON') && lines()[17].slice(30, 38).includes('ON') && lines()[8].slice(30, 38).includes('off'), 'button lamps 1 and 4 light, 2 stays off', `${lines()[8]}\n${lines()[17]}`);
  j.setButton(0, false); j.setButton(3, false); pc.run(200);
  // calibration: centre, upper left, lower right (at 80% of the travel), button 1 each
  pc.type('C'); pc.run(300);
  const press = () => { j.setButton(0, true); pc.run(150); j.setButton(0, false); pc.run(250); };
  check(pc.hasText('Centre the joystick and press button 1.'), 'calibration: centre prompt');
  j.setAxis(0, 0.05); j.setAxis(1, -0.05); pc.run(100); press();
  check(pc.hasText('UPPER LEFT corner'), 'calibration: upper left prompt');
  j.setAxis(0, -0.8); j.setAxis(1, -0.8); pc.run(100); press();
  check(pc.hasText('LOWER RIGHT corner'), 'calibration: lower right prompt');
  j.setAxis(0, 0.8); j.setAxis(1, 0.8); pc.run(100); press();
  pc.run(300);
  const msg = lines()[22];
  check(/Calibrated: X \d+-\d+-\d+  Y \d+-\d+-\d+/.test(msg), 'calibration done', pc.screen());
  j.setAxis(0, -0.8); j.setAxis(1, -0.8); pc.run(300);
  const cul = findCross();
  check(cul && cul[0] === 42 && cul[1] === 3, 'calibrated: the upper-left position puts the crosshair in the corner', JSON.stringify(cul));
  j.setAxis(0, 0.05); j.setAxis(1, -0.05); pc.run(300);
  const cc = findCross();
  check(cc && cc[0] === 59 && cc[1] === 11, 'calibrated: the calibrated centre is the middle', JSON.stringify(cc));
  pc.type('S'); pc.run(500);
  check(pc.hasText('Saved JOYSTICK.CFG'), 'S: saved');
  pc.type('{ESC}'); pc.waitText('C:\\>', { timeoutMs: 5000 }); pc.run(300);
  const cfg = readFile(pc, 'JOYSTICK.CFG') || '';
  const n = (k) => +((cfg.match(new RegExp(k + '=(\\d+)')) || [])[1]);
  check(cfg.includes('[JoystickA]') && n('XMin') < n('XCenter') && n('XCenter') < n('XMax') && n('YMin') < n('YCenter') && n('YCenter') < n('YMax'),
    'JOYSTICK.CFG: min < centre < max for X and Y', cfg);
  console.log('     ' + cfg.split('\r\n').filter((l) => /^[XY]/.test(l)).slice(0, 6).join(' '));
  check(!lines().join('').includes('Joystick Test'), 'Esc: back to DOS, screen cleared');
  pc.type('JOYTEST\r'); pc.waitText('Joystick Test and Calibration', { timeoutMs: 10000 }); pc.run(300);
  check(pc.hasText('JOYSTICK.CFG loaded: X ' + n('XMin')), 'JOYTEST again: JOYSTICK.CFG loaded');
  await pc.png(path.join(OUT, 'joytest.png'));
  pc.type('{ESC}'); pc.waitText('C:\\>', { timeoutMs: 5000 });
}
console.log(failures ? `${failures} FAILED` : 'all passed');
process.exit(failures ? 1 : 0);
