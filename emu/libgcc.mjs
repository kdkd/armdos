// Native execution of libgcc's ARMv5 integer division routines.
//
// libgcc's __udivsi3 / __divsi3 (lib1funcs.S, ARM state, CLZ version) divide
// with a computed jump into a 32-step unrolled shift-subtract table. Compiled
// C that divides by a variable calls them constantly (Quake: ~1 call per 60
// instructions), and the computed jump costs the JIT several block dispatches
// per call. When the JIT compiles a block at an address whose code is exactly
// one of these routines (checked word for word, apart from the tail branch to
// __aeabi_idiv0), it executes it natively instead.
//
// The native version is *architecturally exact*: it produces the same final
// r0-r3, r12 and N/Z/C/V as executing the routine, and reports the exact
// number of instructions the routine executes, so emulated timing is unchanged
// (checked against the interpreter by emu/tests/cpu/divtest.mjs). Division by
// zero (which leaves via __aeabi_idiv0) is not handled natively.

// ---------------------------------------------------------------- templates
const steps = (cmpBase, adc, subBase) => {
  const w = [];
  for (let s = 31; s >= 0; s--) w.push((cmpBase | (s << 7)) >>> 0, adc >>> 0, (subBase | (s << 7)) >>> 0);
  return w;
};
// __udivsi3 / __aeabi_uidiv, without the final "b __aeabi_idiv0"
export const UDIV_WORDS = [0xe2512001, 0x012fff1e, 0x3a000074, 0xe1500001, 0x9a00006b, 0xe1110002, 0x0a00006c,
  0xe16f3f10, 0xe16f2f11, 0xe0423003, 0xe273301f, 0x10833083, 0xe3a02000, 0x108ff103, 0xe1a00000,
  ...steps(0xe1500001, 0xe0a22002, 0x20400001),
  0xe1a00002, 0xe12fff1e, 0x03a00001, 0x13a00000, 0xe12fff1e, 0xe16f2f11, 0xe262201f, 0xe1a00230, 0xe12fff1e,
  0xe3500000, 0x13e00000];
// __divsi3 / __aeabi_idiv (from its first instruction), without the final branch
export const SDIV_WORDS = [0xe3510000, 0x0a000081, 0xe020c001, 0x42611000, 0xe2512001, 0x0a000070, 0xe1b03000,
  0x42603000, 0xe1530001, 0x9a00006f, 0xe1110002, 0x0a000071, 0xe16f2f13, 0xe16f0f11, 0xe0402002, 0xe272201f,
  0x10822082, 0xe3a00000, 0x108ff102, 0xe1a00000,
  ...steps(0xe1530001, 0xe0a00000, 0x20433001),
  0xe35c0000, 0x42600000, 0xe12fff1e, 0xe13c0000, 0x42600000, 0xe12fff1e, 0x33a00000, 0x01a00fcc, 0x03800001,
  0xe12fff1e, 0xe16f2f11, 0xe262201f, 0xe35c0000, 0xe1a00233, 0x42600000, 0xe12fff1e, 0xe3500000, 0xc3e00102,
  0xb3a00102];

/**
 * Which routine starts at `addr`: 1 = __udivsi3, 2 = __divsi3, 3 =
 * .divsi3_skip_div0_test (__divsi3 + 8, called by __aeabi_idivmod), 0 = none.
 * fetch(a) returns the word at a.
 */
export function divRoutineAt(fetch, addr) {
  const match = (tpl, base) => { for (let k = 0; k < tpl.length; k++) if ((fetch(base + 4 * k) >>> 0) !== tpl[k]) return false; return (fetch(base + 4 * tpl.length) & 0x0F000000) === 0x0A000000; };
  const w = fetch(addr) >>> 0;
  if (w === UDIV_WORDS[0] && match(UDIV_WORDS, addr)) return 1;
  if (w === SDIV_WORDS[0] && match(SDIV_WORDS, addr)) return 2;
  if (w === SDIV_WORDS[2] && match(SDIV_WORDS, addr - 8)) return 3;
  return 0;
}

// ---------------------------------------------------------------- execution
// Results: DV[0..4] = r0, r1, r2, r3, r12; DV[5] = nv, DV[6] = zv (the CPU's
// N/Z representation: N iff nv < 0, Z iff zv === 0), DV[7] = C, DV[8] = V,
// DV[9] = instructions executed (including the final bx lr).
export const DV = new Int32Array(10);

function setSubFlags(a, b) {                   // CMP a, b / SUBS
  const res = (a - b) | 0;
  DV[5] = res; DV[6] = res; DV[7] = (a >>> 0) >= (b >>> 0) ? 1 : 0; DV[8] = ((a ^ b) & (a ^ res)) >>> 31;
}
// the unrolled shift-subtract steps for shift sh..0: returns quotient; the
// remainder is left in REM, flags of the last CMP in DV
let REM = 0;
function steps32(num, den, sh) {
  let q = 0, r = num;
  for (let s = sh; s >= 0; s--) {
    const t = (den << s) | 0;
    const cf = (r >>> 0) >= (t >>> 0) ? 1 : 0;
    if (s === 0) setSubFlags(r, t);
    q = ((q << 1) + cf) | 0;
    if (cf) r = (r - t) | 0;
  }
  REM = r;
  return q;
}

/** __udivsi3(n, d), d != 0; r2, r3, r12 in: r3/r12 unchanged where not written. */
export function udiv(n, d, r3in, r12in) {
  // subs r2, r1, #1
  let r2 = (d - 1) | 0;
  setSubFlags(d, 1);
  DV[1] = d; DV[4] = r12in;
  if (d === 1) { DV[0] = n; DV[2] = r2; DV[3] = r3in; DV[9] = 2; return; }       // bxeq lr
  // (d == 0: bcc -> __aeabi_idiv0, not handled here)
  setSubFlags(n, d);                                                              // cmp r0, r1
  if ((n >>> 0) <= (d >>> 0)) {                                                   // bls
    DV[0] = n === d ? 1 : 0; DV[2] = r2; DV[3] = r3in; DV[9] = 8; return;
  }
  if ((d & r2) === 0) {                                                           // tst r1, r2; beq: power of two
    DV[5] = 0; DV[6] = 0;                                                         // N, Z of (d & (d-1)) = 0; C, V from the cmp
    const sh = 31 - Math.clz32(d);
    DV[0] = n >>> sh; DV[2] = sh; DV[3] = r3in; DV[9] = 11; return;
  }
  const sh = Math.clz32(d) - Math.clz32(n);                                       // clz; clz; sub; rsbs
  let r3 = 31 - sh;
  if (r3 !== 0) r3 *= 3;                                                          // addne
  const q = steps32(n, d, sh);
  DV[0] = q; DV[2] = q; DV[3] = r3;                                               // mov r0, r2
  DV[9] = 14 + (sh === 31 ? 1 : 0) + 3 * (sh + 1) + 2;
}

/**
 * __divsi3(a, b) (kind 2) or .divsi3_skip_div0_test (kind 3, entered with the
 * flags of a preceding "cmp r1, #0" - nIn = incoming nv), b != 0.
 */
export function sdiv(a, b, kind, nIn, r2in, r3in) {
  let count = 0, n = nIn;
  if (kind === 2) { count = 2; n = b; }                                           // cmp r1, #0; beq (not taken)
  const ip = a ^ b;                                                               // eor ip, r0, r1
  const B = n < 0 ? (-b | 0) : b;                                                 // rsbmi r1, r1, #0
  DV[1] = B; DV[4] = ip;
  const r2 = (B - 1) | 0;                                                         // subs r2, r1, #1
  setSubFlags(B, 1);
  count += 4;
  if (B === 1) {                                                                  // beq -> teq ip, r0; rsbmi; bx
    const t = ip ^ a;
    DV[5] = t; DV[6] = t;                                                         // teq: N, Z; C, V from subs
    DV[0] = t < 0 ? (-a | 0) : a; DV[2] = r2; DV[3] = r3in; DV[9] = count + 3; return;
  }
  const A = a < 0 ? (-a | 0) : a;                                                 // movs r3, r0; rsbmi r3, r0, #0
  setSubFlags(A, B);                                                              // cmp r3, r1
  count += 4;
  if ((A >>> 0) <= (B >>> 0)) {                                                   // bls -> movcc/asreq/orreq/bx
    DV[0] = (A >>> 0) < (B >>> 0) ? 0 : ((ip >> 31) | 1); DV[2] = r2; DV[3] = A; DV[9] = count + 4; return;
  }
  count += 2;                                                                     // tst r1, r2; beq
  if ((B & r2) === 0) {                                                           // power of two
    const sh = 31 - Math.clz32(B);
    const q = A >>> sh;
    DV[0] = ip < 0 ? (-q | 0) : q; DV[2] = sh; DV[3] = A;
    setSubFlags(ip, 0);                                                           // cmp ip, #0
    DV[9] = count + 6; return;
  }
  const sh = Math.clz32(B) - Math.clz32(A);
  let r2b = 31 - sh;
  if (r2b !== 0) r2b *= 3;
  const q = steps32(A, B, sh);
  DV[0] = ip < 0 ? (-q | 0) : q; DV[2] = r2b; DV[3] = REM;
  setSubFlags(ip, 0);                                                             // cmp ip, #0 (rsbmi, bx follow)
  DV[9] = count + 7 + (sh === 31 ? 1 : 0) + 3 * (sh + 1) + 3;
}
