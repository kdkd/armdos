// The ARM-PC's MPU-401 + General MIDI synthesizer on the page ("Sound Canvas"-style,
// SoundFont based: emu/dev/gmsynth.mjs with the ARM-PC GS sound set, apps/midi).
//
//   const gm = new GmAudio(sbAudio);         // after the SbAudio
//   gm.setImages(images);                    // images.json: images.gm = the sound set
//   new Machine({ ..., ...gm.machineOptions() });
//   gm.mountToggle(afterButton);             // the "MIDI" toolbar switch (Sound Canvas / off)
//
// The emulator's MPU-401 runs in remote mode: the MIDI bytes it receives travel with the
// Sound Blaster's audio chunks (audio-sb.js) into the AudioWorklet, where the synth
// (audio-gm-worklet.js) renders them in step - off the main thread, in emulated time.
// Nothing is fetched until a program first touches the MPU-401: then the worklet module is
// added and the ~6 MB sound set downloaded and parsed (here, so the audio thread never
// stalls), and handed over. MIDI sent before that only sets channel state (programs,
// controllers); notes start sounding once the sound set has arrived.
// With the switch off the MPU-401 is not there at all (ports read FFh): programs fall
// back to FM music on the OPL3, as on a PC without a MIDI card.
import { prefs, fetchBinary } from './util.js';
import { parseSf2 } from '../emu/dev/sf2.js';
import { GmSynth } from '../emu/dev/gmsynth.js';

export class GmAudio {
  constructor(sb) {
    this.sb = sb; sb.gm = this;
    this.enabled = prefs.get('gmSynth', true);
    this.url = null; this.state = 'idle';          // idle -> loading -> ready | failed
    this.moduleReady = false; this.pre = [];
    this.local = null;                             // ScriptProcessor fallback: synth on this thread
    this.onState = null;
  }
  setImages(images) { this.url = images?.gm?.file || null; this.expect = images?.gm || null; }

  machineOptions() {
    return { mpu: { remote: true, present: this.enabled, onFirstUse: () => this.start() } };
  }
  setEnabled(on) {
    this.enabled = on; prefs.set('gmSynth', on);
    const m = this.sb.m;
    if (m?.mpu) m.mpu.present = on;                // takes effect at the program's next detection
    if (this.btn) this.btn.setAttribute('aria-pressed', String(on));
  }

  /** A toolbar switch after `after` (a button): "MIDI" on = Sound Canvas-style synth, off = no MPU-401. */
  mountToggle(after) {
    if (!after) return;
    const b = document.createElement('button');
    b.className = after.className; b.id = 'gmBtn'; b.textContent = 'MIDI';
    b.title = 'MIDI synth: Sound Canvas-style General MIDI (SoundFont) on the MPU-401 at 330h, or off (no MPU-401: FM music)';
    b.setAttribute('aria-pressed', String(this.enabled));
    b.onclick = () => this.setEnabled(!this.enabled);
    after.after(b);
    this.btn = b;
  }

  // ------------------------------------------------------------ loading
  start() {
    if (this.state !== 'idle') return;
    this.state = 'loading'; this.emit();
    this.load().then(() => { this.state = 'ready'; this.emit(); },
      (e) => { console.error('General MIDI sound set:', e); this.state = 'failed'; this.emit(); });
  }
  emit() { if (this.onState) this.onState(this.state); }
  async load() {
    const ctx = this.sb.sound.ctx;
    const sfP = this.url ? fetchBinary(this.url, null, this.expect).then((b) => parseSf2(b)) : Promise.reject(new Error('no sound set in images.json'));
    // the worklet module (once the SB node exists as an AudioWorklet)
    for (let i = 0; i < 100 && !this.sb.node; i++) await new Promise((r) => setTimeout(r, 50));
    if (this.sb.kind === 'worklet') {
      await ctx.audioWorklet.addModule(new URL('./audio-gm-worklet.js', import.meta.url));
      this.moduleReady = true;
      this.post({ pre: this.pre }); this.pre = [];
    } else {
      this.local = new GmSynth(ctx ? ctx.sampleRate : 48000);
      this.local.stateOnly = true; for (const b of this.pre) this.apply(this.local, b); this.local.stateOnly = false;
      this.pre = [];
    }
    const sf = await sfP;
    if (this.local) this.local.loadSoundFont(sf);
    else this.post({ sf }, [sf.data.buffer]);
  }
  post(gm, transfer = []) { this.sb.node?.port.postMessage({ gm }, transfer); }
  apply(syn, v) { if (v === 256) syn.reset(); else syn.midiByte(v); }

  /** From audio-sb.js for each chunk with MPU data: returns what to forward to the worklet (or null). */
  chunk(l, r, c) {
    if (this.local) {                               // ScriptProcessor fallback
      const syn = this.local, ev = c.midi, g = (c.gl + c.gr) / 2;
      let pos = 0;
      for (let i = 0; i < ev.length; i++) {
        const off = ev[i] >> 9;
        if (off > pos) { syn.render(l, r, pos, off - pos, g); pos = off; }
        this.apply(syn, ev[i] & 511);
      }
      if (pos < l.length) syn.render(l, r, pos, l.length - pos, g);
      return null;
    }
    if (this.moduleReady) return c;
    if (this.pre.length < 65536) for (const e of c.midi) this.pre.push(e & 511);
    return null;
  }
}
