// AudioWorklet side of the ARM-PC's sound card output (see audio-sb.js).
// The emulator posts [left, right] Float32Array chunks rendered in emulated
// time; this queue plays them at the device clock and absorbs the drift
// between the two: it waits for ~60 ms before starting, drops the oldest
// audio when more than ~180 ms is queued (the machine ran ahead), resamples
// by +-0.5% when the queue drifts away from its target, and pads silence
// (then waits for the target again) when it runs dry.
class ArmPcSbProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.q = []; this.off = 0; this.avail = 0;
    this.target = Math.round(0.06 * sampleRate); this.max = Math.round(0.18 * sampleRate);
    this.playing = false; this.pos = 0;           // fractional read position in the head chunk
    this.stats = { received: 0, played: 0, underruns: 0, dropped: 0 };
    this.lastL = 0; this.lastR = 0; this.tick = 0;
    this.port.onmessage = (e) => {
      const d = e.data;
      if (d === 'flush') { this.q = []; this.avail = 0; this.pos = 0; this.playing = false; return; }
      if (d.gm) { globalThis.armpcGm?.message(d.gm); return; }         // the GM synth (audio-gm-worklet.js)
      const [l, r] = d;
      if (d[2] && globalThis.armpcGm) globalThis.armpcGm.process(l, r, d[2]);   // MPU-401 MIDI of this chunk
      this.q.push([l, r]); this.avail += l.length; this.stats.received += l.length;
      if (this.avail > this.max) this.drop(this.avail - this.target);
    };
  }
  drop(n) {
    this.stats.dropped += n;
    while (n > 0 && this.q.length) {
      const [l] = this.q[0], left = l.length - Math.floor(this.pos);
      if (left <= n) { this.q.shift(); this.avail -= left; n -= left; this.pos = 0; }
      else { this.pos += n; this.avail -= n; n = 0; }
    }
  }
  process(inputs, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0], n = L.length;
    if (!this.playing && this.avail >= this.target) this.playing = true;
    // gentle rate adjustment keeps the queue near its target
    const step = this.avail > this.target * 1.5 ? 1.005 : this.avail < this.target * 0.5 ? 0.995 : 1;
    for (let i = 0; i < n; i++) {
      if (!this.playing || !this.q.length) {
        if (this.playing) { this.playing = false; this.stats.underruns++; }
        this.lastL *= 0.995; this.lastR *= 0.995;          // no click when the stream stops
        L[i] = this.lastL; R[i] = this.lastR;
        continue;
      }
      const [cl, cr] = this.q[0], k = Math.floor(this.pos);
      L[i] = this.lastL = cl[k]; R[i] = this.lastR = cr[k];
      const np = this.pos + step, used = Math.floor(np) - k;
      this.pos = np; this.avail -= used; this.stats.played++;
      if (Math.floor(this.pos) >= cl.length) { this.pos -= cl.length; this.q.shift(); }
    }
    if (++this.tick % 64 === 0) this.port.postMessage({ ...this.stats, queued: this.avail });
    return true;
  }
}
registerProcessor('armpc-sb', ArmPcSbProcessor);
