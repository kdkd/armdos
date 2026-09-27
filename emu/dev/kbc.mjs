// 8042 keyboard controller (ports 0x60/0x64) + AT keyboard + PS/2 mouse (aux).
// Keyboard bytes are scan code set 1 and raise IRQ1; mouse bytes raise IRQ12.
// Bytes wait in a queue and are moved into the one-byte output buffer when it
// is empty, KBC_DELAY_NS after the previous byte was read.

import { makeSeq, breakSeq } from './keymap.mjs';

const KBC_DELAY_NS = 20000;       // 20 us between bytes

export class KBC {
  constructor(m) {
    this.m = m;                     // machine: pic, timeNs(), reschedule(), resetRequest(), onLeds
    this.mousePresent = true;       // false: nothing on the PS/2 port (Machine.setHardware)
    this.reset();
  }
  reset() {
    this.q = [];                    // pending bytes: (byte | aux<<8)
    this.obf = 0; this.out = 0; this.outAux = 0;
    this.nextAt = 0;                // earliest time to load the next byte
    this.cmdByte = 0x47;            // kbd IRQ on, sys flag, translate; aux IRQ on (bit 1)
    this.cmdByte |= 0x02;
    this.pendingCmd = 0;            // controller command awaiting a data byte (0x60)
    this.kbdCmd = 0;                // keyboard command awaiting argument
    this.auxCmd = 0;                // mouse command awaiting argument
    this.lastWasCmd = 0;
    this.kbdEnabled = true; this.kbdScanning = true;
    this.mouse = { enabled: false, rate: 100, res: 2, scale: 0, dx: 0, dy: 0, buttons: 0, dirty: false, remote: false, lastPkt: 0 };
    this.leds = 0;
    this.m.pic.lower(1); this.m.pic.lower(12);
  }

  // ---- host input
  keyDown(code) {
    if (code === 'ControlLeft' || code === 'ControlRight') this.ctrlHeld = (this.ctrlHeld | 0) + 1;
    // Ctrl+Pause is Ctrl-Break: a real keyboard sends E0 46 / E0 C6 instead of the E1 sequence
    const s = (code === 'Pause' && this.ctrlHeld > 0) ? [0xE0, 0x46, 0xE0, 0xC6] : makeSeq(code);
    if (s) this.pushKbd(s); return !!s;
  }
  keyUp(code) {
    if ((code === 'ControlLeft' || code === 'ControlRight') && this.ctrlHeld > 0) this.ctrlHeld--;
    const s = breakSeq(code); if (s) this.pushKbd(s); return !!s;
  }
  pushKbd(bytes) {
    if (!this.kbdScanning) return;
    if (this.q.length > 256) return;            // overflow: drop
    for (const b of bytes) this.q.push(b);
    this.kick();
  }
  mouseMove(dx, dy) { if (!this.mousePresent) return; const mo = this.mouse; mo.dx += dx; mo.dy += dy; mo.dirty = true; this.m.reschedule(); }
  mouseButtons(bits) { if (!this.mousePresent) return; const mo = this.mouse; if (mo.buttons !== (bits & 7)) { mo.buttons = bits & 7; mo.dirty = true; this.m.reschedule(); } }
  queueEmpty() { return this.q.length === 0 && !this.obf; }

  kick() { if (!this.obf) this.m.reschedule(); }

  // ---- scheduling
  nextEventNs() {
    if (this.obf) return Infinity;
    if (this.q.length) return Math.max(this.nextAt, 0);
    const mo = this.mouse;
    if (mo.dirty && mo.enabled && !mo.remote && !(this.cmdByte & 0x20)) return Math.max(this.nextAt, mo.lastPkt + 1e9 / mo.rate);
    return Infinity;
  }
  service() {
    const now = this.m.timeNs();
    if (this.obf || now < this.nextAt) return;
    if (!this.q.length) {
      const mo = this.mouse;
      if (mo.dirty && mo.enabled && !mo.remote && now >= mo.lastPkt + 1e9 / mo.rate - 1) { this.mousePacket(); mo.lastPkt = now; }
      if (!this.q.length) return;
    }
    const v = this.q.shift();
    this.obf = 1; this.out = v & 0xFF; this.outAux = v >> 8;
    if (this.outAux) { if (this.cmdByte & 2) this.m.pic.raise(12); }
    else if (this.cmdByte & 1) this.m.pic.raise(1);
  }
  mousePacket() {
    const mo = this.mouse;
    let dx = Math.max(-255, Math.min(255, Math.round(mo.dx))), dy = Math.max(-255, Math.min(255, Math.round(-mo.dy)));
    mo.dx -= dx; mo.dy += dy;
    if (Math.abs(mo.dx) < 1 && Math.abs(mo.dy) < 1) { mo.dx = 0; mo.dy = 0; mo.dirty = false; }
    const b0 = 0x08 | mo.buttons | (dx < 0 ? 0x10 : 0) | (dy < 0 ? 0x20 : 0);
    this.q.push(0x100 | b0, 0x100 | (dx & 0xFF), 0x100 | (dy & 0xFF));
  }

  // ---- ports
  read(port) {
    if (port === 0x64) {
      return this.obf | (this.lastWasCmd ? 0x08 : 0) | 0x04 | 0x10 | (this.obf && this.outAux ? 0x20 : 0);
    }
    if (port === 0x60) {
      const v = this.out;
      if (this.obf) {
        this.obf = 0;
        this.m.pic.lower(this.outAux ? 12 : 1);
        this.nextAt = this.m.timeNs() + KBC_DELAY_NS;
        if (this.q.length || this.mouse.dirty) this.m.reschedule();
      }
      return v;
    }
    return 0xFF;
  }
  // reply bytes go to the front of the queue (responses precede pending keys)
  reply(bytes, aux = 0) {
    const tagged = bytes.map((b) => b | (aux << 8));
    this.q.unshift(...tagged);
    this.kick();
  }
  write(port, v) {
    if (port === 0x64) {
      this.lastWasCmd = 1;
      switch (v) {
        case 0x20: this.reply([this.cmdByte]); break;
        case 0x60: case 0xD1: case 0xD2: case 0xD3: case 0xD4: this.pendingCmd = v; break;
        case 0xA7: this.cmdByte |= 0x20; break;
        case 0xA8: this.cmdByte &= ~0x20; break;
        case 0xA9: this.reply([0x00]); break;
        case 0xAA: this.reply([0x55]); break;
        case 0xAB: this.reply([0x00]); break;
        case 0xAD: this.cmdByte |= 0x10; this.kbdEnabled = false; break;
        case 0xAE: this.cmdByte &= ~0x10; this.kbdEnabled = true; break;
        case 0xC0: this.reply([0xBF]); break;
        case 0xD0: this.reply([0xDF]); break;
        case 0xE0: this.reply([0x00]); break;
        default:
          if (v >= 0xF0) { if (!(v & 1)) this.m.resetRequest(); }   // pulse reset line
      }
      return;
    }
    // port 0x60
    this.lastWasCmd = 0;
    const pc = this.pendingCmd;
    if (pc) {
      this.pendingCmd = 0;
      switch (pc) {
        case 0x60: this.cmdByte = v; this.kbdEnabled = !(v & 0x10); return;
        case 0xD1: if (!(v & 1)) this.m.resetRequest(); return;
        case 0xD2: this.reply([v]); return;
        case 0xD3: this.reply([v], 1); return;
        case 0xD4: this.mouseCmd(v); return;
      }
    }
    this.keyboardCmd(v);
  }
  keyboardCmd(v) {
    if (this.kbdCmd) {
      const c = this.kbdCmd; this.kbdCmd = 0;
      if (c === 0xED) { this.leds = v & 7; this.m.leds(this.leds); }
      if (c === 0xF0 && v === 0) { this.reply([0xFA, 0x01]); return; }
      this.reply([0xFA]); return;
    }
    switch (v) {
      case 0xED: case 0xF3: case 0xF0: this.kbdCmd = v; this.reply([0xFA]); break;
      case 0xEE: this.reply([0xEE]); break;
      case 0xF2: this.reply([0xFA, 0xAB, 0x41]); break;
      case 0xF4: this.kbdScanning = true; this.reply([0xFA]); break;
      case 0xF5: this.kbdScanning = false; this.q = this.q.filter((b) => b >> 8); this.reply([0xFA]); break;
      case 0xF6: this.kbdScanning = true; this.reply([0xFA]); break;
      case 0xFF: this.q = this.q.filter((b) => b >> 8); this.kbdScanning = true; this.reply([0xFA, 0xAA]); break;
      default: this.reply([0xFA]);
    }
  }
  mouseCmd(v) {
    const mo = this.mouse;
    if (!this.mousePresent) { this.auxCmd = 0; this.reply([0xFE], 1); return; }   // no device: the 8042 times out
    if (this.auxCmd) {
      const c = this.auxCmd; this.auxCmd = 0;
      if (c === 0xF3) mo.rate = Math.max(10, Math.min(200, v));
      if (c === 0xE8) mo.res = v & 3;
      this.reply([0xFA], 1); return;
    }
    switch (v) {
      case 0xFF: Object.assign(mo, { enabled: false, rate: 100, res: 2, scale: 0, dx: 0, dy: 0, dirty: false, remote: false });
        this.q = this.q.filter((b) => !(b >> 8)); this.reply([0xFA, 0xAA, 0x00], 1); break;
      case 0xF6: Object.assign(mo, { enabled: false, rate: 100, res: 2, scale: 0 }); this.reply([0xFA], 1); break;
      case 0xF5: mo.enabled = false; this.reply([0xFA], 1); break;
      case 0xF4: mo.enabled = true; mo.dx = 0; mo.dy = 0; this.reply([0xFA], 1); break;
      case 0xF3: case 0xE8: this.auxCmd = v; this.reply([0xFA], 1); break;
      case 0xF2: this.reply([0xFA, 0x00], 1); break;
      case 0xE6: mo.scale = 0; this.reply([0xFA], 1); break;
      case 0xE7: mo.scale = 1; this.reply([0xFA], 1); break;
      case 0xEA: mo.remote = false; this.reply([0xFA], 1); break;
      case 0xF0: mo.remote = true; this.reply([0xFA], 1); break;
      case 0xE9: this.reply([0xFA, (mo.remote ? 0x40 : 0) | (mo.enabled ? 0x20 : 0) | (mo.scale ? 0x10 : 0) | (mo.buttons & 7), mo.res, mo.rate], 1); break;
      case 0xEB: {
        this.reply([0xFA], 1);
        const saveQ = this.q; this.q = []; this.mousePacket(); const pkt = this.q; this.q = saveQ;
        this.q.splice(1, 0, ...pkt); break;
      }
      case 0xEC: case 0xEE: this.reply([0xFA], 1); break;
      default: this.reply([0xFE], 1);
    }
  }
}
