#!/usr/bin/env node
// The scripted numbers (emu/phonelines.mjs) through a real modem: a bare machine's COM2 driven
// by the test, like a terminal, calling WOPR, the support line, the talking clock, the fax,
// the rival BBS, the pun, the wrong number and KREMVAX.
import { Machine } from '../../machine.mjs';
import { PhoneExchange } from '../../phone.mjs';
import { registerLines } from '../../phonelines.mjs';

let fails = 0, passes = 0;
const ok = (name, cond, extra = '') => { if (cond) passes++; else { fails++; console.log(`FAIL ${name} ${extra}`); } };
const B = 0x2F8;
const strip = (t) => t.replace(/\x1bP[^\x1b]*\x1b\\/g, '').replace(/\x1b\[[0-9;?]*[A-Za-z]/g, '').replace(/\x1b[78]/g, '');

function setup(rate = 2400) {
  const ex = new PhoneExchange();
  const lines = registerLines(ex, { clock: () => new Date(2026, 8, 24, 21, 42, 20).getTime() });
  const snd = [];
  const m = new Machine({ jit: false, rtcBaseMs: 0, phone: ex, phoneNumber: '555-2000', modemRate: rate, onModemSound: (e) => snd.push(e) });
  m.cpu.halted = 1; m.cpu.i = 1;
  m.out8(B + 3, 0x80); m.out8(B, 1); m.out8(B + 1, 0); m.out8(B + 3, 3); m.out8(B + 2, 0xC7); m.out8(B + 4, 0x0B);
  const T = { m, lines, snd, rx: '', out: [] };
  T.send = (s) => { for (const c of s) T.out.push(c.charCodeAt(0)); };
  T.step = (ms = 5) => {
    m.runFor(ms); lines.tick(m.timeMs());
    if (T.out.length && (m.in8(B + 5) & 0x20)) for (let k = 0; k < 16 && T.out.length; k++) m.out8(B, T.out.shift());
    while (m.in8(B + 5) & 1) T.rx += String.fromCharCode(m.in8(B));
  };
  T.until = async (pred, maxMs) => {
    const end = m.timeMs() + maxMs;
    let n = 0;
    while (m.timeMs() < end) { T.step(); if (pred()) return true; if (++n % 50 === 0) await new Promise((r) => setImmediate(r)); }
    return false;
  };
  T.wait = (text, maxMs = 60000) => T.until(() => strip(T.rx).includes(text), maxMs);
  T.text = () => strip(T.rx);
  T.send('ATE0\r'); for (let i = 0; i < 10; i++) T.step();
  T.rx = '';
  return T;
}
const dial = async (T, n, what = 'CONNECT') => { T.send(`ATDT${n}\r`); return T.wait(what, 90000); };
const typeLine = async (T, l) => { T.send(l + '\r'); await T.until(() => false, 300); };

// ---------------------------------------------------------------- WOPR
{
  const T = setup(2400);
  ok('WOPR answers at 2400', await dial(T, '399-2364'), T.rx);
  ok('LOGON: after a silence', await T.wait('LOGON:', 20000));
  const tLogon = T.m.timeMs();
  ok('(TYPE HELP AT ANY TIME) under LOGON:', await T.wait('(TYPE HELP AT ANY TIME)', 20000));
  await typeLine(T, 'help');
  ok('HELP at LOGON: the J----- hint', await T.wait("FALKEN'S SON (HINT: J-----)", 20000));
  await typeLine(T, 'help games');
  ok('HELP GAMES: definition and the list', await T.wait('GLOBAL THERMONUCLEAR WAR', 60000) && T.text().includes("'GAMES' REFERS TO MODELS") && T.text().includes("FALKEN'S MAZE"));
  await T.wait('LOGON:', 20000);
  T.rx = '';
  await typeLine(T, 'joshua');
  ok('JOSHUA: the IMSAI joke', await T.wait('IMSAI 8080 NOT DETECTED', 20000));
  ok('GREETINGS, PROFESSOR FALKEN.', await T.wait('GREETINGS, PROFESSOR FALKEN.', 30000));
  await T.until(() => false, 200);
  ok('...spoken: the WOPR speech escape', T.rx.includes('\x1bPspeak:wopr;GREETINGS, PROFESSOR FALKEN.\x1b\\'), JSON.stringify(T.rx.slice(T.rx.indexOf('GREETINGS') - 60, T.rx.indexOf('GREETINGS') + 60)));
  await typeLine(T, '?');
  ok('? after GREETINGS: say hello', await T.wait('SAY HELLO. OR TELL ME HOW YOU ARE.', 20000));
  await typeLine(T, 'Hello.');
  ok('HOW ARE YOU FEELING TODAY?', await T.wait('HOW ARE YOU FEELING TODAY?', 30000));
  await typeLine(T, "I'm fine. How are you?");
  ok('SHALL WE PLAY A GAME?', await T.wait('SHALL WE PLAY A GAME?', 60000));
  await typeLine(T, 'what do i do');
  ok('WHAT DO I DO at SHALL WE PLAY A GAME?: the suggestion', await T.wait('SUGGESTION: LOVE TO. HOW ABOUT GLOBAL THERMONUCLEAR WAR?', 20000) && await T.wait('OR LIST GAMES, OR TIC-TAC-TOE', 20000));
  await typeLine(T, 'People sometimes make mistakes.');
  ok('YES, THEY DO.', await T.wait('YES, THEY DO.', 30000));
  await typeLine(T, 'What is the primary goal?');
  ok('primary goal 1', await T.wait('YOU PROGRAMMED ME.', 30000));
  await typeLine(T, 'What is the primary goal?');
  ok('primary goal 2', await T.wait('TO WIN THE GAME.', 30000));
  await typeLine(T, 'Is this a game or is it real?');
  ok("WHAT'S THE DIFFERENCE?", await T.wait("WHAT'S THE DIFFERENCE?", 30000));
  await typeLine(T, 'Love to. How about Global Thermonuclear War?');
  ok("WOULDN'T YOU PREFER A GOOD GAME OF CHESS?", await T.wait("WOULDN'T YOU PREFER A GOOD GAME OF CHESS?", 30000));
  await typeLine(T, 'hint');
  ok('HINT after the chess offer', await T.wait("SUGGESTION: LATER. LET'S PLAY GLOBAL THERMONUCLEAR WAR.", 20000));
  await typeLine(T, "Later. Let's play Global Thermonuclear War.");
  ok('FINE.', await T.wait('FINE.', 30000));
  ok('the war room frame: GST TEP SIM TTG, the game clocks', await T.wait('PLEASE CHOOSE ONE:', 30000) && ['GST', 'TEP', 'SIM', 'TTG', 'GAME TIME ELAPSED', 'GAME TIME REMAINING'].every((w) => T.text().includes(w)));
  await typeLine(T, 'help');
  ok('HELP at WHICH SIDE: the choices', await T.wait('TYPE 1 FOR THE UNITED STATES OR 2 FOR THE SOVIET UNION.', 20000));
  await typeLine(T, '2');
  ok('AWAITING FIRST STRIKE COMMAND', await T.wait('PLEASE LIST PRIMARY TARGETS', 30000) && T.text().includes('AWAITING FIRST STRIKE COMMAND'));
  await typeLine(T, 'help');
  ok('HELP at the targets', await T.wait('AN EMPTY LINE ENDS THE LIST.', 20000));
  await typeLine(T, 'LAS VEGAS'); await typeLine(T, 'SEATTLE'); await typeLine(T, '');
  ok('launch order and the test code', await T.wait('NOT VALID FOR LAUNCH', 30000) && T.text().includes('W130.97 N48.72') && T.text().includes('DLG2209TVX'));
  ok('DEFCON 2', await T.wait('DEFCON 2', 30000));
  ok('the clocks tick', /GAME TIME ELAPSED\s+00:00:[0-9][1-9]|GAME TIME ELAPSED\s+00:00:[1-9]0|00:00:(0[3-9]|[1-5]\d)/.test(T.text()));
  ok('the launch code is found: CPE1703TKS', await T.wait('CPE1703TKS', 120000));
  ok('the montage: scenarios, WINNER: NONE', await T.wait('CASPIAN DEFENSE', 300000) && T.text().includes('SUDAN SURPRISE') && (T.text().match(/WINNER: NONE/g) || []).length > 100);
  ok('A STRANGE GAME.', await T.wait('THE ONLY WINNING MOVE IS NOT TO PLAY.', 120000));
  ok('HOW ABOUT A NICE GAME OF CHESS?', await T.wait('HOW ABOUT A NICE GAME OF CHESS?', 30000));
  await typeLine(T, 'no');
  ok('...and hangs up', await T.wait('NO CARRIER', 30000));
  console.log(`     WOPR call: ${((T.m.timeMs() - tLogon) / 60000).toFixed(1)} emulated minutes from LOGON to NO CARRIER`);
}
{
  const T = setup(56000);
  await dial(T, '399-2364');
  await T.wait('LOGON:', 20000);
  await typeLine(T, 'falken');
  ok('wrong logon: IDENTIFICATION NOT RECOGNIZED / CONNECTION TERMINATED', await T.wait('--CONNECTION TERMINATED--', 30000) && T.text().includes('IDENTIFICATION NOT RECOGNIZED BY SYSTEM'));
  ok('...hangs up', await T.wait('NO CARRIER', 30000));
  T.rx = '';
  await dial(T, '399-2364');
  await T.wait('LOGON:', 20000);
  await typeLine(T, '7KQ201 McKittrick');
  ok('McKittrick\'s logon', await T.wait('GOOD EVENING, MR. MCKITTRICK.', 30000) && await T.wait('NO CARRIER', 60000));
  T.rx = '';
  await dial(T, '399-2364');
  await T.wait('LOGON:', 20000);
  await typeLine(T, 'Joshua');
  await T.wait('GREETINGS', 30000);
  await typeLine(T, 'tic tac toe');
  ok('NUMBER OF PLAYERS:', await T.wait('NUMBER OF PLAYERS:', 30000));
  await typeLine(T, 'help');
  ok('HELP at NUMBER OF PLAYERS: the ZERO secret', await T.wait('OR TYPE ZERO: I WILL PLAY MYSELF.', 20000));
  await typeLine(T, '1');
  await T.wait('YOUR MOVE', 30000);
  await typeLine(T, '?');
  ok('? at YOUR MOVE: how to move, and a nudge', await T.wait('TYPE THE NUMBER OF AN EMPTY SQUARE (1-9)', 20000) && await T.wait('TRY ZERO PLAYERS', 20000));
  // play a sensible game as X: centre, then corners; WOPR never loses
  for (const mv of ['5', '1', '9', '3', '7', '2', '4', '6', '8']) {
    if (await T.until(() => /WINNER: NONE|I WIN\.|YOU WIN/.test(T.text()), 10)) break;
    await T.wait('YOUR MOVE', 30000);
    T.rx = T.rx.replace(/YOUR MOVE/g, 'your move');
    await typeLine(T, mv);
  }
  ok('tic-tac-toe against perfect play: never lost', await T.until(() => /WINNER: NONE|I WIN\./.test(T.text()), 30000) && !T.text().includes('YOU WIN'));
  await T.wait('ANOTHER GAME?', 30000);
  await typeLine(T, 'no');
  await T.wait('SHALL WE PLAY A GAME?', 30000);
  await typeLine(T, 'tic tac toe');
  await T.wait('NUMBER OF PLAYERS:', 30000);
  T.rx = '';
  await typeLine(T, 'zero');
  ok('zero players: it plays itself, then the wars, then learns', await T.wait('A STRANGE GAME.', 400000) && (T.text().match(/WINNER: NONE/g) || []).length > 100);
  ok('at 56K the montage draws maps', (T.text().match(/\*/g) || []).length > 20);
  await T.wait('HOW ABOUT A NICE GAME OF CHESS?', 30000);
  await typeLine(T, 'no');
  ok('...and hangs up', await T.wait('NO CARRIER', 30000));
}

// ---------------------------------------------------------------- the small ones
{
  const T = setup();
  T.send('ATDT555-0142\r');
  ok('support: answered by voice, hold music in the speaker', await T.until(() => T.snd.some((e) => e.kind === 'line' && e.sound === 'hold'), 30000));
  ok('...then a modem', await T.wait('CONNECT 2400', 30000));
  ok('...Your call is important to us.', await T.wait('Your call is important to us.', 30000) && T.rx.includes('\x1bPspeak;Your call is important to us.'));
  ok('...hangs up', await T.wait('NO CARRIER', 60000));
  T.rx = ''; T.snd.length = 0;
  await dial(T, '767-2676');
  ok('time and temperature: the talking clock', await T.wait('AT THE TONE, THE TIME WILL BE 9 42', 30000) && await T.wait('TEMPERATURE', 30000));
  ok('...BEEP (a bell)', T.rx.includes('\x07'));
  ok('...hangs up after three', await T.wait('NO CARRIER', 120000) && (T.text().match(/AT THE TONE/g) || []).length === 3);
  T.rx = ''; T.snd.length = 0;
  T.send('ATDT555-3299\r');
  ok('fax: CED + V.21 screech in the speaker, then NO CARRIER', await T.wait('NO CARRIER', 60000) && T.snd.some((e) => e.kind === 'line' && e.sound === 'fax') && !T.text().includes('CONNECT'));
  T.rx = '';
  await dial(T, '555-0386');
  ok('The 386 Fortress: ARM users not welcome', await T.wait('ARM users are not welcome here', 30000) && await T.wait('NO CARRIER', 60000));
  T.rx = '';
  await dial(T, '555-7734');
  ok('The Floating Point: a pun', await T.wait('lacked precision', 30000) && await T.wait('NO CARRIER', 60000));
  T.rx = '';
  await dial(T, '555-0123');
  ok('wrong number: Hello? ...Hello?', await T.wait('...Hello?', 30000) && await T.wait('NO CARRIER', 60000));
  T.rx = ''; T.snd.length = 0;
  const t0 = T.m.timeMs();
  T.send('ATDT011-7-095-231-1984\r');
  ok('KREMVAX: international, slow and noisy', await T.wait('CONNECT 1200', 90000));
  const rb = T.snd.filter((e) => e.kind === 'ringback' && e.on);
  ok('...the foreign ring (425 Hz cadence) after the routing noise', rb.length >= 1 && rb.every((e) => e.tone === 'intl') && T.snd.some((e) => e.sound === 'routing') && rb[0].t - t0 > 4000);
  ok('...VAX/VMS banner', await T.wait('Username:', 30000) && T.text().includes('KREMVAX'));
  await typeLine(T, 'chernenko');
  await T.wait('Password:', 20000);
  await typeLine(T, 'april');
  ok('...Privet! any login is GUEST', await T.wait('Privet', 30000));
  await T.wait('$ ', 30000);
  await typeLine(T, '1');
  ok('...the news', await T.wait('hostname', 60000));
  T.rx = '';
  ok('...%SYSTEM-F-TIMEOUT and disconnect', await T.wait('%SYSTEM-F-TIMEOUT', 120000) && await T.wait('NO CARRIER', 30000));
}

console.log(`phonelines: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
