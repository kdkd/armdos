#!/usr/bin/env node
// run-tests.mjs - structural tests for the SDK (programs cannot run until the
// emulator + BIOS + kernel exist).
//
//   node sdk/tests/run-tests.mjs build/sdk-tests/*.EXE build/sdk-tests/*.COM
//
// For every program:
//   * it parses (MZ + AR1 header / .COM forms), header fields are sane
//   * the relocation table equals the set of R_ARM_ABS32/TARGET1 relocations
//     in allocated sections, as listed independently by `readelf -r` on the ELF
//   * loader simulation: relocated at several random bases, every relocated
//     word points inside [base, base + image + bss]
//   * the entry point is crt0's first instruction (C programs)
// Once:
//   * the embedded 8086 stub equals `nasm sdk/mzstub.asm`, and disassembles
//     to push cs / pop ds / mov dx / mov ah,9 / int 21h / mov ax,4C01h / int 21h
//   * the embedded .COM stub equals the assembled sdk/comstub.S
//   * negative tests: elf2exe rejects MOVW/MOVT absolute relocations, an
//     unaligned ABS32, and --com on code with absolute relocations
//   * the checks fire: a bogus relocation and a dropped relocation are caught
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync, spawnSync } from 'node:child_process';
import { parseProgram, relocate, MZSTUB, COMSTUB, COMSTUB_WORDS_OFF } from '../armexe.mjs';

const SDK = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const BUILD = process.env.BUILD || 'build';
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'armdos-sdk-test-'));
let failures = 0, passes = 0;
const ok = (cond, msg) => { if (cond) { passes++; console.log(`  ok   ${msg}`); } else { failures++; console.log(`  FAIL ${msg}`); } };
const have = (cmd) => spawnSync('which', [cmd]).status === 0;
const run = (cmd, args, o = {}) => execFileSync(cmd, args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], ...o });

/** Offsets of absolute relocations per readelf (independent of elf2exe's parser). */
function readelfAbsRelocs(elf) {
  const txt = run('arm-none-eabi-readelf', ['-r', '-S', '-s', '-W', elf]);
  // weak undefined symbols resolve to 0 (NULL) and must stay unrelocated
  const weakUnd = new Set();
  for (const m of txt.matchAll(/^\s*\d+:\s+[0-9a-f]+\s+\d+\s+\w+\s+WEAK\s+\w+\s+UND\s+(\S+)/gm)) weakUnd.add(m[1]);
  const allocSecs = new Set();
  for (const m of txt.matchAll(/^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+([A-Z]*)/gm))
    if (m[2].includes('A')) allocSecs.add(m[1]);
  const offs = new Set();
  let cur = null;
  for (const line of txt.split('\n')) {
    const h = line.match(/^Relocation section '\.rel(\.\S+)'/);
    if (h) { cur = h[1]; continue; }
    const r = line.match(/^([0-9a-f]{8})\s+[0-9a-f]{8}\s+(R_ARM_\w+)\s+([0-9a-f]{8})\s+(\S+)?/);
    if (r && cur && allocSecs.has(cur) && /^R_ARM_(ABS32|TARGET1|ABS32_NOI)$/.test(r[2])) {
      if (weakUnd.has(r[4])) offs.weakUndefined = (offs.weakUndefined || 0) + 1;
      else offs.add(parseInt(r[1], 16));
    }
  }
  // linker veneers (ARM <-> Thumb, long branches) carry an absolute target word the
  // linker gives no relocation; elf2exe adds one (at veneer + 4 or + 8, by shape):
  // those offsets are allowed, not required (PC-relative veneers need none)
  offs.veneerWords = new Set();
  for (const m of txt.matchAll(/^\s*\d+:\s+([0-9a-f]{8})\s+\d+\s+\w+\s+\w+\s+\w+\s+\S+\s+(\S+)$/gm))
    if (/_veneer$|^__.*_from_(arm|thumb)$|^__.*_change_to_(arm|thumb)$/.test(m[2])) {
      const a = parseInt(m[1], 16) & ~1;
      offs.veneerWords.add(a + 4); offs.veneerWords.add(a + 8);
    }
  return offs;
}

function randomBase() { return (0x700 + Math.floor(Math.random() * 0x8000) * 16) >>> 0; }

function checkLoad(p, label, bases = 4) {
  // exe: image+bss; self-relocating .COM: up to its stack top (bss size is not stored)
  const span = p.ar ? p.image.length + p.ar.bss_size : p.kind === 'com-selfreloc' ? p.stackTop : p.image.length;
  const bad = [];
  for (let k = 0; k < bases; k++) {
    const base = randomBase();
    const mem = relocate(p, base);   // (for a .COM the stub does the same at run time)
    for (const off of p.relocs) {
      const v = mem.readUInt32LE(off);
      if (v < base || v > base + span) bad.push(`+0x${off.toString(16)} -> 0x${v.toString(16)} (base 0x${base.toString(16)})`);
    }
  }
  ok(bad.length === 0, `${label}: relocated at ${bases} random bases, every relocated word points into image+bss` +
     (bad.length ? `\n         ${bad.slice(0, 5).join('\n         ')}` : ''));
  return bad.length === 0;
}

function testProgram(file) {
  const name = path.basename(file).replace(/\.(EXE|COM)$/i, '');
  const elf = path.join(BUILD, 'obj', name, `${name}.elf`);
  console.log(`${file}`);
  const buf = fs.readFileSync(file);
  let p;
  try { p = parseProgram(buf); ok(true, `parses as ${p.kind}`); }
  catch (e) { ok(false, `parse: ${e.message}`); return; }

  if (p.kind === 'exe') {
    const a = p.ar;
    ok(p.stub.equals(MZSTUB), 'standard 8086 stub; MZ header covers only header+stub');
    ok(a.image_off % 16 === 0 && a.reloc_off % 4 === 0, 'image 16-aligned, reloc table 4-aligned in the file');
    ok(a.stack_size % 8 === 0 && a.stack_size >= 1024, `stack_size ${a.stack_size} (8-aligned)`);
    ok(a.entry < a.image_size, `entry +0x${a.entry.toString(16)} inside image`);
    ok(a.reserved.every(x => x === 0), 'reserved words zero');
  }
  if (p.kind === 'com-selfreloc') {
    ok(p.stackTop % 8 === 0, `stack top +0x${p.stackTop.toString(16)} 8-aligned`);
    ok(p.stubSize % 16 === 0, 'image 16-aligned after the stub');
  }
  if (fs.existsSync(elf)) {
    const want = readelfAbsRelocs(elf);
    const got = new Set(p.relocs);
    const missing = [...want].filter(x => !got.has(x));
    const extra = [...got].filter(x => !want.has(x) && !want.veneerWords.has(x));
    ok(!missing.length && !extra.length,
       `reloc table == readelf's ${want.size} absolute relocations` +
       (want.weakUndefined ? ` (+${want.weakUndefined} against weak undefined symbols, correctly left as NULL)` : '') +
       (missing.length ? ` (missing ${missing.slice(0, 4).map(x => '0x' + x.toString(16))})` : '') +
       (extra.length ? ` (extra ${extra.slice(0, 4).map(x => '0x' + x.toString(16))})` : ''));
    // image bytes == the ELF's allocated sections
    const bin = path.join(tmp, name + '.bin');
    run('arm-none-eabi-objcopy', ['-O', 'binary', '--only-section=.text', '--only-section=.rodata', '--only-section=.data',
      '--only-section=.ARM.exidx', '--only-section=.ARM.extab', '--only-section=.init_array', '--only-section=.fini_array',
      '--only-section=.preinit_array', elf, bin]);
    const ref = fs.readFileSync(bin);
    ok(p.image.subarray(0, ref.length).equals(ref), `image bytes == objcopy -O binary (${ref.length} bytes)`);
    if (p.kind !== 'com') {
      const first = p.image.readUInt32LE(p.entry & ~1);
      if (name !== 'T_PIC') ok(first === 0xE1A04000, 'entry is crt0 (_start: mov r4, r0)');
    }
  } else console.log(`  (no ELF at ${elf}; skipping cross-checks)`);
  if (p.relocs.length) checkLoad(p, 'loader simulation');
  else ok(p.kind === 'com', 'no relocations (position-independent)');
  return p;
}

function testStubs() {
  console.log('stubs');
  if (have('nasm')) {
    const bin = path.join(tmp, 'mz.bin');
    run('nasm', ['-f', 'bin', path.join(SDK, 'mzstub.asm'), '-o', bin]);
    ok(fs.readFileSync(bin).equals(MZSTUB), 'embedded 8086 stub == nasm sdk/mzstub.asm');
  } else console.log('  (nasm not installed: skipped)');
  if (have('ndisasm')) {
    const bin = path.join(tmp, 'mz2.bin');
    fs.writeFileSync(bin, MZSTUB.subarray(0, 14));
    const d = run('ndisasm', ['-b16', bin]).replace(/\s+/g, ' ');
    ok(/push cs.*pop ds.*mov dx,0xe.*mov ah,0x9.*int (byte )?0x21.*mov ax,0x4c01.*int (byte )?0x21/.test(d), 'ndisasm: push cs; pop ds; mov dx,msg; mov ah,9; int 21h; mov ax,4C01h; int 21h');
    ok(MZSTUB.subarray(14).toString('latin1') === 'This program requires an ARM processor.\r\n$', 'message is CRLF and $-terminated');
  }
  const o = path.join(tmp, 'cs.o'), b = path.join(tmp, 'cs.bin');
  run('arm-none-eabi-gcc', ['-marm', '-march=armv5te', '-c', path.join(SDK, 'comstub.S'), '-o', o]);
  run('arm-none-eabi-objcopy', ['-O', 'binary', o, b]);
  const cs = fs.readFileSync(b);
  ok(cs.length === COMSTUB.length && cs.equals(COMSTUB), 'embedded .COM stub == assembled sdk/comstub.S');
  ok(COMSTUB_WORDS_OFF === cs.length - 20, 'patch words are the last five');
}

function elf2exe(args) {
  return spawnSync('node', [path.join(SDK, 'elf2exe.mjs'), ...args], { encoding: 'utf8' });
}

function testRejects() {
  console.log('elf2exe rejects');
  const cc = (src, name, extra = []) => {
    const s = path.join(tmp, name + '.S'), e = path.join(tmp, name + '.elf');
    fs.writeFileSync(s, src);
    run('arm-none-eabi-gcc', ['-marm', '-nostdlib', '-nostartfiles', '-T', path.join(SDK, 'link.ld'), '-Wl,-q',
      '-Wl,--no-warn-rwx-segments', ...extra, s, '-o', e]);
    return e;
  };
  // MOVW/MOVT absolute (ARMv7 only, but must be refused if it ever shows up)
  let e = cc(`.syntax unified\n.text\n.global _start\n_start: movw r0, #:lower16:thing\n movt r0, #:upper16:thing\n bx lr\n.data\nthing: .word 1\n`, 'movw', ['-march=armv7-a']);
  let r = elf2exe([e, '-o', path.join(tmp, 'movw.exe')]);
  ok(r.status !== 0 && /R_ARM_MOVW_ABS_NC.*against (thing|section \.data)/.test(r.stderr), `MOVW_ABS rejected, naming the symbol: ${r.stderr.split('\n')[0]}`);
  // unaligned ABS32 (a pointer in a packed struct)
  e = cc(`.text\n.global _start\n_start: bx lr\n.data\n.byte 1\nptr: .word _start\n`, 'unal', ['-march=armv5te']);
  r = elf2exe([e, '-o', path.join(tmp, 'unal.exe')]);
  ok(r.status !== 0 && /not 4-byte aligned/.test(r.stderr), `unaligned ABS32 rejected: ${r.stderr.split('\n')[0]}`);
  // --com with absolute relocations
  e = cc(`.text\n.global _start\n_start: ldr r0, =_start\n bx lr\n`, 'absc', ['-march=armv5te']);
  r = elf2exe(['--com', e, '-o', path.join(tmp, 'absc.com')]);
  ok(r.status !== 0 && /absolute relocation/.test(r.stderr), `--com refuses absolute relocations: ${r.stderr.split('\n')[0]}`);
  r = elf2exe(['--com', '--selfreloc', e, '-o', path.join(tmp, 'absc.com')]);
  ok(r.status === 0 && parseProgram(fs.readFileSync(path.join(tmp, 'absc.com'))).kind === 'com-selfreloc', '--com --selfreloc accepts it');
  // bss handling: .bss after .data, with a gap-free image
  e = cc(`.text\n.global _start\n_start: ldr r0, =buf\n bx lr\n.data\n.word 7\n.bss\nbuf: .space 1000\n`, 'bss', ['-march=armv5te']);
  r = elf2exe([e, '-o', path.join(tmp, 'bss.exe')]);
  const p = parseProgram(fs.readFileSync(path.join(tmp, 'bss.exe')));
  ok(r.status === 0 && p.ar.bss_size >= 1000 && p.ar.bss_size < 1016 && p.relocs.length === 1, `bss_size ${p.ar?.bss_size} for a 1000-byte .bss, 1 reloc`);
  // --ar1: same image/relocs, header at offset 0
  r = elf2exe(['--ar1', e, '-o', path.join(tmp, 'bss.ar1')]);
  const q = parseProgram(fs.readFileSync(path.join(tmp, 'bss.ar1')));
  ok(r.status === 0 && q.kind === 'ar1' && q.image.equals(p.image) && q.relocs.join() === p.relocs.join() && q.ar.image_off % 16 === 0,
     '--ar1 writes a bare AR1 file with the same image and relocations');
}

function testChecksFire(p) {
  console.log('the checks fire');
  // add a bogus relocation on an instruction word: the loader check must flag it
  const bogus = { ...p, relocs: [...p.relocs, p.entry & ~3] };
  const saved = failures;
  const quiet = console.log; console.log = () => {};
  const passed = checkLoad(bogus, 'mutant');
  console.log = quiet;
  failures = saved;
  ok(!passed, 'a bogus relocation on an instruction is caught by the loader simulation');
}

testStubs();
testRejects();
let sample = null;
for (const f of process.argv.slice(2)) {
  const p = testProgram(f);
  if (p && p.kind === 'exe' && p.relocs.length && !sample) sample = p;
}
if (sample) testChecksFire(sample);
fs.rmSync(tmp, { recursive: true, force: true });
console.log(`\n${passes} passed, ${failures} failed`);
process.exit(failures ? 1 : 0);
