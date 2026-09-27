// Programmatic test API for booting ARM-DOS headlessly (node; also usable in a
// browser if you pass Uint8Arrays instead of paths).
//
//   import { boot } from '../emu/testkit.mjs';
//   const pc = await boot({ rom: 'build/rom.bin', hd: 'build/hd.img' });
//   pc.waitText('C:\\>');
//   pc.type('DIR\r');
//   pc.waitIdle();
//   console.log(pc.screen());
//
// All waits run the machine in emulated time (deterministic, no wall clock)
// and return true/false instead of throwing.

import { Machine } from './machine.mjs';
import { renderScreen, screenLines, screenText } from './render.mjs';
import { disasmArm, disasmThumb } from './disasm.mjs';

async function load(x) {
  if (x == null) return null;
  if (x instanceof Uint8Array) return x;
  if (x instanceof ArrayBuffer) return new Uint8Array(x);
  const fs = await import('node:fs');
  return new Uint8Array(fs.readFileSync(x));
}

export class TestPC {
  constructor(machine) {
    this.m = machine;
    this.serial = '';       // COM1 output
    this.debug = '';        // port E9 output
    this.faults = [];
  }
  get machine() { return this.m; }
  get cpu() { return this.m.cpu; }
  get exitCode() { return this.m.exitCode; }
  get timeMs() { return this.m.timeMs(); }

  run(ms) { this.m.runFor(ms); return this; }
  type(text) { this.m.typeText(text); return this; }
  key(code) { this.m.keyDown(code); this.m.keyUp(code); return this; }

  /** run until pred() is true, checking every stepMs of emulated time */
  until(pred, { timeoutMs = 30000, stepMs = 10 } = {}) {
    const end = this.m.timeMs() + timeoutMs;
    while (this.m.timeMs() < end) {
      if (pred()) return true;
      if (this.m.stopped) return !!pred();
      this.m.runFor(stepMs);
    }
    return !!pred();
  }
  screen() { return screenText(this.m); }
  lines() { return screenLines(this.m); }
  hasText(t) { return screenLines(this.m).join('\n').includes(t); }
  waitText(t, opts) { return this.until(() => this.hasText(t), opts); }
  waitSerial(t, opts) { return this.until(() => this.serial.includes(t) || this.debug.includes(t), opts); }
  waitExit(opts) { return this.until(() => this.m.stopped, opts); }
  /**
   * Idle = keys all delivered and consumed, and the CPU spent >= 90% of the
   * last quietMs of emulated time halted in WFI.
   */
  waitIdle({ timeoutMs = 30000, quietMs = 300, stepMs = 10 } = {}) {
    const hist = [];
    const end = this.m.timeMs() + timeoutMs;
    while (this.m.timeMs() < end && !this.m.stopped) {
      const h0 = this.m.haltedNs, t0 = this.m.timeNs();
      this.m.runFor(stepMs);
      hist.push([this.m.timeNs() - t0, this.m.haltedNs - h0]);
      let span = 0, halted = 0;
      for (let k = hist.length - 1; k >= 0 && span < quietMs * 1e6; k--) { span += hist[k][0]; halted += hist[k][1]; }
      if (span >= quietMs * 1e6 && halted >= 0.9 * span && this.m.typingDone() && this.m.biosKeyBufferEmpty()) return true;
    }
    return false;
  }
  render() { return renderScreen(this.m); }
  async png(path) {
    const img = renderScreen(this.m);
    const bytes = await encodePNG(img.width, img.height, img.data);
    if (path) { const fs = await import('node:fs'); fs.writeFileSync(path, bytes); }
    return bytes;
  }
  /** last n traced instructions (needs trace enabled) as text lines */
  traceLines(n = 64) { return formatTrace(this.m.cpu, n); }
  regsText() { return formatRegs(this.m.cpu); }
}

/**
 * Boot a machine. Options: rom, hd, fd (paths or Uint8Array), fdWriteProtected,
 * mhz, jit (default true), rtcBaseMs (default: fixed 2026-01-01 12:00 local, for
 * determinism), trace (number of instructions to keep; disables the JIT),
 * any other Machine option.
 */
export async function boot(opts = {}) {
  const rom = await load(opts.rom);
  const hd = await load(opts.hd);
  const fd = await load(opts.fd);
  let pc;
  const traceN = opts.trace | 0;
  const m = new Machine({
    ...opts, rom, hd, fd,
    jit: traceN ? false : opts.jit !== false,
    rtcBaseMs: opts.rtcBaseMs ?? new Date(2026, 0, 1, 12, 0, 0).getTime(),
    onSerial: (b) => { pc.serial += String.fromCharCode(b); opts.onSerial && opts.onSerial(b); },
    onDebug: (b) => { pc.debug += String.fromCharCode(b); opts.onDebug && opts.onDebug(b); },
  });
  pc = new TestPC(m);
  if (opts.preloadFont) {         // true = emu/fonts/vga8x16.bin, or a Uint8Array / path
    const f = opts.preloadFont === true ? await load(new URL('./fonts/vga8x16.bin', import.meta.url).pathname) : await load(opts.preloadFont);
    m.loadFont(f, f.length / 256);
  }
  if (traceN) { m.cpu.trace = new Int32Array(traceN * 2); }
  m.cpu.onFault = (kind, pcAddr, addr) => { pc.faults.push({ kind, pc: pcAddr >>> 0, addr: addr >>> 0, timeMs: m.timeMs() }); opts.onFault && opts.onFault(kind, pcAddr, addr); };
  return pc;
}

const hex8 = (x) => (x >>> 0).toString(16).padStart(8, '0');
export function formatTrace(cpu, n = 64) {
  const tr = cpu.trace; if (!tr) return [];
  const total = tr.length / 2, out = [];
  for (let k = Math.max(0, total - n); k < total; k++) {
    const p = (cpu.tracePos + k * 2) % tr.length;
    const a = tr[p] >>> 0, w = tr[p + 1];
    if (a === 0 && w === 0) continue;
    if (a & 1) { const pcA = a & ~1; const d = disasmThumb(w & 0xFFFF, pcA, cpu.fetchHalf(pcA + 2)); out.push(`${hex8(pcA)}  ${(w & 0xFFFF).toString(16).padStart(4, '0')}      ${d.text}`); }
    else out.push(`${hex8(a)}  ${hex8(w)}  ${disasmArm(w, a)}`);
  }
  return out;
}
export function formatRegs(cpu) {
  const r = cpu.regs();
  const names = ['r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7', 'r8', 'r9', 'r10', 'r11', 'r12', 'sp', 'lr', 'pc'];
  r[15] = cpu.pc >>> 0;
  let s = '';
  for (let i = 0; i < 16; i++) s += `${names[i].padStart(3)}=${hex8(r[i])}${i % 4 === 3 ? '\n' : '  '}`;
  const c = cpu.getCPSR() >>> 0;
  s += `cpsr=${hex8(c)} [${c & 0x80000000 ? 'N' : 'n'}${c & 0x40000000 ? 'Z' : 'z'}${c & 0x20000000 ? 'C' : 'c'}${c & 0x10000000 ? 'V' : 'v'} ${c & 0x80 ? 'I' : 'i'}${c & 0x40 ? 'F' : 'f'}${c & 0x20 ? 'T' : 't'} mode ${(c & 0x1F).toString(16)}]  spsr=${hex8(cpu.spsr)}`;
  const v = cpu.vfpState();
  if (v.enabled) {
    s += `\nfpscr=${hex8(v.fpscr)} fpexc=${hex8(v.fpexc)}`;
    for (let k = 0; k < 16; k++) s += `${k % 4 === 0 ? '\n' : '  '}d${String(k).padEnd(2)}=${hex8(v.sBits[2 * k + 1])}${hex8(v.sBits[2 * k]).slice(0)} (${+v.d[k].toPrecision(8)})`;
  }
  return s;
}

// ---- PNG (node: zlib; elsewhere: stored deflate blocks)
const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
function crc32(buf, start, end) { let c = 0xFFFFFFFF; for (let i = start; i < end; i++) c = CRC[(c ^ buf[i]) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; }
function storedZlib(raw) {
  const nblk = Math.ceil(raw.length / 65535) || 1;
  const out = new Uint8Array(2 + raw.length + nblk * 5 + 4);
  out[0] = 0x78; out[1] = 0x01;
  let o = 2;
  for (let b = 0; b < nblk; b++) {
    const s = b * 65535, len = Math.min(65535, raw.length - s);
    out[o++] = b === nblk - 1 ? 1 : 0; out[o++] = len & 0xFF; out[o++] = len >> 8; out[o++] = ~len & 0xFF; out[o++] = (~len >> 8) & 0xFF;
    out.set(raw.subarray(s, s + len), o); o += len;
  }
  let a = 1, bb = 0; for (let i = 0; i < raw.length; i++) { a = (a + raw[i]) % 65521; bb = (bb + a) % 65521; }
  out[o++] = bb >> 8; out[o++] = bb & 0xFF; out[o++] = a >> 8; out[o++] = a & 0xFF;
  return out;
}
export async function encodePNG(w, h, rgba) {
  const raw = new Uint8Array((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 4 + 1)] = 0; raw.set(rgba.subarray(y * w * 4, (y + 1) * w * 4), y * (w * 4 + 1) + 1); }
  let z;
  try { const zlib = await import('node:zlib'); z = new Uint8Array(zlib.deflateSync(raw)); } catch { z = storedZlib(raw); }
  const chunks = [];
  const chunk = (type, data) => {
    const c = new Uint8Array(12 + data.length), dv = new DataView(c.buffer);
    dv.setUint32(0, data.length); for (let i = 0; i < 4; i++) c[4 + i] = type.charCodeAt(i);
    c.set(data, 8); dv.setUint32(8 + data.length, crc32(c, 4, 8 + data.length)); chunks.push(c);
  };
  const ihdr = new Uint8Array(13), dv = new DataView(ihdr.buffer);
  dv.setUint32(0, w); dv.setUint32(4, h); ihdr[8] = 8; ihdr[9] = 6;
  chunk('IHDR', ihdr); chunk('IDAT', z); chunk('IEND', new Uint8Array(0));
  const sig = [137, 80, 78, 71, 13, 10, 26, 10];
  const total = 8 + chunks.reduce((s, c) => s + c.length, 0);
  const out = new Uint8Array(total); out.set(sig, 0);
  let o = 8; for (const c of chunks) { out.set(c, o); o += c.length; }
  return out;
}
