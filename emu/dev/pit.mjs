// PIT 8253/8254 (ports 0x40-0x43), 1.193182 MHz input.
// Counters are evaluated lazily from emulated time: a channel remembers when
// its count was loaded (in PIT ticks) and derives count/output from the
// elapsed ticks. Modes 0, 2 and 3 are exact; 1/5 behave like 0 (gate-triggered
// one-shots triggered at load), 4 like 0 with a one-tick pulse.
// Channel 0 -> IRQ0 on each rising edge of OUT. Channel 2 -> speaker (gate =
// port 0x61 bit 0).

export const PIT_HZ = 1193182;
const NS_PER_TICK = 1e9 / PIT_HZ;

class Channel {
  constructor(n) { this.n = n; this.reset(); }
  reset() {
    this.mode = 3; this.rw = 3; this.bcd = 0;
    this.reload = 0x10000;       // effective divisor
    this.loadTick = 0;           // tick at which counting started
    this.armed = false;          // a count has been loaded
    this.gate = this.n !== 2;    // ch0/1 gate tied high
    this.frozenElapsed = 0;      // elapsed ticks at gate-low (modes 0/4)
    this.latched = -1; this.latchHi = false;   // latched value / next byte is hi
    this.status = -1;            // latched status byte (read-back)
    this.readHi = false; this.writeHi = false; this.wlo = 0;
    this.nullCount = true;
    this.fired = false;          // mode 0: terminal count reached
  }
}

export class PIT {
  constructor(m) {
    this.m = m;                  // machine: nowTick(), pic, onSpeaker, reschedule()
    this.ch = [new Channel(0), new Channel(1), new Channel(2)];
    this.reset();
  }
  reset() {
    for (const c of this.ch) c.reset();
    // BIOS will program ch0; until then it is idle (no IRQ0)
    this.lastSpeaker = 0;
    this.due = Infinity;
  }
  tickNow() { return this.m.timeNs() / NS_PER_TICK; }

  // elapsed whole ticks since load (respecting gate)
  elapsed(c, t) {
    if (!c.armed) return 0;
    if (!c.gate) return c.frozenElapsed;
    return Math.floor(t - c.loadTick);
  }
  count(c, t) {
    const N = c.reload;
    if (!c.armed) return 0;
    const e = this.elapsed(c, t);
    switch (c.mode) {
      case 2: return N - (e % N);                      // N..1
      case 3: {                                        // decrements by 2, twice per period
        const half = Math.ceil(N / 2);
        const ph = e % N;
        const v = ph < half ? N - 2 * ph : N - 2 * (ph - half);
        return ((v & ~1) || N) & 0xFFFF;
      }
      default: return (N - e) & 0xFFFF;                  // mode 0/1/4/5: counts down, wraps
    }
  }
  out(c, t) {
    if (!c.armed) return c.mode === 0 ? 0 : 1;
    const N = c.reload, e = this.elapsed(c, t);
    switch (c.mode) {
      case 2: return (e % N) === N - 1 ? 0 : 1;
      case 3: return (e % N) < Math.ceil(N / 2) ? 1 : 0;
      case 4: case 5: return e === N ? 0 : 1;
      default: return e >= N ? 1 : 0;
    }
  }
  // next tick (absolute, > t) at which OUT of channel 0 rises; Infinity if none
  nextRise(c, t) {
    if (!c.armed || !c.gate) return Infinity;
    const N = c.reload, e = Math.max(0, Math.floor(t - c.loadTick + 1e-6));   // tolerate float error at edges
    switch (c.mode) {
      case 2: case 3: {            // rising edge at e = k*N (mode 3: start of high half); mode 2 at k*N too
        const k = Math.floor(e / N) + 1;
        return c.loadTick + k * N;
      }
      case 0: case 1:
        if (c.fired) return Infinity;
        return c.loadTick + N;
      default: return Infinity;
    }
  }
  // the next channel-0 rising edge, kept as an absolute time so that an edge
  // falling exactly on "now" is still due
  schedule() {
    const t = this.nextRise(this.ch[0], this.tickNow());
    this.due = t === Infinity ? Infinity : t * NS_PER_TICK;
    this.m.reschedule();
  }
  nextEventNs() { return this.due; }
  // called by the machine when emulated time reaches nextEventNs()
  service() {
    const c = this.ch[0];
    const dueTick = Math.round(this.due / NS_PER_TICK);
    if (c.mode === 0 || c.mode === 1) { c.fired = true; this.due = Infinity; this.m.pic.raise(0); return; }
    // modes 2/3: edge-triggered request, then the next edge one period later
    this.m.pic.lower(0);
    this.m.pic.raise(0);
    this.due = (dueTick + c.reload) * NS_PER_TICK;
    const now = this.m.timeNs();
    if (this.due <= now) this.schedule();       // fell far behind (should not happen)
  }

  read(port) {
    if (port === 0x43) return 0xFF;
    const c = this.ch[port - 0x40];
    if (c.status >= 0) { const s = c.status; c.status = -1; return s; }
    let v;
    if (c.latched >= 0) {
      v = c.latched;
      if (c.rw === 3) {
        if (!c.latchHi) { c.latchHi = true; return v & 0xFF; }
        c.latched = -1; c.latchHi = false; return (v >>> 8) & 0xFF;
      }
      c.latched = -1;
      return c.rw === 2 ? (v >>> 8) & 0xFF : v & 0xFF;
    }
    v = this.count(c, this.tickNow());
    if (c.bcd) v = toBcd(v);
    if (c.rw === 1) return v & 0xFF;
    if (c.rw === 2) return (v >>> 8) & 0xFF;
    if (!c.readHi) { c.readHi = true; return v & 0xFF; }
    c.readHi = false; return (v >>> 8) & 0xFF;
  }

  write(port, val) {
    const t = this.tickNow();
    if (port === 0x43) {
      const sc = val >>> 6;
      if (sc === 3) {                        // read-back (8254)
        for (let n = 0; n < 3; n++) if (val & (2 << n)) {
          const c = this.ch[n];
          if (!(val & 0x20) && c.latched < 0) { let v = this.count(c, t); if (c.bcd) v = toBcd(v); c.latched = v; c.latchHi = false; }
          if (!(val & 0x10)) c.status = (this.out(c, t) << 7) | (c.nullCount ? 0x40 : 0) | (c.rw << 4) | (c.mode << 1) | c.bcd;
        }
        return;
      }
      const c = this.ch[sc];
      const rw = (val >>> 4) & 3;
      if (rw === 0) {                         // counter latch
        if (c.latched < 0) { let v = this.count(c, t); if (c.bcd) v = toBcd(v); c.latched = v; c.latchHi = false; }
        return;
      }
      c.rw = rw; c.mode = (val >>> 1) & 7; if (c.mode > 5) c.mode -= 4; c.bcd = val & 1;
      c.readHi = false; c.writeHi = false; c.latched = -1; c.nullCount = true;
      if (c.mode === 0) { c.armed = false; }   // OUT goes low, waits for count
      if (sc === 0) { this.m.pic.lower(0); this.schedule(); }
      if (sc === 2) this.speakerUpdate();
      return;
    }
    const c = this.ch[port - 0x40];
    let complete = false, v = 0;
    if (c.rw === 1) { v = val; complete = true; }
    else if (c.rw === 2) { v = val << 8; complete = true; }
    else if (!c.writeHi) { c.wlo = val; c.writeHi = true; if (c.mode === 0) c.armed = false; }
    else { v = c.wlo | (val << 8); c.writeHi = false; complete = true; }
    if (!complete) return;
    let N = c.bcd ? fromBcd(v) : v;
    if (N === 0) N = c.bcd ? 10000 : 0x10000;
    if (c.mode === 3 && N === 1) N = 0x10000;   // mode 3 with count 1 is illegal on real parts
    if (c.mode === 2 && N === 1) N = 2;
    // modes 2/3 reprogrammed while running: new count takes effect at the next reload;
    // we approximate by restarting now (what most software expects).
    c.reload = N; c.loadTick = Math.floor(t) + 1; c.armed = true; c.fired = false; c.nullCount = false;
    if (!c.gate) c.frozenElapsed = 0;
    if (c.n === 0) { this.m.pic.lower(0); this.schedule(); }
    if (c.n === 2) this.speakerUpdate();
  }

  setGate2(g) {
    const c = this.ch[2];
    g = !!g;
    if (g === c.gate) return;
    const t = this.tickNow();
    if (!g) { c.frozenElapsed = this.elapsed(c, t); c.gate = false; }
    else {
      c.gate = true;
      // modes 1,2,3,5 restart on the rising gate; 0/4 resume
      if (c.mode === 0 || c.mode === 4) c.loadTick = Math.floor(t) - c.frozenElapsed;
      else { c.loadTick = Math.floor(t) + 1; c.fired = false; }
    }
    this.speakerUpdate();
  }
  out2() { return this.out(this.ch[2], this.tickNow()); }
  // next time OUT2 (port 61h bit 5) can change
  out2StableUntil(nowNs) {
    const c = this.ch[2];
    if (!c.armed || !c.gate) return Infinity;
    const t = this.tickNow(), e = Math.max(0, Math.floor(t - c.loadTick + 1e-6)), N = c.reload;
    let next;
    switch (c.mode) {
      case 2: { const ph = e % N; next = ph < N - 1 ? e + (N - 1 - ph) : e + 1; break; }
      case 3: { const ph = e % N, half = Math.ceil(N / 2); next = ph < half ? e + (half - ph) : e + (N - ph); break; }
      default: next = e < N ? N : Infinity;
    }
    return next === Infinity ? Infinity : (c.loadTick + next) * NS_PER_TICK;
  }

  speakerUpdate() {
    const c = this.ch[2];
    const on = this.m.speakerEnable && c.gate && c.armed && (c.mode === 3 || c.mode === 2);
    const freq = on ? PIT_HZ / c.reload : 0;
    const key = on ? freq : 0;
    if (key !== this.lastSpeaker) { this.lastSpeaker = key; this.m.speaker(!!on, freq); }
  }
}

function toBcd(v) { v %= 10000; return (v % 10) | (((v / 10 | 0) % 10) << 4) | (((v / 100 | 0) % 10) << 8) | ((v / 1000 | 0) << 12); }
function fromBcd(v) { return (v & 15) + ((v >>> 4) & 15) * 10 + ((v >>> 8) & 15) * 100 + ((v >>> 12) & 15) * 1000; }
