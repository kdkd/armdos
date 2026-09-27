// apps/online/tests/service.mjs - the ARM-DOS Online endpoint (emu/online/) on its own, in
// fixture mode (canned responses, no network), driven by a JS client that speaks the
// frame protocol: sign on, every channel, links, a picture (the GIF is decoded and checked),
// cancel, sign off, a person with a terminal, and a service that is down.
import fs from 'node:fs';
import path from 'node:path';
import { OnlineService } from '../../../emu/online/service.mjs';
import { frame, FrameReader, bin } from '../../../emu/online/protocol.mjs';
import { decodeGif } from '../../../emu/online/picture.mjs';
import { fixtureFetch, decodePPM, RECORDED_AT } from './fixtures.mjs';
import { toCP437, fromCP437 } from '../../../emu/online/cp437.mjs';

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; return ok; };
const OUT = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../../../build/online-test');
fs.mkdirSync(OUT, { recursive: true });

function makeCall(svc, { rate = 14400 } = {}) {
  const got = [];                 // frames {t, p}
  let text = '';
  const reader = new FrameReader((t, p) => got.push({ t, p }), { onJunk: (b) => { text += String.fromCharCode(b); } });
  const leg = {
    call: { rate }, open: true, rate, txRate: rate, connected: false, owner: null, hungUp: false,
    answer(max) { this.connected = true; return Math.min(rate, max); },
    send(b) { reader.feed(b); },
    remoteBacklog: () => 0,
    hangup() { this.hungUp = true; this.connected = false; },
  };
  let now = svc.now || 0;
  const run = async (ms) => { for (let t = 0; t < ms; t += 20) { now += 20; svc.tick(now); if (t % 200 === 0) await new Promise((r) => setImmediate(r)); } };
  const tx = (type, payload = []) => leg.owner.onData(frame(type, payload));
  check(svc.incoming(leg) === true, 'incoming call accepted');
  return { got, leg, run, tx, text: () => text, clear: () => { got.length = 0; } };
}
const docOf = (frames) => {
  const d = frames.find((f) => f.t === 'D');
  if (!d) return null;
  const id = d.p[0] | (d.p[1] << 8), kind = String.fromCharCode(d.p[2]);
  const [title, channel] = bin(d.p.subarray(3)).split('\0');
  const body = frames.filter((f) => f.t === 'T').map((f) => bin(f.p)).join('');
  const end = frames.find((f) => f.t === 'E');
  const plain = body.split('\n').map((l) => l.replace(/\x01..|\x02|\x03./g, ''));
  return { id, kind, title, channel, body, plain, text: plain.join('\n'), ended: !!end };
};
async function until(c, pred, ms = 20000) { for (let t = 0; t < ms; t += 200) { if (pred()) return true; await c.run(200); } return pred(); }

const missing = [];
const svc = new OnlineService({ fetch: fixtureFetch({ missing }), decodeImage: decodePPM, now: () => RECORDED_AT, answerAfterMs: 500 });
svc.net.minGapMs = 0; svc.net.hostGaps = {};
const c = makeCall(svc);
await c.run(12000);
check(c.leg.connected && c.text().includes('ARM-DOS Online'), 'answers, trains, sends its banner (text outside frames)');

// ---- sign on
c.tx('H', 'Tester\0secret\0ONLINE 1.00');
await until(c, () => c.got.some((f) => f.t === 'W'));
const w = c.got.find((f) => f.t === 'W');
check(w && bin(w.p).startsWith('Tester\0') && bin(w.p).includes('Welcome to ARM-DOS Online, Tester!'), 'H -> W welcome with the screen name');

// ---- encyclopedia search
c.clear(); c.tx('S', 'ARM architecture');
await until(c, () => c.got.some((f) => f.t === 'E'));
let d = docOf(c.got);
check(c.got.some((f) => f.t === 'S' && bin(f.p).includes('Searching')), 'S -> status "Searching the Encyclopedia..."');
check(d && d.kind === 'L' && d.title === 'Search: ARM architecture' && d.channel === 'Encyclopedia', 'search -> list document');
check(d && d.text.includes('1. ARM architecture family') && d.text.includes('10. '), 'ten results, numbered');
check(d && d.plain.every((l) => l.length <= 76), 'every line fits 76 columns');
const search = d;

// ---- follow result 1 -> article
const lk = svc.docs.get(search.id).links;
const n1 = lk.findIndex((l) => l && l.kind === 'A' && l.title === 'ARM architecture family');
c.clear(); c.tx('F', [search.id & 255, search.id >> 8, n1 & 255, n1 >> 8]);
await until(c, () => c.got.some((f) => f.t === 'E'));
d = docOf(c.got);
check(d && d.kind === 'E' && d.title === 'ARM architecture family', 'follow link -> the article');
check(d && d.text.includes('ARM (stylised in lowercase as arm) is a family of RISC'), 'article: the introduction as plain text');
check(d && /HISTORY\n\xCD+/.test(d.text) && d.text.includes('Quick facts: ARM'), 'article: headings underlined, infobox as quick facts');
check(d && d.text.includes('[Picture: ARM1 2nd processor for the BBC Micro]'), 'article: pictures as picture links');
check(d && !/\[\d+\]/.test(d.text.split('Quick facts')[0]), 'article: no footnote markers');
fs.writeFileSync(path.join(OUT, 'article.txt'), d.text.split('').map((ch) => fromCP437(ch.charCodeAt(0))).join(''));
const art = d, artDoc = svc.docs.get(d.id);
// the encoded form: links are 01 a b ... 02, styles 03 s
check(/\x01..\x03l/.test(art.body) && art.body.includes('\x03h'), 'encoding: link and style codes');

// ---- a picture
const pn = artDoc.links.findIndex((l) => l && l.kind === 'P' && /Acorn-ARM/.test(l.src));
c.clear(); c.tx('F', [art.id & 255, art.id >> 8, pn & 255, pn >> 8]);
await until(c, () => c.got.some((f) => f.t === 'F'), 30000);
const g = c.got.find((f) => f.t === 'G');
const gifBytes = Uint8Array.from(c.got.filter((f) => f.t === 'B').flatMap((f) => [...f.p]));
if (check(!!g, 'picture link -> G header')) {
  const [name, cap] = bin(g.p).split('\0');
  const q = g.p.subarray(name.length + cap.length + 2);
  const W = q[0] | (q[1] << 8), H = q[2] | (q[3] << 8), size = q[4] | (q[5] << 8) | (q[6] << 16) | (q[7] << 24);
  check(name === 'ACORNARM.GIF' && cap.startsWith('ARM1 2nd processor'), `G: 8.3 name ${name}, caption`);
  check(W <= 320 && H <= 184 && size === gifBytes.length, `G: ${W}x${H}, ${size} bytes = what the B frames carried`);
  const gif = decodeGif(gifBytes);
  check(gif.interlaced && gif.width === W && gif.height === H && Math.max(...gif.pixels) < 240, 'GIF: interlaced, pixels use palette 0-239');
  check(Math.abs(W / (H * 1.2) - 500 / 332) < 0.05, 'GIF: aspect corrected for mode 13h (1.2:1 pixels)');
  fs.writeFileSync(path.join(OUT, name), gifBytes);
}

// ---- external link, weather, dictionary, news, today, random
const un = artDoc.links.findIndex((l) => l && l.kind === 'U');
if (un > 0) {
  c.clear(); c.tx('F', [art.id & 255, art.id >> 8, un & 255, un >> 8]);
  await until(c, () => c.got.some((f) => f.t === 'R'));
  const r = c.got.find((f) => f.t === 'R');
  check(r && r.p[0] === 0x49 && bin(r.p).includes('World Wide Web'), 'external link -> I message about the World Wide Web');
}
c.clear(); c.tx('W', 'Berlin');
await until(c, () => c.got.some((f) => f.t === 'E' || f.t === 'R'));
d = docOf(c.got);
check(d && d.kind === 'W' && d.title.startsWith('Weather: Berlin') && /\d+\xF8C/.test(d.text) && d.text.includes('THE WEEK AHEAD'), 'weather: Berlin, current conditions, the week ahead');
fs.writeFileSync(path.join(OUT, 'weather.txt'), d ? d.text.split('').map((ch) => fromCP437(ch.charCodeAt(0))).join('') : '');
c.clear(); c.tx('D', 'modem');
await until(c, () => c.got.some((f) => f.t === 'E'));
d = docOf(c.got);
check(d && d.kind === 'D' && d.text.includes('Noun') && d.text.includes('A device that encodes digital computer signals'), 'dictionary: modem');
c.clear(); c.tx('N');
await until(c, () => c.got.some((f) => f.t === 'E'));
d = docOf(c.got);
check(d && d.kind === 'N' && d.text.includes(' 1. ') && d.text.includes('points by'), 'technology news: the top stories');
const news = d;
const nn = svc.docs.get(news.id).links.findIndex((l) => l && l.kind === 'N');
c.clear(); c.tx('F', [news.id & 255, news.id >> 8, nn & 255, nn >> 8]);
await until(c, () => c.got.some((f) => f.t === 'E'));
d = docOf(c.got);
check(d && d.kind === 'N' && d.text.includes('points by'), 'news: a story with its comments');
c.clear(); c.tx('T', '1104');
await until(c, () => c.got.some((f) => f.t === 'E'));
d = docOf(c.got);
check(d && d.kind === 'T' && d.text.includes('On this day, November 4'), 'today in history: November 4');
c.clear(); c.tx('R');
await until(c, () => c.got.some((f) => f.t === 'E'));
d = docOf(c.got);
check(d && d.kind === 'E' && d.text.includes('From Wikipedia'), `random article: ${d?.title}`);
check(missing.length === 0, 'every URL had a fixture' + (missing.length ? ': ' + missing.join(' ') : ''));

// ---- cancel: X while a long document is on its way
svc.maxBacklog = 64;              // pretend the line is slow so the document is still queued
c.leg.remoteBacklog = () => 64;   // nothing drains
c.clear(); c.tx('A', 'ARM architecture family');
await until(c, () => svc.out.length > 10);
c.tx('X');
c.leg.remoteBacklog = () => 0; svc.maxBacklog = 0;
await c.run(1000);
const zi = c.got.findIndex((f) => f.t === 'Z');
check(zi >= 0 && !c.got.slice(zi + 1).some((f) => f.t === 'T'), 'X: the queue is dropped, Z acknowledges, nothing after it');

// ---- a service that is down
const svc2 = new OnlineService({ fetch: fixtureFetch({ fail: () => true }), decodeImage: decodePPM, now: () => RECORDED_AT, answerAfterMs: 0 });
const c2 = makeCall(svc2);
await c2.run(8000);
c2.tx('H', 'X\0\0'); await c2.run(200);
c2.tx('S', 'anything');
await until(c2, () => c2.got.some((f) => f.t === 'R'));
let r = c2.got.find((f) => f.t === 'R');
check(r && r.p[0] === 0x45 && bin(r.p).includes('The Encyclopedia is not available right now.'), 'network down: "The Encyclopedia is not available right now."');
c2.clear(); c2.tx('W', 'Berlin');
await until(c2, () => c2.got.some((f) => f.t === 'R'));
r = c2.got.find((f) => f.t === 'R');
check(r && bin(r.p).includes('The Weather Center is not available right now.'), 'network down: the Weather Center too');

// ---- sign off
c.clear(); c.tx('Q');
await until(c, () => c.leg.hungUp, 5000);
check(c.got.some((f) => f.t === 'Q' && bin(f.p).includes('Goodbye')) && c.leg.hungUp && svc.state === 'idle', 'Q -> goodbye, then the service hangs up');

// ---- a person dialling in with TERM
const c3 = makeCall(svc);
await c3.run(8000);
c3.leg.owner.onData([13]);
await until(c3, () => c3.leg.hungUp, 5000);
check(c3.text().includes('type ONLINE at the C:\\> prompt') && c3.leg.hungUp, 'a terminal caller gets a note and a hang-up');

// ---- cp437
check(toCP437('Café – “ARM” ×2 ½ 東') === 'Caf\x82 - "ARM" x2 \xAB ?', 'CP437 conversion');

console.log(failures ? `\n${failures} FAILED` : '\nall passed');
process.exit(failures ? 1 : 0);
