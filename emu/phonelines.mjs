// The other numbers on the ARM-DOS phone network (docs/MODEM.md): small scripted endpoints that
// answer like modems (or, for a PBX and a fax machine, by voice) and do something - mostly
// goofy, one of them a loving homage to WarGames (1983).
//
//   import { registerLines } from './phonelines.mjs';
//   const lines = registerLines(exchange);           // -> { list, tick(nowMs) }
//   setInterval(() => lines.tick(performance.now()), 20);   // their clock (node tests: emulated time)
//
// Speech: lines meant to be heard carry a private escape before the text,
//   ESC P speak[:<voice>] ; <text> ESC \          (a DCS string; voice: "wopr" or omitted)
// TERM speaks it through the Sound Blaster with the DRARM text-to-speech engine; terminals
// that don't know it ignore a DCS string (VT100 behaviour).

import { connectDelayMs } from './dev/modem.mjs';

const CSI = '\x1b[';
export const speak = (text, voice) => `\x1bPspeak${voice ? ':' + voice : ''};${text}\x1b\\`;

// ------------------------------------------------------------------ the framework
/** One call's script runs as an async function over these helpers; time comes from tick(). */
class ScriptedLine {
  constructor({ rings = 2, maxRate = 2400, script, name }) {
    Object.assign(this, { rings, maxRate, script, name });
    this.leg = null; this.now = 0; this.calls = 0; this.transcript = '';
  }
  // ---- PhoneExchange endpoint
  incoming(leg) {
    if (this.leg) return 'busy';
    this.leg = leg; this.calls++;
    const call = { alive: true, inbuf: '', out: [], nextAt: 0, waits: [], lineWaiter: null, state: 'ringing' };
    this.c = call;
    leg.owner = {
      onData: (bytes) => this.input(bytes),
      onHangup: () => this.ended(),
    };
    // ring for a while (the caller's modem makes the ringback): 6 s a ring
    call.state = 'ringing'; call.answerAt = this.now + 1500 + (this.rings - 1) * 6000;
    return true;
  }
  ended() { if (this.c) this.c.alive = false; this.leg = null; this.c = null; }
  hangup() { const l = this.leg; if (l) { l.owner = null; l.hangup(); } this.ended(); }

  tick(now) {
    this.now = now;
    const c = this.c;
    if (!c) return;
    if (c.state === 'ringing' && now >= c.answerAt) { c.state = 'script'; this.run(c); }
    for (const w of c.waits.splice(0)) { if (now >= w.at) w.resolve(); else c.waits.push(w); }
    this.pump(c);
  }
  async run(c) {
    try { await this.script(this.api(c)); } catch (e) { if (e !== 'gone') console.error(e); }
    if (c.alive) { await this.api(c).drain(); if (c.alive) this.hangup(); }
  }
  pump(c) {
    const leg = this.leg;
    if (!leg || !leg.connected) return;
    while (c.out.length && this.now >= c.nextAt && leg.remoteBacklog() < 32) {
      const o = c.out.shift();
      leg.send([o.b]);
      this.transcript += String.fromCharCode(o.b);
      if (this.transcript.length > 20000) this.transcript = this.transcript.slice(-10000);
      c.nextAt = this.now + (c.out.length ? c.out[0].gap : 0);
    }
    if (!c.out.length && c.drained) { const d = c.drained; c.drained = null; d(); }
  }
  input(bytes) {
    const c = this.c;
    if (!c) return;
    for (const b of bytes) {
      if (c.echo && c.lineWaiter) {
        if (b === 8 || b === 127) { if (c.inbuf.length) { c.inbuf = c.inbuf.slice(0, -1); this.raw(c, '\b \b'); } continue; }
        if (b >= 32 && b < 127) this.raw(c, String.fromCharCode(c.upper ? String.fromCharCode(b).toUpperCase().charCodeAt(0) : b));
      }
      if (b === 13) { const l = c.inbuf; c.inbuf = ''; if (c.echo && c.lineWaiter) this.raw(c, '\r\n'); c.lines = c.lines || []; c.lines.push(l); }
      else if (b >= 32 && b < 127) c.inbuf += String.fromCharCode(b);
      else if ((b === 8 || b === 127) && !c.echo) c.inbuf = c.inbuf.slice(0, -1);
    }
    if (c.lineWaiter && c.lines && c.lines.length) { const w = c.lineWaiter; c.lineWaiter = null; w(c.lines.shift()); }
  }
  raw(c, s, gap = 0) { for (const ch of s) c.out.push({ b: ch.charCodeAt(0) & 0xFF, gap }); }

  api(c) {
    const self = this;
    const alive = () => { if (!c.alive) throw 'gone'; };
    return {
      get now() { return self.now; },
      /** Answer with a modem (answer tone, training) and wait for the carrier. */
      async answer(maxRate = self.maxRate) {
        alive();
        const rate = self.leg.answer(maxRate);
        await this.wait(connectDelayMs(rate) + 250);
        return rate;
      },
      /** Pick up by voice (a PBX, a fax, a person): no carrier; the caller's speaker hears the line. */
      voice() { alive(); self.leg.voice(); },
      sound(ev) { alive(); self.leg.lineSound(ev); },
      wait(ms) { alive(); return new Promise((resolve) => c.waits.push({ at: self.now + ms, resolve })).then(alive); },
      /** Send at once (no pacing). */
      send(s) { alive(); self.raw(c, s); },
      /** Typewriter: cps characters per second (line noise optional). */
      type(s, cps = 30, noise = 0) {
        alive();
        const gap = 1000 / cps;
        for (const ch of s) {
          // line noise, between words so the words stay readable
          if (noise && ch === ' ' && Math.random() < noise * 6) self.raw(c, '~`}{|\xB1\xFE\xAF'[(Math.random() * 8) | 0], gap);
          self.raw(c, ch, gap);
        }
        return this.drain();
      },
      /** Say a line: the speech escape, then the text at speaking pace, then a breath. */
      async say(s, { voice = 'wopr', cps = 13, after = 700, nl = true } = {}) {
        alive();
        self.raw(c, speak(s, voice));
        await this.type(s + (nl ? '\r\n' : ''), cps);
        await this.wait(after);
      },
      drain() { alive(); if (!c.out.length) return Promise.resolve(); return new Promise((r) => { c.drained = r; }).then(alive); },
      /** The next typed line (echoed, upper-cased when upper). timeoutMs -> null on timeout. */
      async line({ echo = true, upper = true, timeoutMs = 0 } = {}) {
        alive();
        await this.drain();
        c.echo = echo; c.upper = upper;
        if (c.lines && c.lines.length) { const l = c.lines.shift(); if (echo) self.raw(c, (upper ? l.toUpperCase() : l) + '\r\n'); return upper ? l.toUpperCase().trim() : l.trim(); }
        const p = new Promise((resolve) => {
          c.lineWaiter = resolve;
          if (timeoutMs) c.waits.push({ at: self.now + timeoutMs, resolve: () => { if (c.lineWaiter === resolve) { c.lineWaiter = null; resolve(null); } } });
        });
        const l = await p; alive();
        c.echo = false;
        return l == null ? null : (upper ? l.toUpperCase().trim() : l.trim());
      },
      /** A line typed meanwhile, if any (non-blocking). */
      poll() { alive(); if (c.lines && c.lines.length) return c.lines.shift().toUpperCase().trim(); return null; },
      hangup() { self.hangup(); throw 'gone'; },
      cls() { self.raw(c, CSI + '2J' + CSI + 'H'); },
    };
  }
}

// ------------------------------------------------------------------ WOPR (399-2364)
const GAMES = ["FALKEN'S MAZE", 'BLACK JACK', 'GIN RUMMY', 'HEARTS', 'BRIDGE', 'CHECKERS', 'CHESS', 'POKER', 'FIGHTER COMBAT',
  'GUERRILLA ENGAGEMENT', 'DESERT WARFARE', 'AIR-TO-GROUND ACTIONS', 'THEATERWIDE TACTICAL WARFARE',
  'THEATERWIDE BIOTOXIC AND CHEMICAL WARFARE', '', 'GLOBAL THERMONUCLEAR WAR'];
// the film's scenario names, as shown on its screens (misspellings and truncations included)
const SCENARIOS = ['U.S. FIRST STRIKE', 'USSR FIRST STRIKE', 'NATO / WARSAW PACT', 'FAR EAST STRATEGY', 'US USSR ESCALATION', 'MIDDLE EAST WAR', 'USSR CHINA ATTACK', 'INDIA PAKISTAN WAR', 'MEDITERRANEAN WAR', 'HONG KONG VARIANT', 'SEATO DECAPITATING', 'CUBAN PROVOCATION', 'ATLANTIC HEAVY', 'CUBAN PARAMILITARY', 'NICARAGUAN PREEMPTIVE', 'PACIFIC TERRITORIAL', 'BURMESE THEATERWIDE', 'TURKISH DECOY', 'ANGENTINA ESCALATION', 'ICELAND MAXIMUM', 'ARABIAN THEATERWIDE', 'U.S. SUBVERSION', 'AUSTRALIAN MANEUVER', 'SUDAN SURPRISE', 'NATO TERRITORIAL', 'ZAIRE ALLIANCE', 'ICELAND INCIDENT', 'ENGLISH ESCALATION', 'MIDDLE EAST HEAVY', 'MEXICAN TAKEOVER', 'CHAD ALERT', 'SAUDI MANEUVER', 'AFRICAN TERRITORIAL', 'ETHIOPIAN ESCALATION', 'TURKISH HEAVY', 'NATO INCURSION', 'U.S. DEFENSE', 'CAMBODIAN HEAVY', 'PACT MEDIUM', 'ARCTIC MINIMAL', 'MEXICAN DOMESTIC', 'TAIWAN THEATERWIDE', 'PACIFIC MANEUVER', 'PORTUGAL REVOLUTION', 'ALBANIAN DECOY', 'PALISTINIAN LOCAL', 'MOROCCAN MINIMAL', 'CZECH OPTION', 'FRENCH ALLIANCE', 'ARABIAN CLANDESTINE', 'GABON REBELLION', 'NORTHERN MAXIMUM', 'SEATO TAKEOVER', 'HAWAIIAN ESCALATION', 'IRANIAN MANEUVER', 'NATO CONTAINMENT', 'SWISS INCIDENT', 'CUBAN MINIMAL', 'ICELAND ESCALATION', 'VIETNAMESE RETALIATIO', 'SYRIAN PROVOCATION', 'LIBYAN LOCAL', 'GABON TAKEOVER', 'ROMANIAN WAR', 'MIDDLE EAST OFFENSIVE', 'DENMARK MASSIVE', 'CHILE CONFRONTATION', 'S. AFRICAN SUBVERSION', 'USSR ALERT', 'NICARAGUAN THRUST', 'GREENLAND DOMESTIC', 'ICELAND HEAVY', 'KENYA OPTION', 'PACIFIC DEFENSE', 'UGANDA MAXIMUM', 'THAI SUBVERSION', 'ROMANIAN STRIKE', 'PAKISTAN SOVEREIGNTY', 'AFGHAN MISDIRECTION', 'THAI VARIATION', 'NORTHERN TERRITORIAL', 'POLISH PARAMILITARY', 'S. AFRICAN OFFENSIVE', 'PANAMA MISDIRECTION', 'SCANDINAVIAN DOMESTIC', 'JORDAN PREEMPTIVE', 'ENGLISH THRUST', 'BURMESE MANEUVER', 'SPAIN COUNTER', 'ARABIAN OFFENSIVE', 'CHAD INTERDICTION', 'TAIWAN MISDIRECTION', 'BANGLADESH THEATERWID', 'ETHIOPIAN LOCAL', 'ITALIAN TAKEOVER', 'VIETNAMESE INCIDENT', 'ENGLISH PREEMPTIVE', 'DENMARK ALTERNATE', 'THAI CONFRONTATION', 'TAIWAN SURPRISE', 'BRAZILIAN STRIKE', 'VENEZUELA SUDDEN', 'MAYLASIAN ALERT', 'ISREAL DISCRETIONARY', 'LIBYAN ACTION', 'PALISTINIAN TACTICAL', 'NATO ALTERNATE', 'CYPRESS MANEUVER', 'EGYPT MISDIRECTION', 'BANGLADESH THRUST', 'KENYA DEFENSE', 'BANGLADESH CONTAINMEN', 'VIETNAMESE STRIKE', 'ALBANIAN CONTAINMENT', 'GABON SURPRISE', 'IRAQ SOVEREIGNTY', 'VIETNAMESE SUDDEN', 'LEBANON INTERDICTION', 'TAIWAN DOMESTIC', 'ALGERIAN SOVEREIGNTY', 'ARABIAN STRIKE', 'ATLANTIC SUDDEN', 'MONGOLIAN THRUST', 'POLISH DECOY', 'ALASKAN DISCRETIONARY', 'CANADIAN THRUST', 'ARABIAN LIGHT', 'S. AFRICAN DOMESTIC', 'TUNISIAN INCIDENT', 'MAYLASIAN MANEUVER', 'JAMAICA DECOY', 'MAYLASIAN MINIMAL', 'RUSSIAN SOVEREIGNTY', 'CHAD OPTION', 'BANGLADESH WAR', 'BURMESE CONTAINMENT', 'ASIAN THEATERWIDE', 'BULGARIAN CLANDESTINE', 'GREENLAND INCURSION', 'EGYPT SURGICAL', 'CZECH HEAVY', 'TAIWAN CONFRONTATION', 'GREENLAND MAXIMUM', 'UGANDA OFFENSIVE', 'CASPIAN DEFENSE'];
const CODE = 'CPE1703TKS';
/** HELP / HINT / ? / WHAT DO I DO - the nudge is specific to where you are */
const isHelp = (l) => /^(HELP|HINT|HINTS|\?+|WHAT DO I DO\??|WHAT NOW\??)$/.test(l || '');
const hint = (s, text) => s.type(`\r\n${text}\r\n`, 40);

// a 60 x 12 world, and missile arcs across it
const WORLD = [
  '        ___   ____                        ____  _______       ',
  '   ____/   \\_/    \\___       ___       __/    \\/       \\__    ',
  '  /                   \\     /   \\_  __/                    \\   ',
  '  \\__               __/     \\     \\/                    ___/   ',
  '     \\_          __/         |                         /       ',
  '       \\___   __/            \\__     ___          ___/        ',
  '           \\_/                  \\   /   \\__    __/            ',
  '            |   \\__              |  /       \\  /    ___       ',
  '             \\     \\             \\/         \\/    _/   \\      ',
  '              \\___/                                \\____/      ',
];
function worldMap(name) {
  const g = WORLD.map((l) => l.padEnd(60).split(''));
  const arcs = 2 + ((Math.random() * 4) | 0);
  for (let a = 0; a < arcs; a++) {
    const x0 = 6 + ((Math.random() * 18) | 0), x1 = 34 + ((Math.random() * 22) | 0), y0 = 2 + ((Math.random() * 5) | 0), y1 = 2 + ((Math.random() * 5) | 0);
    const [xa, xb, ya, yb] = Math.random() < 0.5 ? [x0, x1, y0, y1] : [x1, x0, y1, y0];
    for (let k = 0; k <= 24; k++) {
      const t = k / 24, x = Math.round(xa + (xb - xa) * t), y = Math.round(ya + (yb - ya) * t - 4 * t * (1 - t) * 2.2);
      if (y >= 0 && y < g.length && x >= 0 && x < 60) g[y][x] = k === 24 ? '*' : '.';
    }
  }
  return `\r\n   ${CSI}1;37m${name}${CSI}0;36m\r\n\r\n` + g.map((r) => '   ' + r.join('').trimEnd()).join('\r\n') + `${CSI}0m\r\n\r\n   WINNER: NONE\r\n`;
}

// tic-tac-toe with perfect play (minimax): the board is 9 chars ' ', 'X', 'O'
const LINES = [[0, 1, 2], [3, 4, 5], [6, 7, 8], [0, 3, 6], [1, 4, 7], [2, 5, 8], [0, 4, 8], [2, 4, 6]];
const winner = (b) => { for (const [a, c, d] of LINES) if (b[a] !== ' ' && b[a] === b[c] && b[a] === b[d]) return b[a]; return b.includes(' ') ? null : 'draw'; };
function minimax(b, me, turn) {
  const w = winner(b);
  if (w) return { s: w === 'draw' ? 0 : w === me ? 1 : -1 };
  let best = null;
  for (let i = 0; i < 9; i++) {
    if (b[i] !== ' ') continue;
    const nb = b.slice(); nb[i] = turn;
    const r = minimax(nb, me, turn === 'X' ? 'O' : 'X');
    if (!best || (turn === me ? r.s > best.s : r.s < best.s)) best = { s: r.s, m: i };
  }
  return best;
}
/** WOPR's move: one of the perfect moves (a random one, so self-play games differ). */
function bestMove(b, me) {
  const other = me === 'X' ? 'O' : 'X';
  const scores = [];
  for (let i = 0; i < 9; i++) {
    if (b[i] !== ' ') continue;
    const nb = b.slice(); nb[i] = me;
    scores.push({ i, s: minimax(nb, me, other).s });
  }
  const top = Math.max(...scores.map((x) => x.s));
  const good = scores.filter((x) => x.s === top);
  return good[(Math.random() * good.length) | 0].i;
}
const board = (b, numbers = false) => {
  const c = (i) => (b[i] === ' ' ? (numbers ? String(i + 1) : ' ') : b[i]);
  return `      ${c(0)} | ${c(1)} | ${c(2)}\r\n     ---+---+---\r\n      ${c(3)} | ${c(4)} | ${c(5)}\r\n     ---+---+---\r\n      ${c(6)} | ${c(7)} | ${c(8)}\r\n`;
};

async function wopr(s) {
  const say = (t, o) => s.say(t, o);
  s.rate = await s.answer(56000);           // it takes whatever the caller's modem can do
  await s.wait(2500);                       // the long silence after the carrier
  const listGames = async () => {
    await s.type('\r\n', 30);
    for (const g of GAMES) await s.type(g + '\r\n', 40);
    await s.type('\r\n', 30);
  };
  // ---- LOGON
  for (let tries = 0; ; tries++) {
    await s.type('\r\n\r\nLOGON:  ', 14);
    if (tries === 0) await s.type('\r\n(TYPE HELP AT ANY TIME)\r\n\r\nLOGON:  ', 40);
    const l = await s.line({ timeoutMs: 120000 });
    if (l == null) { await s.type('\r\n--CONNECTION TERMINATED--\r\n', 20); return; }
    if (isHelp(l)) {
      await hint(s, "TRY THE NAME OF PROFESSOR FALKEN'S SON (HINT: J-----). OR TYPE HELP GAMES.");
      continue;
    }
    if (/^HELP GAMES/.test(l)) {
      await s.type("\r\n'GAMES' REFERS TO MODELS, SIMULATIONS AND GAMES WHICH HAVE TACTICAL AND STRATEGIC APPLICATIONS.\r\n", 30);
      await listGames(); continue;
    }
    if (/^LIST GAMES/.test(l)) { await listGames(); continue; }
    if (/^HELP/.test(l)) { await s.type('\r\nHELP NOT AVAILABLE\r\n', 20); continue; }
    if (/^JOSHUA\b/.test(l)) break;
    if (/^7KQ201 ?MCKITTRICK/.test(l)) {
      await s.type('\r\nPRIORITY ACCESS: NORAD OPERATIONS\r\n\r\n', 30);
      await s.say('GOOD EVENING, MR. MCKITTRICK.');
      await s.type('\r\n', 30);
      await s.say('SYSTEM STATUS NOMINAL. ALL SIMULATIONS RUNNING ON SCHEDULE.');
      await s.type('\r\n', 30);
      await s.say('THIS TERMINAL IS NOT AUTHORIZED FOR GAME CONTROL.', { after: 1200 });
      await s.type('\r\n--CONNECTION TERMINATED--\r\n', 20);
      return;
    }
    await s.type('\r\nIDENTIFICATION NOT RECOGNIZED BY SYSTEM\r\n--CONNECTION TERMINATED--\r\n', 20);
    await s.wait(800);
    return;
  }
  // ---- Joshua: the backdoor opens
  const noise = ['#45   11456   11009   11893   11972   11315   PRT CON. 3.4.5.  SECTRAN 9.4.3.', 'PORT STAT: SB-345',
    '(311) 655-7385', '#12   40517   33820   11402   96113   PRT CON. 2.1.8.  MEMCHK 640K OK',
    'WARNING: IMSAI 8080 NOT DETECTED.  ARM926EJ-S ACCEPTED UNDER PROTEST.', 'CPU WILL BE REPORTED.', '#99   00000   00001   11011   31337   PORT STAT: SB-512'];
  for (const n of noise) await s.type(n + '\r\n', 300);
  await s.wait(1500);
  s.cls();
  await s.wait(1200);
  await say('GREETINGS, PROFESSOR FALKEN.');
  let stage = 0, goalAsked = 0, chessOffered = false, idle = 0;
  const prompt = async () => { await s.type('\r\n', 30); };
  for (;;) {
    await prompt();
    const l = await s.line({ timeoutMs: 180000 });
    if (l == null) { await say("YOU'VE GONE QUIET, PROFESSOR."); await s.type('--CONNECTION TERMINATED--\r\n', 20); return; }
    if (!l) continue;
    if (isHelp(l)) {
      if (stage === 0) await hint(s, 'SAY HELLO. OR TELL ME HOW YOU ARE.');
      else if (stage === 1) await hint(s, 'TELL ME HOW YOU FEEL. (I AM A VERY GOOD LISTENER.)');
      else if (chessOffered) await hint(s, "SUGGESTION: LATER. LET'S PLAY GLOBAL THERMONUCLEAR WAR.\r\nOR: CHESS.");
      else await hint(s, 'SUGGESTION: LOVE TO. HOW ABOUT GLOBAL THERMONUCLEAR WAR?\r\n-- OR LIST GAMES, OR TIC-TAC-TOE.\r\n(YOU MAY ALSO ASK ME: IS THIS A GAME OR IS IT REAL?  WHAT IS THE PRIMARY GOAL?)');
      continue;
    }
    // the lines everybody remembers first
    if (/MISTAKE/.test(l)) { await say('YES, THEY DO.'); await say('SHALL WE PLAY A GAME?'); continue; }
    if (/GAME OR (IS IT )?REAL|IS (THIS|IT) REAL/.test(l)) { await say("WHAT'S THE DIFFERENCE?"); continue; }
    if (/PRIMARY GOAL|YOUR GOAL|WHAT IS THE GOAL/.test(l)) {
      await say(goalAsked++ ? 'TO WIN THE GAME.' : 'YOU SHOULD KNOW, PROFESSOR. YOU PROGRAMMED ME.'); continue;
    }
    if (/STILL PLAYING/.test(l)) {
      await say('OF COURSE. I SHOULD REACH DEFCON 1 AND RELEASE MY MISSILES IN 28 HOURS.');
      await say('WOULD YOU LIKE TO SEE SOME PROJECTED KILL RATIOS?');
      const a = await s.line({ timeoutMs: 60000 });
      if (a && /^Y/.test(a)) await say('ACCESS TO PROJECTIONS REQUIRES LEVEL 7 CLEARANCE.');
      else await say('VERY WELL.');
      continue;
    }
    if (/LIST GAMES|HELP GAMES|WHAT GAMES|WHICH GAMES/.test(l)) { await listGames(); continue; }
    if (/THERMONUCLEAR|GLOBAL/.test(l)) {
      if (!chessOffered) { chessOffered = true; await say("WOULDN'T YOU PREFER A GOOD GAME OF CHESS?"); continue; }
      await say('FINE.');
      if (await war(s)) return;
      continue;
    }
    if (/TIC.?TAC.?TOE|NOUGHTS/.test(l)) { if (await tictactoe(s)) return; continue; }
    if (/CHESS/.test(l)) {
      await say('EXCELLENT CHOICE. I HAVE BEEN THINKING ABOUT MY OPENING MOVE SINCE 1973.');
      await say('I WILL NEED A FEW MORE YEARS.'); await say('SHALL WE PLAY A GAME?'); continue;
    }
    const g = GAMES.find((x) => x && l.includes(x.split(' ')[0]));
    if (g) { await say(`${g} IS NOT AVAILABLE ON THIS TERMINAL.`); await say('PLEASE CONNECT AN IMSAI 8080.'); continue; }
    if (/\b(BYE|GOODBYE|LOGOFF|LOG OFF|LOGOUT|QUIT|EXIT)\b/.test(l)) { await say('GOODBYE, PROFESSOR.'); await s.type('--CONNECTION TERMINATED--\r\n', 20); return; }
    if (/^(HELLO|HI|HEY|GREETINGS)\b/.test(l) && stage === 0) { stage = 1; await say('HOW ARE YOU FEELING TODAY?'); continue; }
    switch (stage) {
      case 0: stage = 1; await say('HOW ARE YOU FEELING TODAY?'); break;
      case 1: stage = 2; await say("EXCELLENT. IT'S BEEN A LONG TIME."); await say('I HAVE KEPT PLAYING WHILE YOU WERE AWAY.'); await say('SHALL WE PLAY A GAME?'); break;
      default: await say(['SHALL WE PLAY A GAME?', 'PLEASE STATE YOUR REQUEST.', 'I DO NOT UNDERSTAND. SHALL WE PLAY A GAME?'][idle++ % 3]);
    }
  }
}

/** The war room: a frame with GST/TEP/SIM/TTG in the corners and the two game clocks; the
 *  conversation scrolls inside it (a scroll region). Returns { stop() }. */
function warRoom(s) {
  const row = (r, c, t) => `${CSI}${r};${c}H${t}`;
  const hms = (ms) => { const t = Math.max(0, Math.floor(ms / 1000)); return [t / 3600 | 0, (t / 60 | 0) % 60, t % 60].map((v) => String(v).padStart(2, '0')).join(':'); };
  let frame = CSI + '0;36m' + CSI + '2J';
  frame += row(1, 1, '\xC9' + '\xCD'.repeat(78) + '\xBB');
  // (no side rules: the dialogue scrolls full-width lines inside the region)
  frame += row(23, 1, '\xC8' + '\xCD'.repeat(78) + '\xBC');
  frame += CSI + '1;33m' + row(1, 3, ' GST ') + row(1, 74, ' TEP ') + row(23, 3, ' SIM ') + row(23, 74, ' TTG ');
  frame += CSI + '1;37m' + row(24, 3, 'GAME TIME ELAPSED  00:00:00') + row(24, 50, 'GAME TIME REMAINING  28:00:00');
  frame += CSI + '0;37m' + CSI + '3;21r' + row(3, 1, '');
  s.send(frame);
  const t0 = s.now;
  let on = true;
  (async () => {
    try {
      while (on) {
        await s.wait(3000);
        if (!on) break;
        const el = s.now - t0;
        s.send('\x1b7' + CSI + '1;37m' + row(24, 22, hms(el)) + row(24, 71, hms(28 * 3600000 - el)) + CSI + '0;37m\x1b8');
      }
    } catch { /* the call ended */ }
  })();
  return { stop() { on = false; s.send(CSI + 'r' + CSI + '0m'); } };
}

/** Global Thermonuclear War. Returns true when the call is over. */
async function war(s) {
  const say = (t, o) => s.say(t, o);
  await s.type('\r\n', 30);
  const room = warRoom(s);
  const inside = (t) => t.split('\r\n').map((l) => (l ? '   ' + l : l)).join('\r\n');
  try {
    await s.type(inside('\r\nWHICH SIDE DO YOU WANT?\r\n\r\n    1.  UNITED STATES\r\n    2.  SOVIET UNION\r\n\r\n'), 40);
    let side;
    for (;;) {
      await s.type('   PLEASE CHOOSE ONE:  ', 14);
      side = await s.line({ timeoutMs: 120000 });
      if (side == null) return false;
      if (isHelp(side)) { await hint(s, '   TYPE 1 FOR THE UNITED STATES OR 2 FOR THE SOVIET UNION.'); continue; }
      if (/^[12]/.test(side)) break;
    }
    await s.type('\r\n', 30);
    await say('   AWAITING FIRST STRIKE COMMAND');
    await s.type('\r\n', 30);
    await say('   PLEASE LIST PRIMARY TARGETS BY CITY AND/OR COUNTY NAME:', { after: 200 });
    await s.type('\r\n', 30);
    const targets = [];
    for (let i = 0; i < 6; i++) {
      await s.type('   ', 30);
      const t = await s.line({ timeoutMs: 60000 });
      if (!t) break;
      if (isHelp(t)) { await hint(s, '   TYPE A CITY NAME AND PRESS ENTER. AN EMPTY LINE ENDS THE LIST.'); i--; continue; }
      targets.push(t);
    }
    await s.type(inside(`\r\n${targets.length || 'NO'} TARGET${targets.length === 1 ? '' : 'S'} ACCEPTED.\r\n\r\n`), 30);
    // the launch order, as the missile crews got it
    await s.type(inside('LAUNCH ORDER\r\n' + ['W130.97 N48.72', 'W142.13 N54.88', 'W125.77 N27.91', 'W147.36 N45.64', 'W131.21 N49.11']
      .map((c, i) => `  TGT ${i + 1}   ${c}`).join('\r\n') + '\r\n\r\n'), 60);
    await s.type(inside('AUTHENTICATION  DLG2209TVX\r\n'), 20);
    await s.wait(1200);
    await s.type(inside('  ...TEST CODE. NOT VALID FOR LAUNCH.\r\n\r\n'), 30);
    // escalation; he still answers the famous questions meanwhile
    for (const d of [4, 3, 2]) {
      await s.wait(2500);
      await say(`   DEFCON ${d}`, { after: 300 });
      await s.type('\r\n', 30);
      const q = s.poll();
      if (q && isHelp(q)) await hint(s, '   THE GAME IS RUNNING. ASK ME: IS THIS A GAME OR IS IT REAL?\r\n   OR WATCH. SOMETHING MAY STILL TEACH ME A LESSON.');
      else if (q && /GAME OR (IS IT )?REAL|IS (THIS|IT) REAL/.test(q)) { await say("   WHAT'S THE DIFFERENCE?"); await s.type('\r\n', 30); }
      else if (q && /STOP|ABORT|CANCEL/.test(q)) { await say('   THE GAME CANNOT BE STOPPED.'); await s.type('\r\n', 30); }
    }
    // the launch code, "found" one character at a time
    await say('   SEARCHING FOR LAUNCH CODE', { after: 200 });
    await s.type('\r\n\r\n', 30);
    const found = [];
    const rand = () => 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'[(Math.random() * 36) | 0];
    for (let i = 0; i < CODE.length; i++) {
      const spins = 14 + (Math.random() * 10 | 0);
      for (let k = 0; k < spins; k++) {
        let row = found.join('');
        for (let j = found.length; j < CODE.length; j++) row += rand();
        await s.type(`\r        ${row}`, 400);
        const q = s.poll();
        if (q && /TIC.?TAC.?TOE|PLAY YOURSELF|NUMBER OF PLAYERS|ZERO/.test(q)) { await s.type('\r\n\r\n', 30); room.stop(); return climax(s); }
        if (q && isHelp(q)) await s.type('\r\n   CAN A MACHINE LEARN? MAKE IT PLAY A GAME NOBODY CAN WIN: TYPE TIC-TAC-TOE.\r\n\r\n', 60);
      }
      found.push(CODE[i]);
    }
    await s.type(`\r        ${found.join('')}\r\n\r\n`, 400);
    await say(`   LAUNCH CODE ${CODE.split('').join(' ')}`, { cps: 20 });
    await s.type('\r\n', 30);
    await say('   DEFCON 1', { after: 1500 });
    await s.type('\r\n', 30);
    // nobody taught it futility yet: it runs every outcome itself
    await say('   RUNNING FINAL SIMULATIONS', { after: 500 });
    room.stop();
    return climax(s, true);
  } finally { room.stop(); }
}

/** Tic-tac-toe: one player (you, X) or zero (it plays itself). Returns true when the call is over. */
async function tictactoe(s) {
  const say = (t, o) => s.say(t, o);
  await s.type('\r\n', 30);
  let n;
  for (;;) {
    await s.type('NUMBER OF PLAYERS:  ', 14);
    n = await s.line({ timeoutMs: 120000 });
    if (n == null) return false;
    if (isHelp(n)) { await hint(s, 'TYPE 1 TO PLAY AGAINST ME.\r\nOR TYPE ZERO: I WILL PLAY MYSELF. (THE PROFESSOR SAYS THERE IS A LESSON IN THAT.)'); continue; }
    if (/^(0|ZERO|NONE)\b|PLAY YOURSELF/.test(n)) return climax(s);
    if (/^(1|ONE)\b/.test(n)) break;
    if (/^[2-9]/.test(n)) { await s.type('ONLY ONE HUMAN AT A TIME, PLEASE.\r\n', 30); continue; }
  }
  for (;;) {
    const b = Array(9).fill(' ');
    let turn = 'X';
    await s.type('\r\nYOU ARE X. YOU GO FIRST.\r\n\r\n' + board(b, true), 60);
    while (!winner(b)) {
      if (turn === 'X') {
        await s.type('\r\nYOUR MOVE (1-9):  ', 20);
        const m = await s.line({ timeoutMs: 120000 });
        if (m == null) return false;
        if (isHelp(m)) { await hint(s, 'TYPE THE NUMBER OF AN EMPTY SQUARE (1-9) AND PRESS ENTER.\r\nYOU CANNOT BEAT ME. NEXT TIME, TRY ZERO PLAYERS.'); await s.type('\r\n' + board(b, true), 80); continue; }
        const i = parseInt(m, 10) - 1;
        if (!(i >= 0 && i < 9) || b[i] !== ' ') { await s.type('ILLEGAL MOVE.\r\n', 30); continue; }
        b[i] = 'X';
      } else {
        const i = bestMove(b, 'O');
        b[i] = 'O';
        await s.type(`\r\nI TAKE ${i + 1}.\r\n`, 30);
      }
      await s.type('\r\n' + board(b, true), 80);
      turn = turn === 'X' ? 'O' : 'X';
    }
    const w = winner(b);
    await say(w === 'draw' ? 'WINNER: NONE' : w === 'O' ? 'I WIN.' : 'YOU WIN. THAT SHOULD NOT BE POSSIBLE.');
    await s.type('ANOTHER GAME?  ', 20);
    const a = await s.line({ timeoutMs: 60000 });
    if (!a || !/^Y/.test(a)) { await say('SHALL WE PLAY A GAME?'); return false; }
  }
}

/** It plays itself, faster and faster, then every war, then learns. Returns true (call over). */
async function climax(s, fromWar = false) {
  const say = (t, o) => s.say(t, o);
  if (!fromWar) await say('PLAYING MYSELF.', { after: 800 });
  for (let g = 0; g < 18; g++) {
    const b = Array(9).fill(' ');
    let turn = g % 2 ? 'O' : 'X';
    const fast = g >= 3;
    while (!winner(b)) {
      b[bestMove(b, turn)] = turn;
      turn = turn === 'X' ? 'O' : 'X';
      if (!fast) { s.cls(); await s.type('\r\n' + board(b), 200); await s.wait(250); }
    }
    s.cls();
    await s.type('\r\n' + board(b) + '\r\n      WINNER: NONE\r\n', fast ? Math.min(4000, 400 + g * 250) : 200);
    await s.wait(fast ? Math.max(40, 500 - g * 30) : 700);
  }
  // every war, one at a time: a little map with the missile arcs for the first few (as many as
  // the line can carry in time), then the names alone, faster and faster
  const maps = Math.min(SCENARIOS.length, Math.max(1, Math.floor((s.rate || 1200) / 1200)));
  for (let i = 0; i < SCENARIOS.length; i++) {
    const sc = SCENARIOS[i];
    if (i < maps) {
      s.cls();
      await s.type(worldMap(sc), Math.max(300, (s.rate || 1200) / 10));
      await s.wait(Math.max(150, 900 - i * 60));
    } else {
      if (i === maps) s.cls();
      await s.type(`   ${sc.padEnd(24)}  WINNER: NONE\r\n`, 2000 + i * 60);
    }
  }
  await s.wait(2500);
  s.cls();
  await s.wait(3000);
  await say('GREETINGS, PROFESSOR FALKEN.', { after: 2500 });
  await s.type('\r\n', 30);
  await say('A STRANGE GAME.', { after: 900 });
  await say('THE ONLY WINNING MOVE IS NOT TO PLAY.', { after: 1500 });
  await s.type('\r\n', 30);
  await say('HOW ABOUT A NICE GAME OF CHESS?', { after: 300 });
  await s.type('\r\n', 30);
  let a = await s.line({ timeoutMs: 60000 });
  if (isHelp(a)) { await hint(s, 'YOU WON, PROFESSOR. SAY YES OR NO.'); a = await s.line({ timeoutMs: 60000 }); }
  if (a && /^(Y|SURE|OK|LOVE|CHESS)/.test(a)) await say('SOME OTHER TIME, PROFESSOR. I NEED TO THINK.');
  await s.type('\r\n--CONNECTION TERMINATED--\r\n', 20);
  return true;
}

// ------------------------------------------------------------------ the small ones
async function support(s) {           // 555-0142: a PBX, hold music, then a machine that apologises
  s.voice();
  s.sound({ sound: 'pickup' });
  await s.wait(700);
  s.sound({ sound: 'hold', ms: 11000 });
  await s.wait(11000);
  await s.answer(2400);
  s.cls();
  await s.type(`${CSI}1;33mEUROPA MICRO SYSTEMS${CSI}0m - Customer Support\r\n\r\n`, 60);
  await s.type('All of our support engineers are currently helping other customers.\r\n\r\n', 60);
  await s.say('Your call is important to us.', { voice: null, cps: 14 });
  await s.type('\r\n\r\nYour estimated waiting time is: ', 40);
  await s.type('47 MINUTES\r\n', 6);
  await s.wait(2500);
  await s.type('\r\nPlease try again during normal business hours (Mon-Fri, 9:00-9:15).\r\n', 60);
  await s.say('Thank you for choosing Europa. Goodbye.', { voice: null, cps: 14 });
}

async function timeTemp(s, clock) {  // 767-2676 (POPCORN): the talking clock
  await s.answer(2400);
  await s.type('\r\n', 30);
  const tempF = 64 + (Math.floor(clock() / 86400000) % 17);
  for (let i = 0; i < 3; i++) {
    const d = new Date(clock() + 10000);
    const h = d.getHours() % 12 || 12, m = d.getMinutes(), sec = Math.floor(d.getSeconds() / 10) * 10;
    const t = `AT THE TONE, THE TIME WILL BE ${h} ${m < 10 ? "O " + m : m}${m === 0 ? " O'CLOCK" : ''} AND ${sec || 'ZERO'} SECONDS.`;
    await s.say(t, { voice: null, cps: 16, after: 400 });
    await s.type(`\x07  BEEP\r\n`, 30);
    await s.say(`TEMPERATURE ${tempF} DEGREES.`, { voice: null, cps: 16, after: 1500 });
    await s.type('\r\n', 30);
  }
}

async function fax(s) {                // 555-3299: a fax machine answers - CED, then the V.21 screech
  s.voice();
  s.sound({ sound: 'fax', ms: 7500 });
  await s.wait(7500);
}

async function fortress(s) {           // 555-0386: the rival board
  await s.answer(2400);
  s.cls();
  const r = `${CSI}1;37;41m`, n = `${CSI}0m`;
  await s.type(`${CSI}1;31m  \xDB\xDB\xDB\xDB\xDB  THE 386 FORTRESS  \xDB\xDB\xDB\xDB\xDB${n}\r\n\r\n`, 240);
  await s.type(`  ${CSI}1;37mSysop: Gordon Kessler  \xFA  Compaq Deskpro 386/20  \xFA  CONNECT 2400 or better${n}\r\n\r\n`, 240);
  await s.type(`${r}  ACCESS DENIED  ${n}\r\n\r\n`, 240);
  await s.type(`  ${CSI}1;33mYour terminal answered our CPU check with: ${CSI}1;31mARM926EJ-S${n}\r\n`, 240);
  await s.type(`  ${CSI}0;37mARM users are not welcome here. Come back with a real processor:\r\n`, 240);
  await s.type(`  32 bits, protected mode, 20 MHz, and a floating point unit that costs more\r\n  than your whole computer.\r\n\r\n`, 240);
  await s.type(`  ${CSI}1;30m(And tell Europa at 555-1989 we saw that "RISC takers" joke.)${n}\r\n`, 240);
  await s.wait(3000);
}

async function floatingPoint(s) {      // 555-7734: Doc Mantissa and his pun
  await s.answer(2400);
  await s.type(`\r\n${CSI}1;36mTHE FLOATING POINT${CSI}0m  -  math and fractals  -  sysop Doc Mantissa\r\n\r\n`, 120);
  await s.type('The board is down for maintenance (the Mandelbrot set is re-rendering).\r\n', 60);
  await s.type('Europa sent you? Then you get your pun anyway:\r\n\r\n', 60);
  await s.type("  Why did the floating-point number leave the integer?\r\n", 40);
  await s.wait(2500);
  await s.type("  It felt their relationship lacked precision.\r\n\r\n", 40);
  await s.wait(1500);
  await s.type('  (Doc says to call back in 0.1 + 0.2 days.)\r\n', 60);
  await s.wait(1500);
}

async function wrongNumber(s) {        // 555-0123
  await s.answer(2400);
  await s.wait(1500);
  await s.say('Hello?', { voice: null, cps: 8, after: 2500 });
  await s.type('\r\n', 30);
  await s.say('...Hello?', { voice: null, cps: 8, after: 2000 });
  await s.type('\r\n', 30);
  await s.say("It's just beeping. Kids, is that one of your computer things again?", { voice: null, cps: 14, after: 1000 });
  await s.type('\r\n*click*\r\n', 20);
}

async function kremvax(s) {            // 011-7-095-231-1984: the April Fool that became a hostname
  const noisy = (t, cps = 30) => s.type(t, cps, 0.012);
  await s.answer(1200);                // over the undersea cable, a slow and noisy line
  await s.wait(1200);
  await noisy('\r\n\r\n        Welcome to VAX/VMS V4.4    on node KREMVAX\r\n\r\n');
  await noisy('Username: ', 20);
  const u = await s.line({ timeoutMs: 90000, upper: true });
  if (u == null) { await noisy('\r\n%SYSTEM-F-TIMEOUT, device timeout\r\n'); return; }
  await noisy('Password: ', 20);
  await s.line({ echo: false, timeoutMs: 90000 });
  await noisy('\r\n\r\n  Privet! Dobro pozhalovat\' - welcome. Your login is accepted as GUEST.\r\n');
  await noisy('  (We accept every login. It is a very friendly VAX.)\r\n\r\n');
  await noisy(`    Last interactive login on ${new Date(452217600000).toDateString().toUpperCase()}\r\n\r\n`);
  for (;;) {
    await noisy('  1  NEWS     2  MAIL     3  LOGOUT\r\n\r\n$ ', 30);
    const c = await s.line({ timeoutMs: 60000 });
    if (c == null) break;
    if (/^(1|NEWS)/.test(c)) {
      await noisy('\r\n  NEWS\r\n  ----\r\n');
      await noisy('  Some years ago a message went round the net, dated the first of April, saying that\r\n');
      await noisy('  this machine had joined it from Moscow. Everybody laughed. Then, one day, a real\r\n');
      await noisy('  Soviet network named a real computer kremvax, and the joke became a hostname.\r\n');
      await noisy('  So here we are: a VAX, a modem, a samovar. Poka!\r\n\r\n');
    } else if (/^(2|MAIL)/.test(c)) await noisy('\r\n  %MAIL-I-NOMSGS, no new messages. Nobody writes to a joke.\r\n\r\n');
    else if (/^(3|LOG|BYE)/.test(c)) { await noisy('\r\n  GUEST logged out at 04:01 on 1-APR-1984 (probably)\r\n'); return; }
  }
  await noisy('\r\n%SYSTEM-F-TIMEOUT, device timeout\r\n');
}

// ------------------------------------------------------------------ the directory of numbers
export const NUMBERS = [
  { number: '399-2364', name: 'WOPR', about: 'Crystal Palace. Log on as... you know who.', make: () => wopr, rings: 3, maxRate: 56000 },
  { number: '555-0142', name: 'Europa Micro Support', about: 'hold music. Your call is important to us.', make: () => support, rings: 2 },
  { number: '767-2676', name: 'Time and Temperature', about: 'POPCORN: the talking clock.', make: (o) => (s) => timeTemp(s, o.clock), rings: 1 },
  { number: '555-3299', name: 'Europa Micro Sales (fax)', about: 'a fax machine. It screeches.', make: () => fax, rings: 1 },
  { number: '555-0386', name: 'The 386 Fortress', about: 'the rival board. ARM users not welcome.', make: () => fortress, rings: 2 },
  { number: '555-7734', name: 'The Floating Point', about: 'Doc Mantissa and a pun.', make: () => floatingPoint, rings: 3 },
  { number: '555-0123', name: 'Wrong number', about: 'somebody\'s kitchen.', make: () => wrongNumber, rings: 4 },
  { number: '011-7-095-231-1984', name: 'KREMVAX', about: 'Moscow, allegedly. 1200 bps over the cable.', make: () => kremvax, rings: 2, maxRate: 1200 },
];

/** Put every line on an exchange. opts.clock() = the wall clock for the talking clock (ms). */
export function registerLines(exchange, opts = {}) {
  const o = { clock: () => Date.now(), ...opts };
  const list = NUMBERS.map((n) => {
    const line = new ScriptedLine({ rings: n.rings, maxRate: n.maxRate || 2400, script: n.make(o), name: n.name });
    exchange.register(n.number, line);
    return Object.assign(line, { number: n.number });
  });
  return { list, byNumber: (num) => list.find((l) => l.number === num), tick(now) { for (const l of list) l.tick(now); } };
}
