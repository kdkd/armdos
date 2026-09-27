// ARM-DOS Online at 555-0199: a PhoneExchange endpoint (like the Host Link, emu/hostlink.mjs)
// that answers like a modem, speaks the frame protocol of protocol.mjs with ONLINE.EXE
// (apps/online) and fetches what the caller asks for from the real Internet: Wikipedia,
// Wiktionary, Open-Meteo, Hacker News (channels.mjs). Pictures are converted to
// interlaced 256-colour GIFs in the page (picture.mjs) and sent down the line, which the
// caller's modem paces at the line rate.
//
//   const online = new OnlineService({ fetch, decodeImage, onStatus });
//   exchange.register(ONLINE_NUMBER, online);
//   setInterval(() => online.tick(performance.now()), 20);   // its clock
//
// Node tests pass a fixture `fetch` (canned responses, no network), a `decodeImage` for
// their image fixtures and a fixed `now` (wall-clock milliseconds, for "3 hours ago").

import { connectDelayMs } from '../dev/modem.mjs';
import { frame, FrameReader, bin, u16, u32 } from './protocol.mjs';
import { Fetcher, ServiceError } from './net.mjs';
import * as ch from './channels.mjs';
import { makeGif, browserDecode } from './picture.mjs';
import { toCP437 } from './cp437.mjs';

export const ONLINE_NUMBER = '555-0199';
const DATA_CHUNK = 128, TEXT_CHUNK = 256;

export class OnlineService {
  constructor({ fetch, decodeImage = typeof createImageBitmap === 'function' ? browserDecode : null, now = () => Date.now(),
    onStatus = () => {}, answerAfterMs = 2500, maxBacklog = 0, fetcher = null, hostName = 'EUROPA-1' } = {}) {
    this.net = fetcher || new Fetcher({ fetch, now });
    Object.assign(this, { decodeImage, onStatus, answerAfterMs, maxBacklog, hostName });
    this.leg = null; this.state = 'idle'; this.now = 0; this.at = 0;
    this.calls = 0; this.gifCache = new Map();
    this.log = [];                                   // requests served (tests)
    this.reset();
  }
  reset() {
    this.out = []; this.outPos = 0;                 // frames waiting; bytes of out[0] already sent
    this.reader = new FrameReader((t, p) => this.onFrame(t, p), { onJunk: (b) => this.junk(b) });
    this.docs = new Map(); this.nextDoc = 1;
    this.gen = 0; this.chain = Promise.resolve();
    this.member = ''; this.signedOn = false; this.human = false;
  }

  // ------------------------------------------------------------------ PhoneExchange endpoint
  incoming(leg) {
    if (this.leg) return 'busy';
    this.leg = leg;
    leg.owner = { onData: (b) => this.reader.feed(b), onHangup: () => this.ended('the caller hung up') };
    this.state = 'ringing'; this.at = this.now + this.answerAfterMs;
    this.calls++;
    this.onStatus('ringing');
    return true;
  }
  ended(why) {
    this.leg = null; this.state = 'idle'; this.gen++;
    this.reset();
    this.onStatus('idle: ' + why);
  }
  hangup() { const l = this.leg; if (l) { this.leg = null; l.owner = null; l.hangup(); } this.ended('hung up'); }

  tick(now) {
    this.now = now;
    switch (this.state) {
      case 'ringing':
        if (now >= this.at && this.leg) {
          const rate = this.leg.answer(56000);            // a V.90 "server": the caller's switch decides
          this.state = 'training'; this.at = now + connectDelayMs(rate) + 150;
          this.onStatus(`answered, training at ${rate}`);
        }
        break;
      case 'training':
        if (now >= this.at) {
          this.state = 'online';
          this.text(`\r\nARM-DOS Online  host ${this.hostName}\r\n`);
          this.onStatus('online');
        }
        break;
      case 'bye':
        if (!this.out.length && this.leg && this.leg.remoteBacklog() === 0 && now >= this.at) this.hangup();
        break;
    }
    this.pump();
  }
  pump() {
    const leg = this.leg;
    if (!leg || !leg.connected) return;
    let room = (this.maxBacklog || Math.max(48, (leg.txRate || leg.rate) / 40)) - leg.remoteBacklog();
    while (room > 0 && this.out.length) {
      const f = this.out[0];
      const n = Math.min(room, f.length - this.outPos);
      leg.send(f.subarray(this.outPos, this.outPos + n));
      this.outPos += n; room -= n;
      if (this.outPos >= f.length) { this.out.shift(); this.outPos = 0; }
    }
  }
  send(type, payload) { this.out.push(frame(type, payload)); }
  text(s) { this.out.push(Uint8Array.from(Array.from(toCP437(s), (c) => c.charCodeAt(0)))); }

  // ------------------------------------------------------------------ a person with a terminal
  junk(b) {
    if (this.signedOn || this.human || this.state !== 'online') return;
    if (b === 13 || b === 32 || b === 27) {
      this.human = true;
      this.text('\r\n\r\nThis is ARM-DOS Online, the information service for ARM-DOS computers.\r\n' +
        'It talks to its own program, not to a terminal: type ONLINE at the C:\\> prompt\r\n' +
        'and it dials this number for you. Goodbye!\r\n');
      this.state = 'bye'; this.at = this.now + 1500;
    }
  }

  // ------------------------------------------------------------------ requests
  onFrame(type, p) {
    // (the caller may say hello a moment before our own training period is over)
    if (this.state !== 'online' && this.state !== 'training') return;
    const s = bin(p);
    if (type === 'X') { this.cancel(); return; }
    if (type === 'H') {
      const [name] = s.split('\0');
      this.member = (name || 'Guest').replace(/[^\x20-\x7E]/g, '').slice(0, 16) || 'Guest';
      this.signedOn = true;
      this.onStatus('signed on: ' + this.member);
      this.send('W', `${this.member}\0` + toCP437(`Welcome to ARM-DOS Online, ${this.member}!\nYou are connected to host ${this.hostName}.`));
      return;
    }
    if (!this.signedOn) return;
    if (type === 'Q') {
      this.cancel(true);
      this.send('Q', toCP437(`Thank you for using ARM-DOS Online, ${this.member}. Goodbye!`));
      this.state = 'bye'; this.at = this.now + 500;
      return;
    }
    const gen = this.gen;
    this.chain = this.chain.then(() => (gen === this.gen ? this.serve(type, p, s, gen) : null)).catch((e) => console.error('ARM-DOS Online:', e));
  }
  cancel(quiet = false) {
    this.gen++;
    // keep the frame that is half on the line; drop the rest
    this.out = this.outPos ? [this.out[0]] : [];
    if (!quiet) this.send('Z', []);
  }

  async serve(type, p, s, gen) {
    const live = () => gen === this.gen && this.leg;
    let chan = 'E';
    try {
      switch (type) {
        case 'S': chan = 'E'; this.status('Searching the Encyclopedia...'); return this.sendDoc(await ch.search(this.net, s), gen);
        case 'A': chan = 'E'; this.status('Looking up the article...'); return this.sendDoc(await ch.article(this.net, s), gen);
        case 'R': {
          chan = 'E'; this.status('Picking an article at random...');
          const t = await ch.randomTitle(this.net);
          if (!live()) return;
          return this.sendDoc(await ch.article(this.net, t), gen);
        }
        case 'D': chan = 'D'; this.status('Opening the Dictionary...'); return this.sendDoc(await ch.define(this.net, s), gen);
        case 'W': chan = 'W'; this.status('Calling the Weather Center...'); return this.sendDoc(await ch.weatherSearch(this.net, s), gen);
        case 'N': chan = 'N'; this.status('Fetching the news wire...'); return this.sendDoc(await ch.news(this.net), gen);
        case 'T': {
          chan = 'T';
          const m = /^(\d\d)(\d\d)$/.exec(s);
          const d = new Date(this.net.now());
          const [mm, dd] = m ? [+m[1], +m[2]] : [d.getMonth() + 1, d.getDate()];
          this.status('Opening the history books...');
          return this.sendDoc(await ch.onThisDay(this.net, mm, dd), gen);
        }
        case 'F': {
          const id = p[0] | (p[1] << 8), n = p[2] | (p[3] << 8);
          const doc = this.docs.get(id);
          const target = doc?.links[n];
          if (!target) return this.message('I', 'That link has expired. Go back and try again.');
          return await this.follow(target, gen);
        }
        default:
          return this.message('E', 'ARM-DOS Online did not understand that request.');
      }
    } catch (e) {
      if (!live()) return;
      if (!(e instanceof ServiceError)) console.error('ARM-DOS Online:', e);
      const which = e.channel || chan;
      this.message('E', ch.UNAVAILABLE[which] + '\nPlease try again in a few minutes.\n(' + String(e.message || e).slice(0, 70) + ')');
    }
  }
  async follow(t, gen) {
    const channelOf = { A: 'E', S: 'E', D: 'D', W: 'W', N: 'N', P: 'P' };
    try {
      switch (t.kind) {
        case 'A': this.status('Looking up ' + t.title + '...'); return this.sendDoc(await ch.article(this.net, t.title), gen);
        case 'S': this.status('Searching the Encyclopedia...'); return this.sendDoc(await ch.search(this.net, t.query), gen);
        case 'D': this.status('Opening the Dictionary...'); return this.sendDoc(await ch.define(this.net, t.word), gen);
        case 'W': this.status('Calling the Weather Center...'); return this.sendDoc(await ch.weather(this.net, t.place), gen);
        case 'N': this.status('Fetching the story...'); return this.sendDoc(await ch.newsItem(this.net, t.id), gen);
        case 'P': return await this.picture(t, gen);
        case 'U': return this.message('I', 'That link leads out of ARM-DOS Online, to the World Wide Web:\n' + t.url.slice(0, 200) + '\nYou can visit it on the computer you are using right now.');
      }
    } catch (e) { e.channel = channelOf[t.kind]; throw e; }
    return this.message('I', 'That link goes nowhere.');
  }
  status(text) { this.send('S', toCP437(text)); }
  message(cls, text) { if (this.leg) this.send('R', cls + toCP437(text)); }

  sendDoc(doc, gen) {
    if (gen !== this.gen || !this.leg) return;
    const id = this.nextDoc++ & 0xFFFF || (this.nextDoc++ & 0xFFFF);
    this.docs.set(id, doc);
    if (this.docs.size > 40) this.docs.delete(this.docs.keys().next().value);
    const body = doc.encode();
    this.log.push({ kind: doc.kind, title: doc.title, bytes: body.length, links: doc.links.length - 1 });
    this.onStatus(`sending "${doc.title}" (${body.length} bytes)`);
    this.send('D', [...u16(id), doc.kind.charCodeAt(0), ...Array.from(toCP437(doc.title).slice(0, 70) + '\0' + doc.channel + '\0', (c) => c.charCodeAt(0))]);
    for (let i = 0; i < body.length; i += TEXT_CHUNK) this.send('T', body.slice(i, i + TEXT_CHUNK));
    this.send('E', [...u16(doc.lines.length), doc.truncated ? 1 : 0]);
  }

  async picture(t, gen) {
    if (!this.decodeImage) throw new ServiceError('no picture decoder here');
    this.status('Fetching the picture...');
    let g = this.gifCache.get(t.src);
    if (!g) {
      const bytes = await this.net.get(t.src, 'bytes');
      if (gen !== this.gen) return;
      this.status('Converting the picture to 256 colours...');
      const file = (t.file || '').replace(/^File:/, '');
      g = await makeGif(bytes, { decode: this.decodeImage, comment: `ARM-DOS Online. ${file} - from Wikimedia Commons, see its page for the licence.` });
      g.name = dosGifName(file || t.caption || 'PICTURE');
      this.gifCache.set(t.src, g);
      if (this.gifCache.size > 12) this.gifCache.delete(this.gifCache.keys().next().value);
    }
    if (gen !== this.gen || !this.leg) return;
    this.log.push({ kind: 'P', title: g.name, bytes: g.gif.length, width: g.width, height: g.height });
    this.onStatus(`sending ${g.name} (${g.gif.length} bytes, ${g.width}x${g.height})`);
    const cap = toCP437(t.caption || '').slice(0, 76);
    this.send('G', [...Array.from(g.name + '\0' + cap + '\0', (c) => c.charCodeAt(0)), ...u16(g.width), ...u16(g.height), ...u32(g.gif.length)]);
    for (let i = 0; i < g.gif.length; i += DATA_CHUNK) this.send('B', g.gif.subarray(i, i + DATA_CHUNK));
    this.send('F', []);
  }
}

/** "Acorn-ARM-Evaluation-System.jpg" -> "ACORNARM.GIF" */
export function dosGifName(s) {
  const base = String(s).replace(/\.[A-Za-z0-9]+$/, '').toUpperCase().replace(/[^A-Z0-9]/g, '');
  return (base.slice(0, 8) || 'PICTURE') + '.GIF';
}
