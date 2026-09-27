// A National Semiconductor NS16550A UART (docs/MODEM.md): COM2 at 0x2F8-0x2FF, IRQ 3.
//
// Registers (offset from the base port):
//   0 RBR (read) / THR (write) / DLL (DLAB=1)     4 MCR  DTR RTS OUT1 OUT2 LOOP
//   1 IER (ERBFI ETBEI ELSI EDSSI) / DLM (DLAB=1) 5 LSR  DR OE PE FE BI THRE TEMT RXFE
//   2 IIR (read) / FCR (write)                     6 MSR  DCTS DDSR TERI DDCD CTS DSR RI DCD
//   3 LCR  word length, stop bits, parity, break, DLAB   7 SCR
//
// Timing is real, in emulated time: a character takes (start + data + parity + stop bits)
// x divisor x 16 / 1.8432 MHz on the wire, so THRE/TEMT and the receive FIFO fill at the
// programmed baud rate. 16-byte receive and transmit FIFOs (FCR bit 0), trigger levels
// 1/4/8/14, the character timeout interrupt (4 character times with data below the
// trigger and no FIFO activity), the THRE interrupt with its IIR-read / THR-write clearing
// rules, line status errors (overrun) and modem status deltas. OUT2 gates the IRQ onto the
// bus as on a PC. Loopback (MCR bit 4) wires TX->RX and RTS->CTS, DTR->DSR, OUT1->RI,
// OUT2->DCD internally.
//
// What is on the other end of the wire is a "backend" (dev/modem.mjs), with:
//   backend.txByte(byte)            a character finished shifting out of TSR
//   backend.txCharNs()              extra minimum time per transmitted char (a modem's line
//                                   rate when it is online), 0 if none
//   backend.mcrChanged(mcr, old)    DTR/RTS/OUT1 changed
//   backend.rxReady()               the receive FIFO has room again (flow control)
// and the backend drives the receiver with receive(byte) / rxFree() and the modem status
// inputs with setModemLines({ cts, dsr, ri, dcd }).
//
// IRQ policy: the INTR line is the OR of the enabled, pending conditions. The PC's PIC is
// edge triggered; we raise on every rising edge *and* whenever a new event (a received
// character, THR becoming empty, a modem status change) happens while the line is already
// high. Real hardware would give no new edge there; this leniency rescues handlers that
// service only one condition per interrupt, and costs a well-written handler (one that
// loops until IIR bit 0 is set) nothing but an occasional spurious "no interrupt" IIR.

const CLOCK_HZ = 1843200;
const TRIG = [1, 4, 8, 14];

export class UART16550 {
  constructor(m, { base = 0x2F8, irq = 3, name = 'COM2' } = {}) {
    this.m = m; this.base = base; this.irq = irq; this.name = name;
    this.backend = null;
    this.baudOverride = 0;
    this.ext = { cts: false, dsr: false, ri: false, dcd: false };   // modem status inputs
    this.rx = []; this.rxErr = [];         // receive FIFO (bytes, per-byte error bits)
    this.tx = [];                          // transmit FIFO / holding register
    this.reset();
  }

  reset() {
    this.ier = 0; this.lcr = 0; this.mcr = 0; this.scr = 0; this.fcr = 0;
    this.dll = 0x0C; this.dlm = 0;         // 9600 baud after a BIOS; 0 on real power-up, harmless
    this.rx.length = 0; this.rxErr.length = 0; this.tx.length = 0;
    this.lsrErr = 0;                       // OE/PE/FE/BI latched until LSR is read
    this.tsr = -1; this.tsrDoneNs = Infinity;
    this.thriPending = false;
    this.msrDelta = 0;
    this.lastMsr = this.msrLines();
    this.timeoutNs = Infinity; this.timedOut = false;
    this.intr = false;
    this.m.pic.lower(this.irq);
    if (this.backend) this.backend.mcrChanged(this.mcr, 0xFF);
  }

  // ---------------------------------------------------------------- helpers
  get fifo() { return (this.fcr & 1) !== 0; }
  divisor() { return (this.dlm << 8 | this.dll) || 0x10000; }
  bitsPerChar() { return 1 + 5 + (this.lcr & 3) + ((this.lcr & 8) ? 1 : 0) + ((this.lcr & 4) ? ((this.lcr & 3) ? 2 : 1.5) : 1); }
  /** Duration of one character at the programmed baud rate and format, ns. */
  charNs() { return this.bitsPerChar() * 1e9 / this.baud(); }
  /** baudOverride (tests): run the wire at this rate whatever the divisor says. */
  baud() { return this.baudOverride || CLOCK_HZ / 16 / this.divisor(); }
  rxLimit() { return this.fifo ? 16 : 1; }
  /** Room in the receive FIFO (the backend holds characters rather than overrun it). */
  rxFree() { return this.rxLimit() - this.rx.length; }
  get loop() { return (this.mcr & 0x10) !== 0; }

  msrLines() {
    if (this.loop) {
      const c = this.mcr;
      return ((c & 2) ? 0x10 : 0) | ((c & 1) ? 0x20 : 0) | ((c & 4) ? 0x40 : 0) | ((c & 8) ? 0x80 : 0);
    }
    const e = this.ext;
    return (e.cts ? 0x10 : 0) | (e.dsr ? 0x20 : 0) | (e.ri ? 0x40 : 0) | (e.dcd ? 0x80 : 0);
  }
  msrUpdate() {
    const now = this.msrLines(), was = this.lastMsr, ch = now ^ was;
    if (!ch) return;
    let d = 0;
    if (ch & 0x10) d |= 1;
    if (ch & 0x20) d |= 2;
    if ((was & 0x40) && !(now & 0x40)) d |= 4;        // TERI: trailing edge of RI only
    if (ch & 0x80) d |= 8;
    this.lastMsr = now;
    if (d) { this.msrDelta |= d; this.update(true); }
  }
  /** Backend: the modem's status outputs. */
  setModemLines(l) {
    Object.assign(this.ext, l);
    this.msrUpdate();
  }

  lsr() {
    let v = this.lsrErr;
    if (this.rx.length) v |= 1;
    if (this.tx.length === 0) v |= 0x20;
    if (this.tx.length === 0 && this.tsr < 0) v |= 0x40;
    if (this.fifo && this.rxErr.some((e) => e)) v |= 0x80;
    return v;
  }

  // ---------------------------------------------------------------- interrupts
  timeoutActive() { return this.timedOut && this.fifo && this.rx.length > 0; }
  source() {        // IIR bits 3:0, highest priority first
    const ier = this.ier;
    if ((ier & 4) && (this.lsrErr & 0x1E)) return 0x06;
    if (ier & 1) {
      if (this.fifo ? this.rx.length >= TRIG[this.fcr >> 6] : this.rx.length > 0) return 0x04;
      if (this.timeoutActive()) return 0x0C;
    }
    if ((ier & 2) && this.thriPending) return 0x02;
    if ((ier & 8) && this.msrDelta) return 0x00;
    return 0x01;
  }
  update(event = false) {
    const on = this.source() !== 0x01;
    const gated = on && (this.mcr & 8) !== 0 && !this.loop;
    if (gated && (!this.intr || event)) this.m.pic.raise(this.irq);
    else if (!gated && this.intr) this.m.pic.lower(this.irq);
    this.intr = gated;
  }
  armTimeout() {
    this.timedOut = false;
    if (this.fifo && this.rx.length) {
      this.timeoutNs = this.m.timeNs() + 4 * this.charNs();
      this.m.reschedule();
    } else this.timeoutNs = Infinity;
  }

  // ---------------------------------------------------------------- receiver
  /** Backend (or loopback): a character arrives. Returns false if it was lost (overrun). */
  receive(b, err = 0) {
    if (this.rx.length >= this.rxLimit()) {
      this.lsrErr |= 2;                    // overrun: the character in the shift register is lost
      this.update(true);
      return false;
    }
    this.rx.push(b & 0xFF); this.rxErr.push(err);
    if (err) this.lsrErr |= err;
    this.armTimeout();
    this.update(true);
    return true;
  }
  readRbr() {
    if (!this.rx.length) return 0;
    const v = this.rx.shift(); this.rxErr.shift();
    this.armTimeout();
    this.update();
    if (this.backend && this.rx.length === this.rxLimit() - 1) this.backend.rxReady?.();
    return v;
  }

  // ---------------------------------------------------------------- transmitter
  startTx() {
    if (this.tsr >= 0 || !this.tx.length) return;
    this.tsr = this.tx.shift();
    let ns = this.charNs();
    const extra = this.loop || !this.backend ? 0 : this.backend.txCharNs();
    if (extra > ns) ns = extra;
    this.tsrDoneNs = this.m.timeNs() + ns;
    if (!this.tx.length) { this.thriPending = true; this.update(true); }
    this.m.reschedule();
  }
  finishTx() {
    const b = this.tsr;
    this.tsr = -1; this.tsrDoneNs = Infinity;
    if (this.loop) this.receive(b);
    else if (this.lcr & 0x40) { /* break: nothing meaningful goes out */ }
    else if (this.backend) this.backend.txByte(b);
    this.startTx();
  }

  // ---------------------------------------------------------------- scheduler
  nextEventNs() {
    let t = this.tsrDoneNs;
    if (this.timeoutNs < t && (this.ier & 1)) t = this.timeoutNs;
    return t;
  }
  service() {
    const now = this.m.timeNs();
    if (this.tsrDoneNs <= now) this.finishTx();
    if (this.timeoutNs <= now) { this.timeoutNs = Infinity; if (this.rx.length) { this.timedOut = true; this.update(true); } }
  }

  // ---------------------------------------------------------------- ports
  read(port) {
    const dlab = this.lcr & 0x80;
    switch (port - this.base) {
      case 0: return dlab ? this.dll : this.readRbr();
      case 1: return dlab ? this.dlm : this.ier;
      case 2: {
        const s = this.source();
        if (s === 0x02) { this.thriPending = false; this.update(); }
        return s | (this.fifo ? 0xC0 : 0);
      }
      case 3: return this.lcr;
      case 4: return this.mcr;
      case 5: {
        const v = this.lsr();
        if (this.lsrErr) { this.lsrErr = 0; this.update(); }
        return v;
      }
      case 6: {
        const v = this.msrLines() | this.msrDelta;
        if (this.msrDelta) { this.msrDelta = 0; this.update(); }
        return v;
      }
      case 7: return this.scr;
    }
    return 0xFF;
  }
  write(port, v) {
    const dlab = this.lcr & 0x80;
    switch (port - this.base) {
      case 0:
        if (dlab) { this.dll = v; return; }
        if (this.tx.length >= (this.fifo ? 16 : 1)) return;      // overrun of the THR: lost
        this.tx.push(v);
        this.thriPending = false;
        this.startTx();
        this.update();
        return;
      case 1:
        if (dlab) { this.dlm = v; return; }
        {
          const old = this.ier;
          this.ier = v & 0x0F;
          // enabling ETBEI with THR empty raises THRE at once (the 16550 does this)
          if ((v & 2) && !(old & 2) && this.tx.length === 0) this.thriPending = true;
          this.update(true);
        }
        return;
      case 2: {
        const wasFifo = this.fifo;
        if ((v & 1) !== (this.fcr & 1)) { this.rx.length = 0; this.rxErr.length = 0; this.tx.length = 0; }
        this.fcr = v & 0xC9;
        if (v & 2) { this.rx.length = 0; this.rxErr.length = 0; this.timeoutNs = Infinity; this.timedOut = false; }
        if (v & 4) this.tx.length = 0;
        if (!wasFifo && this.fifo || (v & 2)) this.backend?.rxReady?.();
        this.update();
        return;
      }
      case 3: this.lcr = v; return;
      case 4: {
        const old = this.mcr;
        this.mcr = v & 0x1F;
        if (this.backend && ((old ^ this.mcr) & 0x17)) this.backend.mcrChanged(this.mcr, old);
        this.msrUpdate();
        this.update();
        return;
      }
      case 7: this.scr = v; return;
    }
  }
}
