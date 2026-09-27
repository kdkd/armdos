// disasm.mjs -- ARMv5TE (ARM926EJ-S) disassembler, ARM and Thumb (v5T) state.
// Pure ES module, no dependencies.
//
// Output follows `arm-none-eabi-objdump -d` (binutils 2.4x, -marmv5tej / force-thumb):
//   * lowercase mnemonic + condition, TAB, operands; comments as "\t@ ...".
//   * registers r0-r9, sl, fp, ip, sp, lr, pc (objdump's default names).
//   * immediates decimal "#N"; objdump's "\t@ 0x..." hex comment is added when the
//     value is > 32 or < -16.  Non-canonical rotated immediates print as "#imm, rot".
//   * branch / literal targets are bare hex addresses "0x8010" (what objdump prints
//     when there are no symbols; the "<label>" part is never emitted).
//   * PC-relative loads carry "\t@ 0x<addr>" (ARM) / "\t@ (0x<addr>)" (Thumb).
//   * objdump's "<UNPREDICTABLE>", "<illegal shifter operand>" and
//     "p-variant is OBSOLETE" annotations are reproduced.
//   * undefined encodings print ".word\t0x%08x" (ARM) / ".short\t0x%04x" (Thumb)
//     instead of objdump's "@ <UNDEFINED> instruction: ..." line.
//   * 16-bit Thumb branches use objdump's ".n" suffix (b.n, beq.n).
//   * VFP (ARM state, cp10/cp11): the VFPv2 instruction set of the ARM/AT's VFP9-S
//     (ARMv5 ARM ARM, part C), in objdump's UAL text -- "v*" mnemonics with the
//     condition before the type suffix (vaddne.f32, vcvtr.s32.f64, vcmpe.f64 d0, #0.0,
//     vldr d0, [pc, #8]\t@ 0x<addr>, vldmia r0!, {d0-d1}, vpush/vpop, vmov r0, r1, d0,
//     vmov.32 d0[1], r0, vmrs APSR_nzcv, fpscr, "vmrs r0, fpinst\t@ Impl def").
//     FLDMX/FSTMX (odd imm8) print as fldmiax/fldmdbx/fstmiax/fstmdbx "...\t@ Deprecated".
//     UNPREDICTABLE forms (empty/overlong register lists, pc written back or as a
//     transfer register, s31 pairs) keep objdump's text plus "@ <UNPREDICTABLE>".
//     Everything else in cp10/cp11 is UNDEFINED on this machine and prints .word:
//     d16-d31 (a D/N/M bit set on a double), VFPv3/v4/FP16/ARMv8 (vmov #imm,
//     fixed-point vcvt, vcvtb/t, vfma, vrint, vsel, ...), NEON scalar transfers,
//     MVFRn, other system registers, generic cdp/ldc/stc/mcr/mrc/mcrr/mrrc forms and
//     every cond=1111 form -- where objdump -marmv5tej (which enables every FPU
//     extension) would print those later readings.
// Known differences from objdump (besides the VFPv3+ readings above):
//   * NEON data-processing / element load-store space (cond 15, 0xf2-0xf4) prints .word.
//   * encodings objdump gives a v7/v8 meaning even in v5 mode: hlt (e1000070) and
//     banked-register mrs/msr print as the v5 tst/msr reading.
//   * Thumb: v6+ 16-bit encodings (cbz, it/hints, cps, rev, sxt/uxt, bxns) print as
//     .short; a BL/BLX prefix not followed by a suffix halfword prints as .short
//     (objdump would decode a Thumb-2 32-bit instruction there).
// Checked against objdump by emu/tests/disasm/test-disasm.mjs.
//
// API:
//   disasmArm(word, addr)            -> string
//   disasmThumb(hw, addr, next)      -> { text, size }   (next = following halfword)
//   disasm(read32, read16, addr, thumb) -> { text, size, word }

const RN = ['r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7', 'r8', 'r9', 'sl', 'fp', 'ip', 'sp', 'lr', 'pc'];
const CC = ['eq', 'ne', 'cs', 'cc', 'mi', 'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le', '', ''];
const SH = ['lsl', 'lsr', 'asr', 'ror'];
const DP = ['and', 'eor', 'sub', 'rsb', 'add', 'adc', 'sbc', 'rsc', 'tst', 'teq', 'cmp', 'cmn', 'orr', 'mov', 'bic', 'mvn'];
const hex = (v) => '0x' + (v >>> 0).toString(16);
const hex8 = (v) => '0x' + (v >>> 0).toString(16).padStart(8, '0');

// Per-instruction state (module-level; disassembly is synchronous).
let vic, unp, uReg, UReg;
const reset = () => { vic = null; unp = false; uReg = -1; UReg = -1; };
// Register with objdump modifiers: P = unpredictable if pc, u/U = unpredictable if
// equal to the previous u/U register.
function reg(n, P, u, U) {
  if (P && n === 15) unp = true;
  if (u) { if (uReg === n) unp = true; uReg = n; }
  if (U) { if (UReg === n) unp = true; UReg = n; }
  return RN[n];
}
function tail(s) {
  if (vic !== null && (vic > 32 || vic < -16)) s += '\t@ ' + hex(vic);
  if (unp) s += '\t@ <UNPREDICTABLE>';
  return s;
}
const undefA = (w) => '.word\t' + hex8(w);

// arm_decode_shift
function shift(w, printShift) {
  let s = RN[w & 15];
  if (w & 0xff0) {
    if (!(w & 0x10)) {
      let amt = (w >>> 7) & 31; const t = (w >>> 5) & 3;
      if (amt === 0) { if (t === 3) return s + ', rrx'; amt = 32; }
      s += printShift ? ', ' + SH[t] + ' #' + amt : ', #' + amt;
    } else if (w & 0x80) s += '\t@ <illegal shifter operand>';
    else s += (printShift ? ', ' + SH[(w >>> 5) & 3] + ' ' : ', ') + RN[(w >>> 8) & 15];
  }
  return s;
}
// %o : shifter operand
function op2(w) {
  if (w & 0x02000000) {
    const rot = (w >>> 7) & 30, imm = w & 0xff;
    const a = ((imm >>> rot) | (imm << ((32 - rot) & 31))) >>> 0;
    let i = 0;
    for (; i < 32; i += 2) if ((((a << i) | (a >>> ((32 - i) & 31))) >>> 0) <= 0xff) break;
    vic = a;
    return i !== rot ? '#' + imm + ', ' + rot : '#' + (a | 0);
  }
  return shift(w, true);
}
// %a : addressing mode 2 (ldr/str/pld)
function addr2(w, pc) {
  const P = w & 0x01000000, U = w & 0x00800000, W = w & 0x00200000, neg = U ? '' : '-';
  if ((w & 0x020f0000) === 0x000f0000) {
    let off = w & 0xfff, s = '[pc';
    if (P) {
      if (W || !U || off) s += ', #' + neg + off;
      s += ']' + (W ? '!' : '');
      off = pc + 8 + (U ? off : -off);
    } else { s += '], #' + neg + off; off = pc + 8; }
    return s + '\t@ ' + hex(off);
  }
  let s = '[' + RN[(w >>> 16) & 15], off = 0;
  if (P) {
    if (!(w & 0x02000000)) { off = w & 0xfff; if (W || !U || off) s += ', #' + neg + off; }
    else s += ', ' + neg + shift(w, true);
    s += ']' + (W ? '!' : '');
  } else if (!(w & 0x02000000)) { off = w & 0xfff; s += '], #' + neg + off; }
  else s += '], ' + neg + shift(w, true);
  vic = U ? off : -off;
  return s;
}
// %s : addressing mode 3 (ldrh/strh/ldrsb/ldrsh/ldrd/strd)
function addr3(w, pc) {
  const P = w & 0x01000000, U = w & 0x00800000, W = w & 0x00200000, I = w & 0x00400000, neg = U ? '' : '-';
  let off = ((w >>> 4) & 0xf0) | (w & 15);
  if ((w & 0x004f0000) === 0x004f0000) {
    if (P) {
      const s = (off || !U) ? '[pc, #' + neg + off + ']' : '[pc]';
      return s + '\t@ ' + hex(pc + 8 + (U ? off : -off));
    }
    unp = true;
    return '[pc], #' + neg + off;
  }
  let s = '[' + RN[(w >>> 16) & 15];
  if (P) {
    if (I) { if (W || !U || off) s += ', #' + neg + off; vic = U ? off : -off; }
    else { s += ', ' + neg + RN[w & 15]; if (W && (w & 15) === ((w >>> 12) & 15)) unp = true; }
    return s + ']' + (W ? '!' : '');
  }
  if (I) { s += '], #' + neg + off; vic = U ? off : -off; }
  else { s += '], ' + neg + RN[w & 15]; if ((w & 15) === ((w >>> 12) & 15)) unp = true; }
  if (W || (!I && (w & 15) === 15)) unp = true;
  return s;
}
function regList(l) {
  const r = [];
  for (let i = 0; i < 16; i++) if (l & (1 << i)) r.push(RN[i]);
  return '{' + r.join(', ') + '}';
}
// coprocessor %A
function addrCp(w, pc) {
  const cp = (w >>> 8) & 15, rn = (w >>> 16) & 15, P = w & 0x01000000, U = w & 0x00800000, W = w & 0x00200000;
  let off = w & 0xff, s = '[' + RN[rn];
  if (P || W) { off *= cp === 9 ? 2 : 4; if (!U) off = -off; if (rn !== 15) vic = off; }
  if (P) s += off ? ', #' + off + ']' + (W ? '!' : '') : (!U ? ', #-0]' : ']');
  else {
    s += ']';
    if (W) { if (off) s += ', #' + off; else if (!U) s += ', #-0'; }
    else { s += ', {' + (!U && !off ? '-' : '') + off + '}'; vic = off; }
  }
  if (rn === 15 && (P || W)) s += '\t@ ' + hex(off + pc + 8 - (pc & 3));
  return s;
}
// ---- VFP (cp10/cp11): the VFPv2 instruction set of the VFP9-S (ARM ARM, ARMv5
// edition, part C), in objdump's UAL syntax.  Table rows are [value, mask, format]
// in binutils arm-dis.c coprocessor_opcodes order and syntax (first match wins),
// cut down to VFPv2: double-precision forms need D/N/M = 0 (there is no d16-d31),
// and nothing from VFPv3/v4/FP16/ARMv8 or the NEON scalar transfers is decoded -
// on this machine those encodings are UNDEFINED, so they print as .word, like
// every other cp10/cp11 encoding VFPv2 does not define (generic cdp/ldc/mcr/mcrr
// forms and all cond=1111 forms included).
// Format codes (subset of arm-dis.c):
//   %c cond   %A coprocessor address (addrCp)
//   %yN / %zN single / double register: 0 = Vm, 1 = Vd, 2 = Vn, 3 = {list},
//             4 = "Sm, Sm+1" pair   %B D-register list for vldm/vstm/vpush/vpop
//   %<fields><k> with fields "a-b" or "a" joined by ',' (first field = low bits),
//   k: r core reg, R core reg (UNPREDICTABLE if pc), U core reg (UNPREDICTABLE if pc
//      or the same as the previous U), d decimal, D d-reg,
//      'X print X if 1, `X print X if 0,
//      ?.. pick char ((1<<width) - value) from the following 1<<width chars.
// UNPREDICTABLE VFPv2 forms keep objdump's text and get "@ <UNPREDICTABLE>": an
// empty or too long register list, pc as a written-back base, pc or a repeated
// register as a core transfer register, s31 as the first of a pair.
const VFP = [
  // Register load/store
  [0x0d2d0b00, 0x0fff0f01, 'vpush%c\t%B'],
  [0x0d200b00, 0x0ff00f01, 'vstmdb%c\t%16-19r!, %B'],
  [0x0d300b00, 0x0ff00f01, 'vldmdb%c\t%16-19r!, %B'],
  [0x0c800b00, 0x0fd00f01, "vstmia%c\t%16-19r%21'!, %B"],
  [0x0cbd0b00, 0x0fff0f01, 'vpop%c\t%B'],
  [0x0c900b00, 0x0fd00f01, "vldmia%c\t%16-19r%21'!, %B"],
  [0x0d000b00, 0x0f700f00, 'vstr%c\t%12-15D, %A'],
  [0x0d100b00, 0x0f700f00, 'vldr%c\t%12-15D, %A'],
  [0x0d2d0a00, 0x0fbf0f00, 'vpush%c\t%y3'],
  [0x0d200a00, 0x0fb00f00, 'vstmdb%c\t%16-19r!, %y3'],
  [0x0d300a00, 0x0fb00f00, 'vldmdb%c\t%16-19r!, %y3'],
  [0x0c800a00, 0x0f900f00, "vstmia%c\t%16-19r%21'!, %y3"],
  [0x0cbd0a00, 0x0fbf0f00, 'vpop%c\t%y3'],
  [0x0c900a00, 0x0f900f00, "vldmia%c\t%16-19r%21'!, %y3"],
  [0x0d000a00, 0x0f300f00, 'vstr%c\t%y1, %A'],
  [0x0d100a00, 0x0f300f00, 'vldr%c\t%y1, %A'],
  [0x0d200b01, 0x0ff00f01, 'fstmdbx%c\t%16-19r!, %z3\t@ Deprecated'],
  [0x0d300b01, 0x0ff00f01, 'fldmdbx%c\t%16-19r!, %z3\t@ Deprecated'],
  [0x0c800b01, 0x0fd00f01, "fstmiax%c\t%16-19r%21'!, %z3\t@ Deprecated"],
  [0x0c900b01, 0x0fd00f01, "fldmiax%c\t%16-19r%21'!, %z3\t@ Deprecated"],
  // Core <-> VFP transfers (FMDRR FMRRD FMSRR FMRRS, FMDLR FMDHR FMRDL FMRDH)
  [0x0c400b10, 0x0ff00ff0, 'vmov%c\t%0-3D, %12-15R, %16-19R'],
  [0x0c500b10, 0x0ff00ff0, 'vmov%c\t%12-15U, %16-19U, %0-3D'],
  [0x0c400a10, 0x0ff00fd0, 'vmov%c\t%y4, %12-15R, %16-19R'],
  [0x0c500a10, 0x0ff00fd0, 'vmov%c\t%12-15U, %16-19U, %y4'],
  [0x0e000b10, 0x0fd00ff0, 'vmov%c.32\t%16-19D[%21d], %12-15R'],
  [0x0e100b10, 0x0fd00ff0, 'vmov%c.32\t%12-15R, %16-19D[%21d]'],
  // System registers (FMXR FMRX FMSTAT): FPSID, FPSCR, FPEXC, the VFP9-S's FPINST/FPINST2
  [0x0ee00a10, 0x0fff0fff, 'vmsr%c\tfpsid, %12-15R'],
  [0x0ee10a10, 0x0fff0fff, 'vmsr%c\tfpscr, %12-15R'],
  [0x0ee80a10, 0x0fff0fff, 'vmsr%c\tfpexc, %12-15R'],
  [0x0ee90a10, 0x0fff0fff, 'vmsr%c\tfpinst, %12-15R\t@ Impl def'],
  [0x0eea0a10, 0x0fff0fff, 'vmsr%c\tfpinst2, %12-15R\t@ Impl def'],
  [0x0ef00a10, 0x0fff0fff, 'vmrs%c\t%12-15R, fpsid'],
  [0x0ef1fa10, 0x0fffffff, 'vmrs%c\tAPSR_nzcv, fpscr'],
  [0x0ef10a10, 0x0fff0fff, 'vmrs%c\t%12-15r, fpscr'],
  [0x0ef80a10, 0x0fff0fff, 'vmrs%c\t%12-15R, fpexc'],
  [0x0ef90a10, 0x0fff0fff, 'vmrs%c\t%12-15R, fpinst\t@ Impl def'],
  [0x0efa0a10, 0x0fff0fff, 'vmrs%c\t%12-15R, fpinst2\t@ Impl def'],
  // FMSR FMRS
  [0x0e000a10, 0x0ff00f7f, 'vmov%c\t%y2, %12-15R'],
  [0x0e100a10, 0x0ff00f7f, 'vmov%c\t%12-15R, %y2'],
  // Data processing, extension opcodes
  [0x0eb50a40, 0x0fbf0f70, "vcmp%7'e%c.f32\t%y1, #0.0"],
  [0x0eb50b40, 0x0fff0f70, "vcmp%7'e%c.f64\t%z1, #0.0"],
  [0x0eb00a40, 0x0fbf0fd0, 'vmov%c.f32\t%y1, %y0'],
  [0x0eb00ac0, 0x0fbf0fd0, 'vabs%c.f32\t%y1, %y0'],
  [0x0eb00b40, 0x0fff0ff0, 'vmov%c.f64\t%z1, %z0'],
  [0x0eb00bc0, 0x0fff0ff0, 'vabs%c.f64\t%z1, %z0'],
  [0x0eb10a40, 0x0fbf0fd0, 'vneg%c.f32\t%y1, %y0'],
  [0x0eb10ac0, 0x0fbf0fd0, 'vsqrt%c.f32\t%y1, %y0'],
  [0x0eb10b40, 0x0fff0ff0, 'vneg%c.f64\t%z1, %z0'],
  [0x0eb10bc0, 0x0fff0ff0, 'vsqrt%c.f64\t%z1, %z0'],
  [0x0eb70ac0, 0x0fff0fd0, 'vcvt%c.f64.f32\t%z1, %y0'],
  [0x0eb70bc0, 0x0fbf0ff0, 'vcvt%c.f32.f64\t%y1, %z0'],
  [0x0eb80a40, 0x0fbf0f50, 'vcvt%c.f32.%7?su32\t%y1, %y0'],
  [0x0eb80b40, 0x0fff0f50, 'vcvt%c.f64.%7?su32\t%z1, %y0'],
  [0x0eb40a40, 0x0fbf0f50, "vcmp%7'e%c.f32\t%y1, %y0"],
  [0x0eb40b40, 0x0fff0f70, "vcmp%7'e%c.f64\t%z1, %z0"],
  [0x0ebc0a40, 0x0fbe0f50, 'vcvt%7`r%c.%16?su32.f32\t%y1, %y0'],
  [0x0ebc0b40, 0x0fbe0f70, 'vcvt%7`r%c.%16?su32.f64\t%y1, %z0'],
  // Data processing, three registers
  [0x0e000a00, 0x0fb00f50, 'vmla%c.f32\t%y1, %y2, %y0'],
  [0x0e000a40, 0x0fb00f50, 'vmls%c.f32\t%y1, %y2, %y0'],
  [0x0e000b00, 0x0ff00ff0, 'vmla%c.f64\t%z1, %z2, %z0'],
  [0x0e000b40, 0x0ff00ff0, 'vmls%c.f64\t%z1, %z2, %z0'],
  [0x0e100a00, 0x0fb00f50, 'vnmls%c.f32\t%y1, %y2, %y0'],
  [0x0e100a40, 0x0fb00f50, 'vnmla%c.f32\t%y1, %y2, %y0'],
  [0x0e100b00, 0x0ff00ff0, 'vnmls%c.f64\t%z1, %z2, %z0'],
  [0x0e100b40, 0x0ff00ff0, 'vnmla%c.f64\t%z1, %z2, %z0'],
  [0x0e200a00, 0x0fb00f50, 'vmul%c.f32\t%y1, %y2, %y0'],
  [0x0e200a40, 0x0fb00f50, 'vnmul%c.f32\t%y1, %y2, %y0'],
  [0x0e200b00, 0x0ff00ff0, 'vmul%c.f64\t%z1, %z2, %z0'],
  [0x0e200b40, 0x0ff00ff0, 'vnmul%c.f64\t%z1, %z2, %z0'],
  [0x0e300a00, 0x0fb00f50, 'vadd%c.f32\t%y1, %y2, %y0'],
  [0x0e300a40, 0x0fb00f50, 'vsub%c.f32\t%y1, %y2, %y0'],
  [0x0e300b00, 0x0ff00ff0, 'vadd%c.f64\t%z1, %z2, %z0'],
  [0x0e300b40, 0x0ff00ff0, 'vsub%c.f64\t%z1, %z2, %z0'],
  [0x0e800a00, 0x0fb00f50, 'vdiv%c.f32\t%y1, %y2, %y0'],
  [0x0e800b00, 0x0ff00ff0, 'vdiv%c.f64\t%z1, %z2, %z0'],
].map(([v, m, f]) => [v >>> 0, m >>> 0, f]);

// %<fields> bitfield value; returns [value, width, index after the field spec].
function vfpField(w, f, i) {
  let v = 0, width = 0;
  for (;;) {
    let a = 0; while (f.charCodeAt(i) >= 48 && f.charCodeAt(i) <= 57) a = a * 10 + f.charCodeAt(i++) - 48;
    let b = a;
    if (f[i] === '-') { i++; b = 0; while (f.charCodeAt(i) >= 48 && f.charCodeAt(i) <= 57) b = b * 10 + f.charCodeAt(i++) - 48; }
    v |= ((w >>> a) & ((1 << (b - a + 1)) - 1)) << width; width += b - a + 1;
    if (f[i] !== ',') return [v, width, i];
    i++;
  }
}
function vfpFmt(w, pc, f) {
  let s = '';
  for (let i = 0; i < f.length; i++) {
    const ch = f[i];
    if (ch !== '%') { s += ch; continue; }
    const k = f[++i];
    if (k === 'c') { s += CC[w >>> 28]; continue; }
    if (k === 'A') { s += addrCp(w, pc); continue; }
    if (k === 'B') {
      const r = (w >>> 12) & 15, o = (w >>> 1) & 0x3f, n = (w & 0xff) >>> 1;
      if (!n || r + n > 16) unp = true;
      s += o === 1 ? '{d' + r + '}' : r + o > 32 ? '{d' + r + '-<overflow reg d' + (r + o - 1) + '>}' : '{d' + r + '-d' + (r + o - 1) + '}';
      continue;
    }
    if (k === 'y' || k === 'z') {
      const sg = k === 'y', t = f[++i], p = sg ? 's' : 'd';
      let r;
      if (t === '0' || t === '4') r = sg ? ((w & 15) << 1) | ((w >>> 5) & 1) : w & 15;
      else if (t === '2') r = sg ? (((w >>> 16) & 15) << 1) | ((w >>> 7) & 1) : (w >>> 16) & 15;
      else r = sg ? (((w >>> 12) & 15) << 1) | ((w >>> 22) & 1) : (w >>> 12) & 15;
      if (t === '3') {
        let n = w & 0xff; if (!sg) n >>= 1;
        if (!n || r + n > (sg ? 32 : 16)) unp = true;
        s += '{' + p + r + (--n ? '-' + p + (r + n) : '') + '}';
      } else {
        if (t === '4' && r === 31) unp = true;
        s += p + r + (t === '4' ? ', ' + p + (r + 1) : '');
      }
      continue;
    }
    const [v, width, j] = vfpField(w, f, i);
    i = j;
    switch (f[i]) {
      case 'r': s += RN[v]; break;
      case 'R': s += reg(v, 1); break;
      case 'U': s += reg(v, 1, 1); break;
      case 'd': s += v; break;
      case 'D': s += 'd' + v; break;
      case "'": i++; if (v) s += f[i]; break;
      case '`': i++; if (!v) s += f[i]; break;
      case '?': s += f[i + (1 << width) - v]; i += 1 << width; break;
    }
  }
  return s;
}
// cp10/cp11 (cond != 1111): the VFPv2 text, or null (UNDEFINED -> .word).
function vfp(w, pc) {
  for (let i = 0; i < VFP.length; i++) {
    const e = VFP[i];
    if (((w & e[1]) >>> 0) === e[0]) {
      // vldm/vstm (not vldr/vstr, not mcrr/mrrc) writing back to pc
      if ((w & 0x0e200000) === 0x0c200000 && ((w >>> 16) & 15) === 15) unp = true;
      return vfpFmt(w, pc, e[2]);
    }
  }
  return null;
}

function coproc(w, pc, c, two) {
  const cp = (w >>> 8) & 15, n = (w >>> 16) & 15, d = (w >>> 12) & 15, m = w & 15;
  const hi = (w >>> 20) & 0xff;
  if (cp === 10 || cp === 11) return two ? null : vfp(w, pc);
  if (cp === 9 && (two || (w >>> 28) !== 14)) unp = true;
  if (!two && (hi & 0xfe) === 0xc4) {
    if (hi & 1) return 'mrrc' + c + '\t' + cp + ', ' + ((w >>> 4) & 15) + ', ' + reg(d, 1, 1) + ', ' + reg(n, 1, 1) + ', cr' + m;
    return 'mcrr' + c + '\t' + cp + ', ' + ((w >>> 4) & 15) + ', ' + reg(d, 1) + ', ' + RN[n] + ', cr' + m;
  }
  if (cp === 9 && (two ? (w & 0x0f000010) === 0x0e000000 : (w & 0x0f10f010) !== 0x0e10f010)) return null;
  const t = two ? '2' : '';
  if ((w & 0x0f000000) === 0x0e000000) {
    const tl = ', cr' + n + ', cr' + m + ', {' + ((w >>> 5) & 7) + '}';
    if (!(w & 0x10)) return 'cdp' + t + c + '\t' + cp + ', ' + ((w >>> 20) & 15) + ', cr' + d + ', cr' + n + ', cr' + m + ', {' + ((w >>> 5) & 7) + '}';
    const o = ((w >>> 21) & 7) + ', ';
    if (w & 0x00100000) return 'mrc' + t + c + '\t' + cp + ', ' + o + (d === 15 && !two ? 'APSR_nzcv' : RN[d]) + tl;
    return 'mcr' + t + c + '\t' + cp + ', ' + o + reg(d, 1) + tl;
  }
  return (w & 0x00100000 ? 'ldc' : 'stc') + t + (w & 0x00400000 ? 'l' : '') + c + '\t' + cp + ', cr' + d + ', ' + addrCp(w, pc);
}
const XY = (w) => (w & 0x20 ? 't' : 'b') + (w & 0x40 ? 't' : 'b');

function armBody(w, pc) {
  const cond = w >>> 28, c = CC[cond];
  const rd = (w >>> 12) & 15, rn = (w >>> 16) & 15, rm = w & 15, rs = (w >>> 8) & 15;
  const S = (w & 0x00100000) ? 's' : '';
  if (cond === 15) {
    if (((w & 0xfe000000) >>> 0) === 0xfa000000)
      return 'blx\t' + hex(pc + 8 + (((w << 8) >> 6)) + ((w >>> 23) & 2));
    if (((w & 0xfc70f000) >>> 0) === 0xf450f000) return 'pld\t' + addr2(w, pc);
    if ((w & 0x0e000000) === 0x0c000000 || (w & 0x0f000000) === 0x0e000000) return coproc(w, pc, '', true);
    return null;
  }
  const top = (w >>> 25) & 7;
  if (top >= 6) {
    if ((w & 0x0f000000) === 0x0f000000) return 'svc' + c + '\t' + hex8(w & 0xffffff);
    return coproc(w, pc, c, false);
  }
  if (top === 5) return 'b' + (w & 0x01000000 ? 'l' : '') + c + '\t' + hex(pc + 8 + ((w << 8) >> 6));
  if (top === 4) {
    const L = w & 0x00100000, list = w & 0xffff, x = w & 0x0fff0000;
    const bits = list.toString(2).replace(/0/g, '').length;
    if (x === 0x092d0000 || x === 0x08bd0000) {
      if (bits === 1) return (L ? 'ldmfd' : 'stmfd') + c + '\tsp!, ' + regList(list);
      if (!bits) unp = true;
      return (L ? 'pop' : 'push') + c + '\t' + regList(list);
    }
    const mode = ['da', 'ia', 'db', 'ib'][(w >>> 23) & 3];
    let m = L ? 'ldm' : 'stm';
    if (L ? (w & 0x01800000) !== 0x00800000 : (w & 0x01f00000) !== 0x00800000 || (w & 0x00400000)) m += mode;
    if (!L && (w & 0x0ff00000) === 0x08800000) m = 'stm';
    return m + c + '\t' + reg(rn, 1) + (w & 0x00200000 ? '!' : '') + ', ' + regList(list) + (w & 0x00400000 ? '^' : '');
  }
  if (top === 2 || top === 3) {
    if (top === 3 && (w & 0x10)) {
      if (((w & 0xfff000f0) >>> 0) === 0xe7f000f0) { vic = ((w >>> 4) & 0xfff0) | (w & 15); return 'udf\t#' + vic; }
      return null;
    }
    const L = w & 0x00100000, B = w & 0x00400000;
    if ((w & 0x0fff0fff) === 0x052d0004) return 'push' + c + '\t{' + RN[rd] + '}\t\t@ (str' + c + ' ' + RN[rd] + ', ' + addr2(w, pc) + ')';
    if ((w & 0x0fff0fff) === 0x049d0004) return 'pop' + c + '\t{' + RN[rd] + '}\t\t@ (ldr' + c + ' ' + RN[rd] + ', ' + addr2(w, pc) + ')';
    const t = (w & 0x01200000) === 0x00200000 ? 't' : '';
    return (L ? 'ldr' : 'str') + (B ? 'b' : '') + t + c + '\t' + reg(rd, B || (L && t)) + ', ' + addr2(w, pc);
  }
  // ---- top 0/1: data processing, misc, multiplies, extra load/store ----
  if (w === 0xe1a00000) return 'nop\t\t\t@ (mov r0, r0)';
  const f0 = w & 0x0ffffff0;
  if (f0 === 0x012fff10) return 'bx' + c + '\t' + RN[rm];
  const b74 = w & 0xf0;
  if ((w & 0x0e000090) === 0x00000090) {
    // multiplies / swp / extra load-store
    if ((w & 0x0fe000f0) === 0x00000090) return 'mul' + S + c + '\t' + reg(rn, 1) + ', ' + reg(rm, 1) + ', ' + reg(rs, 1);
    if ((w & 0x0fe000f0) === 0x00200090) return 'mla' + S + c + '\t' + reg(rn, 1) + ', ' + reg(rm, 1) + ', ' + reg(rs, 1) + ', ' + reg(rd, 1);
    if ((w & 0x0fb00ff0) === 0x01000090) return 'swp' + (w & 0x00400000 ? 'b' : '') + c + '\t' + reg(rd, 1, 0, 1) + ', ' + reg(rm, 1, 1) + ', [' + reg(rn, 1, 1, 1) + ']';
    if ((w & 0x0f8000f0) === 0x00800090)
      return (w & 0x00400000 ? 's' : 'u') + (w & 0x00200000 ? 'mlal' : 'mull') + S + c + '\t' + reg(rd, 1, 1) + ', ' + reg(rn, 1, 1) + ', ' + reg(rm, 1) + ', ' + reg(rs, 1);
    if ((w & 0x0e1000d0) === 0x000000d0) return (b74 === 0xd0 ? 'ldrd' : 'strd') + c + '\t' + RN[rd] + ', ' + addr3(w, pc);
    if ((w & 0x0e5000f0) === 0x004000b0 || (w & 0x0e500ff0) === 0x000000b0) return 'strh' + c + '\t' + reg(rd, 1) + ', ' + addr3(w, pc);
    if ((w & 0x0e5000f0) === 0x00500090 || (w & 0x0e500ff0) === 0x00100090) return null;
    if ((w & 0x0e500090) === 0x00500090 || (w & 0x0e500f90) === 0x00100090)
      return 'ldr' + (w & 0x40 ? 's' : '') + (w & 0x20 ? 'h' : 'b') + c + '\t' + reg(rd, 1) + ', ' + addr3(w, pc);
  }
  if (f0 === 0x012fff20) return 'bxj' + c + '\t' + reg(rm, 1);
  if (((w & 0xfff000f0) >>> 0) === 0xe1200070) return 'bkpt\t0x' + (((w >>> 4) & 0xfff0) | (w & 15)).toString(16).padStart(4, '0');
  if (f0 === 0x012fff30) return 'blx' + c + '\t' + reg(rm, 1);
  if ((w & 0x0fff0ff0) === 0x016f0f10) return 'clz' + c + '\t' + reg(rd, 1) + ', ' + reg(rm, 1);
  if ((w & 0x0f900090) === 0x01000080) {
    const op = (w >>> 21) & 3;
    // (objdump's smlatt/smlawt table entries don't flag pc in some operands)
    const tt = (w & 0x60) === 0x60;
    if (op === 0) return 'smla' + XY(w) + c + '\t' + reg(rn, !tt) + ', ' + reg(rm, !tt) + ', ' + reg(rs, 1) + ', ' + reg(rd, 1);
    if (op === 1) {
      if (!(w & 0x20)) return 'smlaw' + (w & 0x40 ? 't' : 'b') + c + '\t' + reg(rn, 1) + ', ' + reg(rm, !(w & 0x40)) + ', ' + reg(rs, 1) + ', ' + reg(rd, 1);
      if (!(w & 0xf000)) return 'smulw' + (w & 0x40 ? 't' : 'b') + c + '\t' + reg(rn, 1) + ', ' + reg(rm, 1) + ', ' + reg(rs, 1);
    }
    if (op === 2) return 'smlal' + XY(w) + c + '\t' + reg(rd, 1, 1) + ', ' + reg(rn, 1, 1) + ', ' + reg(rm, 1) + ', ' + reg(rs, 1);
    if (op === 3 && !(w & 0xf000)) return 'smul' + XY(w) + c + '\t' + reg(rn, 1) + ', ' + reg(rm, 1) + ', ' + reg(rs, 1);
  }
  if ((w & 0x0f900ff0) === 0x01000050)
    return ['qadd', 'qsub', 'qdadd', 'qdsub'][(w >>> 21) & 3] + c + '\t' + reg(rd, 1) + ', ' + reg(rm, 1) + ', ' + reg(rn, 1);

  const op = (w >>> 21) & 15, I = w & 0x02000000;
  const P = !I && (w & 0x10);                            // register-shifted register
  if (P && (w & 0x80) && op !== 9 && op !== 13) return null;
  if (op >= 8 && op <= 11) {
    if ((w & 0x0db0f000) === 0x0120f000) {
      let f = (w & 0x00400000 ? 'SPSR_' : 'CPSR_');
      if (w & 0x80000) f += 'f'; if (w & 0x40000) f += 's'; if (w & 0x20000) f += 'x'; if (w & 0x10000) f += 'c';
      return 'msr' + c + '\t' + f + ', ' + op2(w);
    }
    if ((w & 0x0fbf0fff) === 0x010f0000) return 'mrs' + c + '\t' + reg(rd, 1) + ', ' + (w & 0x00400000 ? 'SPSR' : 'CPSR');
    if (!S && op === 9) return null;
    let s = DP[op];
    let obs = false;
    if (rd === 15) { s += 'p'; obs = true; }
    s += c + '\t' + reg(rn, P) + ', ' + op2(w);
    if (obs) s += '\t@ p-variant is OBSOLETE';
    return s;
  }
  if (op === 13 || op === 15) {
    if (rn && op === 13) return null;
    if (op === 15 || I) return DP[op] + S + c + '\t' + reg(rd, P) + ', ' + op2(w);
    if (!(w & 0xff0)) return 'mov' + S + c + '\t' + RN[rd] + ', ' + RN[rm];
    const t = (w >>> 5) & 3;
    if (t === 3 && (w & 0xff0) === 0x60) return 'rrx' + S + c + '\t' + RN[rd] + ', ' + RN[rm];
    return SH[t] + S + c + '\t' + reg(rd, 1) + ', ' + shift(w, false);
  }
  return DP[op] + S + c + '\t' + reg(rd, P) + ', ' + reg(rn, P) + ', ' + op2(w);
}

export function disasmArm(word, addr) {
  const w = word >>> 0; reset();
  const s = armBody(w, addr >>> 0);
  return s === null ? undefA(w) : tail(s);
}

// ---------------------------------------------------------------- Thumb
const lo = (hw, b) => RN[(hw >>> b) & 7];
function tList(l, extra) {
  const r = [];
  for (let i = 0; i < 8; i++) if (l & (1 << i)) r.push(RN[i]);
  if (extra) r.push(extra);
  return '{' + r.join(', ') + '}';
}
function thumbBody(hw, pc) {
  const d = lo(hw, 0), s = lo(hw, 3), top5 = hw >>> 11;
  const imm5 = (hw >>> 6) & 31, i8 = hw & 0xff, r8 = lo(hw, 8);
  switch (top5) {
    case 0: return imm5 ? 'lsls\t' + d + ', ' + s + ', #' + imm5 : 'movs\t' + d + ', ' + s;
    case 1: return 'lsrs\t' + d + ', ' + s + ', #' + (imm5 || 32);
    case 2: return 'asrs\t' + d + ', ' + s + ', #' + (imm5 || 32);
    case 3: {
      const o = (hw & 0x200) ? 'subs' : 'adds', t = lo(hw, 6);
      return o + '\t' + d + ', ' + s + ', ' + ((hw & 0x400) ? '#' + ((hw >>> 6) & 7) : t);
    }
    case 4: vic = i8; return 'movs\t' + r8 + ', #' + i8;
    case 5: vic = i8; return 'cmp\t' + r8 + ', #' + i8;
    case 6: vic = i8; return 'adds\t' + r8 + ', #' + i8;
    case 7: vic = i8; return 'subs\t' + r8 + ', #' + i8;
    case 8:
      if (!(hw & 0x400)) {
        const o = ['ands', 'eors', 'lsls', 'lsrs', 'asrs', 'adcs', 'sbcs', 'rors', 'tst', 'negs', 'cmp', 'cmn', 'orrs', 'muls', 'bics', 'mvns'][(hw >>> 6) & 15];
        return o + '\t' + d + ', ' + s;
      } else {
        const op = (hw >>> 8) & 3, hs = RN[(hw >>> 3) & 15], hd = RN[(hw & 7) | ((hw >>> 4) & 8)];
        if (op === 3) {
          if (hw & 0x80) return (hw & 7) ? null : 'blx\t' + hs;
          return 'bx\t' + hs;
        }
        if (hw === 0x46c0) return 'nop\t\t\t@ (mov r8, r8)';
        return ['add', 'cmp', 'mov'][op] + '\t' + hd + ', ' + hs;
      }
    case 9: {
      const a = ((pc + 4) & ~3) + i8 * 4;
      return 'ldr\t' + r8 + ', [pc, #' + i8 * 4 + ']\t@ (' + hex(a) + ')';
    }
    case 10: case 11: {
      const o = (hw >>> 9) & 7, m = lo(hw, 6);
      return ['str', 'strh', 'strb', 'ldrsb', 'ldr', 'ldrh', 'ldrb', 'ldrsh'][o] + '\t' + d + ', [' + s + ', ' + m + ']';
    }
    case 12: case 13: vic = imm5 * 4; return (top5 & 1 ? 'ldr' : 'str') + '\t' + d + ', [' + s + ', #' + vic + ']';
    case 14: case 15: vic = imm5; return (top5 & 1 ? 'ldrb' : 'strb') + '\t' + d + ', [' + s + ', #' + vic + ']';
    case 16: case 17: vic = imm5 * 2; return (top5 & 1 ? 'ldrh' : 'strh') + '\t' + d + ', [' + s + ', #' + vic + ']';
    case 18: case 19: vic = i8 * 4; return (top5 & 1 ? 'ldr' : 'str') + '\t' + r8 + ', [sp, #' + vic + ']';
    case 20: return 'add\t' + r8 + ', pc, #' + i8 * 4 + '\t@ (adr ' + r8 + ', ' + hex(((pc + 4) & ~3) + i8 * 4) + ')';
    case 21: vic = i8 * 4; return 'add\t' + r8 + ', sp, #' + vic;
    case 22: case 23: {
      const k = (hw >>> 8) & 15;
      if (k === 0) { vic = (hw & 0x7f) * 4; return (hw & 0x80 ? 'sub' : 'add') + '\tsp, #' + vic; }
      if (k === 4 || k === 5) return 'push\t' + tList(i8, k & 1 ? 'lr' : '');
      if (k === 12 || k === 13) return 'pop\t' + tList(i8, k & 1 ? 'pc' : '');
      if (k === 14) return 'bkpt\t0x' + i8.toString(16).padStart(4, '0');
      return null;
    }
    case 24: return 'stmia\t' + r8 + '!, ' + tList(i8);
    case 25: return 'ldmia\t' + r8 + (i8 & (1 << ((hw >>> 8) & 7)) ? '' : '!') + ', ' + tList(i8);
    case 26: case 27: {
      const cond = (hw >>> 8) & 15;
      if (cond === 15) { vic = i8; return 'svc\t' + i8; }
      if (cond === 14) { vic = i8; return 'udf\t#' + i8; }
      return 'b' + CC[cond] + '.n\t' + hex(pc + 4 + (((hw & 0xff) << 24) >> 23));
    }
    case 28: return 'b.n\t' + hex(pc + 4 + (((hw & 0x7ff) << 21) >> 20));
  }
  return null;
}

export function disasmThumb(hw, addr, next) {
  hw &= 0xffff; addr >>>= 0; reset();
  if ((hw & 0xf800) === 0xf000 && next !== undefined) {
    const n = next & 0xffff;
    const off = (((hw & 0x7ff) << 21) >> 9) + ((n & 0x7ff) << 1);
    if ((n & 0xf800) === 0xf800) return { text: 'bl\t' + hex(addr + 4 + off), size: 4 };
    if ((n & 0xf801) === 0xe800) return { text: 'blx\t' + hex((addr + 4 + off) & ~3), size: 4 };
  }
  const s = hw >= 0xe800 ? null : thumbBody(hw, addr);
  return { text: s === null ? '.short\t0x' + hw.toString(16).padStart(4, '0') : tail(s), size: 2 };
}

export function disasm(read32, read16, addr, thumb) {
  if (thumb) {
    const hw = read16(addr) & 0xffff;
    const r = disasmThumb(hw, addr, (hw & 0xf800) === 0xf000 ? read16(addr + 2) : undefined);
    return { text: r.text, size: r.size, word: r.size === 4 ? ((hw << 16) | (read16(addr + 2) & 0xffff)) >>> 0 : hw };
  }
  const w = read32(addr) >>> 0;
  return { text: disasmArm(w, addr), size: 4, word: w };
}
