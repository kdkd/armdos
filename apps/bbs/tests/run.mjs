// apps/bbs/tests/run.mjs - two ARM-PCs on one phone line: the visitor runs
// TERM.EXE, the other machine boots build/bbs.img (The ARM Pit BBS). The
// caller dials, registers as a new user, reads and posts messages, downloads
// a file by ZMODEM (checked byte for byte on the visitor's disk), uploads one,
// plays the door game, and logs off (NO CARRIER). Screenshots go to
// build/term-test/bbs-*.png.
import fs from 'node:fs';
import path from 'node:path';
import { makeDisk, startPC, runAll, has, screen, check, failed, readFile, tapCom2, PhoneExchange, B, ROOT } from '../../term/tests/lib.mjs';

const rtc = new Date(1989, 10, 4, 21, 30, 0).getTime();
const notes = 'Notes from the Analytical Engine.\r\n'.repeat(40);
const img = makeDisk('visitor', {
  files: [{ src: 'build/TERM.EXE', dst: 'TERM\\TERM.EXE' }, { src: 'apps/term/data/TERM.DIR', dst: 'TERM\\TERM.DIR' }],
  texts: { 'NOTES.TXT': notes },
  dirs: ['TERM', 'TERM\\DOWNLOAD'],
});
const ex = new PhoneExchange();            // the real modems of docs/MODEM.md, 2400 bps
const v = await startPC(img, { rtcBaseMs: rtc, phone: ex, phoneNumber: '555-2000' });
const bbs = await startPC(new Uint8Array(fs.readFileSync(B('bbs.img'))), { rtcBaseMs: rtc, phone: ex, phoneNumber: '555-1989' });
const pcs = [v, bbs], T = [];

// everything the caller's UART receives (modem result codes + the BBS), escape sequences removed
const rec = tapCom2(v);
let mark = 0;
const plain = () => rec.text.replace(/\x1b\[[0-9;?]*[A-Za-z]/g, '');
const since = () => plain().slice(mark);
async function expect(text, ms = 60000) {
  const ok = await runAll(pcs, T, ms, 4, () => since().includes(text));
  if (ok) { const p = plain(); mark = p.indexOf(text, mark) + text.length; }
  else console.log(`     (waited for ${JSON.stringify(text)}; last: ${JSON.stringify(since().slice(-300))})`);
  return ok;
}
async function say(keys, pause = 300) { v.type(keys); await runAll(pcs, T, pause); await runAll(pcs, T, 20000, 4, () => v.m.typingDone()); }
const shot = (pc, name) => pc.shot(`bbs-${name}.png`);

// ---- boot both machines
check(await runAll(pcs, T, 30000, 4, () => has(v, 'C:\\>') && has(bbs, 'Waiting for call')), 'both machines booted; BBS shows "Waiting for call..."');
check(has(bbs, 'Last caller:') && has(bbs, 'Calls today:'), 'BBS sysop screen: last caller, calls today');
await shot(bbs, 'waiting');

// ---- TERM, dialing directory, dial
v.type('CD \\TERM\rTERM\r');
check(await runAll(pcs, T, 8000, 4, () => has(v, 'Alt-Z for Help')), 'TERM starts with its status line');
await say('{ALT+D}', 800);
check(has(v, 'DIALING DIRECTORY') && has(v, 'The ARM Pit BBS') && has(v, '555-1989') && has(v, 'ARM-DOS Host Link'), 'Alt-D: dialing directory with the BBS and the Host Link');
await shot(v, 'term-directory');
await say('{ENTER}', 100);
check(await runAll(pcs, T, 5000, 4, () => has(v, 'ATDT5551989')), 'dialing box shows ATZ / init / ATDT5551989');
check(has(v, 'Dialing:') && has(v, 'Attempt:') && has(v, 'AT&C1&D2'), 'dialing box: attempt, time, modem init string');
await shot(v, 'term-dialing');
check(await runAll(pcs, T, 20000, 4, () => has(bbs, 'RING') || has(bbs, 'CONNECT')), 'BBS modem log shows RING');
check(await expect('What is your FIRST name?', 60000), 'CONNECT; BBS logon screen asks "What is your FIRST name?"');
check(has(v, 'Online 00:0'), 'TERM status line: Online timer');
await shot(v, 'logon');

// ---- new user
await say('Ada\r'); check(await expect('What is your LAST name?'), 'asks for the LAST name');
await say('Lovelace\r'); check(await expect('Did you enter your name correctly'), 'unknown name: "Did you enter your name correctly?"');
await say('Y'); check(await expect('City and State'), 'new-user questionnaire starts');
await shot(v, 'newuser');
await say('London, UK\r'); await expect('phone number');
await say('555-1815\r'); await expect('birthdate');
await say('12-10-15\r'); await expect('computer');
await say('Analytical Engine\r'); await expect('lines');
await say('\r'); await expect('password');
await say('BABBAGE\r'); await expect('again');
await say('BABBAGE\r');
check(await expect('Is this information correct'), 'questionnaire summary');
await say('Y'); check(await expect('access level 10'), 'new user saved with access level 10');
await say('\r'); check(await expect('caller number'), 'welcome screen with caller number');
await say('\r'); check(await expect('View the bulletins'), 'offers bulletins');
await say('Y'); check(await expect('Bulletin # to read'), 'bulletin menu');
await say('1\r'); check(await expect('RULES OF THE ARM PIT'), 'bulletin 1');
await say('\r'); await expect('Bulletin # to read');
await say('\r'); check(await expect('Main Menu'), 'main menu');
await runAll(pcs, T, 1500);
check(has(v, '[M] Messages') && has(v, '[D] Doors'), 'main menu shows [M]essages [F]iles [B]ulletins [D]oors ...');
check(has(v, 'Goodbye'), 'main menu has [G]oodbye');
await shot(v, 'mainmenu');
await shot(bbs, 'session-local');
check(has(bbs, 'Ada Lovelace') && has(bbs, 'Time left'), 'sysop console: caller name and time left on the status line');

// ---- messages
await say('M'); check(await expect('Message Menu'), 'message menu');
await say('R'); await expect('Read from message');
await say('\r'); check(await expect('Welcome to The ARM Pit!'), 'reads message #1 (the sysop welcome)');
await say('N'); check(await expect('Anyone else got the new ARM/AT?'), 'Next: message #2 from Dave Morgan');
await runAll(pcs, T, 2500);
await shot(v, 'message');
await say('Q'); await expect('Message Menu');
await say('E'); await expect('To (Enter = All)');
await say('\r'); await expect('Subject:');
await say('Hello from 1843\r'); await expect(' 1:');
await say('The Analytical Engine weaves algebraical patterns\r'); await expect(' 2:');
await say('just as the Jacquard loom weaves flowers and leaves.\r'); await expect(' 3:');
await say('/S\r');
const nextMain = (fs.readFileSync(path.join(ROOT, 'apps/bbs/data/BBS/MSGS/MAIN.MSG'), 'latin1').match(/^@@/gm) || []).length + 1;
check(await expect(`Saving message #${nextMain}`), `posted message #${nextMain}`);
const mainMsg = readFile(bbs, 'BBS\\MSGS\\MAIN.MSG')?.toString('latin1') || '';
check(mainMsg.includes('From: Ada Lovelace') && mainMsg.includes('Subj: Hello from 1843') && mainMsg.includes('Jacquard loom'), 'message is in C:\\BBS\\MSGS\\MAIN.MSG on the BBS disk');
await say('\r'); await expect('Message Menu');
await say('Q'); await expect('Main Menu');

// ---- files: list, ZMODEM download, ZMODEM upload
await say('F'); check(await expect('File Menu'), 'file menu');
await say('A'); await expect('Area #');
await say('3\r'); await expect('File Menu');
await say('L'); check(await expect('ARMTIPS.TXT'), 'file list shows ARMTIPS.TXT');
await runAll(pcs, T, 1500);
await shot(v, 'filelist');
await say('\r'); await expect('File Menu');
await say('D'); await expect('Filename to download');
await say('ARMTIPS.TXT\r'); check(await expect('Estimated transfer time'), 'download: size and estimated time');
await say('Z');
check(await runAll(pcs, T, 30000, 4, () => has(v, 'ZMODEM DOWNLOAD')), 'TERM starts the ZMODEM download by itself (auto-download)');
await runAll(pcs, T, 4000);
check(has(v, 'ARMTIPS.TXT') && has(v, 'CPS:'), 'transfer window: file name, bytes, CPS, time left');
await shot(v, 'zmodem-download');
check(await expect('Transfer successful', 120000), 'BBS: "Transfer successful"');
const src = fs.readFileSync(path.join(ROOT, 'apps/bbs/data/BBS/FILES/TEXT/ARMTIPS.TXT'));
const got = readFile(v, 'TERM\\DOWNLOAD\\ARMTIPS.TXT');
check(got && Buffer.compare(got, src) === 0, `ARMTIPS.TXT arrived intact on the visitor's C:\\TERM\\DOWNLOAD (${got ? got.length : 0} of ${src.length} bytes)`);
await say('\r'); await expect('File Menu');
await say('D'); await expect('Filename to download');
await say('MODEMS.TXT\r'); await expect('Protocol');
await say('Y'); check(await expect('Start your download now'), 'YMODEM download offered');
await say('{PGDN}', 600); await say('Y', 300);
check(await expect('Transfer successful', 90000), 'YMODEM (batch, 1K blocks) download from the BBS');
const modems = readFile(v, 'TERM\\DOWNLOAD\\MODEMS.TXT');
check(modems && Buffer.compare(modems, fs.readFileSync(path.join(ROOT, 'apps/bbs/data/BBS/FILES/TEXT/MODEMS.TXT'))) === 0, 'MODEMS.TXT intact (YMODEM truncates to the real size)');
await say('\r'); await expect('File Menu');
await say('U'); await expect('Protocol');
await say('Z'); await expect('Describe your file');
await say('Notes on the Engine\r'); check(await expect('Begin your ZMODEM upload'), 'upload: BBS waits for ZMODEM');
await say('{PGUP}', 600); check(has(v, 'UPLOAD'), 'PgUp: protocol menu');
await say('Z', 500); await say('C:\\NOTES.TXT\r', 500);
check(await expect('Got it: NOTES.TXT', 90000), 'upload received by the BBS');
const up = readFile(bbs, 'BBS\\FILES\\UPLOADS\\NOTES.TXT');
check(up && up.toString('latin1') === notes, 'NOTES.TXT intact in C:\\BBS\\FILES\\UPLOADS on the BBS disk');
check((readFile(bbs, 'BBS\\FILES\\UPLOADS\\FILES.BBS')?.toString('latin1') || '').includes('NOTES.TXT'), 'FILES.BBS lists the upload');
await say('\r'); await expect('File Menu');
await say('Q'); await expect('Main Menu');

// ---- the door
await say('D'); check(await expect('Door # to open'), 'doors menu');
await say('1\r'); check(await expect('Opening door'), 'BBS opens the door (DOOR.SYS / DORINFO1.DEF written)');
check(await expect('R I S C', 30000), 'DRAGON.EXE talks to the caller over the same line');
const dropsys = readFile(bbs, 'BBS\\DOOR.SYS')?.toString('latin1') || '';
check(dropsys.startsWith('COM2:') && dropsys.includes('Ada Lovelace'), 'DOOR.SYS: COM2:, caller name');
check(await expect('become an adventurer'), 'new player prompt');
await say('Y'); await expect('Press [Enter]');
await say('\r'); check(await expect('Town Square'), 'door: town square menu');
await runAll(pcs, T, 1500);
await shot(v, 'door-town');
await say('F'); await expect('The Forest');
let fights = 0, kills = 0;
for (let turn = 0; turn < 40 && fights < 3; turn++) {
  const s = since();
  if (/\(A\)ttack/.test(s.slice(-120)) || s.slice(-80).includes('ttack')) { await say('A', 400); mark = plain().length - 40; }
  else if (s.includes('Press [Enter]')) { if (s.includes('You have killed')) kills++; await say('\r', 400); mark = plain().length; }
  else if (s.includes('fights left')) { fights++; await say('L', 400); mark = plain().length - 0; }
  else if (s.includes('slain')) break;
  await runAll(pcs, T, 2500);
}
check(fights >= 2, `played ${fights} forest turns in the door (${kills} kills)`);
await shot(v, 'door-forest');
await runAll(pcs, T, 3000);
if (since().includes('Press [Enter]') || has(v, 'Press [Enter]')) await say('\r', 1500);
// back to town and out (fight/pause states: answer until the forest menu or the BBS)
for (let i = 0; i < 12 && !has(v, 'Welcome back to The ARM Pit'); i++) {
  const s = plain().slice(-400);
  if (/\[L H R V\]: ?$/.test(s.trimEnd()) || s.trimEnd().endsWith('[L H R V]:')) await say('R', 800);
  else if (s.trimEnd().endsWith(']:') && s.includes('Town Square')) await say('Q', 800);
  else if (s.includes('ttack') && s.trimEnd().endsWith(':')) await say('A', 800);
  else if (s.includes('Press [Enter]')) await say('\r', 800);
  else if (s.includes('Come back tomorrow')) await say('\r', 800);
  await runAll(pcs, T, 3000);
}
check(await expect('Welcome back to The ARM Pit', 30000), 'door exits back to the BBS, line still up');
const players = readFile(bbs, 'BBS\\DOORS\\DRAGON\\DRAGON.DAT');
check(players && players.toString('latin1').includes('Ada Lovelace'), 'DRAGON.DAT has the new player');
await say('\r'); await expect('Door # to open');
await say('\r'); check(await expect('Main Menu'), 'back at the main menu');

// ---- goodbye
await say('G'); check(await expect('Log off now'), 'Goodbye asks to confirm');
await say('Y');
check(await expect('Thank you for calling The ARM Pit!'), '"Thank you for calling The ARM Pit!"');
check(await expect('NO CARRIER', 30000), 'BBS drops DTR: TERM shows NO CARRIER')
await runAll(pcs, T, 2000);
check(has(v, 'Offline'), 'TERM status line back to Offline');
await shot(v, 'goodbye');
check(await runAll(pcs, T, 20000, 4, () => has(bbs, 'Waiting for call') && has(bbs, 'Ada Lovelace')), 'BBS recycles: waiting for the next call, last caller = Ada Lovelace') || console.log(screen(bbs));
await shot(bbs, 'after-call');
const log = readFile(bbs, 'BBS\\CALLERS.LOG')?.toString('latin1') || '';
check(log.includes('NEW USER: Ada Lovelace') && log.includes('downloaded ARMTIPS.TXT') && log.includes('opened door'), 'CALLERS.LOG records the call');

// ---- log on again as the (now known) user, then hang up from TERM (Alt-H)
await say('{ALT+D}', 800); await say('1', 100);
check(await expect('What is your FIRST name?', 60000), 'second call connects');
await say('Ada\r'); await expect('LAST name');
await say('Lovelace\r'); check(await expect('Password?'), 'known user: asks for the password');
await say('BABBAGE\r'); check(await expect('Welcome back'), 'password accepted: "Welcome back"');
await say('\r'); await expect('View the bulletins');
await say('N'); check(await expect('Main Menu'), 'main menu (bulletins skipped)');
await say('W'); check(await expect("WHO'S ONLINE") && await expect('Ada Lovelace'), "[W]ho's online");
await say('\r'); await expect('Main Menu');
await say('U'); check(await expect('USERLOG') && await expect('Dave Morgan') && await expect('Ada Lovelace'), '[U]serlog lists the 1989 regulars and the new user');
await runAll(pcs, T, 1500);
await shot(v, 'userlog');
await say('\r'); await expect('Main Menu');
await say('C'); await expect('Subject:');
await say('Great board!\r'); await expect(' 1:');
await say('Thanks for the ZMODEM help.\r'); await expect(' 2:');
await say('/S\r'); check(await expect('Saving message #1'), '[C]omment to sysop saved');
check((readFile(bbs, 'BBS\\MSGS\\COMMENTS.MSG')?.toString('latin1') || '').includes('To: Europa Sysop'), 'comment in C:\\BBS\\MSGS\\COMMENTS.MSG');
await say('\r'); await expect('Main Menu');
await say('{ALT+H}', 3000);
check(await runAll(pcs, T, 15000, 4, () => has(v, 'Offline')), 'Alt-H hangs up (DTR drop)');
check(await runAll(pcs, T, 30000, 4, () => has(bbs, 'Waiting for call')), 'BBS notices the carrier loss and recycles') || console.log(screen(bbs), screen(v));
check((readFile(bbs, 'BBS\\CALLERS.LOG')?.toString('latin1') || '').includes('carrier lost'), 'CALLERS.LOG: carrier lost');

// ---- a guest who types GUEST as the last name too: the guest account all the same
await say('{ALT+D}', 800); await say('1', 100);
check(await expect('What is your FIRST name?', 60000), 'third call connects');
await say('GUEST\r'); await expect('LAST name');
const m0 = mark;
await say('GUEST\r');
check(await expect('Password?') && !plain().slice(m0, mark).includes('was not found') && plain().slice(m0, mark).includes('Searching for GUEST in'),
  'GUEST GUEST: searched as plain GUEST (the guest account), asks for the password', plain().slice(m0, mark));
await say('GUEST\r');
check(await runAll(pcs, T, 20000, 4, () => since().includes('Welcome') || since().includes('Main Menu')) && !since().includes('Incorrect password'), 'the guest password logs on');
await say('{ALT+H}', 3000);
check(await runAll(pcs, T, 15000, 4, () => has(v, 'Offline')) && await runAll(pcs, T, 30000, 4, () => has(bbs, 'Waiting for call')), 'hung up; the BBS recycles');

// ---- the sysop logs on locally (F1 on the BBS machine)
bbs.type('{F1}');
check(await runAll(pcs, T, 10000, 4, () => has(bbs, 'What is your FIRST name?')), 'F1 on the BBS console: local logon');
bbs.type('Europa\r'); await runAll(pcs, T, 1500);
bbs.type('Sysop\r'); await runAll(pcs, T, 1500);
bbs.type('ACORN\r');
check(await runAll(pcs, T, 10000, 4, () => has(bbs, 'Welcome back')), 'sysop logged on locally');
bbs.type('\r'); await runAll(pcs, T, 1500); bbs.type('N');
check(await runAll(pcs, T, 10000, 4, () => has(bbs, 'Main Menu')), 'local session: main menu');
bbs.type('G'); await runAll(pcs, T, 1000); bbs.type('Y');
check(await runAll(pcs, T, 20000, 4, () => has(bbs, 'Waiting for call')), 'local session ends, BBS waits for calls again');

await say('{ALT+X}', 500); await say('Y', 500);
check(await runAll(pcs, T, 5000, 4, () => has(v, 'C:\\TERM>')), 'Alt-X exits TERM to DOS');
console.log(failed() ? `${failed()} FAILED` : 'all passed');
process.exit(failed() ? 1 : 0);
