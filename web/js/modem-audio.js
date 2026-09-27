// The modem's speaker (docs/MODEM.md): every sound synthesised with WebAudio from the modem's
// sound events (emu/dev/modem.mjs). Events carry emulated milliseconds and are placed on the
// audio clock the same way as the PC speaker (Sound.frameSync: audioBase + (t - emuBase)).
//
// The trainings are not recordings: emu/modemsound.mjs generates the actual line signals of
// each modulation (V.22bis, V.32bis, V.34, and the whole V.8bis/V.8/V.34/V.90 56K sequence,
// tuned against a real 56K call - see docs/MODEM.md) at 8 kHz, both directions mixed as the
// calling modem's speaker hears them; here they go through a small, tinny speaker.

const DTMF_ROW = { 1: 697, 2: 697, 3: 697, A: 697, 4: 770, 5: 770, 6: 770, B: 770, 7: 852, 8: 852, 9: 852, C: 852, '*': 941, 0: 941, '#': 941, D: 941 };
const DTMF_COL = { 1: 1209, 4: 1209, 7: 1209, '*': 1209, 2: 1336, 5: 1336, 8: 1336, 0: 1336, 3: 1477, 6: 1477, 9: 1477, '#': 1477, A: 1633, B: 1633, C: 1633, D: 1633 };
const VOL = [0.22, 0.4, 0.7, 1.0];

import { handshake, dataSignal, lineSignal } from '../emu/modemsound.js';
import { modulation } from '../emu/dev/modem.js';

// ------------------------------------------------------------------ the speaker
export class ModemAudio {
  constructor(sound) { this.sound = sound; this.cache = {}; this.ready = false; this.active = new Set(); this.log = []; this.vol = 2; }

  init() {
    const s = this.sound;
    if (this.ready || !s.ctx) return !!this.ready;
    const c = s.ctx;
    // a 2" speaker on the modem card, behind the case: telephone band, a peaky cone, a bit of grit
    this.in = c.createGain(); this.in.gain.value = 1;
    this.volume = c.createGain(); this.volume.gain.value = 0.5;
    const hp = c.createBiquadFilter(); hp.type = 'highpass'; hp.frequency.value = 320; hp.Q.value = 0.7;
    const lp = c.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 3600; lp.Q.value = 0.9;
    const pk = c.createBiquadFilter(); pk.type = 'peaking'; pk.frequency.value = 1900; pk.gain.value = 6; pk.Q.value = 1.4;
    const sh = c.createWaveShaper();
    const curve = new Float32Array(1024); for (let i = 0; i < 1024; i++) { const x = i / 511.5 - 1; curve[i] = Math.tanh(1.6 * x) / Math.tanh(1.6); }
    sh.curve = curve;
    this.in.connect(hp); hp.connect(lp); lp.connect(pk); pk.connect(sh); sh.connect(this.volume); this.volume.connect(s.master);
    // continuous tone pairs, gated: dial tone, ringback, busy, answer tone
    this.pairs = {};
    const pair = (name, f1, f2, g = 0.18) => {
      const gate = c.createGain(); gate.gain.value = 0; gate.connect(this.in);
      for (const f of [f1, f2].filter(Boolean)) {
        const o = c.createOscillator(); o.frequency.value = f; const og = c.createGain(); og.gain.value = g;
        o.connect(og); og.connect(gate); o.start();
      }
      this.pairs[name] = gate;
    };
    pair('dialtone', 350, 440, 0.16); pair('ringback', 440, 480, 0.15); pair('ringintl', 425, 0, 0.17); pair('busy', 480, 620, 0.15); pair('answer', 2100, 0, 0.22);
    this.ready = true;
    return true;
  }

  /** Emulated ms -> audio time (seconds). */
  at(tMs) {
    const s = this.sound;
    let t = s.audioBase + (tMs - s.emuBase) / 1000;
    if (!(t >= s.ctx.currentTime)) t = s.ctx.currentTime + 0.005;
    return t;
  }

  /** A modem sound event (emu/dev/modem.mjs onSound). */
  event(e) {
    this.log.push({ kind: e.kind, t: e.t, digit: e.digit, on: e.on, mod: e.mod, ms: e.ms });
    if (this.log.length > 200) this.log.shift();
    const s = this.sound;
    if (!s.ctx) return;
    if (!this.ready) this.init();
    const t = this.at(e.t);
    if (e.vol !== undefined && e.vol !== this.vol) { this.vol = e.vol; this.volume.gain.setTargetAtTime(VOL[e.vol] * 0.5, t, 0.01); }
    switch (e.kind) {
      case 'relay': this.relay(t, e.on); break;
      case 'ringback':
        if (e.on) this.gate(e.tone === 'intl' ? 'ringintl' : 'ringback', true, t);
        else { this.gate('ringback', false, t); this.gate('ringintl', false, t); }
        break;
      case 'dialtone': case 'busy': case 'answer': this.gate(e.kind, e.on, t); break;
      case 'pickup': { const p = lineSignal('pickup', 150); this.play(p.samples, t, false, p.sr); break; }
      case 'line': { const l = lineSignal(e.sound, e.ms || 150); this.play(l.samples, t, false, l.sr); break; }
      case 'dtmf': this.dtmf(t, e.digit, e.ms / 1000); break;
      case 'pulse': this.pulse(t, e.n || +e.digit || 10); break;
      case 'handshake': { const key = (e.mod || modulation(e.rate)) + e.role; const h = this.cache[key] || (this.cache[key] = handshake(e.mod || modulation(e.rate), { role: e.role })); this.lastHandshake = { mod: e.mod, secs: h.total, t }; this.play(h.samples, t, false, h.sr); break; }
      case 'carrier': if (e.on) this.carrier(t, e.rate); else this.hush(t); break;
      case 'hush': this.hush(t); break;
    }
  }
  gate(name, on, t) { const g = this.pairs[name].gain; g.cancelScheduledValues(t); g.setTargetAtTime(on ? 1 : 0, t, 0.004); }
  hush(t) {
    for (const g of Object.values(this.pairs)) { g.gain.cancelScheduledValues(t); g.gain.setTargetAtTime(0, t, 0.004); }
    for (const src of this.active) { try { src.stop(t + 0.01); } catch { /* not started */ } }
    this.active.clear();
  }
  relay(t, on) {
    // the off-hook relay on the card: a sharp tick and a small thump on the line
    const s = this.sound, c = s.ctx;
    s.burst(t, { type: 'highpass', f: on ? 3000 : 2600, dur: 0.003, gain: 0.28 });
    s.tone(t, { f: on ? 180 : 140, f2: 60, dur: 0.03, gain: 0.12, type: 'triangle' });
    // the line itself clicks in the speaker as the loop closes / opens
    const o = c.createBufferSource(); o.buffer = s.white;
    const g = c.createGain(); g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(0.35, t + 0.001); g.gain.exponentialRampToValueAtTime(0.001, t + 0.025);
    o.connect(g); g.connect(this.in); o.start(t); o.stop(t + 0.04);
  }
  dtmf(t, digit, dur) {
    const c = this.sound.ctx, d = String(digit).toUpperCase();
    const g = c.createGain(); g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(0.2, t + 0.004);
    g.gain.setValueAtTime(0.2, t + dur - 0.004); g.gain.linearRampToValueAtTime(0, t + dur);
    g.connect(this.in);
    for (const f of [DTMF_ROW[d], DTMF_COL[d]]) {
      if (!f) continue;
      const o = c.createOscillator(); o.frequency.value = f; o.connect(g); o.start(t); o.stop(t + dur + 0.01);
      this.active.add(o); o.onended = () => this.active.delete(o);
    }
  }
  pulse(t, n) {
    // 10 pulses per second, 60 ms break / 40 ms make: a click at each edge, loud on break
    for (let i = 0; i < n; i++) {
      const tb = t + i * 0.1;
      this.sound.burst(tb, { type: 'bandpass', f: 1800, q: 0.8, dur: 0.004, gain: 0.25, dest: this.in });
      this.sound.burst(tb + 0.06, { type: 'bandpass', f: 1400, q: 0.8, dur: 0.003, gain: 0.12, dest: this.in });
      this.sound.tone(tb, { f: 900, dur: 0.012, gain: 0.03, type: 'square' });   // the relay contacts
    }
  }
  play(samples, t, loop = false, sr = this.sound.ctx.sampleRate) {
    const c = this.sound.ctx;
    const b = c.createBuffer(1, samples.length, sr); b.getChannelData(0).set(samples);
    const src = c.createBufferSource(); src.buffer = b; src.loop = loop;
    src.connect(this.in); src.start(t);
    this.active.add(src); src.onended = () => this.active.delete(src);
    return src;
  }
  carrier(t, rate) { const d = dataSignal(modulation(rate)); this.play(d.samples, t, true, d.sr); }
}
