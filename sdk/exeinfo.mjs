#!/usr/bin/env node
// exeinfo.mjs - dump an ARM-DOS program's headers and relocations (a mini
// EXEHDR). Understands .EXE (MZ + AR1), self-relocating .COM and raw .COM.
//
//   node sdk/exeinfo.mjs [--relocs] [--disasm] [--json] FILE...
//
//   --relocs   list every relocation (offset, stored word, what it points into)
//   --disasm   disassemble the image with arm-none-eabi-objdump (linked at 0)
//   --json     machine-readable output
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { parseProgram, MZSTUB, MZSTUB_MESSAGE, FLAG_THUMB, FLAG_XMS } from './armexe.mjs';

const hex = (n, w = 8) => '0x' + (n >>> 0).toString(16).toUpperCase().padStart(w, '0');

function describe(file, buf, opt) {
  const p = parseProgram(buf);
  const out = [];
  const say = (s = '') => out.push(s);
  const bss = p.ar ? p.ar.bss_size : 0;
  const memEnd = p.kind === 'com-selfreloc' ? p.stackTop : p.image.length + bss;   // .COM: bss size not stored
  say(`${file}: ${buf.length} bytes, ${p.kind === 'exe' ? 'ARM-DOS EXE (MZ + AR1)' : p.kind === 'ar1' ? 'bare AR1 image (no MZ stub)' : p.kind === 'com-selfreloc' ? 'self-relocating ARM .COM' : 'raw ARM .COM (position-independent image)'}`);

  if (p.ar) {
    const m = p.mz, a = p.ar;
    if (m) {
    say('MZ header');
    say(`  bytes in last page   ${m.e_cblp}          pages in file  ${m.e_cp}  (DOS part = ${m.e_cp * 512 - (m.e_cblp ? 512 - m.e_cblp : 0)} bytes)`);
    say(`  relocations          ${m.e_crlc}          header paras   ${m.e_cparhdr}`);
    say(`  min/max alloc paras  ${hex(m.e_minalloc, 4)} / ${hex(m.e_maxalloc, 4)}`);
    say(`  SS:SP                ${hex(m.e_ss, 4)}:${hex(m.e_sp, 4)}   CS:IP ${hex(m.e_cs, 4)}:${hex(m.e_ip, 4)}`);
    say(`  e_lfarlc             ${hex(m.e_lfarlc, 4)}     e_lfanew       ${hex(m.e_lfanew)}`);
    const msgOk = p.stub.includes(Buffer.from(MZSTUB_MESSAGE));
    say(`  8086 stub            ${p.stub.length} bytes${p.stub.equals(MZSTUB) ? ' (standard)' : ''}${msgOk ? `: "${MZSTUB_MESSAGE}"` : ''}`);
    }
    say('AR1 header');
    const fl = [a.flags & FLAG_THUMB ? 'thumb-entry' : '', a.flags & FLAG_XMS ? 'xms-heap' : ''].filter(Boolean).join(',');
    say(`  flags                ${hex(a.flags, 4)}${fl ? ' (' + fl + ')' : ''}`);
    say(`  image                ${hex(a.image_off)} + ${a.image_size} bytes`);
    say(`  bss                  ${a.bss_size} bytes`);
    say(`  stack                ${a.stack_size} bytes`);
    say(`  entry                +${hex(a.entry)}`);
    say(`  relocations          ${a.reloc_count} at ${hex(a.reloc_off)}`);
    say(`  min/max extra        ${a.min_extra} / ${a.max_extra === 0xFFFFFFFF ? 'all (0xFFFFFFFF)' : a.max_extra}`);
    if (a.reserved.some(Boolean)) say(`  reserved             ${a.reserved.map(x => hex(x)).join(' ')}  (should be zero)`);
    const need = 0x100 + a.image_size + a.bss_size + a.stack_size + a.min_extra;
    say(`  memory needed        ${need} bytes = ${Math.ceil(need / 16)} paragraphs (PSP + image + bss + stack + min_extra)`);
  } else if (p.kind === 'com-selfreloc') {
    say(`  stub ${p.stubSize} bytes, image ${p.imageSize} bytes, ${p.relocs.length} relocations, stack top +${hex(p.stackTop)}, entry +${hex(p.entry)}`);
  } else {
    say(`  image ${p.image.length} bytes, entered at offset 0 (PSP+0x100)`);
  }

  // relocation sanity: every relocated word should point into image+bss
  let outside = 0;
  const rows = [];
  for (const off of p.relocs) {
    const v = p.image.readUInt32LE(off);
    const inImage = v < p.image.length, inBss = !inImage && v <= memEnd;
    if (!inImage && !inBss) outside++;
    rows.push({ off, v, where: inImage ? 'image' : inBss ? (v === memEnd ? 'end' : 'bss') : 'OUTSIDE' });
  }
  if (p.relocs.length) {
    say(`Relocations: ${p.relocs.length}, ${rows.filter(r => r.where === 'image').length} into the image, ${rows.filter(r => r.where !== 'image' && r.where !== 'OUTSIDE').length} into bss/end, ${outside} outside`);
    const list = opt.relocs ? rows : rows.slice(0, 8);
    for (const r of list) say(`  +${hex(r.off, 6)}  word ${hex(r.v)}  -> ${r.where}`);
    if (!opt.relocs && rows.length > list.length) say(`  ... (${rows.length - list.length} more; --relocs lists all)`);
  }

  if (opt.disasm) {
    const tmp = path.join(os.tmpdir(), `exeinfo-${process.pid}.bin`);
    fs.writeFileSync(tmp, p.image);
    try {
      say(execFileSync('arm-none-eabi-objdump', ['-D', '-b', 'binary', '-m', 'arm', '-M', 'reg-names-std', tmp], { encoding: 'utf8' })
        .split('\n').slice(6).join('\n'));
    } catch (e) { say(`(disassembly failed: ${e.message})`); }
    finally { fs.rmSync(tmp, { force: true }); }
  }
  return { text: out.join('\n'), json: { file, kind: p.kind, size: buf.length, mz: p.mz, ar: p.ar, relocs: p.relocs, outside } };
}

const args = process.argv.slice(2);
const opt = { relocs: args.includes('--relocs'), disasm: args.includes('--disasm'), json: args.includes('--json') };
const files = args.filter(a => !a.startsWith('--'));
if (!files.length) { console.error('usage: exeinfo.mjs [--relocs] [--disasm] [--json] FILE...'); process.exit(2); }
let rc = 0;
const all = [];
for (const f of files) {
  try {
    const d = describe(f, fs.readFileSync(f), opt);
    if (opt.json) all.push(d.json); else console.log(d.text + '\n');
  } catch (e) { console.error(`exeinfo: ${f}: ${e.message}`); rc = 1; }
}
if (opt.json) console.log(JSON.stringify(all.length === 1 ? all[0] : all, null, 1));
process.exit(rc);
