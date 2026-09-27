// apps/online/tests/run.mjs - ONLINE.EXE on a headless ARM-PC with the real COM2 modem and
// phone exchange, calling the ARM-DOS Online service (emu/online/, fixture mode: canned
// responses, no network): sign on, the Encyclopedia (search, read, Tab to a link, follow
// it, Back), a picture in mode 13h (mid-download and complete, saved to disk), the
// weather for Berlin, printing, sign off (NO CARRIER).
//   node apps/online/tests/run.mjs [rate]      (default 14400; 2400 takes longer)
// Screenshots: build/online-test/*.png
import fs from 'node:fs';
import path from 'node:path';
import { makeDisk, runAll, has, screen, check, failed, readFile, tapCom2, PhoneExchange, B } from '../../term/tests/lib.mjs';
import { boot } from '../../../emu/testkit.mjs';
import { OnlineService } from '../../../emu/online/service.mjs';
import { fixtureFetch, decodePPM, RECORDED_AT } from './fixtures.mjs';

const RATE = +(process.argv[2] || 14400);
const OUT = B('online-test');
fs.mkdirSync(OUT, { recursive: true });

const img = makeDisk('onlinetest', {
  files: [{ src: 'build/ONLINE.EXE', dst: 'ONLINE\\ONLINE.EXE' }, { src: 'apps/online/data/ONLINE.CFG', dst: 'ONLINE\\ONLINE.CFG' },
          { src: 'apps/online/data/ONLINE.BAT', dst: 'DOS\\ONLINE.BAT' }, { src: 'build/MOUSE.COM', dst: 'DOS\\MOUSE.COM' }],
  dirs: ['ONLINE', 'ONLINE\\PICTURES'],
  autoexec: 'MOUSE\n',
});
const ex = new PhoneExchange();
const printed = [];
const pc = await boot({ rom: B('rom.bin'), hd: img, rtcBaseMs: new Date(1989, 10, 4, 21, 30, 0).getTime(), phone: ex, phoneNumber: '555-2000',
  modemRate: RATE, onPrint: (b) => printed.push(b) });
const shot = async (n) => { const p = path.join(OUT, n + '.png'); await pc.png(p); console.log(`     screenshot ${p}`); };
const rec = tapCom2(pc);
const missing = [];
const svc = new OnlineService({ fetch: fixtureFetch({ missing }), decodeImage: decodePPM, now: () => RECORDED_AT });
svc.net.minGapMs = 0; svc.net.hostGaps = {};
ex.register('555-0199', svc);
const T = [(now) => svc.tick(now)];
const run = (ms, pred) => runAll([pc], T, ms, 4, pred);
async function say(keys, pause = 300) { pc.type(keys); await run(pause); await run(20000, () => pc.m.typingDone()); }
const vmode = () => pc.m.vga.mode;
const cellAttr = (x, y) => pc.m.cpu.m8[0xB8000 + (y * 80 + x) * 2 + 1];
/** the text of the highlighted (selected) link on screen */
function selected() {
  let s = '';
  for (let y = 2; y < 23; y++) for (let x = 2; x < 79; x++) if (cellAttr(x, y) === 0x70) s += pc.lines()[y][x];
  return s.trim();
}

check(await run(30000, () => has(pc, 'C:\\>')), 'boots to C:\\> (MOUSE loaded)');
pc.type('ONLINE\r');
check(await run(10000, () => has(pc, 'Screen Name:') && has(pc, 'Access number 555-0199')), 'ONLINE: the sign-on screen');
check(has(pc, 'Set the speed switch on the modem (below the PC) first: 2400 bps to 56K.'), 'ONLINE: the note about the modem\'s speed switch');
check(has(pc, 'Dialing') && has(pc, 'Connecting') && has(pc, 'Verifying password'), 'sign-on: the three steps');
await shot('signon');

// ---- sign on
await say('Tester\r');
pc.type('secret\r');
check(await run(8000, () => has(pc, 'ATDT555-0199')), 'dials ATDT555-0199');
await run(1500);
await shot('dialing');
check(await run(60000, () => has(pc, 'Welcome, Tester!')), `CONNECT ${RATE}, verified, main menu: "Welcome, Tester!"`);
// menu_screen() writes "Welcome" near its start and the status bar last, some 30,000
// instructions (0.3 ms) later: a 4 ms run step can end in between, and then the channels and
// the status bar are not on screen yet. Where the step ends shifts with every change to the
// code, so let the drawing finish before looking at it.
await run(1000, () => has(pc, 'Online 00:0'));
check(svc.member === 'Tester', 'the service knows the screen name');
check(has(pc, 'Encyclopedia') && has(pc, 'Technology News') && has(pc, 'Today in History') && has(pc, 'Sign Off'), 'main menu: the channels');
check(new RegExp(`${RATE} bps`).test(screen(pc)) && has(pc, 'Online 00:0'), 'status bar: connect speed and online time');
await run(1500);
await shot('menu');
check(readFile(pc, 'ONLINE\\ONLINE.CFG')?.toString().includes('NAME=Tester'), 'screen name saved in ONLINE.CFG');

// ---- the Encyclopedia
await say('1', 600);
check(has(pc, 'Search the Encyclopedia for:'), '1 = Encyclopedia: the search prompt');
await say('ARM architecture\r', 300);
check(await run(30000, () => has(pc, '1. ARM architecture family') && has(pc, 'Line 1 of 52 ')), 'search results arrive: 10 articles');
await shot('search');
await say('\t', 300);
check(selected() === 'ARM architecture family', `Tab selects the first result (${selected()})`);
await say('\r', 300);
const header = () => pc.lines()[1].trim();
check(await run(60000, () => header().startsWith('ARM architecture family') && has(pc, 'is a family of RISC instruction set')), 'Enter: the article arrives and is shown as it comes');
check(has(pc, 'ARM architecture family') && has(pc, 'Encyclopedia'), 'title and channel on screen');
await shot('article');
await say('\t\t', 300);
check(selected() === 'RISC', `Tab Tab: the link "RISC" (${selected()})`);
await say('\r', 300);
check(await run(90000, () => header().startsWith('Reduced instruction set computer') && has(pc, 'a reduced instruction set computer')), 'follow the link: Reduced instruction set computer');
await shot('link');
await say('{ESC}', 800);
check(await run(60000, () => header().startsWith('ARM architecture family') && has(pc, 'is a family of RISC instruction set')), 'Esc: back to the ARM article (asked for again: it was cut short)');
check(await run(RATE < 9600 ? 400000 : 120000, () => /Line 1 of \d{3,} /.test(screen(pc))), 'the whole article arrives');
await say('{PGDN}{PGDN}', 400);
check(!has(pc, 'is a family of RISC instruction set'), 'PgDn scrolls');
await shot('scrolled');

// ---- a picture: its link number, then watch it arrive in mode 13h
const armDoc = [...svc.docs.values()].find((d) => d.title === 'ARM architecture family');
const pn = armDoc.links.findIndex((l) => l && l.kind === 'P' && /Acorn-ARM/.test(l.src));
const before = rec.text.length;
await say(String(pn), 200);
check(has(pc, 'Go to link number: ' + pn), `typing ${pn}: "Go to link number"`);
await say('\r', 100);
check(await run(30000, () => vmode() === 0x13), 'the picture link switches to VGA mode 13h');
const gifLen = () => svc.gifCache.values().next().value?.gif.length || 1;
check(await run(RATE < 9600 ? 300000 : 120000, () => rec.text.length - before > gifLen() * 0.45), 'the GIF comes down the line');
await shot('picture-arriving');
check(await run(RATE < 9600 ? 400000 : 180000, () => rec.text.length - before > gifLen() && svc.out.length === 0 && pc.m.modem.backlog() === 0), 'the whole GIF arrived');
await run(3000);
await shot('picture-complete');
await say('S', 1500);
const saved = readFile(pc, 'ONLINE\\PICTURES\\ACORNARM.GIF');
const g = svc.gifCache.values().next().value;
check(saved && Buffer.compare(saved, Buffer.from(g.gif)) === 0, `S saves C:\\ONLINE\\PICTURES\\ACORNARM.GIF (${saved?.length} bytes, identical)`);
await say(' ', 1000);
check(vmode() === 0x03 && has(pc, 'ARM architecture family'), 'any key: back to the text reader');

// ---- printing
await say('P', 3000);
const ptxt = Buffer.from(printed).toString('latin1');
check(ptxt.includes('ARM-DOS Online - Encyclopedia') && ptxt.includes('ARM architecture family') && ptxt.includes('\f'), `P prints the article on PRN (${printed.length} bytes)`);

// ---- weather
await say('{F10}', 800);
check(has(pc, 'Welcome, Tester!'), 'F10: the main menu');
await say('3', 600);
check(has(pc, 'City (for example'), '3 = Weather: the city prompt');
await say('Berlin\r', 300);
check(await run(30000, () => has(pc, 'THE WEEK AHEAD') && has(pc, 'Berlin, State of Berlin, DE') && /Line 1 of \d+ /.test(screen(pc))), 'weather for Berlin');
await shot('weather');

// ---- sign off
await say('{ALT+X}', 300);
check(await run(30000, () => has(pc, 'Signed off after')), 'Alt-X: signed off');
check(has(pc, 'NO CARRIER'), 'the modem said NO CARRIER');
check(svc.state === 'idle' && !pc.m.modem.carrier, 'the line is free again');
await shot('signoff');
await say('{ESC}', 1000);
check(await run(5000, () => has(pc, 'Thank you for using ARM-DOS Online.') && has(pc, 'C:\\>')), 'Esc: back to DOS');
check(missing.length === 0, 'every request had a fixture' + (missing.length ? ': ' + missing.join(' ') : ''));
console.log(`\nemulated time ${(pc.m.timeMs() / 1000).toFixed(1)} s at ${RATE} bps`);
console.log(failed() ? `\n${failed()} FAILED` : '\nall passed');
process.exit(failed() ? 1 : 0);
