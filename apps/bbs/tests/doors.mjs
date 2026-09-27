// apps/bbs/tests/doors.mjs - the board's lore, message bases, doors and the
// DEFRAG download, on two ARM-PCs on one phone line (like run.mjs). The line
// runs at 115200 (PhoneExchange lineRate + both UARTs' baudOverride) so a
// long session stays quick. A regular (Susan Oyelaran) calls: the sysop's
// "welcome back" note and new-message summary, bulletins 5-10 (history, the
// sysop, the Wall of Fame, the live last-10-callers list, the doors' news, the
// user group), a threaded Flame Pit conversation with quoting, the Sysop
// Announcements list, all four doors (a few turns each, back to the BBS with
// the line up), the doors' news bulletin afterwards, and DEFRAG11.ZIP from the
// Utilities area by ZMODEM, UNZIPped on the visitor's disk (when
// build/DEFRAG11.ZIP exists). Screenshots: build/term-test/pit-*.png.
import fs from 'node:fs';
import zlib from 'node:zlib';
import path from 'node:path';
import { makeDisk, startPC, runAll, has, screen, check, failed, readFile, tapCom2, PhoneExchange, B, ROOT } from '../../term/tests/lib.mjs';

const rtc = new Date(1989, 10, 5, 20, 15, 0).getTime();
/** the members of a zip file: {name: Buffer} (stored or deflated) */
function unzipMembers(buf) {
  const out = {};
  let e = buf.length - 22;
  while (e > 0 && buf.readUInt32LE(e) !== 0x06054b50) e--;
  let p = buf.readUInt32LE(e + 16);
  for (let i = 0, n = buf.readUInt16LE(e + 10); i < n; i++) {
    const method = buf.readUInt16LE(p + 10), csize = buf.readUInt32LE(p + 20), nl = buf.readUInt16LE(p + 28);
    const xl = buf.readUInt16LE(p + 30), cl = buf.readUInt16LE(p + 32), lo = buf.readUInt32LE(p + 42);
    const name = buf.toString('latin1', p + 46, p + 46 + nl);
    const d = lo + 30 + buf.readUInt16LE(lo + 26) + buf.readUInt16LE(lo + 28);
    const raw = buf.subarray(d, d + csize);
    out[name] = method === 8 ? zlib.inflateRawSync(raw) : Buffer.from(raw);
    p += 46 + nl + xl + cl;
  }
  return out;
}
const defrag = fs.existsSync(B('DEFRAG11.ZIP'));
const img = makeDisk('pit-visitor', {
  files: [{ src: 'build/TERM.EXE', dst: 'TERM\\TERM.EXE' }, { src: 'apps/term/data/TERM.DIR', dst: 'TERM\\TERM.DIR' },
          { src: 'build/UNZIP.EXE', dst: 'DOS\\UNZIP.EXE' }],
  dirs: ['TERM', 'TERM\\DOWNLOAD'],
});
const ex = new PhoneExchange({ lineRate: 115200 });
const v = await startPC(img, { rtcBaseMs: rtc, phone: ex, phoneNumber: '555-2000' });
const bbs = await startPC(new Uint8Array(fs.readFileSync(B('bbs.img'))), { rtcBaseMs: rtc, phone: ex, phoneNumber: '555-1989' });
v.m.com2.baudOverride = 115200; bbs.m.com2.baudOverride = 115200;
const pcs = [v, bbs], T = [];

const rec = tapCom2(v);
let mark = 0;
const plain = () => rec.text.replace(/\x1b\[[0-9;?]*[A-Za-z]/g, '');
const since = () => plain().slice(mark);
async function expect(text, ms = 30000) {
  const ok = await runAll(pcs, T, ms, 4, () => since().includes(text));
  if (ok) { const p = plain(); mark = p.indexOf(text, mark) + text.length; }
  else console.log(`     (waited for ${JSON.stringify(text)}; last: ${JSON.stringify(since().slice(-300))})`);
  return ok;
}
async function say(keys, pause = 200) { v.type(keys); await runAll(pcs, T, pause); await runAll(pcs, T, 20000, 4, () => v.m.typingDone()); }
const settle = (ms = 800) => runAll(pcs, T, ms);
const shot = (pc, name) => pc.shot(`pit-${name}.png`);
const tail = (n = 400) => plain().slice(-n);

// ---- boot, dial, log on as a regular
check(await runAll(pcs, T, 30000, 4, () => has(v, 'C:\\>') && has(bbs, 'Waiting for call')), 'both machines booted');
v.type('CD \\TERM\rTERM\r');
check(await runAll(pcs, T, 8000, 4, () => has(v, 'Alt-Z for Help')), 'TERM starts');
await say('{ALT+D}', 600); await say('{ENTER}', 100);
check(await expect('What is your FIRST name?', 60000), 'the BBS answers');
await say('Susan\r'); await expect('LAST name');
await say('Oyelaran\r'); check(await expect('Password?'), 'a regular: password prompt');
await say('PASSWORD\r'); check(await expect('Welcome back'), '"Welcome back, Susan!"');
check(await expect('A note from Europa, your sysop:'), "the sysop's note for a repeat caller");
check(await expect('New since your last call:'), 'new messages since the last call, per area');
await settle();
await shot(v, 'welcome-back');

// ---- bulletins
await say('\r'); await expect('View the bulletins');
await say('Y'); check(await expect('Bulletin # to read'), 'bulletin menu');
check(since().length >= 0 && plain().includes('A history of The ARM Pit') && plain().includes('SFARM, the user group'), 'bulletin menu lists 10 bulletins');
await settle(); await shot(v, 'bulletins');
const bulletin = async (n, text, what) => {
  await say(`${n}\r`);
  let ok = await expect(text);
  for (let i = 0; i < 6 && !tail(60).includes('Press [Enter]'); i++) {           // page through More prompts
    if (tail(40).includes('More [Y,n,=]?')) await say('Y'); else await settle(500);
  }
  check(ok, what);
  return ok;
};
await bulletin(5, 'A HISTORY OF THE ARM PIT', 'bulletin 5: the history of the board');
check(plain().includes('02-29-88') && plain().includes('00017'), 'history: the leap-day crash, prototype serial 00017');
await settle(); await shot(v, 'history');
await say('\r'); await expect('Bulletin # to read');
await bulletin(6, 'MEET THE SYSOP', 'bulletin 6: the sysop bio');
check(plain().includes('Parity'), 'the sysop bio mentions the cat');
await say('\r'); await expect('Bulletin # to read');
await bulletin(7, 'THE WALL OF FAME', 'bulletin 7: the Wall of Fame');
check(plain().includes('Captain Carrier'), 'Wall of Fame handles');
await settle(); await shot(v, 'walloffame');
await say('\r'); await expect('Bulletin # to read');
await bulletin(8, 'CALLERS TO The ARM Pit', 'bulletin 8: the last callers (live)');
check(/1\s+Susan Oyelaran/.test(plain().slice(-3000)) && plain().slice(-3000).includes('Karen Whitfield'), 'last callers: this call on top, then the seeded callers');
await settle(); await shot(v, 'lastcallers');
await say('\r'); await expect('Bulletin # to read');
await bulletin(9, "TODAY'S NEWS FROM THE DOORS", 'bulletin 9: news from the doors');
check(['Legend of the RISC Dragon', 'RISC Wars', 'Byte-Sized Trivia', 'Pit Poker'].every((d) => plain().slice(-4000).includes(d)), 'door news: all four doors');
await settle(); await shot(v, 'doornews');
await say('\r'); await expect('Bulletin # to read');
await bulletin(10, 'SFARM', 'bulletin 10: the user group');
await say('\r'); await expect('Bulletin # to read');
await say('\r'); check(await expect('Main Menu'), 'main menu');
await settle(); await shot(v, 'mainmenu');
check(has(v, '[L] Last callers'), 'main menu has [L]ast callers');
await say('L'); check(await expect('Susan Oyelaran'), '[L]ast callers from the main menu');
await say('\r'); await expect('Main Menu');

// ---- messages: the areas, a Flame Pit thread, the sysop's announcements
await say('M'); await expect('Message Menu');
await say('A'); check(await expect('Area #'), 'area list');
const areas = plain().slice(-1500);
check(['Main Board', 'ARM/AT Hardware', "Programmers' Corner", 'Games & Adventures', 'For Sale / Trade', 'Sysop Announcements', 'The Flame Pit'].every((a) => areas.includes(a)), 'seven message areas');
const counts = [...areas.matchAll(/\((\d+) msgs\)/g)].map((m) => +m[1]);
check(counts.length === 7 && counts.every((n) => n >= 8), `every area has messages (${counts.join(', ')})`);
await settle(); await shot(v, 'areas');
await say('7\r'); await expect('Message Menu');
await say('R'); await expect('Read from message');
/** after a key that shows a message: page through More prompts to the reader's prompt */
async function toPrompt() {
  for (let i = 0; i < 8 && !/\[Q\]uit: $/.test(tail(80)); i++) { if (/More \[Y,n,=\]\? $/.test(tail(40))) await say('Y'); else await settle(400); }
  return /\[Q\]uit: $/.test(tail(80));
}
await say('1\r'); check(await expect('Msg #1 of') && await toPrompt(), 'Flame Pit message 1');
await say('N'); check(await expect('Msg #2 of') && await toPrompt(), 'next: message 2');
let quoted = false;
for (let i = 0; i < 6 && !quoted; i++) {
  const body = plain().slice(plain().lastIndexOf('Msg #'));
  quoted = /\r\n([A-Z]{1,3})?> \S/.test(body);
  if (!quoted) { await say('N'); await expect('Msg #'); await toPrompt(); }
}
await settle();
check(quoted, 'a reply in the thread quotes the previous message ("> ")');
await shot(v, 'flame-thread');
await say('Q'); await expect('Message Menu');
await say('A'); await expect('Area #');
await say('6\r'); await expect('Message Menu');
await say('S'); check(await expect('New in the file area'), 'Sysop Announcements: "New in the file area: a disk optimizer!"');
for (let i = 0; i < 4 && !tail(60).includes('Press [Enter]'); i++) { if (tail(40).includes('More [Y,n,=]?')) await say('Y'); else await settle(400); }
await settle(); await shot(v, 'sysop-scan');
await say('\r'); await expect('Message Menu');
await say('Q'); await expect('Main Menu');

// ---- the doors
await say('D'); check(await expect('Door # to open'), 'door menu');
check(['Legend of the RISC Dragon', 'RISC Wars', 'Byte-Sized Trivia', 'Pit Poker'].every((d) => plain().slice(-1500).includes(d)), 'four doors listed');
await settle(); await shot(v, 'doors');

/** answer prompts until pred() or the BBS is back; steps: [[regex on the tail, keys], ...] */
async function drive(steps, done, max = 60) {
  for (let i = 0; i < max; i++) {
    if (done()) return true;
    const t = tail(300).trimEnd();
    let hit = false;
    for (const [re, keys] of steps) if (re.test(t)) { await say(keys, 300); hit = true; break; }
    if (!hit) await settle(600);
  }
  return done();
}
const back = () => since().includes('Welcome back to The ARM Pit');
globalThis.__pit = { say, expect, settle, shot: (n) => shot(v, n), tail, since, plain, drive, back, v, bbs, pcs, T, readFile, check, has, screen };
for (const d of ['riscwars', 'trivia', 'poker']) {
  const f = path.join(path.dirname(new URL(import.meta.url).pathname), `door-${d}.mjs`);
  if (fs.existsSync(f)) { const mod = await import(f); await mod.default(globalThis.__pit); }
}
await say('\r'); await expect('Main Menu');
await say('B'); await expect('Bulletin # to read');
await bulletin(9, "TODAY'S NEWS FROM THE DOORS", 'door news after playing');
{
  const shown = plain().slice(-6000).replace(/\s+/g, ' ');
  const lastNews = ['DRAGON\\DRAGNEWS.TXT', 'RISCWARS\\NEWS.TXT', 'TRIVIA\\NEWS.TXT', 'POKER\\NEWS.TXT'].map((f) =>
    (readFile(bbs, 'BBS\\DOORS\\' + f)?.toString('latin1') || '').trim().split(/\r?\n/).pop() || '');
  check(lastNews.every((l) => l && shown.includes(l.slice(0, 50).replace(/\s+/g, ' '))), 'door news shows the newest headline of every door (from the BBS disk)');
}
await settle(); await shot(v, 'doornews-after');
await say('\r'); await expect('Bulletin # to read');
await say('\r'); await expect('Main Menu');

// ---- DEFRAG11.ZIP from the Utilities area
await say('F'); await expect('File Menu');
await say('A'); await expect('Area #');
await say('1\r'); await expect('File Menu');
await say('L'); check(await expect('DEFRAG11.ZIP'), 'Utilities area lists DEFRAG11.ZIP');
await settle(); await shot(v, 'utilities');
for (let i = 0; i < 4 && !tail(60).includes('Press [Enter]'); i++) { if (tail(40).includes('More')) await say('Y'); else await settle(400); }
await say('\r'); await expect('File Menu');
if (defrag) {
  await say('D'); await expect('Filename to download');
  await say('DEFRAG11.ZIP\r'); check(await expect('Estimated transfer time'), 'DEFRAG11.ZIP found');
  await say('Z');
  check(await expect('Transfer successful', 120000), 'ZMODEM download of DEFRAG11.ZIP');
  const got = readFile(v, 'TERM\\DOWNLOAD\\DEFRAG11.ZIP');
  check(got && Buffer.compare(got, fs.readFileSync(B('DEFRAG11.ZIP'))) === 0, `DEFRAG11.ZIP intact on the visitor's disk (${got ? got.length : 0} bytes)`);
  await say('\r'); await expect('File Menu');
} else console.log('     (build/DEFRAG11.ZIP not built: download + UNZIP skipped)');
await say('Q'); await expect('Main Menu');
await say('G'); await expect('Log off now');
await say('Y'); check(await expect('Thank you for calling The ARM Pit!'), 'log off');
check(await expect('NO CARRIER', 30000), 'NO CARRIER');
check(await runAll(pcs, T, 15000, 4, () => has(v, 'Offline')), 'TERM goes offline');
await say('{ALT+X}', 500); await say('Y', 500);
check(await runAll(pcs, T, 20000, 4, () => has(v, 'thank you') && /C:\\TERM>_?\s*$/.test(screen(v).trimEnd())), 'out of TERM') || console.log(JSON.stringify(screen(v).trimEnd().slice(-200)));
await settle(500);
if (defrag) {
  await say('UNZIP C:\\TERM\\DOWNLOAD\\DEFRAG11.ZIP C:\\DEFRAG\\\r', 500);
  check(await runAll(pcs, T, 20000, 4, () => has(v, 'C:\\TERM>') && /DEFRAG\.EXE/i.test(screen(v))), 'UNZIP DEFRAG11.ZIP on the visitor machine');
  await settle(); await shot(v, 'unzip');
  const members = unzipMembers(fs.readFileSync(B('DEFRAG11.ZIP')));
  for (const want of ['DEFRAG.EXE', 'DEFRAG.DOC', 'FILE_ID.DIZ']) {
    const name = Object.keys(members).find((n) => n.toUpperCase().endsWith(want));
    const got = readFile(v, 'DEFRAG\\' + want);
    check(name && got && Buffer.compare(got, members[name]) === 0, `C:\\DEFRAG\\${want} extracted intact (${got ? got.length : 0} bytes)`);
  }
}
const log = readFile(bbs, 'BBS\\LASTCALL.TXT')?.toString('latin1') || '';
check(log.trim().split(/\r?\n/).length === 10 && log.includes('Susan Oyelaran|Atlanta, GA'), 'LASTCALL.TXT keeps the last 10 callers');
console.log(failed() ? `${failed()} FAILED` : 'all passed');
process.exit(failed() ? 1 : 0);
