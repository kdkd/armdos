// ATA hard disks on the primary IDE channel, PIO, LBA28 and CHS: the master (C:) and an
// optional slave (D:). Ports 0x1F0-0x1F7, 0x3F6. Commands complete instantly (BSY is never
// seen); IRQ14 is raised when a sector is ready / a command completes, if nIEN (0x3F6 bit 1)
// is clear; reading 0x1F7 clears it. As on real drives, both latch every task-file write and
// the one that drive/head bit 4 selects answers (ATAChannel, below).
//
// Streaming (optional): ata.source = { ready(lba, n) -> bool, fetch(lba, n) -> Promise<bool>,
// wrote?(lba) }. A READ touching sectors the source doesn't have yet keeps BSY set (DRQ
// clear) until fetch() resolves, then completes normally (diskActivity is reported then);
// false = the medium can't be read: ERR with UNC (0x40). Writes go to the image at once and
// are reported with wrote(lba) so late-arriving data never overwrites them. A new command,
// SRST or reset while waiting cancels the wait.

const BSY = 0x80, DRDY = 0x40, DSC = 0x10, DRQ = 0x08, ERR = 0x01;
const ABRT = 0x04, IDNF = 0x10, UNC = 0x40;

function geometry(sectors) {
  // classic translation-free geometry: 16 heads, 63 sectors/track
  const heads = 16, spt = 63;
  let cyls = Math.floor(sectors / (heads * spt));
  if (cyls > 16383) cyls = 16383;
  return { cyls: Math.max(1, cyls), heads, spt };
}

export class ATA {
  constructor(m, image, unit = 0) {
    this.m = m;                    // machine: pic, diskActivity(drive,lba,count,write)
    this.unit = unit;              // 0 master, 1 slave
    this.drive = 0x80 + unit;      // (the BIOS number, for the machine's disk activity callbacks)
    this.buf = new Uint8Array(512);
    this.source = null;            // optional streaming sector source (see above)
    this.waitToken = 0;
    this.setImage(image);
    this.reset();
  }
  setImage(img) {
    this.img = img || null;
    this.sectors = img ? Math.floor(img.length / 512) : 0;
    this.geom = geometry(this.sectors);
  }
  reset() {
    this.err = 0x01;               // diagnostic code: no error
    this.feat = 0; this.count = 1; this.lba0 = 1; this.lba1 = 0; this.lba2 = 0; this.dh = 0xA0;
    this.status = this.img ? DRDY | DSC : 0;
    this.nIEN = 0;
    this.pos = 0; this.remain = 0; this.mode = 0;   // mode: 0 idle, 1 read (to host), 2 write (from host), 3 identify
    this.cur = 0;                  // current LBA
    this.waitToken++;              // cancels a pending streamed read
    this.m.pic.lower(14);
  }
  get waiting() { return this.mode === 4; }
  present() { return !!this.img && ((this.dh >> 4) & 1) === this.unit; }
  irq() { if (!this.nIEN) this.m.pic.raise(14); }

  curLBA() {
    if (this.dh & 0x40) return ((this.dh & 0x0F) << 24) | (this.lba2 << 16) | (this.lba1 << 8) | this.lba0;
    const c = (this.lba2 << 8) | this.lba1, h = this.dh & 0x0F, s = this.lba0;
    return (c * this.geom.heads + h) * this.geom.spt + (s - 1);
  }
  setLBA(l) {                       // update task file after a transfer
    if (this.dh & 0x40) { this.lba0 = l & 0xFF; this.lba1 = (l >>> 8) & 0xFF; this.lba2 = (l >>> 16) & 0xFF; this.dh = (this.dh & 0xF0) | ((l >>> 24) & 0x0F); }
    else {
      const spt = this.geom.spt, hd = this.geom.heads;
      const s = (l % spt) + 1, t = Math.floor(l / spt), h = t % hd, c = Math.floor(t / hd);
      this.lba0 = s; this.lba1 = c & 0xFF; this.lba2 = (c >>> 8) & 0xFF; this.dh = (this.dh & 0xF0) | h;
    }
  }
  abort(code = ABRT) { this.err = code; this.status = DRDY | DSC | ERR; this.mode = 0; this.irq(); }

  command(cmd) {
    if (!this.present()) return;
    this.err = 0;
    if (this.mode === 4) { this.waitToken++; this.mode = 0; }   // a new command abandons a streamed wait
    const n = this.count || 256;
    switch (cmd) {
      case 0x20: case 0x21: case 0xC4: {            // READ SECTORS (/MULTIPLE)
        const l = this.curLBA();
        if (l < 0 || l + n > this.sectors) { this.abort(IDNF); return; }
        this.cur = l; this.remain = n;
        if (this.source && !this.source.ready(l, n)) {
          const token = ++this.waitToken;
          this.mode = 4; this.status = BSY | DSC;
          Promise.resolve(this.source.fetch(l, n)).then((ok) => ok, () => false).then((ok) => {
            if (token !== this.waitToken || this.mode !== 4) return;
            if (!ok) { this.err = UNC; this.status = DRDY | DSC | ERR; this.mode = 0; this.irq(); return; }
            this.mode = 1; this.m.diskActivity(this.drive, l, n, false); this.loadSector();
          });
          return;
        }
        this.mode = 1;
        this.m.diskActivity(this.drive, l, n, false);
        this.loadSector();
        return;
      }
      case 0x30: case 0x31: case 0xC5: {            // WRITE SECTORS (/MULTIPLE)
        const l = this.curLBA();
        if (l < 0 || l + n > this.sectors) { this.abort(IDNF); return; }
        this.cur = l; this.remain = n; this.mode = 2; this.pos = 0;
        this.m.diskActivity(this.drive, l, n, true);
        this.status = DRDY | DSC | DRQ;              // no IRQ before the first sector
        return;
      }
      case 0xEC: this.identify(); return;
      case 0x40: case 0x41: {                       // VERIFY
        const l = this.curLBA();
        if (l < 0 || l + n > this.sectors) { this.abort(IDNF); return; }
        this.setLBA(l + n - 1); this.status = DRDY | DSC; this.irq(); return;
      }
      case 0x90: this.err = 0x01; this.status = DRDY | DSC; this.irq(); return;   // diagnostics
      case 0xE7: case 0xEA: case 0x91: case 0xEF: case 0xC6: case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE5:
      case 0x70:
        this.status = DRDY | DSC; if (cmd === 0xE5) this.count = 0xFF; this.irq(); return;
      default:
        if ((cmd & 0xF0) === 0x10) { this.status = DRDY | DSC; this.irq(); return; }   // recalibrate
        this.abort(); return;
    }
  }
  loadSector() {
    const off = this.cur * 512;
    this.buf.set(this.img.subarray(off, off + 512));
    this.pos = 0;
    this.status = DRDY | DSC | DRQ;
    this.irq();
  }
  identify() {
    const w = new Uint16Array(256);
    const g = this.geom;
    const str = (at, len, s) => { s = s.padEnd(len * 2, ' '); for (let i = 0; i < len; i++) w[at + i] = (s.charCodeAt(2 * i) << 8) | s.charCodeAt(2 * i + 1); };
    w[0] = 0x0040;
    w[1] = g.cyls; w[3] = g.heads; w[4] = 512 * g.spt; w[5] = 512; w[6] = g.spt;
    str(10, 10, 'ARMPC0000000000001');
    w[20] = 3; w[21] = 16; w[22] = 4;
    str(23, 4, '1.00');
    str(27, 20, 'ARM-PC FIXED DISK');
    w[47] = 0x8010;
    w[49] = 0x0200;                 // LBA supported
    w[51] = 0x0200; w[53] = 0x0001;
    const cur = g.cyls * g.heads * g.spt;
    w[54] = g.cyls; w[55] = g.heads; w[56] = g.spt; w[57] = cur & 0xFFFF; w[58] = cur >>> 16;
    w[59] = 0x0110;
    w[60] = this.sectors & 0xFFFF; w[61] = this.sectors >>> 16;
    w[80] = 0x001E; w[82] = 0x4000; w[83] = 0x4000; w[84] = 0x4000;
    this.buf.set(new Uint8Array(w.buffer));
    this.mode = 3; this.remain = 1; this.pos = 0;
    this.status = DRDY | DSC | DRQ;
    this.irq();
  }
  // data port
  readData16() {
    if (!(this.status & DRQ) || (this.mode !== 1 && this.mode !== 3)) return 0xFFFF;
    const v = this.buf[this.pos] | (this.buf[this.pos + 1] << 8);
    this.pos += 2;
    if (this.pos >= 512) this.sectorDoneRead();
    return v;
  }
  readData8() {
    if (!(this.status & DRQ) || (this.mode !== 1 && this.mode !== 3)) return 0xFF;
    const v = this.buf[this.pos++];
    if (this.pos >= 512) this.sectorDoneRead();
    return v;
  }
  sectorDoneRead() {
    if (this.mode === 3) { this.mode = 0; this.status = DRDY | DSC; return; }
    this.remain--; this.setLBA(this.cur); this.cur++;
    this.count = this.remain & 0xFF;
    if (this.remain > 0) this.loadSector();
    else { this.mode = 0; this.status = DRDY | DSC; }
  }
  writeData16(v) {
    if (this.mode !== 2 || !(this.status & DRQ)) return;
    this.buf[this.pos] = v & 0xFF; this.buf[this.pos + 1] = (v >>> 8) & 0xFF;
    this.pos += 2;
    if (this.pos >= 512) this.sectorDoneWrite();
  }
  writeData8(v) {
    if (this.mode !== 2 || !(this.status & DRQ)) return;
    this.buf[this.pos++] = v;
    if (this.pos >= 512) this.sectorDoneWrite();
  }
  sectorDoneWrite() {
    this.img.set(this.buf, this.cur * 512);
    if (this.source && this.source.wrote) this.source.wrote(this.cur);
    this.m.diskWritten(this.drive, this.cur, 1);
    this.remain--; this.setLBA(this.cur); this.cur++; this.pos = 0;
    this.count = this.remain & 0xFF;
    if (this.remain > 0) { this.status = DRDY | DSC | DRQ; }
    else { this.mode = 0; this.status = DRDY | DSC; }
    this.irq();
  }

  read(port) {
    const sel = this.present();
    switch (port) {
      case 0x1F0: return this.readData8();
      case 0x1F1: return sel ? this.err : 0;
      case 0x1F2: return sel ? this.count : 0;
      case 0x1F3: return this.lba0;
      case 0x1F4: return this.lba1;
      case 0x1F5: return this.lba2;
      case 0x1F6: return this.dh;
      case 0x1F7: this.m.pic.lower(14); return sel ? this.status : 0;
      case 0x3F6: return sel ? this.status : 0;
      case 0x3F7: return 0xFF;
    }
    return 0xFF;
  }
  write(port, v) {
    switch (port) {
      case 0x1F0: this.writeData8(v); return;
      case 0x1F1: this.feat = v; return;
      case 0x1F2: this.count = v; return;
      case 0x1F3: this.lba0 = v; return;
      case 0x1F4: this.lba1 = v; return;
      case 0x1F5: this.lba2 = v; return;
      case 0x1F6: this.dh = v | 0xA0; return;
      case 0x1F7: this.m.pic.lower(14); this.command(v); return;
      case 0x3F6:
        this.nIEN = (v >>> 1) & 1;
        if (v & 4) { this.srst = 1; this.mode = 0; this.waitToken++; this.status = this.img ? BSY : 0; }
        else if (this.srst) { this.srst = 0; this.reset(); this.nIEN = (v >>> 1) & 1; }
        return;
    }
  }
}

/** The primary IDE channel: a master and an optional slave. Task-file and control writes reach
 *  both drives (each keeps its own copy, as real drives do); commands, data and status go to the
 *  selected one. With no slave, selecting it reads back 0 (no drive), as before. */
export class ATAChannel {
  constructor(master, slave = null) { this.master = master; this.slave = slave; }
  get sel() { return this.slave && ((this.master.dh >> 4) & 1) ? this.slave : this.master; }
  reset() { this.master.reset(); this.slave?.reset(); }
  readData16() { return this.sel.readData16(); }
  writeData16(v) { this.sel.writeData16(v); }
  read(port) { return this.sel.read(port); }      // (no slave: the master answers, reporting status 0 while the slave is selected)
  write(port, v) {
    if (port === 0x1F0) { this.sel.writeData8(v); return; }
    if (port === 0x1F7) { this.sel.write(port, v); return; }
    this.master.write(port, v);
    this.slave?.write(port, v);
  }
}
