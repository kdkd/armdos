#!/usr/bin/env node
// elbowview.mjs - the inspector's ELBOW view (web/js/elbow-panel.js):
//  1. web/js/x86disasm.js against ndisasm -b16 on real x86 binaries (a linear
//     sweep of each program's code: every instruction both decode at the same
//     offset must have the same length and, normalised, the same text);
//  2. ELBOW's descriptor (ports FCh-FFh, ARCH.md 4.7) read the way the page
//     reads it (web/js/elbow-probe.js) while ELBOW runs an x86 program headless.
//   node apps/x86/tests/elbowview.mjs [--disasm-only] [-v]
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { disasm86 } from '../../../web/js/x86disasm.js';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const verbose = process.argv.includes('-v');
let fails = 0, passes = 0;
const check = (name, ok, extra = '') => { if (ok) passes++; else fails++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${extra ? '  (' + extra + ')' : ''}`); };

// ------------------------------------------------------------------ 1. disassembler
// normalise both sides: size/distance keywords dropped (ndisasm prints them in
// more places than we do), numbers compared as 32-bit values (this ndisasm
// sign-extends imm8 to 64 bits), ndisasm's "shl ax,0x0" for D1 /4 is "shl ax,1"
const PFX = { es: 0x26, cs: 0x2e, ss: 0x36, ds: 0x3e, fs: 0x64, gs: 0x65, o32: 0x66, a32: 0x67, lock: 0xf0, repne: 0xf2, rep: 0xf3 };
function norm(t) {
  if (PFX[t.trim()]) return 'db #' + PFX[t.trim()].toString(16);     // a prefix before something invalid
  t = t.replace('{pt} ', 'ds ').replace('{pn} ', 'cs ');               // (branch hints)
  // ndisasm's names for later CPUs' readings of old prefixes, and its o32 forms
  t = t.replace(/\b(bnd|xacquire) /, 'repne ').replace(/\bxrelease /, 'rep ').replace(/,ecx$/, '');
  t = t.replace(/\b(leave|ret|retf|enter)d\b/, 'o32 $1');
  t = t.replace(/^(?:(?:es|cs|ss|ds|fs|gs) )+(loop|loope|loopne|jcxz|jecxz) /, '$1 ');   // (ndisasm drops those)
  t = t.toLowerCase().replace(/\b(byte|word|dword|qword|tword|near|short)\s+/g, '');
  t = t.replace(/(rol|ror|rcl|rcr|shl|shr|sal|sar) (.*),0x0$/, '$1 $2,1');
  t = t.replace(/([+-]?)0x([0-9a-f]+)/g, (_, sg, x) => {
    let v = BigInt('0x' + x) & 0xFFFFFFFFn;
    if (sg === '-') v = (-v) & 0xFFFFFFFFn;
    return '#' + v.toString(16);
  });
  return t.replace(/\s+/g, ' ').trim();
}
function codeOf(file) {
  const b = fs.readFileSync(file);
  if (b.length > 0x1C && b[0] === 0x4D && b[1] === 0x5A) {
    const hdr = b.readUInt16LE(8) * 16;
    return b.subarray(hdr, Math.min(b.length, hdr + 0x10000));
  }
  return b.subarray(0, 0x10000);
}
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'elbowview-'));
const FILES = [
  'apps/x86/demo/msdos20/DEBUG.COM', 'apps/x86/demo/msdos20/EDLIN.COM', 'apps/x86/demo/msdos20/MASM.EXE', 'apps/x86/demo/msdos20/LINK.EXE',
  'apps/x86/demo/freedos/TREE.COM', 'apps/x86/demo/freedos/CHOICE.EXE', 'apps/x86/demo/BENCH86.EXE',
  'apps/x86/demo/apps/SOPWITH/SOPWITH.EXE', 'apps/x86/demo/apps/BASIC/GWBASIC.EXE', 'apps/x86/demo/apps/SC/SC.EXE',
  '3rdparty/secondreality/SECOND.EXE', '3rdparty/secondreality/START.EXE', '3rdparty/secondreality/GLENZ.EXE',
  '3rdparty/secondreality/PAM.EXE', '3rdparty/secondreality/TECHNO.EXE', '3rdparty/secondreality/U2A.EXE',
];
let total = 0, same = 0, lenBad = 0, textBad = 0, x87 = 0, wait = 0, refused = 0;
const samples = [];
for (const f of FILES) {
  const full = path.join(ROOT, f);
  if (!fs.existsSync(full)) { console.log(`skip ${f} (missing)`); continue; }
  const code = codeOf(full), bin = path.join(tmp, 'code.bin');
  fs.writeFileSync(bin, code);
  // (ndisasm continues a long instruction's bytes on a "-XX" line)
  const nd = new Map();
  let lastOff = -1;
  for (const l of execFileSync('ndisasm', ['-b16', bin], { maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] }).toString('latin1').split('\n')) {
    const m = l.match(/^([0-9A-F]{8})\s+([0-9A-F]+)\s+(.*)$/);
    if (m) { lastOff = parseInt(m[1], 16); nd.set(lastOff, { len: m[2].length / 2, text: m[3] }); }
    else { const c = l.match(/^\s+-([0-9A-F]+)$/); if (c && lastOff >= 0) nd.get(lastOff).len += c[1].length / 2; }
  }
  const rd = (o) => (o < code.length ? code[o] : 0);
  let ip = 0, fTotal = 0, fSame = 0;
  while (ip < code.length) {
    const d = disasm86(rd, ip);
    if (ip + d.len > code.length) break;
    const n = nd.get(ip);
    if (n) {
      fTotal++;
      if (n.len !== d.len) {
        // ndisasm merges WAIT into the x87 instruction after it (FSTSW = 9B DF E0); it refuses
        // encodings a real 8086/386 runs (82h, the D0-D3 /6 and F6/F7 /1 aliases, LOCK on any
        // instruction, stray prefixes) and knows later ones (MMX, ...) that we print as db
        if (/^(f|wait)/.test(n.text) || /^wait/.test(d.text)) wait++;
        else if (/^db /.test(n.text) || PFX[n.text.trim()] || (/^db /.test(d.text) && d.len === 1)) refused++;
        else { lenBad++; if (samples.length < 40) samples.push(`${f} ${ip.toString(16)}: len ${d.len} "${d.text}" vs ndisasm ${n.len} "${n.text}"`); }
      } else if (norm(d.text) === norm(n.text)) { fSame++; if (/^f/.test(d.mnem)) x87++; }
      else { textBad++; if (samples.length < 40 || verbose) samples.push(`${f} ${ip.toString(16)}: "${d.text}" vs ndisasm "${n.text}"`); }
    }
    ip += d.len;
  }
  total += fTotal; same += fSame;
  if (verbose) console.log(`  ${f}: ${fSame}/${fTotal}`);
}
fs.rmSync(tmp, { recursive: true, force: true });
for (const s of samples) console.log('   ', s);
const compared = same + lenBad + textBad, rate = same / Math.max(1, compared);
check(`x86 disassembler vs ndisasm: ${same}/${compared} instructions identical (${(rate * 100).toFixed(3)}%, ${x87} of them x87): ${lenBad} length and ${textBad} text differences; not compared: ${wait} WAIT+x87 pairs, ${refused} that ndisasm refuses (82h, aliases, stray prefixes) or reads as post-386`, compared > 200000 && rate > 0.999);

// fixed cases (the 386 real-mode instructions ELBOW translates)
const one = (hexs, ip = 0x100) => { const b = Buffer.from(hexs.replace(/\s+/g, ''), 'hex'); return disasm86((o) => b[(o - ip) & 0xFFFF] ?? 0, ip); };
for (const [hx, want, n] of [
  ['66 0f b6 c3', 'movzx eax,bl'], ['0f bf 47 02', 'movsx ax,word [bx+0x2]'], ['66 0f af c3', 'imul eax,ebx'],
  ['66 0f a4 c2 04', 'shld edx,eax,0x4'], ['0f ac d0 03', 'shrd ax,dx,0x3'], ['0f 8c fc ff', 'jl 0x0100'],
  ['0f 95 c1', 'setnz cl'], ['0f b4 1e 00 02', 'lfs bx,[0x200]'], ['0f a1', 'pop fs'], ['0f a8', 'push gs'],
  ['64 8b 05', 'mov ax,[fs:di]'], ['f3 66 a5', 'rep movsd'], ['66 c1 e0 10', 'shl eax,0x10'], ['67 8b 04 58', 'mov ax,[eax+ebx*2]'],
  ['66 0f c8', 'bswap eax'], ['f6 c4 80', 'test ah,0x80'], ['83 7e fe ff', 'cmp word [bp-0x2],-0x1'], ['eb fe', 'jmp 0x0100'],
  ['9a 34 12 00 f0', 'call 0xf000:0x1234'], ['ff 1e 00 03', 'call far [0x300]'], ['0f ff', 'db 0xf', 1], ['d9 ee', 'fldz'],
]) { const d = one(hx); check(`${hx.padEnd(16)} -> ${want}`, d.text === want && d.len === (n || hx.split(' ').length), `${d.text} / ${d.len}`); }

if (process.argv.includes('--disasm-only')) { console.log(`\nelbowview: ${fails ? fails + ' failed' : 'all ' + passes + ' passed'}`); process.exit(fails ? 1 : 0); }

// ------------------------------------------------------------------ 2. descriptor
const { session } = await import('../../dosutil/tests/harness.mjs');
const { readElbow, probeElbow, blockDetail } = await import('../../../web/js/elbow-probe.js');
const { disasmArm } = await import('../../../emu/disasm.mjs');
process.chdir(ROOT);
const files = [{ src: 'build/ELBOW.EXE', dst: 'DOS\\' }, { src: 'build/elbow/fire.COM', dst: 'X\\FIRE.COM' }];
const s = await session({ name: 'x86ev', files, dirs: ['X'] });
s.pc.type('CD \\X\r'); s.waitPrompt();
const m = s.pc.machine;
check('no descriptor before ELBOW runs', m.elbowDesc === 0 && readElbow(m) === null);
s.pc.type('\\DOS\\ELBOW FIRE.COM\r');
s.pc.run(2000);
const E = readElbow(m);
check('ELBOW announces its descriptor (ports FCh-FFh)', !!E && E.version === 1, E ? `at 0x${m.elbowDesc.toString(16)}` : `port 0x${m.elbowDesc.toString(16)}`);
if (E) {
  check('descriptor: code cache inside ELBOW\'s heap, block area before the stubs', E.cache > E.imageEnd && E.stubs > E.cache && E.cacheEnd > E.stubs, `cache 0x${E.cache.toString(16)} image 0x${E.image.toString(16)}-0x${E.imageEnd.toString(16)}`);
  const blocks = E.blocks();
  check('blocks translated', blocks.length > 0 && blocks.every((b) => b.code >= E.cache && b.end > b.code && b.end <= E.stubs), `${blocks.length} blocks`);
  // sample the ARM PC: most samples of this loop are in a translated block
  let inBlock = 0, sampled = 0, sawLoop = false, other = {}, detail = null;
  for (let i = 0; i < 400; i++) {
    m.runFor(0.29); sampled++;
    const p = probeElbow(m);
    if (p.kind === 'block') {
      inBlock++;
      const d = blockDetail(p.E, p.block, disasmArm, p.pc);
      // FIRE's flame loop: pixels averaged and stored back to A000h
      if (d.x86.some((l) => /^(mov|stosb)/.test(l.mnem)) && d.arm.some((l) => l.cur) && d.x86.every((l) => !/^db /.test(l.text))) { sawLoop = true; detail ||= d; }
    } else other[p.kind] = (other[p.kind] || 0) + 1;
  }
  check('probe: the ARM PC is in translated code or a helper it called', inBlock + (other.helper || 0) + (other.stub || 0) > sampled * 0.9 && inBlock > sampled / 10, `${inBlock}/${sampled} in blocks; ${JSON.stringify(other)}`);
  check('probe: blocks decode as x86 (no db) with the PC inside their ARM code', sawLoop);
  const p = (() => { for (let i = 0; i < 200; i++) { m.runFor(0.29); const q = probeElbow(m); if (q.kind === 'block') return { ...q, block: blockDetail(q.E, q.block, disasmArm, q.pc) }; } return null; })();
  if (p && verbose) { console.log(p.block.x86.map((l) => `  ${l.cs.toString(16)}:${l.ip.toString(16)} ${l.text}`).join('\n')); console.log(p.block.arm.map((l) => `  ${l.addr.toString(16)} ${l.text}  ${l.note}`).join('\n')); }
  check('probe: ARM listing covers the block with the PC inside', !!p && p.block.arm.length === (p.block.end - p.block.code) / 4 && p.block.arm.some((l) => l.cur));
}
s.pc.type('x');
s.waitPrompt(20000);
check('ELBOW clears the descriptor when it ends', m.elbowDesc === 0 && readElbow(m) === null, `port 0x${m.elbowDesc.toString(16)}`);
// the interpreter alone: CS:IP from the x86 CPU state
s.pc.type('\\DOS\\ELBOW /NOJIT FIRE.COM\r');
s.pc.run(2000);
{
  const E2 = readElbow(m), kinds = {};
  let cs = -1;
  for (let i = 0; i < 100; i++) { m.runFor(0.29); const p = probeElbow(m); kinds[p.kind] = (kinds[p.kind] || 0) + 1; if (p.kind === 'interp') cs = p.cs; }
  check('/NOJIT: no translator, the probe sees the x86 interpreter', !!E2 && !E2.jit && (kinds.interp || 0) > 80 && cs > 0 && cs < 0xA000, `${JSON.stringify(kinds)} CS=${cs.toString(16)}`);
}
s.pc.type('x');
check('... and the descriptor is gone again', s.waitPrompt(20000) && m.elbowDesc === 0);
console.log(`\nelbowview: ${fails ? fails + ' failed' : 'all ' + passes + ' passed'}`);
process.exit(fails ? 1 : 0);
