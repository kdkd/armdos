// apps/keen/tests/run.mjs - the whole GETKEEN story on two ARM PCs, as a visitor
// would see it: the visitor's machine (C:\GAMES\KEEN as apps/keen/hd.json has
// it: KEEN.EXE without data, GETKEEN.BAT, GETKEEN.SCR; TERM; UNZIP) and The ARM
// Pit BBS (build/bbs.img, with the unmodified KEENDRMS.ZIP in its Games area)
// on one phone line with the real modems (docs/MODEM.md), at their real pacing.
//
//   KEEN without data -> the "not the game data" message
//   GETKEEN -> explanation -> TERM /S:GETKEEN.SCR dials 555-1989 at 14400,
//   logs on as GUEST, downloads KEENDRMS.ZIP by ZMODEM, logs off -> UNZIP -o
//   -> KEEN: loading screen, title, control panel, world map, a level, sound,
//   Esc + Enter -> the shareware ending screen (LAST.SHL) -> DOS works again;
//   GETKEEN a second time goes straight to the game.
//
//   node apps/keen/tests/run.mjs          (14400 bps, as GETKEEN.SCR asks)
//   node apps/keen/tests/run.mjs 2400     (same, but the script without &B14400:
//                                          measures the authentic 2400 bps call)
// Screenshots: build/keen-test/*.png
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { startPC, runAll, has, check, failed, readFile, tapCom2, PhoneExchange, B, ROOT } from '../../term/tests/lib.mjs';
import { build as buildImage } from '../../../disk/mkimage.mjs';

const slow = process.argv.includes('2400');
const OUT = B('keen-test');
fs.mkdirSync(OUT, { recursive: true });
const shot = async (pc, name) => { const p = path.join(OUT, name + '.png'); await pc.png(p); console.log(`     screenshot ${p}`); };

// ---- the zip, as the host sees it (a tiny reader: central directory + inflateRaw)
const ZIP = path.join(ROOT, '3rdparty/keen/KEENDRMS.ZIP');
const zipBuf = fs.readFileSync(ZIP);
function unzipHost(buf) {
  const eocd = buf.lastIndexOf(Buffer.from([0x50, 0x4b, 0x05, 0x06]));
  const n = buf.readUInt16LE(eocd + 10);
  let p = buf.readUInt32LE(eocd + 16);
  const files = {};
  for (let i = 0; i < n; i++) {
    const method = buf.readUInt16LE(p + 10), csize = buf.readUInt32LE(p + 20);
    const nl = buf.readUInt16LE(p + 28), xl = buf.readUInt16LE(p + 30), cl = buf.readUInt16LE(p + 32);
    const lho = buf.readUInt32LE(p + 42), name = buf.toString('latin1', p + 46, p + 46 + nl);
    const d = lho + 30 + buf.readUInt16LE(lho + 26) + buf.readUInt16LE(lho + 28);
    const raw = buf.subarray(d, d + csize);
    files[name.toUpperCase()] = method === 0 ? Buffer.from(raw) : zlib.inflateRawSync(raw);
    p += 46 + nl + xl + cl;
  }
  return files;
}
const zipFiles = unzipHost(zipBuf);
check(Object.keys(zipFiles).length === 24 && zipFiles['KDREAMS.EXE'].length === 81619, 'KEENDRMS.ZIP (apps/keen/data) is the 24-file shareware v1.13 release');

// ---- the visitor's C: (the parts of the real hd.img this story uses)
const tmp = path.join(OUT, 'disk.tmp');
fs.mkdirSync(tmp, { recursive: true });
const text = (name, s) => { const p = path.join(tmp, name); fs.writeFileSync(p, s.replace(/\r?\n/g, '\r\n')); return path.relative(ROOT, p); };
const frag = (app) => JSON.parse(fs.readFileSync(path.join(ROOT, 'apps', app, 'hd.json'), 'utf8'));
const files = [
  { src: 'build/IO.SYS', attr: 'HSR', first: 1 }, { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
  { src: 'build/COMMAND.COM' }, { src: 'build/HIMEM.SYS', dst: 'DOS\\' }, { src: 'build/SBMIX.EXE', dst: 'DOS\\' },
  { src: 'build/UNZIP.EXE', dst: 'DOS\\' },
  { src: text('CONFIG.SYS', 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=20\nBUFFERS=20\nSHELL=C:\\COMMAND.COM C:\\ /P\n'), dst: 'CONFIG.SYS' },
  { src: text('AUTOEXEC.BAT', '@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\nSET BLASTER=A220 I7 D1 H5 T6\nSBMIX /INIT /Q\n'), dst: 'AUTOEXEC.BAT' },
  ...frag('term').files, ...frag('keen').files,
];
const dirs = ['DOS', ...frag('term').dirs, ...frag('keen').dirs];
if (slow) {   // the same script, without asking the modem for 14400
  const scr = fs.readFileSync(path.join(ROOT, 'apps/keen/data/GETKEEN.SCR'), 'latin1')
    .replace('AT&C1&D2&B14400', 'AT&C1&D2').replace('Connecting at 14400 to save you time...', 'Connecting at 2400, the authentic way...');
  const i = files.findIndex((f) => /GETKEEN\.SCR$/i.test(f.dst || ''));
  files[i] = { src: text('GETKEEN.SCR', scr), dst: 'GAMES\\KEEN\\GETKEEN.SCR' };
}
const { img } = buildImage({ format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'VISITOR', date: '1992-10-01 20:00:00',
  boot: { src: 'build/bootsect.bin' }, files, dirs }, ROOT);
fs.rmSync(tmp, { recursive: true, force: true });

// ---- two machines, one phone line
const rtc = new Date(1992, 9, 1, 20, 0, 0).getTime();
const ex = new PhoneExchange();
let speakerOn = 0;
// (nojit: the visitor's machine without the JIT)
const v = await startPC(img, { rtcBaseMs: rtc, phone: ex, phoneNumber: '555-2000', jit: !process.argv.includes('nojit'), onSpeaker(on) { if (on) speakerOn++; } });
const bbs = await startPC(new Uint8Array(fs.readFileSync(B('bbs.img'))), { rtcBaseMs: rtc, phone: ex, phoneNumber: '555-1989' });
const pcs = [v, bbs];
const rec = tapCom2(v);
let oplWrites = 0;
{ const opl = v.m.sb.opl, w = opl.write.bind(opl); opl.write = (port, val, ...r) => { if (port & 1) oplWrites++; return w(port, val, ...r); }; }
const until = (pred, ms) => runAll(pcs, [], ms, 4, pred);
const run = (ms) => runAll(pcs, [], ms, 4);
const typed = async (keys, pause = 300) => { v.type(keys); await run(pause); await until(() => v.m.typingDone(), 20000); };
const hold = async (code, ms) => { v.m.keyDown(code); await run(ms); v.m.keyUp(code); await run(60); };
const mode = () => v.m.vga.mode;
const sec = (ms) => (ms / 1000).toFixed(1) + ' s';

check(await until(() => has(v, 'C:\\>') && has(bbs, 'Waiting for call'), 30000), 'visitor at C:\\>, The ARM Pit BBS "Waiting for call..."');

// ---- KEEN without its data
await typed('CD \\GAMES\\KEEN\rDIR /W\r', 1500);
check(has(v, 'KEEN     EXE') || has(v, 'KEEN.EXE'), 'C:\\GAMES\\KEEN holds KEEN.EXE (no game data)');
check(has(v, 'GETKEEN  BAT') || has(v, 'GETKEEN.BAT'), 'and GETKEEN.BAT / GETKEEN.SCR / README.TXT');
await typed('CLS\rKEEN\r', 1500);
check(await until(() => has(v, 'not the game data') && has(v, 'run GETKEEN'), 5000), 'KEEN without data: "the engine is here, but not the game data ... run GETKEEN"');
await shot(v, 'keen-nodata');

// ---- GETKEEN
await typed('GETKEEN\r', 1000);
check(await until(() => has(v, 'Keen Dreams is shareware. This will call The ARM Pit BBS'), 5000), 'GETKEEN explains: "Keen Dreams is shareware. This will call The ARM Pit BBS and download it"');
check(has(v, 'LICENSE.DOC') && has(v, 'Press any key to continue'), 'mentions LICENSE.DOC, waits for a key');
await shot(v, 'getkeen-intro');
const t0 = v.m.timeMs();
await typed(' ', 200);
check(await until(() => has(v, 'Script GETKEEN') || has(v, 'GETKEEN'), 6000) && await until(() => has(v, slow ? 'Connecting at 2400' : 'Connecting at 14400 to save you time'), 6000),
  `TERM runs GETKEEN.SCR: "${slow ? 'Connecting at 2400' : 'Connecting at 14400 to save you time...'}"`);
check(await until(() => has(v, 'ATDT5551989'), 15000), 'the dialing box: ATDT5551989');
await shot(v, 'getkeen-dialing');
check(await until(() => rec.text.includes(slow ? 'CONNECT 2400' : 'CONNECT 14400'), 60000), `the modem says ${slow ? 'CONNECT 2400' : 'CONNECT 14400'} (${(rec.text.match(/CONNECT[ 0-9]*/) || ['?'])[0]})`);
check(await until(() => has(v, 'FIRST name?'), 30000), 'the BBS answers: ANSI welcome screen, "What is your FIRST name?"');
await run(300);
await shot(v, 'getkeen-bbs-logon');
check(await until(() => has(v, 'ZMODEM DOWNLOAD'), 120000), 'the script logs on as GUEST, opens the Games area and starts the ZMODEM download');
await run(8000);
check(has(v, 'KEENDRMS.ZIP') && has(v, 'CPS:'), 'transfer window: KEENDRMS.ZIP, bytes, CPS, time left');
await shot(v, 'getkeen-zmodem');
const tx0 = v.m.timeMs();
check(await until(() => !has(v, 'ZMODEM DOWNLOAD'), slow ? 2400000 : 600000), 'the download finishes');
const tx1 = v.m.timeMs();
check(await until(() => has(v, 'Inflating: KDREAMS.EXE') || has(v, 'Press any key'), 120000), 'the script logs off, TERM exits, UNZIP -o KEENDRMS.ZIP: "Inflating: KDREAMS.EXE"');
await until(() => has(v, 'Press any key'), 20000);
const t1 = v.m.timeMs();
check(has(v, 'Inflating: KDREAMS.EGA') && has(v, 'Extracting: KDREAMS.CMP'), 'UNZIP lists every file (Inflating / Extracting)');
await shot(v, 'getkeen-unzip');
check(bbs.lines().join('\n').includes('Waiting for call') || await until(() => has(bbs, 'Waiting for call'), 20000), 'the BBS recycles to "Waiting for call..."');

// the files on the visitor's disk
const got = readFile(v, 'GAMES\\KEEN\\KEENDRMS.ZIP');
check(got && Buffer.compare(got, zipBuf) === 0, `KEENDRMS.ZIP arrived byte for byte (${got ? got.length : 0} of ${zipBuf.length}) and is kept`);
let same = 0;
for (const [name, data] of Object.entries(zipFiles)) { const f = readFile(v, 'GAMES\\KEEN\\' + name); if (f && Buffer.compare(f, data) === 0) same++; }
check(same === 24, `all 24 files of the zip unpacked unmodified into C:\\GAMES\\KEEN (${same}/24), LICENSE.DOC and VENDOR.DOC included`);

// ---- the game
await typed(' ', 200);
check(await until(() => has(v, 'Ready - Press a Key'), 20000), 'KEEN starts: the text-mode loading screen ("Did you know?" ... "Ready - Press a Key")');
const t2 = v.m.timeMs();
await shot(v, 'keen-loading');
await typed('{ENTER}', 200);
check(await until(() => mode() === 0x13, 5000), 'graphics: the emulated EGA screen is shown in VGA mode 13h');
await run(2500);
await shot(v, 'keen-title');
await typed(' ', 200); await run(2500);
await shot(v, 'keen-controlpanel');
await typed('{ENTER}', 200); await run(3000);
await shot(v, 'keen-worldmap');
const h0 = v.m.haltedNs, n0 = v.m.timeNs();
oplWrites = 0; speakerOn = 0;
await hold('ArrowLeft', 900); await hold('ArrowUp', 300); await hold('ControlLeft', 200);
await run(3000);
await shot(v, 'keen-level');
await hold('ArrowRight', 1500); await hold('ControlLeft', 250); await run(400);
await shot(v, 'keen-jump');
const idle = 100 * (v.m.haltedNs - h0) / (v.m.timeNs() - n0);
check(oplWrites > 0 || speakerOn > 0, `sound: ${oplWrites} AdLib register writes (and ${speakerOn} PC speaker tones) while playing`);
check(idle > 50, `the CPU sleeps in WFI ${idle.toFixed(0)}% of the time while playing`);
await typed('{ESC}', 200); await run(2000);
await shot(v, 'keen-quit-menu');
await typed('{ENTER}', 200);
check(await until(() => has(v, 'Thanks for playing KEEN DREAMS') && has(v, 'C:\\GAMES\\KEEN>'), 10000), 'quit: the shareware ending screen (LAST.SHL) and the DOS prompt');
await shot(v, 'keen-ending');
check(mode() === 3, 'back in text mode');
const tick0 = new DataView(v.m.cpu.m8.buffer).getUint32(0x46C, true); await run(1000);
const tick1 = new DataView(v.m.cpu.m8.buffer).getUint32(0x46C, true);
check(tick1 - tick0 >= 17 && tick1 - tick0 <= 20, `the BIOS clock ticks at 18.2 Hz again (${tick1 - tick0} ticks in 1 s)`);
await typed('CLS\rVER\r', 1000);
check(has(v, 'ARM-DOS'), 'the keyboard is back with DOS (VER)');

// ---- GETKEEN again: the data is there, straight to the game
await typed('GETKEEN\r', 1500);
check(await until(() => has(v, 'Ready - Press a Key'), 10000), 'GETKEEN a second time: no call, KEEN starts right away');
await typed('{ENTER}', 200); await run(3000); await typed('{ESC}', 200); await run(2000); await typed('{ENTER}', 200);
check(await until(() => has(v, 'Thanks for playing KEEN DREAMS'), 10000), 'and quits cleanly again');

console.log(`\n     emulated time: GETKEEN from the key press to the UNZIP'd files ${sec(t1 - t0)} (ZMODEM transfer ${sec(tx1 - tx0)}+),` +
            ` to the game's loading screen ${sec(t2 - t0)} at ${slow ? 2400 : 14400} bps`);
const f = failed();
console.log(f ? `\n${f} check(s) FAILED` : '\nall keen checks passed');
process.exit(f ? 1 : 0);
