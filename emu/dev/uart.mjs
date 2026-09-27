// COM1: 16550-ish UART at 0x3F8-0x3FF, IRQ4. Transmit is instantaneous
// (THR always empty); received bytes come from the host via input().

export class UART {
  constructor(m) { this.m = m; this.rx = []; this.reset(); }
  reset() {
    this.ier = 0; this.lcr = 0x03; this.mcr = 0; this.scr = 0; this.dll = 0x0C; this.dlm = 0; this.fcr = 0;
    this.thriPending = false; this.m.pic.lower(4);
  }
  input(bytes) { for (const b of bytes) this.rx.push(b & 0xFF); this.update(); }
  iir() {
    if ((this.ier & 1) && this.rx.length) return 0x04 | (this.fcr & 1 ? 0xC0 : 0);
    if ((this.ier & 2) && this.thriPending) return 0x02 | (this.fcr & 1 ? 0xC0 : 0);
    return 0x01 | (this.fcr & 1 ? 0xC0 : 0);
  }
  update() {
    const on = ((this.ier & 1) && this.rx.length) || ((this.ier & 2) && this.thriPending);
    if (on && (this.mcr & 8)) this.m.pic.raise(4); else this.m.pic.lower(4);
  }
  read(port) {
    const dlab = this.lcr & 0x80;
    switch (port - 0x3F8) {
      case 0: if (dlab) return this.dll; { const v = this.rx.length ? this.rx.shift() : 0; this.update(); return v; }
      case 1: return dlab ? this.dlm : this.ier;
      case 2: { const v = this.iir(); if ((v & 0x0F) === 0x02) { this.thriPending = false; this.update(); } return v; }
      case 3: return this.lcr;
      case 4: return this.mcr;
      case 5: return (this.rx.length ? 1 : 0) | 0x60;
      case 6: return (this.mcr & 0x10) ? ((this.mcr & 0x0F) << 4) : 0xB0;
      case 7: return this.scr;
    }
    return 0xFF;
  }
  write(port, v) {
    const dlab = this.lcr & 0x80;
    switch (port - 0x3F8) {
      case 0:
        if (dlab) { this.dll = v; return; }
        if (this.mcr & 0x10) this.rx.push(v); else this.m.serialOut(v);   // loopback
        this.thriPending = true; this.update(); return;
      case 1: if (dlab) this.dlm = v; else { this.ier = v & 0x0F; if (v & 2) this.thriPending = true; this.update(); } return;
      case 2: this.fcr = v; if (v & 2) { this.rx.length = 0; this.update(); } return;
      case 3: this.lcr = v; return;
      case 4: this.mcr = v & 0x1F; this.update(); return;
      case 7: this.scr = v; return;
    }
  }
}
