// apps/term/tests/script.mjs - TERM's script language against The ARM Pit BBS:
// the visitor's ARM-PC runs C:\TERM\TERM.EXE /S:GETKEEN.SCR from C:\GAMES\KEEN
// (apps/keen/data/GETKEEN.SCR), which dials 555-1989 at 14400, logs on as the
// guest, downloads KEENDRMS.ZIP with ZMODEM, logs off and exits with
// errorlevel 0. Real modem pacing (no lineRate / baudOverride). Also: a BUSY
// number with DIALATTEMPTS 1 -> errorlevel 2.
//
//   node apps/term/tests/script.mjs          the 14400 run (+ the BUSY check)
//   node apps/term/tests/script.mjs 2400     the same script without &B14400: a 2400 bps call
import fs from 'node:fs';
import path from 'node:path';
import { makeDisk, startPC, runAll, has, check, failed, readFile, tapCom2, PhoneExchange, B, ROOT } from './lib.mjs';

const slow = process.argv.includes('2400');
const rtc = new Date(1992, 9, 24, 20, 0, 0).getTime();
let scr = fs.readFileSync(path.join(ROOT, 'apps/keen/data/GETKEEN.SCR'), 'latin1');
if (slow) scr = scr.replace('AT&C1&D2&B14400', 'AT&C1&D2').replace('Connecting at 14400 to save you time...', 'Connecting at 2400 (the scenic route)...');
const zip = fs.readFileSync(path.join(ROOT, '3rdparty/keen/KEENDRMS.ZIP'));
const run = `@ECHO OFF
CD \\GAMES\\KEEN
C:\\TERM\\TERM.EXE /S:%1
IF ERRORLEVEL 3 GOTO E3
IF ERRORLEVEL 2 GOTO E2
IF ERRORLEVEL 1 GOTO E1
ECHO RESULT=0
GOTO END
:E3
ECHO RESULT=3
GOTO END
:E2
ECHO RESULT=2
GOTO END
:E1
ECHO RESULT=1
:END
`;
const busy = `; dial Jenny, once
        SET DIALATTEMPTS 1
        DIAL "867-5309" "Jenny"
        IF FAILURE EXIT 2
        EXIT 0
`;
const img = makeDisk('scriptv', {
  files: [{ src: 'build/TERM.EXE', dst: 'TERM\\TERM.EXE' }, { src: 'apps/term/data/TERM.DIR', dst: 'TERM\\TERM.DIR' }],
  texts: { 'GAMES\\KEEN\\GETKEEN.SCR': scr, 'GAMES\\KEEN\\BUSY.SCR': busy, 'RUN.BAT': run },
  dirs: ['TERM', 'TERM\\DOWNLOAD', 'GAMES', 'GAMES\\KEEN'],
});
const ex = new PhoneExchange();
const v = await startPC(img, { rtcBaseMs: rtc, phone: ex, phoneNumber: '555-2000' });
const bbs = await startPC(new Uint8Array(fs.readFileSync(B('bbs.img'))), { rtcBaseMs: rtc, phone: ex, phoneNumber: '555-1989' });
const pcs = [v, bbs], T = [];
const rec = tapCom2(v);
const shot = (pc, name) => pc.shot(`script-${name}${slow ? '-2400' : ''}.png`);
const wait = (pred, ms) => runAll(pcs, T, ms, 4, pred);

check(await wait(() => has(v, 'C:\\>') && has(bbs, 'Waiting for call'), 30000), 'both machines booted; BBS waiting for call');

if (!slow) {
  v.type('\\RUN BUSY\r');
  check(await wait(() => has(v, 'Script BUSY'), 8000), 'TERM /S:BUSY.SCR: status line shows the script');
  check(await wait(() => has(v, 'RESULT='), 90000) && has(v, 'RESULT=2'), 'BUSY number, DIALATTEMPTS 1: IF FAILURE EXIT 2 -> errorlevel 2');
  v.type('CLS\r'); await wait(() => false, 1500);
}

const t0 = v.m.timeMs();
v.type('\\RUN GETKEEN\r');
check(await wait(() => has(v, 'Script GETKEEN'), 8000), 'status line: "Script GETKEEN"');
check(await wait(() => has(v, 'Connecting at'), 5000), 'the script\'s MESSAGE lines explain the call');
check(await wait(() => has(v, 'DIALING') && has(v, 'ATDT5551989'), 20000), 'dialing box: ATDT5551989');
check(has(v, slow ? 'AT&C1&D2' : 'AT&C1&D2&B14400'), `dialing box shows the init string${slow ? '' : ' with &B14400'}`);
await shot(v, 'dialing');
check(await wait(() => rec.text.includes('CONNECT'), 40000), 'modem: CONNECT');
const cm = rec.text.match(/CONNECT (\d+)/);
check(cm && cm[1] === (slow ? '2400' : '14400'), `line rate: CONNECT ${cm ? cm[1] : '?'}`);
check(await wait(() => has(v, 'FIRST name?'), 60000), 'BBS logon screen: "What is your FIRST name?"');
check(has(v, 'GUEST'), 'the logon screen tells visitors about the GUEST account');
await shot(v, 'logon');
check(await wait(() => has(v, 'caller number'), 30000), 'logged on as the guest (welcome screen)');
check(await wait(() => has(v, 'File Menu') && has(v, 'Games'), 60000), 'file menu, area 2 (Games)');
check(await wait(() => has(v, 'ZMODEM DOWNLOAD'), 60000), 'TERM\'s ZMODEM transfer window');
await wait(() => false, 8000);
check(has(v, 'KEENDRMS.ZIP') && has(v, 'CPS:'), 'transfer window: KEENDRMS.ZIP, CPS');
await shot(v, 'transfer');
const tx0 = v.m.timeMs();
check(await wait(() => has(v, 'Got KEENDRMS.ZIP'), slow ? 2400000 : 600000), 'download complete ("Got KEENDRMS.ZIP!")');
const txs = (v.m.timeMs() - tx0) / 1000;
check(await wait(() => has(v, 'RESULT='), 120000), 'the script logs off and TERM exits');
check(has(v, 'RESULT=0'), 'TERM exit code (errorlevel) 0');
const total = (v.m.timeMs() - t0) / 1000;
await shot(v, 'done');
check(has(bbs, 'Waiting for call'), 'BBS back to "Waiting for call..."');
const got = readFile(v, 'GAMES\\KEEN\\KEENDRMS.ZIP');
check(got && Buffer.compare(got, zip) === 0, `KEENDRMS.ZIP on the visitor's C:\\GAMES\\KEEN is byte-identical (${got ? got.length : 0} of ${zip.length} bytes)`);
console.log(`     emulated time at ${slow ? 2400 : 14400} bps: whole script ${total.toFixed(1)} s (${(total / 60).toFixed(1)} min), transfer ~${txs.toFixed(1)} s`);
if (failed()) { console.log(`${failed()} check(s) FAILED`); process.exit(1); }
console.log('script test: all passed');
