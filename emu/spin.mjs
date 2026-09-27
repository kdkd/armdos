// Spin-loop idle skip: fast-forward emulated time through pure polling loops.
//
// Many DOS programs busy-wait: "wait for vertical retrace" on port 3DAh, the
// refresh bit of port 61h, the BIOS tick at 0:046C. Emulating every iteration
// burns host CPU for nothing. This module detects such a loop and charges its
// iterations without executing them, keeping emulated timing exact:
//
//  1. Trigger: the same I/O port read returns the same value 12 times with no
//     I/O write in between, or a long time slice (>= 20000 instructions) ended
//     (maybe a memory-polling loop; failures back off exponentially).
//  2. Verify: from a clean instruction boundary, single-step (interpreter) one
//     loop iteration: it must come back to the same PC with identical
//     registers, flags, mode, VFP state, perform no I/O writes, take no
//     exception, and only read ports whose device can say how long their value
//     stays constant. With identical state every iteration performs identical
//     memory stores (no-ops after the first), so the loop's future is fixed
//     until an input changes.
//  3. Skip: an input can only change at the next scheduled device event
//     (timer IRQs, keyboard bytes, DMA/SB progress, ...), at the end of the run
//     slice, or when a polled port's value changes (device hint: VGA retrace /
//     hblank edges, the 61h refresh toggle, the RTC second). Whole iterations of
//     P instructions are charged (cpu.icount += k*P) up to that time; the loop
//     then runs normally across the change. Deterministic: no wall clock.
//
// A failed verification at a PC backs off exponentially. Machine hooks:
// onRead(port, value) / onWrite() from in8/out8, afterSlice(executed) and
// check(targetNs) from the run loop. m.spin.enabled = false disables it.

const REPEAT = 12;          // identical reads before trying
const REPEAT_HOT = 3;       // ... right after a skip succeeded (the next wait of a scan-line loop)
const HOT = 8;              // checks that stay "hot" after a success
const MAX_STEPS = 400;      // longest loop iteration verified (instructions)

export class SpinSkip {
  constructor(m) {
    this.m = m;
    this.enabled = true;
    this.lastPort = -1; this.lastVal = -1; this.rep = 0; this.hot = 0;
    this.ioOps = 0;               // I/O operations (reads + writes), for the no-I/O slice trigger
    this.pending = false; this.soon = false;
    this.probe = null;            // during verification: { ports: Set, wrote }
    this.backoff = new Map();     // pc -> { skip, next }
    this.sliceBackoff = 0; this.sliceWait = 0;
    this.stats = { skips: 0, skippedInsns: 0, attempts: 0, failures: 0 };
  }
  onRead(port, v) {
    this.ioOps++;
    if (this.probe) { this.probe.ports.add(port); return; }
    if (port === this.lastPort && v === this.lastVal) {
      if (++this.rep === (this.hot > 0 ? REPEAT_HOT : REPEAT) && this.enabled) { this.pending = true; this.m.cpu.requestStop(); }
    } else { this.lastPort = port; this.lastVal = v; this.rep = 0; }
  }
  onWrite() {
    this.ioOps++;
    this.rep = 0; this.lastPort = -1;
    if (this.probe) this.probe.wrote = true;
  }
  // after a CPU slice: a long slice (typically the one between two timer
  // interrupts) may be a memory poll; failures back off exponentially
  afterSlice(executed, ioBefore) {
    if (this.soon) { this.soon = false; this.pending = true; return; }
    if (this.again) { this.again = false; this.soon = true; return; }   // the loop probably continues after the event
    if (!this.enabled || executed < 20000 || this.m.cpu.halted) return;
    if (this.sliceWait > 0) { this.sliceWait--; return; }
    this.soon = true;     // verify a little into the next slice (after the timer IRQ handler returned)
  }
  // the run loop's next slice budget
  limit(budget) { return this.soon && budget > 600 ? 600 : budget; }

  /** Called by the run loop between slices when pending. Returns instructions charged. */
  check(targetNs) {
    this.pending = false;
    this.rep = 0;
    if (this.hot > 0) this.hot--;
    if (!this.enabled) return 0;
    const m = this.m, cpu = m.cpu;
    if (cpu.halted || m.stopped) return 0;
    const pc0 = cpu.pc | 0;
    const bo = this.backoff.get(pc0);
    if (bo && bo.skip > 0) { bo.skip--; return 0; }
    this.stats.attempts++;
    // ---- verify one iteration by single-stepping (never past a due event)
    const t0 = m.timeNs();
    const maxSteps = Math.min(MAX_STEPS, Math.floor((Math.min(targetNs, m.nextEventNs()) - t0) / m.nsPerInsn));
    if (maxSteps < 2) return 0;
    const s0 = snapshot(cpu);
    this.probe = { ports: new Set(), wrote: false };
    const w0 = m.isaWaitNs;
    const jit = cpu.jit; cpu.jit = null;
    let steps = 0, ok = false;
    try {
      while (steps < maxSteps) {
        const mode = cpu.mode;
        cpu.run(1);
        steps++;
        if (this.probe.wrote || cpu.halted || cpu.mode !== mode || m.stopped) break;
        if ((cpu.pc | 0) === pc0 && cpu.t === s0.t) { ok = same(s0, snapshot(cpu)); break; }
      }
    } finally { cpu.jit = jit; }
    const ports = this.probe.ports;
    const waitNs = m.isaWaitNs - w0;      // bus wait states of one iteration (game port reads)
    this.probe = null;
    m.sync();
    if (!ok) { this.fail(pc0, ports.size === 0); return 0; }
    // ---- how long can nothing change?
    const now = m.timeNs();
    // (port stability counted from the start of the verified iteration: its reads
    //  happened before `now`, and the skipped iterations must read the same values)
    let until = Math.min(targetNs, m.nextEventNs());
    for (const p of ports) until = Math.min(until, stableUntil(m, p, t0));
    const P = steps, iterNs = P * m.nsPerInsn + waitNs;
    const k = Math.floor((until - now) / iterNs);
    if (k <= 0) return 0;
    cpu.icount += k * P;
    m.sync();
    if (waitNs) m.nowNs += k * waitNs;
    this.stats.skips++; this.stats.skippedInsns += k * P;
    this.backoff.delete(pc0); this.sliceBackoff = 0; this.again = true; this.hot = HOT;
    return k * P;
  }
  fail(pc, memoryPoll) {
    this.stats.failures++;
    const bo = this.backoff.get(pc) || { skip: 0, next: 1 };
    bo.skip = bo.next; bo.next = Math.min(bo.next * 2, 4096);
    this.backoff.set(pc, bo);
    if (this.backoff.size > 4096) this.backoff.clear();
    this.sliceBackoff = Math.min(this.sliceBackoff * 2 + 1, 1024); this.sliceWait = this.sliceBackoff;
  }
}

function snapshot(c) {
  return { pc: c.pc | 0, t: c.t, mode: c.mode, r: Int32Array.from(c.r.subarray(0, 15)), n: c.nv < 0, z: c.zv === 0, c: c.c, v: c.v, q: c.q,
    i: c.i, f: c.f, fw: Int32Array.from(c.FW.subarray(0, 32)), fpscr: c.fpscr, irq: c.irqLine };
}
function same(a, b) {
  if (a.pc !== b.pc || a.t !== b.t || a.mode !== b.mode || a.n !== b.n || a.z !== b.z || a.c !== b.c || a.v !== b.v || a.q !== b.q ||
    a.i !== b.i || a.f !== b.f || a.fpscr !== b.fpscr || a.irq !== b.irq) return false;
  for (let k = 0; k < 15; k++) if (a.r[k] !== b.r[k]) return false;
  for (let k = 0; k < 32; k++) if (a.fw[k] !== b.fw[k]) return false;
  return true;
}

// Until when does reading `port` keep returning the same value (with no I/O
// writes and no device events)? Ports without a hint: no skipping.
function stableUntil(m, port, now) {
  if (port === 0x3BA && m.hgc) return m.hgc.statusStableUntil(now);
  if (port === 0x3DA || port === 0x3BA) return m.vga.statusStableUntil(now);
  if (m.hgc && port >= 0x3B0 && port <= 0x3BF) return Infinity;                               // Hercules CRTC (write-only regs read 0)
  if (port === 0x61) {
    const t = (Math.floor(now / 15085) + 1) * 15085;              // refresh toggle (bit 4)
    return Math.min(t, m.pit.out2StableUntil(now));
  }
  if (port === 0x71) return m.cmos.readStableUntil(now);
  if (port === 0x201 && m.joy) return m.joy.stableUntil(now);                                 // game port: until a one-shot ends
  // registers that only change through writes or scheduled device events
  if ((port >= 0x3C0 && port <= 0x3DF && port !== 0x3C9) || port === 0x3E0) return Infinity;   // VGA registers (not the DAC data auto-increment)
  if (port === 0x21 || port === 0xA1 || port === 0x64 || port === 0x70) return Infinity;       // PIC masks, 8042 status, CMOS index
  if (port === 0x3FD || port === 0x3FE || port === 0x2FD || port === 0x2FE) return Infinity;   // UART line / modem status
  if ((port >= 0x1F1 && port <= 0x1F7) || port === 0x3F6) return Infinity;                    // ATA task file / status
  if (port === 0x307 || port === 0x302 || (port >= 0xF0 && port <= 0xF2) || port === 0x379 || port === 0x37A) return Infinity;
  return now;
}
