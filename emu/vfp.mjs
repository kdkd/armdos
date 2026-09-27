// ARM-DOS machine: VFPv2 (the ARM926EJ-S's VFP9-S coprocessor, CP10/CP11).
//
// Architectural state lives on the CPU object (so the JIT can reach it cheaply):
//   c.vbuf  ArrayBuffer of 36 words; c.F32 / c.F64 / c.FW = Float32 / Float64 /
//           Int32 views. S0-S31 = F32[0..31] = FW[0..31]; D0-D15 = F64[0..15]
//           (Dn = S(2n+1):S(2n), little-endian host). Words 32-35 (S32-S35 =
//           D16-D17) are hidden temporaries for multiply-accumulate.
//   c.fpscr, c.fpexc, c.fpinst, c.fpinst2
//   c.fpOff  = 1 when FPEXC.EN is clear (instructions UNDEF)
//   c.fpSlow = 1 when arithmetic cannot take the JIT's native fast path
//              (disabled, FZ, RMode != nearest, LEN != 0, or exact-flags mode)
//
// Arithmetic is exact IEEE 754 with the ARM rules: default NaN 0x7FC00000 /
// 0x7FF8000000000000 (DN), NaN propagation (first signalling NaN, else first
// quiet NaN, quietened), all four rounding modes, flush-to-zero (FZ: denormal
// inputs -> signed zero + IDC, tiny results -> signed zero + UFC), tininess
// detected before rounding, cumulative flags IOC/DZC/OFC. IXC and non-FZ UFC
// are only tracked when c.vfpExactFlags is set (it forces every operation
// through the exact path; see README). Exception trap enables are RAZ/WI (no
// support code), as QEMU does. Short vectors (FPSCR LEN/STRIDE) are
// implemented here (the JIT falls back to this code when LEN != 0).
//
// Single-precision results are computed in double and rounded once with
// Math.fround: for + - * / sqrt of float operands this is correctly rounded
// (53 >= 2*24+2). Directed rounding and inexact detection use the exact sign
// of (true result - double result), computed with BigInt.

export const FPSID_VALUE = 0x41011090;       // VFP9-S r0 (implementer ARM, VFPv2, part 0x10, variant 9)
export const FPSCR_MASK = 0xF7F7009F | 0;    // writable FPSCR bits (as QEMU's arm926)
export const FPEXC_EN = 0x40000000;
const IOC = 1, DZC = 2, OFC = 4, UFC = 8, IXC = 0x10, IDC = 0x80;
const FZ = 1 << 24, DN = 1 << 25;
const T0 = 16, T1 = 17;                      // temp doubles (singles 32..35)

export function vfpInit(c) {
  c.vbuf = new ArrayBuffer(36 * 4);
  c.F32 = new Float32Array(c.vbuf);
  c.F64 = new Float64Array(c.vbuf);
  c.FW = new Int32Array(c.vbuf);
  c.vfpExactFlags = false;
  vfpReset(c);
}
export function vfpReset(c) {
  c.FW.fill(0);
  c.fpscr = 0; c.fpexc = 0; c.fpinst = 0xEE000A00 | 0; c.fpinst2 = 0;
  vfpUpdate(c);
}
export function vfpUpdate(c) {
  c.fpOff = (c.fpexc & FPEXC_EN) ? 0 : 1;
  c.fpSlow = (c.fpOff || (c.fpscr & 0x01C70000) || c.vfpExactFlags) ? 1 : 0;
}
/** Inspector snapshot. */
export function vfpState(c) {
  const s = [], d = [], sw = [];
  for (let k = 0; k < 32; k++) { s.push(c.F32[k]); sw.push(c.FW[k] >>> 0); }
  for (let k = 0; k < 16; k++) d.push(c.F64[k]);
  return { enabled: !c.fpOff, fpsid: FPSID_VALUE >>> 0, fpscr: c.fpscr >>> 0, fpexc: c.fpexc >>> 0, s, d, sBits: sw };
}

// ------------------------------------------------------------ bit helpers
const cf64 = new Float64Array(1), cw64 = new Int32Array(cf64.buffer);
const cf32 = new Float32Array(1), cw32 = new Int32Array(cf32.buffer);
const MIN_D = 2.2250738585072014e-308, MAX_D = 1.7976931348623157e308;
const MIN_F = 1.1754943508222875e-38, MAX_F = 3.4028234663852886e38;

function nextUpD(x) {                // toward +inf
  if (x !== x || x === Infinity) return x;
  if (x === 0) return 5e-324;
  cf64[0] = x; let lo = cw64[0] >>> 0, hi = cw64[1];
  if (x > 0) { lo = (lo + 1) >>> 0; if (lo === 0) hi++; } else { if (lo === 0) hi--; lo = (lo - 1) >>> 0; }
  cw64[0] = lo; cw64[1] = hi; return cf64[0];
}
function nextDownD(x) { return -nextUpD(-x); }
function nextUpF(x) {
  if (x !== x || x === Infinity) return x;
  if (x === 0) return 1.401298464324817e-45;
  cf32[0] = x; cw32[0] += x > 0 ? 1 : -1; return cf32[0];
}
function nextDownF(x) { return -nextUpF(-x); }

// exact decomposition x = m * 2^e (m BigInt, signed), x finite
function dec(x) {
  cf64[0] = x;
  const hi = cw64[1], lo = cw64[0] >>> 0, ex = (hi >>> 20) & 0x7FF;
  let m = (BigInt(hi & 0xFFFFF) << 32n) | BigInt(lo), e;
  if (ex === 0) e = -1074; else { m |= 1n << 52n; e = ex - 1075; }
  return [hi < 0 ? -m : m, e];
}
// sign of sum of terms m*2^e
function sgnSum(terms) {
  let e0 = Infinity; for (const t of terms) if (t[0] !== 0n && t[1] < e0) e0 = t[1];
  if (e0 === Infinity) return 0;
  let s = 0n; for (const [m, e] of terms) if (m !== 0n) s += m << BigInt(e - e0);
  return s > 0n ? 1 : s < 0n ? -1 : 0;
}
const ADD = 0, MUL = 2, DIV = 3, SQRT = 4;
// sign(exact(a op b) - r), all finite
function errSign(op, a, b, r) {
  const A = dec(a), R = dec(r);
  switch (op) {
    case ADD: return sgnSum([A, dec(b), [-R[0], R[1]]]);
    case MUL: { const B = dec(b); return sgnSum([[A[0] * B[0], A[1] + B[1]], [-R[0], R[1]]]); }
    case DIV: { const B = dec(b); return sgnSum([A, [-(R[0] * B[0]), R[1] + B[1]]]) * (b < 0 ? -1 : 1); }
    default: return sgnSum([A, [-(R[0] * R[0]), 2 * R[1]]]);   // sqrt: a - r*r
  }
}

// ------------------------------------------------------------ NaN handling
function isNaNIdx(c, sp, x) {
  if (sp) return (c.FW[x] & 0x7FFFFFFF) > 0x7F800000;
  const hi = c.FW[2 * x + 1] & 0x7FFFFFFF;
  return hi > 0x7FF00000 || (hi === 0x7FF00000 && c.FW[2 * x] !== 0);
}
function isSNaNIdx(c, sp, x) { return isNaNIdx(c, sp, x) && !(c.FW[sp ? x : 2 * x + 1] & (sp ? 0x400000 : 0x80000)); }
function defaultNaN(c, sp, d) { if (sp) c.FW[d] = 0x7FC00000; else { c.FW[2 * d] = 0; c.FW[2 * d + 1] = 0x7FF80000; } }
// x, y register indexes (y < 0: unary). Caller knows at least one is NaN.
function propagateNaN(c, sp, d, x, y) {
  const xs = isSNaNIdx(c, sp, x), ys = y >= 0 && isSNaNIdx(c, sp, y);
  if (xs || ys) c.fpscr |= IOC;
  if (c.fpscr & DN) { defaultNaN(c, sp, d); return; }
  const p = xs ? x : ys ? y : isNaNIdx(c, sp, x) ? x : y;
  const W = c.FW;
  if (sp) W[d] = W[p] | 0x400000;
  else { const lo = W[2 * p], hi = W[2 * p + 1] | 0x80000; W[2 * d] = lo; W[2 * d + 1] = hi; }
}

// ------------------------------------------------------------ rounding / results
function rmode(c) { return (c.fpscr >>> 22) & 3; }
function flushIn(c, v, sp) {
  if (v !== 0 && Math.abs(v) < (sp ? MIN_F : MIN_D)) { c.fpscr |= IDC; return v < 0 ? -0 : 0; }
  return v;
}
function overflowValue(neg, rm, sp) {
  const max = sp ? MAX_F : MAX_D;
  switch (rm) {
    case 0: return neg ? -Infinity : Infinity;
    case 1: return neg ? -max : Infinity;
    case 2: return neg ? -Infinity : max;
    default: return neg ? -max : max;
  }
}
// r = round-to-nearest result, s = sign(exact - r). Writes the result.
function finish(c, sp, d, r, s) {
  const rm = rmode(c), exactF = c.vfpExactFlags, fz = c.fpscr & FZ;
  const min = sp ? MIN_F : MIN_D;
  if (r === Infinity || r === -Infinity) {           // overflow in round-to-nearest
    c.fpscr |= OFC | IXC;
    const v = overflowValue(r < 0, rm, sp);
    if (sp) c.F32[d] = v; else c.F64[d] = v;
    return;
  }
  const ar = Math.abs(r);
  const tiny = (ar < min && (ar !== 0 || s !== 0)) || (ar === min && s * r < 0);
  if (fz && tiny) { c.fpscr |= UFC; if (sp) c.F32[d] = (r < 0 || Object.is(r, -0) || (r === 0 && s < 0)) ? -0 : 0; else c.F64[d] = (r < 0 || Object.is(r, -0) || (r === 0 && s < 0)) ? -0 : 0; return; }
  if (s !== 0) {
    if (rm !== 0) {
      const up = sp ? nextUpF : nextUpD, down = sp ? nextDownF : nextDownD;
      if (rm === 1) { if (s > 0) r = up(r); }
      else if (rm === 2) { if (s < 0) r = down(r); }
      else if (r > 0 && s < 0) r = down(r);
      else if (r < 0 && s > 0) r = up(r);
      if (r === Infinity || r === -Infinity) c.fpscr |= OFC | IXC;
    }
    if (exactF) { c.fpscr |= IXC; if (tiny) c.fpscr |= UFC; }
  }
  if (sp) c.F32[d] = r; else c.F64[d] = r;
}
// single result from an exact-or-RN double rd with s = sign(exact - rd)
function finishSingle(c, d, rd, s) {
  const f = Math.fround(rd);
  if (f === Infinity || f === -Infinity) { finish(c, true, d, f, 0); return; }
  finish(c, true, d, f, f !== rd ? (rd > f ? 1 : -1) : s);
}

// binary/unary arithmetic on register indexes (may be temporaries)
function isDenormIdx(c, sp, x) {
  if (sp) { const b = c.FW[x] & 0x7FFFFFFF; return b !== 0 && b < 0x00800000; }
  const hi = c.FW[2 * x + 1] & 0x7FFFFFFF; return hi < 0x00100000 && (hi | c.FW[2 * x]) !== 0;
}
function arith(c, op, sp, d, x, y, negB = false) {
  if (c.fpSlow === 0) {                        // round-to-nearest, no FZ/exact flags: native math
    const V = sp ? c.F32 : c.F64;
    const a = V[x], b = negB ? -V[y] : V[y];
    let r = op === ADD ? a + b : op === MUL ? a * b : op === DIV ? a / b : Math.sqrt(a);
    if (sp) r = Math.fround(r);
    if (r - r === 0) { V[d] = r; return; }     // finite: done (non-finite: exact path below)
  }
  if (isNaNIdx(c, sp, x) || (op !== SQRT && isNaNIdx(c, sp, y))) {
    // FZ: denormal inputs are flushed (IDC) before NaN handling, as QEMU/softfloat do
    if ((c.fpscr & FZ) && (isDenormIdx(c, sp, x) || (op !== SQRT && isDenormIdx(c, sp, y)))) c.fpscr |= IDC;
    propagateNaN(c, sp, d, x, op === SQRT ? -1 : y); return;
  }
  const V = sp ? c.F32 : c.F64;
  let a = V[x], b = op === SQRT ? 0 : V[y];
  if (negB) b = -b;
  if (c.fpscr & FZ) { a = flushIn(c, a, sp); b = flushIn(c, b, sp); }
  const fa = isFinite(a), fb = isFinite(b);
  let r;
  switch (op) {
    case ADD:
      if (!fa && !fb && a !== b) { c.fpscr |= IOC; defaultNaN(c, sp, d); return; }
      r = a + b;
      if (!fa || !fb) { V[d] = r; return; }
      if (r === 0 && rmode(c) === 2 && !(Object.is(a, 0) && Object.is(b, 0))) { V[d] = -0; return; }
      break;
    case MUL:
      if ((a === 0 && !fb) || (b === 0 && !fa)) { c.fpscr |= IOC; defaultNaN(c, sp, d); return; }
      r = a * b;
      if (!fa || !fb) { V[d] = r; return; }
      break;
    case DIV:
      if ((a === 0 && b === 0) || (!fa && !fb)) { c.fpscr |= IOC; defaultNaN(c, sp, d); return; }
      r = a / b;
      if (!fa || !fb) { V[d] = r; return; }
      if (b === 0) { c.fpscr |= DZC; V[d] = r; return; }
      break;
    default:   // SQRT
      if (a < 0) { c.fpscr |= IOC; defaultNaN(c, sp, d); return; }
      r = Math.sqrt(a);
      if (!fa) { V[d] = r; return; }
  }
  const need = c.fpSlow && ((c.fpscr & 0x01C00000) || c.vfpExactFlags);   // directed rounding / FZ / exact flags
  if (sp) {
    const s = need && isFinite(r) ? errSign(op, a, b, r) : 0;
    finishSingle(c, d, r, s);
  } else {
    const s = need && isFinite(r) ? errSign(op, a, b, r) : 0;
    finish(c, false, d, r, s);
  }
}

// ------------------------------------------------------------ conversions
function roundInt(v, rm) {
  switch (rm) {
    case 0: { let t = Math.round(v); if (t - v === 0.5 && (t % 2 !== 0)) t -= 1; return t; }
    case 1: return Math.ceil(v);
    case 2: return Math.floor(v);
    default: return Math.trunc(v);
  }
}
function toInt(c, sp, d, m, signed, rz) {
  const W = c.FW;
  if (isNaNIdx(c, sp, m)) { c.fpscr |= IOC; W[d] = 0; return; }
  let v = sp ? c.F32[m] : c.F64[m];
  if (c.fpscr & FZ) v = flushIn(c, v, sp);
  const t = roundInt(v, rz ? 3 : rmode(c));
  if (signed) {
    if (t > 2147483647) { c.fpscr |= IOC; W[d] = 0x7FFFFFFF; return; }
    if (t < -2147483648) { c.fpscr |= IOC; W[d] = 0x80000000 | 0; return; }
  } else {
    if (t > 4294967295) { c.fpscr |= IOC; W[d] = -1; return; }
    if (t < 0) { c.fpscr |= IOC; W[d] = 0; return; }
  }
  if (t !== v && c.vfpExactFlags) c.fpscr |= IXC;
  W[d] = t;
}
function fromInt(c, sp, d, m, signed) {
  const v = signed ? c.FW[m] : c.FW[m] >>> 0;
  if (sp) finishSingle(c, d, v, 0); else c.F64[d] = v;
}
function cvtDS(c, d, m) {           // single Sm -> double Dd
  const W = c.FW;
  if (isNaNIdx(c, true, m)) {
    if (isSNaNIdx(c, true, m)) c.fpscr |= IOC;
    if (c.fpscr & DN) { defaultNaN(c, false, d); return; }
    const b = W[m];
    W[2 * d] = (b & 7) << 29; W[2 * d + 1] = (b & 0x80000000) | 0x7FF80000 | ((b >>> 3) & 0x7FFFF);
    return;
  }
  let v = c.F32[m];
  if (c.fpscr & FZ) v = flushIn(c, v, true);
  c.F64[d] = v;
}
function cvtSD(c, d, m) {           // double Dm -> single Sd
  const W = c.FW;
  if (isNaNIdx(c, false, m)) {
    if (isSNaNIdx(c, false, m)) c.fpscr |= IOC;
    if (c.fpscr & DN) { defaultNaN(c, true, d); return; }
    const hi = W[2 * m + 1], lo = W[2 * m] >>> 0;
    W[d] = (hi & 0x80000000) | 0x7FC00000 | ((hi & 0x7FFFF) << 3) | (lo >>> 29);
    return;
  }
  let v = c.F64[m];
  if (c.fpscr & FZ) v = flushIn(c, v, false);
  if (!isFinite(v)) { c.F32[d] = v; return; }
  finishSingle(c, d, v, 0);
}
function compare(c, sp, d, m, withZero, e) {
  let nzcv;
  const aN = isNaNIdx(c, sp, d), bN = !withZero && isNaNIdx(c, sp, m);
  if (aN || bN) {
    if ((c.fpscr & FZ) && (isDenormIdx(c, sp, d) || (!withZero && isDenormIdx(c, sp, m)))) c.fpscr |= IDC;
    if (e || isSNaNIdx(c, sp, d) || (!withZero && isSNaNIdx(c, sp, m))) c.fpscr |= IOC;
    nzcv = 0x3;
  } else {
    const V = sp ? c.F32 : c.F64;
    let a = V[d], b = withZero ? 0 : V[m];
    if (c.fpscr & FZ) { a = flushIn(c, a, sp); b = flushIn(c, b, sp); }
    nzcv = a === b ? 0x6 : a < b ? 0x8 : 0x2;
  }
  c.fpscr = (c.fpscr & 0x0FFFFFFF) | (nzcv << 28);
}

// ------------------------------------------------------------ data processing
// one scalar element: opc (pqrs) 0-8, or 15 with ext
function dpElem(c, sp, opc, d, n, m) {
  const W = c.FW;
  switch (opc) {
    case 4: arith(c, MUL, sp, d, n, m); return;
    case 5: arith(c, MUL, sp, d, n, m); W[sp ? d : 2 * d + 1] ^= 0x80000000; return;   // FNMUL
    case 6: arith(c, ADD, sp, d, n, m); return;
    case 7: arith(c, ADD, sp, d, n, m, true); return;                                 // FSUB = n + (-m)
    case 8: arith(c, DIV, sp, d, n, m); return;
    default: {    // 0 FMAC, 1 FNMAC, 2 FMSC, 3 FNMSC: product then add (not fused)
      const tp = sp ? 2 * T0 : T0, td = sp ? 2 * T1 : T1;
      arith(c, MUL, sp, tp, n, m);
      if (opc & 1) W[sp ? tp : 2 * tp + 1] ^= 0x80000000;
      if (sp) W[td] = W[d]; else { W[2 * td] = W[2 * d]; W[2 * td + 1] = W[2 * d + 1]; }
      if (opc & 2) W[sp ? td : 2 * td + 1] ^= 0x80000000;
      arith(c, ADD, sp, d, td, tp);
    }
  }
}
function unaryElem(c, sp, ext, d, m) {
  const W = c.FW;
  if (ext === 3) { arith(c, SQRT, sp, d, m, -1); return; }
  if (sp) { const v = W[m]; W[d] = ext === 0 ? v : ext === 1 ? v & 0x7FFFFFFF : v ^ 0x80000000; }
  else { const lo = W[2 * m], hi = W[2 * m + 1]; W[2 * d] = lo; W[2 * d + 1] = ext === 0 ? hi : ext === 1 ? hi & 0x7FFFFFFF : hi ^ 0x80000000; }
}

function fastS(F, opc, d, n, m) {
  const f = Math.fround;
  switch (opc) {
    case 0: return f(F[d] + f(F[n] * F[m]));
    case 1: return f(F[d] - f(F[n] * F[m]));
    case 2: return f(-F[d] + f(F[n] * F[m]));
    case 3: return f(-F[d] - f(F[n] * F[m]));
    case 4: return f(F[n] * F[m]);
    case 5: return -f(F[n] * F[m]);
    case 6: return f(F[n] + F[m]);
    case 7: return f(F[n] - F[m]);
    default: return f(F[n] / F[m]);
  }
}
function fastD(F, opc, d, n, m) {
  switch (opc) {
    case 0: return F[d] + F[n] * F[m];
    case 1: return F[d] - F[n] * F[m];
    case 2: return -F[d] + F[n] * F[m];
    case 3: return -F[d] - F[n] * F[m];
    case 4: return F[n] * F[m];
    case 5: return -(F[n] * F[m]);
    case 6: return F[n] + F[m];
    case 7: return F[n] - F[m];
    default: return F[n] / F[m];
  }
}
function dataProc(c, i, pc) {
  const sp = ((i >>> 8) & 1) === 0;
  const opc = ((i >>> 20) & 8) | ((i >>> 19) & 4) | ((i >>> 19) & 2) | ((i >>> 6) & 1);
  const Fd = (i >>> 12) & 15, Dbit = (i >>> 22) & 1, Fn = (i >>> 16) & 15, Nbit = (i >>> 7) & 1, Fm = i & 15, Mbit = (i >>> 5) & 1;
  const sD = (Fd << 1) | Dbit, sN = (Fn << 1) | Nbit, sM = (Fm << 1) | Mbit;
  if (opc <= 8 && c.fpSlow === 0) {           // fast path: scalar, round-to-nearest, finite result
    if (sp) { const r = fastS(c.F32, opc, sD, sN, sM); if (r - r === 0) { c.F32[sD] = r; return (pc + 4) | 0; } }
    else if (!(Dbit | Nbit | Mbit)) { const r = fastD(c.F64, opc, Fd, Fn, Fm); if (r - r === 0) { c.F64[Fd] = r; return (pc + 4) | 0; } }
  }
  if (opc !== 15) {
    if (opc > 8) return c.undefinedInsn(pc);
    if (!sp && (Dbit | Nbit | Mbit)) return c.undefinedInsn(pc);
    const d = sp ? sD : Fd, n = sp ? sN : Fn, m = sp ? sM : Fm;
    if ((c.fpscr & 0x70000) === 0) dpElem(c, sp, opc, d, n, m);
    else vectorLoop(c, sp, d, n, m, (dd, nn, mm) => dpElem(c, sp, opc, dd, nn, mm));
    return (pc + 4) | 0;
  }
  const ext = sN;   // Fn:N selects the operation
  switch (ext) {
    case 0: case 1: case 2: case 3: {
      if (!sp && (Dbit | Mbit)) return c.undefinedInsn(pc);
      const d = sp ? sD : Fd, m = sp ? sM : Fm;
      if ((c.fpscr & 0x70000) === 0) unaryElem(c, sp, ext, d, m);
      else vectorLoop(c, sp, d, -1, m, (dd, nn, mm) => unaryElem(c, sp, ext, dd, mm));
      break;
    }
    case 8: case 9: case 10: case 11:
      if (!sp && (Dbit | Mbit)) return c.undefinedInsn(pc);
      if ((ext & 2) && (Fm | Mbit)) return c.undefinedInsn(pc);      // FCMPZ: Fm must be 0
      compare(c, sp, sp ? sD : Fd, sp ? sM : Fm, (ext & 2) !== 0, (ext & 1) !== 0);
      break;
    case 15:
      if (sp) { if (Dbit) return c.undefinedInsn(pc); cvtDS(c, Fd, sM); }
      else { if (Mbit) return c.undefinedInsn(pc); cvtSD(c, sD, Fm); }
      break;
    case 16: case 17:
      if (sp) fromInt(c, true, sD, sM, ext === 17);
      else { if (Dbit) return c.undefinedInsn(pc); fromInt(c, false, Fd, sM, ext === 17); }
      break;
    case 24: case 25: case 26: case 27:
      if (sp) toInt(c, true, sD, sM, ext >= 26, (ext & 1) !== 0);
      else { if (Mbit) return c.undefinedInsn(pc); toInt(c, false, sD, Fm, ext >= 26, (ext & 1) !== 0); }
      break;
    default:
      return c.undefinedInsn(pc);
  }
  return (pc + 4) | 0;
}
// FPSCR LEN/STRIDE short vectors (scalar when LEN = 0 or Fd is in bank 0)
function vectorLoop(c, sp, d, n, m, fn) {
  const L = (c.fpscr >>> 16) & 7;
  const bank = sp ? 0x18 : 0x0C, off = sp ? 7 : 3;
  if (L === 0 || (d & bank) === 0) { fn(d, n, m); return; }
  const stride = ((c.fpscr >>> 20) & 3) === 3 ? 2 : 1;
  const mScalar = (m & bank) === 0;
  for (let k = 0; k <= L; k++) {
    fn(d, n, m);
    d = (d & bank) | ((d + stride) & off);
    if (n >= 0) n = (n & bank) | ((n + stride) & off);
    if (!mScalar) m = (m & bank) | ((m + stride) & off);
  }
}

// ------------------------------------------------------------ loads / stores / transfers
function loadStore(c, i, pc) {
  const r = c.r, W = c.FW;
  const P = (i >>> 24) & 1, U = (i >>> 23) & 1, Dbit = (i >>> 22) & 1, Wb = (i >>> 21) & 1, L = (i >>> 20) & 1;
  const rn = (i >>> 16) & 15, Fd = (i >>> 12) & 15, sp = ((i >>> 8) & 1) === 0, imm8 = i & 0xFF;
  const base = rn === 15 ? (r[15] & ~3) : r[rn];
  if (P && !Wb) {                                 // FLDS/FSTS/FLDD/FSTD
    const a = (U ? base + imm8 * 4 : base - imm8 * 4) | 0;
    if (!sp && Dbit) return c.undefinedInsn(pc);
    const w0 = sp ? (Fd << 1) | Dbit : 2 * Fd;
    if (L) {
      const lo = c.ld32a(a, pc);
      if (sp) W[w0] = lo; else { const hi = c.ld32a((a + 4) | 0, pc); W[w0] = lo; W[w0 + 1] = hi; }
    } else {
      c.st32(a, W[w0], pc);
      if (!sp) c.st32((a + 4) | 0, W[w0 + 1], pc);
    }
    return (pc + 4) | 0;
  }
  // multiples: IA (P=0,U=1), DB with writeback (P=1,U=0,W=1)
  if (P === U || (P && !Wb)) return c.undefinedInsn(pc);
  if (rn === 15 && Wb) return c.undefinedInsn(pc);
  let w0, nw;
  if (sp) { w0 = (Fd << 1) | Dbit; nw = imm8; if (nw === 0 || w0 + nw > 32) return c.undefinedInsn(pc); }
  else {
    if (Dbit) return c.undefinedInsn(pc);
    const nd = imm8 >>> 1;
    w0 = 2 * Fd; nw = 2 * nd;
    if (nd === 0 || Fd + nd > 16) return c.undefinedInsn(pc);
  }
  let a = (P ? base - imm8 * 4 : base) | 0;
  const wbv = (U ? base + imm8 * 4 : base - imm8 * 4) | 0;
  if (L) {
    const tmp = c._vfpTmp || (c._vfpTmp = new Int32Array(32));
    for (let k = 0; k < nw; k++) { tmp[k] = c.ld32a(a, pc); a = (a + 4) | 0; }
    if (Wb) r[rn] = wbv;
    for (let k = 0; k < nw; k++) W[w0 + k] = tmp[k];
  } else {
    for (let k = 0; k < nw; k++) { c.st32(a, W[w0 + k], pc); a = (a + 4) | 0; }
    if (Wb) r[rn] = wbv;
  }
  return (pc + 4) | 0;
}

function sysReg(c, i, pc) {
  const L = (i >>> 20) & 1, reg = (i >>> 16) & 15, rd = (i >>> 12) & 15;
  const priv = c.mode !== 0x10;
  if (i & 0xEF) return c.undefinedInsn(pc);            // bits 7:5 and 3:0 SBZ
  if (L) {                                             // FMRX
    let v;
    switch (reg) {
      case 0: v = FPSID_VALUE; break;
      case 1:
        if (c.fpOff) return c.undefinedInsn(pc);
        if (rd === 15) {                                                // FMSTAT
          const f = c.fpscr; c.nv = f < 0 ? -1 : 0; c.zv = (f & 0x40000000) ? 0 : 1; c.c = (f >>> 29) & 1; c.v = (f >>> 28) & 1;
          return (pc + 4) | 0;
        }
        v = c.fpscr; break;
      case 8: if (!priv) return c.undefinedInsn(pc); v = c.fpexc; break;
      case 9: if (!priv) return c.undefinedInsn(pc); v = c.fpinst; break;
      case 10: if (!priv) return c.undefinedInsn(pc); v = c.fpinst2; break;
      default: return c.undefinedInsn(pc);
    }
    if (rd === 15) return c.undefinedInsn(pc);
    c.r[rd] = v;
    return (pc + 4) | 0;
  }
  const v = rd === 15 ? (pc + 8) | 0 : c.r[rd];        // FMXR
  switch (reg) {
    case 0: if (!priv) return c.undefinedInsn(pc); break;
    case 1: if (c.fpOff) return c.undefinedInsn(pc); c.fpscr = v & FPSCR_MASK; break;
    case 8: if (!priv) return c.undefinedInsn(pc); c.fpexc = v & FPEXC_EN; break;
    case 9: if (!priv) return c.undefinedInsn(pc); c.fpinst = v; break;
    case 10: if (!priv) return c.undefinedInsn(pc); c.fpinst2 = v; break;
    default: return c.undefinedInsn(pc);
  }
  vfpUpdate(c);
  c.brk = 1;                                           // compiled code caches the mode
  return (pc + 4) | 0;
}

/**
 * Execute a CP10/CP11 instruction (condition already passed). r[15] = pc+8.
 * Returns the next pc (pc+4, or the undefined-instruction vector).
 */
export function vfpExec(c, i, pc) {
  const top = (i >>> 24) & 15;
  const W = c.FW, r = c.r;
  if (top === 0xE) {
    if ((i & 0x10) === 0) return c.fpOff ? c.undefinedInsn(pc) : dataProc(c, i, pc);
    const opc1 = (i >>> 21) & 7, L = (i >>> 20) & 1, rd = (i >>> 12) & 15, cp10 = ((i >>> 8) & 1) === 0;
    if (cp10 && opc1 === 7) return sysReg(c, i, pc);
    if (c.fpOff || (i & 0x6F)) return c.undefinedInsn(pc);
    let w;
    if (cp10) { if (opc1 !== 0) return c.undefinedInsn(pc); w = (((i >>> 16) & 15) << 1) | ((i >>> 7) & 1); }
    else { if (opc1 > 1 || (i & 0x80)) return c.undefinedInsn(pc); w = 2 * ((i >>> 16) & 15) + opc1; }
    if (rd === 15) return c.undefinedInsn(pc);
    if (L) r[rd] = W[w]; else W[w] = r[rd];
    return (pc + 4) | 0;
  }
  if (c.fpOff) return c.undefinedInsn(pc);
  if ((i & 0x0FE00000) === 0x0C400000) {              // MCRR/MRRC: FMDRR/FMRRD/FMSRR/FMRRS
    const L = (i >>> 20) & 1, rn = (i >>> 16) & 15, rd = (i >>> 12) & 15, cp10 = ((i >>> 8) & 1) === 0;
    if ((i & 0xD0) !== 0x10 || rd === 15 || rn === 15) return c.undefinedInsn(pc);
    let w;
    if (cp10) { w = ((i & 15) << 1) | ((i >>> 5) & 1); if (w === 31) return c.undefinedInsn(pc); }
    else { if (i & 0x20) return c.undefinedInsn(pc); w = 2 * (i & 15); }
    if (L) { if (rd === rn) return c.undefinedInsn(pc); r[rd] = W[w]; r[rn] = W[w + 1]; }
    else { W[w] = r[rd]; W[w + 1] = r[rn]; }
    return (pc + 4) | 0;
  }
  if (top === 0xC || top === 0xD) return loadStore(c, i, pc);
  return c.undefinedInsn(pc);
}
