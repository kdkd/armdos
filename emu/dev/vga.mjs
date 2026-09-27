// VGA (ports 0x3C0-0x3DF) + the ARM-PC mode register at 0x3E0 (ARCH.md §6).
// The display is rendered from guest memory by render.mjs; this device holds
// the registers (CRTC, sequencer, graphics controller, attribute controller,
// DAC, misc output, CGA colour select) and the IBM VGA's display memory model:
//
//   * 256 KB of video memory as four 64 KB planes, stored interleaved
//     (vram[offset * 4 + plane], so vram32[offset] holds the four planes'
//     bytes at one offset: exactly the 32-bit latch);
//   * the CPU's view of it through the graphics controller: read modes 0/1
//     (read map select; colour compare / don't care), write modes 0-3
//     (data rotate, set/reset + enable, AND/OR/XOR with the latches, bit mask),
//     the sequencer's map mask, latches loaded on every read.
//
// Memory decoding (ARM-PC): in chain-4 graphics (mode 13h) and in text and CGA
// modes the CPU sees ordinary RAM at A0000h/B8000h, as before (the renderer
// reads it; chain-4 offset o of plane p is RAM[A0000h + (o & ~3) + p]). When a
// program turns chain-4 off in graphics (Mode X/Y) or sets a planar 16-colour
// mode (0Dh/0Eh/10h/12h), A0000h-AFFFFh becomes an MMIO window onto the planes
// (machine.setVgaWindow: loads and stores there go to winRead/winWrite); the
// contents move between RAM and the planes when the window opens or closes.
// Text mode with odd/even off and A0000h mapped (the classic "program plane 2"
// font loading sequence) opens the window too; plane 2's first 8 KB is the
// character generator (machine.font).
//
// Raster effects: register writes that change the picture (DAC, attribute,
// CRTC, sequencer, pel mask) are logged per frame with the scan line they
// happened on (from emulated time: vertical retrace/blank for the first 1.4 ms
// of each 1/70 s, then the displayed lines evenly), and the renderer draws the
// last complete frame from its starting register state plus that timeline
// (renderVga in render.mjs). The CRTC start address is latched at the start of
// retrace, as on the real card.
//
// Writing the mode register 3E0h loads the standard IBM register set for that
// mode (misc, sequencer, CRTC except the cursor registers, graphics controller,
// attribute mode/overscan/plane enable/panning/colour select); the BIOS sets
// the palettes itself.

export const FRAME_NS = 1e9 / 70;          // 70 Hz
export const VRETRACE_NS = 1.4e6;          // first ~1.4 ms of each frame (also the blanking the renderer uses)
const LINE_NS = 31777.6;                   // 31.4686 kHz
const HACTIVE_NS = 25422;
export const ACTIVE_NS = FRAME_NS - VRETRACE_NS;

// the standard VGA power-on DAC palette (6-bit)
export function defaultDac() {
  const d = new Uint8Array(768);
  const ega = [[0, 0, 0], [0, 0, 42], [0, 42, 0], [0, 42, 42], [42, 0, 0], [42, 0, 42], [42, 21, 0], [42, 42, 42],
    [21, 21, 21], [21, 21, 63], [21, 63, 21], [21, 63, 63], [63, 21, 21], [63, 21, 63], [63, 63, 21], [63, 63, 63]];
  let k = 0;
  for (const c of ega) { d[k++] = c[0]; d[k++] = c[1]; d[k++] = c[2]; }
  for (const g of [0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63]) { d[k++] = g; d[k++] = g; d[k++] = g; }
  const groups = [[0, 16, 31, 47, 63], [31, 39, 47, 55, 63], [45, 49, 54, 58, 63],
    [0, 7, 14, 21, 28], [14, 17, 21, 24, 28], [20, 22, 24, 26, 28],
    [0, 4, 8, 12, 16], [8, 10, 12, 14, 16], [11, 12, 13, 15, 16]];
  for (const [a, b, c, dd, e] of groups) {
    const seq = [[a, a, e], [b, a, e], [c, a, e], [dd, a, e], [e, a, e], [e, a, dd], [e, a, c], [e, a, b],
      [e, a, a], [e, b, a], [e, c, a], [e, dd, a], [e, e, a], [dd, e, a], [c, e, a], [b, e, a],
      [a, e, a], [a, e, b], [a, e, c], [a, e, dd], [a, e, e], [a, dd, e], [a, c, e], [a, b, e]];
    for (const s of seq) { d[k++] = s[0]; d[k++] = s[1]; d[k++] = s[2]; }
  }
  return d;   // entries 248-255 remain black
}
// identity at reset: with the default 256-colour DAC (whose first 16 entries are the
// CGA/EGA colours) text and CGA colours are right without any BIOS setup
const DEFAULT_ATTR = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];

// ---- the IBM VGA BIOS register sets: misc, sequencer 0-4, CRTC 0-18h,
// graphics controller 0-8, attribute 10h-14h
const T = (misc, seq, crtc, gc, attr) => ({ misc, seq, crtc, gc, attr });
const CRTC_TEXT80 = [0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, 0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF];
const CRTC_TEXT40 = [0x2D, 0x27, 0x28, 0x90, 0x2B, 0xA0, 0xBF, 0x1F, 0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x14, 0x1F, 0x96, 0xB9, 0xA3, 0xFF];
const GC_TEXT = [0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF];
const GC_PLANAR = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF];
const MODE13 = T(0x63, [0x03, 0x01, 0x0F, 0x00, 0x0E],
  [0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF],
  [0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF], [0x41, 0x00, 0x0F, 0x00, 0x00]);
export const STD_MODES = {
  0x00: T(0x67, [0x03, 0x08, 0x03, 0x00, 0x02], CRTC_TEXT40, GC_TEXT, [0x0C, 0x00, 0x0F, 0x08, 0x00]),
  0x01: T(0x67, [0x03, 0x08, 0x03, 0x00, 0x02], CRTC_TEXT40, GC_TEXT, [0x0C, 0x00, 0x0F, 0x08, 0x00]),
  0x02: T(0x67, [0x03, 0x00, 0x03, 0x00, 0x02], CRTC_TEXT80, GC_TEXT, [0x0C, 0x00, 0x0F, 0x08, 0x00]),
  0x03: T(0x67, [0x03, 0x00, 0x03, 0x00, 0x02], CRTC_TEXT80, GC_TEXT, [0x0C, 0x00, 0x0F, 0x08, 0x00]),
  0x04: T(0x63, [0x03, 0x09, 0x03, 0x00, 0x02],
    [0x2D, 0x27, 0x28, 0x90, 0x2B, 0x80, 0xBF, 0x1F, 0x00, 0xC1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x14, 0x00, 0x96, 0xB9, 0xA2, 0xFF],
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x0F, 0x00, 0xFF], [0x01, 0x00, 0x03, 0x00, 0x00]),
  0x05: T(0x63, [0x03, 0x09, 0x03, 0x00, 0x02],
    [0x2D, 0x27, 0x28, 0x90, 0x2B, 0x80, 0xBF, 0x1F, 0x00, 0xC1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x14, 0x00, 0x96, 0xB9, 0xA2, 0xFF],
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x0F, 0x00, 0xFF], [0x01, 0x00, 0x03, 0x00, 0x00]),
  0x06: T(0x63, [0x03, 0x01, 0x01, 0x00, 0x06],
    [0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0xC1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x00, 0x96, 0xB9, 0xC2, 0xFF],
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x00, 0xFF], [0x01, 0x00, 0x01, 0x00, 0x00]),
  0x0D: T(0x63, [0x03, 0x09, 0x0F, 0x00, 0x06],
    [0x2D, 0x27, 0x28, 0x90, 0x2B, 0x80, 0xBF, 0x1F, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x14, 0x00, 0x96, 0xB9, 0xE3, 0xFF],
    GC_PLANAR, [0x01, 0x00, 0x0F, 0x00, 0x00]),
  0x0E: T(0x63, [0x03, 0x01, 0x0F, 0x00, 0x06],
    [0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x00, 0x96, 0xB9, 0xE3, 0xFF],
    GC_PLANAR, [0x01, 0x00, 0x0F, 0x00, 0x00]),
  0x10: T(0xA3, [0x03, 0x01, 0x0F, 0x00, 0x06],
    [0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x83, 0x85, 0x5D, 0x28, 0x0F, 0x63, 0xBA, 0xE3, 0xFF],
    GC_PLANAR, [0x01, 0x00, 0x0F, 0x00, 0x00]),
  0x12: T(0xE3, [0x03, 0x01, 0x0F, 0x00, 0x06],
    [0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEA, 0x8C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3, 0xFF],
    GC_PLANAR, [0x01, 0x00, 0x0F, 0x00, 0x00]),
  0x13: MODE13,
  0x62: MODE13,        // the ARM-PC's linear 640x480 mode ignores these; chain-4 keeps the window shut
};

// 4 bits -> one 0xFF byte lane per set bit (plane p = byte p of a 32-bit latch)
const EXP = new Int32Array(16);
for (let i = 0; i < 16; i++) EXP[i] = (i & 1 ? 0xFF : 0) | (i & 2 ? 0xFF00 : 0) | (i & 4 ? 0xFF0000 : 0) | (i & 8 ? 0xFF000000 : 0);

// display-state snapshot layout (Uint8Array): crtc 0-31, attr 32-52, seq 53-60,
// misc 61, pel mask 62, dac 64-831
export const SNAP_CRTC = 0, SNAP_ATTR = 32, SNAP_SEQ = 53, SNAP_MISC = 61, SNAP_PEL = 62, SNAP_DAC = 64, SNAP_SIZE = 832;
// timeline event kinds (code = kind << 12 | index)
export const EV_CRTC = 1, EV_ATTR = 2, EV_DAC = 3, EV_SEQ = 4, EV_PEL = 5;
const MAX_EVENTS = 1 << 18;

export class VGA {
  constructor(m) {
    this.m = m;
    this.vram = new Uint8Array(0x40000);
    this.vram32 = new Int32Array(this.vram.buffer);
    this.window = false;      // A0000h-AFFFFh is the planar MMIO window
    this.frame = -1;          // index of the frame being displayed (emulated time / FRAME_NS)
    this.frameStart = new Uint8Array(SNAP_SIZE);
    this.ev = new Int32Array(3 * 4096); this.nev = 0;
    this.doneStart = new Uint8Array(SNAP_SIZE); this.doneEv = new Int32Array(3 * 4096); this.doneN = 0; this.doneValid = false;
    this.reset();
  }
  reset() {
    this.mode = 0x03;
    this.crtc = new Uint8Array(32); this.crtcIndex = 0;
    this.dac = defaultDac(); this.dacW = 0; this.dacR = 0; this.dacC = 0; this.dacReadMode = 0; this.pelMask = 0xFF;
    this.attr = new Uint8Array(21); this.attr.set(DEFAULT_ATTR); this.attr[0x10] = 0x0C; this.attr[0x12] = 0x0F;
    this.attrIndex = 0; this.attrFlip = 0; this.attrPAS = 0x20;
    this.cgaSel = 0x30;
    this.seq = new Uint8Array(8); this.seqIndex = 0;
    this.gc = new Uint8Array(16); this.gcIndex = 0;
    this.misc = 0x67;
    this.latch = 0;
    this.loadMode(0x03);
    this.attr[0x13] = 0;
    this.vram.fill(0);
    this.nev = 0; this.doneN = 0; this.doneValid = false; this.frame = -1; this.nextFrameNs = 0; this.now = 0;
    if (this.window) this.m.setVgaWindow?.(false);
    this.window = false;
    this.dirty = true;
  }

  /** the IBM register set of a BIOS mode (see STD_MODES) */
  loadMode(mode) {
    const t = STD_MODES[mode];
    if (!t) return;
    this.misc = t.misc;
    for (let i = 0; i < 5; i++) this.seq[i] = t.seq[i];
    for (let i = 0; i < 25; i++) if (i < 0x0A || i > 0x0F || i === 0x0C || i === 0x0D) this.crtc[i] = t.crtc[i];
    for (let i = 0; i < 9; i++) this.gc[i] = t.gc[i];
    for (let i = 0; i < 5; i++) this.attr[0x10 + i] = t.attr[i];
    this.gcChanged();
  }

  // text cursor / start address helpers for the renderer
  get startAddr() { return (this.crtc[0x0C] << 8) | this.crtc[0x0D]; }
  get cursorPos() { return (this.crtc[0x0E] << 8) | this.crtc[0x0F]; }
  get blinkEnabled() { return (this.attr[0x10] & 0x08) !== 0; }
  get lineGraphics() { return (this.attr[0x10] & 0x04) !== 0; }

  /** What the display shows: 'lfb' (mode 62h), 'text', 'cga' (modes 4-6 with
   *  their CGA-compatible register set) or 'vga' (every other graphics
   *  state: rendered from the registers and the planes / chain-4 RAM). */
  displayKind() {
    if (this.mode === 0x62) return 'lfb';
    if (!(this.gc[6] & 1)) return 'text';
    if (this.mode >= 4 && this.mode <= 6 && ((this.gc[6] >>> 2) & 3) === 3) return 'cga';     // CGA-compatible: B8000h
    return 'vga';
  }
  get chain4() { return (this.seq[4] & 8) !== 0; }

  // next time the input status (3DAh: vretrace bit 3, blank bit 0) can change
  statusStableUntil(now) {
    const f = Math.floor(now / FRAME_NS) * FRAME_NS, ft = now - f;
    const nf = ft < VRETRACE_NS ? f + VRETRACE_NS : f + FRAME_NS;
    const l = Math.floor(now / LINE_NS) * LINE_NS, lt = now - l;
    const nl = lt < HACTIVE_NS ? l + HACTIVE_NS : l + LINE_NS;
    return Math.min(nf, nl);
  }
  read(port) {
    const now = this.m.timeNs();
    switch (port) {
      case 0x3E0: return this.mode;
      case 0x3DA: case 0x3BA: {
        this.attrFlip = 0;
        const t = now % FRAME_NS;
        const vr = t < VRETRACE_NS;
        const hb = (now % LINE_NS) >= HACTIVE_NS;
        return (vr ? 0x08 : 0) | (vr || hb ? 0x01 : 0);
      }
      case 0x3D4: return this.crtcIndex;
      case 0x3D5: return this.crtc[this.crtcIndex & 31];
      case 0x3C0: return this.attrIndex | this.attrPAS;
      case 0x3C1: return this.attr[this.attrIndex] ?? 0;
      case 0x3C2: return 0x10;
      case 0x3C3: return 1;
      case 0x3C4: return this.seqIndex;
      case 0x3C5: return this.seq[this.seqIndex & 7];
      case 0x3C6: return this.pelMask;
      case 0x3C7: return this.dacReadMode ? 0x00 : 0x03;
      case 0x3C8: return this.dacW;
      case 0x3C9: {
        const v = this.dac[this.dacR * 3 + this.dacC];
        if (++this.dacC === 3) { this.dacC = 0; this.dacR = (this.dacR + 1) & 0xFF; }
        return v;
      }
      case 0x3CA: return 0;
      case 0x3CC: return this.misc;
      case 0x3CE: return this.gcIndex;
      case 0x3CF: return this.gc[this.gcIndex & 15];
      case 0x3D9: return this.cgaSel;
    }
    return 0xFF;
  }
  write(port, v) {
    const now = this.m.timeNs();
    if (now >= this.nextFrameNs) this.roll(now);      // (before the write: it belongs to the new frame)
    this.now = now;
    switch (port) {
      case 0x3E0:
        this.mode = v; this.loadMode(v); this.dirty = true;
        this.log(EV_CRTC, 0x0C, this.crtc[0x0C]);       // (a mode set is a new picture)
        this.updateWindow(); this.m.modeChanged(v); return;
      case 0x3D4: this.crtcIndex = v & 31; return;
      case 0x3D5: {
        const i = this.crtcIndex & 31;
        if (i <= 7 && (this.crtc[0x11] & 0x80)) {        // registers 0-7 write-protected (bit 4 of 7 stays writable)
          if (i === 7) { this.crtc[7] = (this.crtc[7] & ~0x10) | (v & 0x10); this.log(EV_CRTC, 7, this.crtc[7]); }
          return;
        }
        this.crtc[i] = v; this.log(EV_CRTC, i, v); return;
      }
      case 0x3C0:
        if (!this.attrFlip) { this.attrIndex = v & 0x1F; this.attrPAS = v & 0x20; }
        else if (this.attrIndex < 21) { this.attr[this.attrIndex] = v; this.log(EV_ATTR, this.attrIndex, v); }
        this.attrFlip ^= 1; return;
      case 0x3C2: this.misc = v; return;
      case 0x3C4: this.seqIndex = v; return;
      case 0x3C5: {
        const i = this.seqIndex & 7;
        this.seq[i] = v;
        if (i === 1) this.log(EV_SEQ, 1, v);
        else if (i === 2 || i === 4) { this.gcChanged(); if (i === 4) this.updateWindow(); }
        return;
      }
      case 0x3C6: this.pelMask = v; this.log(EV_PEL, 0, v); return;
      case 0x3C7: this.dacR = v; this.dacC = 0; this.dacReadMode = 1; return;
      case 0x3C8: this.dacW = v; this.dacC = 0; this.dacReadMode = 0; return;
      case 0x3C9: {
        const k = this.dacW * 3 + this.dacC;
        this.dac[k] = v & 0x3F; this.log(EV_DAC, k, v & 0x3F);
        if (++this.dacC === 3) { this.dacC = 0; this.dacW = (this.dacW + 1) & 0xFF; }
        return;
      }
      case 0x3CE: this.gcIndex = v; return;
      case 0x3CF: {
        const i = this.gcIndex & 15;
        this.gc[i] = v; this.gcChanged();
        if (i === 6) this.updateWindow();
        return;
      }
      case 0x3D9: this.cgaSel = v; return;
    }
  }

  // --------------------------------------------------------- planar memory
  gcChanged() {
    const g = this.gc;
    this.sr32 = EXP[g[0] & 15]; this.esr32 = EXP[g[1] & 15];
    this.cc32 = EXP[g[2] & 15]; this.dc32 = EXP[g[7] & 15];
    this.rot = g[3] & 7; this.fn = (g[3] >>> 3) & 3;
    this.rmap = (g[4] & 3) << 3; this.rmode = (g[5] >>> 3) & 1; this.wmode = g[5] & 3;
    this.bm = g[8]; this.bm32 = Math.imul(g[8], 0x01010101);
    this.mm = this.seq[2] & 15; this.mm32 = EXP[this.mm];
  }
  /** should A0000h-AFFFFh be the planar window? */
  wantWindow() {
    if (this.mode === 0x62 || this.m.hgc) return false;
    const map = (this.gc[6] >>> 2) & 3;
    if (map > 1) return false;
    if (this.gc[6] & 1) return !(this.seq[4] & 8) && this.displayKind() === 'vga';
    return (this.seq[4] & 4) !== 0;                   // text mode, odd/even off: plane access (fonts)
  }
  updateWindow() {
    const on = this.wantWindow();
    if (on === this.window) return;
    const m8 = this.m.cpu.m8, vr = this.vram, font = this.m.font;
    if (on) {
      // chain-4 RAM -> planes (the picture a Mode X program starts from); plane 2 <- the font
      if (this.gc[6] & 1) for (let i = 0; i < 0x10000; i++) vr[((i & 0xFFFC) << 2) | (i & 3)] = m8[0xA0000 + i];
      if (font) for (let o = 0; o < 8192; o++) vr[(o << 2) | 2] = font[o];
      this.window = true;
      this.m.setVgaWindow(true);
    } else {
      this.window = false;
      this.m.setVgaWindow(false);
      // planes -> chain-4 RAM when a 256-colour chained mode takes over
      if ((this.gc[6] & 1) && (this.seq[4] & 8)) for (let i = 0; i < 0x10000; i++) m8[0xA0000 + i] = vr[((i & 0xFFFC) << 2) | (i & 3)];
    }
    this.dirty = true;
  }
  /** CPU read of window offset o (0-FFFFh): loads the latches */
  winRead(o) {
    const l = this.latch = this.vram32[o & 0xFFFF];
    if (!this.rmode) return (l >>> this.rmap) & 0xFF;
    const x = (l ^ this.cc32) & this.dc32;
    return ~(x | (x >>> 8) | (x >>> 16) | (x >>> 24)) & 0xFF;
  }
  /** CPU write of v to window offset o */
  winWrite(o, v) {
    o &= 0xFFFF;
    const latch = this.latch;
    let d;
    switch (this.wmode) {
      case 0: {
        if (this.rot) v = ((v >>> this.rot) | (v << (8 - this.rot))) & 0xFF;
        d = Math.imul(v, 0x01010101);
        d = (d & ~this.esr32) | (this.sr32 & this.esr32);
        d = this.alu(d, latch);
        d = (d & this.bm32) | (latch & ~this.bm32);
        break;
      }
      case 1: d = latch; break;
      case 2: d = this.alu(EXP[v & 15], latch); d = (d & this.bm32) | (latch & ~this.bm32); break;
      default: {
        if (this.rot) v = ((v >>> this.rot) | (v << (8 - this.rot))) & 0xFF;
        const m = Math.imul(v & this.bm, 0x01010101);
        d = this.alu(this.sr32, latch);
        d = (d & m) | (latch & ~m);
      }
    }
    const mm = this.mm32;
    this.vram32[o] = (this.vram32[o] & ~mm) | (d & mm);
    if ((this.mm & 4) && o < 8192 && !(this.gc[6] & 1)) { this.m.font[o] = (d >>> 16) & 0xFF; this.dirty = true; }
  }
  alu(d, latch) {
    switch (this.fn) {
      case 1: return d & latch;
      case 2: return d | latch;
      case 3: return d ^ latch;
    }
    return d;
  }
  /** plane p's byte at offset o as the display sees it (chain-4: RAM) */
  planeByte(o, p) {
    o &= 0xFFFF;
    if (!this.window && (this.gc[6] & 1) && (this.seq[4] & 8) && !(o & 3)) return this.m.cpu.m8[0xA0000 + o + p];
    return this.vram[(o << 2) | p];
  }

  // --------------------------------------------------------- frame timeline
  snapshot(dst) {
    dst.set(this.crtc, SNAP_CRTC); dst.set(this.attr, SNAP_ATTR); dst.set(this.seq, SNAP_SEQ);
    dst[SNAP_MISC] = this.misc; dst[SNAP_PEL] = this.pelMask; dst.set(this.dac, SNAP_DAC);
  }
  /** displayed scan lines (vertical display end + 1) */
  displayLines(c = this.crtc, base = 0) {
    const v = (c[base + 0x12] | ((c[base + 7] & 2) << 7) | ((c[base + 7] & 0x40) << 3)) + 1;
    return v < 2 ? 2 : v > 1024 ? 1024 : v;
  }
  /** start a new frame's log when emulated time has passed the frame boundary */
  roll(now) {
    const k = Math.floor(now / FRAME_NS);
    if (k === this.frame) return;
    if (k === this.frame + 1 && this.frame >= 0) {
      const t = this.doneStart; this.doneStart = this.frameStart; this.frameStart = t;
      const e = this.doneEv; this.doneEv = this.ev; this.ev = e;
      this.doneN = this.nev; this.doneValid = true;
    } else {             // frames without any register write in between: the current state
      this.snapshot(this.doneStart); this.doneN = 0; this.doneValid = true;
    }
    this.nev = 0;
    this.snapshot(this.frameStart);
    this.frame = k;
    this.nextFrameNs = (k + 1) * FRAME_NS;
  }
  log(kind, index, v) {
    const now = this.now;
    const t = now - this.frame * FRAME_NS - VRETRACE_NS;
    const sl = t < 0 ? -1 : Math.floor(t * this.displayLines() / ACTIVE_NS);
    if (this.nev * 3 + 3 > this.ev.length) {
      if (this.ev.length >= 3 * MAX_EVENTS) return;
      const e = new Int32Array(this.ev.length * 2); e.set(this.ev); this.ev = e;
    }
    const k = this.nev * 3;
    this.ev[k] = sl; this.ev[k + 1] = (kind << 12) | index; this.ev[k + 2] = v;
    this.nev++;
  }
}
