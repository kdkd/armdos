// ARM-PC simplified floppy controller with DMA (ARCH.md §4.1), ports 0x300-0x307.
// Transfers complete instantly; IRQ6 is raised at completion.

const MEDIA = { 368640: 1, 1228800: 2, 737280: 3, 1474560: 4 };

export class FDC {
  constructor(m, image, writeProtected = false) {
    this.m = m;           // machine: cpu (memory), pic, diskActivity, diskWritten
    this.img = null; this.wp = false; this.changed = 0;
    this.insert(image, writeProtected);
    this.changed = image ? 1 : 0;
    this.reset();
  }
  reset() { this.dma = 0; this.cnt = 1; this.lba = 0; this.err = 0; this.lastCyl = 0; }
  insert(img, wp = false) { this.img = img || null; this.wp = !!wp; this.changed = 1; }
  eject() { this.img = null; this.changed = 1; }
  mediaType() {
    if (!this.img) return 0;
    return MEDIA[this.img.length] || (this.img.length > 1228800 ? 4 : this.img.length > 737280 ? 2 : this.img.length > 368640 ? 3 : 1);
  }
  status() {
    return (this.img ? 0x40 : 0) | (this.changed ? 0x20 : 0) | (this.img && this.wp ? 0x10 : 0) | (this.err ? 1 : 0);
  }
  read(port) {
    if (port === 0x307) { const s = this.status(); this.changed = 0; return s; }
    if (port === 0x302) return this.mediaType();
    return 0xFF;
  }
  write(port, v) {
    if (port <= 0x303) { const sh = (port - 0x300) * 8; this.dma = ((this.dma & ~(0xFF << sh)) | (v << sh)) >>> 0; return; }
    if (port === 0x304) { this.cnt = v; return; }
    if (port === 0x305) { this.lba = (this.lba & 0xFF00) | v; return; }
    if (port === 0x306) { this.lba = (this.lba & 0x00FF) | (v << 8); return; }
    if (port === 0x307) this.command(v);
  }
  command(cmd) {
    this.err = 0;
    switch (cmd) {
      case 1: case 2: {
        const n = this.cnt || 256, lba = this.lba, bytes = n * 512;
        if (!this.img || (lba + n) * 512 > this.img.length || this.cnt === 0) { this.err = 1; break; }
        if (cmd === 2 && this.wp) { this.err = 1; break; }
        const a = this.dma >>> 0;
        if (a + bytes > 0x1000000) { this.err = 1; break; }       // DMA reaches RAM only
        this.m.diskActivity(0x00, lba, n, cmd === 2, Math.floor(lba / 36), this.lastCyl);
        this.lastCyl = Math.floor((lba + n - 1) / 36);
        if (cmd === 1) this.m.dmaToMemory(a, this.img.subarray(lba * 512, lba * 512 + bytes));
        else { this.img.set(this.m.cpu.m8.subarray(a, a + bytes), lba * 512); this.m.diskWritten(0x00, lba, n); }
        break;
      }
      case 3: this.lastCyl = 0; break;
      case 4: break;
      default: this.err = 1;
    }
    this.m.pic.raise(6);
  }
}
