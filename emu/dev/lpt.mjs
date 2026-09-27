// LPT1: a minimal Centronics parallel port at 0x378-0x37A with an always-ready
// printer attached. The byte in the data latch is delivered to the host
// (onPrint) on the rising edge of control bit 0 (STROBE, as software sees it).
//
//   0x378  data latch (read back what was written)
//   0x379  status (read): 0xDF = bit7 /BUSY=1 (not busy), bit6 /ACK=1,
//          bit5 PE=0 (paper present), bit4 SELECT=1, bit3 /ERROR=1, bits 2-0 = 1
//   0x37A  control: bit0 STROBE, bit1 AUTOFEED, bit2 /INIT, bit3 SELECT IN,
//          bit4 IRQ enable (no IRQ is ever raised); reads back with bits 7-5 set

export class LPT {
  constructor(m) { this.m = m; this.reset(); }
  reset() { this.data = 0; this.ctrl = 0x0C; }
  read(port) {
    if (port === 0x378) return this.data;
    if (port === 0x379) return 0xDF;
    if (port === 0x37A) return this.ctrl | 0xE0;
    return 0xFF;
  }
  write(port, v) {
    if (port === 0x378) { this.data = v; return; }
    if (port === 0x37A) {
      const rising = (v & 1) && !(this.ctrl & 1);
      this.ctrl = v & 0x1F;
      if (rising) this.m.printOut(this.data);
    }
  }
}
