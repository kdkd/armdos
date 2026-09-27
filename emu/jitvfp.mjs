// JIT code generation for VFPv2 (CP10/CP11) instructions.
//
// Fast paths use native JS float math on the CPU's F32/F64 views (single
// precision rounded with Math.fround after every operation) and Int32 word
// moves (FW) for anything that must be bit-exact. They run only while
// `fpx` (= c.fpSlow at block entry) is 0: VFP enabled, round-to-nearest, no
// flush-to-zero, LEN = 0, no exact-flags mode. Loads, stores and transfers need
// only `fpo` (= c.fpOff) to be 0. Any non-finite result (NaN, infinity,
// overflow, division by zero, invalid operation) re-executes the instruction
// in the exact interpreter (vfp.mjs), which also produces the cumulative flags.
// FMXR ends the block (the interpreter raises brk), so fpx/fpo stay valid.
//
// genVfp(i, pc, fb, S, E) returns the JS body for instruction i, or null when
// the instruction should simply go through the fallback `fb`.
//   fb: statement that runs the interpreter and leaves the block if needed
//   S/E: statement / expression that syncs flags and cached registers

export function genVfp(i, pc, fb, S, E) {
  const top = (i >>> 24) & 15, sp = ((i >>> 8) & 1) === 0;
  if (top === 0xE) return (i & 0x10) ? genXfer(i, fb) : genDP(i, sp, fb);
  if ((i & 0x0FE00000) === 0x0C400000) return genXfer2(i, fb);
  if (top === 0xC || top === 0xD) return genLS(i, pc, sp, fb, S, E);
  return null;
}

function genDP(i, sp, fb) {
  const opc = ((i >>> 20) & 8) | ((i >>> 19) & 4) | ((i >>> 19) & 2) | ((i >>> 6) & 1);
  const Fd = (i >>> 12) & 15, Db = (i >>> 22) & 1, Fn = (i >>> 16) & 15, Nb = (i >>> 7) & 1, Fm = i & 15, Mb = (i >>> 5) & 1;
  const sD = (Fd << 1) | Db, sN = (Fn << 1) | Nb, sM = (Fm << 1) | Mb;
  const A = sp ? (x) => `F32[${x}]` : (x) => `F64[${x}]`;
  const fr = sp ? 'Math.fround' : '';
  const arith = (dst, expr) => `if (fpx !== 0) { ${fb} } else { const x = ${expr}; if (x - x === 0) ${dst} = x; else { ${fb} } }`;
  if (opc !== 15) {
    if (opc > 8) return null;
    if (!sp && (Db | Nb | Mb)) return null;
    const d = sp ? sD : Fd, n = sp ? sN : Fn, m = sp ? sM : Fm;
    const p = `${fr}(${A(n)} * ${A(m)})`;
    let e;
    switch (opc) {
      case 0: e = `${fr}(${A(d)} + ${p})`; break;
      case 1: e = `${fr}(${A(d)} - ${p})`; break;
      case 2: e = `${fr}(-${A(d)} + ${p})`; break;
      case 3: e = `${fr}(-${A(d)} - ${p})`; break;
      case 4: e = p; break;
      case 5: e = `-${p}`; break;
      case 6: e = `${fr}(${A(n)} + ${A(m)})`; break;
      case 7: e = `${fr}(${A(n)} - ${A(m)})`; break;
      default: e = `${fr}(${A(n)} / ${A(m)})`;
    }
    return arith(A(d), e);
  }
  const ext = sN;
  switch (ext) {
    case 0: case 1: case 2: {                         // FCPY / FABS / FNEG: bit-exact word ops
      if (!sp && (Db | Mb)) return null;
      const op = ext === 0 ? '' : ext === 1 ? ' & 0x7FFFFFFF' : ' ^ -2147483648';
      if (sp) return `if (fpx !== 0) { ${fb} } else FW[${sD}] = FW[${sM}]${op};`;
      return `if (fpx !== 0) { ${fb} } else { const lo = FW[${2 * Fm}], hi = FW[${2 * Fm + 1}]; FW[${2 * Fd}] = lo; FW[${2 * Fd + 1}] = hi${op}; }`;
    }
    case 3:
      if (!sp && (Db | Mb)) return null;
      return arith(A(sp ? sD : Fd), `${fr}(Math.sqrt(${A(sp ? sM : Fm)}))`);
    case 8: case 9: case 10: case 11: {
      if (!sp && (Db | Mb)) return null;
      const z = (ext & 2) !== 0;
      if (z && (Fm | Mb)) return null;
      const a = A(sp ? sD : Fd), b = z ? '0' : A(sp ? sM : Fm);
      return `if (fpx !== 0) { ${fb} } else { const a = ${a}, b = ${b}; if (a === a && b === b) c.fpscr = (c.fpscr & 0x0FFFFFFF) | (a === b ? 0x60000000 : a < b ? -2147483648 : 0x20000000); else { ${fb} } }`;
    }
    case 15:
      if (sp) {                                         // FCVTDS Dd <- Sm
        if (Db) return null;
        return `if (fpx !== 0) { ${fb} } else { const a = F32[${sM}]; if (a === a) F64[${Fd}] = a; else { ${fb} } }`;
      }
      if (Mb) return null;                              // FCVTSD Sd <- Dm
      return arith(`F32[${sD}]`, `Math.fround(F64[${Fm}])`);
    case 16: case 17: {                                 // FUITO / FSITO
      const src = `FW[${sM}]${ext === 16 ? ' >>> 0' : ''}`;
      if (sp) return `if (fpx !== 0) { ${fb} } else F32[${sD}] = Math.fround(${src});`;
      if (Db) return null;
      return `if (fpx !== 0) { ${fb} } else F64[${Fd}] = ${src};`;
    }
    case 24: case 25: case 26: case 27: {               // FTOUI / FTOUIZ / FTOSI / FTOSIZ
      if (!sp && Mb) return null;
      const a = sp ? `F32[${sM}]` : `F64[${Fm}]`, signed = ext >= 26, rz = (ext & 1) !== 0;
      if (rz) {
        const range = signed ? 'a > -2147483649 && a < 2147483648' : 'a > -1 && a < 4294967296';
        return `if (fpx !== 0) { ${fb} } else { const a = ${a}; if (${range}) FW[${sD}] = ${signed ? 'a | 0' : 'a >>> 0'}; else { ${fb} } }`;
      }
      const range = signed ? 't >= -2147483648 && t <= 2147483647' : 't >= 0 && t <= 4294967295';
      return `if (fpx !== 0) { ${fb} } else { const a = ${a}; let t = Math.round(a); if (t - a === 0.5 && t % 2 !== 0) t -= 1; if (${range}) FW[${sD}] = t; else { ${fb} } }`;
    }
  }
  return null;
}

// MCR/MRC forms: FMSR/FMRS, FMDLR/FMDHR/FMRDL/FMRDH, FMRX FPSCR / FMSTAT
function genXfer(i, fb) {
  const opc1 = (i >>> 21) & 7, L = (i >>> 20) & 1, rd = (i >>> 12) & 15, cp10 = ((i >>> 8) & 1) === 0;
  if (cp10 && opc1 === 7) {
    if (!L || ((i >>> 16) & 15) !== 1 || (i & 0xEF)) return null;     // only FMRX FPSCR / FMSTAT inline
    if (rd === 15) return `if (fpo !== 0) { ${fb} } else { const f = c.fpscr; nv = f; zv = (f & 0x40000000) ? 0 : 1; cf = (f >>> 29) & 1; vf = (f >>> 28) & 1; }`;
    return `if (fpo !== 0) { ${fb} } else r[${rd}] = c.fpscr;`;
  }
  if ((i & 0x6F) || rd === 15) return null;
  let w;
  if (cp10) { if (opc1 !== 0) return null; w = (((i >>> 16) & 15) << 1) | ((i >>> 7) & 1); }
  else { if (opc1 > 1 || (i & 0x80)) return null; w = 2 * ((i >>> 16) & 15) + opc1; }
  return L ? `if (fpo !== 0) { ${fb} } else r[${rd}] = FW[${w}];` : `if (fpo !== 0) { ${fb} } else FW[${w}] = r[${rd}];`;
}

// MCRR/MRRC forms: FMDRR/FMRRD/FMSRR/FMRRS
function genXfer2(i, fb) {
  const L = (i >>> 20) & 1, rn = (i >>> 16) & 15, rd = (i >>> 12) & 15, cp10 = ((i >>> 8) & 1) === 0;
  if ((i & 0xD0) !== 0x10 || rd === 15 || rn === 15) return null;
  let w;
  if (cp10) { w = ((i & 15) << 1) | ((i >>> 5) & 1); if (w === 31) return null; }
  else { if (i & 0x20) return null; w = 2 * (i & 15); }
  if (L) { if (rd === rn) return null; return `if (fpo !== 0) { ${fb} } else { r[${rd}] = FW[${w}]; r[${rn}] = FW[${w + 1}]; }`; }
  return `if (fpo !== 0) { ${fb} } else { FW[${w}] = r[${rd}]; FW[${w + 1}] = r[${rn}]; }`;
}

function genLS(i, pc, sp, fb, S, E) {
  const P = (i >>> 24) & 1, U = (i >>> 23) & 1, Db = (i >>> 22) & 1, Wb = (i >>> 21) & 1, L = (i >>> 20) & 1;
  const rn = (i >>> 16) & 15, Fd = (i >>> 12) & 15, imm8 = i & 0xFF;
  const base = rn === 15 ? `${((pc + 8) & ~3) | 0}` : `r[${rn}]`;
  if (P && !Wb) {                                               // FLDS/FSTS/FLDD/FSTD
    if (!sp && Db) return null;
    const w0 = sp ? (Fd << 1) | Db : 2 * Fd;
    const a = `const a = (${base} ${U ? '+' : '-'} ${imm8 * 4}) | 0;`;
    if (sp) {
      if (L) return `if (fpo !== 0) { ${fb} } else { ${a} FW[${w0}] = (a & 0xFF000003) === 0 ? m32[a >>> 2] : (${E}c.ld32a(a, ${pc})); }`;
      return `if (fpo !== 0) { ${fb} } else { ${a} if ((a & 0xFF000003) === 0 && pf[a >>> 7] === 0) m32[a >>> 2] = FW[${w0}]; else { ${S}c.st32(a, FW[${w0}], ${pc}); } }`;
    }
    if (L) return `if (fpo !== 0) { ${fb} } else { ${a} if ((a & 0xFF000003) === 0 && ((a + 4) & 0xFF000000) === 0) { FW[${w0}] = m32[a >>> 2]; FW[${w0 + 1}] = m32[(a >>> 2) + 1]; } else { ${S}const lo = c.ld32a(a, ${pc}), hi = c.ld32a((a + 4) | 0, ${pc}); FW[${w0}] = lo; FW[${w0 + 1}] = hi; } }`;
    return `if (fpo !== 0) { ${fb} } else { ${a} if ((a & 0xFF000003) === 0 && ((a + 4) & 0xFF000000) === 0 && pf[a >>> 7] === 0 && pf[(a + 4) >>> 7] === 0) { m32[a >>> 2] = FW[${w0}]; m32[(a >>> 2) + 1] = FW[${w0 + 1}]; } else { ${S}c.st32(a, FW[${w0}], ${pc}); c.st32((a + 4) | 0, FW[${w0 + 1}], ${pc}); } }`;
  }
  // multiples
  if (P === U || (P && !Wb) || rn === 15) return null;
  let w0, nw;
  if (sp) { w0 = (Fd << 1) | Db; nw = imm8; if (nw === 0 || w0 + nw > 32) return null; }
  else { if (Db) return null; const nd = imm8 >>> 1; w0 = 2 * Fd; nw = 2 * nd; if (nd === 0 || Fd + nd > 16) return null; }
  const start = P ? -imm8 * 4 : 0, wb = U ? imm8 * 4 : -imm8 * 4, last = 4 * nw - 4;
  let code = `if (fpo !== 0) { ${fb} } else { const base = r[${rn}]; const a = (base + ${start}) | 0; `;
  if (L) {
    code += `if ((a & 0xFF000003) === 0 && ((a + ${last}) & 0xFF000000) === 0) { const q = a >>> 2; `;
    for (let k = 0; k < nw; k++) code += `FW[${w0 + k}] = m32[q + ${k}]; `;
  } else {
    code += `if ((a & 0xFF000003) === 0 && ((a + ${last}) & 0xFF000000) === 0 && pf[a >>> 7] === 0 && pf[(a + ${last}) >>> 7] === 0) { const q = a >>> 2; `;
    for (let k = 0; k < nw; k++) code += `m32[q + ${k}] = FW[${w0 + k}]; `;
  }
  if (Wb) code += `r[${rn}] = (base + ${wb}) | 0; `;
  return code + `} else { ${fb} } }`;
}
