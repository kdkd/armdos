// ZMODEM (Chuck Forsberg's protocol, as in lrzsz's sz/rz): a byte-stream implementation of
// both ends for the Host Link (docs/MODEM.md). Pure JS, no I/O and no timers of its own:
//
//   const rx = new ZReceiver({ send: (bytes) => ..., onFile: (f) => ..., onDone: (ok, why) => ... });
//   rx.start(nowMs); rx.feed(bytes, nowMs); rx.tick(nowMs);
//
//   const tx = new ZSender({ files: [{ name, data, mtime }], send: (bytes) => ..., onDone, onProgress });
//   tx.start(nowMs); tx.feed(bytes, nowMs); tx.tick(nowMs);
//   const chunk = tx.produce(maxBytes);     // file data is generated lazily, as the line has room
//
// `send` gets headers and control traffic immediately; the sender's data subpackets are pulled
// with produce() so that a ZRPOS (the receiver asking to resume at a position after an error)
// never has megabytes of stale data queued in front of it.
// Supports: hex / binary CRC-16 / binary CRC-32 headers; CRC-16 and CRC-32 data subpackets;
// ZCRCE/G/Q/W; receiver window (ZRINIT buffer size) with ZCRCW; ZRPOS error recovery; ZSKIP;
// ZSINIT; CAN*5 abort; batch of several files; ZFIN / "OO".
// Written for ARM-DOS (C) 2026 Europa; tested against lrzsz 0.12.21 (emu/tests/zmodem).

export const ZPAD = 0x2A, ZDLE = 0x18, ZBIN = 0x41, ZHEX = 0x42, ZBIN32 = 0x43, XON = 0x11, CAN = 0x18;
export const ZRQINIT = 0, ZRINIT = 1, ZSINIT = 2, ZACK = 3, ZFILE = 4, ZSKIP = 5, ZNAK = 6, ZABORT = 7, ZFIN = 8,
  ZRPOS = 9, ZDATA = 10, ZEOF = 11, ZFERR = 12, ZCRC = 13, ZCHALLENGE = 14, ZCOMPL = 15, ZCAN = 16, ZFREECNT = 17,
  ZCOMMAND = 18;
export const ZCRCE = 0x68, ZCRCG = 0x69, ZCRCQ = 0x6A, ZCRCW = 0x6B, ZRUB0 = 0x6C, ZRUB1 = 0x6D;
export const CANFDX = 1, CANOVIO = 2, CANBRK = 4, CANFC32 = 0x20, ESCCTL = 0x40;
export const NAMES = ['ZRQINIT', 'ZRINIT', 'ZSINIT', 'ZACK', 'ZFILE', 'ZSKIP', 'ZNAK', 'ZABORT', 'ZFIN', 'ZRPOS',
  'ZDATA', 'ZEOF', 'ZFERR', 'ZCRC', 'ZCHALLENGE', 'ZCOMPL', 'ZCAN', 'ZFREECNT', 'ZCOMMAND'];
export const ABORT_SEQ = [CAN, CAN, CAN, CAN, CAN, CAN, CAN, CAN, 8, 8, 8, 8, 8, 8, 8, 8];

// ------------------------------------------------------------------ CRCs
const CRC16T = new Uint16Array(256), CRC32T = new Int32Array(256);
for (let i = 0; i < 256; i++) {
  let c = i << 8;
  for (let k = 0; k < 8; k++) c = (c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1;
  CRC16T[i] = c & 0xFFFF;
  let d = i;
  for (let k = 0; k < 8; k++) d = (d & 1) ? (d >>> 1) ^ 0xEDB88320 : d >>> 1;
  CRC32T[i] = d;
}
export function crc16(bytes, crc = 0) {
  for (let i = 0; i < bytes.length; i++) crc = ((crc << 8) ^ CRC16T[((crc >>> 8) ^ bytes[i]) & 0xFF]) & 0xFFFF;
  return crc;
}
export function crc32(bytes, crc = 0xFFFFFFFF) {   // returns the running (non-inverted) value
  for (let i = 0; i < bytes.length; i++) crc = CRC32T[(crc ^ bytes[i]) & 0xFF] ^ (crc >>> 8);
  return crc >>> 0;
}

// ------------------------------------------------------------------ encoding
function needsEscape(b, prev, escctl) {
  switch (b) {
    case 0x18: case 0x10: case 0x11: case 0x13: case 0x90: case 0x91: case 0x93: case 0x98: return true;
    case 0x0D: case 0x8D: return true;           // always (covers the "@ CR" telenet rule)
    case 0xFF: return true;                      // keeps a stray 0xFF from ending up next to Telnet-ish links
  }
  return escctl && (b & 0x60) === 0;
}
/** ZDLE-encode bytes into out (array). */
export function zdleEncode(bytes, out, escctl = false) {
  let prev = 0;
  for (let i = 0; i < bytes.length; i++) {
    const b = bytes[i];
    if (b === 0x7F) { out.push(ZDLE, ZRUB0); }
    else if (b === 0xFF) { out.push(ZDLE, ZRUB1); }
    else if (needsEscape(b, prev, escctl)) out.push(ZDLE, b ^ 0x40);
    else out.push(b);
    prev = b;
  }
  return out;
}
const HEXD = '0123456789abcdef';
const posBytes = (p) => [p & 0xFF, (p >>> 8) & 0xFF, (p >>> 16) & 0xFF, (p >>> 24) & 0xFF];
/** Header bytes (4) for flags ZF0..ZF3 (ZF0 is the last byte, as in the protocol). */
export const flagBytes = (f0 = 0, f1 = 0, f2 = 0, f3 = 0) => [f3, f2, f1, f0];

export function hexHeader(type, data4) {
  const raw = [type, ...data4];
  const crc = crc16(raw);
  const out = [ZPAD, ZPAD, ZDLE, ZHEX];
  for (const b of [...raw, crc >> 8, crc & 0xFF]) out.push(HEXD.charCodeAt(b >> 4), HEXD.charCodeAt(b & 15));
  out.push(0x0D, 0x8A);
  if (type !== ZFIN && type !== ZACK) out.push(XON);
  return out;
}
export function binHeader(type, data4, use32) {
  const raw = [type, ...data4];
  const out = [ZPAD, ZDLE, use32 ? ZBIN32 : ZBIN];
  if (use32) {
    const c = ~crc32(raw) >>> 0;
    zdleEncode([...raw, c & 0xFF, (c >>> 8) & 0xFF, (c >>> 16) & 0xFF, c >>> 24], out);
  } else {
    const c = crc16(raw);
    zdleEncode([...raw, c >> 8, c & 0xFF], out);
  }
  return out;
}
export function subpacket(data, end, use32, escctl = false) {
  const out = [];
  zdleEncode(data, out, escctl);
  out.push(ZDLE, end);
  if (use32) {
    const c = ~crc32([end], crc32(data)) >>> 0;
    zdleEncode([c & 0xFF, (c >>> 8) & 0xFF, (c >>> 16) & 0xFF, c >>> 24], out, escctl);
  } else {
    const c = crc16([end], crc16(data));
    zdleEncode([c >> 8, c & 0xFF], out, escctl);
  }
  if (end === ZCRCW) out.push(XON);
  return out;
}

// ------------------------------------------------------------------ the parser (both ends)
// Emits: onHeader({ type, data: [4], pos, fmt: 'hex'|'bin16'|'bin32' }),
//        onSub({ data: Uint8Array, end }) or onSubError(), onCancel()
// After a header of a type that carries data (ZFILE ZSINIT ZDATA ZCOMMAND), call
// expectData(use32) to read subpackets until ZCRCE/ZCRCW.
class Parser {
  constructor(h) { this.h = h; this.reset(); }
  reset() { this.st = 'idle'; this.cans = 0; this.buf = []; this.esc = false; this.hexc = []; this.pads = 0; }
  expectData(use32) { this.st = 'data'; this.use32 = use32; this.buf = []; this.esc = false; this.end = -1; this.crcb = []; }
  feed(bytes) {
    for (let i = 0; i < bytes.length; i++) this.byte(bytes[i]);
  }
  byte(c) {
    // five CANs in a row abort, in any state
    if (c === CAN) { if (++this.cans >= 5) { this.cans = 0; this.st = 'idle'; this.h.onCancel?.(); return; } }
    else this.cans = 0;
    switch (this.st) {
      case 'idle':
        if (c === ZPAD || c === (ZPAD | 0x80)) { this.st = 'pad'; }
        return;
      case 'pad':
        if (c === ZPAD || c === (ZPAD | 0x80)) return;
        if (c === ZDLE) { this.st = 'fmt'; return; }
        this.st = 'idle'; return;
      case 'fmt':
        if (c === ZHEX) { this.st = 'hex'; this.hexc = []; return; }
        if (c === ZBIN || c === ZBIN32) { this.st = c === ZBIN ? 'bin16' : 'bin32'; this.buf = []; this.esc = false; return; }
        this.st = 'idle'; return;
      case 'hex': {
        const ch = c & 0x7F;
        const v = ch >= 0x30 && ch <= 0x39 ? ch - 0x30 : ch >= 0x61 && ch <= 0x66 ? ch - 0x57 : ch >= 0x41 && ch <= 0x46 ? ch - 0x37 : -1;
        if (v < 0) { this.st = 'idle'; this.h.onBadHeader?.(); return; }
        this.hexc.push(v);
        if (this.hexc.length === 14) {
          const b = []; for (let k = 0; k < 14; k += 2) b.push(this.hexc[k] << 4 | this.hexc[k + 1]);
          this.st = 'idle';
          if (crc16(b.slice(0, 5)) !== (b[5] << 8 | b[6])) { this.h.onBadHeader?.(); return; }
          this.header(b[0], b.slice(1, 5), 'hex');
        }
        return;
      }
      case 'bin16': case 'bin32': {
        const d = this.decode(c);
        if (d === null) return;
        if (d < 0) { this.st = 'idle'; this.h.onBadHeader?.(); return; }
        this.buf.push(d);
        const need = this.st === 'bin16' ? 7 : 9;
        if (this.buf.length === need) {
          const b = this.buf, fmt = this.st;
          this.st = 'idle';
          let good;
          if (fmt === 'bin16') good = crc16(b.slice(0, 5)) === (b[5] << 8 | b[6]);
          else good = (~crc32(b.slice(0, 5)) >>> 0) === ((b[5] | b[6] << 8 | b[7] << 16 | b[8] << 24) >>> 0);
          if (!good) { this.h.onBadHeader?.(); return; }
          this.header(b[0], b.slice(1, 5), fmt);
        }
        return;
      }
      case 'data': {
        if (!this.esc && (c === XON || c === 0x13 || c === 0x91 || c === 0x93)) return;
        if (this.end < 0) {
          if (this.esc) {
            this.esc = false;
            if (c >= ZCRCE && c <= ZCRCW) { this.end = c; this.crcb = []; return; }
            if (c === ZRUB0) { this.buf.push(0x7F); return; }
            if (c === ZRUB1) { this.buf.push(0xFF); return; }
            if ((c & 0x60) === 0x40) { this.buf.push(c ^ 0x40); return; }
            if (c === CAN) return;           // part of an abort: the CAN counter handles it
            this.st = 'idle'; this.h.onSubError?.(); return;
          }
          if (c === ZDLE) { this.esc = true; return; }
          this.buf.push(c);
          if (this.buf.length > 8192) { this.st = 'idle'; this.h.onSubError?.(); }
          return;
        }
        // CRC bytes after the frame end
        const d = this.decode(c);
        if (d === null) return;
        if (d < 0) { this.st = 'idle'; this.h.onSubError?.(); return; }
        this.crcb.push(d);
        if (this.crcb.length === (this.use32 ? 4 : 2)) {
          const data = Uint8Array.from(this.buf), end = this.end, cb = this.crcb;
          let good;
          if (this.use32) good = (~crc32([end], crc32(data)) >>> 0) === ((cb[0] | cb[1] << 8 | cb[2] << 16 | cb[3] << 24) >>> 0);
          else good = crc16([end], crc16(data)) === (cb[0] << 8 | cb[1]);
          if (!good) { this.st = 'idle'; this.h.onSubError?.(); return; }
          if (end === ZCRCG || end === ZCRCQ) this.expectData(this.use32); else this.st = 'idle';
          this.h.onSub?.({ data, end });
        }
        return;
      }
    }
  }
  /** ZDLE decoding for header/CRC bytes: returns a byte, null (need more), or -1 (error). */
  decode(c) {
    if (this.esc) {
      this.esc = false;
      if (c === ZRUB0) return 0x7F;
      if (c === ZRUB1) return 0xFF;
      if ((c & 0x60) === 0x40) return c ^ 0x40;
      if (c === CAN) return null;
      return -1;
    }
    if (c === ZDLE) { this.esc = true; return null; }
    if (c === XON || c === 0x13 || c === 0x91 || c === 0x93) return null;
    return c;
  }
  header(type, data, fmt) {
    const pos = (data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24) >>> 0;
    this.h.onHeader?.({ type, data, pos, fmt });
  }
}

// ------------------------------------------------------------------ receiver
export class ZReceiver {
  /**
   * send(bytes), onFile({ name, data: Uint8Array, size, mtime }), onStart({ name, size }),
   * onProgress({ name, pos, size }), onDone(ok, reason). window: our buffer size (0 = stream).
   */
  constructor({ send, onFile = () => {}, onStart = () => {}, onProgress = () => {}, onDone = () => {}, window = 0, timeoutMs = 10000 }) {
    Object.assign(this, { send, onFile, onStart, onProgress, onDone, window, timeoutMs });
    this.p = new Parser({
      onHeader: (h) => this.header(h),
      onSub: (s) => this.sub(s),
      onSubError: () => this.subError(),
      onBadHeader: () => {},
      onCancel: () => this.finish(false, 'cancelled by the sender'),
    });
    this.state = 'idle'; this.done = false; this.files = []; this.file = null; this.pos = 0; this.use32 = false;
    this.lastMs = 0; this.tries = 0; this.expect = null;
  }
  flags() { return flagBytes(CANFDX | CANOVIO | CANFC32, 0); }
  zrinit() { const w = this.window; this.send(hexHeader(ZRINIT, [w & 0xFF, (w >> 8) & 0xFF, 0, CANFDX | CANOVIO | CANFC32])); }
  start(now = 0) { this.state = 'init'; this.lastMs = now; this.zrinit(); }
  tick(now) {
    if (this.done || this.state === 'idle') return;
    if (now - this.lastMs < this.timeoutMs) return;
    this.lastMs = now;
    if (++this.tries > 8) { this.abort('timed out'); return; }
    if (this.state === 'init' || this.state === 'between') this.zrinit();
    else if (this.state === 'data' || this.state === 'file') this.send(hexHeader(ZRPOS, posBytes(this.pos)));
    else if (this.state === 'fin') this.finish(true);
  }
  feed(bytes, now = 0) {
    if (this.done) return;
    if (this.state === 'fin') {        // after our ZFIN: "OO"
      for (const b of bytes) if (b === 0x4F && ++this.oo >= 2) { this.finish(true); return; }
    }
    this.p.feed(bytes);
    this.lastMs = now; this.nowMs = now;
  }
  header(h) {
    this.tries = 0;
    switch (h.type) {
      case ZRQINIT: if (this.state !== 'data') this.zrinit(); break;
      case ZSINIT: this.use32 = h.fmt === 'bin32'; this.expect = 'sinit'; this.p.expectData(this.use32); break;
      case ZFILE: this.use32 = h.fmt === 'bin32'; this.expect = 'file'; this.p.expectData(this.use32); break;
      case ZDATA:
        if (!this.file) { this.zrinit(); break; }
        if (h.pos !== this.pos) { this.send(hexHeader(ZRPOS, posBytes(this.pos))); break; }
        this.use32 = h.fmt === 'bin32'; this.expect = 'data'; this.state = 'data'; this.p.expectData(this.use32);
        break;
      case ZEOF:
        if (!this.file) { this.zrinit(); break; }
        if (h.pos !== this.pos) { this.send(hexHeader(ZRPOS, posBytes(this.pos))); break; }
        this.completeFile();
        this.state = 'between'; this.zrinit();
        break;
      case ZFIN:
        this.send(hexHeader(ZFIN, [0, 0, 0, 0]));
        this.state = 'fin'; this.oo = 0;
        break;
      case ZFREECNT: this.send(hexHeader(ZACK, posBytes(0x7FFFFFFF))); break;
      case ZCOMMAND: this.send(hexHeader(ZCOMPL, [0, 0, 0, 0])); break;
      case ZABORT: case ZFERR: case ZCAN: this.finish(false, NAMES[h.type]); break;
      default: break;
    }
  }
  sub(s) {
    if (this.expect === 'sinit') { this.expect = null; this.send(hexHeader(ZACK, [0, 0, 0, 0])); return; }
    if (this.expect === 'file') {
      this.expect = null;
      const d = s.data;
      let z = d.indexOf(0); if (z < 0) z = d.length;
      const name = new TextDecoder('latin1').decode(d.subarray(0, z)).replace(/^.*[\\/]/, '');
      const info = new TextDecoder('latin1').decode(d.subarray(z + 1)).replace(/\0.*$/s, '').trim().split(/\s+/);
      const size = info[0] ? parseInt(info[0], 10) : -1;
      const mtime = info[1] ? parseInt(info[1], 8) : 0;
      this.file = { name, size, mtime, chunks: [], len: 0 };
      this.pos = 0; this.state = 'file';
      this.onStart({ name, size });
      this.send(hexHeader(ZRPOS, posBytes(0)));
      return;
    }
    if (this.expect === 'data' && this.file) {
      if (s.data.length) { this.file.chunks.push(s.data); this.file.len += s.data.length; this.pos += s.data.length; }
      this.onProgress({ name: this.file.name, pos: this.pos, size: this.file.size });
      if (s.end === ZCRCQ || s.end === ZCRCW) this.send(hexHeader(ZACK, posBytes(this.pos)));
      if (s.end === ZCRCE || s.end === ZCRCW) this.expect = null;
    }
  }
  subError() {
    // a damaged subpacket: ask for a resend from where we are and ignore data until a header
    this.expect = null;
    if (this.file) this.send(hexHeader(ZRPOS, posBytes(this.pos)));
    else this.zrinit();
  }
  completeFile() {
    const f = this.file; this.file = null;
    const data = new Uint8Array(f.len); let o = 0;
    for (const c of f.chunks) { data.set(c, o); o += c.length; }
    const done = { name: f.name, data, size: f.len, mtime: f.mtime };
    this.files.push(done);
    this.onFile(done);
  }
  abort(why = 'aborted') { if (this.done) return; this.send(ABORT_SEQ); this.finish(false, why); }
  finish(ok, why = '') { if (this.done) return; this.done = true; this.state = 'done'; this.onDone(ok, why); }
}

// ------------------------------------------------------------------ sender
export class ZSender {
  /**
   * files: [{ name, data: Uint8Array, mtime (s) }]. send(bytes) for control traffic;
   * produce(max) for the data stream. onProgress({ name, pos, size, index }), onDone(ok, why),
   * onSkip(name). packet: subpacket size (1024).
   */
  constructor({ files, send, onProgress = () => {}, onDone = () => {}, onSkip = () => {}, packet = 1024, timeoutMs = 10000, flight = 16384 }) {
    Object.assign(this, { files, send, onProgress, onDone, onSkip, packet, timeoutMs, flight });
    this.p = new Parser({
      onHeader: (h) => this.header(h),
      onSub: () => {}, onSubError: () => {}, onBadHeader: () => {},
      onCancel: () => this.finish(false, 'cancelled by the receiver'),
    });
    this.idx = -1; this.state = 'idle'; this.done = false; this.use32 = false; this.window = 0;
    this.out = []; this.pos = 0; this.sinceAck = 0; this.waitAck = false; this.lastMs = 0; this.tries = 0;
    this.escctl = false;
  }
  get file() { return this.files[this.idx]; }
  start(now = 0) {
    this.state = 'init'; this.lastMs = now;
    this.send([0x72, 0x7A, 0x0D]);          // "rz\r" - starts a receiver on a Unix host
    this.send(hexHeader(ZRQINIT, [0, 0, 0, 0]));
  }
  feed(bytes, now = 0) { if (this.done) return; this.lastMs = now; this.p.feed(bytes); }
  tick(now) {
    this.nowMs = now;
    if (this.done || this.state === 'idle') return;
    if (this.state === 'data') {
      // a receiver that never ACKs our ZCRCQ: carry on after a while rather than stall
      // the window is used up and no ZACK came: the receiver may have lost sync (a damaged
      // header). Start again from the last acknowledged position with a fresh ZDATA header.
      if (this.ackWaitSince && now - this.ackWaitSince > Math.min(3000, this.timeoutMs)) {
        if (++this.tries > 8) { this.abort('timed out'); return; }
        this.startData(this.ackPos);
      }
      return;
    }
    if (now - this.lastMs < this.timeoutMs) return;
    this.lastMs = now;
    if (++this.tries > 8) { this.abort('timed out'); return; }
    if (this.state === 'init') this.send(hexHeader(ZRQINIT, [0, 0, 0, 0]));
    else if (this.state === 'file') this.sendFileHeader();
    // ZEOF unanswered: rz silently ignores a ZEOF at the wrong offset (data was lost after its
    // last ZRPOS), so rather than repeat it, resend from the last position the receiver confirmed
    else if (this.state === 'eof') this.startData(this.ackPos ?? 0);
    else if (this.state === 'fin') this.send(hexHeader(ZFIN, [0, 0, 0, 0]));
  }
  hdr(type, d) { return binHeader(type, d, this.use32); }
  nextFile() {
    this.idx++;
    if (this.idx >= this.files.length) {
      this.state = 'fin'; this.out = [];
      this.send(hexHeader(ZFIN, [0, 0, 0, 0]));
      return;
    }
    this.sendFileHeader();
  }
  sendFileHeader() {
    const f = this.file;
    this.state = 'file';
    const left = this.files.slice(this.idx).reduce((a, x) => a + x.data.length, 0);
    const info = `${f.data.length} ${Math.floor(f.mtime || Date.now() / 1000).toString(8)} 100644 0 ${this.files.length - this.idx} ${left}`;
    const enc = new TextEncoder();
    const body = [...enc.encode(f.name), 0, ...enc.encode(info), 0];
    this.send([...this.hdr(ZFILE, flagBytes(1, 0, 0, 0)), ...subpacket(body, ZCRCW, this.use32, this.escctl)]);
  }
  header(h) {
    this.tries = 0;
    switch (h.type) {
      case ZRINIT: {
        const f0 = h.data[3];
        this.use32 = (f0 & CANFC32) !== 0;
        this.escctl = (f0 & ESCCTL) !== 0;
        this.window = h.data[0] | h.data[1] << 8;
        if (this.state === 'init') this.nextFile();
        else if (this.state === 'eof') { this.onProgress({ name: this.file.name, pos: this.file.data.length, size: this.file.data.length, index: this.idx, done: true }); this.nextFile(); }
        else if (this.state === 'file') this.sendFileHeader();
        break;
      }
      case ZRPOS:
        if (this.state === 'file' || this.state === 'data' || this.state === 'eof') this.startData(Math.min(h.pos, this.file.data.length));
        break;
      case ZACK:
        if (this.state === 'data' && this.waitAck) { this.waitAck = false; this.sinceAck = 0; }
        if (h.pos > this.ackPos) { this.ackPos = h.pos; this.ackWaitSince = 0; }
        break;
      case ZSKIP:
        if (this.state === 'file' || this.state === 'data' || this.state === 'eof') { this.onSkip(this.file.name); this.out = []; this.nextFile(); }
        break;
      case ZFIN:
        if (this.state === 'fin') { this.send([0x4F, 0x4F]); this.finish(true); }
        break;
      case ZNAK:
        if (this.state === 'file') this.sendFileHeader();
        else if (this.state === 'init') this.send(hexHeader(ZRQINIT, [0, 0, 0, 0]));
        break;
      case ZABORT: case ZFERR: case ZCAN: this.finish(false, NAMES[h.type]); break;
      case ZRQINIT: break;       // our own echo? ignore
      default: break;
    }
  }
  startData(pos) {
    this.state = 'data'; this.pos = pos; this.waitAck = false; this.sinceAck = 0; this.ackPos = pos; this.ackWaitSince = 0; this.sinceQ = 0;
    this.out = this.hdr(ZDATA, posBytes(pos));
  }
  /** Up to max bytes of the data stream (headers of the data phase included). */
  produce(max) {
    if (this.done) return [];
    if (this.state === 'data' && this.out.length < max && !this.waitAck) this.generate(max);
    if (!this.out.length) return [];
    const n = Math.min(max, this.out.length);
    return this.out.splice(0, n);
  }
  generate(max) {
    const f = this.file, data = f.data;
    while (this.out.length < max && this.state === 'data' && !this.waitAck) {
      // at most `flight` bytes beyond the last ZACK (ZCRCQ every flight/4 asks for one), so a
      // ZRPOS after line noise does not have to wade through a long stale stream
      if (!this.window && this.flight && this.pos - this.ackPos >= this.flight) { if (!this.ackWaitSince) this.ackWaitSince = this.nowMs || 1; break; }
      const n = Math.min(this.packet, data.length - this.pos);
      const chunk = data.subarray(this.pos, this.pos + n);
      this.pos += n; this.sinceAck += n;
      const last = this.pos >= data.length;
      let end = last ? ZCRCE : ZCRCG;
      this.sinceQ += n;
      if (!last && !this.window && this.flight && this.sinceQ >= this.flight / 4) { end = ZCRCQ; this.sinceQ = 0; }
      if (!last && this.window && this.sinceAck + this.packet > this.window) { end = ZCRCW; this.waitAck = true; }
      for (const b of subpacket(chunk, end, this.use32, this.escctl)) this.out.push(b);
      this.onProgress({ name: f.name, pos: this.pos, size: data.length, index: this.idx });
      if (last) {
        for (const b of this.hdr(ZEOF, posBytes(data.length))) this.out.push(b);
        this.state = 'eof';
      }
    }
  }
  abort(why = 'aborted') { if (this.done) return; this.out = []; this.send(ABORT_SEQ); this.finish(false, why); }
  finish(ok, why = '') { if (this.done) return; this.done = true; this.state = 'done'; this.onDone(ok, why); }
}
