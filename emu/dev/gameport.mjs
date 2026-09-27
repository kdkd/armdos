// The IBM game control adapter ("game port") at 201h: four NE558 one-shot timers read
// four potentiometers (two joysticks x two axes) and four button switches.
//
//   write 201h (any value)  fires the four one-shots: each axis bit goes to 1 and stays 1
//                           for 24.2 us + 0.011 us per ohm of the stick's potentiometer
//                           (0-100 kOhm: 24.2 us .. 1124.2 us), as IBM's Technical
//                           Reference gives it. A one-shot that is still timing ignores a
//                           new trigger (the 558 is not retriggerable). An axis with nothing
//                           plugged in (open circuit) never times out.
//   read 201h               bits 0-3 = one-shots still timing: A-X, A-Y, B-X, B-Y
//                           bits 4-7 = buttons A1, A2, B1, B2, 0 = pressed (open = 1)
//
// Time is emulated time (machine.timeNs()), so software that counts its polling loop or
// reads the PIT measures exactly what it would on a PC. Like every 8-bit ISA I/O cycle,
// an access to 201h takes 1 us of bus time (machine.isaWait): that, not the CPU clock,
// is what keeps the classic "IN AL,DX / TEST / LOOP" counts in the ranges DOS programs
// were written for (about 25-1100 per axis here, whatever the CPU clock).
//
// The host side (web page, tests):
//   gp.setAxis(n, pos)    n 0-3 (A-X, A-Y, B-X, B-Y); pos -1 (left/up) .. 0 (centre) .. +1
//                         (right/down) -> 0 .. 100 kOhm; null = unplugged (open circuit)
//   gp.setButton(n, on)   n 0-3 (A1, A2, B1, B2)
//   gp.plug(stick, on)    stick 0 (A) / 1 (B): both axes centred / open circuit
//   gp.state()            { axes: [ohms|null x4], buttons: bits } for indicators

export const JOY_BASE_NS = 24200;          // 24.2 us
export const JOY_NS_PER_OHM = 11;          // 0.011 us per ohm
export const JOY_MAX_OHMS = 100000;        // a 100 kOhm potentiometer
export const ISA_IO_NS = 1000;             // one 8-bit ISA I/O cycle

export class GamePort {
  constructor(m) {
    this.m = m;
    this.ohms = [null, null, null, null];  // nothing plugged in
    this.buttons = 0;                      // bit n = button n held
    this.onChange = null;                  // host callback (indicator lamps)
    this.reset();
  }
  // one-shot start times (ns). A one-shot runs while now < start + pulse: the pulse follows the
  // potentiometer as it is now, so a stick plugged in (or moved) while the timing capacitor
  // charges ends the pulse as the charge reaches the threshold.
  reset() { this.start = [0, 0, 0, 0]; this.fired = [false, false, false, false]; }   // idle since power-on
  end(i) { return this.fired[i] ? this.start[i] + this.pulseNs(i) : -Infinity; }
  /** Duration of axis n's pulse in ns (Infinity when open). */
  pulseNs(n) { const r = this.ohms[n]; return r === null ? Infinity : JOY_BASE_NS + JOY_NS_PER_OHM * r; }
  read() {
    const now = this.m.timeNs();
    let v = ((~this.buttons & 15) << 4);
    for (let i = 0; i < 4; i++) if (this.end(i) > now) v |= 1 << i;
    return v;
  }
  write() {
    const now = this.m.timeNs();
    for (let i = 0; i < 4; i++) if (this.end(i) <= now) { this.start[i] = now; this.fired[i] = true; }
  }
  /** Until when does a read return the same value (no writes, no host input)? */
  stableUntil(now) {
    let t = Infinity;
    for (let i = 0; i < 4; i++) { const x = this.end(i); if (x > now && x < t) t = x; }
    return t;
  }

  // ---------------------------------------------------------------- host side
  setAxis(n, pos) {
    if (pos === null || pos === undefined) this.ohms[n] = null;
    else {
      const p = Math.max(-1, Math.min(1, +pos || 0));
      this.ohms[n] = Math.round((p + 1) / 2 * JOY_MAX_OHMS);
    }
    this.changed();
  }
  /** Set the potentiometer directly (ohms, 0-100000, or null = open). */
  setOhms(n, r) { this.ohms[n] = r === null ? null : Math.max(0, Math.min(JOY_MAX_OHMS, Math.round(r))); this.changed(); }
  setButton(n, on) {
    const b = on ? this.buttons | (1 << n) : this.buttons & ~(1 << n);
    if (b !== this.buttons) { this.buttons = b; this.changed(); }
  }
  plug(stick, on = true) {
    for (const n of [stick * 2, stick * 2 + 1]) this.ohms[n] = on ? JOY_MAX_OHMS / 2 : null;
    if (!on) this.buttons &= ~(3 << (stick * 2));
    this.changed();
  }
  plugged(stick) { return this.ohms[stick * 2] !== null || this.ohms[stick * 2 + 1] !== null; }
  state() { return { axes: this.ohms.slice(), buttons: this.buttons }; }
  changed() { if (this.onChange) this.onChange(this); }
}
