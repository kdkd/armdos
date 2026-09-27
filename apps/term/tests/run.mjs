// apps/term/tests/run.mjs - TERM.EXE on one ARM-PC with the real COM2 modem
// (emu/dev/modem.mjs) and phone exchange (emu/phone.mjs):
//  * dialing directory, BUSY + redial (867-5309), cancel with Esc
//  * a scripted ANSI endpoint (555-0199): colours, cursor positioning, erase,
//    save/restore, DSR (ESC[6n answered), deferred wrap; scrollback; capture
//  * the Host Link (555-0100, emu/hostlink.mjs, JS ZMODEM): ZMODEM download by
//    auto-start, crash recovery (resume after an aborted download), upload
//  * help, setup, hang up, exit
// Screenshots: build/term-test/term-*.png
import { makeDisk, startPC, runAll, has, screen, check, failed, readFile, tapCom2, PhoneExchange } from './lib.mjs';
import { HostLink } from '../../../emu/hostlink.mjs';

const rtc = new Date(1989, 10, 4, 21, 30, 0).getTime();
const payload = Buffer.alloc(9000);
for (let i = 0; i < payload.length; i++) payload[i] = (i * 7 + (i >> 8) * 13) & 0xFF;   // every byte value incl. ZDLE/XON
const upload = Buffer.from('Uploaded from ARM-DOS by TERM.\r\n'.repeat(60), 'latin1');

const img = makeDisk('termtest', {
  files: [{ src: 'build/TERM.EXE', dst: 'TERM\\TERM.EXE' }, { src: 'apps/term/data/TERM.DIR', dst: 'TERM\\TERM.DIR' },
          { src: 'apps/term/data/TERM.BAT', dst: 'DOS\\TERM.BAT' }],
  texts: { 'UP.TXT': upload.toString('latin1') },
  dirs: ['TERM', 'TERM\\DOWNLOAD'],
});
const ex = new PhoneExchange();
const pc = await startPC(img, { rtcBaseMs: rtc, phone: ex, phoneNumber: '555-2000' });
const rec = tapCom2(pc);
const pcs = [pc], T = [];

// ---- the Host Link
const saved = [];
let offer = [{ name: 'payload.bin', data: new Uint8Array(payload), mtime: 626140800 }];
const host = new HostLink({ pickFiles: async () => offer, saveFile: (f) => saved.push(f) });
ex.register('555-0100', host);
T.push((now) => host.tick(now));

// ---- a scripted ANSI test board
const ansiScreen =
  '\x1b[0m\x1b[2J\x1b[H' +
  '\x1b[1;33mYELLOW\x1b[0m \x1b[44;37mWHITE-ON-BLUE\x1b[0m \x1b[5;31mBLINK-RED\x1b[0m \x1b[7mREVERSE\x1b[0m\r\n' +
  '\x1b[10;20HAT-10-20\x1b[s\x1b[3;1Hline3\x1b[uAFTER\r\n' +
  'ERASE-ME-PLEASE\x1b[1G\x1b[K' + 'kept\r\n' +
  'X'.repeat(80) + 'NEXT-LINE\r\n' +
  '\x1b[6n';
let ansiLeg = null, dsr = '';
ex.register('555-0199', {
  incoming(leg) {
    ansiLeg = leg;
    leg.owner = { onData: (b) => { dsr += String.fromCharCode(...b); }, onHangup: () => { ansiLeg = null; } };
    setTimeoutEmu(2000, () => { leg.answer(2400); setTimeoutEmu(6000, () => leg.send(Array.from(Buffer.from(ansiScreen, 'latin1')))); });
    return true;
  },
});
const timers = [];
function setTimeoutEmu(ms, fn) { timers.push({ at: pc.m.timeMs() + ms, fn }); }
T.push((now) => { for (let i = timers.length - 1; i >= 0; i--) if (now >= timers[i].at) { const t = timers.splice(i, 1)[0]; t.fn(); } });

const run = (ms, pred) => runAll(pcs, T, ms, 4, pred);
let mark = 0;
const plain = () => rec.text.replace(/\x1b\[[0-9;?]*[A-Za-z]/g, '');
async function expect(text, ms = 60000) {
  const ok = await run(ms, () => plain().indexOf(text, mark) >= 0);
  if (ok) mark = plain().indexOf(text, mark) + text.length;
  return ok;
}
async function say(keys, pause = 400) { pc.type(keys); await run(pause); await run(20000, () => pc.m.typingDone()); }
const shot = (n) => pc.shot(`term-${n}.png`);
const cell = (x, y) => { const a = 0xB8000 + (y * 80 + x) * 2; return [String.fromCharCode(pc.m.cpu.m8[a]), pc.m.cpu.m8[a + 1]]; };

check(await run(30000, () => has(pc, 'C:\\>')), 'boots to C:\\>');
pc.type('TERM\r');
check(await run(8000, () => has(pc, 'Alt-Z for Help')), 'TERM (C:\\DOS\\TERM.BAT -> C:\\TERM\\TERM.EXE) starts');
check(has(pc, 'ANSI-BBS') && has(pc, '115200 N81') && has(pc, 'FDX') && has(pc, 'Offline'), 'status line: Alt-Z for Help | ANSI-BBS | 115200 N81 (locked port) | FDX | ... | Offline');
await shot('start');

// ---- help, setup
await say('{ALT+Z}', 600);
check(has(pc, 'COMMAND MENU') && has(pc, 'Dialing directory') && has(pc, 'Hang up'), 'Alt-Z: help screen');
await shot('help');
await say(' ', 400);
await say('{ALT+S}', 600);
check(has(pc, 'SETUP') && has(pc, 'COM2') && has(pc, 'AT&C1&D2') && has(pc, 'ZMODEM auto-download'), 'Alt-S: setup (port COM2, init string, ZMODEM options)');
await shot('setup');
await say('{ESC}', 600);
check(readFile(pc, 'TERM\\TERM.CFG')?.toString().includes('INIT=AT&C1&D2'), 'setup saved to C:\\TERM\\TERM.CFG');

// ---- BUSY and redial
await say('{ALT+D}', 800);
await say('{DOWN}{DOWN}', 300);
await say('{ENTER}', 100);
check(await run(40000, () => has(pc, 'Last: BUSY')), 'Jenny (867-5309): BUSY');
check(await run(30000, () => /Attempt:\s+2/.test(screen(pc))), 'redials on BUSY (attempt 2)');
await shot('busy-redial');
await say('{ESC}', 1500);
check(!has(pc, 'DIALING') && has(pc, 'Offline'), 'Esc cancels dialling');

// ---- the ANSI test board (manual dial)
await say('{ALT+D}', 800);
await say('M', 400);
await say('555-0199\r', 100);
check(await run(40000, () => has(pc, 'NEXT-LINE')), 'manual dial 555-0199: connected, ANSI screen received');
await run(2000);
const [c1, a1] = cell(0, 0), [c2, a2] = cell(7, 0), [c3, a3] = cell(21, 0), [c4, a4] = cell(31, 0);
check(c1 === 'Y' && a1 === 0x0E, `ESC[1;33m -> yellow (attr ${a1.toString(16)})`);
check(c2 === 'W' && a2 === 0x17, `ESC[44;37m -> white on blue (attr ${a2.toString(16)})`);
check(c3 === 'B' && a3 === 0x84, `ESC[5;31m -> blinking red (attr ${a3.toString(16)})`);
check(c4 === 'R' && a4 === 0x70, `ESC[7m -> reverse (attr ${a4.toString(16)})`);
check(screen(pc).split('\n')[9].startsWith(' '.repeat(19) + 'AT-10-20AFTER'), 'ESC[10;20H cursor positioning and ESC[s / ESC[u');
check(screen(pc).split('\n')[2].startsWith('line3'), 'ESC[3;1H');
check(/^kept\s*$/.test(screen(pc).split('\n')[10]), 'ESC[1G + ESC[K erase to end of line');
check(screen(pc).split('\n')[11] === 'X'.repeat(80) && screen(pc).split('\n')[12].startsWith('NEXT-LINE'), '80 columns + CR LF: one line, no blank line (deferred wrap)');
check(/\x1b\[\d+;1R/.test(dsr), `ESC[6n answered with the cursor position (${JSON.stringify(dsr)})`);
check(has(pc, 'Online 00:0'), 'status line shows Online hh:mm:ss');
// midnight: the BIOS tick count (40:6C) restarts at 0; the online timer must carry on
{
  const m8 = pc.machine.cpu.m8, t = 0x1800B0 - 18 * 3;          // 3 s before midnight
  m8[0x46C] = t & 0xFF; m8[0x46D] = (t >> 8) & 0xFF; m8[0x46E] = (t >> 16) & 0xFF; m8[0x46F] = 0;
  await run(1500);
  const before = (screen(pc).match(/Online (\d\d):(\d\d):(\d\d)/) || []).slice(1).map(Number);
  await run(5000);
  const tk = m8[0x46C] | m8[0x46D] << 8 | m8[0x46E] << 16;
  const after = (screen(pc).match(/Online (\d\d):(\d\d):(\d\d)/) || []).slice(1).map(Number);
  const secs = (v) => v.length === 3 ? v[0] * 3600 + v[1] * 60 + v[2] : -1;
  check(tk < 18 * 10 && secs(after) - secs(before) >= 4 && secs(after) - secs(before) <= 6,
    `online timer keeps counting across midnight (${before.join(':')} -> ${after.join(':')}, ticks ${tk})`);
}
await shot('ansi');
// scrollback: push the screen up, then look back
ansiLeg.send(Array.from(Buffer.from(Array.from({ length: 40 }, (_, i) => `scroll line ${i}\r\n`).join(''), 'latin1')));
await run(6000);
await say('{ALT+B}', 600);
await say('{HOME}', 400);
check(has(pc, 'SCROLLBACK') && has(pc, 'YELLOW') && has(pc, 'AT-10-20'), 'Alt-B scrollback shows what scrolled away');
await shot('scrollback');
await say('{ESC}', 400);
check(has(pc, 'scroll line 39') && !has(pc, 'SCROLLBACK'), 'Esc leaves the scrollback');
// capture
await say('{ALT+L}', 600);
await say('\r', 400);
check(has(pc, 'LOG ON'), 'Alt-L: capture on (status line LOG ON)');
ansiLeg.send(Array.from(Buffer.from('captured text 12345\r\n', 'latin1')));
await run(2000);
await say('{ALT+L}', 1800);
check((readFile(pc, 'TERM\\TERM.CAP')?.toString('latin1') || '').includes('captured text 12345'), 'capture file C:\\TERM\\TERM.CAP has the text');
await say('{ALT+H}', 3000);
check(await run(10000, () => has(pc, 'Offline')), 'Alt-H hangs up');

// ---- Host Link: ZMODEM download (auto), aborted download + resume, upload
await say('{ALT+D}', 800);
await say('2', 100);
mark = plain().length;
check(await expect('Your choice', 40000) && has(pc, 'ARM-DOS HOST LINK'), 'dials the Host Link (555-0100): banner and menu');
await shot('hostlink');
await say('D', 300);
check(await run(30000, () => has(pc, 'ZMODEM DOWNLOAD')), 'Host Link [D]: TERM auto-starts the ZMODEM download');
await run(8000);
check(has(pc, 'PAYLOAD.BIN') || has(pc, 'payload.bin'), 'transfer window shows the file name');
await shot('download-progress');
await say('{ESC}', 500);                    // abort part way
check(await run(20000, () => !has(pc, 'ZMODEM DOWNLOAD')), 'Esc aborts the download');
const partial = readFile(pc, 'TERM\\DOWNLOAD\\PAYLOAD.BIN');
check(partial && partial.length > 0 && partial.length < payload.length, `partial file kept (${partial ? partial.length : 0} bytes)`);
await expect('Your choice', 30000);
await say('D', 300);
check(await run(30000, () => has(pc, 'ZMODEM DOWNLOAD')), 'download again');
check(await run(30000, () => has(pc, 'Resuming at')), 'crash recovery: "Resuming at ..." (continues the partial file)');
check(await run(120000, () => !has(pc, 'ZMODEM DOWNLOAD')), 'download finishes');
const full = readFile(pc, 'TERM\\DOWNLOAD\\PAYLOAD.BIN');
check(full && Buffer.compare(full, payload) === 0, `PAYLOAD.BIN intact after resume (${full ? full.length : 0} bytes)`);
check(host.sent.includes('PAYLOAD.BIN'), 'Host Link reports the file sent');
await expect('Your choice', 30000);
await say('U', 300);
await run(4000);
await say('{PGUP}', 500);
await say('Z', 400);
await say('C:\\UP.TXT\r', 400);
check(await run(60000, () => saved.length > 0), 'upload to the Host Link with ZMODEM');
check(saved[0] && Buffer.compare(Buffer.from(saved[0].data), upload) === 0 && /UP\.TXT/i.test(saved[0].name), `Host Link got UP.TXT intact (${saved[0] ? saved[0].data.length : 0} bytes)`);
await shot('upload-done');
await expect('Your choice', 30000);
await say('G', 300);
check(await run(20000, () => has(pc, 'Offline') && rec.text.includes('NO CARRIER')), 'Host Link goodbye: NO CARRIER, TERM offline');

// ---- directory bookkeeping, exit
await say('{ALT+D}', 800);
const dir = readFile(pc, 'TERM\\TERM.DIR')?.toString('latin1') || '';
// (11/05: the midnight check above let the clock roll over into the next day)
check(/ARM-DOS Host Link\|555-0100\|2400\|11\/0[45]\/89\|1/.test(dir), 'dialing directory records the last call date and count');
await shot('directory');
await say('{ESC}', 400);
await say('{ALT+X}', 500);
check(has(pc, 'Exit to DOS'), 'Alt-X asks "Exit to DOS (Y/N)?"');
await say('Y', 1000);
check(await run(5000, () => has(pc, 'C:\\>')), 'back at the DOS prompt');
console.log(failed() ? `${failed()} FAILED` : 'all passed');
process.exit(failed() ? 1 : 0);
