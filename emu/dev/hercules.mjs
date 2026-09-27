// Hercules Graphics Card (HGC, 1982) with an IBM 5151-style monochrome monitor.
// MDA-compatible text mode (80x25, 9x14 cells = 720x350) plus the Hercules
// 720x348 1-bpp graphics mode with two 32 KB pages. Selected with
// new Machine({ video: 'hercules', monitor: 'green' | 'amber' | 'white' }).
//
// Ports (x86 port p at 0x10000000 + p, ARCH.md §4):
//   3B4h/3B5h  6845 CRTC index / data (mirrored at 3B0/3B2/3B6 and 3B1/3B3/3B7).
//              R10/R11 cursor start/end (R10 bits 5-6 = 01: cursor off), R12/R13
//              display start, R14/R15 cursor address; R14-R17 read back, the others read 0.
//   3B8h       mode control (write): bit1 graphics, bit3 video enable, bit5 blink
//              enable, bit7 display page 1 (B8000h). Bit 1 needs config bit 0, bit 7
//              needs config bit 1.
//   3BAh       status (read): bit0 horizontal retrace, bit3 video dot stream,
//              bit7 vertical retrace, Hercules-style: 0 while the beam retraces,
//              1 during the display (this toggling bit is how programs tell a
//              Hercules from a plain MDA, whose bit 7 never changes). Bits 4-6 = 000: HGC.
//   3BFh       configuration switch (write): bit0 allow graphics, bit1 map the
//              second 32 KB page at B8000h (power-on: 0, "half", like the real card).
// Memory: B0000h-B7FFFh always; B8000h-BFFFFh only while config bit 1 is set
// (otherwise that range is an empty bus: reads FFh, writes ignored, so a
// Hercules-only machine has no CGA/VGA buffer there). A0000h-AFFFFh: no VGA, empty.
//
// The character generator is the card's own ROM (dev/mdafont.mjs), not the VGA
// font RAM at 0x11000000.

export const HGC_LINE_NS = 1e9 / 18432;          // 18.432 kHz horizontal
export const HGC_FRAME_NS = HGC_LINE_NS * 370;   // 370 lines: ~49.8 Hz
const HACTIVE_NS = HGC_LINE_NS * 720 / 882;      // 720 of 882 dot clocks
const VR_START = 354, VR_END = 370;              // vertical sync lines (bit 7 = 0)
const ACTIVE_LINES = 350;

// phosphors: [background glow, normal, bright] as RGB
export const PHOSPHORS = {
  green: { name: 'P39 green', rgb: [[4, 10, 5], [40, 196, 72], [130, 255, 150]] },
  amber: { name: 'P134 amber', rgb: [[10, 6, 2], [208, 128, 8], [255, 196, 64]] },
  white: { name: 'paper white', rgb: [[8, 8, 8], [184, 184, 172], [250, 250, 240]] },
};

const PAGE1 = 0xB8000, PAGE_LEN = 0x8000, VGA_LO = 0xA0000, VGA_LEN = 0x10000;

export class Hercules {
  constructor(m, { monitor = 'green' } = {}) {
    this.m = m;
    this.setMonitor(monitor);
    this.saved1 = new Uint8Array(PAGE_LEN);      // page 1 contents while it is unmapped
    this.reset();
  }
  setMonitor(name) { this.monitor = PHOSPHORS[name] ? name : 'green'; this.dirty = true; }
  reset() {
    this.crtc = new Uint8Array(18); this.crtcIndex = 0;
    this.crtc[10] = 0x0B; this.crtc[11] = 0x0C; this.crtc[9] = 13;
    this.control = 0x00;              // video off until the BIOS programs the card
    this.config = 0x00;
    // what the bus looks like now (a power cycle clears RAM and the empty-bus flags)
    this.mapped1 = !(this.m.cpu.pflags[PAGE1 >>> 7] & 1);
    this.applyMap();
    // no VGA: the A0000h window is an empty bus
    const m8 = this.m.cpu.m8, pf = this.m.cpu.pflags;
    m8.fill(0xFF, VGA_LO, VGA_LO + VGA_LEN);
    for (let l = VGA_LO >>> 7; l < (VGA_LO + VGA_LEN) >>> 7; l++) pf[l] |= 1;
    this.dirty = true;
  }
  // map/unmap B8000h-BFFFFh following config bit 1 (contents kept on the card)
  applyMap() {
    const want = (this.config & 2) !== 0;
    if (want === this.mapped1) return;
    const m8 = this.m.cpu.m8, pf = this.m.cpu.pflags;
    if (want) {
      m8.set(this.saved1, PAGE1);
      for (let l = PAGE1 >>> 7; l < (PAGE1 + PAGE_LEN) >>> 7; l++) pf[l] &= ~1;
    } else {
      this.saved1.set(m8.subarray(PAGE1, PAGE1 + PAGE_LEN));
      m8.fill(0xFF, PAGE1, PAGE1 + PAGE_LEN);
      for (let l = PAGE1 >>> 7; l < (PAGE1 + PAGE_LEN) >>> 7; l++) pf[l] |= 1;
    }
    this.mapped1 = want;
  }

  get graphics() { return (this.control & 2) !== 0; }
  get videoEnabled() { return (this.control & 8) !== 0; }
  get blinkEnabled() { return (this.control & 0x20) !== 0; }
  get pageBase() { return (this.control & 0x80) ? PAGE1 : 0xB0000; }
  get startAddr() { return (this.crtc[12] << 8) | this.crtc[13]; }
  get cursorPos() { return (this.crtc[14] << 8) | this.crtc[15]; }

  status(now) {
    const t = now % HGC_FRAME_NS, line = Math.floor(t / HGC_LINE_NS);
    const lt = t - line * HGC_LINE_NS;
    const vr = line >= VR_START && line < VR_END;
    const hr = lt >= HACTIVE_NS;
    const dot = !vr && !hr && line < ACTIVE_LINES && (line & 1) && this.videoEnabled;
    return (vr ? 0 : 0x80) | (hr || vr ? 0x01 : 0) | (dot ? 0x08 : 0);    // bits 4-6 = 0: HGC
  }
  // next time the status byte can change (for the spin-loop skipper)
  statusStableUntil(now) {
    const f = Math.floor(now / HGC_FRAME_NS) * HGC_FRAME_NS;
    const line = Math.floor((now - f) / HGC_LINE_NS), l0 = f + line * HGC_LINE_NS;
    return (now - l0) < HACTIVE_NS ? l0 + HACTIVE_NS : l0 + HGC_LINE_NS;
  }

  read(port) {
    switch (port) {
      case 0x3BA: return this.status(this.m.timeNs());
      case 0x3B1: case 0x3B3: case 0x3B5: case 0x3B7: {
        const i = this.crtcIndex & 31;
        return (i >= 14 && i <= 17) ? this.crtc[i] : 0;
      }
    }
    return 0xFF;
  }
  write(port, v) {
    switch (port) {
      case 0x3B0: case 0x3B2: case 0x3B4: case 0x3B6: this.crtcIndex = v & 31; return;
      case 0x3B1: case 0x3B3: case 0x3B5: case 0x3B7:
        if (this.crtcIndex < 18) { this.crtc[this.crtcIndex] = v; this.dirty = true; }
        return;
      case 0x3B8: {
        let c = v;
        if (!(this.config & 1)) c &= ~0x02;
        if (!(this.config & 2)) c &= ~0x80;
        const old = this.control;
        this.control = c; this.dirty = true;
        if ((old ^ c) & 0x02) this.m.modeChanged(c & 2 ? 0x107 : 0x07);   // 107h: Hercules graphics
        return;
      }
      case 0x3BF: this.config = v & 3; this.applyMap(); return;
    }
  }
}
