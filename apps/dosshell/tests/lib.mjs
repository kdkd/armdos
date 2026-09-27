// apps/dosshell/tests/lib.mjs - boot ARM-DOS with the Shell installed as
// SELECT would have left it (C:\DOS\DOSSHELL.BAT, AUTOEXEC.BAT ending with
// DOSSHELL), drive it with keys, and compare screens cell by cell (text and
// attributes) with the real MS-DOS 4.00 Shell's screens captured in DOSBox-X
// (tests/ref/*.txt).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage } from '../../../disk/mkimage.mjs';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const HERE = path.dirname(fileURLToPath(import.meta.url));
export const OUT = path.join(ROOT, 'build', 'dosshell-test');
const B = (p) => path.join(ROOT, 'build', p);

// the captures were made on 09-24-26 at 10:41 am
export const RTC = new Date(2026, 8, 24, 10, 41, 0).getTime();

const CP437 = (() => {
  const low = ' ☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼';
  const hi = 'ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ';
  const t = [];
  for (let i = 0; i < 256; i++) t.push(i < 32 ? low[i] : i < 127 ? String.fromCharCode(i) : i === 127 ? '⌂' : hi[i - 128]);
  return t;
})();

export function makeImage({ autoexec = true, extra = [], config = null, dirs = [], mouse = false } = {}) {
  fs.mkdirSync(OUT, { recursive: true });
  const put = (name, text) => {
    const p = path.join(OUT, name);
    fs.writeFileSync(p, Buffer.from(text.replace(/\r?\n/g, '\r\n'), 'latin1'));
    return path.relative(ROOT, p);
  };
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM' },
    { src: put('CONFIG.SYS', config || 'FILES=20\nBUFFERS=20\n'), dst: 'CONFIG.SYS' },
    { src: put('AUTOEXEC.BAT', '@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\n' + (mouse ? 'MOUSE\n' : '') + (autoexec ? 'DOSSHELL\n' : '')), dst: 'AUTOEXEC.BAT' },
    { src: 'build/SHELLB.COM', dst: 'DOS\\' },
    { src: 'build/SHELLC.EXE', dst: 'DOS\\' },
    { src: 'apps/dosshell/data/DOSSHELL.BAT', dst: 'DOS\\DOSSHELL.BAT' },
    ...['SHELL.MEU', 'DOSUTIL.MEU', 'GAMES.MEU', 'SHELL.HLP', 'SHELL.CLR'].map((f) => ({ src: `build/dosshell/${f}`, dst: `DOS\\${f}` })),
    ...['MOUSE.COM', 'CHKDSK.COM', 'MEM.EXE', 'MORE.COM', 'FIND.EXE', 'MODE.COM', 'TREE.COM', 'LABEL.COM', 'EDLIN.COM', 'PRINT.COM']
      .filter((f) => fs.existsSync(B(f))).map((f) => ({ src: `build/${f}`, dst: 'DOS\\' })),
    ...extra,
  ];
  const m = { format: 'hd', heads: 16, sectorsPerTrack: 63, sizeMB: 32, label: 'ARM-DOS', date: '1988-06-17 12:00:00',
    boot: { src: 'build/bootsect.bin' }, files, dirs: ['DOS', ...dirs] };
  return buildImage(m, ROOT).img;
}

export async function start(opts = {}) {
  const img = opts.img || makeImage(opts);
  const pc = await boot({ rom: B('rom.bin'), hd: new Uint8Array(img), rtcBaseMs: opts.rtc ?? RTC });
  return pc;
}

// the screen as {chars[25][80] (CP437 codes), attrs[25][80]}
export function grab(pc) {
  const m8 = pc.machine.cpu.m8, vga = pc.machine.vga;
  const chars = [], attrs = [];
  for (let r = 0; r < 25; r++) {
    const c = [], a = [];
    for (let x = 0; x < 80; x++) {
      const o = 0xB8000 + (((vga.startAddr + r * 80 + x) * 2) & 0x7FFF);
      c.push(m8[o]); a.push(m8[o + 1]);
    }
    chars.push(c); attrs.push(a);
  }
  return { chars, attrs };
}

const blank = (ch) => ch === 0 || ch === 0x20 || ch === 0xFF;

// the same text + attribute-run format as the reference captures
export function dump(s) {
  const L = [];
  L.push('    ' + [...Array(80)].map((_, i) => Math.floor(i / 10)).join(''));
  L.push('    ' + [...Array(80)].map((_, i) => i % 10).join(''));
  for (let r = 0; r < 25; r++) L.push(String(r).padStart(2, '0') + ' |' + s.chars[r].map((c) => CP437[c]).join('') + '|');
  L.push('attribute runs (row: startcol-endcol=BG/FG hex; FG "-" = blank cells)');
  for (let r = 0; r < 25; r++) {
    const key = (x) => { const a = s.attrs[r][x]; return `${((a >> 4) & 7).toString(16).toUpperCase()}/${blank(s.chars[r][x]) ? '-' : (a & 15).toString(16).toUpperCase()}`; };
    const runs = [];
    let st = 0;
    for (let x = 1; x <= 80; x++) if (x === 80 || key(x) !== key(st)) { runs.push(`${st}-${x - 1}=${key(st)}`); st = x; }
    L.push(String(r).padStart(2, '0') + ': ' + runs.join(' '));
  }
  return L.join('\n') + '\n';
}

// parse a reference capture into per-cell {ch (unicode), bg, fg|null}
export function readRef(name) {
  /* the captures: tests/ref */
  const file = path.join(HERE, name.startsWith('ref/') ? name : 'ref/' + name);
  const t = fs.readFileSync(file, 'utf8').split('\n');
  const rows = [];
  for (let r = 0; r < 25; r++) {
    const line = t[2 + r];
    const s = line.slice(4, line.lastIndexOf('|'));
    rows.push([...s].map((ch) => ({ ch })));
  }
  const ai = t.findIndex((l) => l.startsWith('attribute runs'));
  for (let r = 0; r < 25; r++) {
    const l = t[ai + 1 + r];
    for (const run of l.slice(4).trim().split(' ')) {
      const m = run.match(/^(\d+)-(\d+)=([0-9A-F])\/([0-9A-F-])$/);
      for (let x = +m[1]; x <= +m[2]; x++) { rows[r][x].bg = parseInt(m[3], 16); rows[r][x].fg = m[4] === '-' ? null : parseInt(m[4], 16); }
    }
  }
  return rows;
}

// compare the screen with a reference; mask(r, c) -> true skips a cell;
// patch: {row: text} replaces expected text (and keeps the attributes)
export function compare(s, refName, { mask = () => false, patch = {}, patchAttr = {}, fix = null, fixCell = null, cursor = -1 } = {}) {
  const ref = readRef(refName);
  if (fix)
    for (let r = 0; r < 25; r++) {
      const txt = ref[r].map((c) => c.ch).join(''), nt = [...fix(r, txt)];
      for (let x = 0; x < 80; x++) ref[r][x].ch = nt[x] ?? ' ';
    }
  for (const [r, txt] of Object.entries(patch)) {
    const chars = [...txt];
    for (let x = 0; x < 80; x++) ref[r][x].ch = chars[x] ?? ' ';
  }
  if (fixCell) for (let r = 0; r < 25; r++) for (let x = 0; x < 80; x++) fixCell(r, x, ref[r][x]);
  for (const [r, runs] of Object.entries(patchAttr))
    for (const [a, b, bg, fg] of runs) for (let x = a; x <= b; x++) { ref[r][x].bg = bg; ref[r][x].fg = fg; }
  const diffs = [];
  for (let r = 0; r < 25; r++) for (let x = 0; x < 80; x++) {
    if (mask(r, x)) continue;
    const e = ref[r][x], ch = s.chars[r][x], a = s.attrs[r][x];
    /* the captures caught the blinking cursor as '_': it must be ours too */
    if (e.ch === '_' && r * 80 + x === cursor && blank(ch)) continue;
    const gotCh = CP437[ch] === '\u00a0' ? ' ' : (ch === 0 ? ' ' : CP437[ch]);
    const expBlank = e.fg === null;
    let bad = false;
    if (!expBlank && gotCh !== e.ch) bad = true;
    if (expBlank && !blank(ch) && gotCh !== ' ') bad = true;
    if (((a >> 4) & 7) !== e.bg) bad = true;
    if (!expBlank && (a & 15) !== e.fg) bad = true;
    if (bad) diffs.push(`r${r}c${x}: want '${e.ch}' ${e.bg.toString(16)}/${e.fg === null ? '-' : e.fg.toString(16)} got '${gotCh}' ${((a >> 4) & 7).toString(16)}/${(a & 15).toString(16)}`);
  }
  return diffs;
}

export function save(pc, name) {
  fs.mkdirSync(OUT, { recursive: true });
  fs.writeFileSync(path.join(OUT, name + '.txt'), dump(grab(pc)));
  return pc.png(path.join(OUT, name + '.png'));
}

// keys, then let the machine settle
export function keys(pc, k, ms = 400) { if (process.env.KEYLOG) console.log('KEYS', k.slice(0, 70)); pc.type(k); pc.waitIdle({ quietMs: 300, timeoutMs: 20000 }); pc.run(ms); }

export class Checker {
  constructor(name) { this.name = name; this.n = 0; this.fail = 0; }
  ok(cond, msg, detail) {
    this.n++;
    if (cond) console.log(`ok   ${msg}`);
    else { this.fail++; console.log(`FAIL ${msg}${detail ? '\n     ' + String(detail).split('\n').slice(0, 12).join('\n     ') : ''}`); }
    return cond;
  }
  done() {
    console.log(`${this.name}: ${this.n - this.fail}/${this.n} passed`);
    process.exit(this.fail ? 1 : 0);
  }
}

// A 1.44 MB diskette laid out like the one the real Shell was captured with
// (same names, sizes, dates and order; the contents are filler, except
// README.TXT, which File View shows)
export function makeFloppy() {
  const spec = [
    ['SHELL.CLR', 4406], ['SHELL.HLP', 66527], ['SHELL.MEU', 4588], ['SHELLB.COM', 3894],
    ['SHELLC.EXE', 153855], ['DOSUTIL.MEU', 6660], ['PCIBMDRV.MOS', 263], ['MEM.EXE', 20005],
    ['CHKDSK.COM', 17787], ['TREE.COM', 6302], ['FIND.EXE', 5941], ['MORE.COM', 2134], ['ATTRIB.EXE', 18263],
    ['FC.EXE', 15807], ['LABEL.COM', 4458], ['SUBST.EXE', 18467], ['MODE.COM', 22960], ['DOSSHELL.BAT', 184],
    ['CHARS.COM', 23, '10:39'],
  ];
  fs.mkdirSync(path.join(OUT, 'fd'), { recursive: true });
  const files = [];
  const add = (name, size, time, dst, content) => {
    const p = path.join(OUT, 'fd', (dst || name).replace(/\\/g, '_'));
    fs.writeFileSync(p, content || Buffer.alloc(size, 0x1A));
    files.push({ src: path.relative(ROOT, p), dst: dst || name, date: `2026-09-24 ${time || '10:37'}:00` });
  };
  for (const [n, s, tm] of spec) add(n, s, tm);
  const readme = Buffer.from('Hello from the README file.\r\nSecond line.\r\n', 'latin1');
  add('README.TXT', 43, '10:45', 'README.TXT', readme);
  add('TREE.COM', 6302, '10:45', 'DOS\\TREE.COM', fs.existsSync(B('TREE.COM')) ? fs.readFileSync(B('TREE.COM')) : null);
  add('SORT.EXE', 5882, '10:45', 'DOS\\SORT.EXE');
  add('README.TXT', 43, '10:45', 'WORK\\README.TXT', readme);
  const m = { format: 'fd1440', date: '2026-09-24 10:45:00', files, dirs: ['DOS', 'GAMES', 'GAMES\\ADVENT', 'WORK'] };
  return buildImage(m, ROOT).img;
}
