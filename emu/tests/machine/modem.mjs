#!/usr/bin/env node
// COM2 16550A + Hayes modem + PhoneExchange tests (docs/MODEM.md). Two machines whose CPUs
// sleep; the test plays "terminal program" by driving the UART ports directly, running both
// machines in lock step (each has its own emulated clock).
import { Machine } from '../../machine.mjs';
import { PhoneExchange } from '../../phone.mjs';

let fails = 0, passes = 0;
const ok = (name, cond, extra = '') => { if (cond) passes++; else { fails++; console.log(`FAIL ${name} ${extra}`); } };
const eq = (name, got, want) => ok(name, JSON.stringify(got) === JSON.stringify(want), `got ${JSON.stringify(got)} want ${JSON.stringify(want)}`);

function mk(o = {}) {
  const m = new Machine({ jit: false, rtcBaseMs: 0, ...o });
  m.cpu.halted = 1; m.cpu.i = 1;
  return m;
}
const B = 0x2F8;

/** A polled "terminal" on one machine's COM2. */
class Term {
  constructor(m, name) { this.m = m; this.name = name; this.out = []; this.rx = ''; this.sounds = []; this.irqs = 0; }
  init(divisor = 6) {   // 19200 bps DTE, 8N1, FIFO on, DTR+RTS+OUT2
    const m = this.m;
    m.out8(B + 3, 0x80); m.out8(B + 0, divisor & 0xFF); m.out8(B + 1, divisor >> 8); m.out8(B + 3, 0x03);
    m.out8(B + 2, 0xC7); m.out8(B + 4, 0x0B);
  }
  send(s) { for (const c of s) this.out.push(typeof c === 'number' ? c : c.charCodeAt(0)); }
  poll() {
    const m = this.m;
    if (this.out.length && (m.in8(B + 5) & 0x20)) for (let k = 0; k < 16 && this.out.length; k++) m.out8(B, this.out.shift());   // like an ISR: fill the FIFO on THRE
    while (m.in8(B + 5) & 1) this.rx += String.fromCharCode(m.in8(B));
  }
  take() { const r = this.rx; this.rx = ''; return r; }
  msr() { return this.m.in8(B + 6); }
}
function run(ms, terms, step = 2) {
  for (let t = 0; t < ms; t += step) for (const T of terms) { T.m.runFor(step); T.poll(); }
}
function until(cond, terms, maxMs = 60000, step = 2) {
  for (let t = 0; t < maxMs; t += step) { for (const T of terms) { T.m.runFor(step); T.poll(); } if (cond()) { run(12, terms, 1); return t; } }
  return -1;
}

// ------------------------------------------------------------------ UART registers
{
  const m = mk();
  eq('scratch', (m.out8(B + 7, 0x5A), m.in8(B + 7)), 0x5A);
  m.out8(B + 3, 0x80); m.out8(B, 0x60); m.out8(B + 1, 0x00); m.out8(B + 3, 0x03);
  eq('divisor 1200', Math.round(m.com2.baud()), 1200);
  eq('IIR no fifo, no int', m.in8(B + 2), 0x01);
  m.out8(B + 2, 0x07);
  eq('IIR fifo bits', m.in8(B + 2) & 0xC0, 0xC0);
  eq('LSR idle THRE+TEMT', m.in8(B + 5), 0x60);
  // loopback timing: 10 bits at 1200 baud = 8.33 ms
  m.out8(B + 4, 0x1B);   // loop + OUT2 + RTS + DTR
  eq('MSR loopback lines', m.in8(B + 6) & 0xF0, 0xB0);
  m.out8(B, 0x41);
  eq('THR taken, TEMT clear', m.in8(B + 5) & 0x60, 0x20);
  m.runFor(5);
  eq('not yet received after 5 ms', m.in8(B + 5) & 1, 0);
  m.runFor(4);
  eq('received after 9 ms', [m.in8(B + 5) & 0x61, m.in8(B)], [0x61, 0x41]);
  // FIFO: 16 bytes queue, trigger level 8 with IER RX
  m.out8(B + 2, 0x87); m.out8(B + 1, 0x01); m.out8(0x21, 0x00);
  for (let i = 0; i < 10; i++) m.out8(B, 0x30 + i);
  eq('tx fifo accepts 10', m.in8(B + 5) & 0x20, 0);
  m.runFor(8.34 * 7 + 1);
  eq('7 received, below trigger: no RX int yet', m.in8(B + 2) & 0x0F, 0x01);
  m.runFor(8.34 + 0.2);
  eq('8 received: RX data int', m.in8(B + 2) & 0x0F, 0x04);
  let s = ''; while (m.in8(B + 5) & 1) s += String.fromCharCode(m.in8(B));
  eq('fifo order', s.length >= 8 && s.startsWith('01234567'), true);
  m.runFor(60);
  s = ''; while (m.in8(B + 5) & 1) s += String.fromCharCode(m.in8(B));
  eq('rest arrives', s.endsWith('89'), true);
  // character timeout: 1 byte below trigger -> IIR 0x0C after 4 char times
  m.out8(B, 0x5A); m.runFor(9);
  eq('byte waits below trigger', m.in8(B + 2) & 0x0F, 0x01);
  m.runFor(8.34 * 4 + 1);
  eq('char timeout int', m.in8(B + 2) & 0x0F, 0x0C);
  m.in8(B);
  eq('timeout cleared by read', m.in8(B + 2) & 0x0F, 0x01);
  // THRE interrupt
  m.out8(B + 1, 0x02);
  eq('THRE int on enable', m.in8(B + 2) & 0x0F, 0x02);
  eq('THRE cleared by IIR read', m.in8(B + 2) & 0x0F, 0x01);
  // overrun in non-FIFO mode
  m.out8(B + 1, 0); m.out8(B + 2, 0x00);
  m.out8(B, 1); m.runFor(9); m.out8(B, 2); m.runFor(9);
  eq('overrun sets OE', m.in8(B + 5) & 0x03, 0x03);
  eq('OE cleared by LSR read', m.in8(B + 5) & 0x02, 0);
  // MSR deltas (loopback)
  m.in8(B + 6);
  m.out8(B + 4, 0x1B & ~2);
  eq('DCTS on RTS drop', m.in8(B + 6) & 0x11, 0x01);
  m.out8(B + 4, 0x1F); m.in8(B + 6); m.out8(B + 4, 0x1B);
  eq('TERI on OUT1 falling', m.in8(B + 6) & 0x04, 0x04);
  // OUT2 gates the IRQ
  m.out8(B + 4, 0x03); m.out8(B + 1, 0x02); m.pic.lower(3);
  eq('no IRQ without OUT2', (m.pic.irr >> 3) & 1, 0);
  m.out8(B + 4, 0x0B);
  eq('IRQ with OUT2', (m.pic.irr >> 3) & 1, 1);
}

// ------------------------------------------------------------------ AT command set, single modem
{
  const ex = new PhoneExchange();
  const m = mk({ phone: ex, phoneNumber: '555-2000' });
  const T = new Term(m, 'A'); T.init();
  T.send('AT\r'); run(30, [T]);
  eq('AT -> OK (echo on)', T.take(), 'AT\r\r\nOK\r\n');
  T.send('ATE0V0\r'); run(30, [T]);
  eq('ATE0V0 numeric', T.take(), 'ATE0V0\r0\r');
  T.send('ATQ9\r'); run(30, [T]);
  eq('ERROR numeric', T.take(), '4\r');
  T.send('ATV1S7=45S7?\r'); run(30, [T]);
  eq('S7=45, S7?', T.take(), '\r\n045\r\n\r\nOK\r\n');
  T.send('ATI3\r'); run(60, [T]);
  ok('ATI3', T.take().includes('Internal 2400'));
  T.send('AT&B9600S37?\r'); run(30, [T]);
  eq('AT&B9600 -> S37=9', T.take(), '\r\n009\r\n\r\nOK\r\n');
  T.send('AT&V\r'); run(200, [T]);
  ok('AT&V profile', /ACTIVE PROFILE:[\s\S]*&B9600/.test(T.take()));
  T.send('ATZ\r'); run(30, [T]);
  eq('ATZ', T.take(), '\r\nOK\r\n');
  T.send('A/'); run(30, [T]);
  eq('A/ repeats (echo back on after Z)', T.take(), 'A/\r\nOK\r\n');
  T.send('ATK\r'); run(30, [T]);
  eq('bad command', T.take(), 'ATK\r\r\nERROR\r\n');
  T.send('ATE0\r'); run(30, [T]); T.take();
  // no exchange -> NO DIALTONE
  const lone = mk(); const L = new Term(lone, 'L'); L.init();
  L.send('ATE0\r'); run(30, [L]); L.take(); L.send('ATDT5551234\r'); run(3000, [L]); L.take = ((f) => () => f().replace('ATDT5551234\r', ''))(L.take.bind(L));
  eq('no phone line: NO DIALTONE', L.take(), '\r\nNO DIALTONE\r\n');
  // unknown number: rings, NO ANSWER after S7
  T.send('ATS7=12DT555-4444\r');
  const tNA = until(() => T.rx.includes('NO ANSWER'), [T], 20000);
  ok('unknown number -> NO ANSWER', tNA > 11000 && tNA < 14000, `t=${tNA}`);
  T.take();
  // busy number
  T.send('ATS7=40DT867-5309\r');
  const tB = until(() => T.rx.includes('BUSY'), [T], 20000);
  ok('867-5309 -> BUSY', tB > 1000 && tB < 12000, `t=${tB}`);
  T.take();
  // abort a dial with a keypress
  T.send('ATDT5554444\r'); run(1500, [T]); T.send('x'); run(100, [T]);
  eq('keypress aborts dialling', T.take(), '\r\nNO CARRIER\r\n');
  eq('back on hook', m.modem.hook, false);
}

// ------------------------------------------------------------------ two machines, a call
{
  const ex = new PhoneExchange();
  const events = [];
  ex.on((e) => events.push(e.type));
  const sndA = [];
  const a = mk({ phone: ex, phoneNumber: '555-1000', onModemSound: (e) => sndA.push(e) });
  const b = mk({ phone: ex, phoneNumber: '555-1989' });
  const A = new Term(a, 'A'), Bt = new Term(b, 'B');
  A.init(); Bt.init();
  Bt.send('ATE0S0=2\r'); run(50, [A, Bt]); eq('callee setup', Bt.take(), 'ATE0S0=2\r\r\nOK\r\n');
  A.send('ATE0\r'); run(30, [A]); A.take();
  A.send('ATM1L2DT555-1989\r');
  // RING / RI on the callee
  let riSeen = false, teri = false;
  const tRing = until(() => { const v = Bt.msr(); if (v & 0x40) riSeen = true; if (v & 0x04) teri = true; return Bt.rx.includes('RING'); }, [A, Bt], 20000);
  ok('callee gets RING', tRing > 0, `t=${tRing}`);
  ok('RI high while ringing', riSeen);
  const tConn = until(() => { if (Bt.msr() & 0x04) teri = true; return A.rx.includes('CONNECT') && Bt.rx.includes('CONNECT'); }, [A, Bt], 30000);
  ok('both CONNECT', tConn > 0, `A=${JSON.stringify(A.rx)} B=${JSON.stringify(Bt.rx)}`);
  ok('callee answered after 2 rings (TERI seen)', (Bt.rx.match(/RING/g) || []).length === 2 && teri, JSON.stringify(Bt.rx));
  eq('caller result', A.take(), '\r\nCONNECT 2400\r\n');
  eq('callee result', Bt.take(), '\r\nRING\r\n\r\nRING\r\n\r\nCONNECT 2400\r\n');
  eq('DCD on both', [(A.msr() & 0x80) !== 0, (Bt.msr() & 0x80) !== 0], [true, true]);
  eq('panel', [a.modem.panel().oh, a.modem.panel().cd, a.modem.panel().hs], [true, true, true]);
  const kinds = [...new Set(sndA.map((e) => e.kind))];
  ok('caller sounds', ['relay', 'dialtone', 'dtmf', 'ringback', 'answer', 'handshake', 'hush'].every((k) => kinds.includes(k)), JSON.stringify(kinds));
  eq('DTMF digits', sndA.filter((e) => e.kind === 'dtmf').map((e) => e.digit).join(''), '5551989');
  // 2400 bps pacing: 480 bytes should take ~2 s
  const msg = 'The quick brown fox jumps over the lazy dog. '.repeat(11).slice(0, 480);
  A.send(msg);
  const t0 = a.timeMs();
  const tData = until(() => Bt.rx.length >= 480, [A, Bt], 10000, 1);
  const took = a.timeMs() - t0;
  ok('480 bytes at 2400 bps take ~2 s', took > 1950 && took < 2200, `took ${took.toFixed(0)} ms`);
  eq('data intact A->B', Bt.take(), msg);
  Bt.send('Hello from the ARM Pit\r\n');
  until(() => A.rx.includes('\n'), [A, Bt], 2000, 1);
  eq('data B->A', A.take(), 'Hello from the ARM Pit\r\n');
  // +++ escape with guard time, then ATO back online
  run(1100, [A, Bt]); A.send('+++'); run(1300, [A, Bt]);
  eq('+++ -> OK', A.take(), '\r\nOK\r\n');
  eq('+++ also reached the far end', Bt.take(), '+++');
  Bt.send('held'); run(100, [A, Bt]);
  eq('data held in online command mode', A.take(), '');
  A.send('ATO\r'); run(200, [A, Bt]);
  eq('ATO -> CONNECT then held data', A.take(), '\r\nCONNECT 2400\r\nheld');
  // "+++" inside data (no guard) does not escape
  A.send('a+++b'); run(1500, [A, Bt]);
  eq('+++ without guard is data', [A.take(), Bt.take()], ['', 'a+++b']);
  // hang up from A with ATH; B gets NO CARRIER
  run(1100, [A, Bt]); A.send('+++'); run(1300, [A, Bt]); A.take(); Bt.take();
  A.send('ATH\r');
  until(() => Bt.rx.includes('NO CARRIER'), [A, Bt], 5000);
  eq('ATH -> OK', A.take(), '\r\nOK\r\n');
  eq('far end NO CARRIER', Bt.take(), '\r\nNO CARRIER\r\n');
  eq('DCD dropped', [(A.msr() & 0x80) !== 0, (Bt.msr() & 0x80) !== 0], [false, false]);
  ok('exchange events', events.join(',').startsWith('dial,answer,hangup'), events.join(','));

  // ATZ while the phone rings does not stop it ringing (the BBS resets its modem at start-up)
  Bt.send('ATS0=0\r'); run(50, [A, Bt]); Bt.take();
  A.send('ATDT5551989\r');
  until(() => Bt.rx.includes('RING'), [A, Bt], 10000); Bt.take();
  Bt.send('ATZ\r'); run(100, [A, Bt]);
  eq('ATZ while ringing: OK', Bt.take(), '\r\nOK\r\n');
  Bt.send('ATE0\r'); run(50, [A, Bt]); Bt.take();
  until(() => Bt.rx.includes('RING'), [A, Bt], 8000);
  ok('still ringing after ATZ', Bt.rx.includes('RING'));
  Bt.send('ATA\r');
  until(() => A.rx.includes('CONNECT') && Bt.rx.includes('CONNECT'), [A, Bt], 20000);
  ok('answered after ATZ', A.take().includes('CONNECT 2400'));
  run(1100, [A, Bt]); A.send('+++'); run(1300, [A, Bt]); A.send('ATH\r');
  until(() => Bt.rx.includes('NO CARRIER'), [A, Bt], 5000); A.take(); Bt.take();

  // second call: B answers manually with ATA, B hangs up by dropping DTR (&D2)
  Bt.send('AT&B9600\r'); run(50, [A, Bt]); Bt.take();     // the slower side wins: both ask for 9600
  A.send('AT&B9600DT5551989\r');
  until(() => Bt.rx.includes('RING'), [A, Bt], 10000);
  Bt.send('ATA\r');
  until(() => A.rx.includes('CONNECT') && Bt.rx.includes('CONNECT'), [A, Bt], 20000);
  eq('9600 call', [A.take(), Bt.take().replace(/\r\nRING\r\n/g, '')], ['\r\nCONNECT 9600\r\n', '\r\nCONNECT 9600\r\n']);
  const big = 'x'.repeat(960);
  const t1 = b.timeMs();
  Bt.send(big);
  until(() => A.rx.length >= 960, [A, Bt], 5000, 1);
  const took2 = b.timeMs() - t1;
  ok('960 bytes at 9600 take ~1 s', took2 > 950 && took2 < 1150, `took ${took2.toFixed(0)}`);
  A.take();
  b.out8(B + 4, 0x0A);    // drop DTR
  until(() => A.rx.includes('NO CARRIER'), [A, Bt], 5000);
  eq('DTR drop hangs up the callee', b.modem.hook, false);
  eq('caller sees NO CARRIER', A.take(), '\r\nNO CARRIER\r\n');
  // ringing with DTR low and &D2: no auto-answer
  Bt.take(); b.out8(B + 4, 0x0B);
  Bt.send('ATS0=1\r'); run(50, [A, Bt]); Bt.take();
  b.out8(B + 4, 0x0A);
  A.send('ATS7=15DT5551989\r');
  until(() => A.rx.includes('NO ANSWER'), [A, Bt], 20000);
  ok('no auto-answer with DTR low (&D2)', A.take().includes('NO ANSWER') && b.modem.hook === false);
  // callee busy while it is in a call: a third machine calls B while A is connected to it
  b.out8(B + 4, 0x0B);
  run(8000, [A, Bt]); A.take(); Bt.take();
  A.send('ATS7=30DT5551989\r');
  until(() => A.rx.includes('CONNECT'), [A, Bt], 20000); A.take(); Bt.take();
  const c = mk({ phone: ex }); const C = new Term(c, 'C'); C.init();
  C.send('ATE0\r'); run(30, [C]); C.take();
  C.send('ATDT5551989\r');
  until(() => C.rx.includes('BUSY'), [A, Bt, C], 20000);
  eq('line in use -> BUSY', C.take(), '\r\nBUSY\r\n');
  // caller hangs up (DTR drop): callee gets NO CARRIER after S10
  a.out8(B + 4, 0x0A);
  const tNC = until(() => Bt.rx.includes('NO CARRIER'), [A, Bt], 5000, 1);
  ok('callee NO CARRIER after ~S10 (1.4 s)', tNC >= 1300 && tNC < 1700, `t=${tNC}`);
  // machine reset hangs the modem up
  a.out8(B + 4, 0x0B); Bt.take(); A.take();
  A.send('ATDT5551989\r');
  until(() => A.rx.includes('CONNECT'), [A, Bt], 20000);
  a.reset(); A.init();
  until(() => Bt.rx.includes('NO CARRIER'), [A, Bt], 5000);
  ok('reset of the caller drops the call', Bt.rx.includes('NO CARRIER') && !a.modem.hook);
}

// ------------------------------------------------------------------ every speed of the switch
{
  const cases = [
    { sw: 2400, plain: 'CONNECT 2400', ext: 'CONNECT 2400/V22BIS', down: 2400, up: 2400, mod: 'V22BIS' },
    { sw: 14400, plain: 'CONNECT 14400', ext: 'CONNECT 14400/V32BIS', down: 14400, up: 14400, mod: 'V32BIS' },
    { sw: 33600, plain: 'CONNECT 33600', ext: 'CONNECT 33600/V34', down: 33600, up: 33600, mod: 'V34' },
    { sw: 56000, plain: 'CONNECT 56000', ext: 'CONNECT 53333/V90', down: 53333, up: 31200, mod: 'V90' },
  ];
  for (const c of cases) {
    for (const ext of [false, true]) {
      const ex = new PhoneExchange();
      const snd = [];
      const a = mk({ phone: ex, phoneNumber: '555-1000', modemRate: c.sw, onModemSound: (e) => snd.push(e) });
      const b = mk({ phone: ex, phoneNumber: '555-1989', modemRate: 56000 });
      // a fast modem wants a fast port: DTE locked at 115200 (divisor 1)
      const A = new Term(a, 'A'), Bt = new Term(b, 'B'); A.init(1); Bt.init(1);
      Bt.send('ATE0S0=1\r'); A.send(`ATE0${ext ? 'W2' : ''}\r`); run(50, [A, Bt]); A.take(); Bt.take();
      A.send('ATDT5551989\r');
      const t0 = a.timeMs();
      until(() => /CONNECT[^\r]*\r\n/.test(A.rx) && /CONNECT/.test(Bt.rx), [A, Bt], 60000);
      const tc = a.timeMs() - t0;
      eq(`${c.sw}${ext ? ' W2' : ''}: caller result`, A.take().trim(), ext ? c.ext : c.plain);
      Bt.take();
      if (ext) continue;
      const hs = snd.find((e) => e.kind === 'handshake');
      eq(`${c.sw}: modulation ${c.mod}`, hs && hs.mod, c.mod);
      ok(`${c.sw}: connect time plausible`, c.sw <= 14400 ? tc < 12000 : c.sw === 33600 ? tc > 11000 && tc < 17000 : tc > 19000 && tc < 25000, `${tc.toFixed(0)} ms`);
      // throughput both ways: 3 s worth of line time each
      const nDown = Math.round(c.down / 10 * 3), nUp = Math.round(c.up / 10 * 3);
      const tb = b.timeMs();
      Bt.send('d'.repeat(nDown)); A.send('u'.repeat(nUp));
      let tDown = 0, tUp = 0;
      until(() => { if (!tDown && A.rx.length >= nDown) tDown = b.timeMs() - tb; if (!tUp && Bt.rx.length >= nUp) tUp = b.timeMs() - tb; return tDown && tUp; }, [A, Bt], 20000, 1);
      ok(`${c.sw}: downstream ${c.down} bps (3 s)`, tDown > 2850 && tDown < 3250, `${tDown.toFixed(0)} ms`);
      ok(`${c.sw}: upstream ${c.up} bps (3 s)`, tUp > 2850 && tUp < 3250, `${tUp.toFixed(0)} ms`);
      eq(`${c.sw}: data intact`, [A.take() === 'd'.repeat(nDown), Bt.take() === 'u'.repeat(nUp)], [true, true]);
    }
  }
  // the slower side wins; AT&B / S37 / +MS override per call
  const ex = new PhoneExchange();
  const a = mk({ phone: ex, phoneNumber: '555-1000', modemRate: 56000 });
  const b = mk({ phone: ex, phoneNumber: '555-1989', modemRate: 14400 });
  const A = new Term(a, 'A'), Bt = new Term(b, 'B'); A.init(1); Bt.init(1);
  Bt.send('ATE0S0=1\r'); A.send('ATE0\r'); run(50, [A, Bt]); A.take(); Bt.take();
  const call = (dial) => { A.send(dial); until(() => /CONNECT[^\r]*\r\n/.test(A.rx), [A, Bt], 60000); const r = A.take().trim(); run(1100, [A, Bt]); A.send('+++'); run(1300, [A, Bt]); A.send('ATH\r'); until(() => Bt.rx.includes('NO CARRIER'), [A, Bt], 5000); run(100, [A, Bt]); A.take(); Bt.take(); return r; };
  eq('56K caller, 14400 callee -> 14400', call('ATDT5551989\r'), 'CONNECT 14400');
  eq('AT+MS=V22B', call('AT+MS=V22B;DT5551989\r'), 'CONNECT 2400');
  eq('AT+MS=V34,1,300,9600', call('AT+MS=V34,1,300,9600;DT5551989\r'), 'CONNECT 9600');
  eq('ATS37=5', call('ATS37=5DT5551989\r'), 'CONNECT 1200');
  A.send('AT+MS?\r'); run(50, [A, Bt]);
  eq('AT+MS?', A.take(), '\r\n+MS: 1,1,300,1200\r\n\r\nOK\r\n');
  A.send('ATS37=0&V\r'); run(300, [A, Bt]);
  ok('&V shows the switch', /SPEED SWITCH: 56000  MODULATION: V\.90/.test(A.take()));
}

console.log(`modem: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
