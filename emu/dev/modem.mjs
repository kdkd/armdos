// The ARM-PC's internal Hayes-compatible modem behind COM2 (docs/MODEM.md): "Europa Micro
// Systems SmartLine" — a Hayes Smartmodem 2400 command set on a card whose speed switch (the
// page's front panel) picks 2400 V.22bis, 14400 V.32bis, 33600 V.34 or 56K V.90; AT&B, S37
// and AT+MS= override per call. Both ends follow the slower one.
//
//   const modem = new Modem(machine, uart, { exchange, number, onSound })
//
// All timing is the machine's emulated time. The UART (dev/uart16550.mjs) is the DTE side;
// the phone line is a leg on a PhoneExchange (../phone.mjs).
//
// Data path: characters the computer sends leave the UART at the line rate while online
// (the UART asks txCharNs()), characters from the line wait in `rxq` and are handed to the
// UART one character time apart (line rate, or the DTE rate for the modem's own messages).
// The internal modem never overruns the UART: if the receive FIFO is full it waits (as if
// its buffer were flow controlled by the FIFO). With AT&K3 it also honours RTS (holds data
// while RTS is low) and drops CTS while the far end's buffer is over 1 KB.
//
// Sound: onSound({ kind, t, ...}) with t = emulated ms, only for what the speaker lets you
// hear (ATM, ATL); `relay` clicks are mechanical and always audible. Kinds:
//   relay {on}            the off-hook relay (on = off hook)
//   dialtone {on}         350 + 440 Hz
//   dtmf {digit, ms}      one DTMF digit
//   pulse {digit}         a pulse-dialled digit (n x 100 ms, 60 % break)
//   ringback {on}         440 + 480 Hz, 2 s on 4 s off (the modem sends each burst)
//   busy {on}             480 + 620 Hz, 0.5 s on 0.5 s off
//   answer {on}           2100 Hz answer tone (heard on both ends)
//   handshake {ms, rate, role}   the training noise; role 'caller' | 'answerer'
//   carrier {on, rate}    the data carriers once connected (only heard with ATM2)
//   hush                  the speaker is switched off (ATM1 at CONNECT, ATM0, on hook)
// `vol` (0-3, from ATL) is on every event.

import { normalizeNumber, formatNumber } from '../phone.mjs';
import { handshakeMs as trainingMs } from '../modemsound.mjs';

export const RESULTS = {
  OK: 0, CONNECT: 1, RING: 2, 'NO CARRIER': 3, ERROR: 4, 'CONNECT 1200': 5, 'NO DIALTONE': 6, BUSY: 7,
  'NO ANSWER': 8, 'CONNECT 2400': 10, 'CONNECT 4800': 11, 'CONNECT 9600': 12, 'CONNECT 14400': 13,
  'CONNECT 7200': 24, 'CONNECT 12000': 25, 'CONNECT 300': 1,
};
// S37 "desired line speed" codes (Hayes V-series / USR style; 12-20 are this card's extension).
// S37=0 means "the card's switch setting" (the page's speed switch; 2400 from the factory).
export const S37_RATES = { 3: 300, 5: 1200, 6: 2400, 7: 4800, 8: 7200, 9: 9600, 10: 12000, 11: 14400,
  12: 16800, 13: 19200, 14: 21600, 15: 24000, 16: 26400, 17: 28800, 18: 31200, 19: 33600, 20: 56000 };
const RATE_S37 = Object.fromEntries(Object.entries(S37_RATES).map(([k, v]) => [v, +k]));
export const MAX_RATE = 56000;
export const SWITCH_RATES = [2400, 14400, 33600, 56000];
/** The modulation a nominal rate trains with. */
export function modulation(rate) {
  return rate <= 300 ? 'V21' : rate <= 1200 ? 'V22' : rate <= 2400 ? 'V22BIS' : rate <= 14400 ? 'V32BIS' : rate <= 33600 ? 'V34' : 'V90';
}
// +MS= modulation names / V.250 codes -> the fastest rate of that modulation
const MS_MODS = { V21: 300, B103: 300, V22: 1200, B212: 1200, V22B: 2400, V32: 9600, V32B: 14400, V34: 33600, V90: 56000, K56: 56000,
  0: 300, 1: 1200, 2: 2400, 9: 9600, 10: 14400, 11: 33600, 12: 56000, 64: 300, 69: 1200 };

const MS = 1e6;
const FACTORY_S = [0, 0, 43, 13, 10, 8, 2, 30, 2, 6, 14, 70, 50];
// timeline of a connection (ms): the answerer waits (billing delay), sends 2100 Hz, then
// both modems train. The caller hears the same thing, a moment later.
const T_BILLING = 400, T_ANSWER = 2400;
// V.22bis/V.32bis: V.25 answer tone then the training. V.34 and V.90 start with V.8 (ANSam,
// CM/JM), so their whole sequence from the answer onwards is one "handshake" (the ANSam is in it).
// (the durations come from the synthesised sequences in emu/modemsound.mjs: V.34 ~8.2 s, V.90 ~17.9 s)
const handshakeMs = (rate) => rate <= 4800 && rate > 2400 ? 3000 : trainingMs(modulation(rate));
const answerToneMs = (rate) => rate > 14400 ? 0 : T_ANSWER;
const RING_ON = 2000, RING_OFF = 4000;
/** From going off hook to answer until both modems say CONNECT, at a line rate (ms). */
export const connectDelayMs = (rate) => T_BILLING + answerToneMs(rate) + handshakeMs(rate);

export class Modem {
  constructor(m, uart, { exchange = null, number = null, onSound = null, onChange = null } = {}) {
    this.m = m; this.uart = uart; uart.backend = this;
    this.exchange = exchange; this.number = null;
    this.onSound = onSound || (() => {});
    this.onChange = onChange || (() => {});
    this.agenda = [];                 // [{ ns, fn, tag }]
    this.rxq = []; this.rxPace = [];   // to the DTE: bytes, and whether each is line data (1) or a message (0)
    this.nextRxNs = 0; this.rxTimer = false;
    this.rxBytes = 0; this.txBytes = 0;
    this.lastNumber = '';
    this.switchRate = 2400;
    this.rxRate = 0; this.txRate = 0;
    this.factory();
    this.state = 'idle';
    this.hook = false; this.carrier = false; this.leg = null; this.rate = 0; this.dialing = '';
    this.online = false;
    if (exchange && number) this.attach(exchange, number);
  }

  /** Put this modem's line on an exchange under a number. */
  attach(exchange, number) {
    if (this.exchange && this.number) this.exchange.unregister(this.number, this);
    this.exchange = exchange; this.number = number ? normalizeNumber(number) : null;
    if (exchange && this.number) exchange.register(this.number, this);
  }

  // ---------------------------------------------------------------- configuration
  factory() {
    this.S = new Uint8Array(256);
    FACTORY_S.forEach((v, i) => { this.S[i] = v; });
    this.S[37] = 0;
    this.wmode = 0;
    this.echo = 1; this.verbose = 1; this.quiet = 0; this.xlevel = 4;
    this.spkMode = 1; this.spkVol = 2;
    this.dcdMode = 1; this.dtrMode = 2; this.flow = 0;
    this.sreg = 0;
    this.cmd = ''; this.inAT = 0; this.lastCmd = '';
    this.escCount = 0; this.lastTxNs = -1e18; this.cmdNs = -1e18;
  }
  desiredRate() { return S37_RATES[this.S[37]] || this.switchRate; }
  /** The card's speed switch (the page's front panel): the rate S37=0 trains at. */
  setSwitch(rate) { this.switchRate = SWITCH_RATES.includes(+rate) ? +rate : 2400; this.changed?.(); }
  lineCharNs() { return this.rxRate ? 10e9 / this.rxRate : 0; }     // receiving
  txLineCharNs() { return this.txRate ? 10e9 / this.txRate : 0; }   // transmitting
  get dtr() { return (this.uart.mcr & 1) !== 0; }
  get rts() { return (this.uart.mcr & 2) !== 0; }

  /** A reset of the whole card (the machine's RESET line, ATZ does the command part). */
  reset() {
    this.hangup(true);
    this.agenda = this.state === 'ringing' ? this.agenda.filter((e) => e.tag === 'ring') : [];
    this.rxq.length = 0; this.rxPace.length = 0; this.rxTimer = false;
    this.factory();
    if (this.state !== 'ringing') this.state = 'idle';
    this.lines();
    this.changed();
  }

  // ---------------------------------------------------------------- time / agenda
  now() { return this.m.timeNs(); }
  nowMs() { return this.m.timeNs() / MS; }
  after(ms, fn, tag = null) {
    const ns = this.now() + ms * MS;
    this.agenda.push({ ns, fn, tag });
    this.agenda.sort((a, b) => a.ns - b.ns);
    this.m.reschedule();
  }
  cancel(tag) { this.agenda = this.agenda.filter((e) => e.tag !== tag); }
  cancelCall() { this.agenda = this.agenda.filter((e) => e.tag !== 'call' && e.tag !== 'ring' && e.tag !== 'esc'); }
  nextEventNs() {
    let t = this.agenda.length ? this.agenda[0].ns : Infinity;
    if (this.rxTimer && this.nextRxNs < t) t = this.nextRxNs;
    return t;
  }
  service() {
    const now = this.now();
    for (let guard = 0; guard < 64 && this.agenda.length && this.agenda[0].ns <= now; guard++) {
      const e = this.agenda.shift();
      e.fn();
    }
    if (this.rxTimer && this.nextRxNs <= now) this.deliver();
  }

  // ---------------------------------------------------------------- sound + status
  audible() {
    if (!this.hook) return false;
    switch (this.spkMode) {
      case 0: return false;
      case 1: return !this.carrier;
      case 2: return true;
      case 3: return !this.carrier && this.state !== 'dialing';
    }
    return false;
  }
  sound(kind, ev = {}) {
    if (kind !== 'relay' && kind !== 'hush' && !this.audible()) return;
    this.onSound({ kind, t: this.nowMs(), vol: this.spkVol, ...ev });
  }
  changed() { this.onChange(this.panel()); }
  /** Front panel lamps (Hayes Smartmodem order) and a line status for the page. */
  panel() {
    return {
      hs: this.carrier && this.rate >= 2400, speed: this.switchRate, aa: this.S[0] > 0 || this.state === 'ringing', cd: this.dcd(),
      oh: this.hook, rd: this.rxBytes, sd: this.txBytes, tr: this.dtr, mr: true,
      ri: !!this.ri, state: this.state, rate: this.rate, number: this.dialing, online: this.online,
      ringing: this.state === 'ringing',
    };
  }
  dcd() { return this.dcdMode === 0 ? true : this.carrier; }
  lines() {
    const flowHold = this.flow === 3 && this.leg && this.leg.remoteBacklog() > 1024;
    this.uart.setModemLines({ cts: !flowHold, dsr: true, ri: !!this.ri, dcd: this.dcd() });
  }

  // ---------------------------------------------------------------- DTE side (from the UART)
  txCharNs() { return this.online ? this.txLineCharNs() : 0; }

  txByte(b) {
    this.txBytes++;
    if (this.online) { this.dataOut(b); return; }
    if (this.state === 'dialing' || this.state === 'waitcarrier' || this.state === 'answering' || this.state === 'training') {
      // any key aborts a call in progress (but not the LF of a CR LF, e.g. ECHO ATDT...>COM2)
      if (this.now() - this.cmdNs < 150 * MS) return;
      this.hangup(); this.result('NO CARRIER'); return;
    }
    this.commandChar(b);
  }

  mcrChanged(mcr, old) {
    const dtrWas = (old & 1) !== 0 && old !== 0xFF, dtrNow = (mcr & 1) !== 0;
    if (dtrWas && !dtrNow) {
      if (this.dtrMode === 1 && this.online) { this.online = false; this.result('OK'); }
      else if (this.dtrMode === 2 && this.hook) { this.hangup(); this.result('NO CARRIER'); }
      else if (this.dtrMode === 3) { this.hangup(); this.factory(); }
    }
    if ((mcr ^ old) & 2) this.kick();
    this.changed();
  }
  rxReady() { this.kick(); }

  // ---------------------------------------------------------------- to the DTE (paced)
  toDte(bytes, isData) {
    for (const b of bytes) { this.rxq.push(b & 0xFF); this.rxPace.push(isData ? 1 : 0); }
    this.kick();
  }
  kick() {
    if (this.rxTimer || !this.rxq.length) return;
    if (!this.online && !this.rxPace.includes(0)) return;       // only held line data: wait for ATO
    if (this.flow === 3 && !this.rts && !this.rxPace.includes(0)) return;
    this.rxTimer = true;
    const now = this.now();
    if (this.nextRxNs < now) this.nextRxNs = now;
    this.m.reschedule();
  }
  deliver() {
    this.rxTimer = false;
    if (!this.rxq.length) return;
    let idx = 0;
    if (!this.online || (this.flow === 3 && !this.rts)) {       // line data is held (online command mode / RTS low)
      idx = this.rxPace.indexOf(0);                              // but the modem's own messages go through
      if (idx < 0) return;
    }
    const isData = this.rxPace[idx];
    if (this.uart.rxFree() <= 0) return;                         // rxReady() will kick us
    const b = this.rxq[idx];
    this.rxq.splice(idx, 1); this.rxPace.splice(idx, 1);
    this.uart.receive(b);
    if (isData) this.leg?.consumed?.(1);
    let ns = this.uart.charNs();
    if (isData) ns = Math.max(ns, this.lineCharNs());
    this.nextRxNs = this.now() + ns;
    if (this.flow === 3) this.lines();
    this.kick();
  }
  backlog() { let n = 0; for (const p of this.rxPace) n += p; return n; }
  flushData() {
    for (let i = this.rxq.length - 1; i >= 0; i--) if (this.rxPace[i]) { this.rxq.splice(i, 1); this.rxPace.splice(i, 1); }
  }
  say(text) { this.toDte([...text].map((c) => c.charCodeAt(0)), false); }
  crlf() { return String.fromCharCode(this.S[3], this.S[4]); }
  result(name) {
    if (this.quiet) return;
    let code = name;
    if (name.startsWith('CONNECT') && this.xlevel === 0) code = 'CONNECT';
    if (name === 'NO DIALTONE' && (this.xlevel === 0 || this.xlevel === 1 || this.xlevel === 3)) code = 'NO CARRIER';
    if (name === 'BUSY' && this.xlevel < 3) code = 'NO CARRIER';
    if (name === 'NO ANSWER' && this.xlevel < 3) code = 'NO CARRIER';
    if (this.verbose) this.say(this.crlf() + code + this.crlf());
    else this.say(String(RESULTS[code.split('/')[0]] ?? (code.startsWith('CONNECT') ? 1 : 4)) + String.fromCharCode(this.S[3]));
  }
  info(text) {       // informational text (ATI, ATS?, AT&V)
    if (this.verbose) this.say(this.crlf() + text.split('\n').join(this.crlf()) + this.crlf());
    else this.say(text.split('\n').join(this.crlf()) + this.crlf());
  }

  // ---------------------------------------------------------------- command mode
  commandChar(b) {
    const S = this.S;
    if (this.echo) this.toDte([b], false);
    const c = String.fromCharCode(b & 0x7F);
    if (this.inAT === 0) { if (c === 'A' || c === 'a') this.inAT = 1; return; }
    if (this.inAT === 1) {
      if (c === 'T' || c === 't') { this.inAT = 2; this.cmd = ''; return; }
      if (c === '/') { this.inAT = 0; this.cmdNs = this.now(); this.execute(this.lastCmd); return; }
      this.inAT = (c === 'A' || c === 'a') ? 1 : 0; return;
    }
    if (b === S[3]) { this.inAT = 0; this.lastCmd = this.cmd; this.cmdNs = this.now(); this.execute(this.cmd); return; }
    if (b === S[5]) { if (this.cmd.length) this.cmd = this.cmd.slice(0, -1); else this.inAT = 1; return; }
    if (b < 32) return;
    if (this.cmd.length < 255) this.cmd += c;
  }

  /** Execute the text of one command line (after "AT"). */
  execute(line) {
    const S = this.S;
    let i = 0;
    const s = line;
    const peek = () => (s[i] || '').toUpperCase();
    const num = (def = 0) => {
      let j = i; while (j < s.length && s[j] >= '0' && s[j] <= '9') j++;
      if (j === i) return def;
      const v = parseInt(s.slice(i, j), 10); i = j; return v;
    };
    const infos = [];
    const flush = () => { for (const t of infos) this.info(t); infos.length = 0; };
    try {
      while (i < s.length) {
        const c = s[i++].toUpperCase();
        switch (c) {
          case ' ': case '-': break;
          case 'A': flush(); this.answerCmd(); return;
          case 'D': flush(); this.dialCmd(s.slice(i)); return;
          case 'E': { const v = num(); if (v > 1) throw 0; this.echo = v; break; }
          case 'V': { const v = num(); if (v > 1) throw 0; this.verbose = v; break; }
          case 'Q': { const v = num(); if (v > 1) throw 0; this.quiet = v; break; }
          case 'X': { const v = num(); if (v > 4) throw 0; this.xlevel = v; break; }
          case 'M': { const v = num(); if (v > 3) throw 0; this.spkMode = v; if (!this.audible()) this.sound('hush'); break; }
          case 'L': { const v = num(); if (v > 3) throw 0; this.spkVol = v; break; }
          case 'H': {
            const v = num();
            if (v === 0) { if (this.hook) this.hangup(); }
            else if (v === 1) { if (!this.hook) { this.offHook(); this.state = 'offhook'; this.changed(); } }
            else throw 0;
            break;
          }
          case 'O': {
            num();
            flush();
            if (this.carrier && this.leg) {
              // CONNECT goes ahead of the line data that waited in online command mode
              const held = [];
              for (let k = this.rxq.length - 1; k >= 0; k--) if (this.rxPace[k]) { held.unshift(this.rxq[k]); this.rxq.splice(k, 1); this.rxPace.splice(k, 1); }
              this.result(this.connectText());
              this.dataIn(held);
              this.online = true; this.changed(); this.kick(); return;
            }
            this.result('NO CARRIER'); return;
          }
          case 'Z': num(); flush(); this.hangup(); this.factory(); this.lines(); this.changed(); this.result('OK'); return;
          case 'I': {
            const v = num();
            const t = { 0: '2400', 1: '255', 2: 'OK', 3: 'Europa Micro Systems Internal 2400 Modem V1.00', 4: 'ARM-PC COM2 16550A, 300-14400 bps' }[v];
            if (t === undefined) throw 0;
            infos.push(t); break;
          }
          case 'S': {
            const n = num(-1);
            if (n < 0 || n > 255) throw 0;
            this.sreg = n;
            if (s[i] === '=') { i++; const v = num(0); if (v > 255) throw 0; this.setS(n, v); }
            else if (s[i] === '?') { i++; infos.push(String(S[n]).padStart(3, '0')); }
            break;
          }
          case '?': infos.push(String(S[this.sreg]).padStart(3, '0')); break;
          case '=': { const v = num(0); if (v > 255) throw 0; this.setS(this.sreg, v); break; }
          case 'W': { const v = num(); if (v > 2) throw 0; this.wmode = v; break; }
          case '+': {
            // AT+MS=<mod>[,<automode>[,<min rate>[,<max rate>]]] and AT+MS? (V.250 modulation select)
            const m = /^MS(=([A-Z0-9]+)(,(\d*))?(,(\d*))?(,(\d*))?|\?)/i.exec(s.slice(i));
            if (!m) throw 0;
            i += m[0].length;
            if (s[i] === ';') i++;           // extended commands end with ';' when more follow
            if (m[1] === '?') { const r = this.desiredRate(); infos.push(`+MS: ${({ V21: 0, V22: 1, V22BIS: 2, V32BIS: 10, V34: 11, V90: 12 })[modulation(r)]},1,300,${r}`); break; }
            const top = MS_MODS[m[2].toUpperCase()];
            if (!top) throw 0;
            let r = top;
            if (m[8]) { const mx = +m[8]; if (mx) r = Math.min(top, mx); }
            const code = RATE_S37[r] ?? RATE_S37[Object.keys(RATE_S37).map(Number).filter((x) => x <= r).pop()];
            S[37] = code;
            break;
          }
          case 'B': case 'C': case 'N': case 'P': case 'T': case 'Y': case 'F': num(); break;
          case '&': {
            const k = peek(); i++;
            if (k === 'B') { const v = num(-1); if (!(v in RATE_S37)) throw 0; S[37] = RATE_S37[v]; break; }
            const v = num();
            switch (k) {
              case 'C': if (v > 1) throw 0; this.dcdMode = v; this.lines(); break;
              case 'D': if (v > 3) throw 0; this.dtrMode = v; break;
              case 'K': if (v > 4) throw 0; this.flow = v === 3 ? 3 : 0; this.lines(); break;
              case 'F': this.factory(); this.lines(); break;
              case 'V': infos.push(this.profile()); break;
              case 'W': case 'Y': case 'Z': case 'G': case 'J': case 'L': case 'M': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'X': case 'A': break;
              default: throw 0;
            }
            break;
          }
          case '\\': case '%': case '*': case '#': case '"': i++; num(); break;
          default: throw 0;
        }
      }
      flush();
      this.changed();
      this.result('OK');
    } catch (e) {
      if (e !== 0) throw e;
      infos.length = 0;
      this.result('ERROR');
    }
  }
  setS(n, v) {
    this.S[n] = v;
    if (n === 0) this.changed();
    if (n === 37 && !(v in S37_RATES)) this.S[37] = 0;     // 0 = the speed switch
  }
  profile() {
    const S = this.S, s3 = (n) => `S${String(n).padStart(2, '0')}:${String(S[n]).padStart(3, '0')}`;
    return [
      'ACTIVE PROFILE:',
      `B1 E${this.echo} L${this.spkVol} M${this.spkMode} P Q${this.quiet} V${this.verbose} W${this.wmode} X${this.xlevel} Y0 &C${this.dcdMode} &D${this.dtrMode} &K${this.flow} &B${this.desiredRate()}`,
      `SPEED SWITCH: ${this.switchRate}  MODULATION: ${modulation(this.desiredRate()).replace('BIS', 'bis').replace(/^V/, 'V.')}`,
      [0, 1, 2, 3, 4, 5, 6].map(s3).join(' '),
      [7, 8, 9, 10, 11, 12, 37].map(s3).join(' '),
      '',
      'TELEPHONE NUMBER: ' + (this.lastNumber ? formatNumber(this.lastNumber) : ''),
    ].join('\n');
  }
  connectText() {
    if (this.rate === 300) return 'CONNECT';
    if (!this.wmode) return `CONNECT ${this.rate}`;
    // ATW1/W2: the line rate actually received and the modulation, e.g. CONNECT 53333/V90
    return `CONNECT ${this.rxRate}/${modulation(this.rate)}`;
  }
  setRates(leg) { this.rxRate = leg.rxRate || this.rate; this.txRate = leg.txRate || this.rate; }

  // ---------------------------------------------------------------- the hook
  offHook() {
    if (this.hook) return;
    this.hook = true;
    this.sound('relay', { on: true });
    this.changed();
  }
  /** Hang up (local). quiet = no result code. */
  hangup(quiet = true) {
    // on hook and the phone is ringing: ATZ / ATH / a reset don't stop the far end ringing
    if (!this.hook && this.state === 'ringing' && this.leg) { this.changed(); return quiet; }
    this.cancelCall();
    const wasHook = this.hook;
    if (this.leg) { const l = this.leg; this.leg = null; l.owner = null; l.hangup(); }
    this.online = false; this.carrier = false; this.rate = 0; this.rxRate = 0; this.txRate = 0; this.dialing = '';
    this.ri = false; this.escCount = 0;
    this.flushData();
    if (wasHook) { this.sound('hush'); this.hook = false; this.sound('relay', { on: false }); }
    this.hook = false;
    this.state = 'idle';
    this.lines();
    this.changed();
    return quiet;
  }

  // ---------------------------------------------------------------- dialling
  dialCmd(rest) {
    let str = rest, returnToCmd = false, pulse = false;
    if (/^L/i.test(str)) str = this.lastNumber;
    const semi = str.indexOf(';');
    if (semi >= 0) { returnToCmd = true; str = str.slice(0, semi); }
    const steps = [];
    let digits = '';
    for (const ch0 of str) {
      const ch = ch0.toUpperCase();
      if (ch === 'T') pulse = false;
      else if (ch === 'P') pulse = true;
      else if (/[0-9*#ABCD]/.test(ch) && !(pulse && !/[0-9]/.test(ch))) { steps.push({ d: ch, pulse }); if (/[0-9]/.test(ch)) digits += ch; }
      else if (ch === ',') steps.push({ pause: this.S[8] * 1000 });
      else if (ch === 'W') steps.push({ wait: true });
      else if (ch === '!') steps.push({ flash: true });
      // R, @, S=, spaces, dashes, parentheses: ignored
    }
    this.lastNumber = digits;
    if (!this.exchange) {
      this.offHook(); this.state = 'dialing'; this.changed();
      this.after(this.S[6] * 1000, () => { this.hangup(); this.result('NO DIALTONE'); }, 'call');
      return;
    }
    this.offHook();
    this.state = 'dialing';
    this.dialing = formatNumber(digits);
    this.changed();
    // dial tone, then the digits
    this.sound('dialtone', { on: true });
    let t = 900;
    let toneOn = true;
    const stopTone = () => { if (toneOn) { toneOn = false; this.sound('dialtone', { on: false }); } };
    for (const s of steps) {
      if (s.pause) { const at = t; this.after(at, () => stopTone(), 'call'); t += s.pause; continue; }
      if (s.wait) { t += 300; continue; }
      if (s.flash) { const at = t; this.after(at, () => { stopTone(); this.sound('relay', { on: false }); }, 'call'); this.after(at + 500, () => this.sound('relay', { on: true }), 'call'); t += 1500; continue; }
      if (s.pulse) {
        const n = s.d === '0' ? 10 : +s.d, at = t;
        this.after(at, () => { stopTone(); this.sound('pulse', { digit: s.d, n }); }, 'call');
        t += n * 100 + 700;
      } else {
        const ms = Math.max(50, this.S[11]), at = t;
        this.after(at, () => { stopTone(); this.sound('dtmf', { digit: s.d, ms }); }, 'call');
        t += ms * 2;
      }
    }
    this.after(t, () => {
      stopTone();
      if (returnToCmd) { this.state = 'offhook'; this.changed(); this.result('OK'); return; }
      this.placeCall(digits);
    }, 'call');
  }
  placeCall(digits) {
    this.state = 'waitcarrier';
    this.changed();
    this.after(this.S[7] * 1000, () => {
      const busy = this.busyHeard;
      this.hangup();
      this.result(busy ? 'BUSY' : 'NO ANSWER');
    }, 'call');
    this.busyHeard = false;
    const self = this;
    this.leg = this.exchange.dial(digits, {
      onRinging(info) { self.ringback(info); },
      onBusy() { self.busySignal(); },
      onAnswer(rate) { self.farAnswered(rate); },
      onVoice() { self.farVoice(); },
      onLineSound(ev) { self.sound('line', ev); },
      onData(bytes) { self.dataIn(bytes); },
      onHangup(why) { self.farHungUp(why); },
      backlog() { return self.backlog(); },
    }, { rate: this.desiredRate() });
  }
  ringback({ international = false } = {}) {
    // the network takes a moment, then the US ringback (440+480 Hz, 2 s on, 4 s off); an
    // international call routes for longer, then rings the far country's way (a single
    // ~425 Hz tone, 0.8 s on, 3.2 s off - the European/Soviet cadence)
    const on = international ? 800 : RING_ON, off = international ? 3200 : RING_OFF;
    const tone = international ? 'intl' : 'us';
    const cycle = () => {
      if (this.state !== 'waitcarrier') return;
      this.sound('ringback', { on: true, tone });
      this.after(on, () => { if (this.state === 'waitcarrier') this.sound('ringback', { on: false, tone }); }, 'ring');
      this.after(on + off, cycle, 'ring');
    };
    if (international) this.after(1500, () => this.sound('line', { sound: 'routing', ms: 3000 }), 'ring');
    this.after(international ? 4800 : 1200, cycle, 'ring');
  }
  busySignal() {
    this.busyHeard = true;
    this.cancel('call');
    let n = 0;
    const cycle = () => {
      if (this.state !== 'waitcarrier') return;
      if (n++ >= 4) { this.hangup(); this.result('BUSY'); return; }
      this.sound('busy', { on: true });
      this.after(500, () => this.sound('busy', { on: false }), 'ring');
      this.after(1000, cycle, 'ring');
    };
    this.after(600, cycle, 'ring');
  }
  /** The far end picked up without a modem (voice, PBX, fax): stop the ringback, keep listening. */
  farVoice() {
    if (this.state !== 'waitcarrier') return;
    this.cancel('ring');
    this.sound('ringback', { on: false });
    this.sound('pickup');   // (the page: a click on the line)
  }
  farAnswered(rate) {
    if (this.state !== 'waitcarrier') return;
    this.cancel('ring');
    this.sound('ringback', { on: false });
    this.state = 'training';
    this.rate = rate; this.setRates(this.leg);
    this.changed();
    // S7 counts to the answer; a V.90 training alone takes ~18 s, so allow for it (as the
    // fast modems' firmware did with its larger S7 defaults)
    this.cancel('call');
    this.after(connectDelayMs(rate) + 5000, () => { if (!this.carrier) { this.hangup(); this.result('NO CARRIER'); } }, 'call');
    this.train('caller');
  }

  // ---------------------------------------------------------------- answering
  /** PhoneExchange: somebody is calling this modem's number. */
  incoming(leg) {
    if (this.hook || this.leg || this.state !== 'idle') return 'busy';
    this.leg = leg;
    const self = this;
    leg.owner = {
      onData(bytes) { self.dataIn(bytes); },
      onHangup(why) { self.farHungUp(why); },
      backlog() { return self.backlog(); },
    };
    this.state = 'ringing';
    this.changed();
    const ring = () => {
      if (this.state !== 'ringing') return;
      this.ri = true; this.lines();
      this.S[1] = Math.min(255, this.S[1] + 1);
      this.result('RING');
      this.changed();
      this.after(RING_ON, () => {
        this.ri = false; this.lines(); this.changed();
        if (this.state !== 'ringing') return;
        const autoOk = this.S[0] > 0 && this.S[1] >= this.S[0] && (this.dtrMode !== 2 || this.dtr);
        if (autoOk) this.answer();
        else this.after(RING_OFF, ring, 'ring');
      }, 'ring');
    };
    this.after(300, ring, 'ring');
    return true;
  }
  answerCmd() {
    if (this.state === 'ringing' && this.leg) { this.answer(); return; }
    // ATA without a call: go off hook in answer mode and wait for a carrier that never comes
    this.hangup();
    this.offHook();
    this.state = 'answering';
    this.changed();
    this.sound('answer', { on: true });
    this.after(T_ANSWER, () => this.sound('answer', { on: false }), 'call');
    this.after(this.S[7] * 1000, () => { this.hangup(); this.result('NO CARRIER'); }, 'call');
  }
  answer() {
    this.cancel('ring');
    this.ri = false;
    this.offHook();
    this.state = 'training';
    const rate = this.leg.answer(this.desiredRate());
    this.rate = rate; this.setRates(this.leg);
    this.lines(); this.changed();
    this.train('answerer');
    this.after(Math.max(this.S[7] * 1000, connectDelayMs(rate) + 5000), () => { if (!this.carrier) { this.hangup(); this.result('NO CARRIER'); } }, 'call');
  }

  /** The training timeline after the answering modem goes off hook (both ends run it). */
  train(role) {
    const rate = this.rate, ans = answerToneMs(rate), hs = handshakeMs(rate), mod = modulation(rate);
    if (ans) {
      this.after(T_BILLING, () => this.sound('answer', { on: true }), 'call');
      this.after(T_BILLING + ans, () => { this.sound('answer', { on: false }); this.sound('handshake', { ms: hs, rate, mod, role }); }, 'call');
    } else {
      this.after(T_BILLING, () => this.sound('handshake', { ms: hs, rate, mod, role }), 'call');
    }
    this.after(T_BILLING + ans + hs, () => this.carrierUp(), 'call');
  }

  // ---------------------------------------------------------------- connected
  carrierUp() {
    if (!this.leg || !this.leg.open) { this.hangup(); this.result('NO CARRIER'); return; }
    this.cancel('call');
    this.carrier = true; this.state = 'connected';
    this.S[1] = 0;
    this.result(this.connectText());
    this.online = true;
    if (this.spkMode === 2) this.sound('carrier', { on: true, rate: this.rate }); else this.sound('hush');
    this.lines(); this.changed();
    this.kick();
  }
  dataIn(bytes) {
    this.rxBytes += bytes.length;          // the RD lamp: data arriving from the line
    for (const b of bytes) { this.rxq.push(b & 0xFF); this.rxPace.push(1); }
    this.kick();
    if (this.flow === 3) this.lines();
  }
  dataOut(b) {
    const now = this.now(), guard = this.S[12] * 20 * MS, esc = this.S[2];
    if (esc < 128 && b === esc && guard > 0) {
      if (this.escCount === 0 ? now - this.lastTxNs >= guard : (this.escCount < 3 && now - this.lastTxNs < guard)) {
        this.escCount++;
        if (this.escCount === 3) {
          const third = now;
          this.after(this.S[12] * 20, () => {
            if (this.escCount === 3 && this.lastTxNs === third && this.online) {
              this.escCount = 0; this.online = false; this.changed(); this.result('OK');
            }
          }, 'esc');
        }
      } else this.escCount = 0;
    } else this.escCount = 0;
    this.lastTxNs = now;
    this.leg?.send([b]);
    if (this.flow === 3) this.lines();
  }
  farHungUp(why) {
    const leg = this.leg;
    if (leg) leg.owner = null;
    this.leg = null;
    if (this.state === 'ringing') {       // the caller gave up before we answered
      this.cancelCall(); this.ri = false; this.state = 'idle'; this.lines(); this.changed();
      return;
    }
    if (this.carrier) {
      // carrier lost: S10 tenths of a second later we hang up
      this.after(this.S[10] * 100, () => {
        const pend = this.backlog();
        const finish = () => { this.hangup(); this.result('NO CARRIER'); };
        if (pend && this.online) this.after(pend * this.lineCharNs() / MS + 5, finish, 'call'); else finish();
      }, 'call');
      return;
    }
    this.hangup(); this.result('NO CARRIER');
  }
}
