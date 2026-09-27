// CMOS / RTC (MC146818) at ports 0x70 (index) / 0x71 (data).
// Time registers are derived from: rtcBaseMs (host wall clock at power-on, or
// a fixed value in deterministic mode) + emulated milliseconds + a user
// offset that writes to the time registers adjust. Local time is shown.
// 0x0A bit7 UIP always reads 0. 0x0B: bit1 24h (default 1), bit2 binary
// (default 0 = BCD), bit4 UIE, bit6 PIE. 0x0C flags (read clears, lowers IRQ8).
// 0x0D bit7 = valid RAM. 0x0E-0x7F = 114 bytes battery RAM (0x32 = century).

const MS_DAY = 86400000;

export class CMOS {
  constructor(m, { rtcBaseMs, ram, onRamWrite } = {}) {
    this.m = m;
    this.rtcBaseMs = rtcBaseMs ?? Date.now();
    this.offsetMs = 0;
    this.ram = new Uint8Array(128);
    this.onRamWrite = onRamWrite || null;
    if (ram) this.ram.set(ram.subarray(0, 128));
    else this.defaults();
    this.index = 0;
    this.regA = 0x26; this.regB = 0x02; this.regC = 0;
    this.nextPeriodic = Infinity; this.nextUpdate = Infinity;
  }
  // IBM AT standard locations, so a fresh machine has sensible settings
  defaults() {
    const r = this.ram;
    r[0x10] = 0x40;               // drive A: 1.44 MB, no B:
    r[0x12] = 0xF0;               // hard disk 0 type 15 -> extended (0x19)
    r[0x19] = 47;                 // user type
    r[0x14] = 0x41;               // equipment: floppy present, 80x25 colour (EGA/VGA = 00), 1 drive
    r[0x15] = 0x80; r[0x16] = 0x02;   // 640 KB base
    r[0x17] = 0x00; r[0x18] = 0x3C;   // 15360 KB extended
    r[0x30] = 0x00; r[0x31] = 0x3C;
    let sum = 0; for (let i = 0x10; i <= 0x2D; i++) sum += r[i];
    r[0x2E] = (sum >> 8) & 0xFF; r[0x2F] = sum & 0xFF;
  }
  reset() { this.regB &= ~0x70; this.regC = 0; this.schedule(); }

  nowMs() { return this.rtcBaseMs + this.m.timeNs() / 1e6 + this.offsetMs; }
  date() { return new Date(this.nowMs()); }
  enc(v) { return (this.regB & 4) ? v : ((v / 10) | 0) * 16 + (v % 10); }
  dec(v) { return (this.regB & 4) ? v : (v >> 4) * 10 + (v & 15); }
  encHour(h) {
    if (this.regB & 2) return this.enc(h);
    const pm = h >= 12; let h12 = h % 12; if (h12 === 0) h12 = 12;
    return this.enc(h12) | (pm ? 0x80 : 0);
  }
  decHour(v) {
    if (this.regB & 2) return this.dec(v);
    const pm = v & 0x80; let h = this.dec(v & 0x7F) % 12; return pm ? h + 12 : h;
  }

  // next time a read of port 71h (at the current index) can return something else
  readStableUntil(now) {
    const i = this.index;
    if (i <= 0x09 || i === 0x32) return now + (1000 - (this.nowMs() % 1000)) * 1e6;   // the clock ticks
    if (i === 0x0C) return now;                                                   // read clears flags
    return Infinity;
  }
  read(port) {
    if (port === 0x70) return 0xFF;
    const i = this.index, d = this.date();
    switch (i) {
      case 0x00: return this.enc(d.getSeconds());
      case 0x02: return this.enc(d.getMinutes());
      case 0x04: return this.encHour(d.getHours());
      case 0x06: return this.enc(d.getDay() + 1);
      case 0x07: return this.enc(d.getDate());
      case 0x08: return this.enc(d.getMonth() + 1);
      case 0x09: return this.enc(d.getFullYear() % 100);
      case 0x32: return this.enc((d.getFullYear() / 100) | 0);
      case 0x0A: return this.regA & 0x7F;
      case 0x0B: return this.regB;
      case 0x0C: { const v = this.regC; this.regC = 0; this.m.pic.lower(8); return v; }
      case 0x0D: return 0x80;
      default: return this.ram[i];
    }
  }
  write(port, v) {
    if (port === 0x70) { this.index = v & 0x7F; return; }
    const i = this.index;
    const setField = (fn) => {
      const d = this.date(), before = d.getTime();
      fn(d);
      this.offsetMs += d.getTime() - before;
    };
    switch (i) {
      case 0x00: setField((d) => d.setSeconds(this.dec(v), d.getMilliseconds())); return;
      case 0x02: setField((d) => d.setMinutes(this.dec(v))); return;
      case 0x04: setField((d) => d.setHours(this.decHour(v))); return;
      case 0x06: return;                               // day of week is derived
      case 0x07: setField((d) => d.setDate(this.dec(v))); return;
      case 0x08: setField((d) => d.setMonth(this.dec(v) - 1)); return;
      case 0x09: setField((d) => d.setFullYear(((d.getFullYear() / 100) | 0) * 100 + this.dec(v))); return;
      case 0x32: setField((d) => d.setFullYear(this.dec(v) * 100 + (d.getFullYear() % 100))); return;
      case 0x0A: this.regA = v & 0x7F; this.schedule(); return;
      case 0x0B: this.regB = v; this.schedule(); return;
      case 0x0C: case 0x0D: return;
      default:
        if (this.ram[i] !== v) { this.ram[i] = v; if (this.onRamWrite) this.onRamWrite(this.ram, i); }
    }
  }

  // periodic (PIE) and update-ended (UIE) interrupts on IRQ8
  schedule() {
    const now = this.m.timeNs();
    const rate = this.regA & 15;
    this.nextPeriodic = (this.regB & 0x40) && rate ? now + 1e9 / (32768 >> (rate - 1)) : Infinity;
    if (this.regB & 0x10) {
      const ms = this.nowMs();
      this.nextUpdate = now + (1000 - (ms % 1000)) * 1e6;
    } else this.nextUpdate = Infinity;
    this.m.reschedule();
  }
  nextEventNs() { return Math.min(this.nextPeriodic, this.nextUpdate); }
  service() {
    const now = this.m.timeNs();
    let fire = 0;
    if (now >= this.nextPeriodic - 1) {
      const rate = this.regA & 15;
      this.regC |= 0x40; fire = 1;
      this.nextPeriodic += 1e9 / (32768 >> (rate - 1));
      if (this.nextPeriodic <= now) this.nextPeriodic = now + 1e9 / (32768 >> (rate - 1));
    }
    if (now >= this.nextUpdate - 1) { this.regC |= 0x10; fire = 1; this.nextUpdate += 1e9; if (this.nextUpdate <= now) this.nextUpdate = now + 1e9; }
    if (fire) { this.regC |= 0x80; this.m.pic.raise(8); }
  }
}
void MS_DAY;
