// The ARM-PC's Sound Blaster 16 + OPL3 (and nothing else) on the page: the
// emulator renders its audio in emulated time (emu/dev/audio.mjs,
// machine.audio) and this module plays it through an AudioWorklet (or a
// ScriptProcessor where worklets are unavailable), into the page's master
// gain (so the sound button mutes it too). The PC speaker stays in audio.js.
// OUTPUT_GAIN balances the card against the page's other sounds: an OPL
// voice at full level is only -18 dBFS in the emulator's calibrated output, so
// the card gets +9.5 dB here (the master compressor catches the peaks).
//
//   const sb = new SbAudio(sound);   sb.attach(machine);   // after sound.init() (a user gesture)
//
// Nothing is created before attach(): no AudioContext without a gesture.
const OUTPUT_GAIN = 3;

export class SbAudio {
  constructor(sound) {
    this.sound = sound; this.node = null; this.kind = null; this.m = null;
    this.pending = []; this.stats = null;
    this.fallbackQ = []; this.fallbackAvail = 0;
  }
  /** Route machine.audio into WebAudio. Safe to call again for a new machine. */
  attach(m) {
    const ctx = this.sound.ctx;
    if (!ctx || !m.audio) return;
    this.m = m;
    m.audio.start(ctx.sampleRate, (l, r, midi) => this.push(l, r, midi));
    if (!this.node && !this.creating) this.create(ctx);
  }
  async create(ctx) {
    this.creating = true;
    try {
      if (!ctx.audioWorklet) throw new Error('no AudioWorklet');
      await ctx.audioWorklet.addModule(new URL('./audio-sb-worklet.js', import.meta.url));
      const node = new AudioWorkletNode(ctx, 'armpc-sb', { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
      node.port.onmessage = (e) => { this.stats = e.data; };
      this.node = node; this.kind = 'worklet';
    } catch (e) {
      // ScriptProcessor fallback: same queue policy, on the main thread
      const sp = ctx.createScriptProcessor(2048, 0, 2), target = Math.round(0.08 * ctx.sampleRate);
      let playing = false;
      sp.onaudioprocess = (ev) => {
        const L = ev.outputBuffer.getChannelData(0), R = ev.outputBuffer.getChannelData(1);
        if (!playing && this.fallbackAvail >= target) playing = true;
        for (let i = 0; i < L.length; i++) {
          const h = this.fallbackQ[0];
          if (!playing || !h) { L[i] = R[i] = 0; playing = false; continue; }
          L[i] = h[0][h[2]]; R[i] = h[1][h[2]]; this.fallbackAvail--;
          if (++h[2] >= h[0].length) this.fallbackQ.shift();
        }
      };
      this.node = sp; this.kind = 'scriptprocessor';
    }
    this.gain = ctx.createGain(); this.gain.gain.value = OUTPUT_GAIN;
    this.node.connect(this.gain); this.gain.connect(this.sound.master);
    this.creating = false;
    for (const [l, r] of this.pending) this.push(l, r);
    this.pending = [];
  }
  push(l, r, midi) {
    if (midi && this.gm) midi = this.gm.chunk(l, r, midi);   // MPU-401 MIDI for the GM synth (audio-gm.js)
    if (!this.node) { if (this.pending.length < 64) this.pending.push([l, r]); return; }
    if (this.kind === 'worklet') this.node.port.postMessage(midi ? [l, r, midi] : [l, r], [l.buffer, r.buffer]);
    else {
      this.fallbackQ.push([l, r, 0]); this.fallbackAvail += l.length;
      const max = 0.2 * this.sound.ctx.sampleRate;
      while (this.fallbackAvail > max && this.fallbackQ.length > 1) this.fallbackAvail -= this.fallbackQ.shift()[0].length;
    }
  }
  /** Drop queued audio (pause, power off). */
  flush() {
    this.pending = [];
    if (this.kind === 'worklet') this.node.port.postMessage('flush');
    else { this.fallbackQ = []; this.fallbackAvail = 0; }
  }
}
