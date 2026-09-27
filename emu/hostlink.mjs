// The Host Link at 555-0100 (docs/MODEM.md): a PhoneExchange endpoint that answers like a
// modem and offers the caller a tiny ANSI menu plus ZMODEM transfers to and from the
// visitor's real computer. Isomorphic: the page supplies the file picker and the download;
// node tests supply callbacks too.
//
//   const host = new HostLink({
//     pickFiles: async () => [{ name, data: Uint8Array, mtime }] | [],   // [D]ownload to ARM-DOS
//     saveFile: (file) => {},                                            // [U]pload from ARM-DOS
//     onStatus: (text) => {},                                            // what it is doing
//   });
//   exchange.register('555-0100', host);
//   setInterval(() => host.tick(performance.now()), 20);    // its clock: call tick() often
//
// It paces its output by the caller's modem buffer (about 1/4 s of data waiting), so
// the menu scrolls in at the line rate and a ZMODEM ZRPOS is answered promptly.

import { ZReceiver, ZSender, ZPAD, ZDLE, ZHEX } from './zmodem.mjs';
import { connectDelayMs } from './dev/modem.mjs';

const ESC = '\x1b[';
const enc = (s) => Array.from(s, (c) => c.charCodeAt(0) & 0xFF);

/** A DOS 8.3 name for a file from the visitor's computer. */
export function dosName(name) {
  const base = String(name).replace(/^.*[\\/]/, '').toUpperCase();
  const dot = base.lastIndexOf('.');
  const clean = (s, n) => s.replace(/[^A-Z0-9!#$%&'()\-@^_`{}~]/g, '_').slice(0, n);
  const stem = clean(dot > 0 ? base.slice(0, dot) : base, 8) || 'FILE';
  const ext = dot > 0 ? clean(base.slice(dot + 1), 3) : '';
  return ext ? `${stem}.${ext}` : stem;
}

export class HostLink {
  constructor({ pickFiles = async () => [], saveFile = () => {}, onStatus = () => {}, answerAfterMs = 2500, maxBacklog = 0 } = {}) {
    Object.assign(this, { pickFiles, saveFile, onStatus, answerAfterMs, maxBacklog });
    this.leg = null; this.state = 'idle'; this.now = 0; this.at = 0; this.out = [];
    this.zin = null; this.zout = null; this.inbuf = [];
    this.calls = 0; this.sent = []; this.received = [];
  }

  // ------------------------------------------------------------------ PhoneExchange endpoint
  incoming(leg) {
    if (this.leg) return 'busy';
    this.leg = leg;
    leg.owner = {
      onData: (bytes) => this.input(bytes),
      onHangup: () => this.ended('the caller hung up'),
    };
    this.state = 'ringing'; this.at = this.now + this.answerAfterMs;
    this.calls++;
    this.onStatus('ringing');
    return true;
  }
  ended(why) {
    this.zin = null; this.zout = null;
    this.leg = null; this.state = 'idle'; this.out = []; this.inbuf = [];
    this.onStatus('idle: ' + why);
  }
  hangup() { const l = this.leg; if (l) { this.leg = null; l.owner = null; l.hangup(); } this.ended('goodbye'); }

  // ------------------------------------------------------------------ the clock
  tick(now) {
    this.now = now;
    this.runWaits();
    switch (this.state) {
      case 'ringing':
        if (now >= this.at && this.leg) {
          const rate = this.leg.answer(56000);   // a V.90 "server": the caller's modem decides
          this.state = 'training'; this.at = now + connectDelayMs(rate) + 150;
          this.onStatus(`answered, training at ${rate}`);
        }
        break;
      case 'training':
        if (now >= this.at) { this.state = 'menu'; this.banner(); this.menu(); }
        break;
      case 'bye':
        if (!this.out.length && this.leg && this.leg.remoteBacklog() === 0 && now >= this.at) this.hangup();
        break;
    }
    if (this.zin) this.zin.tick(now);
    if (this.zout) this.zout.tick(now);
    this.pump();
  }
  pump() {
    const leg = this.leg;
    if (!leg || !leg.connected) return;
    // keep about a quarter of a second of data waiting in the caller's modem: enough to keep
    // the line busy between ticks, little enough that a ZRPOS / ZACK round trip is quick
    let room = (this.maxBacklog || Math.max(48, (leg.txRate || leg.rate) / 40)) - leg.remoteBacklog();
    if (room <= 0) return;
    if (this.out.length) {
      const n = Math.min(room, this.out.length);
      leg.send(this.out.splice(0, n));
      room -= n;
    }
    if (room > 0 && this.zout && !this.out.length) {
      const chunk = this.zout.produce(room);
      if (chunk.length) leg.send(chunk);
    }
  }
  print(s) { for (const b of typeof s === 'string' ? enc(s) : s) this.out.push(b); }

  // ------------------------------------------------------------------ screens
  banner() {
    const w = 66, line = '\xCD'.repeat(w);   // wide enough for the longest row: padEnd never truncates
    this.print(`${ESC}0m${ESC}2J${ESC}H\r\n`);
    this.print(`${ESC}1;36m\xC9${line}\xBB\r\n`);
    const row = (t, a = '1;37') => this.print(`${ESC}1;36m\xBA${ESC}${a}m${t.padEnd(w)}${ESC}1;36m\xBA\r\n`);
    row('  ARM-DOS HOST LINK', '1;33');
    row('  The line between this 1988 PC and the computer you are using', '0;37');
    this.print(`${ESC}1;36m\xC8${line}\xBC${ESC}0m\r\n`);
  }
  menu() {
    this.state = 'menu';
    this.print(`\r\n${ESC}1;37m [${ESC}1;33mD${ESC}1;37m]${ESC}0m Download a file from your computer to ARM-DOS\r\n`);
    this.print(`${ESC}1;37m [${ESC}1;33mU${ESC}1;37m]${ESC}0m Upload a file from ARM-DOS to your computer\r\n`);
    this.print(`${ESC}1;37m [${ESC}1;33mG${ESC}1;37m]${ESC}0m Goodbye\r\n\r\n`);
    this.print(`${ESC}1;32mYour choice: ${ESC}0m`);
    this.onStatus('menu');
  }

  // ------------------------------------------------------------------ input
  input(bytes) {
    if (this.zin) { this.zin.feed(bytes, this.now); return; }
    if (this.zout) { this.zout.feed(bytes, this.now); return; }
    if (this.state !== 'menu') return;
    for (const b of bytes) {
      this.inbuf.push(b); if (this.inbuf.length > 8) this.inbuf.shift();
      // a ZMODEM sender started without asking first ("**" ZDLE "B00" = ZRQINIT)
      const k = this.inbuf.length;
      if (k >= 6 && this.inbuf[k - 6] === ZPAD && this.inbuf[k - 5] === ZPAD && this.inbuf[k - 4] === ZDLE && this.inbuf[k - 3] === ZHEX && this.inbuf[k - 2] === 0x30 && this.inbuf[k - 1] === 0x30) {
        this.print('U\r\n'); this.startUpload(true); return;
      }
      const c = String.fromCharCode(b).toUpperCase();
      if (c === 'D') { this.print('D\r\n'); this.startDownload(); return; }
      if (c === 'U') { this.print('U\r\n'); this.startUpload(false); return; }
      if (c === 'G') { this.print('G\r\n'); this.goodbye(); return; }
      if (c === '?' || c === 'M') { this.print('\r\n'); this.menu(); return; }
    }
  }
  goodbye() {
    this.print(`\r\n${ESC}1;36mThanks for calling the ARM-DOS Host Link. Goodbye!${ESC}0m\r\n`);
    this.state = 'bye'; this.at = this.now + 500;
  }

  // ------------------------------------------------------------------ [D]ownload (host -> ARM-DOS)
  async startDownload() {
    this.state = 'picking';
    this.print(`\r\n${ESC}1;37mChoose the file(s) in your browser's file window...${ESC}0m\r\n`);
    this.onStatus('picking files');
    let files = [];
    try { files = (await this.pickFiles()) || []; } catch { files = []; }
    if (!this.leg) return;
    if (!files.length) { this.print('\r\nNo file chosen.\r\n'); this.menu(); return; }
    files = files.map((f) => ({ name: dosName(f.name), data: f.data, mtime: f.mtime || Math.floor(Date.now() / 1000) }));
    for (const f of files) this.print(`  ${f.name.padEnd(13)}${String(f.data.length).padStart(9)} bytes\r\n`);
    const total = files.reduce((a, f) => a + f.data.length, 0);
    const bps = this.leg.txRate || this.leg.rate;
    const secs = Math.ceil(total / (bps / 10));
    this.print(`\r\nSending with ZMODEM: about ${secs < 90 ? secs + ' seconds' : Math.round(secs / 60) + ' minutes'} at ${bps} bps.\r\n`);
    this.print(`Start your ZMODEM download now (TERM starts it by itself).\r\n`);
    this.state = 'zsend';
    this.onStatus(`sending ${files.map((f) => f.name).join(', ')}`);
    this.zout = new ZSender({
      files,
      send: (b) => this.leg?.send(b),
      onProgress: (p) => this.onStatus(`sending ${p.name}: ${p.pos} / ${p.size}`),
      onDone: (ok, why) => {
        this.zout = null;
        if (ok) this.sent.push(...files.map((f) => f.name));
        this.after(500, () => {
          this.print(ok ? `\r\n${ESC}1;32mTransfer complete.${ESC}0m\r\n` : `\r\n${ESC}1;31mTransfer failed (${why}).${ESC}0m\r\n`);
          this.menu();
        });
      },
    });
    this.flushThen(() => this.zout?.start(this.now));
  }

  // ------------------------------------------------------------------ [U]pload (ARM-DOS -> host)
  startUpload(already) {
    this.state = 'zrecv';
    if (!already) this.print(`\r\n${ESC}1;37mStart your ZMODEM upload now (in TERM: PgUp, then Z).${ESC}0m\r\n`);
    this.onStatus('waiting for an upload');
    this.zin = new ZReceiver({
      send: (b) => this.leg?.send(b),
      onStart: (f) => this.onStatus(`receiving ${f.name} (${f.size} bytes)`),
      onProgress: (p) => this.onStatus(`receiving ${p.name}: ${p.pos} / ${p.size}`),
      onFile: (f) => { this.received.push(f); try { this.saveFile(f); } catch (e) { console.error(e); } },
      onDone: (ok, why) => {
        const n = this.zin ? this.zin.files.length : 0;
        this.zin = null;
        this.after(800, () => {
          this.print(ok || n ? `\r\n${ESC}1;32m${n} file(s) received - your browser saves them.${ESC}0m\r\n` : `\r\n${ESC}1;31mNo upload (${why}).${ESC}0m\r\n`);
          this.menu();
        });
      },
    });
    const z = this.zin;
    this.flushThen(() => z.start(this.now));      // our ZRINIT answers the sender's ZRQINIT
  }

  // ------------------------------------------------------------------ small helpers
  after(ms, fn) { const t = this.now + ms; const chk = () => { if (this.now >= t) fn(); else this.waits.push(chk); }; (this.waits ||= []).push(chk); }
  flushThen(fn) { const chk = () => { if (!this.out.length) fn(); else (this.waits ||= []).push(chk); }; (this.waits ||= []).push(chk); }
  runWaits() { const w = this.waits || []; this.waits = []; for (const f of w) f(); }
}
