// apps/term/tests/wopr.mjs - TERM and the talking numbers (emu/phonelines.mjs, docs/MODEM.md):
//  * the Alt-Z menu runs the Alt-command you press (Alt-D from the menu opens the directory)
//  * WOPR (399-2364) from the dialling directory: LOGON: JOSHUA, "GREETINGS, PROFESSOR FALKEN."
//    printed AND spoken - the ESC P speak:wopr;... ESC \ string makes TERM talk through the
//    Sound Blaster (the audio is captured and checked), nothing of the escape on the screen
//  * the war room (GST/TEP/SIM/TTG frame, game clocks, launch code search)
//  * Ctrl-Alt-Del in the middle of the call: the BIOS drops DTR, the modem hangs up, WOPR sees
//    the line go dead; after the reboot TERM starts offline and dials again (767-2676, the
//    talking clock, spoken in the default voice)
// Screenshots: build/term-test/wopr-*.png
import { makeDisk, startPC, runAll, has, screen, check, failed, keys, PhoneExchange } from './lib.mjs';
import { registerLines } from '../../../emu/phonelines.mjs';

const RATE = 44100;
const rtc = new Date(1989, 10, 4, 21, 30, 0).getTime();
const img = makeDisk('woprtest', {
  files: [{ src: 'build/TERM.EXE', dst: 'TERM\\TERM.EXE' }, { src: 'apps/term/data/TERM.DIR', dst: 'TERM\\TERM.DIR' },
          { src: 'apps/term/data/TERM.BAT', dst: 'DOS\\TERM.BAT' }],
  dirs: ['TERM', 'TERM\\DOWNLOAD'],
  autoexec: 'SET BLASTER=A220 I7 D1 H5 T6\n',
});
const ex = new PhoneExchange();
const lines = registerLines(ex, { clock: () => new Date(1989, 10, 4, 21, 42, 20).getTime() });
let total = 0;
const chunks = [];
const pc = await startPC(img, { rtcBaseMs: rtc, phone: ex, phoneNumber: '555-2000',
  audio: { rate: RATE, speaker: true, onAudio: (l) => { chunks.push({ at: total, l }); total += l.length; } } });
const pcs = [pc], T = [(now) => lines.tick(now)];
const run = (ms, pred) => runAll(pcs, T, ms, 4, pred);
async function say(k, pause = 400) { pc.type(k); await run(pause); await run(20000, () => pc.m.typingDone()); }
const shot = (n) => pc.shot(`wopr-${n}.png`);
/** RMS of the captured audio between two emulated times (ms) */
/** the loudest 100 ms (RMS) of the captured audio between two emulated times (ms) */
function peak(t0, t1) { let m = 0; for (let t = t0; t < t1; t += 100) m = Math.max(m, rms(t, t + 100)); return m; }
function rms(t0, t1) {
  pc.m.audio.pump();
  const a = Math.round(t0 * RATE / 1000), b = Math.round(t1 * RATE / 1000);
  let s = 0, n = 0;
  for (const c of chunks) {
    for (let i = Math.max(a, c.at); i < Math.min(b, c.at + c.l.length); i++) { const v = c.l[i - c.at]; s += v * v; n++; }
  }
  return n ? Math.sqrt(s / n) : 0;
}
const wopr = lines.byNumber('399-2364');

check(await run(30000, () => has(pc, 'C:\\>')), 'boots to C:\\>');
await say('TERM\r', 500);
check(await run(8000, () => has(pc, 'Alt-Z for Help')), 'TERM starts');
// ---- Alt-Z menu passes Alt-commands through
await say('{ALT+Z}', 600);
check(has(pc, 'COMMAND MENU') && has(pc, 'Alt-V') && has(pc, 'Voice'), 'Alt-Z: the command menu (with Alt-V Voice)');
await shot('altz');
await say('{ALT+D}', 800);
check(has(pc, 'DIALING DIRECTORY') && !has(pc, 'COMMAND MENU'), 'Alt-D pressed in the Alt-Z menu opens the directory at once');
check(has(pc, 'WOPR - Crystal Palace') && has(pc, 'Time and Temperature') && has(pc, 'KREMVAX'), 'the directory lists WOPR, the talking clock, KREMVAX');
await say('{ALT+S}', 800);
check(has(pc, 'SETUP') && !has(pc, 'DIALING DIRECTORY'), 'Alt-S pressed in the directory opens setup');
await say('{ESC}', 600);
await say('{ALT+D}', 800);
// ---- WOPR
await say('4', 300);
check(await run(60000, () => has(pc, 'LOGON:') && has(pc, '(TYPE HELP AT ANY TIME)')), 'WOPR (399-2364): CONNECT, silence, LOGON: (TYPE HELP AT ANY TIME)');
await say('JOSHUA\r', 400);
check(await run(30000, () => has(pc, 'IMSAI 8080 NOT DETECTED')), 'JOSHUA: the backdoor opens (IMSAI 8080 NOT DETECTED)');
const tGreet = pc.m.timeMs();
check(await run(30000, () => has(pc, 'GREETINGS, PROFESSOR FALKEN.')), 'GREETINGS, PROFESSOR FALKEN.');
await run(2500);
const tEnd = pc.m.timeMs();
const loud = peak(tGreet, tEnd), before = peak(tGreet - 3000, tGreet - 1500);
{ const { writeWav } = await import('../../sbtest/tests/audio.mjs'); pc.m.audio.pump();
  const a = Math.round(tGreet * RATE / 1000), b = Math.round(tEnd * RATE / 1000), L = new Float32Array(b - a);
  for (const c of chunks) for (let i = Math.max(a, c.at); i < Math.min(b, c.at + c.l.length); i++) L[i - a] = c.l[i - c.at];
  writeWav('build/term-test/wopr-greetings.wav', L, L); }
check(loud > 0.008 && loud > 5 * before, `...spoken through the Sound Blaster (RMS ${loud.toFixed(4)} vs ${before.toFixed(4)} before)`);
check(!/speak|wopr;/.test(screen(pc)), 'the speech escape itself never reaches the screen');
await shot('greetings');
await say('HELLO.\r');
check(await run(30000, () => has(pc, 'HOW ARE YOU FEELING TODAY?')), 'HOW ARE YOU FEELING TODAY?');
await say("I'M FINE. HOW ARE YOU?\r");
check(await run(60000, () => has(pc, 'SHALL WE PLAY A GAME?')), 'SHALL WE PLAY A GAME?');
await say('HELP\r');
check(await run(30000, () => has(pc, 'SUGGESTION: LOVE TO. HOW ABOUT GLOBAL THERMONUCLEAR WAR?')), 'HELP at SHALL WE PLAY A GAME?: the suggestion');
await say('LOVE TO. HOW ABOUT GLOBAL THERMONUCLEAR WAR?\r');
check(await run(30000, () => has(pc, "WOULDN'T YOU PREFER A GOOD GAME OF CHESS?")), "WOULDN'T YOU PREFER A GOOD GAME OF CHESS?");
await say("LATER. LET'S PLAY GLOBAL THERMONUCLEAR WAR.\r");
check(await run(30000, () => has(pc, 'PLEASE CHOOSE ONE:')), 'FINE. - the war room');
check(['GST', 'TEP', 'SIM', 'TTG', 'GAME TIME ELAPSED', 'GAME TIME REMAINING'].every((w) => has(pc, w)), 'war room frame: GST TEP SIM TTG, GAME TIME ELAPSED / REMAINING');
await say('2\r');
check(await run(30000, () => has(pc, 'PLEASE LIST PRIMARY TARGETS')), 'AWAITING FIRST STRIKE COMMAND / PLEASE LIST PRIMARY TARGETS');
await say('LAS VEGAS\r'); await say('SEATTLE\r'); await say('\r');
check(await run(60000, () => has(pc, 'SEARCHING FOR LAUNCH CODE')), 'launch order, DEFCON, SEARCHING FOR LAUNCH CODE');
await run(4000);
await shot('warroom');
// ---- Ctrl-Alt-Del in the middle of the call
check(!!wopr.leg && pc.m.modem.carrier, 'still on the line with WOPR');
await say('{CTRL+ALT+DEL}', 300);
check(await run(5000, () => !wopr.leg && !pc.m.modem.hook && !pc.m.modem.carrier), 'Ctrl-Alt-Del: the BIOS drops DTR, the modem hangs up, WOPR sees the line go dead');
check(await run(40000, () => has(pc, 'C:\\>')), 'reboots to C:\\>');
await say('TERM\r', 500);
check(await run(8000, () => has(pc, 'Alt-Z for Help') && has(pc, 'Offline')), 'TERM after the reboot: Offline');
await say('{ALT+D}', 800);
await say('6', 300);
check(await run(60000, () => has(pc, 'AT THE TONE, THE TIME WILL BE 9 42')), 'dials again: the talking clock (767-2676)');
const tClock = pc.m.timeMs();
await run(4000);
check(peak(tClock - 2000, pc.m.timeMs()) > 0.008, '...spoken in the default voice');
await shot('clock');
check(await run(90000, () => has(pc, 'Offline')), 'the clock hangs up; TERM offline');
await say('{ALT+X}', 500); await say('Y', 1000);
check(await run(5000, () => has(pc, 'C:\\>')), 'Alt-X back to DOS');
console.log(failed() ? `${failed()} FAILED` : 'all passed');
process.exit(failed() ? 1 : 0);
