// 8237A DMA controllers of a PC/AT (ARCH.md §4.2): DMA1 = channels 0-3 (8-bit,
// ports 0x00-0x0F), DMA2 = channels 4-7 (16-bit, ports 0xC0-0xDF, word
// transfers), page registers 0x81-0x8F, plus the EISA-style high page
// registers 0x481-0x48B (address bits 24-31, the ARM-PC's 32-bit extension).
//
// The machine's devices pull their transfers through `read()` / `write()`
// (the Sound Blaster is the only DMA user; the floppy controller has its own
// bus-master DMA, ARCH.md §4.1). The 8237 itself is modelled as a register
// file with current/base address and count, single/block/demand modes treated
// alike (the device paces the transfer), auto-initialisation, address
// decrement, terminal count (status bits 0-3, cleared by reading status) and
// the mask register. Channel 4 is the cascade and never transfers.
//
// Physical address, 8-bit channel n:  hipage<<24 | page<<16 | address
//                   16-bit channel n: hipage<<24 | (page & 0xFE)<<16 | address<<1
// so an 8-bit transfer wraps inside its 64 KB page and a 16-bit one inside its
// 128 KB page, as on the AT. Writing a low page register clears the high page
// (EISA behaviour), so ISA software that never touches 0x48x gets 24-bit
// addresses. RAM is 16 MB, below the 24-bit limit, so every buffer is
// reachable either way; reads of non-RAM addresses return 0xFF, writes to them
// are dropped.

const RAM_TOP = 0x1000000;          // 16 MB of RAM (ARCH.md §3)
const PAGE_PORT = { 0x87: 0, 0x83: 1, 0x81: 2, 0x82: 3, 0x8F: 4, 0x8B: 5, 0x89: 6, 0x8A: 7 };

class Chan {
  constructor(n) { this.n = n; this.reset(); }
  reset() {
    this.baseAddr = 0; this.baseCount = 0; this.addr = 0; this.count = 0;
    this.mode = 0; this.page = 0; this.hipage = 0; this.masked = true; this.tc = false;
  }
}

export class DMA {
  constructor(m) {
    this.m = m;
    this.ch = []; for (let i = 0; i < 8; i++) this.ch.push(new Chan(i));
    this.extraPages = new Uint8Array(16);    // the unused page registers are scratch bytes on an AT
    this.listeners = [];                     // () => void, called before any register access (devices catch up first)
    this.reset();
  }
  reset() {
    for (const c of this.ch) c.reset();
    this.ff = [false, false];                // byte-pointer flip-flops
    this.status = [0, 0]; this.command = [0, 0]; this.temp = [0, 0];
    this.extraPages.fill(0);
  }
  onAccess(fn) { this.listeners.push(fn); }
  touch() { for (const f of this.listeners) f(); }

  // ------------------------------------------------------------ ports
  handles(port) {
    return port <= 0x0F || (port >= 0x81 && port <= 0x8F) || (port >= 0xC0 && port <= 0xDF) || (port >= 0x481 && port <= 0x48B);
  }
  read(port) {
    this.touch();
    if (port >= 0x481) { const n = PAGE_PORT[port - 0x400]; return n !== undefined ? this.ch[n].hipage : 0xFF; }
    if (port >= 0x80 && port <= 0x8F) { const n = PAGE_PORT[port]; return n !== undefined ? this.ch[n].page : this.extraPages[port & 15]; }
    let ctl, reg;
    if (port <= 0x0F) { ctl = 0; reg = port; } else { ctl = 1; reg = (port - 0xC0) >> 1; }
    if (reg < 8) {
      const c = this.ch[ctl * 4 + (reg >> 1)];
      const v = (reg & 1) ? c.count : c.addr;
      const hi = this.ff[ctl]; this.ff[ctl] = !hi;
      return hi ? (v >> 8) & 0xFF : v & 0xFF;
    }
    switch (reg) {
      case 8: {                                         // status: TC bits 0-3 (cleared), requests 4-7
        const s = this.status[ctl]; this.status[ctl] &= 0xF0; return s;
      }
      case 13: return this.temp[ctl];
      case 15: {                                        // (8237 has no mask read-back; the 82C37 family does)
        let v = 0xF0; for (let i = 0; i < 4; i++) if (this.ch[ctl * 4 + i].masked) v |= 1 << i; return v;
      }
    }
    return 0xFF;
  }
  write(port, v) {
    this.touch();
    if (port >= 0x481) { const n = PAGE_PORT[port - 0x400]; if (n !== undefined) { this.ch[n].hipage = v; } return; }
    if (port >= 0x80 && port <= 0x8F) {
      const n = PAGE_PORT[port];
      if (n !== undefined) { this.ch[n].page = v; this.ch[n].hipage = 0; } else this.extraPages[port & 15] = v;
      return;
    }
    let ctl, reg;
    if (port <= 0x0F) { ctl = 0; reg = port; } else { ctl = 1; reg = (port - 0xC0) >> 1; }
    if (reg < 8) {
      const c = this.ch[ctl * 4 + (reg >> 1)];
      const hi = this.ff[ctl]; this.ff[ctl] = !hi;
      if (reg & 1) { c.baseCount = hi ? (c.baseCount & 0xFF) | (v << 8) : (c.baseCount & 0xFF00) | v; c.count = c.baseCount; }
      else { c.baseAddr = hi ? (c.baseAddr & 0xFF) | (v << 8) : (c.baseAddr & 0xFF00) | v; c.addr = c.baseAddr; }
      c.tc = false;
      return;
    }
    switch (reg) {
      case 8: this.command[ctl] = v; return;
      case 9: {                                          // software request
        const bit = 1 << (4 + (v & 3));
        if (v & 4) this.status[ctl] |= bit; else this.status[ctl] &= ~bit;
        return;
      }
      case 10: { const c = this.ch[ctl * 4 + (v & 3)]; c.masked = !!(v & 4); return; }
      case 11: { const c = this.ch[ctl * 4 + (v & 3)]; c.mode = v & 0xFC; return; }
      case 12: this.ff[ctl] = false; return;
      case 13:                                           // master clear
        for (let i = 0; i < 4; i++) { const c = this.ch[ctl * 4 + i]; c.masked = true; }
        this.ff[ctl] = false; this.status[ctl] = 0; this.command[ctl] = 0; this.temp[ctl] = 0;
        return;
      case 14: for (let i = 0; i < 4; i++) { this.ch[ctl * 4 + i].masked = false; } return;
      case 15: for (let i = 0; i < 4; i++) { this.ch[ctl * 4 + i].masked = !!(v & (1 << i)); } return;
    }
  }

  // ------------------------------------------------------------ device side
  /** The channel will transfer (unmasked, not finished in single-cycle mode, not the cascade). */
  ready(n) { const c = this.ch[n]; return n !== 4 && !c.masked && !(c.tc && !(c.mode & 0x10)) && (this.command[n >> 2] & 4) === 0; }
  /** Transfer direction from the mode register: 1 = write to memory (device -> memory), 2 = read memory. */
  direction(n) { return (this.ch[n].mode >> 2) & 3; }
  physAddr(n) {
    const c = this.ch[n];
    return n < 4 ? ((c.hipage << 24) | (c.page << 16) | c.addr) >>> 0
      : ((c.hipage << 24) | ((c.page & 0xFE) << 16) | (c.addr << 1)) >>> 0;
  }
  /** One unit transferred: advance address, count down, terminal count. Returns false once TC stops the channel. */
  step(n) {
    const c = this.ch[n];
    c.addr = (c.mode & 0x20) ? (c.addr - 1) & 0xFFFF : (c.addr + 1) & 0xFFFF;
    if (c.count === 0) {                              // count went 0 -> FFFF: terminal count
      this.status[n >> 2] |= 1 << (n & 3);
      if (c.mode & 0x10) { c.addr = c.baseAddr; c.count = c.baseCount; }
      else { c.count = 0xFFFF; c.tc = true; c.masked = true; return false; }
      return true;
    }
    c.count--;
    return true;
  }
  /**
   * Read up to `units` transfer units (bytes on 0-3, 16-bit words on 5-7) from
   * memory into `out` (bytes, little-endian words) starting at out[pos].
   * Returns the number of units transferred (fewer if the channel stopped or
   * is masked).
   */
  read8or16(n, units, out, pos = 0) {
    const m8 = this.m.cpu.m8, ramTop = RAM_TOP, act = this.m.cpu.act;
    const wide = n >= 4;
    let done = 0;
    while (done < units && this.ready(n)) {
      const a = this.physAddr(n);
      if (act !== null && a < ramTop) act.r[a >>> 8] += wide ? 2 : 1;       // memory map (memmap.mjs)
      if (wide) {
        out[pos++] = a < ramTop ? m8[a] : 0xFF;
        out[pos++] = a + 1 < ramTop ? m8[a + 1] : 0xFF;
      } else out[pos++] = a < ramTop ? m8[a] : 0xFF;
      done++;
      if (!this.step(n)) break;
    }
    return done;
  }
  /** Write units (bytes or words from `data` at pos) to memory. Returns units written. */
  write8or16(n, units, data, pos = 0) {
    const wide = n >= 4, ramTop = RAM_TOP;
    let done = 0;
    while (done < units && this.ready(n)) {
      const a = this.physAddr(n), len = wide ? 2 : 1;
      if (a + len <= ramTop) this.m.dmaToMemory(a, data.subarray(pos, pos + len));
      pos += len; done++;
      if (!this.step(n)) break;
    }
    return done;
  }
}
