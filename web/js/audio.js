// Every sound on the page, synthesised with WebAudio: the PC speaker, the power
// switch, fan and PSU hum, the CRT's degauss, floppy steps and spindle, hard
// disk chatter and the dot-matrix printer. Nothing is sampled.

function noiseBuffer(ctx, seconds, kind) {
  const n = Math.floor(ctx.sampleRate * seconds);
  const b = ctx.createBuffer(1, n, ctx.sampleRate), d = b.getChannelData(0);
  let last = 0;
  for (let i = 0; i < n; i++) {
    const w = Math.random() * 2 - 1;
    if (kind === 'brown') { last = (last + 0.02 * w) / 1.02; d[i] = last * 3.5; }
    else d[i] = w;
  }
  return b;
}

export class Sound {
  constructor() {
    this.ctx = null; this.muted = false; this.ambient = false;
    this.fdHeadFree = 0; this.fdMotorOff = 0; this.hdNext = 0;
    this.emuBase = 0; this.audioBase = 0;
  }
  get ready() { return !!this.ctx; }
  get now() { return this.ctx ? this.ctx.currentTime : 0; }

  /** Must be called from a user gesture. */
  init() {
    if (this.ctx) { if (this.ctx.state !== 'running') this.ctx.resume(); return true; }
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return false;
    const c = this.ctx = new AC({ latencyHint: 'interactive' });
    this.master = c.createGain(); this.master.gain.value = this.muted ? 0 : 1;
    const comp = c.createDynamicsCompressor();
    comp.threshold.value = -14; comp.knee.value = 10; comp.ratio.value = 3; comp.attack.value = 0.004; comp.release.value = 0.2;
    this.master.connect(comp); comp.connect(c.destination);
    // fan, PSU hum, disk spindle and the flyback whine have their own switch (off by default)
    this.ambientGain = c.createGain(); this.ambientGain.gain.value = this.ambient ? 1 : 0; this.ambientGain.connect(this.master);
    this.white = noiseBuffer(c, 2, 'white');
    this.brown = noiseBuffer(c, 4, 'brown');
    // the PC speaker: a square wave through a tiny paper cone
    this.spkOsc = c.createOscillator(); this.spkOsc.type = 'square'; this.spkOsc.frequency.value = 440;
    this.spkGate = c.createGain(); this.spkGate.gain.value = 0;
    const hp = c.createBiquadFilter(); hp.type = 'highpass'; hp.frequency.value = 260; hp.Q.value = 0.8;
    const lp = c.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 3800; lp.Q.value = 1.4;
    const cone = c.createBiquadFilter(); cone.type = 'peaking'; cone.frequency.value = 1400; cone.gain.value = 5; cone.Q.value = 1.2;
    const vol = c.createGain(); vol.gain.value = 0.16;
    this.spkOsc.connect(this.spkGate); this.spkGate.connect(hp); hp.connect(lp); lp.connect(cone); cone.connect(vol); vol.connect(this.master);
    this.spkOsc.start();
    return true;
  }
  setAmbient(on) {
    this.ambient = on;
    if (this.ambientGain) this.ambientGain.gain.setTargetAtTime(on ? 1 : 0, this.ctx.currentTime, 0.15);
  }
  setMuted(m) {
    this.muted = m;
    if (this.master) this.master.gain.setTargetAtTime(m ? 0 : 1, this.ctx.currentTime, 0.03);
  }

  // ---------------------------------------------------------------- building blocks
  src(buf, t, dur, loop = false) {
    const s = this.ctx.createBufferSource(); s.buffer = buf; s.loop = loop;
    s.start(t, loop ? 0 : Math.random() * Math.max(0, buf.duration - dur - 0.01));
    if (!loop) s.stop(t + dur + 0.05);
    return s;
  }
  filt(type, f, q = 0.7) { const b = this.ctx.createBiquadFilter(); b.type = type; b.frequency.value = f; b.Q.value = q; return b; }
  env(t, peak, attack, decay, sustain = 0) {
    const g = this.ctx.createGain(); g.gain.value = 0;
    g.gain.setValueAtTime(0, t);
    g.gain.linearRampToValueAtTime(peak, t + attack);
    g.gain.setTargetAtTime(sustain, t + attack, decay / 3);
    return g;
  }
  /** A filtered noise burst. */
  burst(t, { dur = 0.03, type = 'bandpass', f = 1000, q = 1, gain = 0.3, attack = 0.001, decay = dur, dest = this.master, buf = this.white }) {
    const s = this.src(buf, t, dur + decay), fl = this.filt(type, f, q), g = this.env(t, gain, attack, decay);
    s.connect(fl); fl.connect(g); g.connect(dest);
  }
  /** A decaying tone. */
  tone(t, { f = 100, f2 = null, type = 'sine', dur = 0.1, gain = 0.2, attack = 0.002, dest = this.master }) {
    const o = this.ctx.createOscillator(); o.type = type; o.frequency.setValueAtTime(f, t);
    if (f2) o.frequency.exponentialRampToValueAtTime(f2, t + dur);
    const g = this.env(t, gain, attack, dur);
    o.connect(g); g.connect(dest); o.start(t); o.stop(t + attack + dur * 2 + 0.05);
  }

  // ---------------------------------------------------------------- PC speaker
  /** Call once per emulator slice so speaker events land at the right audio time. */
  frameSync(emuMs) { if (!this.ctx) return; this.emuBase = emuMs; this.audioBase = this.ctx.currentTime + 0.045; }
  speaker(on, freq, emuMs) {
    if (!this.ctx) return;
    let t = this.audioBase + (emuMs - this.emuBase) / 1000;
    if (!(t >= this.ctx.currentTime)) t = this.ctx.currentTime;
    const g = this.spkGate.gain;
    if (on && freq > 18 && freq < 20000) {
      this.spkOsc.frequency.setValueAtTime(freq, t);
      g.setTargetAtTime(1, t, 0.0008);
    } else g.setTargetAtTime(0, t, 0.0012);
  }
  speakerOff() { if (this.ctx) { this.spkGate.gain.cancelScheduledValues(0); this.spkGate.gain.setTargetAtTime(0, this.ctx.currentTime, 0.002); } }

  // ---------------------------------------------------------------- mechanics
  click(kind = 'button') {
    if (!this.ctx) return;
    const t = this.now + 0.005;
    if (kind === 'button') {
      this.burst(t, { f: 2600, q: 1.5, dur: 0.004, gain: 0.35 });
      this.tone(t, { f: 900, dur: 0.012, gain: 0.05, type: 'triangle' });
      this.burst(t + 0.07, { f: 3200, q: 1.5, dur: 0.003, gain: 0.18 });
    } else if (kind === 'latch') {
      this.burst(t, { f: 1900, q: 2, dur: 0.006, gain: 0.4 });
      this.tone(t, { f: 420, dur: 0.02, gain: 0.08, type: 'triangle' });
    }
  }
  powerSwitch(on) {
    if (!this.ctx) return;
    const t = this.now + 0.005;
    this.burst(t, { type: 'highpass', f: 2500, dur: 0.004, gain: 0.5 });
    this.burst(t + 0.002, { type: 'lowpass', f: 1100, dur: 0.05, decay: 0.07, gain: 0.55 });
    this.tone(t, { f: on ? 95 : 80, f2: 45, dur: 0.12, gain: 0.45 });
    if (on) this.tone(t + 0.03, { f: 50, dur: 0.25, gain: 0.08, type: 'triangle' });   // the PSU relay taking the load
  }
  fanStart() {
    if (!this.ctx || this.fan) return;
    const c = this.ctx, t = this.now;
    const s = this.src(this.brown, t, 0, true);
    const lp = this.filt('lowpass', 90, 0.5), bp = this.filt('peaking', 190, 2); bp.gain.value = 6;
    lp.frequency.setTargetAtTime(520, t, 0.7);
    const g = c.createGain(); g.gain.value = 0; g.gain.setTargetAtTime(0.07, t, 0.6);
    s.connect(lp); lp.connect(bp); bp.connect(g); g.connect(this.ambientGain);
    // mains hum from the power supply transformer
    const hum = c.createOscillator(); hum.frequency.value = 120; const hg = c.createGain(); hg.gain.value = 0; hg.gain.setTargetAtTime(0.006, t, 0.3);
    hum.connect(hg); hg.connect(this.ambientGain); hum.start(t);
    // the hard disk's spindle spinning up to 3600 rpm
    const sp = c.createOscillator(); sp.type = 'triangle'; sp.frequency.setValueAtTime(15, t + 0.3); sp.frequency.exponentialRampToValueAtTime(240, t + 5);
    const spg = c.createGain(); spg.gain.value = 0; spg.gain.setTargetAtTime(0.012, t + 0.3, 0.8); spg.gain.setTargetAtTime(0.004, t + 6, 2);
    const splp = this.filt('lowpass', 900);
    sp.connect(splp); splp.connect(spg); spg.connect(this.ambientGain); sp.start(t + 0.3);
    this.fan = { s, lp, g, hum, hg, sp, spg };
  }
  fanStop() {
    const f = this.fan; if (!f) return;
    const t = this.now;
    f.g.gain.cancelScheduledValues(t); f.g.gain.setTargetAtTime(0, t, 0.9);
    f.lp.frequency.cancelScheduledValues(t); f.lp.frequency.setTargetAtTime(70, t, 1.0);
    f.hg.gain.setTargetAtTime(0, t, 0.05);
    f.sp.frequency.cancelScheduledValues(t); f.sp.frequency.setValueAtTime(f.sp.frequency.value, t); f.sp.frequency.exponentialRampToValueAtTime(10, t + 4);
    f.spg.gain.cancelScheduledValues(t); f.spg.gain.setTargetAtTime(0, t, 1.2);
    f.s.stop(t + 5); f.hum.stop(t + 1); f.sp.stop(t + 5);
    this.fan = null;
    this.whineStop();
  }
  /** The monitor: relay click, the degauss coil's BWONG (k = strength), and the flyback's whine. */
  degauss(k = 1, whine = true, delay = 0.22) {
    if (!this.ctx) return;
    const c = this.ctx, t = this.now + delay;
    const G = (v) => v * k;
    this.burst(t, { type: 'bandpass', f: 1600, q: 1.2, dur: 0.006, gain: 0.35 });   // relay
    const o1 = c.createOscillator(); o1.type = 'sawtooth'; o1.frequency.value = 60;
    const o2 = c.createOscillator(); o2.type = 'square'; o2.frequency.value = 120;
    const lp = this.filt('lowpass', 420, 1.5);
    const g = c.createGain(); g.gain.value = 0;
    g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(G(0.42), t + 0.012); g.gain.setTargetAtTime(0.0, t + 0.03, 0.33);
    const g2 = c.createGain(); g2.gain.value = 0.35;
    o1.connect(lp); o2.connect(g2); g2.connect(lp); lp.connect(g); g.connect(this.master);
    o1.start(t); o2.start(t); o1.stop(t + 2.2); o2.stop(t + 2.2);
    this.tone(t, { f: 58, f2: 40, dur: 0.18, gain: G(0.35) });                        // the thunk
    this.burst(t + 0.01, { type: 'lowpass', f: 300, dur: 0.3, decay: 0.4, gain: G(0.25), buf: this.brown });
    // flyback whine (15.7 kHz): faint, then your ears get used to it
    if (!whine) return;
    this.whineStop();
    const w = c.createOscillator(); w.frequency.value = 15734;
    const wg = c.createGain(); wg.gain.value = 0;
    wg.gain.setTargetAtTime(0.006, t + 0.2, 0.3); wg.gain.setTargetAtTime(0.0015, t + 3, 3);
    w.connect(wg); wg.connect(this.ambientGain); w.start(t + 0.1);
    this.whine = { w, wg };
  }
  whineStop() {
    if (!this.whine) return;
    const t = this.now; this.whine.wg.gain.cancelScheduledValues(t); this.whine.wg.gain.setTargetAtTime(0, t, 0.05); this.whine.w.stop(t + 0.5);
    this.whine = null;
  }
  crtOff() {
    if (!this.ctx) return;
    const t = this.now;
    this.tone(t, { f: 900, f2: 120, dur: 0.18, gain: 0.03, type: 'sine' });
    this.burst(t, { type: 'highpass', f: 5000, dur: 0.12, gain: 0.05 });
    this.whineStop();
  }

  // ---------------------------------------------------------------- floppy drive
  fdStep(t, loud = 1) {
    this.burst(t, { f: 1500 + Math.random() * 300, q: 3, dur: 0.004, decay: 0.012, gain: 0.3 * loud });
    this.tone(t, { f: 210, dur: 0.008, gain: 0.07 * loud, type: 'square' });
  }
  /** Head movement: one click per cylinder, ~3 ms per step. */
  fdSeek(from, to) {
    if (!this.ctx) return;
    const n = Math.abs(to - from);
    this.fdMotor();
    if (!n) return;
    let t = Math.max(this.now + 0.01, this.fdHeadFree);
    if (t > this.now + 0.6) return;                 // don't build up a backlog
    for (let k = 0; k < n; k++) this.fdStep(t + k * 0.0032, 0.8);
    this.fdHeadFree = t + n * 0.0032 + 0.012;
  }
  /** Recalibrate: step out 80 times, grinding against track 0. */
  fdRecal(steps = 80) {
    if (!this.ctx) return;
    this.fdMotor();
    let t = Math.max(this.now + 0.01, this.fdHeadFree);
    for (let k = 0; k < steps; k++) this.fdStep(t + k * 0.0036, 1);
    this.fdHeadFree = t + steps * 0.0036 + 0.02;
  }
  /**
   * The POST seek: the head steps out to track 0 and back in, twice: the
   * classic "grrk-grrk" of a PC finding its floppy drive. About half a second.
   */
  fdPostSeek() {
    if (!this.ctx) return;
    this.fdMotor();
    let t = Math.max(this.now + 0.02, this.fdHeadFree);
    const run = (n, rate, loud) => {
      for (let k = 0; k < n; k++) {
        const tt = t + k * rate + (Math.random() - 0.5) * 0.0006;
        this.burst(tt, { f: 950 + Math.random() * 250, q: 2.2, dur: 0.003, decay: 0.014, gain: 0.42 * loud });
        this.tone(tt, { f: 150, dur: 0.007, gain: 0.09 * loud, type: 'square' });
      }
      t += n * rate;
      this.burst(t, { type: 'lowpass', f: 700, dur: 0.006, decay: 0.03, gain: 0.3 * loud });   // the head settling
    };
    run(14, 0.0055, 1);   t += 0.07;     // out
    run(10, 0.0055, 0.85); t += 0.12;    // back in
    run(14, 0.0055, 0.9); t += 0.07;
    run(10, 0.0055, 0.8);
    this.fdHeadFree = t + 0.03;
  }
  /** The spindle: a soft rhythmic whirr, on until two seconds after the last access. */
  fdMotor() {
    if (!this.ctx) return;
    const c = this.ctx, t = this.now;
    this.fdMotorOff = t + 2.0;
    if (this.motor) { this.motor.g.gain.cancelScheduledValues(t); this.motor.g.gain.setTargetAtTime(0.16, t, 0.08); return; }
    const s = this.src(this.white, t, 0, true);
    const bp = this.filt('bandpass', 380, 0.9), lp = this.filt('lowpass', 1400);
    const g = c.createGain(); g.gain.value = 0; g.gain.setTargetAtTime(0.16, t, 0.12);
    const trem = c.createGain(); trem.gain.value = 0.6;
    const lfo = c.createOscillator(); lfo.frequency.value = 5; const lg = c.createGain(); lg.gain.value = 0.4;
    lfo.connect(lg); lg.connect(trem.gain); lfo.start(t);
    const hum = c.createOscillator(); hum.type = 'triangle'; hum.frequency.value = 55; const hg = c.createGain(); hg.gain.value = 0.25;
    hum.connect(hg); hg.connect(lp);
    s.connect(bp); bp.connect(lp); lp.connect(trem); trem.connect(g); g.connect(this.master); hum.start(t);
    this.burst(t, { type: 'lowpass', f: 700, dur: 0.02, gain: 0.25 });      // head load
    this.motor = { s, g, lfo, hum };
    const check = () => {
      if (!this.motor) return;
      if (this.now < this.fdMotorOff) { setTimeout(check, 250); return; }
      const m = this.motor, t2 = this.now; this.motor = null;
      m.g.gain.setTargetAtTime(0, t2, 0.25); m.s.stop(t2 + 1.5); m.lfo.stop(t2 + 1.5); m.hum.stop(t2 + 1.5);
    };
    setTimeout(check, 300);
  }
  fdRead(count) { this.fdMotor(); }
  fdInsert() {
    if (!this.ctx) return;
    const t = this.now + 0.01;
    this.burst(t, { type: 'bandpass', f: 900, q: 0.8, dur: 0.07, decay: 0.05, gain: 0.18 });     // sliding in
    this.burst(t + 0.09, { type: 'lowpass', f: 2200, dur: 0.012, decay: 0.03, gain: 0.55 });      // shutter + clamp
    this.tone(t + 0.09, { f: 160, dur: 0.05, gain: 0.12, type: 'triangle' });
    this.burst(t + 0.16, { f: 2400, q: 2, dur: 0.004, gain: 0.25 });
  }
  fdEject() {
    if (!this.ctx) return;
    const t = this.now + 0.01;
    this.burst(t, { f: 2600, q: 1.5, dur: 0.004, gain: 0.3 });
    this.burst(t + 0.03, { type: 'lowpass', f: 1800, dur: 0.02, decay: 0.05, gain: 0.6 });        // ka-
    this.tone(t + 0.03, { f: 130, dur: 0.06, gain: 0.16, type: 'triangle' });
    this.burst(t + 0.07, { type: 'bandpass', f: 1200, q: 0.8, dur: 0.06, decay: 0.05, gain: 0.2 }); // -chunk, the disk springs out
  }

  // ---------------------------------------------------------------- hard disk
  hdAccess(cyl, prevCyl, isWrite) {
    if (!this.ctx) return;
    const t = this.now;
    if (t < this.hdNext) return;
    this.hdNext = t + 0.022;
    const d = Math.abs(cyl - prevCyl);
    const t0 = t + 0.005 + Math.random() * 0.004;
    this.burst(t0, { f: 3400, q: 3, dur: 0.002, decay: 0.006, gain: 0.16 });
    if (d > 0) {
      const settle = 0.004 + Math.sqrt(d) * 0.0018;
      this.burst(t0 + settle, { f: 2800, q: 4, dur: 0.002, decay: 0.008, gain: 0.13 });
      this.tone(t0, { f: 480 + d * 3, dur: settle, gain: 0.02, type: 'sawtooth' });
    }
    if (isWrite) this.burst(t0 + 0.012, { f: 4200, q: 5, dur: 0.002, gain: 0.05 });
  }

  // ---------------------------------------------------------------- printer
  /** The head crossing a line: an AM buzz, one pulse per character. */
  printLine(nChars, cps, t = this.now) {
    if (!this.ctx || nChars <= 0) return;
    const c = this.ctx, dur = nChars / cps;
    const s = this.src(this.white, t, dur + 0.05);
    const bp = this.filt('bandpass', 2300, 1.6);
    const buzz = c.createOscillator(); buzz.type = 'sawtooth'; buzz.frequency.value = 1150;
    const bz = c.createGain(); bz.gain.value = 0.05;
    const am = c.createGain(); am.gain.value = 0;
    const lfo = c.createOscillator(); lfo.type = 'square'; lfo.frequency.value = cps;
    const lg = c.createGain(); lg.gain.value = 0.5;
    const off = c.createConstantSource ? c.createConstantSource() : null;
    lfo.connect(lg); lg.connect(am.gain);
    if (off) { off.offset.value = 0.5; off.connect(am.gain); off.start(t); off.stop(t + dur + 0.02); }
    const g = c.createGain(); g.gain.value = 0;
    g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(0.32, t + 0.01); g.gain.setValueAtTime(0.32, t + dur); g.gain.linearRampToValueAtTime(0, t + dur + 0.02);
    s.connect(bp); bp.connect(am); buzz.connect(bz); bz.connect(am); am.connect(g); g.connect(this.master);
    buzz.start(t); lfo.start(t); buzz.stop(t + dur + 0.05); lfo.stop(t + dur + 0.05);
    // the carriage motor underneath
    this.tone(t, { f: 95, dur, gain: 0.04, type: 'triangle' });
  }
  printReturn(cols, t = this.now) {
    if (!this.ctx) return;
    const dur = Math.min(0.35, 0.05 + cols * 0.003);
    this.burst(t, { type: 'bandpass', f: 500, q: 0.7, dur, decay: 0.05, gain: 0.12, buf: this.brown });
    this.tone(t, { f: 140, f2: 90, dur, gain: 0.04, type: 'triangle' });
    this.burst(t + dur, { type: 'lowpass', f: 1500, dur: 0.01, gain: 0.2 });
  }
  printFeed(lines = 1, t = this.now) {
    if (!this.ctx) return;
    const dur = Math.min(0.9, 0.035 * lines);
    const c = this.ctx, o = c.createOscillator(); o.type = 'square'; o.frequency.value = 180;
    const g = this.env(t, 0.05, 0.004, dur); const lp = this.filt('lowpass', 900);
    o.connect(lp); lp.connect(g); g.connect(this.master); o.start(t); o.stop(t + dur + 0.1);
  }
}
