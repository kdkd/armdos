// PIC: the two cascaded 8259As of a PC/AT, presented as ARCH.md §4 describes:
//   read 0x20  = number of the highest-priority pending unmasked IRQ (0-15),
//                or 0xFF; marks it in service (acknowledge)
//   write 0x20 value 0x20 = non-specific EOI for the master levels (IRQ 0-7 and
//                the cascade level used by IRQ 8-15); 0x60+n = specific EOI
//   write 0xA0 value 0x20 = non-specific EOI for IRQ 8-15 (0x60+n specific)
//   0x21 / 0xA1 = mask registers (bit set = masked)
// Priority is the PC's: 0, 1, 8..15 (through the cascade on level 2), 3..7.
// Edge-triggered: raise() latches a request, lower() withdraws it if not yet
// acknowledged. ICW1-4 initialisation sequences are accepted and ignored.
// Leniencies (documented in emu/README.md): the cascade level is "any of IRQ
// 8-15 in service" (no separate master bit), so an EOI to 0x20 alone also
// finishes a slave IRQ; mask bit 2 of port 0x21 does not mask IRQ 8-15.

export class PIC {
  constructor(onChange) {
    this.onChange = onChange;   // (level) => void : CPU IRQ line
    this.reset();
  }
  reset() {
    this.irr = 0; this.isr = 0; this.imr = 0xFFFF;
    this.icw = [0, 0];          // remaining ICW bytes expected per controller
    this.line = 0;
    this.update();
  }
  // master-level priority rank of an IRQ (0 = highest)
  static rank(n) { return n < 2 ? n : n >= 8 ? 2 : n; }
  masterBusyRank() {            // highest-priority in-service master level, or 8
    for (let l = 0; l < 8; l++) {
      if (l === 2) { if (this.isr & 0xFF00) return 2; continue; }
      if (this.isr & (1 << l)) return l;
    }
    return 8;
  }
  slaveBusyRank() { for (let n = 8; n < 16; n++) if (this.isr & (1 << n)) return n; return 16; }
  // highest-priority deliverable IRQ (respecting in-service levels), or -1
  best(respectIsr) {
    const pend = this.irr & ~this.imr & 0xFFFF;
    if (!pend) return -1;
    const mb = respectIsr ? this.masterBusyRank() : 8, sb = respectIsr ? this.slaveBusyRank() : 16;
    for (let l = 0; l < 8; l++) {
      if (l >= mb) break;
      if (l === 2) {
        for (let n = 8; n < 16 && n < sb; n++) if (pend & (1 << n)) return n;
        continue;
      }
      if (pend & (1 << l)) return l;
    }
    return -1;
  }
  update() {
    const lvl = this.best(true) >= 0 ? 1 : 0;
    if (lvl !== this.line) { this.line = lvl; this.onChange(lvl); }
  }
  raise(n) { const b = 1 << n; if (!(this.irr & b)) { this.irr |= b; this.update(); } }
  lower(n) { const b = 1 << n; if (this.irr & b) { this.irr &= ~b; this.update(); } }
  ack() {
    // spec: the highest-priority pending unmasked IRQ; prefer one that is
    // deliverable given the in-service levels
    let n = this.best(true);
    if (n < 0) n = this.best(false);
    if (n < 0) return 0xFF;
    this.irr &= ~(1 << n);
    this.isr |= 1 << n;
    this.update();
    return n;
  }
  read(port) {
    switch (port) {
      case 0x20: return this.ack();
      case 0x21: return this.imr & 0xFF;
      case 0xA0: return 0xFF;      // (no second acknowledge register)
      case 0xA1: return (this.imr >>> 8) & 0xFF;
    }
    return 0xFF;
  }
  write(port, v) {
    const slave = port >= 0xA0 ? 1 : 0;
    if ((port & 1) === 0) {
      if (v & 0x10) { this.icw[slave] = (v & 1) ? 3 : 2; return; }       // ICW1
      if ((v & 0x18) === 0) {                                           // OCW2
        const cmd = v >>> 5;
        if (cmd === 1 || cmd === 5) this.eoi(slave, -1);
        else if (cmd === 3 || cmd === 7) this.eoi(slave, v & 7);
      }
      return;                                                            // OCW3 ignored
    }
    if (this.icw[slave]) { this.icw[slave]--; return; }                // ICW2-4
    if (slave) this.imr = (this.imr & 0x00FF) | (v << 8);
    else this.imr = (this.imr & 0xFF00) | v;
    this.update();
  }
  eoi(slave, specific) {
    if (slave) {
      if (specific >= 0) this.isr &= ~(1 << (8 + specific));
      else for (let n = 8; n < 16; n++) if (this.isr & (1 << n)) { this.isr &= ~(1 << n); break; }
    } else if (specific >= 0) {
      if (specific !== 2) this.isr &= ~(1 << specific);
    } else {
      const r = this.masterBusyRank();
      if (r === 2) { for (let n = 8; n < 16; n++) if (this.isr & (1 << n)) { this.isr &= ~(1 << n); break; } }
      else if (r < 8) this.isr &= ~(1 << r);
    }
    this.update();
  }
}
