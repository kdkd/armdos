// x86state.mjs - read ELBOW.EXE's emulated-CPU state out of the ARM machine (debug aid)
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { ROOT } from '../../dosutil/tests/harness.mjs';

const B = (p) => path.join(ROOT, 'build', p);
let symsCache;
function syms() {
  if (!symsCache) {
    symsCache = new Map();
    for (const l of execFileSync('arm-none-eabi-nm', ['--defined-only', B('obj/ELBOW/ELBOW.elf')]).toString().split('\n')) {
      const p = l.split(' ');
      if (p.length === 3) symsCache.set(p[2], parseInt(p[0], 16));
    }
  }
  return symsCache;
}
export function x86base(pc) {
  const exe = fs.readFileSync(B('ELBOW.EXE'));
  const imgOff = exe.readUInt32LE(0x80 + 8);
  const sig = exe.subarray(imgOff, imgOff + 64);
  const m8 = pc.cpu.m8;
  let base = -1;
  for (let a = 0x600; a < 0xA0000; a += 16) if (Buffer.from(m8.subarray(a, a + 64)).equals(sig)) base = a;
  return base;
}
export function x86state(pc) {
  const base = x86base(pc);
  if (base < 0) return 'ELBOW.EXE not in memory';
  const S = syms();
  const m8 = pc.cpu.m8;
  const dv = new DataView(m8.buffer, m8.byteOffset);
  const c = base + S.get('cpu');
  const u32 = (a) => dv.getUint32(a, true), u16 = (a) => dv.getUint16(a, true);
  const names = ['EAX', 'ECX', 'EDX', 'EBX', 'ESP', 'EBP', 'ESI', 'EDI'];
  const r = names.map((n, i) => `${n}=${u32(c + i * 4).toString(16).padStart(8, '0')}`).join(' ');
  const eip = u32(c + 32), flags = u32(c + 36);
  // sreg at offset: e[8](32) eip flags lf_op lf_a lf_b lf_res (6*4=24 -> 56) sbase[8] (32 -> 88) sreg[8]
  const sreg = [0, 1, 2, 3, 4, 5].map((i) => u16(c + 88 + i * 2));
  const memp = u32(base + S.get('mem'));
  const lin = (sreg[1] << 4) + (eip & 0xFFFF);
  const code = [...m8.subarray(memp + lin, memp + lin + 16)].map((b) => b.toString(16).padStart(2, '0')).join(' ');
  const g = (n) => S.has(n) ? u32(base + S.get(n)) : 'n/a';
  const extra = `jit_enabled=${g('jit_enabled')} icount=${g('cpu')} irq_pending=${g('irq_pending').toString(16)} x86_active=${g('x86_active')} kbd_latched=${g('kbd_latched')} kbq_head=${g('kbq_head')} kbq_tail=${g('kbq_tail')} BDA kb head=${u16(0x41A).toString(16)} tail=${u16(0x41C).toString(16)} flags=${m8[0x417].toString(16)}`;
  return `${extra}\n${r}\nEIP=${eip.toString(16)} FLAGS=${flags.toString(16)} lf_op=${u32(c + 40)} ES=${sreg[0].toString(16)} CS=${sreg[1].toString(16)} SS=${sreg[2].toString(16)} DS=${sreg[3].toString(16)} FS=${sreg[4].toString(16)} GS=${sreg[5].toString(16)}\ncode: ${code}\nmem=${memp.toString(16)}`;
}
export function x86mem(pc, lin, n) {
  const base = x86base(pc);
  const memp = new DataView(pc.cpu.m8.buffer, pc.cpu.m8.byteOffset).getUint32(base + syms().get('mem'), true);
  return Buffer.from(pc.cpu.m8.subarray(memp + lin, memp + lin + n));
}
export function x86ring(pc) {
  const base = x86base(pc);
  const S = syms();
  const dv = new DataView(pc.cpu.m8.buffer, pc.cpu.m8.byteOffset);
  const r = base + S.get('ring'), pos = dv.getUint32(base + S.get('ring_pos'), true);
  const out = [];
  for (let i = 0; i < 64; i++) {
    const k = ((pos + i) & 63) * 16;
    const a = dv.getUint32(r + k, true), f = dv.getUint32(r + k + 4, true), ax = dv.getUint32(r + k + 8, true), sp = dv.getUint32(r + k + 12, true);
    out.push(`${(a >>> 16).toString(16).padStart(4, '0')}:${(a & 0xFFFF).toString(16).padStart(4, '0')} f=${f.toString(16)} AX=${(ax & 0xFFFF).toString(16)} SI=${(ax >>> 16).toString(16)} SP=${(sp & 0xFFFF).toString(16)} op=${((sp >>> 16) & 0xFF).toString(16)} ${(sp >>> 24).toString(16)}`);
  }
  return out.join('\n');
}
