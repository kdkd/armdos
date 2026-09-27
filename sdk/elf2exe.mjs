#!/usr/bin/env node
// elf2exe.mjs - convert a statically linked ARM ELF (linked at 0 with
// -Wl,-q so relocations survive) into an ARM-DOS program (ARCH.md section 8).
//
//   node sdk/elf2exe.mjs [options] prog.elf -o PROG.EXE
//
// Options
//   -o FILE            output file (default: input with .EXE/.COM)
//   --stack N          stack_size (default 8192)
//   --min-extra N      min_extra: minimum heap bytes beyond the stack (default 0)
//   --max-extra N      max_extra (default 0xFFFFFFFF = as much as possible)
//   --flags N          header flags (bit1 = wants an extended-memory heap);
//                      bit0 (Thumb entry) is set automatically
//   --com              write a raw .COM: the image only. Fails if the image has
//                      absolute relocations, unless --selfreloc is given, in
//                      which case a small ARM prologue that relocates the image
//                      at run time is put in front (only when needed)
//   --selfreloc        see --com
//   --ar1              write a bare AR1 file (header at offset 0, no MZ/8086
//                      stub): for images DOS loads itself (ARMDOS.SYS, DEVICE=)
//   --target2 rel|abs  how R_ARM_TARGET2 was resolved (must match the linker's
//                      --target2; sdk.mk links with rel, the default)
//   -v                 list sections and relocation statistics
//
// Relocations: R_ARM_ABS32 / R_ARM_TARGET1 / R_ARM_ABS32_NOI in allocated
// sections become table entries (image offset of the word to add the load
// base to). PC-relative types need nothing. Anything else (MOVW/MOVT_ABS,
// ABS16, GOT, TLS, SB-relative ...) cannot be expressed as "add the load base
// to this word" and is rejected with the symbol and section named.
import fs from 'node:fs';
import path from 'node:path';
import { buildExe, buildAr1, buildSelfRelocCom, FLAG_THUMB } from './armexe.mjs';

// ---------------------------------------------------------------- ELF ------
const SHT_PROGBITS = 1, SHT_SYMTAB = 2, SHT_RELA = 4, SHT_NOBITS = 8, SHT_REL = 9;
const SHF_WRITE = 1, SHF_ALLOC = 2, SHF_EXECINSTR = 4, SHF_TLS = 0x400;
const SHN_UNDEF = 0, SHN_ABS = 0xFFF1, SHN_COMMON = 0xFFF2;

const R = {
  NONE: 0, PC24: 1, ABS32: 2, REL32: 3, LDR_PC_G0: 4, ABS16: 5, ABS12: 6, THM_ABS5: 7, ABS8: 8,
  SBREL32: 9, THM_CALL: 10, THM_PC8: 11, PLT32: 27, CALL: 28, JUMP24: 29, THM_JUMP24: 30,
  TARGET1: 38, V4BX: 40, TARGET2: 41, PREL31: 42, MOVW_ABS_NC: 43, MOVT_ABS: 44,
  MOVW_PREL_NC: 45, MOVT_PREL: 46, THM_MOVW_ABS_NC: 47, THM_MOVT_ABS: 48,
  THM_MOVW_PREL_NC: 49, THM_MOVT_PREL: 50, THM_JUMP19: 51, THM_JUMP6: 52,
  THM_ALU_PREL_11_0: 53, THM_PC12: 54, ABS32_NOI: 55, REL32_NOI: 56,
  GNU_VTENTRY: 100, GNU_VTINHERIT: 101, THM_JUMP11: 102, THM_JUMP8: 103,
};
const RNAME = Object.fromEntries(Object.entries(R).map(([k, v]) => [v, 'R_ARM_' + k]));
const relName = t => RNAME[t] || `R_ARM_type${t}`;

const ABSOLUTE = new Set([R.ABS32, R.TARGET1, R.ABS32_NOI]);
const IGNORE = new Set([R.NONE, R.V4BX, R.GNU_VTENTRY, R.GNU_VTINHERIT]);
const PCREL = new Set([
  R.PC24, R.REL32, R.LDR_PC_G0, R.THM_CALL, R.THM_PC8, R.PLT32, R.CALL, R.JUMP24, R.THM_JUMP24,
  32, 33, 34,                                   // ALU_PCREL_7_0/15_8/23_15 (obsolete)
  R.PREL31, R.MOVW_PREL_NC, R.MOVT_PREL, R.THM_MOVW_PREL_NC, R.THM_MOVT_PREL,
  R.THM_JUMP19, R.THM_JUMP6, R.THM_ALU_PREL_11_0, R.THM_PC12, R.REL32_NOI,
  57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70,  // ALU/LDR/LDRS/LDC_PC_Gn
  R.THM_JUMP11, R.THM_JUMP8,
]);

function parseElf(buf, file) {
  const fail = m => { throw new Error(`${file}: ${m}`); };
  if (buf.length < 52 || buf.readUInt32BE(0) !== 0x7F454C46) fail('not an ELF file');
  if (buf[4] !== 1) fail('not a 32-bit ELF');
  if (buf[5] !== 1) fail('not little-endian');
  const e = {
    type: buf.readUInt16LE(16), machine: buf.readUInt16LE(18), entry: buf.readUInt32LE(24),
    phoff: buf.readUInt32LE(28), shoff: buf.readUInt32LE(32), flags: buf.readUInt32LE(36),
    shentsize: buf.readUInt16LE(46), shnum: buf.readUInt16LE(48), shstrndx: buf.readUInt16LE(50),
  };
  if (e.machine !== 40) fail(`e_machine ${e.machine} is not ARM (40)`);
  if (e.type !== 2) fail(`e_type ${e.type}: expected a linked executable (ET_EXEC); link with sdk/link.ld`);
  if (!e.shoff || !e.shnum) fail('no section headers');
  const secs = [];
  for (let i = 0; i < e.shnum; i++) {
    const o = e.shoff + i * e.shentsize;
    secs.push({
      index: i, nameOff: buf.readUInt32LE(o), type: buf.readUInt32LE(o + 4), flags: buf.readUInt32LE(o + 8),
      addr: buf.readUInt32LE(o + 12), offset: buf.readUInt32LE(o + 16), size: buf.readUInt32LE(o + 20),
      link: buf.readUInt32LE(o + 24), info: buf.readUInt32LE(o + 28), entsize: buf.readUInt32LE(o + 36),
    });
  }
  const shstr = secs[e.shstrndx];
  const cstr = (off) => { let end = off; while (buf[end]) end++; return buf.toString('latin1', off, end); };
  for (const s of secs) s.name = cstr(shstr.offset + s.nameOff);

  let syms = [];
  const symtab = secs.find(s => s.type === SHT_SYMTAB);
  if (symtab) {
    const strtab = secs[symtab.link];
    for (let o = symtab.offset; o < symtab.offset + symtab.size; o += 16) {
      const info = buf[o + 12];
      syms.push({ name: cstr(strtab.offset + buf.readUInt32LE(o)), value: buf.readUInt32LE(o + 4),
                  size: buf.readUInt32LE(o + 8), bind: info >> 4, type: info & 15, shndx: buf.readUInt16LE(o + 14) });
    }
  }
  return { e, secs, syms };
}

// ------------------------------------------------------------ convert ------
export function convert(buf, file, opt) {
  const { e, secs, syms } = parseElf(buf, file);
  const errors = [], warnings = [];
  const symName = (i) => {
    const s = syms[i];
    if (!s) return `symbol #${i}`;
    if (s.type === 3 /* STT_SECTION */) return `section ${secs[s.shndx]?.name ?? s.shndx}`;
    return s.name || `symbol #${i}`;
  };

  for (const s of secs)
    if ((s.flags & SHF_ALLOC) && (s.flags & SHF_TLS) && s.size)
      errors.push(`section ${s.name}: thread-local storage is not supported`);

  // --- the image: allocated sections with contents, from address 0
  const alloc = secs.filter(s => (s.flags & SHF_ALLOC) && s.size > 0);
  const loaded = alloc.filter(s => s.type !== SHT_NOBITS);
  const nobits = alloc.filter(s => s.type === SHT_NOBITS);
  if (!loaded.length) throw new Error(`${file}: no loadable sections`);
  const lowest = Math.min(...alloc.map(s => s.addr));
  if (lowest !== 0) errors.push(`image does not start at address 0 (lowest section ${alloc.find(s => s.addr === lowest).name} at 0x${lowest.toString(16)}); link with sdk/link.ld`);
  const imageSize = Math.max(...loaded.map(s => s.addr + s.size));
  const image = Buffer.alloc(imageSize);                       // gaps are zero-filled
  const sorted = [...loaded].sort((a, b) => a.addr - b.addr);
  for (let i = 0; i < sorted.length; i++) {
    const s = sorted[i];
    if (i && s.addr < sorted[i - 1].addr + sorted[i - 1].size)
      errors.push(`sections ${sorted[i - 1].name} and ${s.name} overlap`);
    buf.copy(image, s.addr, s.offset, s.offset + s.size);
  }
  const end = Math.max(imageSize, ...alloc.map(s => s.addr + s.size));
  const bssSize = end - imageSize;
  for (const s of nobits)
    if (s.addr < imageSize)
      warnings.push(`NOBITS section ${s.name} at 0x${s.addr.toString(16)} lies inside the image; it is stored as zeros`);

  // --- linker-generated veneers (ARM/Thumb interworking, long branches). GNU ld emits
  // no relocation for the absolute address some of them hold, so recognise the known
  // shapes: an absolute target word gets a relocation here (vetoRelocs below), a veneer
  // that only branches PC-relative needs none, and anything else is refused. (Needed
  // when newlib's multilib is Thumb code, as in Arm's own toolchain for macOS.)
  const veneerRelocs = [];
  const word = (a) => (a >= 0 && a + 4 <= imageSize ? image.readUInt32LE(a) : -1);
  const half = (a) => (a >= 0 && a + 2 <= imageSize ? image.readUInt16LE(a) : -1);
  const LDR_IP_PC = 0xE59FC000, BX_IP = 0xE12FFF1C, LDR_PC_PC4 = 0xE51FF004;
  const isArmB = (w) => w !== -1 && (w & 0x0F000000) === 0x0A000000 && (w >>> 28) !== 15;   // B/BL (PC-relative)
  const absAt = (a, name) => {
    const t = word(a) & ~1;
    if (t < 0 || t >= end) { errors.push(`linker-generated veneer ${name}: its target 0x${(t >>> 0).toString(16)} is outside the program`); return; }
    veneerRelocs.push(a);
  };
  for (const s of syms) {
    if (!/_veneer$|^__.*_from_(arm|thumb)$|^__.*_change_to_(arm|thumb)$/.test(s.name) || s.shndx === SHN_UNDEF) continue;
    const a = s.value & ~1;
    if (word(a) >>> 0 === LDR_IP_PC && word(a + 4) >>> 0 === BX_IP) absAt(a + 8, s.name);          // ARM -> Thumb: ldr ip,[pc]; bx ip; .word
    else if (word(a) >>> 0 === LDR_PC_PC4) absAt(a + 4, s.name);                                      // long branch: ldr pc,[pc,#-4]; .word
    else if (half(a) === 0x4778) {                                                                     // Thumb -> ARM: bx pc; (nop); then ARM code
      const w = word(a + 4) >>> 0;
      if (isArmB(w)) continue;                                                                         // ...b target: PC-relative
      if (w === LDR_PC_PC4) { absAt(a + 8, s.name); continue; }
      errors.push(`linker-generated veneer ${s.name} at 0x${a.toString(16)}: unknown shape`);
    } else if (isArmB(word(a) >>> 0)) continue;
    else errors.push(`linker-generated veneer ${s.name} at 0x${a.toString(16)}: unknown shape (keep the program ARM-only or within branch range)`);
  }

  // --- relocations
  const relocs = new Set();
  const stats = {};
  let hadRelocSections = false;
  for (const rs of secs) {
    if (rs.type !== SHT_REL && rs.type !== SHT_RELA) continue;
    const target = secs[rs.info];
    if (!target || !(target.flags & SHF_ALLOC)) continue;      // debug info etc.
    hadRelocSections = true;
    const entsize = rs.type === SHT_RELA ? 12 : 8;
    for (let o = rs.offset; o < rs.offset + rs.size; o += entsize) {
      const off = buf.readUInt32LE(o), info = buf.readUInt32LE(o + 4);
      const type = info & 0xFF, symi = info >>> 8;
      const sym = syms[symi];
      stats[relName(type)] = (stats[relName(type)] || 0) + 1;
      const where = () => `${relName(type)} at 0x${off.toString(16)} in ${target.name} against ${symName(symi)}`;
      if (IGNORE.has(type)) continue;
      if (type === R.TARGET2 && opt.target2 !== 'abs') continue;           // --target2=rel: PC-relative
      if (PCREL.has(type)) {
        if (sym && sym.shndx === SHN_ABS && symi !== 0)
          errors.push(`${where()}: PC-relative reference to an absolute address cannot survive relocation`);
        continue;
      }
      if (ABSOLUTE.has(type) || (type === R.TARGET2 && opt.target2 === 'abs')) {
        if (sym && symi !== 0 && sym.shndx === SHN_ABS) {
          warnings.push(`${where()}: absolute symbol, left unrelocated`);
          continue;
        }
        if (sym && symi !== 0 && sym.shndx === SHN_UNDEF) {
          if (sym.bind !== 2) errors.push(`${where()}: undefined symbol`);
          continue;                                                        // weak undefined = 0
        }
        if (target.type === SHT_NOBITS) { errors.push(`${where()}: relocation in a NOBITS section`); continue; }
        if (off & 3) { errors.push(`${where()}: word is not 4-byte aligned (packed struct holding a pointer?)`); continue; }
        if (off + 4 > imageSize) { errors.push(`${where()}: outside the image`); continue; }
        relocs.add(off);
        continue;
      }
      errors.push(`${where()}: this relocation type cannot be expressed as "add the load base to a word"` +
                  (type === R.MOVW_ABS_NC || type === R.MOVT_ABS || type === R.THM_MOVW_ABS_NC || type === R.THM_MOVT_ABS
                    ? ' (compile with -mno-movt / for ARMv5TE, or load the address from a literal)' : ''));
    }
  }
  if (!hadRelocSections && !opt.com)
    warnings.push('no relocation sections found: was the program linked with -Wl,-q (--emit-relocs)?');

  for (const a of veneerRelocs) relocs.add(a);
  // a relocation must not sit on top of code the loader would corrupt: sanity only
  const list = [...relocs].sort((a, b) => a - b);
  return { e, secs, syms, image, imageSize, bssSize, relocs: list, entry: e.entry, errors, warnings, stats, alloc };
}

// ---------------------------------------------------------------- CLI ------
function parseNum(s, what) {
  const n = /^0x/i.test(s) ? parseInt(s, 16) : /^\d+[kK]$/.test(s) ? parseInt(s) * 1024 : Number(s);
  if (!Number.isFinite(n) || n < 0 || n > 0xFFFFFFFF) throw new Error(`bad number for ${what}: ${s}`);
  return n >>> 0;
}

function main(argv) {
  const opt = { stack: 8192, minExtra: 0, maxExtra: 0xFFFFFFFF, flags: 0, com: false, selfreloc: false,
                target2: 'rel', verbose: false, out: null, input: null };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i], next = () => { if (i + 1 >= argv.length) throw new Error(`${a} needs a value`); return argv[++i]; };
    if (a === '-o') opt.out = next();
    else if (a === '--stack') opt.stack = parseNum(next(), a);
    else if (a === '--min-extra') opt.minExtra = parseNum(next(), a);
    else if (a === '--max-extra') opt.maxExtra = parseNum(next(), a);
    else if (a === '--flags') opt.flags = parseNum(next(), a);
    else if (a === '--com') opt.com = true;
    else if (a === '--selfreloc') opt.selfreloc = true;
    else if (a === '--ar1') opt.ar1 = true;
    else if (a === '--target2') opt.target2 = next();
    else if (a === '-v' || a === '--verbose') opt.verbose = true;
    else if (a === '-h' || a === '--help') { console.log(fs.readFileSync(new URL(import.meta.url)).toString().split('\n').filter(l => l.startsWith('//')).map(l => l.slice(3)).join('\n')); return 0; }
    else if (a.startsWith('-')) throw new Error(`unknown option ${a}`);
    else if (!opt.input) opt.input = a;
    else throw new Error(`extra argument ${a}`);
  }
  if (!opt.input) throw new Error('usage: elf2exe.mjs [options] input.elf -o OUTPUT');
  if (opt.stack & 7) opt.stack = (opt.stack + 7) & ~7;
  const out = opt.out || opt.input.replace(/\.elf$/i, '') + (opt.com ? '.COM' : '.EXE');
  const r = convert(fs.readFileSync(opt.input), opt.input, opt);
  const tag = path.basename(opt.input);

  for (const w of r.warnings) console.error(`elf2exe: ${tag}: warning: ${w}`);
  if (r.errors.length) {
    for (const e of r.errors) console.error(`elf2exe: ${tag}: error: ${e}`);
    return 1;
  }
  if (opt.verbose) {
    for (const s of r.alloc) console.error(`  ${s.name.padEnd(16)} 0x${s.addr.toString(16).padStart(6, '0')} ${String(s.size).padStart(7)}${s.type === SHT_NOBITS ? ' (nobits)' : ''}`);
    console.error(`  relocation types: ${JSON.stringify(r.stats)}`);
  }
  const flags = (opt.flags & ~FLAG_THUMB) | (r.entry & 1 ? FLAG_THUMB : 0);
  let file;
  if (opt.com) {
    if (r.relocs.length && !opt.selfreloc) {
      console.error(`elf2exe: ${tag}: error: --com needs position-independent code, but the image has ${r.relocs.length} absolute relocation(s), e.g. at 0x${r.relocs[0].toString(16)}; use --selfreloc or build an .EXE`);
      return 1;
    }
    if (r.relocs.length) file = buildSelfRelocCom({ image: r.image, bssSize: r.bssSize, stackSize: opt.stack, entry: r.entry, relocs: r.relocs });
    else {
      if (r.entry !== 0) { console.error(`elf2exe: ${tag}: error: a raw .COM is entered at offset 0, but the entry point is 0x${r.entry.toString(16)}`); return 1; }
      file = r.image;
    }
    if (file[0] === 0x4D && file[1] === 0x5A) { console.error(`elf2exe: ${tag}: error: .COM image starts with "MZ" and would be taken for an EXE`); return 1; }
  } else if (opt.ar1) {
    file = buildAr1({ image: r.image, bssSize: r.bssSize, stackSize: opt.stack, entry: r.entry, relocs: r.relocs,
                      minExtra: opt.minExtra, maxExtra: opt.maxExtra, flags });
  } else {
    file = buildExe({ image: r.image, bssSize: r.bssSize, stackSize: opt.stack, entry: r.entry, relocs: r.relocs,
                      minExtra: opt.minExtra, maxExtra: opt.maxExtra, flags });
  }
  fs.writeFileSync(out, file);
  if (opt.verbose || process.env.ELF2EXE_VERBOSE)
    console.error(`elf2exe: ${out}: ${file.length} bytes (image ${r.imageSize}, bss ${r.bssSize}, ${r.relocs.length} relocs)`);
  return 0;
}

if (import.meta.url === `file://${process.argv[1]}` || process.argv[1]?.endsWith('elf2exe.mjs')) {
  try { process.exitCode = main(process.argv.slice(2)); }
  catch (err) { console.error(`elf2exe: ${err.message}`); process.exitCode = 1; }
}
