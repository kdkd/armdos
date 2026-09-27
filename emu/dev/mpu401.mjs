// Roland MPU-401 MIDI interface at 330h (data 330h, status/command 331h),
// IRQ 9 (INT 71h), with a General MIDI synthesizer behind it - the ARM-PC's
// "Sound Canvas" daughterboard. BLASTER=A220 I7 D1 H5 P330 T6.
//
// The interface (as programs see it):
//  * 331h read = status: bit 7 DSR (0 = a byte is waiting at 330h), bit 6 DRR
//    (0 = ready for a command/data byte: always ready here), other bits 1.
//  * 331h write = command. Intelligent mode (after power-on/reset): FFh reset ->
//    ACK FEh; 3Fh UART mode -> ACK FEh; ACh version -> FEh 15h; ADh revision ->
//    FEh 01h; any other command -> FEh (accepted, no effect: the sequencer,
//    tracks and timer of the intelligent mode are not emulated). UART mode:
//    only FFh is a command (back to intelligent mode, no ACK - as the real
//    MPU-401 and DOSBox); everything else written to 331h is ignored.
//  * 330h write in UART mode = a MIDI byte out (to the synth); in intelligent
//    mode = ignored. 330h read = the next byte of the input queue (ACKs; there
//    is no MIDI IN), FFh when empty.
//  * IRQ 9 is raised while the input queue holds bytes (an ACK), lowered when it
//    is read empty (programs normally poll and leave IRQ 9 masked).
// Command responses are immediate (a real MPU needs up to a few ms for a reset;
// programs wait for the ACK by polling DSR, which works either way).
//
// MIDI bytes are timestamped with the emulated time they were written and
// rendered at that time by the synthesizer (dev/gmsynth.mjs) through the
// machine's audio output (dev/audio.mjs), like the OPL3's register writes:
//  * local synth (node tests, headless --wav): options.synth (a GmSynth), mixed
//    straight into the audio chunks;
//  * remote synth (the web page's AudioWorklet): options.remote = true; the
//    bytes are handed to the audio consumer with the chunk they belong to
//    (render() returns { midi: Int32Array of (sampleOffset << 9 | byte), gl, gr },
//    byte 256 = reset the synth), and the consumer synthesises them in step.
// The synth's level follows the SB16 mixer's master x FM ("MIDI") volume, as
// a Wave Blaster on an SB16 did.
//
// options: { base = 0x330, irq = 9, present = true, synth, remote, onFirstUse(), onMidi(tNs, byte) }

const volGain = (reg) => { const v = reg >> 3; return v === 0 ? 0 : Math.pow(10, (v - 31) / 10); };

export class MPU401 {
  constructor(m, o = {}) {
    this.m = m;
    this.base = o.base ?? 0x330; this.irq = o.irq ?? 9;
    this.present = o.present !== false;
    this.synth = o.synth || null;
    this.remote = !!o.remote;
    this.onFirstUse = o.onFirstUse || null;
    this.onMidi = o.onMidi || null;
    this.used = false;
    this.q = [];                       // [tNs, value] pairs waiting for the renderer (value 256 = synth reset)
    this.bytesOut = 0;
    this.uart = false; this.inq = [];
    this.reset();
  }

  /** Machine reset (the ISA reset line resets the daughterboard too). */
  reset() {
    this.uart = false; this.inq.length = 0;
    this.m.pic?.lower(this.irq);
    if (this.used) this.toSynth(256);
  }

  attachSynth(synth) { this.synth = synth; this.remote = false; }

  audioStart(now, rate) {
    this.q.length = 0;
    if (this.synth && this.synth.rate !== rate && this.synth.setRate) this.synth.setRate(rate);
  }
  audioStop() {
    if (this.synth) for (let i = 0; i < this.q.length; i += 2) this.apply(this.synth, this.q[i + 1]);
    this.q.length = 0;
  }

  // ------------------------------------------------------------ ports
  read(port) {
    if (!this.present) return 0xFF;
    if (port & 1) return (this.inq.length ? 0 : 0x80) | 0x3F;
    if (!this.inq.length) return 0xFF;
    const v = this.inq.shift();
    if (!this.inq.length) this.m.pic?.lower(this.irq);
    return v;
  }
  write(port, v) {
    if (!this.present) return;
    if (!(port & 1)) { if (this.uart) this.midiOut(v); return; }
    this.firstUse();
    if (this.uart) {
      if (v === 0xFF) { this.uart = false; this.inq.length = 0; this.m.pic?.lower(this.irq); }
      return;
    }
    switch (v) {
      case 0xFF: this.inq.length = 0; this.ack(); break;
      case 0x3F: this.uart = true; this.ack(); break;
      case 0xAC: this.ack(0x15); break;                  // version 1.5
      case 0xAD: this.ack(0x01); break;                  // revision
      default: this.ack(); break;
    }
  }
  ack(extra) {
    this.inq.push(0xFE);
    if (extra !== undefined) this.inq.push(extra);
    this.m.pic?.raise(this.irq);
  }

  firstUse() {
    if (this.used) return;
    this.used = true;
    if (this.onFirstUse) this.onFirstUse();
  }

  midiOut(b) {
    this.firstUse();
    this.bytesOut++;
    if (this.onMidi) this.onMidi(this.m.timeNs(), b);
    this.toSynth(b);
  }
  toSynth(v) {
    if (this.m.audio.enabled && (this.synth || this.remote)) this.q.push(this.m.timeNs(), v);
    else if (this.synth) this.apply(this.synth, v);
  }
  apply(syn, v) { if (v === 256) syn.reset(); else syn.midiByte(v); }

  // ------------------------------------------------------------ rendering
  /** Called by dev/audio.mjs for each chunk: mixes (local) or returns the chunk's MIDI (remote). */
  render(L, R, n, t0, dt) {
    if (!this.used) return null;
    const x = this.m.sb?.mixer, gl = x ? volGain(x[0x30]) * volGain(x[0x34]) : 1, gr = x ? volGain(x[0x31]) * volGain(x[0x35]) : 1;
    const q = this.q, tEnd = t0 + n * dt;
    let k = 0;
    while (k < q.length && q[k] < tEnd) k += 2;
    if (this.remote) {
      const midi = new Int32Array(k >> 1);
      for (let i = 0; i < k; i += 2) {
        let off = Math.floor((q[i] - t0) / dt); if (off < 0) off = 0; if (off >= n) off = n - 1;
        midi[i >> 1] = (off << 9) | q[i + 1];
      }
      q.splice(0, k);
      return { midi, gl, gr };
    }
    const syn = this.synth;
    if (!syn) { q.splice(0, k); return null; }
    // render up to each event, apply it, continue (the synth mixes both channels with one gain: use the mean,
    // then balance)
    const g = (gl + gr) / 2;
    let pos = 0;
    for (let i = 0; i < k; i += 2) {
      let off = Math.floor((q[i] - t0) / dt); if (off < pos) off = pos; if (off > n) off = n;
      if (off > pos) { syn.render(L, R, pos, off - pos, g); pos = off; }
      this.apply(syn, q[i + 1]);
    }
    q.splice(0, k);
    if (pos < n) syn.render(L, R, pos, n - pos, g);
    return null;
  }
}
