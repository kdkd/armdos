// ATAPI CD-ROM drive: secondary IDE channel master (ports 0x170-0x177, 0x376/0x377,
// IRQ 15 -> INT 77h), SFF-8020i / MMC packet commands, with Red Book CD audio that plays
// in emulated time through the machine's audio output (dev/audio.mjs) - the magic of CD
// audio: the drive keeps playing while the CPU does anything else.
//
//   const disc = new CdDisc(manifest, { data, audio });   // see CdDisc below
//   m.cdrom.insert(disc);  m.cdrom.eject();  m.cdrom.trayButton();
//
// Protocol: PACKET (A0h) -> DRQ with interrupt reason CoD=1/IO=0, the host writes the
// 12-byte CDB as 6 words to 0x170; data-in phases present the byte count in 0x174/0x175
// (limited by what the host wrote there before the command, rounded to whole 2048-byte
// sectors for READ) with reason IO=1, CoD=0; completion = reason CoD=1/IO=1, DRDY, IRQ
// (if nIEN is clear). Errors: status ERR (CHK), error register = sense key << 4 | ABRT.
// ATA commands: A1h IDENTIFY PACKET DEVICE, 08h DEVICE RESET, ECh -> aborted with the
// ATAPI signature (14h/EBh in 0x174/0x175), EFh/E0h-E5h/90h accepted.
//
// Packet commands: 00 TEST UNIT READY, 03 REQUEST SENSE, 12 INQUIRY, 1B START STOP UNIT
// (tray eject/load), 1E PREVENT/ALLOW MEDIUM REMOVAL, 25 READ CAPACITY, 28/A8 READ(10/12),
// 2B SEEK, 42 READ SUB-CHANNEL (formats 1-3), 43 READ TOC (formats 0/1), 45/A5 PLAY AUDIO
// (10/12), 47 PLAY AUDIO MSF, 4A GET EVENT STATUS NOTIFICATION (media class), 4B
// PAUSE/RESUME, 4E STOP PLAY/SCAN, 55/5A MODE SELECT/SENSE(10) (pages 01h, 0Dh, 0Eh audio
// volume, 2Ah capabilities, 3Fh), 1A/15 the 6-byte forms, BB SET CD SPEED, BE READ CD
// (user data of data sectors, 2352-byte CD-DA of audio sectors).
//
// Timing: commands complete at once, except: the disc spins up for 1.2 s after the tray
// closes (NOT READY, 04/01 "becoming ready"), then reports UNIT ATTENTION 06/28 once;
// a READ of data the disc image does not have yet (streamed) keeps BSY until it arrives;
// audio that is not decoded yet holds the play position (the drive "seeks") until it is.

const BSY = 0x80, DRDY = 0x40, DSC = 0x10, DRQ = 0x08, CHK = 0x01;
const IRQ = 15;
const SPINUP_NS = 1.2e9;
const AUDIO_RATE = 44100, FRAMES_PER_SECTOR = 588;
const NS_PER_FRAME = 1e9 / AUDIO_RATE;

// audio status codes (READ SUB-CHANNEL byte 1)
export const AS_INVALID = 0x00, AS_PLAYING = 0x11, AS_PAUSED = 0x12, AS_DONE = 0x13, AS_ERROR = 0x14, AS_NONE = 0x15;

export const lba2msf = (lba) => { const a = lba + 150; return [Math.floor(a / 4500), Math.floor(a / 75) % 60, a % 75]; };
export const msf2lba = (m, s, f) => (m * 60 + s) * 75 + f - 150;

/**
 * A disc in the drive. manifest = disc.json (apps/cdrom: { title, volumeId, mcn,
 * tracks: [{ number, type: 'data'|'audio', sectors, pregap, frames, ... }] }).
 * io.data: the data track, either a Uint8Array (the ISO image) or a streamed source
 *   { img: Uint8Array (full size), ready(lba, n) -> bool, fetch(lba, n) -> Promise<bool> }.
 * io.audio: { get(trackNo) -> { left, right } (Float32Array or Int16Array, 44.1 kHz) | null,
 *   request(trackNo) -> Promise (starts decoding; resolves when get() has it) }.
 *   A plain function trackNo -> {left, right} (synchronous) works too.
 */
export class CdDisc {
  constructor(manifest, io = {}) {
    this.manifest = manifest;
    this.title = manifest.title || '';
    this.mcn = manifest.mcn || '';
    this.tracks = [];
    let lba = 0;
    for (const t of manifest.tracks) {
      const pregap = this.tracks.length ? (t.pregap ?? (t.type === 'audio' ? 150 : 0)) : 0;
      lba += pregap;
      this.tracks.push({ number: t.number, type: t.type, audio: t.type === 'audio', start: lba, pregap,
        sectors: t.sectors, end: lba + t.sectors, isrc: t.isrc || '', title: t.title || '' });
      lba += t.sectors;
    }
    this.leadout = lba;
    const d = io.data;
    if (d instanceof Uint8Array) this.data = { img: d, ready: () => true, fetch: async () => true };
    else this.data = d || { img: new Uint8Array(0), ready: () => true, fetch: async () => true };
    const a = io.audio;
    if (typeof a === 'function') this.audio = { get: a, request: async () => {} };
    else this.audio = a || { get: () => null, request: async () => {} };
  }
  get firstTrack() { return this.tracks[0].number; }
  get lastTrack() { return this.tracks[this.tracks.length - 1].number; }
  /** the track holding lba (pregaps belong to the track that follows them) */
  trackAt(lba) {
    for (let i = 0; i < this.tracks.length; i++) {
      const t = this.tracks[i];
      if (lba < t.end) return lba >= t.start - t.pregap ? t : null;
    }
    return null;
  }
  track(no) { return this.tracks.find((t) => t.number === no) || null; }
  control(t) { return t.audio ? 0x00 : 0x04; }          // Q control: 4 = data track, 0 = 2-channel audio
}

const put32 = (b, o, v) => { b[o] = (v >>> 24) & 255; b[o + 1] = (v >>> 16) & 255; b[o + 2] = (v >>> 8) & 255; b[o + 3] = v & 255; };
const get32 = (b, o) => ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0;
const get16 = (b, o) => (b[o] << 8) | b[o + 1];
const volGain = (reg) => { const v = reg >> 3; return v === 0 ? 0 : Math.pow(10, (v - 31) / 10); };   // CT1745, 2 dB steps

class SenseError extends Error { constructor(key, asc, ascq = 0) { super('sense'); this.key = key; this.asc = asc; this.ascq = ascq; } }
const sense = (k, a, q) => { throw new SenseError(k, a, q); };

export class ATAPI {
  constructor(m, o = {}) {
    this.m = m;
    this.onChange = o.onChange || (() => {});      // (state) tray/disc/LED/play changes (the page)
    this.onActivity = o.onActivity || (() => {});  // (lba, count) data reads (LED blink)
    this.disc = null;
    this.trayOpen = false;
    this.locked = false;
    this.readyAtNs = 0;
    this.unitAttention = 0x29;        // power-on reset reported at the first command
    this.mediaEvent = 0;              // GESN media event pending: 1 eject request, 2 new media, 3 removal
    this.headphones = null;           // { volume 0..1 } = the front jack is in use: CD audio bypasses the SB16
    this.knob = 1;                    // front-panel volume knob (headphones only)
    this.pcmCache = new Map();
    this.cdb = new Uint8Array(12);
    this.volume = [0x01, 0xFF, 0x02, 0xFF];   // MODE page 0Eh: port 0 = channel 0 at FFh, port 1 = channel 1
    this.play = { state: 'idle', frame: 0, endFrame: 0, baseNs: 0, hold: false, status: AS_NONE };
    this.head = 0;                    // LBA under the head (for sub-channel when idle)
    this.speedKB = 353;               // SET CD SPEED
    this.reset();
  }

  // ------------------------------------------------------------ host API
  /** Put a disc in (opens and closes the tray as a person would). */
  insert(disc) {
    if (this.locked) return false;
    this.stopAudio(AS_NONE);
    this.disc = disc; this.pcmCache.clear();
    this.trayOpen = false;
    this.mediaChanged();
    this.changed();
    return true;
  }
  /** Take the disc out (host "disc box"). Refused while the drive is locked. */
  eject() {
    if (this.locked) { this.mediaEvent = 1; this.changed(); return false; }
    this.openTray(); return true;
  }
  /** The tray button on the front panel. */
  trayButton() {
    if (this.locked) { this.mediaEvent = 1; this.changed(); return false; }
    if (this.trayOpen) this.closeTray(); else this.openTray();
    return true;
  }
  openTray() {
    this.stopAudio(AS_NONE);
    const had = !!this.disc;
    this.trayOpen = true;
    if (had) this.mediaEvent = 3;
    this.changed();
  }
  closeTray() {
    this.trayOpen = false;
    if (this.disc) this.mediaChanged();
    this.changed();
  }
  /** Tray open: replace (or remove, disc = null) the disc lying in it. */
  setDiscInTray(disc) { this.disc = disc; this.pcmCache.clear(); this.changed(); }
  mediaChanged() {
    this.readyAtNs = this.m.timeNs() + SPINUP_NS;
    this.unitAttention = 0x28;
    this.mediaEvent = 2;
    this.play = { state: 'idle', frame: 0, endFrame: 0, baseNs: 0, hold: false, status: AS_NONE };
    this.head = 0;
  }
  get present() { return !!this.disc && !this.trayOpen; }
  get ready() { return this.present && this.m.timeNs() >= this.readyAtNs; }
  state() {
    this.update();
    const p = this.play;
    return { trayOpen: this.trayOpen, disc: this.disc ? (this.disc.title || true) : null, ready: this.ready, locked: this.locked,
      playing: p.state === 'play', paused: p.state === 'pause', holding: p.hold,
      track: this.present ? (this.disc.trackAt(this.posLba())?.number ?? 0) : 0, lba: this.posLba() };
  }
  changed() { try { this.onChange(this.state()); } catch (e) { /* the page's problem */ } }

  // ------------------------------------------------------------ ATA registers
  reset() {
    this.err = 0x01; this.feat = 0; this.count = 1; this.sector = 1; this.dh = 0xA0;
    this.setSignature();
    this.status = 0; this.nIEN = 0;
    this.mode = 0;                    // 0 idle, 1 packet (receiving CDB), 2 data in, 3 data out, 4 waiting (BSY)
    this.buf = null; this.pos = 0; this.len = 0; this.chunk = 0; this.chunkLeft = 0;
    this.waitToken = (this.waitToken || 0) + 1;
    this.senseKey = 0; this.asc = 0; this.ascq = 0;
    this.status = DRDY | DSC;
    this.m.pic?.lower(IRQ);
  }
  /** the machine's RESET line (power-on / reset button): the drive resets and stops playing */
  busReset() {
    this.stopAudio(AS_NONE);
    this.locked = false; this.unitAttention = 0x29;
    this.reset();
  }
  setSignature() { this.count = 1; this.sector = 1; this.bcl = 0x14; this.bch = 0xEB; }
  selected() { return (this.dh & 0x10) === 0; }
  irq() { if (!this.nIEN) this.m.pic.raise(IRQ); }

  read(port) {
    if (!this.selected() && port !== 0x376) return port === 0x177 ? 0 : 0;
    switch (port) {
      case 0x170: return this.readData8();
      case 0x171: return this.err;
      case 0x172: return this.count;
      case 0x173: return this.sector;
      case 0x174: return this.bcl;
      case 0x175: return this.bch;
      case 0x176: return this.dh;
      case 0x177: this.m.pic.lower(IRQ); this.update(); return this.status;
      case 0x376: return this.selected() ? this.status : 0;
    }
    return 0xFF;
  }
  write(port, v) {
    switch (port) {
      case 0x170: this.writeData8(v); return;
      case 0x171: this.feat = v; return;
      case 0x172: this.count = v; return;
      case 0x173: this.sector = v; return;
      case 0x174: this.bcl = v; return;
      case 0x175: this.bch = v; return;
      case 0x176: this.dh = v | 0xA0; return;
      case 0x177: this.m.pic.lower(IRQ); if (this.selected()) this.command(v); return;
      case 0x376:
        this.nIEN = (v >>> 1) & 1;
        if (v & 4) { this.srst = 1; this.mode = 0; this.waitToken++; this.status = BSY; }
        else if (this.srst) { this.srst = 0; const n = this.nIEN; this.reset(); this.nIEN = n; }
        return;
    }
  }

  command(cmd) {
    if (this.mode === 4) { this.waitToken++; this.mode = 0; }
    this.err = 0;
    switch (cmd) {
      case 0xA0:                                     // PACKET
        if (this.feat & 1) { this.ataAbort(); return; }   // no DMA
        this.mode = 1; this.pos = 0;
        this.limit = (this.bcl | (this.bch << 8)) || 0xFFFE;
        if (this.limit & 1) this.limit--;
        this.count = 0x01;                           // CoD
        this.status = DRDY | DSC | DRQ;
        return;
      case 0xA1: this.identify(); return;            // IDENTIFY PACKET DEVICE
      case 0x08:                                     // DEVICE RESET
        this.mode = 0; this.setSignature(); this.err = 0; this.status = DSC; return;
      case 0xEC: this.setSignature(); this.ataAbort(); return;   // IDENTIFY DEVICE: "I am ATAPI"
      case 0x90: this.setSignature(); this.err = 0x01; this.status = DRDY | DSC; this.irq(); return;
      case 0xEF: case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE5: case 0xE7:
        this.status = DRDY | DSC; if (cmd === 0xE5) this.count = 0xFF; this.irq(); return;
      default: this.ataAbort(); return;
    }
  }
  ataAbort() { this.err = 0x04; this.status = DRDY | DSC | CHK; this.mode = 0; this.irq(); }

  identify() {
    const w = new Uint16Array(256);
    const str = (at, len, s) => { s = s.padEnd(len * 2, ' '); for (let i = 0; i < len; i++) w[at + i] = (s.charCodeAt(2 * i) << 8) | s.charCodeAt(2 * i + 1); };
    w[0] = 0x85C0;                   // ATAPI, CD-ROM (type 5), removable, 50 us DRQ, 12-byte packets
    str(10, 10, 'ARMPCCD00000001');
    str(23, 4, '1.00');
    str(27, 20, 'ARM-PC CD-ROM DRIVE');
    w[49] = 0x0200;                  // LBA (no DMA: PIO only)
    w[51] = 0x0200; w[53] = 0x0002;
    w[64] = 0x0003; w[67] = 0x00B4; w[68] = 0x00B4;
    w[71] = 100; w[72] = 100;
    w[80] = 0x001E;                  // ATA/ATAPI-1..4
    this.startDataIn(new Uint8Array(w.buffer), 512);
    this.count = 0x02;
  }

  // ------------------------------------------------------------ data port
  readData16() {
    if (this.mode !== 2 || !(this.status & DRQ)) return 0xFFFF;
    const v = this.buf[this.pos] | ((this.pos + 1 < this.len ? this.buf[this.pos + 1] : 0) << 8);
    this.advanceIn(2);
    return v;
  }
  readData8() {
    if (this.mode !== 2 || !(this.status & DRQ)) return 0xFF;
    const v = this.buf[this.pos];
    this.advanceIn(1);
    return v;
  }
  advanceIn(n) {
    this.pos += n; this.chunkLeft -= n;
    if (this.pos >= this.len) { this.complete(); return; }
    if (this.chunkLeft <= 0) this.nextChunk();
  }
  writeData16(v) { this.writeData8(v & 0xFF); this.writeData8(v >>> 8); }
  writeData8(v) {
    if (this.mode === 1 && (this.status & DRQ)) {
      this.cdb[this.pos++] = v;
      if (this.pos >= 12) { this.mode = 0; this.status = BSY | DSC; this.packet(); }
      return;
    }
    if (this.mode === 3 && (this.status & DRQ)) {
      this.buf[this.pos++] = v;
      if (this.pos >= this.len) { this.mode = 0; this.status = BSY; this.outDone(this.buf); }
    }
  }

  /** start a data-in phase for bytes (at most alloc of them) */
  startDataIn(bytes, alloc = bytes.length, unit = 1) {
    const n = Math.min(bytes.length, alloc);
    if (n <= 0) { this.complete(); return; }
    this.buf = bytes; this.len = n; this.pos = 0; this.mode = 2; this.unit = unit;
    this.nextChunk();
  }
  nextChunk() {
    const left = this.len - this.pos;
    let c = Math.min(left, this.limit || 0xFFFE);
    if (this.unit > 1 && c < left && c >= this.unit) c -= c % this.unit;   // whole sectors per DRQ
    this.chunk = c; this.chunkLeft = c;
    this.bcl = c & 0xFF; this.bch = (c >>> 8) & 0xFF;
    this.count = 0x02;                               // IO=1, CoD=0
    this.status = DRDY | DSC | DRQ;
    this.irq();
  }
  startDataOut(n, done) {
    this.buf = new Uint8Array(n); this.len = n; this.pos = 0; this.mode = 3; this.outDone = (b) => { try { done(b); this.complete(); } catch (e) { this.fail(e); } };
    this.bcl = n & 0xFF; this.bch = (n >>> 8) & 0xFF;
    this.count = 0x00;                               // IO=0, CoD=0
    this.status = DRDY | DSC | DRQ;
    this.irq();
  }
  complete() {
    this.mode = 0; this.count = 0x03;                // CoD + IO: status phase
    this.status = DRDY | DSC; this.err = 0;
    this.senseKey = 0; this.asc = 0; this.ascq = 0;
    this.irq();
  }
  fail(e) {
    if (!(e instanceof SenseError)) throw e;
    this.senseKey = e.key; this.asc = e.asc; this.ascq = e.ascq;
    this.mode = 0; this.count = 0x03;
    this.err = (e.key << 4) | 0x04;
    this.status = DRDY | DSC | CHK;
    this.irq();
  }

  // ------------------------------------------------------------ packet commands
  needMedium() {
    if (!this.disc || this.trayOpen) sense(2, 0x3A, this.trayOpen ? 0x02 : 0x01);
    if (this.m.timeNs() < this.readyAtNs) sense(2, 0x04, 0x01);
  }
  packet() {
    const c = this.cdb, op = c[0];
    try {
      // a pending unit attention is reported by any command except INQUIRY / REQUEST SENSE / GESN
      if (this.unitAttention && op !== 0x12 && op !== 0x03 && op !== 0x4A) {
        if (this.unitAttention === 0x29 || this.ready) { const a = this.unitAttention; this.unitAttention = 0; sense(6, a, 0); }
      }
      switch (op) {
        case 0x00: this.needMedium(); this.complete(); return;
        case 0x03: return this.requestSense();
        case 0x12: return this.inquiry();
        case 0x1B: return this.startStop();
        case 0x1E: this.locked = (c[4] & 1) === 1; this.changed(); this.complete(); return;
        case 0x25: return this.readCapacity();
        case 0x28: return this.readData(get32(c, 2), get16(c, 7));
        case 0xA8: return this.readData(get32(c, 2), get32(c, 6));
        case 0x2B: this.needMedium(); this.stopAudio(AS_NONE); this.head = Math.min(get32(c, 2), this.disc.leadout); this.complete(); return;
        case 0x42: return this.readSubchannel();
        case 0x43: return this.readToc();
        case 0x45: return this.playLba(get32(c, 2), get16(c, 7));
        case 0xA5: return this.playLba(get32(c, 2), get32(c, 6));
        case 0x47: return this.playMsf();
        case 0x4A: return this.eventStatus();
        case 0x4B: return this.pauseResume((c[8] & 1) === 1);
        case 0x4E: this.needMedium(); this.stopAudio(AS_NONE); this.complete(); return;
        case 0x5A: return this.modeSense(c[2], get16(c, 7), 8);
        case 0x1A: return this.modeSense(c[2], c[4], 4);
        case 0x55: return this.modeSelect(get16(c, 7), 8);
        case 0x15: return this.modeSelect(c[4], 4);
        case 0xBB: { const s = get16(c, 2); this.speedKB = s === 0xFFFF ? 353 : Math.min(353, Math.max(176, s)); this.complete(); return; }
        case 0xBE: return this.readCd();
        default: sense(5, 0x20, 0);
      }
    } catch (e) { this.fail(e); }
  }

  requestSense() {
    const b = new Uint8Array(18);
    b[0] = 0x70; b[2] = this.senseKey; b[7] = 10; b[12] = this.asc; b[13] = this.ascq;
    // an audio play in progress is reported as additional sense 00/11 when nothing else is pending
    if (!this.senseKey) { this.update(); if (this.play.state === 'play') b[13] = 0x11; else if (this.play.state === 'pause') b[13] = 0x12; }
    const alloc = this.cdb[4];
    this.startDataIn(b, alloc);
  }
  inquiry() {
    const b = new Uint8Array(36);
    b[0] = 0x05; b[1] = 0x80; b[2] = 0x00; b[3] = 0x21; b[4] = 31;
    const s = (o, len, t) => { for (let i = 0; i < len; i++) b[o + i] = (t.charCodeAt(i) || 0x20) & 0x7F; };
    s(8, 8, 'ARM-PC  '); s(16, 16, 'CD-ROM DRIVE    '); s(32, 4, '1.00');
    this.startDataIn(b, this.cdb[4]);
  }
  startStop() {
    const c = this.cdb[4], loej = c & 2, start = c & 1;
    if (loej) {
      if (!start) { if (this.locked) sense(5, 0x53, 0x02); this.openTray(); }
      else if (this.trayOpen) this.closeTray();
    } else if (!start) this.stopAudio(AS_NONE);
    this.complete();
  }
  readCapacity() {
    this.needMedium();
    const b = new Uint8Array(8);
    put32(b, 0, this.disc.leadout - 1); put32(b, 4, 2048);
    this.startDataIn(b);
  }

  readData(lba, n) {
    this.needMedium();
    const d = this.disc;
    if (n === 0) { this.complete(); return; }
    if (lba + n > d.leadout) sense(5, 0x21, 0);
    const t = d.tracks[0];
    if (t.audio || lba + n > t.end) sense(5, 0x64, 0);     // illegal mode for this track (audio)
    this.stopAudio(AS_NONE);                                // a data read ends audio play
    this.head = lba + n;
    const go = () => {
      const img = d.data.img, off = lba * 2048, bytes = new Uint8Array(n * 2048);
      bytes.set(img.subarray(off, Math.min(off + n * 2048, img.length)));
      this.onActivity(lba, n);
      this.startDataIn(bytes, bytes.length, 2048);
    };
    if (d.data.ready(lba, n)) { go(); return; }
    const token = ++this.waitToken;
    this.mode = 4; this.status = BSY | DSC;
    this.onActivity(lba, 0);
    Promise.resolve(d.data.fetch(lba, n)).then((ok) => ok, () => false).then((ok) => {
      if (token !== this.waitToken || this.mode !== 4) return;
      if (!ok) { this.fail(new SenseError(3, 0x11, 0)); return; }  // unrecovered read error
      go();
    });
  }

  readCd() {
    // READ CD: byte 1 bits 2-4 expected sector type, 2-5 LBA, 6-8 length, 9 flags
    this.needMedium();
    const c = this.cdb, d = this.disc;
    const lba = get32(c, 2), n = (c[6] << 16) | (c[7] << 8) | c[8], type = (c[1] >> 2) & 7, flags = c[9];
    if (n === 0) { this.complete(); return; }
    if (lba + n > d.leadout) sense(5, 0x21, 0);
    const t = d.trackAt(lba);
    if (!t) sense(5, 0x21, 0);
    if (!t.audio) {
      if (type === 1 || !(flags & 0x10)) sense(5, 0x64, 0);
      return this.readData(lba, n);
    }
    if (type > 1) sense(5, 0x64, 0);
    if (!flags) { this.complete(); return; }
    this.stopAudio(AS_NONE);
    const go = () => {
      const out = new Uint8Array(n * 2352), dv = new DataView(out.buffer);
      for (let s = 0; s < n; s++) {
        const tr = d.trackAt(lba + s);
        const pcm = tr && tr.audio ? this.pcm(tr) : null;
        for (let i = 0; i < FRAMES_PER_SECTOR; i++) {
          const f = (lba + s - (tr ? tr.start : 0)) * FRAMES_PER_SECTOR + i;
          let l = 0, r = 0;
          if (pcm && f >= 0 && f < pcm.left.length) { l = pcm.left[f]; r = pcm.right[f]; if (pcm.float) { l = Math.round(Math.max(-1, Math.min(1, l)) * 32767); r = Math.round(Math.max(-1, Math.min(1, r)) * 32767); } }
          dv.setInt16(s * 2352 + i * 4, l, true); dv.setInt16(s * 2352 + i * 4 + 2, r, true);
        }
      }
      this.head = lba + n;
      this.onActivity(lba, n);
      this.startDataIn(out, out.length, 2352);
    };
    const need = [...new Set(Array.from({ length: n }, (_, i) => d.trackAt(lba + i)).filter((x) => x && x.audio))];
    const missing = need.filter((x) => !this.pcm(x));
    if (!missing.length) { go(); return; }
    const token = ++this.waitToken;
    this.mode = 4; this.status = BSY | DSC;
    Promise.all(missing.map((x) => this.loadPcm(x))).then(() => true, () => false).then((ok) => {
      if (token !== this.waitToken || this.mode !== 4) return;
      if (!ok) { this.fail(new SenseError(3, 0x11, 0)); return; }
      go();
    });
  }

  addr(b, o, lba, msf) {
    if (msf) { const [m, s, f] = lba2msf(lba); b[o] = 0; b[o + 1] = m; b[o + 2] = s; b[o + 3] = f; }
    else put32(b, o, lba >>> 0);
  }
  readToc() {
    this.needMedium();
    const c = this.cdb, d = this.disc, msf = (c[1] & 2) !== 0;
    let format = c[2] & 0x0F;
    if (!format && (c[9] >> 6)) format = c[9] >> 6;
    const alloc = get16(c, 7);
    if (format === 0) {
      const start = c[6];
      if (start > d.lastTrack && start !== 0xAA) sense(5, 0x24, 0);
      const list = d.tracks.filter((t) => start !== 0xAA && t.number >= start);
      const b = new Uint8Array(4 + (list.length + 1) * 8);
      let o = 4;
      for (const t of list) { b[o + 1] = 0x10 | d.control(t); b[o + 2] = t.number; this.addr(b, o + 4, t.start, msf); o += 8; }
      b[o + 1] = 0x10 | d.control(d.tracks[d.tracks.length - 1]); b[o + 2] = 0xAA; this.addr(b, o + 4, d.leadout, msf);
      b[0] = (b.length - 2) >> 8; b[1] = (b.length - 2) & 255; b[2] = d.firstTrack; b[3] = d.lastTrack;
      this.startDataIn(b, alloc);
      return;
    }
    if (format === 1) {
      const b = new Uint8Array(12);
      b[1] = 10; b[2] = 1; b[3] = 1;
      const t = d.tracks[0];
      b[5] = 0x10 | d.control(t); b[6] = t.number; this.addr(b, 8, t.start, msf);
      this.startDataIn(b, alloc);
      return;
    }
    sense(5, 0x24, 0);
  }

  readSubchannel() {
    const c = this.cdb, msf = (c[1] & 2) !== 0, subq = (c[2] & 0x40) !== 0, format = c[3], alloc = get16(c, 7);
    this.needMedium();
    this.update();
    const status = this.audioStatus();
    if (!subq) { const b = new Uint8Array(4); b[1] = status; this.startDataIn(b, alloc); return; }
    const d = this.disc;
    if (format === 1) {
      const b = new Uint8Array(16);
      b[1] = status; b[3] = 12; b[4] = 0x01;
      const lba = this.posLba();
      const t = d.trackAt(lba) || d.tracks[d.tracks.length - 1];
      b[5] = 0x10 | d.control(t); b[6] = t.number; b[7] = lba < t.start ? 0 : 1;
      this.addr(b, 8, lba, msf);
      const rel = lba - t.start;                   // in the pregap: counts down to index 1
      if (msf) { const a = Math.abs(rel); b[12] = 0; b[13] = Math.floor(a / 4500); b[14] = Math.floor(a / 75) % 60; b[15] = a % 75; }
      else put32(b, 12, rel >>> 0);
      this.startDataIn(b, alloc);
      return;
    }
    if (format === 2 || format === 3) {
      const b = new Uint8Array(24);
      b[1] = status; b[3] = 20; b[4] = format;
      let code = '';
      if (format === 2) code = d.mcn;
      else { const t = d.track(c[6]); if (!t) sense(5, 0x24, 0); b[5] = 0x10 | d.control(t); b[6] = t.number; code = t.isrc; }
      if (code) { b[8] = 0x80; for (let i = 0; i < code.length && i < 15; i++) b[9 + i] = code.charCodeAt(i); }
      this.startDataIn(b, alloc);
      return;
    }
    sense(5, 0x24, 0);
  }

  eventStatus() {
    const c = this.cdb;
    if (!(c[1] & 1)) sense(5, 0x24, 0);            // only polled operation
    const want = c[4], alloc = get16(c, 7);
    if (!(want & 0x10)) { const b = new Uint8Array(4); b[1] = 2; b[2] = 0x80; b[3] = 0x10; this.startDataIn(b, alloc); return; }
    const b = new Uint8Array(8);
    b[1] = 6; b[2] = 0x04; b[3] = 0x10;
    b[4] = this.mediaEvent; this.mediaEvent = 0;
    b[5] = (this.trayOpen ? 1 : 0) | (this.disc && !this.trayOpen ? 2 : 0);
    this.startDataIn(b, alloc);
  }

  // mode pages
  page(code, pc) {
    const chg = pc === 1;
    switch (code) {
      case 0x01: return Uint8Array.of(0x01, 0x06, 0, chg ? 0 : 5, 0, 0, 0, 0);
      case 0x0D: return Uint8Array.of(0x0D, 0x06, 0, chg ? 0 : 0x05, 0, chg ? 0 : 60, 0, chg ? 0 : 75);
      case 0x0E: {
        const v = chg ? [0x0F, 0xFF, 0x0F, 0xFF] : pc === 2 ? [0x01, 0xFF, 0x02, 0xFF] : this.volume;
        return Uint8Array.of(0x0E, 0x0E, chg ? 0 : 0x04, 0, 0, 0, 0, 0, v[0], v[1], v[2], v[3], 0, 0, 0, 0);
      }
      case 0x2A: {
        if (chg) return new Uint8Array(20).fill(0).map((_, i) => (i === 0 ? 0x2A : i === 1 ? 0x12 : 0));
        const b = new Uint8Array(20);
        b[0] = 0x2A; b[1] = 0x12;
        b[4] = 0x01;                                // audio play
        b[5] = 0x01 | 0x02;                         // CD-DA commands, accurate CD-DA stream
        b[6] = 0x01 | (this.locked ? 0x02 : 0) | 0x08 | (1 << 5);   // lock, lock state, eject, tray
        b[7] = 0x03;                                // separate volume / mute per channel
        b[8] = 353 >> 8; b[9] = 353 & 255;          // max speed (2x) KB/s
        b[10] = 1; b[11] = 0;                       // 256 volume levels
        b[12] = 0; b[13] = 128;                     // 128 KB buffer
        b[14] = this.speedKB >> 8; b[15] = this.speedKB & 255;
        return b;
      }
    }
    return null;
  }
  modeSense(p, alloc, hdr) {
    const code = p & 0x3F, pc = p >> 6;
    if (pc === 3) sense(5, 0x39, 0);                // saving parameters not supported
    let pages;
    if (code === 0x3F) pages = [0x01, 0x0D, 0x0E, 0x2A].map((x) => this.page(x, pc));
    else { const pg = this.page(code, pc); if (!pg) sense(5, 0x24, 0); pages = [pg]; }
    const body = pages.reduce((n, x) => n + x.length, 0);
    const b = new Uint8Array(hdr + body);
    const medium = !this.disc ? (this.trayOpen ? 0x71 : 0x70) : 0x03;
    if (hdr === 8) { b[0] = (b.length - 2) >> 8; b[1] = (b.length - 2) & 255; b[2] = medium; }
    else { b[0] = b.length - 1; b[1] = medium; }
    let o = hdr;
    for (const x of pages) { b.set(x, o); o += x.length; }
    this.startDataIn(b, alloc);
  }
  modeSelect(n, hdr) {
    if (n === 0) { this.complete(); return; }
    this.startDataOut(n, (b) => {
      let o = hdr + (hdr === 8 ? get16(b, 6) : b[3]);
      while (o + 2 <= b.length) {
        const code = b[o] & 0x3F, len = b[o + 1];
        if (code === 0x0E && o + 12 <= b.length) {
          this.volume = [b[o + 8] & 0x0F, b[o + 9], b[o + 10] & 0x0F, b[o + 11]];
        } else if (code !== 0x01 && code !== 0x0D && code !== 0x2A) sense(5, 0x26, 0);
        o += 2 + len;
      }
    });
  }

  // ------------------------------------------------------------ audio
  audioStatus() {
    const p = this.play;
    if (p.state === 'play') return AS_PLAYING;
    if (p.state === 'pause') return AS_PAUSED;
    const s = p.status;
    if (s === AS_DONE || s === AS_ERROR) { p.status = AS_NONE; return s; }   // reported once
    return AS_NONE;
  }
  playLba(lba, n) {
    this.needMedium();
    const d = this.disc;
    let start = lba === 0xFFFFFFFF ? this.posLba() : lba;
    if (n === 0) { this.complete(); return; }
    this.startPlay(start, start + n);
  }
  playMsf() {
    this.needMedium();
    const c = this.cdb;
    const start = (c[3] === 0xFF && c[4] === 0xFF && c[5] === 0xFF) ? this.posLba() : msf2lba(c[3], c[4], c[5]);
    const end = msf2lba(c[6], c[7], c[8]);
    if (start === end) { this.complete(); return; }
    if (start > end) sense(5, 0x24, 0);
    this.startPlay(start, end);
  }
  startPlay(start, end) {
    const d = this.disc;
    if (start < 0 || end > d.leadout || start >= d.leadout) sense(5, 0x21, 0);
    const t = d.trackAt(start);
    if (!t || !t.audio) sense(5, 0x64, 0);          // PLAY on the data track
    this.update();
    this.play = { state: 'play', frame: start * FRAMES_PER_SECTOR, endFrame: end * FRAMES_PER_SECTOR, baseNs: this.m.timeNs(), hold: false, status: AS_PLAYING };
    this.checkHold(this.m.timeNs());
    this.complete();
    this.changed();
  }
  pauseResume(resume) {
    this.needMedium();
    const p = this.play;
    this.update();
    if (resume) {
      if (p.state !== 'pause') { if (p.state === 'play') { this.complete(); return; } sense(5, 0x2C, 0); }
      p.state = 'play'; p.baseNs = this.m.timeNs(); this.checkHold(p.baseNs);
    } else {
      if (p.state !== 'play') { if (p.state === 'pause') { this.complete(); return; } sense(5, 0x2C, 0); }
      p.frame = this.framePos(this.m.timeNs()); p.state = 'pause';
    }
    this.complete();
    this.changed();
  }
  stopAudio(status) {
    const p = this.play;
    if (p.state === 'play' || p.state === 'pause') {
      this.head = Math.floor(this.framePos(this.m.timeNs()) / FRAMES_PER_SECTOR);
      this.play = { state: 'idle', frame: 0, endFrame: 0, baseNs: 0, hold: false, status };
      this.changed();
    }
  }
  framePos(now) {
    const p = this.play;
    if (p.state !== 'play' || p.hold) return p.frame;
    return Math.min(p.endFrame, p.frame + (now - p.baseNs) / NS_PER_FRAME);
  }
  posLba() {
    const p = this.play;
    if (p.state === 'play' || p.state === 'pause') return Math.min(Math.floor(this.framePos(this.m.timeNs()) / FRAMES_PER_SECTOR), this.disc ? this.disc.leadout - 1 : 0);
    return this.head;
  }
  /** bring the play state up to now: end of play, holds for undecoded audio */
  update() {
    const p = this.play;
    if (p.state !== 'play') return;
    const now = this.m.timeNs();
    if (p.hold) { this.checkHold(now); return; }
    const f = this.framePos(now);
    if (f >= p.endFrame) {
      this.head = Math.floor(p.endFrame / FRAMES_PER_SECTOR) - 1;
      this.play = { state: 'idle', frame: 0, endFrame: 0, baseNs: 0, hold: false, status: AS_DONE };
      this.changed();
      return;
    }
    this.checkHold(now);
  }
  /** the audio at the play position must be decoded, else hold there until it is */
  checkHold(now) {
    const p = this.play, d = this.disc;
    if (p.state !== 'play' || !d) return;
    const f = p.hold ? p.frame : this.framePos(now);
    const lba = Math.floor(f / FRAMES_PER_SECTOR);
    const t = d.trackAt(lba);
    const inPregap = t && lba < t.start;
    const ok = !t || !t.audio || inPregap || !!this.pcm(t);
    if (!ok) {
      if (!p.hold) { p.frame = f; p.hold = true; this.changed(); }
      p.baseNs = now;
      this.loadPcm(t).then(() => {
        if (this.play !== p || !p.hold) return;
        p.hold = false; p.baseNs = this.m.timeNs(); this.changed();
      }, () => {
        if (this.play !== p) return;
        this.play = { state: 'idle', frame: 0, endFrame: 0, baseNs: 0, hold: false, status: AS_ERROR };
        this.changed();
      });
      return;
    }
    if (p.hold) { p.hold = false; p.baseNs = now; this.changed(); }
    // decode the next track ahead of time (30 s before the boundary)
    if (t && t.audio && !inPregap) {
      const left = (t.end * FRAMES_PER_SECTOR - f) * NS_PER_FRAME;
      const next = d.tracks[d.tracks.indexOf(t) + 1];
      if (next && next.audio && left < 30e9 && next.start * FRAMES_PER_SECTOR < p.endFrame && !this.pcm(next)) this.loadPcm(next).catch(() => {});
    } else if (inPregap && t.audio && !this.pcm(t)) this.loadPcm(t).catch(() => {});
  }
  pcm(t) {
    let e = this.pcmCache.get(t.number);
    if (e) return e;
    const got = this.disc.audio.get(t.number);
    if (!got) return null;
    e = { left: got.left, right: got.right || got.left, float: !(got.left instanceof Int16Array) };
    this.pcmCache.set(t.number, e);
    // keep at most three decoded tracks (a 4-minute track is ~85 MB as float)
    if (this.pcmCache.size > 3) for (const k of this.pcmCache.keys()) { if (this.pcmCache.size <= 3) break; if (k !== t.number) this.pcmCache.delete(k); }
    return e;
  }
  loadPcm(t) {
    if (this.pcm(t)) return Promise.resolve();
    if (!t._loading) {
      t._loading = Promise.resolve(this.disc.audio.request(t.number)).then(() => { t._loading = null; if (!this.pcm(t)) throw new Error('audio track ' + t.number + ' unavailable'); },
        (e) => { t._loading = null; throw e; });
    }
    return t._loading;
  }

  /** Mix CD audio into L/R (dev/audio.mjs): n samples from t0 ns, dt ns apart. */
  render(L, R, n, t0, dt) {
    const p = this.play, d = this.disc;
    if (p.state !== 'play' || p.hold || !d) return;
    // analog out: the CD line into the SB16 (mixer CD x master), or the front headphone jack
    let gl, gr;
    if (this.headphones) { gl = gr = this.knob; }
    else {
      const x = this.m.sb ? this.m.sb.mixer : null;
      if (!x) { gl = gr = 1; } else { gl = volGain(x[0x36]) * volGain(x[0x30]); gr = volGain(x[0x37]) * volGain(x[0x31]); }
    }
    const v = this.volume;
    const p0l = (v[0] & 1 ? v[1] / 255 : 0), p0r = (v[0] & 2 ? v[1] / 255 : 0);   // port 0 (left out) from channel 0/1
    const p1l = (v[2] & 1 ? v[3] / 255 : 0), p1r = (v[2] & 2 ? v[3] / 255 : 0);   // port 1 (right out)
    if (!(gl || gr) || !(p0l || p0r || p1l || p1r)) return;
    let track = null, pcm = null, tf0 = 0;
    for (let i = 0; i < n; i++) {
      const t = t0 + i * dt;
      if (t < p.baseNs) continue;
      const f = p.frame + (t - p.baseNs) / NS_PER_FRAME;
      if (f >= p.endFrame) break;
      const lba = Math.floor(f / FRAMES_PER_SECTOR);
      if (!track || lba < track.start || lba >= track.end) {
        track = d.trackAt(lba);
        if (!track || !track.audio || lba < track.start) { track = null; continue; }
        pcm = this.pcm(track);
        if (!pcm) { this.update(); return; }        // not decoded: hold (next render continues)
        tf0 = track.start * FRAMES_PER_SECTOR;
      }
      const x = f - tf0, k = Math.floor(x), a = x - k;
      const Ls = pcm.left, Rs = pcm.right;
      if (k + 1 >= Ls.length) continue;
      let l = Ls[k] + (Ls[k + 1] - Ls[k]) * a, r = Rs[k] + (Rs[k + 1] - Rs[k]) * a;
      if (!pcm.float) { l /= 32768; r /= 32768; }
      L[i] += (l * p0l + r * p0r) * gl;
      R[i] += (l * p1l + r * p1r) * gr;
    }
  }
}
