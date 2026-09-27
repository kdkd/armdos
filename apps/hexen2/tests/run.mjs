#!/usr/bin/env node
// apps/hexen2/tests/run.mjs - boot ARM-DOS headless and test Hexen II (H2.EXE).
//
//   node apps/hexen2/tests/run.mjs [nodata] [play] [timedemo] [files] [--pak FILE] [--out DIR] [--mhz N]
//
// ARM-DOS ships H2.EXE WITHOUT the game data (Activision's demo licence), so:
//
//   nodata    (always) C:\GAMES\HEXEN2 as apps/hexen2/hd.json has it: HEXEN2
//             and INSTALL explain how to get PAK0.PAK; H2CHECK rejects a
//             wrong file; H2 without PAK0.PAK stops with a readable error in
//             text mode; without HIMEM.SYS the extender stub says so.
//
// The rest needs the Hexen II demo's pak0.pak (v1.11, 27,750,257 bytes) for
// LOCAL testing - --pak FILE or $HEXEN2_PAK. Without it
// they are skipped (not failed). The pak only ever goes into disk images
// held in memory here; nothing with it is written anywhere.
//
//   play      INSTALL (size, directory, CRC-32), HEXEN2 from C:\, the main
//             menu, Single Player -> New Game -> class -> difficulty, the
//             demo's first map, walk, turn, attack (sound), jump, mouse, the
//             other demo maps from the console, quick save and load, quit ->
//             text mode, vectors restored, CONFIG.CFG written.
//   timedemo  records a demo while walking through Blackmarsh, plays it back
//             with "timedemo": fps at 100 MHz (unless --mhz).
//   files     the web page's Files panel way in: web/js/fat.js (the panel's
//             FAT writer) copies pak0.pak into C:\GAMES\HEXEN2\DATA1 of the
//             factory image, then INSTALL and a game start.
//
// Screenshots go to build/hexen2-test/.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const B = (p) => path.join(ROOT, 'build', p);

const argv = process.argv.slice(2);
const opt = (name, def) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : def; };
const OUT = path.resolve(opt('--out', B('hexen2-test')));
const MHZ = +opt('--mhz', 0) || undefined;
const PAK = opt('--pak', process.env.HEXEN2_PAK || '');
const which = argv.filter((a, i) => !a.startsWith('--') && !(i > 0 && argv[i - 1].startsWith('--') && argv[i - 1] !== '--no-jit'));
fs.mkdirSync(OUT, { recursive: true });

const PAK_SIZE = 27750257;
const havePak = (() => { try { return fs.statSync(PAK).size === PAK_SIZE; } catch { return false; } })();

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; return ok; };

// ------------------------------------------------------------ the disk
const frag = JSON.parse(fs.readFileSync(path.join(ROOT, 'apps/hexen2/hd.json'), 'utf8'));

/** A C: like the release one for Hexen II (in memory). pak: host file for DATA1\PAK0.PAK. */
function makeImage({ autoexec = 'MOUSE', himem = true, pak = null, extra = [] } = {}) {
  const tmp = fs.mkdtempSync(path.join(OUT, 'img-'));
  try {
    const files = [
      { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
      { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
      { src: 'build/COMMAND.COM' },
      { src: 'build/HIMEM.SYS', dst: 'DOS\\' },
      { src: 'build/MOUSE.COM', dst: 'DOS\\' },
      { src: 'build/MORE.COM', dst: 'DOS\\', optional: true },
      ...frag.files,
      ...extra,
    ];
    if (pak) files.push({ src: pak, dst: 'GAMES\\HEXEN2\\DATA1\\PAK0.PAK' });
    const put = (dst, text) => {
      const host = path.join(tmp, dst.replace(/[\\/]/g, '_'));
      fs.writeFileSync(host, text.replace(/\r?\n/g, '\r\n'));
      files.push({ src: host, dst });
    };
    put('CONFIG.SYS', (himem ? 'DEVICE=C:\\DOS\\HIMEM.SYS\n' : '') + 'FILES=20\n');
    put('AUTOEXEC.BAT', `@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\nSET BLASTER=A220 I7 D1 H5 T6\n${autoexec}\n`);
    const { img } = buildImage({
      format: 'hd', sizeMB: 64, heads: 16, sectorsPerTrack: 63, label: 'HEXEN2TEST',
      date: '1997-11-07 12:00:00', boot: { src: 'build/bootsect.bin' }, files,
      dirs: [...frag.dirs, 'DOS'],
    }, ROOT);
    return img;
  } finally { fs.rmSync(tmp, { recursive: true, force: true }); }
}

const start = (img) => boot({ rom: B('rom.bin'), hd: img, mhz: MHZ, jit: !argv.includes('--no-jit') });

const mode = (pc) => pc.machine.vga.mode;
const ivt = (pc, n) => pc.cpu.m32[n];
const tap = (pc, code, after = 400) => { pc.machine.keyDown(code); pc.run(80); pc.machine.keyUp(code); pc.run(after); };
const hold = (pc, code, ms) => { pc.machine.keyDown(code); pc.run(ms); pc.machine.keyUp(code); pc.run(60); };
function typeKeys(pc, text, after = 300) {
  for (const ch of text) {
    if (ch === '_') { pc.machine.keyDown('ShiftLeft'); tap(pc, 'Minus', 30); pc.machine.keyUp('ShiftLeft'); continue; }
    const code = ch === ' ' ? 'Space' : ch === '\r' ? 'Enter' : /[0-9]/.test(ch) ? 'Digit' + ch : 'Key' + ch.toUpperCase();
    tap(pc, code, 30);
  }
  pc.run(after);
}
const consoleCmd = (pc, text, after = 800) => typeKeys(pc, text + '\r', after);
// open the console (if it is not open already: the echo "]cmd" shows it took) and run cmd
function inConsole(pc, text, after = 800) {
  for (let i = 0; i < 3; i++) {
    const mark = pc.debug.length;
    tap(pc, 'Backquote', 600);
    consoleCmd(pc, text, after);
    if (pc.debug.slice(mark).includes(']' + text)) return true;
  }
  return false;
}
const shot = async (pc, file) => { await pc.png(path.join(OUT, file)); console.log(`     screenshot ${path.join(OUT, file)}`); };
const colours = (pc) => { const img = pc.render(); const s = new Set(); for (let i = 0; i < img.data.length; i += 4 * 7) s.add(img.data[i] << 16 | img.data[i + 1] << 8 | img.data[i + 2]); return s.size; };
const frameHash = (pc) => { const img = pc.render(); let h = 0; for (let i = 0; i < img.data.length; i += 3) h = (h * 31 + img.data[i]) | 0; return h; };
const busy = (pc, ms) => { const h0 = pc.machine.haltedNs, e0 = pc.machine.timeNs(); pc.run(ms); return 1 - (pc.machine.haltedNs - h0) / (pc.machine.timeNs() - e0); };
const screenLog = (pc) => pc.screen().split('\n').filter((l) => l.trim()).map((l) => '     | ' + l).join('\n');
const since = (pc, mark) => pc.debug.slice(mark);
// the level's name, which the server prints after its version banner
// (after the version banner and a \x1d\x1e..\x1f rule; \x02 = coloured text)
const levelName = (log) => ([...log.matchAll(/SERVER \(\d+ CRC\)\n+\x1d\x1e+\x1f\n+\x02?([^\n]+)\n/g)].pop() || [])[1];

function readHostFile(pc, dosPath) {
  try {
    const img = pc.machine.ata.img;
    const fr = new FatReader(Buffer.from(img.buffer, img.byteOffset, img.length));
    const ent = fr.lookup(dosPath);
    return ent ? Buffer.from(fr.readFile(ent)) : null;
  } catch { return null; }
}

// Sound Blaster activity (DSP commands) and the audio it produces.
function soundProbe(pc) {
  const p = { dsp: 0, windows: [] };
  const sb = pc.machine.sb;
  if (!sb) return p;
  const sw = sb.write.bind(sb);
  sb.write = (port, v) => { if ((port & 0xF) === 0xC) p.dsp++; return sw(port, v); };
  let acc = 0, n = 0;
  pc.machine.audio.start(22050, (l, r) => {
    for (let i = 0; i < l.length; i++) {
      acc += l[i] * l[i] + r[i] * r[i]; n += 2;
      if (n >= 22050 / 5) { p.windows.push([pc.timeMs, Math.sqrt(acc / n)]); acc = 0; n = 0; }
    }
  });
  p.loudSince = (t) => Math.max(0, ...p.windows.filter(([tm]) => tm >= t).map(([, v]) => v));
  return p;
}

// type at the DOS prompt, answering MORE's "-- More --"
function dosType(pc, text, until, timeoutMs = 30000) {
  pc.type(text);
  return pc.until(() => {
    if (pc.hasText('-- More --')) { pc.type(' '); pc.run(200); }
    return until();
  }, { timeoutMs });
}

// ---------------------------------------------------------------- nodata
async function nodata() {
  console.log('== nodata (C:\\GAMES\\HEXEN2 as shipped, no PAK0.PAK)');
  let pc = await start(makeImage());
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('CLS\r'); pc.run(300);
  dosType(pc, 'HEXEN2\r', () => pc.hasText('HEXEN2 to play') || pc.hasText('whole story'));
  pc.run(500);
  await shot(pc, 'nodata-hexen2.png');
  console.log(screenLog(pc));
  check(pc.hasText('PAK0.PAK is not there yet') && pc.hasText('h2demo.exe') && pc.hasText('Install\\Hexen2\\data1\\pak0.pak'),
    'HEXEN2 without the data: INSTALL explains where PAK0.PAK comes from');
  check(pc.hasText('Copy files in') && pc.hasText('GETPAK'), 'the two ways in (Files panel, Host Link) are named');
  check(pc.until(() => pc.hasText('C:\\>'), { timeoutMs: 5000 }), 'back at the prompt');

  // H2.EXE itself without the data
  pc.type('CLS\rCD \\GAMES\\HEXEN2\rH2\r');
  check(pc.until(() => pc.hasText('FATAL ERROR') && pc.hasText('C:\\GAMES\\HEXEN2>'), { timeoutMs: 30000 }), 'H2 without PAK0.PAK stops with an error');
  console.log(screenLog(pc));
  check(mode(pc) === 3 && pc.hasText('Unable to find a proper Hexen II installation'), 'the error is readable in text mode');
  await shot(pc, 'nodata-h2.png');

  // H2CHECK on a wrong file
  pc.type('CLS\rECHO NOT A PAK>DATA1\\PAK0.PAK\rINSTALL\r');
  check(pc.until(() => pc.hasText('This is not a pak file') && pc.hasText('again'), { timeoutMs: 20000 }), 'INSTALL/H2CHECK reject a wrong PAK0.PAK');
  console.log(screenLog(pc));
  pc.type('DEL DATA1\\PAK0.PAK\r'); pc.run(500);

  // no HIMEM.SYS: the extender stub says what it needs
  pc = await start(makeImage({ himem: false, autoexec: '' }));
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt (no HIMEM.SYS)');
  pc.type('GAMES\\HEXEN2\\H2\r');
  check(pc.until(() => pc.hasText('HIMEM.SYS') && pc.lines().slice(-3).join('').includes('C:\\>'), { timeoutMs: 20000 }) ||
        pc.until(() => pc.hasText('extended memory manager'), { timeoutMs: 1000 }), 'without HIMEM.SYS: "requires an extended memory manager (HIMEM.SYS)"');
  console.log(screenLog(pc));
}

// ------------------------------------------------------------------ play
async function play() {
  console.log('== play (the Hexen II demo, local pak0.pak)');
  const pc = await start(makeImage({ pak: PAK }));
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('CLS\rGAMES\\HEXEN2\\INSTALL\r');
  check(pc.until(() => pc.hasText('Type HEXEN2 to play') || pc.hasText('again'), { timeoutMs: 60000 }), 'INSTALL finished');
  console.log(screenLog(pc));
  check(pc.hasText('BB755DBC') && pc.hasText('797 files, CRC 22780') && pc.hasText('the Hexen II demo, version 1.11'),
    'INSTALL: size, directory and CRC-32 of the demo pak');
  await shot(pc, 'install.png');

  pc.type('CLS\r'); pc.run(300);
  const snd = soundProbe(pc);
  const old08 = ivt(pc, 8), old09 = ivt(pc, 9);
  const t0 = performance.now(), e0 = pc.timeMs;
  pc.type('HEXEN2\r');
  check(pc.waitText('Hexen II for ARM-DOS', { timeoutMs: 10000 }), 'banner "Hexen II for ARM-DOS"');
  check(pc.until(() => mode(pc) === 0x13, { timeoutMs: 30000 }), 'mode 13h set');
  check(ivt(pc, 8) !== old08 && ivt(pc, 9) !== old09, 'INT 08h and 09h hooked');
  check(pc.until(() => pc.debug.includes('Hexen II Initialized'), { timeoutMs: 30000 }), 'Hexen II initialized');
  check(pc.debug.includes('Playing the demo version'), '"Playing the demo version." (pak detected by the engine)');
  const alloc = (pc.debug.match(/([\d.]+) megabyte heap/) || [])[1];
  console.log(`     hunk: ${alloc} MB; start-up took ${(pc.timeMs - e0).toFixed(0)} ms emulated`);
  pc.run(2500);
  await shot(pc, 'menu.png');
  check(colours(pc) > 40, `the main menu is on the screen (${colours(pc)} colours)`);
  check(snd.dsp > 0, `Sound Blaster programmed (${snd.dsp} DSP writes)`);

  // Single Player -> New Game -> class -> difficulty
  tap(pc, 'Enter', 800);
  await shot(pc, 'menu-sp.png');
  tap(pc, 'Enter', 800);
  await shot(pc, 'menu-class.png');
  tap(pc, 'ArrowDown', 300);                     // the Crusader
  tap(pc, 'Enter', 800);
  await shot(pc, 'menu-skill.png');
  let mark = pc.debug.length;
  tap(pc, 'Enter', 200);
  check(pc.until(() => /maps\/demo1\.bsp|Blackmarsh/.test(since(pc, mark)), { timeoutMs: 60000 }), 'New Game loads the first demo map');
  pc.run(6000);
  const level1 = levelName(since(pc, mark));
  console.log(since(pc, mark).split('\n').filter((l) => l.trim()).map((l) => '     | ' + l).join('\n'));
  await shot(pc, 'demo1.png');
  check(colours(pc) > 60, `the level is on the screen (${colours(pc)} colours)`);
  check(snd.loudSince(e0) > 0.001, `sound playing (RMS ${snd.loudSince(e0).toFixed(4)})`);

  const h0 = frameHash(pc);
  hold(pc, 'ArrowUp', 1500);
  await shot(pc, 'demo1-walk.png');
  check(frameHash(pc) !== h0, 'walking changes the view');
  hold(pc, 'ArrowLeft', 600);
  const tf = pc.timeMs;
  tap(pc, 'ControlLeft', 150);
  await shot(pc, 'demo1-attack.png');
  pc.run(800);
  check(snd.loudSince(tf) > 0.001, `attack audible (RMS ${snd.loudSince(tf).toFixed(4)})`);
  tap(pc, 'Slash', 800);                         // jump
  const hm = frameHash(pc);
  for (let i = 0; i < 10; i++) { pc.machine.mouseMove(40, 0); pc.run(50); }
  pc.run(300);
  await shot(pc, 'demo1-mouse.png');
  check(frameHash(pc) !== hm && pc.debug.includes('mouse available'), 'the mouse turns the view');
  const b = busy(pc, 2000);
  console.log(`     CPU busy ${(b * 100).toFixed(0)}% while standing in the level`);

  // quick save (F6) and quick load (F9)
  mark = pc.debug.length;
  tap(pc, 'F6', 3000);
  const saved = readHostFile(pc, 'GAMES\\HEXEN2\\DATA1\\QUICK\\INFO.DAT');
  check(!!saved, `F6 quick save writes DATA1\\QUICK (${saved ? saved.length : 0} bytes of INFO.DAT)`);
  hold(pc, 'ArrowUp', 1000);
  mark = pc.debug.length;
  tap(pc, 'F9', 200);
  check(pc.until(() => /Loading game|demo1/.test(since(pc, mark)), { timeoutMs: 30000 }), 'F9 quick load');
  pc.run(3000);
  await shot(pc, 'demo1-loaded.png');

  // the other maps of the demo, from the console
  const names = { demo1: level1 };
  for (const m of ['demo2', 'demo3', 'ravdm1']) {
    mark = pc.debug.length;
    inConsole(pc, 'map ' + m, 200);
    const after = () => { const t = since(pc, mark); const i = t.indexOf(']map ' + m); return i < 0 ? '' : t.slice(i); };
    const ok = pc.until(() => after().includes('FATAL') || !!levelName(after()), { timeoutMs: 60000 });
    pc.run(5000);
    names[m] = levelName(after());
    check(ok && !since(pc, mark).includes('FATAL') && mode(pc) === 0x13, `console "map ${m}": "${names[m]}"`);
    await shot(pc, `${m}.png`);
    if (m === 'demo3') {
      // The Mill: the archers by the entrance come for you - fight back
      tap(pc, 'Escape', 300); tap(pc, 'Escape', 300);   // (console down after the load)
      const mf = pc.debug.length, hf = frameHash(pc);
      for (let i = 0; i < 40 && !/Archer|killed|was /.test(since(pc, mf)); i++) {
        pc.machine.keyDown('ControlLeft'); hold(pc, i & 1 ? 'ArrowLeft' : 'ArrowRight', 500); pc.run(700);
        pc.machine.keyUp('ControlLeft'); pc.run(200);
        if (i === 3) await shot(pc, 'demo3-fight.png');
      }
      const fight = since(pc, mf);
      console.log('     ' + (fight.split('\n').filter((l) => /Archer|killed|was /.test(l)).join(' / ') || 'no obituary'));
      check(/Archer|killed|was /.test(fight) && frameHash(pc) !== hf, 'a fight in The Mill (the archers attack, obituary)');
      await shot(pc, 'demo3-fight2.png');
    }
  }
  mark = pc.debug.length;
  inConsole(pc, 'timerefresh', 200);
  pc.until(() => /seconds .*fps/.test(since(pc, mark)), { timeoutMs: 120000 });
  console.log(`     timerefresh in the Atrium of Immolation: ${(since(pc, mark).match(/[\d.]+ seconds[^\n]*fps\)/) || ['?'])[0]}`);
  tap(pc, 'Backquote', 600);
  mark = pc.debug.length;
  inConsole(pc, 'sys_stack', 600);
  consoleCmd(pc, 'version', 600);
  await shot(pc, 'console.png');
  console.log('     ' + (since(pc, mark).match(/zone: [^\n]*\nstack: [^\n]*/) || [JSON.stringify(since(pc, mark).slice(0, 300))])[0]);
  tap(pc, 'Backquote', 600);

  // quit: Esc -> Quit -> Y
  tap(pc, 'Escape', 800);
  for (let i = 0; i < 4; i++) tap(pc, 'ArrowDown', 200);
  tap(pc, 'Enter', 1000);
  await shot(pc, 'quit-prompt.png');
  tap(pc, 'KeyY', 300);
  check(pc.until(() => mode(pc) === 3 && pc.hasText('C:\\>'), { timeoutMs: 30000 }), 'back at the DOS prompt in text mode');
  await shot(pc, 'quit.png');
  console.log(screenLog(pc));
  check(ivt(pc, 8) === old08 && ivt(pc, 9) === old09, 'INT 08h and 09h restored');
  const cfg = readHostFile(pc, 'GAMES\\HEXEN2\\DATA1\\CONFIG.CFG');
  check(cfg && /bind "?CTRL"? "?\+attack/i.test(cfg.toString()), `DATA1\\CONFIG.CFG written on quit (${cfg ? cfg.length : 0} bytes)`);
  console.log(`     level names: ${JSON.stringify(names)}`);
  console.log(`     ${pc.timeMs.toFixed(0)} ms emulated in ${(performance.now() - t0).toFixed(0)} ms host`);
  if (pc.faults.length) console.log('     faults:', pc.faults.slice(0, 5));
}

// -------------------------------------------------------------- timedemo
async function timedemo() {
  console.log('== timedemo (record a walk through Blackmarsh, play it back)');
  const pc = await start(makeImage({ pak: PAK, autoexec: '' }));
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('CD \\GAMES\\HEXEN2\rH2 -nosound +record bench demo1\r');
  check(pc.until(() => /Blackmarsh|demo1\.bsp/.test(pc.debug) && pc.debug.includes('recording'), { timeoutMs: 60000 }) ||
        pc.until(() => /Blackmarsh/.test(pc.debug), { timeoutMs: 1000 }), 'recording on demo1');
  pc.run(4000);
  // a walk: forward, turns, a look around
  for (const [k, ms] of [['ArrowUp', 2500], ['ArrowLeft', 700], ['ArrowUp', 2000], ['ArrowRight', 1400], ['ArrowUp', 2500],
    ['ArrowLeft', 1500], ['ArrowUp', 1500], ['ArrowDown', 1000], ['ArrowRight', 2000]]) hold(pc, k, ms);
  tap(pc, 'Backquote', 600);
  consoleCmd(pc, 'stop', 1500);
  const dem = readHostFile(pc, 'GAMES\\HEXEN2\\DATA1\\BENCH.DEM');
  check(dem && dem.length > 1000, `BENCH.DEM recorded (${dem ? dem.length : 0} bytes)`);
  const mark = pc.debug.length;
  consoleCmd(pc, 'timedemo bench', 200);
  const t0 = performance.now(), i0 = pc.cpu.icount, e0 = pc.timeMs;
  const done = pc.until(() => /frames .*seconds .*fps/.test(since(pc, mark)), { timeoutMs: 600000, stepMs: 100 });
  const ms = performance.now() - t0, insns = pc.cpu.icount - i0;
  const line = (since(pc, mark).match(/\d+ frames [^\n]*fps/) || [''])[0];
  check(done, `timedemo: ${line}`);
  console.log(`     ${MHZ || 100} MHz: ${(pc.timeMs - e0).toFixed(0)} ms emulated, ${(insns / 1e6).toFixed(0)}M instructions, ` +
    `host ${ms.toFixed(0)} ms = ${(insns / ms / 1000).toFixed(0)} host MIPS`);
  await shot(pc, 'timedemo-end.png');
}

// ----------------------------------------------------------------- files
async function files() {
  console.log('== files (the Files panel: web/js/fat.js writes pak0.pak onto the factory C:)');
  const { FatVolume } = await import('../../../web/js/fat.js');
  const img = makeImage();
  const v = new FatVolume(img);
  let dir = null;
  for (const name of ['GAMES', 'HEXEN2', 'DATA1']) dir = v.entries(dir).find((e) => e.name === name && (e.attr & 0x10));
  check(!!dir, 'C:\\GAMES\\HEXEN2\\DATA1 is on the factory C:');
  const data = new Uint8Array(fs.readFileSync(PAK));
  const t = performance.now();
  const n83 = v.writeFile(dir, 'pak0.pak', data, { when: new Date('1997-11-07T00:24:00') });
  console.log(`     wrote ${n83} (${data.length} bytes) in ${(performance.now() - t).toFixed(0)} ms, ${v.takeDirty().length} sectors`);
  check(n83 === 'PAK0.PAK', 'the host name pak0.pak becomes PAK0.PAK');
  const pc = await start(img);
  check(pc.waitText('C:\\>', { timeoutMs: 20000 }), 'DOS prompt');
  pc.type('CLS\rDIR \\GAMES\\HEXEN2\\DATA1\r'); pc.run(1500);
  console.log(screenLog(pc));
  check(pc.hasText('27750257') || pc.hasText('27,750,257'), 'DIR shows PAK0.PAK with its size');
  pc.type('CLS\rC:\\GAMES\\HEXEN2\\INSTALL\r');
  check(pc.until(() => pc.hasText('Type HEXEN2 to play'), { timeoutMs: 60000 }), 'INSTALL: the copied PAK0.PAK is good (CRC-32)');
  pc.type('HEXEN2 -nosound\r');
  check(pc.until(() => mode(pc) === 0x13 && pc.debug.includes('Hexen II Initialized'), { timeoutMs: 60000 }), 'HEXEN2 starts the game');
  pc.run(2000);
  await shot(pc, 'files-menu.png');
}

const all = { nodata, play, timedemo, files };
for (const n of (which.length ? which : ['nodata', 'play'])) {
  if (n !== 'nodata' && !havePak) { console.log(`== ${n}: SKIPPED - no Hexen II demo pak0.pak (--pak FILE or HEXEN2_PAK)`); continue; }
  await all[n]();
}
console.log(failures ? `${failures} FAILED` : 'all passed');
process.exit(failures ? 1 : 0);
