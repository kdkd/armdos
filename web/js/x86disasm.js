// A compact 16-bit x86 disassembler (real mode): 8086/186/286 plus the 386
// real-mode extensions ELBOW runs (66h/67h prefixes, 32-bit registers and
// addressing, FS/GS, 0F xx: Jcc near, SETcc, MOVZX/MOVSX, IMUL r,r/m,
// SHLD/SHRD, BT*, BSF/BSR, LSS/LFS/LGS, PUSH/POP FS/GS, BSWAP/XADD/CMPXCHG)
// and the x87 escape opcodes. Intel syntax as ndisasm prints it; an opcode it
// does not know is "db xx" (one byte). Used by the inspector's ELBOW view
// (web/js/elbow-panel.js); checked against ndisasm by apps/x86/tests/elbowview.mjs.
//
//   disasm86(read, ip) -> { len, mnem, ops, text, target? }
//     read(off) returns the byte at CS:off (the offset wraps at 64 KB)

const R8 = ['al', 'cl', 'dl', 'bl', 'ah', 'ch', 'dh', 'bh'];
const R16 = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di'];
const R32 = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi'];
const SREG = ['es', 'cs', 'ss', 'ds', 'fs', 'gs', 'segr6', 'segr7'];
const EA16 = ['bx+si', 'bx+di', 'bp+si', 'bp+di', 'si', 'di', 'bp', 'bx'];
const ALU = ['add', 'or', 'adc', 'sbb', 'and', 'sub', 'xor', 'cmp'];
const SHF = ['rol', 'ror', 'rcl', 'rcr', 'shl', 'shr', 'sal', 'sar'];
const CC = ['o', 'no', 'c', 'nc', 'z', 'nz', 'na', 'a', 's', 'ns', 'pe', 'po', 'l', 'nl', 'ng', 'g'];
const SEGP = { 0x26: 'es', 0x2e: 'cs', 0x36: 'ss', 0x3e: 'ds', 0x64: 'fs', 0x65: 'gs' };

// one-byte opcodes: [mnemonic, operands]; mnemonic '#n' = group n (by the ModRM reg field)
// operands: E/G = r/m and reg (b byte, w word, v word/dword by 66h, d dword), I = immediate,
// S = sign-extended imm8, J = relative, A = far pointer, O = moffs, M = memory only,
// Z = register in the opcode's low bits, a = AL/AX/EAX, lowercase words = literal registers
const OP1 = [];
for (let k = 0; k < 8; k++) {
  const b = k * 8, n = ALU[k];
  OP1[b] = [n, 'Eb,Gb']; OP1[b + 1] = [n, 'Ev,Gv']; OP1[b + 2] = [n, 'Gb,Eb']; OP1[b + 3] = [n, 'Gv,Ev'];
  OP1[b + 4] = [n, 'al,Ib']; OP1[b + 5] = [n, 'av,Iv'];
}
Object.assign(OP1, {
  0x06: ['push', 'es'], 0x07: ['pop', 'es'], 0x0e: ['push', 'cs'], 0x16: ['push', 'ss'], 0x17: ['pop', 'ss'],
  0x1e: ['push', 'ds'], 0x1f: ['pop', 'ds'], 0x27: ['daa', ''], 0x2f: ['das', ''], 0x37: ['aaa', ''], 0x3f: ['aas', ''],
  0x60: ['pusha*', ''], 0x61: ['popa*', ''], 0x62: ['bound', 'Gv,M'], 0x63: ['arpl', 'Ew,Gw'],
  0x68: ['push', 'Iv'], 0x69: ['imul', 'Gv,Ev,Iv'], 0x6a: ['push', 'Sv'], 0x6b: ['imul', 'Gv,Ev,Sv'],
  0x6c: ['insb', ''], 0x6d: ['ins*', ''], 0x6e: ['outsb', ''], 0x6f: ['outs*', ''],
  0x80: ['#1', 'Eb,Ib'], 0x81: ['#1', 'Ev,Iv'], 0x82: ['#1', 'Eb,Ib'], 0x83: ['#1', 'Ev,Sv'],
  0x84: ['test', 'Eb,Gb'], 0x85: ['test', 'Ev,Gv'], 0x86: ['xchg', 'Gb,Eb'], 0x87: ['xchg', 'Gv,Ev'],
  0x88: ['mov', 'Eb,Gb'], 0x89: ['mov', 'Ev,Gv'], 0x8a: ['mov', 'Gb,Eb'], 0x8b: ['mov', 'Gv,Ev'],
  0x8c: ['mov', 'Ev,Sw'], 0x8d: ['lea', 'Gv,M'], 0x8e: ['mov', 'Sw,Ew'], 0x8f: ['#8f', 'Ev'],
  0x90: ['nop', ''], 0x98: ['cbw*', ''], 0x99: ['cwd*', ''], 0x9a: ['call', 'A'], 0x9b: ['wait', ''],
  0x9c: ['pushf*', ''], 0x9d: ['popf*', ''], 0x9e: ['sahf', ''], 0x9f: ['lahf', ''],
  0xa0: ['mov', 'al,Ob'], 0xa1: ['mov', 'av,Ov'], 0xa2: ['mov', 'Ob,al'], 0xa3: ['mov', 'Ov,av'],
  0xa4: ['movsb', ''], 0xa5: ['movs*', ''], 0xa6: ['cmpsb', ''], 0xa7: ['cmps*', ''], 0xa8: ['test', 'al,Ib'], 0xa9: ['test', 'av,Iv'],
  0xaa: ['stosb', ''], 0xab: ['stos*', ''], 0xac: ['lodsb', ''], 0xad: ['lods*', ''], 0xae: ['scasb', ''], 0xaf: ['scas*', ''],
  0xc0: ['#2', 'Eb,Ib'], 0xc1: ['#2', 'Ev,Ib'], 0xc2: ['ret', 'Iw'], 0xc3: ['ret', ''], 0xc4: ['les', 'Gv,M'], 0xc5: ['lds', 'Gv,M'],
  0xc6: ['#c6', 'Eb,Ib'], 0xc7: ['#c6', 'Ev,Iv'], 0xc8: ['enter', 'Iw,Ib'], 0xc9: ['leave', ''], 0xca: ['retf', 'Iw'], 0xcb: ['retf', ''],
  0xcc: ['int3', ''], 0xcd: ['int', 'Ib'], 0xce: ['into', ''], 0xcf: ['iret*', ''],
  0xd0: ['#2', 'Eb,1'], 0xd1: ['#2', 'Ev,1'], 0xd2: ['#2', 'Eb,cl'], 0xd3: ['#2', 'Ev,cl'], 0xd4: ['aam', 'Ib'], 0xd5: ['aad', 'Ib'],
  0xd6: ['salc', ''], 0xd7: ['xlatb', ''],
  0xe0: ['loopne', 'Jb'], 0xe1: ['loope', 'Jb'], 0xe2: ['loop', 'Jb'], 0xe3: ['jcxz', 'Jb'],
  0xe4: ['in', 'al,Ib'], 0xe5: ['in', 'av,Ib'], 0xe6: ['out', 'Ib,al'], 0xe7: ['out', 'Ib,av'],
  0xe8: ['call', 'Jv'], 0xe9: ['jmp', 'Jv'], 0xea: ['jmp', 'A'], 0xeb: ['jmp', 'Jb'],
  0xec: ['in', 'al,dx'], 0xed: ['in', 'av,dx'], 0xee: ['out', 'dx,al'], 0xef: ['out', 'dx,av'],
  0xf1: ['int1', ''], 0xf4: ['hlt', ''], 0xf5: ['cmc', ''], 0xf6: ['#3', 'Eb'], 0xf7: ['#3', 'Ev'],
  0xf8: ['clc', ''], 0xf9: ['stc', ''], 0xfa: ['cli', ''], 0xfb: ['sti', ''], 0xfc: ['cld', ''], 0xfd: ['std', ''],
  0xfe: ['#4', 'Eb'], 0xff: ['#5', 'Ev'],
});
for (let k = 0; k < 8; k++) {
  OP1[0x40 + k] = ['inc', 'Zv']; OP1[0x48 + k] = ['dec', 'Zv']; OP1[0x50 + k] = ['push', 'Zv']; OP1[0x58 + k] = ['pop', 'Zv'];
  OP1[0xb0 + k] = ['mov', 'Zb,Ib']; OP1[0xb8 + k] = ['mov', 'Zv,Iv'];
  if (k) OP1[0x90 + k] = ['xchg', 'av,Zv'];
}
for (let k = 0; k < 16; k++) OP1[0x70 + k] = ['j' + CC[k], 'Jb'];
// '*': the operand size picks the name (16/32 bits)
const WIDE = {
  'pusha*': ['pusha', 'pushad'], 'popa*': ['popa', 'popad'], 'ins*': ['insw', 'insd'], 'outs*': ['outsw', 'outsd'],
  'cbw*': ['cbw', 'cwde'], 'cwd*': ['cwd', 'cdq'], 'pushf*': ['pushf', 'pushfd'], 'popf*': ['popf', 'popfd'],
  'movs*': ['movsw', 'movsd'], 'cmps*': ['cmpsw', 'cmpsd'], 'stos*': ['stosw', 'stosd'], 'lods*': ['lodsw', 'lodsd'],
  'scas*': ['scasw', 'scasd'], 'iret*': ['iret', 'iretd'],
};
const STRING = new Set(['insb', 'insw', 'insd', 'outsb', 'outsw', 'outsd', 'movsb', 'movsw', 'movsd', 'cmpsb', 'cmpsw', 'cmpsd',
  'stosb', 'stosw', 'stosd', 'lodsb', 'lodsw', 'lodsd', 'scasb', 'scasw', 'scasd']);
const GROUP = {
  '#1': ALU, '#2': SHF, '#3': ['test', 'test', 'not', 'neg', 'mul', 'imul', 'div', 'idiv'],
  '#4': ['inc', 'dec'], '#5': ['inc', 'dec', 'call', 'call far', 'jmp', 'jmp far', 'push'], '#8f': ['pop'], '#c6': ['mov'],
};

// two-byte opcodes (0F xx)
const OP2 = {
  0x00: ['#6', 'Ew'], 0x01: ['#7', 'Ew'], 0x02: ['lar', 'Gv,Ew'], 0x03: ['lsl', 'Gv,Ew'], 0x06: ['clts', ''], 0x08: ['invd', ''],
  0x09: ['wbinvd', ''], 0x0b: ['ud2', ''], 0x20: ['mov', 'Rd,Cd'], 0x21: ['mov', 'Rd,Dd'], 0x22: ['mov', 'Cd,Rd'], 0x23: ['mov', 'Dd,Rd'],
  0x30: ['wrmsr', ''], 0x31: ['rdtsc', ''], 0x32: ['rdmsr', ''], 0xa2: ['cpuid', ''],
  0xa0: ['push', 'fs'], 0xa1: ['pop', 'fs'], 0xa3: ['bt', 'Ev,Gv'], 0xa4: ['shld', 'Ev,Gv,Ib'], 0xa5: ['shld', 'Ev,Gv,cl'],
  0xa8: ['push', 'gs'], 0xa9: ['pop', 'gs'], 0xab: ['bts', 'Ev,Gv'], 0xac: ['shrd', 'Ev,Gv,Ib'], 0xad: ['shrd', 'Ev,Gv,cl'],
  0xaf: ['imul', 'Gv,Ev'], 0xb0: ['cmpxchg', 'Eb,Gb'], 0xb1: ['cmpxchg', 'Ev,Gv'], 0xb2: ['lss', 'Gv,M'], 0xb3: ['btr', 'Ev,Gv'],
  0xb4: ['lfs', 'Gv,M'], 0xb5: ['lgs', 'Gv,M'], 0xb6: ['movzx', 'Gv,Eb!'], 0xb7: ['movzx', 'Gv,Ew!'], 0xba: ['#8', 'Ev,Ib'],
  0xbb: ['btc', 'Ev,Gv'], 0xbc: ['bsf', 'Gv,Ev'], 0xbd: ['bsr', 'Gv,Ev'], 0xbe: ['movsx', 'Gv,Eb!'], 0xbf: ['movsx', 'Gv,Ew!'],
  0xc0: ['xadd', 'Eb,Gb'], 0xc1: ['xadd', 'Ev,Gv'],
};
for (let k = 0; k < 16; k++) { OP2[0x80 + k] = ['j' + CC[k], 'Jv']; OP2[0x90 + k] = ['set' + CC[k], 'Eb']; }
for (let k = 0; k < 8; k++) OP2[0xc8 + k] = ['bswap', 'Zd'];
Object.assign(GROUP, {
  '#6': ['sldt', 'str', 'lldt', 'ltr', 'verr', 'verw'], '#7': ['sgdt', 'sidt', 'lgdt', 'lidt', 'smsw', null, 'lmsw', 'invlpg'],
  '#8': [null, null, null, null, 'bt', 'bts', 'btr', 'btc'],
});

// x87 (D8-DF): memory forms by [opcode - D8][reg] = [mnemonic, size]
const FMEM = [
  ['fadd', 'fmul', 'fcom', 'fcomp', 'fsub', 'fsubr', 'fdiv', 'fdivr'].map((n) => [n, 'dword']),
  [['fld', 'dword'], null, ['fst', 'dword'], ['fstp', 'dword'], ['fldenv', ''], ['fldcw', ''], ['fnstenv', ''], ['fnstcw', '']],
  ['fiadd', 'fimul', 'ficom', 'ficomp', 'fisub', 'fisubr', 'fidiv', 'fidivr'].map((n) => [n, 'dword']),
  [['fild', 'dword'], ['fisttp', 'dword'], ['fist', 'dword'], ['fistp', 'dword'], null, ['fld', 'tword'], null, ['fstp', 'tword']],
  ['fadd', 'fmul', 'fcom', 'fcomp', 'fsub', 'fsubr', 'fdiv', 'fdivr'].map((n) => [n, 'qword']),
  [['fld', 'qword'], ['fisttp', 'qword'], ['fst', 'qword'], ['fstp', 'qword'], ['frstor', ''], null, ['fnsave', ''], ['fnstsw', '']],
  ['fiadd', 'fimul', 'ficom', 'ficomp', 'fisub', 'fisubr', 'fidiv', 'fidivr'].map((n) => [n, 'word']),
  [['fild', 'word'], ['fisttp', 'word'], ['fist', 'word'], ['fistp', 'word'], ['fbld', 'tword'], ['fild', 'qword'], ['fbstp', 'tword'], ['fistp', 'qword']],
];
const D9X = { 0xd0: 'fnop', 0xe0: 'fchs', 0xe1: 'fabs', 0xe4: 'ftst', 0xe5: 'fxam', 0xe8: 'fld1', 0xe9: 'fldl2t', 0xea: 'fldl2e',
  0xeb: 'fldpi', 0xec: 'fldlg2', 0xed: 'fldln2', 0xee: 'fldz', 0xf0: 'f2xm1', 0xf1: 'fyl2x', 0xf2: 'fptan', 0xf3: 'fpatan',
  0xf4: 'fxtract', 0xf5: 'fprem1', 0xf6: 'fdecstp', 0xf7: 'fincstp', 0xf8: 'fprem', 0xf9: 'fyl2xp1', 0xfa: 'fsqrt',
  0xfb: 'fsincos', 0xfc: 'frndint', 0xfd: 'fscale', 0xfe: 'fsin', 0xff: 'fcos' };
function x87reg(op, m) {
  const i = m & 7, st = 'st' + i, r = (m >> 3) & 7;
  switch (op) {
    case 0xd8: return [['fadd', 'fmul', 'fcom', 'fcomp', 'fsub', 'fsubr', 'fdiv', 'fdivr'][r], st];
    case 0xd9: return r === 0 ? ['fld', st] : r === 1 ? ['fxch', st] : D9X[m] ? [D9X[m], ''] : null;
    case 0xda: return m === 0xe9 ? ['fucompp', ''] : r < 4 ? [['fcmovb', 'fcmove', 'fcmovbe', 'fcmovu'][r], st] : null;
    case 0xdb: return m === 0xe2 ? ['fnclex', ''] : m === 0xe3 ? ['fninit', ''] : m === 0xe0 ? ['fneni', ''] : m === 0xe1 ? ['fndisi', ''] : m === 0xe4 ? ['fnsetpm', ''] : null;
    case 0xdc: return [['fadd', 'fmul', 'fcom', 'fcomp', 'fsubr', 'fsub', 'fdivr', 'fdiv'][r], r === 2 || r === 3 ? st : st + ',st0'];
    case 0xdd: return [['ffree', null, 'fst', 'fstp', 'fucom', 'fucomp', null, null][r], st];
    case 0xde: return m === 0xd9 ? ['fcompp', ''] : r === 2 || r === 3 ? null : [['faddp', 'fmulp', null, null, 'fsubrp', 'fsubp', 'fdivrp', 'fdivp'][r], st];
    case 0xdf: return m === 0xe0 ? ['fnstsw', 'ax'] : null;
  }
  return null;
}

const h = (v) => '0x' + (v >>> 0).toString(16);
const h4 = (v) => '0x' + (v & 0xffff).toString(16).padStart(4, '0');
const sh = (v) => (v < 0 ? '-' : '') + h(Math.abs(v));

export function disasm86(read, ip) {
  let p = ip & 0xffff, len = 0;
  const byte = () => { const b = read(p) & 0xff; p = (p + 1) & 0xffff; len++; return b; };
  const s8 = () => (byte() << 24) >> 24;
  const u16 = () => byte() | (byte() << 8);
  const u32 = () => (byte() | (byte() << 8) | (byte() << 16) | (byte() << 24)) >>> 0;
  let seg = null, o32 = false, a32 = false, rep = 0, lock = false, prefixes = 0, segUsed = false;
  let op, oUsed = false;
  for (;;) {
    op = byte();
    if (SEGP[op]) seg = SEGP[op];
    else if (op === 0x66) o32 = true;
    else if (op === 0x67) a32 = true;
    else if (op === 0xf2 || op === 0xf3) rep = op;
    else if (op === 0xf0) lock = true;
    else break;
    if (++prefixes >= 14) break;
  }
  const bad = () => ({ len: 1, mnem: 'db', ops: h((read(ip & 0xffff) & 0xff)), text: 'db ' + h(read(ip & 0xffff) & 0xff) });

  let ent, two = false;
  if (op === 0x0f) { two = true; op = byte(); ent = OP2[op]; }
  else ent = OP1[op];
  let modrm = -1, mod = 0, reg = 0, rm = 0, memText = null, mnem = '', spec = '';
  if (!two && op >= 0xd8 && op <= 0xdf) return x87(op);
  if (!ent) return bad();
  [mnem, spec] = ent;

  // ModRM
  const needModrm = /E|G|M|Sw|R|C|D/.test(spec) || mnem[0] === '#';
  if (needModrm) {
    modrm = byte(); mod = modrm >> 6; reg = (modrm >> 3) & 7; rm = modrm & 7;
    if (mod !== 3) memText = ea();
  }
  function ea() {
    let base, disp = 0, hasDisp = false;
    if (!a32) {
      if (mod === 0 && rm === 6) { disp = u16(); return mem(null, h(disp)); }
      base = EA16[rm];
      if (mod === 1) { disp = s8(); hasDisp = true; } else if (mod === 2) { disp = (u16() << 16) >> 16; hasDisp = true; }
    } else {
      let r = rm;
      if (r === 4) {
        const sib = byte(), ss = sib >> 6, idx = (sib >> 3) & 7, b = sib & 7;
        const parts = [];
        if (b === 5 && mod === 0) { disp = u32() | 0; hasDisp = true; } else parts.push(R32[b]);
        if (idx !== 4) parts.push(R32[idx] + (ss ? '*' + (1 << ss) : ''));
        base = parts.join('+');
      } else if (r === 5 && mod === 0) { disp = u32(); return mem(null, h(disp)); }
      else base = R32[r];
      if (mod === 1) { disp = s8(); hasDisp = true; } else if (mod === 2) { disp = u32() | 0; hasDisp = true; }
    }
    let t = base || '';
    if (hasDisp) t += (t ? (disp < 0 ? '-' : '+') + h(Math.abs(disp)) : h(disp));
    return mem(null, t);
  }
  function mem(_, t) { segUsed = !!seg; return '[' + (seg ? seg + ':' : '') + t + ']'; }

  // groups
  if (mnem[0] === '#') {
    const g = GROUP[mnem];
    mnem = g[reg];
    if (!mnem) return bad();
    if (ent[0] === '#3' && reg <= 1) spec += (spec === 'Eb' ? ',Ib' : ',Iv');
    if ((ent[0] === '#5' && (reg === 3 || reg === 5)) && mod === 3) return bad();
    if ((ent[0] === '#8f' || ent[0] === '#c6') && reg !== 0) return bad();
    if (ent[0] === '#4' && reg > 1) return bad();
  }
  if (mnem.endsWith('*')) { mnem = WIDE[mnem][o32 ? 1 : 0]; oUsed = true; }
  if (/v|J|A|Zd/.test(spec)) oUsed = true;
  if (op === 0xe3 && !two && a32) mnem = 'jecxz';
  if ((op === 0xd4 || op === 0xd5) && !two) { const b = byte(); return fin(b === 10 ? '' : h(b)); }
  if (spec === 'M' || spec.endsWith(',M')) { if (mod === 3) return bad(); }

  // operands
  const vsz = o32 ? 32 : 16;
  const hasReg = /G|Z|av|^al|,al|Sw|R|C|D/.test(spec);
  const ops = [];
  let target;
  for (const s of spec ? spec.split(',') : []) {
    const k = s[0], w = s[1];
    const size = w === 'b' ? 8 : w === 'w' ? 16 : w === 'd' ? 32 : vsz;
    const regs = size === 8 ? R8 : size === 16 ? R16 : R32;
    if (k === 'E') {
      if (mod === 3) ops.push(regs[rm]);
      else {
        let kw = '';
        if (s.endsWith('!') || !hasReg) kw = (size === 8 ? 'byte ' : size === 16 ? 'word ' : 'dword ');
        if (mnem === 'call far' || mnem === 'jmp far') kw = '';
        ops.push(kw + memText);
      }
    } else if (k === 'G') ops.push(regs[reg]);
    else if (k === 'M') ops.push(memText);
    else if (k === 'S') ops.push(w === 'w' ? SREG[reg] : sh(s8()));
    else if (k === 'I') {
      if (w === 'b') ops.push(h(byte()));
      else if (w === 'w') ops.push(h(u16()));
      else ops.push(h(o32 ? u32() : u16()));
    } else if (k === 'J') {
      const d = w === 'b' ? s8() : o32 ? u32() | 0 : (u16() << 16) >> 16;
      target = (p + d) & 0xffff;
      ops.push(h4(target));
    } else if (k === 'A') {
      const off = o32 ? u32() : u16(), sg = u16();
      ops.push(h4(sg) + ':' + (o32 ? h(off) : h4(off)));
    } else if (k === 'O') {
      const off = a32 ? u32() : u16();
      ops.push(mem(null, h(off)));
    } else if (k === 'Z') ops.push((size === 8 ? R8 : size === 32 || w === 'd' ? R32 : regs)[op & 7]);
    else if (k === 'a') ops.push(w === 'v' ? (o32 ? 'eax' : 'ax') : 'al');
    else if (k === 'R') ops.push(R32[rm]);
    else if (k === 'C') ops.push('cr' + reg);
    else if (k === 'D') ops.push('dr' + reg);
    else ops.push(s);                                     // literal: al, cl, dx, 1, es, fs...
  }
  if (mnem === 'bswap' && !o32) return bad();
  return fin(ops.join(','), target);

  function fin(opsText, tgt) {
    // prefixes the instruction does not use are shown (as ndisasm does)
    let pre = '';
    if (seg && !segUsed) pre += seg + ' ';
    if (lock) pre += 'lock ';
    if (rep) pre += rep === 0xf2 ? 'repne ' : STRING.has(mnem) && /^(cmps|scas)/.test(mnem) ? 'repe ' : 'rep ';
    if (o32 && !oUsed) pre += 'o32 ';
    if (a32 && !memText && !/^(jcxz|jecxz|loop)/.test(mnem) && !/O/.test(spec)) pre += 'a32 ';
    const m = pre + mnem;
    const r = { len, mnem: m, ops: opsText, text: opsText ? m + ' ' + opsText : m };
    if (tgt !== undefined) r.target = tgt;
    return r;
  }

  function x87(esc) {
    modrm = byte(); mod = modrm >> 6; reg = (modrm >> 3) & 7;
    if (mod !== 3) {
      rm = modrm & 7; memText = ea();
      const e = FMEM[esc - 0xd8][reg];
      if (!e) return bad();
      return finX(e[0], (e[1] ? e[1] + ' ' : '') + memText);
    }
    const e = x87reg(esc, modrm);
    if (!e || !e[0]) return bad();
    return finX(e[0], e[1]);
  }
  function finX(m, o) { mnem = m; return fin(o); }
}
