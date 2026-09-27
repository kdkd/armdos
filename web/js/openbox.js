// "Open the box": the ARM/AT with its lid off. A top-down drawing of the chassis (motherboard,
// ISA slots with their cards, SIMMs, the clock jumper, the drives, the power supply) and a
// parts bin. Visitors move cards between the slots and the bin, change the SIMMs and the CPU
// clock jumper, plug the mouse in or out and take the CD-ROM drive out - the way you would
// reconfigure a 1990 PC. Nothing may be touched while the machine is on (it says so); the
// changes take effect at the next power-on (Machine.setHardware, emu/README.md).
//
//   const box = new OpenBox({ state, sound, line, gm, cd, setMonitor });
//   box.mount($('case'))                        // the "Open case" control + the panel after the case
//   new Machine({ ..., ...box.machineOptions() })
//   box.apply(machine)                          // before a later powerCycle()
//   box.setPowered(on)
//
// The configuration lives in localStorage (prefs 'hw'); the display card follows the Monitor
// selector (prefs 'monitor'), the MIDI daughterboard the MIDI switch (prefs 'gmSynth') and
// the modem card's speed jumper the modem's speed switch (prefs 'modemSpeed').

import { $, el, prefs } from './util.js';

export const CLOCKS = [12, 25, 33, 50, 100, 133];
export const RAMS = [1, 2, 4, 8, 16];
const SLOT16 = [false, false, true, true, true, true, true];     // slots 0-1 are 8-bit (no AT extension)
export const FACTORY = Object.freeze({ ram: 16, mhz: 100, sound: 'sb16', modem: true, cdrom: true, mouse: true, game: true,
  slots: Object.freeze(['io', 'modem', null, 'sb16', null, 'vga', 'ide']) });
const SPEED_NAME = { 2400: '2400', 14400: '14.4', 33600: '33.6', 56000: '56K' };

// the cards: bits (8/16), length on the board, group (one of each in the machine), fixed (screwed in)
const CARDS = {
  vga:      { name: 'VGA display card', bits: 16, len: 290, group: 'video' },
  hercules: { name: 'Hercules-compatible monochrome card', bits: 8, len: 322, group: 'video' },
  sb16:     { name: 'Sound Blaster 16', bits: 16, len: 312, group: 'sound' },
  adlib:    { name: 'AdLib-compatible FM music card', bits: 8, len: 196, group: 'sound' },
  modem:    { name: 'internal modem', bits: 8, len: 236, group: 'modem' },
  io:       { name: 'serial/parallel/game I/O card', bits: 8, len: 216, fixed: true },
  ide:      { name: 'IDE/floppy controller', bits: 16, len: 290, fixed: true },
};
const W = 90;                                   // card width on the board
const SX = (i) => 150 + i * 100;                // slot i: the card's left edge

// ------------------------------------------------------------------ drawing helpers (SVG markup)
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const R = (x, y, w, h, fill, extra = '') => `<rect x="${x}" y="${y}" width="${w}" height="${h}" fill="${fill}" ${extra}/>`;
const T = (x, y, s, cls, extra = '') => `<text x="${x}" y="${y}" class="${cls}" ${extra}>${esc(s)}</text>`;
const TC = (x, y, s, cls, extra = '') => T(x, y, s, cls, `text-anchor="middle" ${extra}`);

/** A DIP package, long axis horizontal: legs along the top and bottom edges. */
function dip(x, y, w, h, lines = [], { label = 'chip', legs = true, fill = 'url(#obChip)' } = {}) {
  let s = '';
  if (legs) {
    const n = Math.max(4, Math.floor((w - 6) / 5));
    for (let i = 0; i < n; i++) { const lx = x + 4 + i * ((w - 8) / (n - 1)) - 1; s += R(lx, y - 2, 2, 2.4, '#c9ccd0') + R(lx, y + h - .4, 2, 2.4, '#c9ccd0'); }
  }
  s += `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="1" fill="${fill}"/>`;
  s += `<rect x="${x}" y="${y}" width="${w}" height="1" fill="rgba(255,255,255,.12)"/>`;
  s += `<circle cx="${x + 3.2}" cy="${y + h / 2}" r="1.6" fill="#0b0b0c"/>`;
  const fs = lines.length > 1 ? Math.min(5.4, h / (lines.length + .8)) : Math.min(6, h * .5);
  lines.forEach((ln, i) => { s += TC(x + w / 2 + 2, y + h / 2 + fs * (i - (lines.length - 1) / 2) + fs * .36, ln, label, `font-size="${fs.toFixed(1)}"`); });
  return s;
}
/** A square PLCC/QFP: legs on all four sides. */
function quad(x, y, a, lines = [], { fill = 'url(#obChip)', legColor = '#c9ccd0', label = 'chip' } = {}) {
  let s = '';
  const n = Math.max(4, Math.floor(a / 5));
  for (let i = 0; i < n; i++) {
    const p = x + 4 + i * ((a - 8) / (n - 1)) - .8, q = y + 4 + i * ((a - 8) / (n - 1)) - .8;
    s += R(p, y - 2.2, 1.6, 2.4, legColor) + R(p, y + a - .2, 1.6, 2.4, legColor) + R(x - 2.2, q, 2.4, 1.6, legColor) + R(x + a - .2, q, 2.4, 1.6, legColor);
  }
  s += `<rect x="${x}" y="${y}" width="${a}" height="${a}" rx="1.5" fill="${fill}"/>`;
  s += `<path d="M${x} ${y + 6}L${x + 6} ${y}" stroke="rgba(255,255,255,.18)" stroke-width="1"/>`;
  const fs = Math.min(6.2, a / 7);
  lines.forEach((ln, i) => { s += TC(x + a / 2, y + a / 2 + fs * 1.15 * (i - (lines.length - 1) / 2) + fs * .36, ln, label, `font-size="${fs.toFixed(1)}"`); });
  return s;
}
const cap = (x, y, r, fill = 'url(#obCap)') => `<circle cx="${x}" cy="${y}" r="${r}" fill="${fill}"/><circle cx="${x}" cy="${y}" r="${r * .62}" fill="none" stroke="rgba(255,255,255,.35)" stroke-width=".7"/><path d="M${x - r * .4} ${y}h${r * .8}M${x} ${y - r * .4}v${r * .8}" stroke="rgba(0,0,0,.35)" stroke-width=".8"/>`;
const smalls = (x, y, n, dx = 7) => { let s = ''; for (let i = 0; i < n; i++) s += R(x + i * dx, y, 4, 2, '#8c6b3a', 'rx=".4"') + R(x + i * dx + 1.2, y, 1.6, 2, '#d5d0c4'); return s; };
/** Pin header: cols x rows pins, pitch p. */
function header(x, y, cols, rows, p = 4, shroud = false) {
  let s = shroud ? `<rect x="${x - 3}" y="${y - 3}" width="${(cols - 1) * p + 6}" height="${(rows - 1) * p + 6}" rx="1" fill="#141416" stroke="#2b2b2e" stroke-width=".6"/>` : '';
  for (let c = 0; c < cols; c++) for (let r = 0; r < rows; r++) s += R(x + c * p - .9, y + r * p - .9, 1.8, 1.8, '#d7c07a');
  return s;
}
/** A paper label (EPROM stickers, drive labels). */
const paper = (x, y, w, h, lines, fs = 5.2, cls = 'paper') => {
  let s = `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx=".8" fill="url(#obPaper)"/>`;
  lines.forEach((ln, i) => { s += TC(x + w / 2, y + h / 2 + fs * 1.2 * (i - (lines.length - 1) / 2) + fs * .36, ln, cls, `font-size="${fs}"`); });
  return s;
};
const screw = (x, y, r = 3.2) => `<circle cx="${x}" cy="${y}" r="${r}" fill="url(#obScrew)" stroke="rgba(0,0,0,.45)" stroke-width=".5"/><path d="M${x - r * .6} ${y}h${r * 1.2}M${x} ${y - r * .6}v${r * 1.2}" stroke="rgba(0,0,0,.55)" stroke-width=".8"/>`;
const dsub = (x, y, w, rows = 2, c = '#26282b') => {
  let s = `<path d="M${x} ${y}h${w}l-2.2 8h${-(w - 4.4)}z" fill="${c}" stroke="#76797d" stroke-width=".8"/>`;
  for (let r = 0; r < rows; r++) { const n = Math.max(3, Math.round((w - 6) / 3.2)) - r; for (let i = 0; i < n; i++) s += `<circle cx="${x + 3 + (r ? 1.6 : 0) + i * ((w - 6 - (r ? 3.2 : 0)) / Math.max(1, n - 1))}" cy="${y + 2.4 + r * 3.2}" r=".7" fill="#9a9da1"/>`; }
  return s;
};
const jack = (x, y, c = '#1b1c1e') => `<circle cx="${x}" cy="${y}" r="3.6" fill="${c}" stroke="#8a8d91" stroke-width=".9"/><circle cx="${x}" cy="${y}" r="1.3" fill="#050505"/>`;

// ------------------------------------------------------------------ the cards (local coords: bracket at y 0, fingers down the left edge)
function fingers(bits) {
  let s = '';
  for (let y = 58; y < 206; y += 4.9) s += R(-.5, y, 6.5, 3.3, 'url(#obGold)');
  if (bits === 16) for (let y = 216; y < 274; y += 4.9) s += R(-.5, y, 6.5, 3.3, 'url(#obGold)');
  return s;
}
function bracket(inner = '') {
  return `<rect x="-3" y="-1" width="${W + 6}" height="17" rx="1.5" fill="url(#obBracket)" stroke="rgba(0,0,0,.35)" stroke-width=".6"/>
    <rect x="-3" y="14" width="${W + 6}" height="2.2" fill="rgba(0,0,0,.22)"/>${screw(W - 4, 7.5, 2.6)}${inner}`;
}
function pcb(len, fill, bits) {
  const notch = bits === 16 ? `<rect x="-1" y="207.5" width="4" height="7" fill="#0d0e0f" opacity=".0"/>` : '';
  return `<path d="M0 12H${W}V${len - 4}q0 4 -4 4H4q-4 0 -4 -4z" fill="${fill}"/>
    <path d="M0 12H${W}V${len - 4}q0 4 -4 4H4q-4 0 -4 -4z" fill="url(#obTraces)" opacity=".55"/>
    <path d="M.6 12.6H${W - .6}" stroke="rgba(255,255,255,.18)" stroke-width="1"/>${notch}`;
}
const title = (lines, y = 30, fs = 7.4) => lines.map((l, i) => TC(W / 2 + 3, y + i * (fs + 1.6), l, i ? 'silk sub' : 'silk strong', `font-size="${i ? fs * .72 : fs}"`)).join('');

export function cardSvg(id, { speed = 2400, midi = true, game = true } = {}) {
  const c = CARDS[id], L = c.len;
  let s = '';
  switch (id) {
    case 'vga':
      s += bracket(dsub(30, 3.6, 28, 3, '#1f3f7a'));
      s += pcb(L, 'url(#obPcbDeep)', 16);
      s += title(['VGA 256K', 'EUROPA MICRO SYSTEMS']);
      s += quad(22, 56, 52, ['EMS', 'VGA-1', '8836']);
      s += dip(18, 128, 62, 16, ['G171 RAMDAC']);
      s += `<rect x="72" y="152" width="11" height="16" rx="3" fill="url(#obCan)"/>` + T(54, 164, '28.322', 'silk tiny');
      for (let i = 0; i < 4; i++) s += dip(16 + (i % 2) * 34, 188 + Math.floor(i / 2) * 26, 28, 14, ['41464']);
      s += dip(16, 250, 58, 18, [], { fill: 'url(#obChip)' }) + paper(24, 252, 44, 14, ['VGA BIOS', 'v2.10'], 4.4);
      s += smalls(18, 176, 8) + cap(80, 110, 4.2);
      break;
    case 'hercules':
      s += bracket(dsub(8, 3.6, 22, 2) + dsub(38, 3.6, 44, 2));
      s += pcb(L, 'url(#obPcbOlive)', 8);
      s += title(['MONOCHROME', 'GRAPHICS · HGC 720×348'], 30, 7);
      s += dip(18, 62, 64, 18, ['HD6845SP', 'CRTC']);
      s += dip(18, 92, 30, 16, ['43256']) + dip(52, 92, 30, 16, ['43256']);
      for (let i = 0; i < 12; i++) s += dip(14 + (i % 3) * 24, 122 + Math.floor(i / 3) * 22, 20, 12, ['74LS'], { label: 'chip small' });
      s += dip(18, 216, 64, 16, ['CHAR ROM', '2364']);
      s += `<rect x="70" y="246" width="12" height="18" rx="3" fill="url(#obCan)"/>` + T(56, 274, '16.257MHz', 'silk tiny');
      s += smalls(14, 252, 7) + cap(24, 290, 4.2) + cap(40, 290, 4.2) + T(12, 312, 'LPT 3BC', 'silk tiny');
      break;
    case 'sb16':
      s += bracket(jack(10, 7.5) + jack(21, 7.5) + jack(32, 7.5) + `<rect x="40" y="2.5" width="8" height="10" rx="1" fill="#34373b"/><path d="M41 4h6M41 6h6M41 8h6M41 10h6" stroke="#9aa0a6" stroke-width=".6"/>` + dsub(52, 3.6, 30, 2));
      s += pcb(L, 'url(#obPcb)', 16);
      s += title(['SOUND BLASTER', '16 · ASP · CT2230']);
      s += quad(16, 56, 40, ['CT1745', 'MIXER']) + quad(62, 60, 22, ['CT', '1741'], { label: 'chip small' });
      s += dip(16, 108, 60, 16, ['YMF262-M', 'OPL3']);
      s += dip(16, 132, 60, 14, ['CT1741 DSP 4.05']);
      s += cap(22, 164, 5) + cap(36, 164, 5) + cap(50, 164, 3.6) + cap(62, 164, 3.6) + cap(75, 166, 5.5);
      s += dip(16, 178, 32, 12, ['TDA1517']) + dip(54, 178, 26, 12, ['74HC'], { label: 'chip small' });
      s += header(20, 214, 13, 2, 4.6, true) + T(18, 230, 'J11 WAVETABLE', 'silk tiny');
      s += T(12, 250, 'IRQ 7  DMA 1/5', 'silk tiny') + header(16, 256, 6, 2, 4) + `<rect x="${16 + 1 * 4 - 2.6}" y="253.4" width="5.2" height="9.2" rx=".8" fill="#161616"/>`;
      s += smalls(14, 276, 9) + smalls(14, 286, 9);
      if (midi) s += `<g class="ob-daughter" data-part="midi">${daughter()}</g>`;

      break;
    case 'adlib':
      s += bracket(jack(14, 7.5) + `<circle cx="34" cy="7.5" r="4.4" fill="#2a2c2f" stroke="#8a8d91" stroke-width=".8"/><path d="M34 4v3.6" stroke="#d0d3d6" stroke-width="1"/>` + T(46, 10, 'VOL', 'silk tiny dark'));
      s += pcb(L, 'url(#obPcb)', 8);
      s += title(['FM MUSIC', 'ADLIB COMPATIBLE · 388h'], 30, 7.4);
      s += dip(16, 62, 62, 18, ['YMF262', 'FM OPL3']);
      s += dip(16, 92, 28, 14, ['YAC512']) + dip(50, 92, 28, 14, ['LM386']);
      for (let i = 0; i < 4; i++) s += dip(14 + (i % 2) * 36, 118 + Math.floor(i / 2) * 20, 30, 12, ['74LS245'], { label: 'chip small' });
      s += `<rect x="16" y="160" width="12" height="16" rx="3" fill="url(#obCan)"/>` + T(32, 172, '14.318', 'silk tiny') + cap(66, 168, 5.5);
      break;
    case 'modem': {
      s += bracket(`<rect x="12" y="3" width="12" height="9" rx="1" fill="#1b1c1e" stroke="#8a8d91" stroke-width=".7"/><rect x="30" y="3" width="12" height="9" rx="1" fill="#1b1c1e" stroke="#8a8d91" stroke-width=".7"/>` + T(50, 10.4, 'LINE  PHONE', 'silk tiny dark'));
      s += pcb(L, 'url(#obPcbTeal)', 8);
      s += title(['SMARTLINE ' + SPEED_NAME[speed], 'INTERNAL MODEM · COM2'], 30, 7.2);
      s += quad(16, 56, 34, [speed > 2400 ? 'RC288' : 'RC224', 'ATF'], { label: 'chip small' });
      s += dip(56, 58, 28, 14, ['16550A']) + dip(56, 78, 28, 12, ['27C256'], { label: 'chip small' });
      s += `<rect x="14" y="104" width="26" height="22" rx="2" fill="url(#obIvory)"/>` + TC(27, 117, 'DAA', 'paper', 'font-size="5"');
      s += `<rect x="46" y="104" width="18" height="14" rx="1" fill="#2d3035"/>` + TC(55, 113, 'RLY', 'chip small', 'font-size="4.4"');
      s += `<g class="ob-dip" data-part="speed">`;
      s += `<rect x="14" y="138" width="46" height="16" rx="1" fill="#b3261c"/>`;
      [2400, 14400, 33600, 56000].forEach((v, i) => {
        s += `<rect x="${17 + i * 10.6}" y="140.5" width="7" height="11" rx=".8" fill="#f2efe8"/><rect x="${17 + i * 10.6 + 1}" y="${v === speed ? 141.5 : 146}" width="5" height="4.5" fill="#2b2b2b"/>`;
        s += TC(20.5 + i * 10.6, 160, String(i + 1), 'silk tiny');
      });
      s += T(14, 135, 'SW1 SPEED     ON ▲', 'silk tiny') + T(14, 167, '1:2400 2:14.4 3:33.6 4:56K', 'silk tiny', 'font-size="4"') + `</g>`;
      s += `<circle cx="72" cy="150" r="11" fill="#202225"/><circle cx="72" cy="150" r="8.4" fill="none" stroke="#3b3e42" stroke-width="1.4"/><circle cx="72" cy="150" r="3" fill="#3b3e42"/>`;
      s += smalls(14, 176, 10) + dip(16, 190, 30, 12, ['LM324'], { label: 'chip small' }) + cap(62, 198, 5) + cap(76, 198, 4);
      s += T(14, 224, 'FCC PART 68 · REN 0.9B', 'silk tiny');
      break;
    }
    case 'io':
      s += bracket(dsub(8, 3.6, 22, 2) + dsub(38, 3.6, 44, 2));
      s += pcb(L, 'url(#obPcb)', 8);
      s += title(['MULTI I/O', 'COM1 3F8 · LPT1 378'], 30, 7.4);
      s += dip(16, 58, 62, 16, ['NS16450', 'UART']);
      s += dip(16, 84, 44, 12, ['74LS374']) + dip(16, 102, 44, 12, ['74LS244']);
      s += dip(16, 120, 28, 12, ['1488']) + dip(50, 120, 28, 12, ['1489']);
      s += header(18, 146, 6, 2, 4) + `<rect x="${18 + 4 - 2.6}" y="143.4" width="5.2" height="9.2" rx=".8" fill="#161616"/>` + T(46, 152, 'J1 ADDR', 'silk tiny');
      s += smalls(14, 166, 9);
      // the game port (201h, the DA-15 on the bracket's ribbon) and its enable jumper J2
      s += `<g class="ob-gamejp" data-part="game" tabindex="0" role="button" aria-label="J2, the game port jumper: ${game ? 'enabled' : 'disabled'}; click to move the cap">`;
      s += `<rect x="12" y="176" width="72" height="18" fill="transparent"/>` + header(18, 185, 3, 1, 4) + `<rect x="${game ? 15.4 : 19.4}" y="180.4" width="9.2" height="9.2" rx=".8" fill="#161616"/>`;
      s += T(34, 188, game ? 'J2 GAME ON' : 'J2 GAME OFF', 'silk tiny') + `</g>` + dip(16, 198, 28, 12, ['NE558'], { label: 'chip small' }) + T(48, 207, 'GAME 201h', 'silk tiny');
      break;
    case 'ide':
      s += bracket(`<path d="M10 4h70M10 7h70M10 10h70" stroke="rgba(0,0,0,.3)" stroke-width="1.2"/>`);
      s += pcb(L, 'url(#obPcbDeep)', 16);
      s += title(['IDE / FLOPPY', 'AT BUS CONTROLLER'], 30, 7.4);
      s += quad(18, 56, 36, ['FDC', '37C65'], { label: 'chip small' }) + dip(60, 58, 24, 30, [], {}) + TC(72, 76, '7406', 'chip small', 'font-size="4.4"');
      s += dip(16, 104, 64, 14, ['EMS 82C710 IDE']);
      s += smalls(16, 126, 9);
      s += header(26, 150, 17, 2, 3.6, true) + T(18, 146, 'J2 FLOPPY', 'silk tiny');
      s += header(22, 190, 20, 2, 3.2, true) + T(18, 186, 'J3 PRIMARY IDE', 'silk tiny');
      s += header(22, 230, 20, 2, 3.2, true) + T(18, 226, 'J4 SECONDARY IDE', 'silk tiny');
      s += `<rect x="14" y="252" width="64" height="12" rx="1" fill="rgba(0,0,0,.25)"/>` + TC(46, 260, 'LED  ·  HDD ACTIVITY', 'silk tiny');
      break;
  }
  s += fingers(c.bits);
  if (c.fixed) s += `<circle cx="${W - 4}" cy="7.5" r="1.4" fill="#d8392b"/>`;      // the red "don't" dab on the screw
  return s;
}
function daughter() {
  // the GS wavetable daughterboard on the SB16's J11 (an MPU-401 with a General MIDI synth behind it)
  return `<rect x="10" y="196" width="74" height="100" rx="2" fill="url(#obPcbBlue)" filter="url(#obLift)"/>
    <rect x="10" y="196" width="74" height="100" rx="2" fill="url(#obTraces)" opacity=".5"/>
    ${TC(47, 208, 'GS WAVETABLE', 'silk strong', 'font-size="6.6"')}${TC(47, 216, 'MIDI · MPU-401 · GM/GS', 'silk sub', 'font-size="4.2"')}
    ${quad(18, 224, 28, ['GS-1', 'SYNTH'], { label: 'chip small' })}${dip(52, 226, 26, 24, ['ROM', '4MB'], { label: 'chip small' })}
    ${dip(18, 262, 28, 12, ['DAC'], { label: 'chip small' })}${dip(52, 262, 26, 12, ['SRAM'], { label: 'chip small' })}
    ${smalls(18, 284, 8)}`;
}

// ------------------------------------------------------------------ the chassis
function simm(size) {                // one 30-pin SIMM seen from above, 276 x 22
  const n = size === '256K' ? 3 : 9, big = size === '4MB';
  let s = `<rect x="0" y="0" width="276" height="22" rx="1.5" fill="url(#obSimm)"/>`;
  for (let i = 0; i < 26; i++) s += R(6 + i * 10.3, 19.2, 6, 2.8, 'url(#obGold)');
  const cw = big ? 24 : n === 3 ? 34 : 20, gap = n === 3 ? 56 : big ? 5 : 8.4;
  for (let i = 0; i < n; i++) s += `<rect x="${12 + i * (cw + gap)}" y="${big ? 2.6 : 3.6}" width="${cw}" height="${big ? 14 : 12}" rx=".8" fill="url(#obChip)"/>`;
  s += T(n === 3 ? 60 : 244, n === 3 ? 13 : 13.6, `${size} 70`, 'silk tiny', n === 3 ? '' : 'text-anchor="end" opacity="0"');
  s += `<rect x="248" y="4" width="22" height="10" rx="1" fill="url(#obPaper)"/>` + TC(259, 11, size, 'paper', 'font-size="5"');
  return s;
}
/** Which SIMMs are in which sockets for a size (MB): two banks of four 9-bit SIMMs (32 bits + parity). */
export function simmsFor(mb) {
  return { 1: ['256K', 4], 2: ['256K', 8], 4: ['1MB', 4], 8: ['1MB', 8], 16: ['4MB', 4] }[mb] || ['4MB', 4];
}

export class OpenBox {
  constructor({ state, sound, line, gm, cd, setMonitor, lastPhosphor = () => 'green' }) {
    this.state = state; this.sound = sound; this.line = line; this.gm = gm; this.cd = cd;
    this.setMonitor = setMonitor; this.lastPhosphor = lastPhosphor;
    this.powered = false; this.open = false;
    this.cfg = this.load();
    this.build();
  }

  // ------------------------------------------------------------ configuration
  load() {
    const p = prefs.get('hw', null) || {};
    const c = { ...FACTORY, slots: [...FACTORY.slots] };
    if (RAMS.includes(p.ram)) c.ram = p.ram;
    if (CLOCKS.includes(p.mhz)) c.mhz = p.mhz;
    for (const k of ['modem', 'cdrom', 'mouse', 'game']) if (typeof p[k] === 'boolean') c[k] = p[k];
    if (Array.isArray(p.slots) && p.slots.length === 7 && p.slots.every((x) => x === null || CARDS[x])) c.slots = [...p.slots];
    // the cards follow the sound setting / the monitor and fixed cards are always in their slots
    c.slots[0] = 'io'; c.slots[6] = 'ide';
    if (['sb16', 'adlib', 'none'].includes(p.sound)) c.sound = p.sound;
    this.syncSlots(c);
    return c;
  }
  save() {
    const { ram, mhz, sound, modem, cdrom, mouse, game, slots } = this.cfg;
    prefs.set('hw', { ram, mhz, sound, modem, cdrom, mouse, game, slots });
  }
  get video() { return this.state.monitor === 'vga' ? 'vga' : 'hercules'; }
  get midi() { return !!this.gm?.enabled; }
  get speed() { return this.line?.speed || 2400; }
  /** Make the slots agree with the settings (a card for each setting, none for what is out). */
  syncSlots(c = this.cfg) {
    const want = new Set(['io', 'ide', this.video]);
    if (c.sound !== 'none') want.add(c.sound);
    if (c.modem) want.add('modem');
    for (let i = 0; i < 7; i++) if (c.slots[i] && !want.has(c.slots[i])) c.slots[i] = null;
    for (const id of want) if (!c.slots.includes(id)) {
      const i = this.freeSlot(id, c);
      if (i >= 0) c.slots[i] = id;
    }
  }
  freeSlot(id, c = this.cfg, not = -1) {
    const bits = CARDS[id].bits;
    for (let i = 1; i <= 5; i++) if (i !== not && !c.slots[i] && (bits === 8 || SLOT16[i])) return i;
    return -1;
  }
  hardware() {
    const c = this.cfg;
    return { ram: c.ram, mhz: c.mhz, sound: c.sound, mouse: c.mouse, modem: c.modem, cdrom: c.cdrom, joystick: this.gamePort !== 'none' };
  }
  /** Who answers at 201h: the Sound Blaster 16's game port, else the multi-I/O card's (jumper J2). */
  get gamePort() { return this.cfg.sound === 'sb16' ? 'sb16' : this.cfg.game ? 'io' : 'none'; }
  machineOptions() {
    const h = this.hardware();
    const o = { ram: h.ram, mhz: h.mhz, sound: h.sound, mouse: h.mouse, modem: h.modem, joystick: h.joystick };
    if (!h.cdrom) o.cdrom = false;
    return o;
  }
  /** Before a power cycle of an existing machine. */
  apply(m) {
    const h = this.hardware();
    m.setHardware({ ...h, cdrom: h.cdrom, video: this.video, monitor: this.video === 'vga' ? undefined : this.state.monitor });
    if (m.mpu) m.mpu.present = this.midi;
    this.applied = this.snapshot();
  }
  snapshot() { return JSON.stringify({ ...this.hardware(), video: this.state.monitor, midi: this.midi && this.cfg.sound === 'sb16' }); }
  /** The page's other controls: MIDI switch, cd bay, modem panel, mouse button. */
  propagate() {
    const c = this.cfg;
    this.cd?.setPresent?.(c.cdrom);
    this.line?.setPresent?.(c.modem);
    const mb = $('mouseBtn');
    if (mb) { mb.disabled = !c.mouse; mb.title = c.mouse ? 'Capture the mouse for DOS programs (or Shift+click the screen)' : 'No mouse is plugged in (open the case: it goes in the PS/2 port)'; }
  }

  // ------------------------------------------------------------ DOM
  build() {
    // gradients etc. live in a hidden SVG that is always in the document (the parts bin's
    // thumbnails use them too)
    if (!$('obDefs')) {
      const d = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
      d.id = 'obDefs'; d.setAttribute('width', '0'); d.setAttribute('height', '0'); d.setAttribute('aria-hidden', 'true');
      d.style.cssText = 'position:absolute;width:0;height:0;overflow:hidden';
      d.innerHTML = DEFS;
      document.body.append(d);
    }
    // the way in: the front panel's keylock (on an AT it locked the cover); the toolbar and the app bar get buttons too
    this.btn = el('button', { class: 'pushbtn ob-keybtn', id: 'openCaseBtn', type: 'button', 'aria-expanded': 'false', 'aria-controls': 'openbox', title: 'Turn the key and take the lid off: cards, memory, the clock jumper' },
      el('span', { class: 'keylock', 'aria-hidden': 'true' }, el('i')), el('em', { text: 'OPEN CASE' }));
    this.btn.onclick = () => this.toggle();
    this.warn = el('div', { class: 'ob-warn', role: 'alert', hidden: true },
      el('b', { text: 'The machine is switched on.' }), el('span', { text: ' Switch it off before you touch anything inside: cards, SIMMs and jumpers only go in or out with the power off.' }));
    this.note = el('p', { class: 'ob-note', 'aria-live': 'polite' });
    this.stage = el('div', { class: 'ob-stage' });
    this.scroll = el('div', { class: 'ob-scroll' }, this.stage);
    this.lid = el('div', { class: 'ob-lid', 'aria-hidden': 'true' });
    this.lid.innerHTML = `<div class="ob-lid-rear"><i class="thumb"></i><i class="thumb"></i><i class="thumb"></i></div>
      <div class="ob-lid-panel"></div><div class="ob-lid-vents">${'<i></i>'.repeat(18)}</div>
      <div class="ob-lid-badge">EUROPA<small>ARM/AT</small></div>
      <div class="ob-lid-sticker"><b>CAUTION</b>Disconnect power before removing the cover. No user-serviceable parts inside, except all of them.</div>`;
    this.stage.append(this.lid);
    this.bin = el('div', { class: 'ob-bin', 'aria-label': 'Parts bin' });
    this.binList = el('div', { class: 'ob-bin-list' });
    this.bin.append(el('div', { class: 'ob-bin-cap' }, el('b', { text: 'PARTS BIN' }), el('span', { text: 'anti-static · drag a part into a slot, or click it' })), this.binList);
    this.ramSel = this.segmented('SIMMs', RAMS.map((v) => [v, v + ' MB']), (v) => this.change(() => { this.cfg.ram = v; }, `${v} MB of SIMMs`), 'obRam');
    this.clkSel = this.segmented('CPU clock (J1)', CLOCKS.map((v) => [v, v + '']), (v) => this.change(() => { this.cfg.mhz = v; }, `The clock jumper on ${v} MHz`), 'obClk', 'MHz');
    this.summary = el('dl', { class: 'ob-summary' });
    this.factory = el('button', { class: 'btn small ob-factory', type: 'button', text: 'Factory configuration' });
    this.factory.onclick = () => this.change(() => this.resetFactory(), 'Factory configuration restored');
    this.closeBtn = el('button', { class: 'btn small ob-close', type: 'button', text: 'Put the lid back' });
    this.closeBtn.onclick = () => this.toggle(false);
    this.sheet = el('div', { class: 'ob-sheet' },
      el('div', { class: 'ob-sheet-head' }, el('b', { text: 'ARM/AT' }), el('span', { text: 'SYSTEM BOARD REV. C · CONFIGURATION' })),
      this.ramSel.root, this.clkSel.root, this.summary,
      el('div', { class: 'ob-sheet-actions' }, this.factory, this.closeBtn));
    this.root = el('section', { class: 'openbox', id: 'openbox', hidden: true, 'aria-label': 'Inside the system unit' },
      el('div', { class: 'ob-head' }, el('h2', {}, 'Inside the ARM/AT ', el('span', { class: 'h2-sub', text: 'changes take effect at the next power-on' })), this.appClose()),
      this.warn, this.scroll, el('div', { class: 'ob-bench' }, this.bin, this.sheet), this.note);
    this.render();
  }
  appClose() { const b = el('button', { class: 'ob-x', type: 'button', 'aria-label': 'Close', text: '✕' }); b.onclick = () => this.toggle(false); return b; }
  segmented(label, opts, onPick, id, unit = '') {
    const root = el('div', { class: 'ob-seg', role: 'radiogroup', 'aria-label': label });
    const btns = opts.map(([v, t]) => { const b = el('button', { type: 'button', role: 'radio', 'data-v': String(v), text: t }); b.onclick = () => onPick(v); return b; });
    root.append(el('span', { class: 'ob-seg-cap', text: label + (unit ? ` (${unit})` : '') }), el('div', { class: 'ob-seg-row', id }, ...btns));
    return { root, set: (v) => { for (const b of btns) { const on = b.dataset.v === String(v); b.setAttribute('aria-checked', String(on)); b.classList.toggle('on', on); } } };
  }
  mount(caseEl) {
    const lock = caseEl.querySelector('.buttons > .keylock');
    if (lock) lock.replaceWith(this.btn); else caseEl.querySelector('.buttons')?.append(this.btn);
    // the panel spans the page below the desk: the board needs the room to be legible
    const desk = caseEl.closest('.desk');
    if (desk) desk.after(this.root); else caseEl.after(this.root);
    // the toolbar and the app bar get a way in too
    const tb = $('cadBtn');
    if (tb) { this.tbBtn = el('button', { class: 'key', id: 'openCaseKey', type: 'button', title: 'Open the case: cards, memory, clock', text: 'Open case' }); this.tbBtn.onclick = () => this.toggle(); tb.parentNode.insertBefore(this.tbBtn, $('crtBtn')); }
    const kbd = $('abInsp');
    if (kbd) { this.abBtn = el('button', { class: 'ab', id: 'abCase', 'aria-pressed': 'false', title: 'Open the case', text: 'CASE' }); this.abBtn.onclick = () => this.toggle(); kbd.before(this.abBtn); }
    this.propagate();
  }

  toggle(force) {
    const open = force ?? !this.open;
    if (open === this.open) return;
    this.open = open;
    this.sound?.init?.();
    for (const b of [this.btn, this.tbBtn, this.abBtn]) if (b) { b.setAttribute('aria-expanded', String(open)); b.setAttribute('aria-pressed', String(open)); }
    clearTimeout(this.lidT);
    if (open) {
      this.render();
      this.root.hidden = false;
      document.documentElement.classList.add('ob-open');
      this.root.classList.remove('lid-off', 'closing');
      void this.root.offsetWidth;                  // restart the animation
      this.sound?.click?.('latch');
      this.root.classList.add('lid-off');
      if (!document.documentElement.classList.contains('app')) requestAnimationFrame(() => this.root.scrollIntoView({ block: 'nearest', behavior: 'smooth' }));
    } else {
      this.root.classList.add('closing');
      this.root.classList.remove('lid-off');
      this.lidT = setTimeout(() => { this.root.hidden = true; this.root.classList.remove('closing'); document.documentElement.classList.remove('ob-open'); }, 720);
      this.sound?.click?.('latch');
    }
  }
  setPowered(on) {
    this.powered = on;
    this.root.classList.toggle('powered', on);
    this.warn.hidden = true;
    this.render();
  }
  /** Every change goes through here: refused while the power is on. */
  change(fn, msg) {
    if (this.powered) { this.refuse(); return false; }
    fn();
    this.syncSlots();
    this.save();
    this.propagate();
    this.render();
    this.say(msg ? `${msg}. It takes effect when you switch the machine on.` : '');
    this.sound?.click?.('button');
    return true;
  }
  refuse() {
    this.warn.hidden = false;
    this.warn.classList.remove('shake'); void this.warn.offsetWidth; this.warn.classList.add('shake');
    this.root.classList.remove('zap'); void this.root.offsetWidth; this.root.classList.add('zap');
    this.sound?.click?.('button');
    clearTimeout(this.warnT); this.warnT = setTimeout(() => { this.warn.hidden = true; }, 6000);
  }
  say(t) { this.note.textContent = t; }
  /** Something outside changed a card (the Monitor selector, the MIDI switch, the speed switch). */
  external() { this.syncSlots(); this.save(); this.render(); }
  resetFactory() {
    const f = FACTORY;
    Object.assign(this.cfg, { ram: f.ram, mhz: f.mhz, sound: f.sound, modem: f.modem, cdrom: f.cdrom, mouse: f.mouse, game: f.game, slots: [...f.slots] });
    if (this.state.monitor !== 'vga') this.setMonitor?.('vga');
    if (!this.midi) this.gm?.setEnabled?.(true);
    if (this.speed !== 2400) this.line?.setSpeed?.(2400);
  }

  // ------------------------------------------------------------ actions
  installed(id) { return this.cfg.slots.includes(id); }
  /** Put a card in (slot i, or wherever it fits). Returns a message when it cannot (and changes nothing). */
  install(id, i = -1) {
    const card = CARDS[id], c = this.cfg;
    const fits = (cid, k) => CARDS[cid].bits === 8 || SLOT16[k];
    const from = c.slots.indexOf(id);
    const rival = card.group ? c.slots.find((x) => x && x !== id && CARDS[x].group === card.group) : null;
    const rivalAt = rival ? c.slots.indexOf(rival) : -1;
    let move = null;                               // [slot, card]: a card that makes room
    if (i < 0) {
      if (from >= 0) return null;
      i = rivalAt >= 0 && fits(id, rivalAt) ? rivalAt : this.freeSlot(id);
      if (i < 0) return card.bits === 16 ? `There is no free 16-bit slot for the ${card.name}.` : 'There is no free slot for it.';
    } else {
      if (!fits(id, i)) return `The ${card.name} is a 16-bit card: it needs a slot with the AT extension connector.`;
      const occ = c.slots[i];
      if (occ === id) return null;
      if (occ && CARDS[occ].fixed) return `The ${CARDS[occ].name} stays in that slot.`;
      if (occ && occ !== rival) {                  // swap with where the card came from, or shift the other card over
        const j = from >= 0 && fits(occ, from) ? from : this.freeSlot(occ, c, i);
        if (j < 0) return `Slot ${i + 1} is taken and its card has nowhere to go.`;
        move = [j, occ];
      }
    }
    if (from >= 0) c.slots[from] = null;
    if (rivalAt >= 0) c.slots[rivalAt] = null;
    if (move) c.slots[move[0]] = move[1];
    c.slots[i] = id;
    if (card.group === 'video') this.setMonitor?.(id === 'vga' ? 'vga' : this.lastPhosphor());
    if (card.group === 'sound') c.sound = id;
    if (id === 'modem') c.modem = true;
    return null;
  }
  remove(id) {
    const card = CARDS[id];
    if (card.fixed) return `The ${card.name} stays in: the machine cannot boot without it.`;
    if (card.group === 'video') return 'A PC needs a display card. Drag the other one in to swap them.';
    const i = this.cfg.slots.indexOf(id);
    if (i >= 0) this.cfg.slots[i] = null;
    if (card.group === 'sound') this.cfg.sound = 'none';
    if (id === 'modem') this.cfg.modem = false;
    return null;
  }
  act(kind, id, target) {
    // kind: 'card' (id) from the board or the bin; target: slot index, 'bin', or undefined (click)
    if (this.powered) { this.refuse(); return; }
    let err = null, msg = '';
    const t = () => {
      if (kind === 'card') {
        const inSlot = this.installed(id);
        if (target === 'bin' || (target === undefined && inSlot)) { err = this.remove(id); msg = `The ${CARDS[id].name} is in the parts bin`; }
        else { err = this.install(id, typeof target === 'number' ? target : -1); msg = `The ${CARDS[id].name} is in slot ${this.cfg.slots.indexOf(id) + 1}`; }
      } else if (kind === 'midi') {
        const on = target === 'bin' ? false : target === undefined ? !this.midi : true;
        this.gm?.setEnabled?.(on);
        msg = on ? (this.cfg.sound === 'sb16' ? 'The GS wavetable daughterboard is on the Sound Blaster 16' : 'The daughterboard goes on the Sound Blaster 16 (which is in the bin)') : 'The MIDI daughterboard is in the parts bin';
      } else if (kind === 'cdrom') {
        this.cfg.cdrom = target === 'bin' ? false : target === undefined ? !this.cfg.cdrom : true;
        msg = this.cfg.cdrom ? 'The CD-ROM drive is in its bay, on the secondary IDE cable' : 'The CD-ROM drive is in the parts bin';
      } else if (kind === 'mouse') {
        this.cfg.mouse = target === 'bin' ? false : target === undefined ? !this.cfg.mouse : true;
        msg = this.cfg.mouse ? 'The mouse is plugged into the PS/2 port' : 'The mouse is unplugged';
      } else if (kind === 'game') {
        this.cfg.game = !this.cfg.game;
        msg = `J2 on the multi-I/O card: its game port ${this.cfg.game ? 'enabled' : 'disabled'}` +
          (this.cfg.sound === 'sb16' ? ' (the Sound Blaster 16 has its own game port at 201h)' : '');
      } else if (kind === 'speed') {
        const S = [2400, 14400, 33600, 56000];
        this.line?.setSpeed?.(S[(S.indexOf(this.speed) + 1) % 4]);
        msg = `SW1 set to ${SPEED_NAME[this.speed]}`;
      } else if (kind === 'ram') {
        this.cfg.ram = RAMS[(RAMS.indexOf(this.cfg.ram) + 1) % RAMS.length];
        msg = `${this.cfg.ram} MB of SIMMs`;
      } else if (kind === 'mhz') {
        this.cfg.mhz = target;
        msg = `The clock jumper on ${target} MHz`;
      }
    };
    const ok = this.change(t, null);
    if (!ok) return;
    if (err) { this.say(err); this.flashNote(); return; }
    this.say(`${msg}. It takes effect when you switch the machine on.`);
  }
  flashNote() { this.note.classList.remove('err'); void this.note.offsetWidth; this.note.classList.add('err'); }

  // ------------------------------------------------------------ rendering
  render() {
    const c = this.cfg;
    this.stage.querySelector('svg.ob-board')?.remove();
    const tmp = document.createElement('div');
    tmp.innerHTML = this.boardSvg();
    const svg = tmp.firstElementChild;
    this.stage.insertBefore(svg, this.lid);
    this.board = svg;
    this.bindBoard(svg);
    this.renderBin();
    this.ramSel.set(c.ram); this.clkSel.set(c.mhz);
    const sound = { sb16: 'Sound Blaster 16', adlib: 'AdLib (FM only)', none: 'none' }[c.sound];
    const rows = [
      ['Memory', `${c.ram} MB (${simmsFor(c.ram)[1]} × ${simmsFor(c.ram)[0]} SIMMs)`],
      ['CPU clock', `${c.mhz} MHz`],
      ['Display', this.video === 'vga' ? 'VGA, colour monitor' : `Hercules, ${({ green: 'green', amber: 'amber', white: 'paper-white' })[this.state.monitor] || ''} monitor`],
      ['Sound', sound + (c.sound === 'sb16' && this.midi ? ' + GS wavetable' : '')],
      ['Modem', c.modem ? `internal, ${SPEED_NAME[this.speed]} (COM2)` : 'none'],
      ['Hard disks', 'C: and D:, 128 MB each, primary IDE'],
      ['CD-ROM', c.cdrom ? 'ATAPI drive, secondary IDE' : 'none'],
      ['Mouse', c.mouse ? 'PS/2' : 'none'],
      ['Game port', { sb16: '201h on the Sound Blaster 16', io: '201h on the multi-I/O card', none: 'none' }[this.gamePort]],
    ];
    this.summary.textContent = '';
    for (const [k, v] of rows) this.summary.append(el('dt', { text: k }), el('dd', { text: v }));
    const changed = this.powered && this.applied && this.applied !== this.snapshot();
    this.root.classList.toggle('pending', !!changed);
    if (changed && !this.note.textContent) this.say('The running machine has the old configuration until the next power-on.');
  }

  boardSvg() {
    const c = this.cfg;
    const slots = c.slots;
    let s = `<svg class="ob-board" viewBox="0 0 1200 820" role="img" aria-label="The ARM/AT's system board and drives, seen from above">`;
    // chassis tray
    s += `<rect x="0" y="0" width="1200" height="820" rx="10" fill="url(#obSteel)"/><rect x="0" y="0" width="1200" height="820" rx="10" fill="url(#obSpangle)" opacity=".5"/>`;
    s += `<rect x="0" y="0" width="1200" height="30" rx="8" fill="url(#obSteelDark)"/>`;
    s += `<rect x="0" y="802" width="1200" height="18" rx="6" fill="url(#obBeige)"/><rect x="0" y="802" width="1200" height="2" fill="rgba(0,0,0,.25)"/>`;
    s += T(18, 815, 'FRONT', 'stamp') + T(18, 21, 'REAR', 'stamp light');
    // the motherboard
    s += `<g class="ob-mb"><rect x="22" y="34" width="856" height="762" rx="5" fill="url(#obPcb)" filter="url(#obBoardShadow)"/>
      <rect x="22" y="34" width="856" height="762" rx="5" fill="url(#obTraces)" opacity=".75"/>
      <rect x="22" y="34" width="856" height="762" rx="5" fill="url(#obSheen)"/>`;
    for (const [x, y] of [[36, 380], [548, 368], [866, 368], [36, 782], [548, 784], [866, 782], [866, 48]]) s += `<circle cx="${x}" cy="${y}" r="6" fill="#d9d4a8"/><circle cx="${x}" cy="${y}" r="3.2" fill="#6f7479"/>`;
    s += T(214, 752, 'EUROPA MICRO SYSTEMS · ARM/AT · REV. C · © 1988', 'silk strong', 'font-size="9"');
    s += T(214, 765, 'MADE IN ENGLAND  ·  PCB 100-0417-03  ·  94V-0', 'silk sub', 'font-size="6.5"');
    // keyboard + mouse ports at the rear edge
    s += `<g class="ob-ports">${this.ports()}</g>`;
    // ISA slots
    for (let i = 0; i < 7; i++) {
      const x = SX(i);
      s += `<g class="ob-slot${SLOT16[i] ? ' s16' : ''}" data-slot="${i}">`;
      s += `<rect x="${x - 6}" y="52" width="16" height="156" rx="1.5" fill="url(#obSlot)"/>` + `<path d="M${x + 2} 56V204" stroke="#b99a4c" stroke-width="1.6" stroke-dasharray="1.4 3.5"/>`;
      if (SLOT16[i]) s += `<rect x="${x - 6}" y="212" width="16" height="66" rx="1.5" fill="url(#obSlot)"/><path d="M${x + 2} 216V274" stroke="#b99a4c" stroke-width="1.6" stroke-dasharray="1.4 3.5"/>`;
      s += TC(x + W / 2, 345, `J${i + 5}  ·  ${SLOT16[i] ? '16-BIT' : '8-BIT'}`, 'silk', 'font-size="6.4"');
      s += `<rect class="ob-drop" x="${x - 10}" y="26" width="${W + 20}" height="300" fill="transparent"/>`;
      if (!slots[i]) s += `<rect x="${x - 3}" y="6" width="${W + 6}" height="20" rx="1.5" fill="url(#obBracket)" stroke="rgba(0,0,0,.3)" stroke-width=".6"/>${screw(x + W - 4, 16, 2.6)}<path d="M${x + 8} 12h${W - 24}M${x + 8} 16h${W - 24}M${x + 8} 20h${W - 24}" stroke="rgba(0,0,0,.18)" stroke-width="1.4" stroke-linecap="round"/>`;
      s += `</g>`;
    }
    // board chips
    s += this.boardChips();
    s += `</g>`;
    // the cards (after the board so they sit on top)
    for (let i = 0; i < 7; i++) {
      const id = slots[i];
      if (!id) continue;
      s += `<g class="ob-card${CARDS[id].fixed ? ' fixed' : ''}" data-card="${id}" data-slot-of="${i}" transform="translate(${SX(i)} 10)" tabindex="0" role="button" aria-label="${esc(CARDS[id].name)} in slot ${i + 1}${CARDS[id].fixed ? ' (fixed)' : ': click to take it out'}"><g filter="url(#obCardShadow)">${cardSvg(id, { speed: this.speed, midi: this.midi, game: c.game })}</g></g>`;
    }
    // ribbons from the IDE card to the drives, power from the PSU
    s += this.cables();
    s += this.psu();
    s += this.drives();
    s += this.mouseCable();
    s += `</svg>`;
    return s;
  }

  ports() {
    let s = `<rect x="30" y="36" width="104" height="52" rx="3" fill="#232427"/>`;
    s += `<circle cx="58" cy="60" r="16" fill="#1a1a1c" stroke="#8b8f93" stroke-width="2"/>` + [0, 1, 2, 3, 4].map((k) => `<circle cx="${58 + 8 * Math.cos(Math.PI * (0.9 + k * 0.3))}" cy="${60 - 8 * Math.sin(Math.PI * (0.9 + k * 0.3))}" r="1.3" fill="#a8acb0"/>`).join('');
    s += `<g class="ob-mouseport" data-part="mouse-port"><circle cx="108" cy="60" r="12" fill="#1a1a1c" stroke="#3f8f4a" stroke-width="2.4"/>` + [0, 1, 2, 3, 4, 5].map((k) => `<circle cx="${108 + 6 * Math.cos(Math.PI * (k / 3 + .1))}" cy="${60 + 6 * Math.sin(Math.PI * (k / 3 + .1))}" r="1" fill="#a8acb0"/>`).join('') + `<rect class="ob-drop" x="92" y="36" width="34" height="56" fill="transparent"/></g>`;
    s += TC(58, 98, 'KEYBOARD', 'silk', 'font-size="6"') + TC(108, 98, 'PS/2 MOUSE', 'silk', 'font-size="6"');
    return s;
  }
  mouseCable() {
    if (!this.cfg.mouse) return '';
    return `<g class="ob-mouse" data-part="mouse" tabindex="0" role="button" aria-label="The mouse plug in the PS/2 port: click to unplug">
      <path d="M108 50C108 30 116 18 126 -4" stroke="#d6d2c6" stroke-width="5" fill="none" stroke-linecap="round"/>
      <path d="M108 50C108 30 116 18 126 -4" stroke="rgba(0,0,0,.18)" stroke-width="1.2" fill="none" transform="translate(1.6 0)"/>
      <rect x="99" y="48" width="18" height="24" rx="5" fill="url(#obIvory)" stroke="rgba(0,0,0,.3)" stroke-width=".7" filter="url(#obCardShadow)"/>
      <path d="M103 54h10M103 57h10" stroke="rgba(0,0,0,.2)" stroke-width="1"/></g>`;
  }
  boardChips() {
    const c = this.cfg;
    let s = '';
    // CPU (ceramic PGA with a gold lid) + the VFP9-S module
    s += `<g class="ob-cpu"><rect x="52" y="388" width="158" height="158" rx="4" fill="url(#obCeramic)" filter="url(#obCardShadow)"/>`;
    for (let i = 0; i < 17; i++) for (const [dx, dy] of [[i * 9, 0], [i * 9, 144], [0, i * 9], [144, i * 9]]) s += `<circle cx="${59 + dx}" cy="${395 + dy}" r="1.1" fill="#9f9784"/>`;
    s += `<rect x="78" y="414" width="106" height="106" rx="3" fill="url(#obLid)"/><rect x="78" y="414" width="106" height="106" rx="3" fill="none" stroke="rgba(0,0,0,.25)" stroke-width=".8"/>`;
    s += TC(131, 452, 'ARM926EJ-S', 'lidtext', 'font-size="11.5"') + TC(131, 466, 'RISC PROCESSOR', 'lidtext sub', 'font-size="6.5"') + TC(131, 486, 'EUROPA  ARM/AT', 'lidtext sub', 'font-size="6.5"') + TC(131, 497, `8834 · ${c.mhz} MHz`, 'lidtext sub', 'font-size="6.5"');
    s += `</g>` + TC(131, 560, 'U1  CPU', 'silk', 'font-size="7"');
    s += `<g class="ob-vfp">${header(232, 396, 2, 14, 9)}<rect x="226" y="392" width="112" height="134" rx="3" fill="url(#obPcbBlue)" filter="url(#obLift)"/><rect x="226" y="392" width="112" height="134" rx="3" fill="url(#obTraces)" opacity=".5"/>`;
    s += TC(282, 404, 'VFP9-S COPROCESSOR MODULE', 'silk', 'font-size="5.5"') + quad(250, 414, 64, ['VFP9-S', 'FLOATING', 'POINT', 'ARM · VFPv2']) + smalls(244, 492, 11) + smalls(244, 502, 11) + T(236, 518, 'J3  FPU', 'silk tiny') + `</g>`;
    // clock: oscillator + synthesizer + J1
    s += `<rect x="56" y="588" width="46" height="32" rx="7" fill="url(#obCan)"/>` + TC(79, 602, 'OSC', 'can', 'font-size="5.6"') + TC(79, 610, '14.318', 'can', 'font-size="5.6"');
    s += dip(112, 594, 50, 22, ['ICS 90C64', 'CLK SYNTH'], { label: 'chip small' });
    s += `<g class="ob-jumper" data-part="j1">` + T(176, 580, 'J1  CPU CLOCK (MHz)', 'silk', 'font-size="7.4"');
    s += `<rect x="176" y="588" width="176" height="34" rx="2" fill="#101112"/>`;
    CLOCKS.forEach((v, i) => {
      const x = 191 + i * 29;
      s += `<g class="ob-jpos" data-mhz="${v}" tabindex="0" role="button" aria-label="Jumper J1 on ${v} MHz"><rect x="${x - 14}" y="584" width="28" height="56" fill="transparent"/>`;
      for (const y of [596, 611]) s += `<rect x="${x - 2.6}" y="${y - 2.6}" width="5.2" height="5.2" fill="#3a3b3e"/><rect x="${x - 1.5}" y="${y - 1.5}" width="3" height="3" fill="url(#obGold)"/>`;
      s += TC(x, 636, String(v), 'silk', 'font-size="8"');
      if (v === c.mhz) s += `<rect x="${x - 6}" y="589.5" width="12" height="28" rx="2" fill="#1d1d20" stroke="#000" stroke-width=".6" filter="url(#obCardShadow)"/><rect x="${x - 4.4}" y="591" width="8.8" height="3" rx="1" fill="rgba(255,255,255,.22)"/><rect x="${x - 3}" y="606" width="6" height="7" rx="1" fill="#0b0b0c"/>`;
      s += `</g>`;
    });
    s += `</g>`;
    // chipset, KBC, RTC, the ISA glue
    s += quad(372, 396, 70, ['EMS 82C381', 'SYSTEM', 'CONTROLLER', '8836']) + quad(462, 396, 70, ['EMS 82C382', 'BUS', 'CONTROLLER', '8837']);
    s += dip(372, 490, 160, 20, ['8742  KEYBOARD CONTROLLER  ·  ARM/AT KB-BIOS']);
    s += dip(372, 522, 100, 18, ['MC146818AP  RTC']) + dip(480, 522, 52, 18, ['8259A'], { label: 'chip small' });
    s += dip(372, 552, 76, 16, ['8254-2 PIT'], { label: 'chip small' }) + dip(456, 552, 76, 16, ['8237A-5'], { label: 'chip small' });
    s += dip(372, 580, 76, 16, ['8237A-5'], { label: 'chip small' }) + dip(456, 580, 76, 16, ['74F245'], { label: 'chip small' });
    for (let i = 0; i < 6; i++) s += dip(372 + (i % 3) * 54, 612 + Math.floor(i / 3) * 24, 48, 14, ['74ALS'], { label: 'chip small' });
    s += smalls(372, 666, 20) + smalls(372, 676, 20) + cap(390, 700, 7) + cap(410, 700, 7) + cap(510, 700, 5) + cap(526, 700, 5);
    s += T(372, 722, 'U3 U4 U7 U8 U9 U10 U11 U12', 'silk sub', 'font-size="5.5"');
    // BIOS ROM with its label
    s += `<g class="ob-rom">${dip(566, 392, 164, 44, [], {})}<rect x="582" y="397" width="132" height="34" rx="1" fill="url(#obPaper)"/>`;
    s += TC(648, 409, 'EUROPA ARM/AT BIOS', 'paper strong', 'font-size="8.4"') + TC(648, 419, 'v1.00  (C)1988', 'paper', 'font-size="7"') + TC(648, 427.5, '27C080 · CHKSUM 7E41', 'paper sub', 'font-size="5"') + `</g>` + T(566, 448, 'U17  BIOS ROM', 'silk', 'font-size="7"');
    // the CMOS battery (by the RTC's corner of the board)
    s += `<g class="ob-bat"><rect x="56" y="690" width="132" height="34" rx="15" fill="url(#obBattery)" filter="url(#obCardShadow)"/><rect x="61" y="692.5" width="122" height="6.5" rx="3" fill="rgba(255,255,255,.28)"/>`;
    s += TC(128, 711, '3.6V NiCd · 60mAh', 'bat', 'font-size="8"') + T(66, 712, '+', 'bat', 'font-size="10"') + `</g>` + T(56, 738, 'BT1  CMOS BATTERY', 'silk', 'font-size="6.4"');
    // a keyboard BIOS EPROM next to the system BIOS, and the ISA bus buffers
    s += dip(748, 392, 112, 30, [], {}) + paper(758, 395, 92, 24, ['KB-BIOS  8742', '(C)1988 EUROPA'], 5.6) + T(748, 434, 'U18  KBC FIRMWARE', 'silk', 'font-size="6"');
    // SIMM sockets: two banks of four
    const [size, n] = simmsFor(c.ram);
    s += `<g class="ob-simms" data-part="simms" tabindex="0" role="button" aria-label="${c.ram} MB of SIMMs: click to change">`;
    s += T(566, 470, `BANK 0`, 'silk', 'font-size="7"') + T(566, 624, `BANK 1`, 'silk', 'font-size="7"') + T(700, 470, 'SIMM 30-PIN · 70ns · PARITY', 'silk sub', 'font-size="5.5"');
    for (let i = 0; i < 8; i++) {
      const y = 476 + i * 36 + (i >= 4 ? 10 : 0);
      s += `<rect x="566" y="${y}" width="296" height="28" rx="2" fill="#e7e1cf"/><rect x="570" y="${y + 22}" width="288" height="3" fill="#b9b19c"/>` +
        `<rect x="566" y="${y}" width="8" height="28" rx="1" fill="#d8d0bb"/><rect x="854" y="${y}" width="8" height="28" rx="1" fill="#d8d0bb"/>`;
      s += T(870, y + 17, `${i + 1}`, 'silk', 'font-size="6"');
      if (i < n) s += `<g transform="translate(576 ${y + 2})" filter="url(#obCardShadow)">${simm(size)}</g>`;
    }
    s += `<rect class="ob-drop" x="560" y="460" width="310" height="320" fill="transparent"/></g>`;
    // front-panel header, speaker wire
    s += header(62, 764, 10, 2, 5) + T(56, 756, 'J2  SPKR · RESET · TURBO · LED', 'silk', 'font-size="5.6"');
    s += `<path d="M64 772C70 786 110 796 130 806" stroke="#c23b2e" stroke-width="1.6" fill="none"/><path d="M69 772C76 788 116 798 136 806" stroke="#222" stroke-width="1.6" fill="none"/><path d="M99 772C110 788 160 798 180 806" stroke="#e0c040" stroke-width="1.6" fill="none"/><path d="M104 772C116 790 166 800 186 806" stroke="#3a7a3a" stroke-width="1.6" fill="none"/>`;
    // power connectors P8/P9
    s += `<rect x="848" y="92" width="22" height="120" rx="2" fill="#f1ede2" stroke="#b9b3a2" stroke-width=".8"/>` + header(859, 98, 1, 12, 9.6) + T(836, 226, 'P8 P9', 'silk', 'font-size="6"');
    return s;
  }
  cables() {
    const c = this.cfg;
    const ide = c.slots.indexOf('ide');
    if (ide < 0) return '';
    const x0 = SX(ide) + 60;
    const ribbon = (y0, x1, y1, w, label) => {
      const d = `M${x0 - 16} ${y0}C${x0 + 70 + (y1 - y0) * .08} ${y0} ${884 - (y1 - 300) * .02} ${y1 - 40} ${x1} ${y1}`;
      return `<path d="${d}" stroke="#cfcfd1" stroke-width="${w}" fill="none" filter="url(#obCardShadow)"/><path d="${d}" stroke="url(#obRibbon)" stroke-width="${w}" fill="none" opacity=".7"/><path d="${d}" stroke="#c0392b" stroke-width="1.6" fill="none" transform="translate(0 ${-w / 2 + 1})"/>` +
        `<rect x="${x0 - 30}" y="${y0 - 7}" width="26" height="14" rx="1.5" fill="#2b2c2f"/><rect x="${x1 - 6}" y="${y1 - 8}" width="14" height="16" rx="1.5" fill="#2b2c2f"/>` + (label ? '' : '');
    };
    let s = '';
    if (c.cdrom) s += ribbon(252, 916, 622, 15);
    s += ribbon(212, 928, 452, 15) + ribbon(172, 928, 356, 13);
    // PSU leads to P8/P9 and to the drives
    const lead = (d, cols) => cols.map((col, k) => `<path d="${d}" stroke="${col}" stroke-width="2.4" fill="none" transform="translate(${k * 2.6} ${k * 1.2})"/>`).join('');
    s += lead('M892 128C876 128 876 140 866 150', ['#e0c040', '#c0392b', '#1c1c1c', '#1c1c1c', '#e0772e', '#1c1c1c']);
    s += lead('M1140 290C1150 320 1154 340 1146 360', ['#e0c040', '#1c1c1c', '#1c1c1c', '#c0392b']);
    s += lead('M1120 290C1130 360 1150 420 1146 456', ['#e0c040', '#1c1c1c', '#1c1c1c', '#c0392b']);
    if (c.cdrom) s += lead('M1100 290C1110 420 1160 520 1150 596', ['#e0c040', '#1c1c1c', '#1c1c1c', '#c0392b']);
    return s;
  }
  psu() {
    let s = `<g class="ob-psu"><rect x="892" y="8" width="294" height="280" rx="4" fill="url(#obSteelDark)" filter="url(#obBoardShadow)"/><rect x="900" y="16" width="278" height="264" rx="3" fill="url(#obSteel)"/>`;
    s += `<circle cx="1060" cy="148" r="98" fill="#2a2c2f"/><g class="ob-fan" style="transform-origin:1060px 148px">`;
    for (let k = 0; k < 7; k++) s += `<path d="M1060 148C${1060 + 30} ${148 - 20} ${1060 + 80} ${148 - 30} ${1060 + 88} ${148 - 4}C${1060 + 70} ${148 + 8} ${1060 + 30} ${148 + 6} 1060 148z" fill="#3b3e43" transform="rotate(${k * 360 / 7} 1060 148)"/>`;
    s += `<circle cx="1060" cy="148" r="22" fill="#44484d"/><circle cx="1060" cy="148" r="7" fill="#2a2c2f"/></g>`;
    for (let r = 20; r <= 96; r += 12) s += `<circle cx="1060" cy="148" r="${r}" fill="none" stroke="#b9bdc1" stroke-width="2"/>`;
    for (let k = 0; k < 4; k++) s += `<path d="M1060 148l${96 * Math.cos(k * Math.PI / 2 + Math.PI / 4)} ${96 * Math.sin(k * Math.PI / 2 + Math.PI / 4)}" stroke="#b9bdc1" stroke-width="2.4"/>`;
    s += paper(912, 28, 96, 46, ['EUROPA POWER', '200 W · 115/230 V~', 'MODEL PS-200AT'], 6.2);
    s += `<rect x="912" y="226" width="96" height="42" rx="1" fill="#f3d23a"/>` + TC(960, 240, '⚠ HIGH VOLTAGE', 'warnlbl', 'font-size="7.6"') + TC(960, 251, 'DO NOT OPEN', 'warnlbl', 'font-size="6"') + TC(960, 261, 'NO USER-SERVICEABLE PARTS', 'warnlbl', 'font-size="4.8"');
    s += `<circle class="ob-psu-led" cx="1168" cy="30" r="4.2"/>`;
    s += `</g>`;
    return s;
  }
  drives() {
    const c = this.cfg;
    let s = `<g class="ob-cage"><rect x="894" y="298" width="290" height="504" rx="3" fill="none" stroke="#8c9195" stroke-width="3"/>`;
    s += `<path d="M900 508H1178" stroke="#8c9195" stroke-width="2.6"/>`;
    // 3.5" floppy + hard disk (behind the 5.25" bay)
    s += `<rect x="928" y="312" width="226" height="88" rx="3" fill="url(#obSteelDark)" filter="url(#obCardShadow)"/><rect x="936" y="320" width="210" height="72" rx="2" fill="url(#obSteel)"/>` + paper(1000, 336, 96, 36, ['3½" FLOPPY DRIVE', '1.44 MB · DRIVE A:'], 6);
    // two hard disks in the cage, on the primary IDE cable: C: (master) and D: (slave, the user's own)
    for (const [y, who] of [[410, 'C: · PRIMARY MASTER'], [456, 'D: · PRIMARY SLAVE']])
      s += `<rect x="928" y="${y}" width="226" height="42" rx="3" fill="#8f9398" filter="url(#obCardShadow)"/><rect x="934" y="${y + 4}" width="214" height="34" rx="2" fill="url(#obAlu)"/>` +
        paper(958, y + 7, 150, 28, ['ARM-PC FIXED DISK · 128 MB', who, 'LBA · ATA · 3600 RPM'], 5.6);
    s += `<g class="ob-bay525" data-part="cd-bay"><rect class="ob-drop" x="896" y="512" width="286" height="288" fill="transparent"/>`;
    if (c.cdrom) {
      s += `<g class="ob-cd" data-part="cdrom" tabindex="0" role="button" aria-label="The CD-ROM drive: click to take it out">
        <rect x="904" y="520" width="274" height="276" rx="3" fill="url(#obSteelBlack)" filter="url(#obCardShadow)"/><rect x="912" y="528" width="258" height="260" rx="2" fill="url(#obDriveTop)"/>
        ${paper(944, 560, 196, 88, ['ARM-PC CD-ROM DRIVE', 'DOUBLE SPEED · 300 KB/s', 'ATAPI · SFF-8020 · MASTER', 'MODEL CDR-2X · 1993'], 7.4)}
        <rect x="944" y="662" width="120" height="34" rx="1" fill="#f3d23a"/>${TC(1004, 676, 'CLASS 1 LASER PRODUCT', 'warnlbl', 'font-size="6.4"')}${TC(1004, 688, 'LASER KLASSE 1', 'warnlbl', 'font-size="5.4"')}
        ${screw(914, 530)}${screw(1168, 530)}${screw(914, 786)}${screw(1168, 786)}</g>`;
    } else {
      s += `<path d="M904 520V796M1178 520V796" stroke="#7c8185" stroke-width="5"/>` + TC(1041, 650, '5¼" BAY', 'stamp', 'font-size="16"') + TC(1041, 670, 'EMPTY', 'stamp', 'font-size="10"');
    }
    s += `</g></g>`;
    return s;
  }

  renderBin() {
    const c = this.cfg, list = this.binList;
    list.textContent = '';
    const items = [];
    const all = ['vga', 'hercules', 'sb16', 'adlib', 'modem'];
    for (const id of all) if (!this.installed(id)) items.push(['card', id]);
    if (!this.midi) items.push(['midi']);
    if (!c.cdrom) items.push(['cdrom']);
    if (!c.mouse) items.push(['mouse']);
    for (const [kind, id] of items) list.append(this.binItem(kind, id));
    if (!items.length) list.append(el('p', { class: 'ob-bin-empty', text: 'Empty. Everything is in the machine.' }));
  }
  binItem(kind, id) {
    let vb, inner, label;
    if (kind === 'card') {
      const L = CARDS[id].len;
      vb = `-6 -4 ${W + 12} ${L + 10}`; inner = cardSvg(id, { speed: this.speed, midi: this.midi && id === 'sb16' }); label = CARDS[id].name;
    } else if (kind === 'midi') { vb = '6 192 82 108'; inner = daughter(); label = 'GS wavetable daughterboard (MPU-401 + General MIDI synth)'; }
    else if (kind === 'cdrom') { vb = '898 514 286 288'; inner = this.drivesLoose(); label = 'CD-ROM drive'; }
    else { vb = '0 0 60 90'; inner = MOUSE; label = 'PS/2 mouse'; }
    const b = el('button', { class: `ob-part ob-${kind}${id ? ' ob-' + id : ''}`, type: 'button', 'data-kind': kind, 'data-id': id || '', title: `${label}: click to fit it, or drag it into the machine` });
    b.innerHTML = `<svg viewBox="${vb}" aria-hidden="true">${inner}</svg><span>${esc(kind === 'card' ? CARDS[id].name.replace(/-compatible /, ' ').replace(/^serial.*/, 'I/O') : label.split(' (')[0])}</span>`;
    b.addEventListener('pointerdown', (e) => this.dragStart(e, kind, id, b));
    b.addEventListener('click', (e) => { if (this.dragMoved) { e.preventDefault(); return; } this.act(kind, id, kind === 'card' ? undefined : 'in'); });
    return b;
  }
  drivesLoose() {
    return `<rect x="904" y="520" width="274" height="276" rx="3" fill="url(#obSteelBlack)"/><rect x="912" y="528" width="258" height="260" rx="2" fill="url(#obDriveTop)"/>${paper(944, 560, 196, 88, ['ARM-PC CD-ROM DRIVE', 'DOUBLE SPEED · 300 KB/s', 'ATAPI · SFF-8020'], 9)}`;
  }

  bindBoard(svg) {
    const act = (kind, id) => (e) => { if (this.dragMoved) return; e.stopPropagation(); this.act(kind, id); };
    for (const g of svg.querySelectorAll('.ob-card')) {
      const id = g.dataset.card;
      g.addEventListener('pointerdown', (e) => { if (e.target.closest('[data-part]')) return; this.dragStart(e, 'card', id, g); });
      g.addEventListener('click', (e) => { if (e.target.closest('[data-part]')) return; act('card', id)(e); });
      g.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); this.act('card', id); } });
    }
    for (const [sel, kind] of [['.ob-daughter', 'midi'], ['.ob-cd', 'cdrom'], ['.ob-mouse', 'mouse'], ['.ob-dip', 'speed'], ['.ob-simms', 'ram'], ['.ob-gamejp', 'game']]) {
      for (const g of svg.querySelectorAll(sel)) {
        if (kind === 'midi' || kind === 'cdrom' || kind === 'mouse') g.addEventListener('pointerdown', (e) => { e.stopPropagation(); this.dragStart(e, kind, null, g); });
        g.addEventListener('click', act(kind));
        g.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); this.act(kind); } });
      }
    }
    for (const g of svg.querySelectorAll('.ob-jpos')) g.addEventListener('click', (e) => { e.stopPropagation(); this.act('mhz', null, +g.dataset.mhz); });
  }

  // ------------------------------------------------------------ drag and drop (pointer events: mouse, pen and touch)
  dragStart(e, kind, id, srcEl) {
    if (e.button > 0) return;
    this.dragMoved = false;
    const x0 = e.clientX, y0 = e.clientY;
    let ghost = null;
    const move = (ev) => {
      const dx = ev.clientX - x0, dy = ev.clientY - y0;
      if (!ghost && Math.hypot(dx, dy) < 6) return;
      if (!ghost) {
        if (this.powered) { this.refuse(); up(); return; }
        this.dragMoved = true;
        ghost = this.makeGhost(kind, id);
        document.body.append(ghost);
        this.root.classList.add('dragging', `drag-${kind}`);
        if (kind === 'card') this.root.dataset.dragBits = CARDS[id].bits;
        srcEl.classList.add('ob-src');
      }
      ev.preventDefault();
      ghost.style.transform = `translate(${ev.clientX}px, ${ev.clientY}px) rotate(-4deg)`;
      this.hover(this.dropAt(ev.clientX, ev.clientY, kind, id, ghost), kind);
    };
    const up = (ev) => {
      removeEventListener('pointermove', move); removeEventListener('pointerup', up); removeEventListener('pointercancel', up);
      if (!ghost) return;
      const t = ev && ev.type === 'pointerup' ? this.dropAt(ev.clientX, ev.clientY, kind, id, ghost) : null;
      ghost.remove();
      srcEl.classList.remove('ob-src');
      this.root.classList.remove('dragging', `drag-${kind}`);
      this.hover(null);
      if (t !== null && t !== undefined) this.act(kind, id, t);
      setTimeout(() => { this.dragMoved = false; }, 0);
    };
    addEventListener('pointermove', move, { passive: false }); addEventListener('pointerup', up); addEventListener('pointercancel', up);
  }
  makeGhost(kind, id) {
    const g = el('div', { class: 'ob-ghost', 'aria-hidden': 'true' });
    let vb, inner, w;
    if (kind === 'card') { vb = `-6 -4 ${W + 12} ${CARDS[id].len + 10}`; inner = cardSvg(id, { speed: this.speed, midi: this.midi }); w = 76; }
    else if (kind === 'midi') { vb = '6 192 82 108'; inner = daughter(); w = 70; }
    else if (kind === 'cdrom') { vb = '898 514 286 288'; inner = this.drivesLoose(); w = 150; }
    else { vb = '0 0 60 90'; inner = MOUSE; w = 60; }
    g.innerHTML = `<svg viewBox="${vb}" style="width:${w}px">${inner}</svg>`;
    return g;
  }
  /** What is under the pointer: a slot number, 'bin', 'in' (the right place for a drive/mouse/daughterboard) or null. */
  dropAt(x, y, kind, id, ghost) {
    ghost.style.visibility = 'hidden';
    const t = document.elementFromPoint(x, y);
    ghost.style.visibility = '';
    if (!t) return null;
    if (t.closest('.ob-bin')) return 'bin';
    if (kind === 'card') { const s = t.closest('.ob-slot, .ob-card'); if (!s) return null; const i = +(s.dataset.slot ?? s.dataset.slotOf); return Number.isFinite(i) ? i : null; }
    if (kind === 'midi') return t.closest('.ob-card[data-card="sb16"]') ? 'in' : null;
    if (kind === 'cdrom') return t.closest('.ob-bay525') ? 'in' : null;
    if (kind === 'mouse') return t.closest('.ob-mouseport, .ob-ports') ? 'in' : null;
    return null;
  }
  hover(t, kind) {
    for (const e of this.root.querySelectorAll('.ob-hot')) e.classList.remove('ob-hot');
    if (t === null || t === undefined) return;
    if (t === 'bin') { this.bin.classList.add('ob-hot'); return; }
    const sel = typeof t === 'number' ? `.ob-slot[data-slot="${t}"]` : { midi: '.ob-card[data-card="sb16"]', cdrom: '.ob-bay525', mouse: '.ob-mouseport' }[kind];
    if (sel) this.board.querySelector(sel)?.classList.add('ob-hot');
  }
}

const MOUSE = `<path d="M30 4C14 4 8 16 8 34v26c0 18 10 26 22 26s22-8 22-26V34C52 16 46 4 30 4z" fill="url(#obIvory)" stroke="rgba(0,0,0,.35)" stroke-width="1"/>
  <path d="M8 36H52M30 4V36" stroke="rgba(0,0,0,.28)" stroke-width="1.2"/><path d="M30 4C30 -6 36 -8 40 -14" stroke="#d6d2c6" stroke-width="3" fill="none"/>
  <text x="30" y="70" class="paper" text-anchor="middle" font-size="5.4">EUROPA</text>`;

const DEFS = `<defs>
  <linearGradient id="obSteel" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#c9cdd1"/><stop offset="1" stop-color="#a9aeb3"/></linearGradient>
  <linearGradient id="obSteelDark" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#8f949a"/><stop offset="1" stop-color="#6d7277"/></linearGradient>
  <linearGradient id="obSteelBlack" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3a3c40"/><stop offset="1" stop-color="#232427"/></linearGradient>
  <linearGradient id="obDriveTop" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#4a4d52"/><stop offset=".5" stop-color="#34373b"/><stop offset="1" stop-color="#2a2c30"/></linearGradient>
  <linearGradient id="obAlu" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#dfe2e5"/><stop offset=".45" stop-color="#b8bcc1"/><stop offset=".55" stop-color="#c9cdd1"/><stop offset="1" stop-color="#a6abb0"/></linearGradient>
  <linearGradient id="obBeige" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#e8e1cf"/><stop offset="1" stop-color="#c9bfa7"/></linearGradient>
  <linearGradient id="obIvory" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#f1ece0"/><stop offset="1" stop-color="#d2cab6"/></linearGradient>
  <linearGradient id="obPcb" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#23704a"/><stop offset=".6" stop-color="#1b5e3d"/><stop offset="1" stop-color="#154f33"/></linearGradient>
  <linearGradient id="obPcbDeep" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#1d5a3c"/><stop offset="1" stop-color="#113a27"/></linearGradient>
  <linearGradient id="obPcbOlive" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#4c6b2d"/><stop offset="1" stop-color="#34501e"/></linearGradient>
  <linearGradient id="obPcbTeal" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#1f6560"/><stop offset="1" stop-color="#15474a"/></linearGradient>
  <linearGradient id="obPcbBlue" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#2a5d86"/><stop offset="1" stop-color="#1b3f60"/></linearGradient>
  <linearGradient id="obSheen" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".09"/><stop offset=".4" stop-color="#fff" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity=".12"/></linearGradient>
  <linearGradient id="obGold" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#f6dc8a"/><stop offset=".5" stop-color="#d9b252"/><stop offset="1" stop-color="#a97f28"/></linearGradient>
  <linearGradient id="obChip" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#34353a"/><stop offset="1" stop-color="#17181a"/></linearGradient>
  <linearGradient id="obSlot" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#2c2c2f"/><stop offset=".5" stop-color="#0f0f10"/><stop offset="1" stop-color="#2c2c2f"/></linearGradient>
  <linearGradient id="obBracket" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#e3e6e9"/><stop offset=".5" stop-color="#b7bcc1"/><stop offset="1" stop-color="#9aa0a5"/></linearGradient>
  <linearGradient id="obCeramic" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ece7da"/><stop offset="1" stop-color="#c3bba7"/></linearGradient>
  <linearGradient id="obLid" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#f3dc92"/><stop offset=".45" stop-color="#d5b25a"/><stop offset=".55" stop-color="#e2c273"/><stop offset="1" stop-color="#b08a36"/></linearGradient>
  <linearGradient id="obBattery" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#4a8ee0"/><stop offset=".5" stop-color="#2463b8"/><stop offset="1" stop-color="#173f7c"/></linearGradient>
  <linearGradient id="obPaper" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fbf8f0"/><stop offset="1" stop-color="#e9e3d3"/></linearGradient>
  <linearGradient id="obSimm" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#1f7a4c"/><stop offset="1" stop-color="#145a37"/></linearGradient>
  <linearGradient id="obRibbon" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".5"/><stop offset=".5" stop-color="#000" stop-opacity=".08"/><stop offset="1" stop-color="#fff" stop-opacity=".3"/></linearGradient>
  <radialGradient id="obCap" cx=".35" cy=".3" r=".8"><stop offset="0" stop-color="#6b8fd0"/><stop offset=".7" stop-color="#27468a"/><stop offset="1" stop-color="#1a2f5e"/></radialGradient>
  <radialGradient id="obCan" cx=".35" cy=".3" r=".9"><stop offset="0" stop-color="#f4f5f6"/><stop offset=".6" stop-color="#b7bbbf"/><stop offset="1" stop-color="#8e9398"/></radialGradient>
  <radialGradient id="obScrew" cx=".35" cy=".3" r=".8"><stop offset="0" stop-color="#f2f3f4"/><stop offset="1" stop-color="#8a8f94"/></radialGradient>
  <pattern id="obTraces" width="64" height="64" patternUnits="userSpaceOnUse">
    <path d="M0 10H22L30 18H64M0 40H12L20 32H44L52 40H64M8 0V6M40 64V52L46 46H64M18 64V56" stroke="#8fd9a8" stroke-opacity=".13" stroke-width="1.1" fill="none"/>
    <circle cx="22" cy="10" r="1.4" fill="#cfe8c0" fill-opacity=".18"/><circle cx="44" cy="32" r="1.4" fill="#cfe8c0" fill-opacity=".18"/><circle cx="40" cy="52" r="1.4" fill="#cfe8c0" fill-opacity=".18"/>
  </pattern>
  <pattern id="obSpangle" width="120" height="120" patternUnits="userSpaceOnUse">
    <path d="M0 0L40 30L20 70L0 60ZM60 0L120 20L90 60L50 40ZM20 70L90 60L120 120L30 120Z" fill="#fff" fill-opacity=".05"/><path d="M40 30L90 60L50 40Z" fill="#000" fill-opacity=".04"/>
  </pattern>
  <filter id="obCardShadow" x="-10%" y="-10%" width="130%" height="130%"><feDropShadow dx="1.6" dy="2.4" stdDeviation="1.6" flood-color="#000" flood-opacity=".42"/></filter>
  <filter id="obLift" x="-10%" y="-10%" width="130%" height="130%"><feDropShadow dx="3" dy="4" stdDeviation="2.4" flood-color="#000" flood-opacity=".5"/></filter>
  <filter id="obBoardShadow" x="-5%" y="-5%" width="110%" height="110%"><feDropShadow dx="0" dy="3" stdDeviation="3" flood-color="#000" flood-opacity=".35"/></filter>
</defs>`;
