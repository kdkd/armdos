// Hot arithmetic paths checked against independent integer results. Every
// template is called 80 times so the same cases cover cold and translated code.
// Uses the normal headless DOS fixture and NASM, including on an ARM Mac.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { session, ROOT } from '../../dosutil/tests/harness.mjs';

const out = path.join(ROOT, 'build/x86-jitmath');
fs.mkdirSync(out, { recursive: true });
const u = (n, bits = 32) => BigInt.asUintN(bits, BigInt(n));
const signed = (n, bits) => BigInt.asIntN(bits, BigInt(n));
const merge = (old, n, bits) => u((u(old) & ~((1n << BigInt(bits)) - 1n)) | u(n, bits));
const tests = [];
const add = (code, result) => tests.push({ code, result });
for (const bits of [8, 16, 32]) for (const sign of [false, true]) {
  const a = bits === 8 ? 'al' : bits === 16 ? 'ax' : 'eax';
  const b = bits === 8 ? 'bl' : bits === 16 ? 'bx' : 'ebx';
  const d = bits === 8 ? 'ah' : bits === 16 ? 'dx' : 'edx';
  for (const source of [b, a, d, `${bits === 8 ? 'byte' : bits === 16 ? 'word' : 'dword'} [si+4]`]) {
    add(`${sign ? 'imul' : 'mul'} ${source}`, (r) => {
      const rhs = source === a ? u(r[0], bits) : source === d ? (bits === 8 ? u(r[0] >> 8n, 8) : u(r[3], bits)) : u(r[1], bits);
      const lhs = u(r[0], bits);
      const p = sign ? signed(lhs, bits) * signed(rhs, bits) : lhs * rhs;
      r[0] = merge(r[0], p, bits === 8 ? 16 : bits);
      if (bits !== 8) r[3] = merge(r[3], p >> BigInt(bits), bits);
    });
  }
}
for (const bits of [16, 32]) {
  const a = bits === 16 ? 'ax' : 'eax', b = bits === 16 ? 'bx' : 'ebx';
  const capture = bits === 16 ? 'movzx edi, ax' : 'mov edi, eax';
  for (const source of [a, b, `${bits === 16 ? 'word' : 'dword'} [si+4]`]) {
    add(`imul ${a}, ${source}\n ${capture}`, (r) => {
      r[0] = merge(r[0], u(r[0], bits) * u(source === a ? r[0] : r[1], bits), bits);
      r[4] = u(r[0], bits);
    });
    for (const imm of [-128, -7, 0, 127, 12345, -32768]) {
      add(`imul ${a}, ${source}, ${imm}\n ${capture}`, (r) => {
        r[0] = merge(r[0], u(source === a ? r[0] : r[1], bits) * BigInt(imm), bits);
        r[4] = u(r[0], bits);
      });
    }
  }
  for (const right of [false, true]) for (const alias of [false, true]) {
    for (const raw of [0, 1, 7, 14, 15, 16, 17, 31, 32, 33]) {
      const count = raw & 31;
      // Counts greater than a 16-bit operand are undefined on x86. Keep the
      // existing helper's behavior covered by the differential CPU suite.
      if (count > bits) continue;
      add(`${right ? 'shrd' : 'shld'} ${a}, ${alias ? a : b}, ${raw}\n ${capture}`, (r) => {
        const dst = u(r[0], bits), src = u(alias ? r[0] : r[1], bits), n = BigInt(count), width = BigInt(bits);
        const v = !count ? dst : right ? (dst >> n) | (src << (width - n)) : (dst << n) | (src >> (width - n));
        r[0] = merge(r[0], v, bits);
        r[4] = u(r[0], bits);
      });
    }
  }
  for (const op of ['shld', 'shrd']) for (const count of [0, 32]) {
    add(`stc\n ${op} ${a}, ${b}, ${count}\n adc edi, 0`, (r) => { r[4]++; });
    // Preserve pending lazy flags too, not only the eager flags set by STC.
    add(`add edx, ebx\n ${op} ${a}, ${b}, ${count}\n adc edi, 0`, (r) => {
      const sum = r[3] + r[1];
      r[3] = u(sum); r[4] += sum >> 32n;
    });
  }
}
// A 32-bit shift by 16 must participate in flag liveness as a 32-bit shift.
add('add ax, bx\n adc dx, cx\n shl eax, 16', (r) => {
  const sum = u(r[0], 16) + u(r[1], 16);
  r[3] = merge(r[3], u(r[3], 16) + u(r[2], 16) + (sum >> 16n), 16);
  r[0] = u(merge(r[0], sum, 16) << 16n);
});
add('shl eax, 16\n adc edx, ecx', (r) => {
  r[3] = u(r[3] + r[2] + ((r[0] >> 16n) & 1n));
  r[0] = u(r[0] << 16n);
});

const edge = [0n, 1n, 0x7fn, 0x80n, 0xffn, 0x7fffn, 0x8000n, 0xffffn,
  0x7fffffffn, 0x80000000n, 0xffffffffn, 0x12345678n, 0xffff8000n, 0xaaaa5555n];
let seed = 0x1988;
const random = () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return BigInt(seed); };
const values = Array.from({ length: 80 }, (_, i) => Array.from({ length: 4 }, (_, j) => i < 28 ? edge[(i + j * 3) % edge.length] : random()));
const hex = (x) => '0x' + u(x).toString(16);
const groups = [];
// Twenty templates keep the COM's input and reference tables below 64 KB.
for (let first = 0; first < tests.length; first += 20) {
  const group = tests.slice(first, first + 20), name = `JM${groups.length}`;
  const asm = ['bits 16', 'cpu 386', 'org 100h', 'mov si, cases', `mov bp, ${group.length * values.length}`,
    'next:', 'mov eax, [si]', 'mov ebx, [si+4]', 'mov ecx, [si+8]', 'mov edx, [si+12]', 'mov edi, 0x13572468',
    'call [si+36]', ...['eax', 'ebx', 'ecx', 'edx', 'edi'].flatMap((r, i) => [`cmp ${r}, [si+${16 + i * 4}]`, 'jne bad']),
    'add si, 38', 'dec bp', 'jnz next', 'mov dx, ok', 'mov ah, 9', 'int 21h', 'mov ax, 0x4c00', 'int 21h',
    'bad:', 'mov dx, fail', 'mov ah, 9', 'int 21h', 'mov ax, 0x4c01', 'int 21h',
    "ok db 'JIT MATH OK',13,10,'$'", "fail db 'JIT MATH BAD',13,10,'$'"];
  group.forEach((test, i) => asm.push(`op${i}:`, test.code, 'cmp bp, bp', 'ret'));
  asm.push('align 4', 'cases:');
  group.forEach((test, i) => values.forEach((v) => {
    const want = [...v, 0x13572468n];
    test.result(want);
    asm.push('dd ' + [...v, ...want].map(hex).join(','), `dw op${i}`);
  }));
  fs.writeFileSync(path.join(out, `${name}.asm`), asm.join('\n') + '\n');
  execFileSync('nasm', ['-f', 'bin', '-o', path.join(out, `${name}.COM`), path.join(out, `${name}.asm`)]);
  groups.push(name);
}
const files = [{ src: process.env.ELBOW || 'build/ELBOW.EXE', dst: 'DOS\\ELBOW.EXE' }, { src: 'build/HIMEM.SYS', dst: 'DOS\\' },
  ...groups.map((n) => ({ src: `build/x86-jitmath/${n}.COM`, dst: 'WORK\\' }))];
const s = await session({ name: 'x86-jitmath', files, config: 'DEVICE=C:\\DOS\\HIMEM.SYS\nFILES=20\nSHELL=C:\\T\\TSHELL.EXE\n' });
for (const mode of ['/NOJIT', '/JIT']) for (const n of groups) {
  const lines = s.run(`\\DOS\\ELBOW ${mode} /STATS \\WORK\\${n}.COM`, { timeoutMs: 60000 });
  assert(lines.includes('JIT MATH OK'), `${mode} ${n}: ${lines.join('\n')}`);
  if (mode === '/JIT') assert(lines.some((l) => /[1-9][0-9]* blocks/.test(l)), `no translated blocks in ${n}`);
  console.log(`ok ${mode} ${n}`);
}
assert.equal(s.pc.faults.length, 0, JSON.stringify(s.pc.faults));
console.log(`JIT math: ${tests.length * values.length} integer reference cases per engine passed`);
