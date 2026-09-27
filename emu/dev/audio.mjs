// The ARM-PC's audio output: renders everything the machine sounds like, in
// emulated time, as stereo float samples at the host's rate (ARCH.md §4.3).
//
//   m.audio.start(48000, (left, right) => { ... });   // Float32Array chunks
//   m.audio.stop();
//
// Sources, mixed sample-accurately against the emulated clock:
//  * the Sound Blaster 16's DAC (DMA or direct), a timestamped sample stream
//    from dev/sb16.mjs, linearly interpolated to the output rate;
//  * the OPL3 (dev/opl3.mjs), clocked at 49716 Hz in emulated time with its
//    register writes applied at the emulated time they were made, linearly
//    resampled;
//  * optionally the PC speaker (square wave at PIT channel 2's frequency; the
//    web page synthesises the speaker itself, headless --wav wants it here).
// Both SB sources pass through the SB16 mixer's master/voice/FM volumes.
//
// The machine calls pump() after every run slice (and every ~20 ms of emulated
// time inside long runs), which emits whatever emulated time has passed. The
// consumer handles drift against its own clock (web/js/audio-sb.js keeps a
// ~60 ms queue, drops when too far ahead, pads silence on underrun). Nothing
// here ever waits for the consumer.
//
// While no consumer is attached, nothing is rendered: the SB still consumes
// its DMA and raises IRQs on time, OPL writes go straight into the chip.

import { OPL_RATE } from './opl3.mjs';

const OPL_NS = 1e9 / OPL_RATE;

export class AudioOut {
  constructor(m) {
    this.m = m;
    this.enabled = false; this.pumpedNs = 0;
    this.rate = 44100; this.onAudio = null;
    this.speakerOn = false;           // mix the PC speaker too
    this.gain = 1;
    // PC speaker
    this.spkEvents = [];              // [tNs, freq(0 = off)]
    this.spkFreq = 0; this.spkPhase = 0; this.spkLp = 0;
  }

  /** Start producing audio: rate in Hz, onAudio(left, right) receives Float32Array chunks. */
  start(rate, onAudio, { speaker = false, gain = 1 } = {}) {
    this.rate = rate; this.onAudio = onAudio; this.speakerOn = speaker; this.gain = gain;
    const now = this.m.timeNs();
    this.baseNs = now; this.produced = 0;
    this.spkEvents.length = 0; this.spkFreq = this.m.pit?.lastSpeaker || 0;
    this.m.sb?.audioStart(now);
    this.m.mpu?.audioStart(now, rate);
    this.enabled = true;
  }
  stop() {
    this.pump();
    this.enabled = false;
    this.onAudio = null;
    this.m.sb?.audioStop();
    this.m.mpu?.audioStop();
  }

  speaker(on, freq) {
    if (!this.enabled || !this.speakerOn) return;
    this.spkEvents.push(this.m.timeNs(), on ? freq : 0);
  }

  /** Render and emit everything up to the current emulated time. */
  pump() {
    if (!this.enabled) return;
    const now = this.m.timeNs();
    this.pumpedNs = now;
    const target = Math.floor((now - this.baseNs) * this.rate / 1e9);
    let n = target - this.produced;
    if (n <= 0) return;
    const dt = 1e9 / this.rate;
    while (n > 0) {
      const k = Math.min(n, 16384);
      const L = new Float32Array(k), R = new Float32Array(k);
      const t0 = this.baseNs + this.produced * dt;
      this.m.sb?.render(L, R, k, t0, dt);
      this.m.cdrom?.render(L, R, k, t0, dt);        // CD audio (dev/atapi.mjs): the drive's analog out into the SB16's CD input
      const midi = this.m.mpu?.render(L, R, k, t0, dt);   // MPU-401 + GM synth (dev/mpu401.mjs): mixed here, or MIDI for a remote synth
      if (this.speakerOn) this.renderSpeaker(L, R, k, t0, dt);
      const g = this.gain;
      for (let i = 0; i < k; i++) {
        let l = L[i] * g, r = R[i] * g;
        L[i] = l > 1 ? 1 : l < -1 ? -1 : l;
        R[i] = r > 1 ? 1 : r < -1 ? -1 : r;
      }
      this.produced += k; n -= k;
      // keep the sample counter small (exact: the base moves by whole samples' worth of ns)
      if (this.produced > 1e7) { this.baseNs += this.produced * dt; this.produced = 0; }
      if (this.onAudio) this.onAudio(L, R, midi || null);
    }
  }

  renderSpeaker(L, R, n, t0, dt) {
    const ev = this.spkEvents, rate = this.rate;
    let e = 0, f = this.spkFreq, ph = this.spkPhase, lp = this.spkLp;
    const a = 1 - Math.exp(-2 * Math.PI * 5000 / rate);    // a little paper-cone lowpass
    for (let i = 0; i < n; i++) {
      const t = t0 + i * dt;
      while (e < ev.length && ev[e] <= t) { f = ev[e + 1]; e += 2; }
      let v = 0;
      if (f > 18 && f < rate / 2) { ph += f / rate; ph -= Math.floor(ph); v = ph < 0.5 ? 0.25 : -0.25; }
      lp += a * (v - lp);
      L[i] += lp; R[i] += lp;
    }
    ev.splice(0, e);
    this.spkFreq = f; this.spkPhase = ph; this.spkLp = lp;
  }
}

export { OPL_NS };
