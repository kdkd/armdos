// The ARM-PC's General MIDI synthesizer inside the page's AudioWorklet (see audio-gm.js).
// Added to the same AudioWorkletGlobalScope as audio-sb-worklet.js, whose processor
// hands it every audio chunk that carries MPU-401 MIDI bytes (the emulator's
// dev/mpu401.mjs remote mode: [sampleOffset << 9 | byte], 256 = reset) and
// the messages { gm: ... } (the sound set, pre-load MIDI state, on/off).
// The synth adds its output into the chunk before it is queued, so the MIDI
// music stays sample-aligned with the Sound Blaster's audio in emulated time.
import { GmSynth } from '../emu/dev/gmsynth.js';

class ArmPcGm {
  constructor() {
    this.syn = new GmSynth(sampleRate);
    this.enabled = true;
  }
  message(m) {
    const syn = this.syn;
    if (m.sf) syn.loadSoundFont(m.sf);                   // parsed on the main thread (dev/sf2.mjs)
    if (m.pre) {                                         // bytes sent before this module was loaded: state only
      syn.stateOnly = true;
      for (const b of m.pre) if (b === 256) syn.reset(); else syn.midiByte(b);
      syn.stateOnly = false;
    }
    if (m.reset) syn.reset();
  }
  /** Mix the chunk's MIDI into l/r (Float32Arrays of one chunk). */
  process(l, r, c) {
    const syn = this.syn, ev = c.midi, g = (c.gl + c.gr) / 2, n = l.length;
    let pos = 0;
    for (let i = 0; i < ev.length; i++) {
      const off = ev[i] >> 9, v = ev[i] & 511;
      if (off > pos) { syn.render(l, r, pos, off - pos, g); pos = off; }
      if (v === 256) syn.reset(); else syn.midiByte(v);
    }
    if (pos < n) syn.render(l, r, pos, n - pos, g);
    for (let i = 0; i < n; i++) {                        // the SB path clamps before; keep the sum in range
      if (l[i] > 1) l[i] = 1; else if (l[i] < -1) l[i] = -1;
      if (r[i] > 1) r[i] = 1; else if (r[i] < -1) r[i] = -1;
    }
  }
}
globalThis.armpcGm = new ArmPcGm();
