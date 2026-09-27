// The picture tube. The emulator renders the VGA display into an RGBA image
// (render.js); this uploads it as a texture and draws it through a CRT shader:
// curvature, scanlines, phosphor bloom, vignette, the power-on warm-up and the
// power-off collapse to a dot. With the effect off (or without WebGL) it draws
// the same image flat and crisp.

const VS = `attribute vec2 p; varying vec2 vUv;
void main() { vUv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5); gl_Position = vec4(p, 0.0, 1.0); }`;

const FS = `precision highp float;
varying vec2 vUv;
uniform sampler2D uTex;
uniform vec2 uSrc;       // source size in texels
uniform vec2 uOut;       // canvas size in pixels
uniform float uCrt;      // 0 flat, 1 full effect
uniform float uBright;   // picture brightness (power / warm-up)
uniform float uSqueeze;  // power-off collapse 0..1
uniform float uDot;      // the lingering dot 0..1
uniform float uWobble;   // degauss wobble 0..1
uniform float uPurity;   // degauss colour-purity swirl 0..1
uniform float uTime;
uniform float uHasPic;
uniform float uMono;     // 1: a monochrome monitor (Hercules card): no shadow mask, more bloom
uniform vec3 uTint;      // its phosphor colour (glass tint, glow)

const float OVERSCAN = 0.935;

vec3 sharp(vec2 t) {
  vec2 texel = t * uSrc;
  vec2 scale = max(uOut * OVERSCAN / uSrc, vec2(1.0));
  vec2 fl = floor(texel);
  vec2 s = fract(texel) - 0.5;
  vec2 range = 0.5 - 0.5 / scale;
  vec2 f = (s - clamp(s, -range, range)) * scale + 0.5;
  return texture2D(uTex, (fl + f) / uSrc).rgb;
}

void main() {
  vec2 c = vUv * 2.0 - 1.0;                    // -1..1 over the glass
  float r2 = dot(c, c);
  vec2 w = c * (1.0 + uCrt * vec2(0.010, 0.014) * vec2(c.y * c.y, c.x * c.x));   // a gentle barrel: a late-80s tube is nearly flat
  vec2 pic = w / OVERSCAN;
  // power-off: squeeze to a line, then to a dot
  float sy = max(1.0 - uSqueeze * 1.35, 0.004);
  float sx = max(1.0 - max(uSqueeze - 0.62, 0.0) * 2.6, 0.004);
  pic = vec2(pic.x / sx, pic.y / sy);

  vec3 glass = mix(vec3(0.058, 0.066, 0.062), vec3(0.05) + uTint * 0.035, uMono) * (1.0 - 0.35 * r2);
  vec3 col = glass;

  vec2 t = pic * 0.5 + 0.5;
  // rounded picture corners
  // the lit area: a rounded rectangle whose corners follow the glass (computed in pixels)
  vec2 hp = 0.5 * uOut * OVERSCAN;
  float R = 0.014 * hp.x * 2.0;   // just softened: a tighter radius crops corner characters
  vec2 d = abs(pic * hp) - (hp - R);
  float edge = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - R;
  float inside = 1.0 - smoothstep(-0.8, 0.8, edge);

  if (uHasPic > 0.5 && inside > 0.0 && uBright > 0.0) {
    vec2 wob = uWobble * 0.01 * vec2(sin(uTime * 23.0 + c.y * 9.0), cos(uTime * 17.0 + c.x * 7.0));
    vec3 p;
    if (uCrt > 0.0) {
      p.r = sharp(t + wob * 1.3).r;
      p.g = sharp(t).g;
      p.b = sharp(t - wob).b;
      // phosphor bloom: a small blur added on top
      vec2 o = 1.25 / uSrc;
      vec3 b = texture2D(uTex, t + vec2(o.x, 0.0)).rgb + texture2D(uTex, t - vec2(o.x, 0.0)).rgb
             + texture2D(uTex, t + vec2(0.0, o.y)).rgb + texture2D(uTex, t - vec2(0.0, o.y)).rgb
             + texture2D(uTex, t + o).rgb + texture2D(uTex, t - o).rgb
             + texture2D(uTex, t + vec2(o.x, -o.y)).rgb + texture2D(uTex, t + vec2(-o.x, o.y)).rgb;
      b *= 0.125;
      p = p * mix(0.92, 0.88, uMono) + b * mix(0.30, 0.55, uMono);
      // a mono tube's halo: the phosphor glows a little into its surroundings
      vec2 o2 = 3.0 / uSrc;
      vec3 h = texture2D(uTex, t + vec2(o2.x, 0.0)).rgb + texture2D(uTex, t - vec2(o2.x, 0.0)).rgb
             + texture2D(uTex, t + vec2(0.0, o2.y)).rgb + texture2D(uTex, t - vec2(0.0, o2.y)).rgb;
      p += h * 0.045 * uMono;
      // scanlines: VGA always scans 400 lines (200-line modes are double-scanned)
      float lines = uSrc.y > 300.0 ? uSrc.y : uSrc.y * 2.0;
      float ppl = uOut.y * OVERSCAN / lines;
      float strength = uCrt * mix(0.34, 0.2, uMono) * smoothstep(1.7, 3.2, ppl);
      float sl = sin(3.14159265 * fract(t.y * lines));
      p *= mix(1.0, 0.55 + 0.6 * sl * sl, strength) * (1.0 + strength * 0.25);
      // aperture grille, only where there are pixels enough to show it
      float m = mod(gl_FragCoord.x, 3.0);
      vec3 mask = m < 1.0 ? vec3(1.0, 0.86, 0.86) : m < 2.0 ? vec3(0.86, 1.0, 0.86) : vec3(0.86, 0.86, 1.0);
      p *= mix(vec3(1.0), mask, uCrt * (1.0 - uMono) * 0.5 * smoothstep(2.2, 3.5, ppl));   // mono tubes have no mask
      // degauss: while the coil's field decays the beams land on the wrong phosphor dots,
      // so colour blotches swirl across the picture and fade (colour tubes only)
      if (uPurity > 0.0) {
        float a = uTime * 2.3 + c.x * 2.1 - c.y * 1.6 + 1.3 * sin(c.x * 3.3 + c.y * 2.1 - uTime * 3.1);
        vec3 tint = 0.5 + 0.5 * cos(a + vec3(0.0, 2.094, 4.189));
        p = mix(p, p * (0.3 + 1.4 * tint) + tint * 0.035, uPurity * (1.0 - uMono) * 0.8);
      }
      // vignette
      p *= mix(1.0, 1.0 - 0.32 * r2, uCrt);
      p = pow(p, vec3(0.96));
    } else {
      p = sharp(t);
    }
    float boost = 1.0 + uSqueeze * 2.5;
    col += p * inside * uBright * boost;
  }
  // the dot that lingers after switching off
  if (uDot > 0.0) {
    float dd = length(c * vec2(1.0, uOut.y / uOut.x) * vec2(1.0, 1.0));
    col += vec3(0.85, 0.95, 1.0) * uDot * (exp(-dd * 70.0) * 1.4 + exp(-dd * 16.0) * 0.18);
  }
  gl_FragColor = vec4(col, 1.0);
}`;

// the monochrome monitors of the Hercules card option (the emulator already
// draws in the phosphor's colour, emu/dev/hercules.mjs): glass tint and the
// afterglow per frame (x/256 of the last frame's light stays)
export const PHOSPHOR = {
  green: { label: 'P39 green', tint: [0.25, 1.0, 0.45], decay: 140, text: [[40, 196, 72], [130, 255, 150]] },
  amber: { label: 'P134 amber', tint: [1.0, 0.62, 0.1], decay: 124, text: [[208, 128, 8], [255, 196, 64]] },
  white: { label: 'paper white', tint: [0.9, 0.9, 0.85], decay: 92, text: [[184, 184, 172], [250, 250, 240]] },
};

export class Display {
  constructor(canvas, { webgl = true } = {}) {
    this.canvas = canvas;
    this.crt = true;
    this.bright = 0; this.squeeze = 0; this.dot = 0; this.wobble = 0; this.purity = 0; this.lastDegauss = -1e9;
    this.hasPic = false;
    this.anim = [];
    this.phosphor = null;                   // null: the VGA colour monitor; else PHOSPHOR key
    this.gl = webgl ? this.initGL() : null;
    if (!this.gl) { this.ctx = canvas.getContext('2d'); canvas.classList.add('gl-off'); this.off = document.createElement('canvas'); this.offCtx = this.off.getContext('2d'); }
    this.srcW = 720; this.srcH = 400;
    this.ro = new ResizeObserver(() => this.resize());
    this.ro.observe(canvas);
    this.resize();
    this.t0 = performance.now();
  }
  get usingGL() { return !!this.gl; }

  initGL() {
    const gl = this.canvas.getContext('webgl', { alpha: false, antialias: false, premultipliedAlpha: false, preserveDrawingBuffer: true });
    if (!gl) return null;
    const sh = (type, src) => { const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s); if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); return s; };
    try {
      const prog = gl.createProgram();
      gl.attachShader(prog, sh(gl.VERTEX_SHADER, VS)); gl.attachShader(prog, sh(gl.FRAGMENT_SHADER, FS));
      gl.linkProgram(prog);
      if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(prog));
      gl.useProgram(prog);
      const buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
      const loc = gl.getAttribLocation(prog, 'p'); gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
      this.tex = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, this.tex);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([0, 0, 0, 255]));
      this.u = {};
      for (const n of ['uTex', 'uSrc', 'uOut', 'uCrt', 'uBright', 'uSqueeze', 'uDot', 'uWobble', 'uPurity', 'uTime', 'uHasPic', 'uMono', 'uTint']) this.u[n] = gl.getUniformLocation(prog, n);
      this.canvas.addEventListener('webglcontextlost', (e) => { e.preventDefault(); });
      return gl;
    } catch (e) { console.warn('CRT shader unavailable, using the flat display', e); return null; }
  }

  resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2.5);
    const w = Math.max(1, Math.round(this.canvas.clientWidth * dpr)), h = Math.max(1, Math.round(this.canvas.clientHeight * dpr));
    if (this.canvas.width !== w || this.canvas.height !== h) { this.canvas.width = w; this.canvas.height = h; }
    this.needsDraw = true;
  }

  /** The monitor: null = VGA colour, or 'green' | 'amber' | 'white' (Hercules mono, PHOSPHOR). */
  setMonitor(name) {
    this.phosphor = PHOSPHOR[name] ? name : null;
    this.glow = null;
    this.needsDraw = true;
  }

  // a mono phosphor's afterglow: what was lit fades over a few frames instead of at once
  afterglow(img) {
    const n = img.width * img.height * 4;
    if (!this.glow || this.glow.length !== n) { this.glow = new Uint8ClampedArray(n); this.glowImg = { width: img.width, height: img.height, data: this.glow }; }
    const g = this.glow, d = img.data, k = PHOSPHOR[this.phosphor].decay;
    for (let i = 0; i < n; i++) { const f = (g[i] * k) >> 8, v = d[i]; g[i] = v > f ? v : f; }
    return this.glowImg;
  }

  /** Upload a new picture ({width, height, data: RGBA}) or null for "no signal". */
  setImage(img) {
    if (!img) { this.hasPic = false; this.glow = null; this.needsDraw = true; return; }
    if (this.phosphor && this.crt && this.gl) img = this.afterglow(img);
    this.hasPic = true; this.srcW = img.width; this.srcH = img.height;
    if (this.gl) {
      const gl = this.gl;
      gl.bindTexture(gl.TEXTURE_2D, this.tex);
      const u8 = img.data instanceof Uint8Array ? img.data : new Uint8Array(img.data.buffer, img.data.byteOffset, img.data.byteLength);
      if (this.texW === img.width && this.texH === img.height) gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, img.width, img.height, gl.RGBA, gl.UNSIGNED_BYTE, u8);
      else { gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, img.width, img.height, 0, gl.RGBA, gl.UNSIGNED_BYTE, u8); this.texW = img.width; this.texH = img.height; }
    } else {
      if (this.off.width !== img.width || this.off.height !== img.height) { this.off.width = img.width; this.off.height = img.height; this.imgData = null; }
      if (!this.imgData || this.imgData.data.buffer !== img.data.buffer) this.imgData = new ImageData(img.data, img.width, img.height);
      this.offCtx.putImageData(this.imgData, 0, 0);
    }
    this.needsDraw = true;
  }

  /** Animate a property: to value over ms (ease-out), after delay ms. */
  animate(prop, to, ms, delay = 0, ease = (x) => 1 - (1 - x) * (1 - x)) {
    this.anim = this.anim.filter((a) => a.prop !== prop);
    const t = performance.now() + delay;
    this.anim.push({ prop, from: null, to, t0: t, ms, ease });
  }
  get animating() { return this.anim.length > 0; }

  powerOn(degauss = true) {
    this.squeeze = 0; this.dot = 0; this.bright = 0;
    this.animate('bright', 1, 1500, 350, (x) => x * x * (3 - 2 * x));
    if (degauss) this.degauss(750);        // (half a second into the warm-up, so the picture is there to swim)
  }
  /** The degauss coil: the picture shakes and colour blotches swirl, then settle. The coil's
   *  thermistor needs minutes to cool, so pressing the button again soon does much less.
   *  Returns the strength (0..1), for the sound. */
  degauss(delay = 0) {
    const now = performance.now(), since = (now - this.lastDegauss) / 1000;
    const k = Math.min(1, 0.15 + since / 60);
    this.lastDegauss = now;
    this.animate('wobble', k, 120, delay); setTimeout(() => this.animate('wobble', 0, 1300), delay + 130);
    this.animate('purity', k, 90, delay); setTimeout(() => this.animate('purity', 0, 1900, 0, (x) => 1 - Math.pow(1 - x, 2.2)), delay + 100);
    return k;
  }
  powerOff() {
    this.anim = [];
    this.animate('squeeze', 1, 260, 0, (x) => x * x);
    this.dot = 0; this.animate('dot', 1, 60, 200);
    setTimeout(() => { this.animate('bright', 0, 60); this.animate('dot', 0, 1400, 0, (x) => 1 - Math.pow(1 - x, 3)); }, 270);
  }

  step(now) {
    for (const a of this.anim) {
      if (now < a.t0) continue;
      if (a.from === null) a.from = this[a.prop];
      const x = Math.min(1, (now - a.t0) / a.ms);
      this[a.prop] = a.from + (a.to - a.from) * a.ease(x);
      a.done = x >= 1;
      this.needsDraw = true;
    }
    this.anim = this.anim.filter((a) => !a.done);
  }

  draw(now = performance.now()) {
    this.step(now);
    if (!this.needsDraw && !this.wobble && !this.purity) return;
    this.needsDraw = false;
    const W = this.canvas.width, H = this.canvas.height;
    if (this.gl) {
      const gl = this.gl, u = this.u;
      gl.viewport(0, 0, W, H);
      gl.uniform1i(u.uTex, 0);
      gl.uniform2f(u.uSrc, this.srcW, this.srcH);
      gl.uniform2f(u.uOut, W, H);
      gl.uniform1f(u.uCrt, this.crt ? 1 : 0);
      gl.uniform1f(u.uBright, this.bright);
      gl.uniform1f(u.uSqueeze, this.squeeze);
      gl.uniform1f(u.uDot, this.dot);
      gl.uniform1f(u.uWobble, this.crt ? this.wobble : 0);
      gl.uniform1f(u.uPurity, this.crt ? this.purity : 0);
      gl.uniform1f(u.uTime, (now - this.t0) / 1000);
      gl.uniform1f(u.uHasPic, this.hasPic ? 1 : 0);
      const ph = PHOSPHOR[this.phosphor];
      gl.uniform1f(u.uMono, ph ? 1 : 0);
      gl.uniform3f(u.uTint, ...(ph ? ph.tint : [0, 0, 0]));
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    } else {
      const c = this.ctx;
      c.fillStyle = '#0f1110'; c.fillRect(0, 0, W, H);
      if (this.hasPic && this.bright > 0) {
        const ov = 0.935, sq = Math.max(1 - this.squeeze * 1.35, 0.004);
        const w = W * ov, h = H * ov * sq;
        c.globalAlpha = Math.min(1, this.bright);
        c.imageSmoothingEnabled = true; c.imageSmoothingQuality = 'high';
        c.save(); c.beginPath();
        if (c.roundRect) c.roundRect((W - w) / 2, (H - h) / 2, w, h, Math.min(w * 0.014, h / 2)); else c.rect((W - w) / 2, (H - h) / 2, w, h);
        c.clip();
        c.drawImage(this.off, (W - w) / 2, (H - h) / 2, w, h);
        c.restore();
        c.globalAlpha = 1;
      }
      if (this.dot > 0) {
        const g = c.createRadialGradient(W / 2, H / 2, 0, W / 2, H / 2, W * 0.03);
        g.addColorStop(0, `rgba(220,240,255,${this.dot})`); g.addColorStop(1, 'rgba(220,240,255,0)');
        c.fillStyle = g; c.fillRect(0, 0, W, H);
      }
    }
  }
}

/** Paint text lines into a native 720x400 image with the VGA font (used before the machine runs). */
export function textImage(font, lines, { fg = [170, 170, 170], hi = [255, 255, 255], img = null } = {}) {
  const w = 720, h = 400;
  img = img || { width: w, height: h, data: new Uint8ClampedArray(w * h * 4) };
  const d = img.data;
  for (let k = 0; k < d.length; k += 4) { d[k] = 0; d[k + 1] = 0; d[k + 2] = 0; d[k + 3] = 255; }
  lines.forEach((line, row) => {
    const bright = line.startsWith('!'); const text = bright ? line.slice(1) : line;
    const col = bright ? hi : fg;
    for (let i = 0; i < text.length && i < 80; i++) {
      const ch = text.charCodeAt(i) & 0xFF;
      for (let y = 0; y < 16; y++) {
        const bits = font[ch * 16 + y];
        for (let x = 0; x < 9; x++) {
          const on = x < 8 ? (bits >> (7 - x)) & 1 : 0;
          if (!on) continue;
          const o = (((row * 16 + y) * w) + i * 9 + x) * 4;
          d[o] = col[0]; d[o + 1] = col[1]; d[o + 2] = col[2];
        }
      }
    }
  });
  return img;
}
