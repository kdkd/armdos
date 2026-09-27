#!/usr/bin/env node
// rescheck.mjs - check that the resident part of a program or driver is
// self-contained: nothing in [from, to) refers to an address at or beyond
// `to` (code or data that DOS no longer keeps once the program has gone
// resident / the driver's INIT has returned its break address).
//
//   node apps/ansi/tests/rescheck.mjs ELF FROM-SYMBOL|0 TO-SYMBOL [ALLOWED-TARGET...]
//
// Absolute references (R_ARM_ABS32 words, e.g. literal pools and data
// pointers) are read from the linked .text; branches (R_ARM_CALL/JUMP24/
// PC24) are decoded. Exit status 0 = self-contained. Used by the tests of
// ANSI.SYS, MOUSE.COM and MODE.COM.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

export function rescheck(elf, fromSym, toSym, allow = []) {
  const syms = new Map();
  for (const l of execFileSync('arm-none-eabi-nm', [elf], { encoding: 'utf8' }).split('\n')) {
    const m = /^([0-9a-f]+) \w (\S+)$/.exec(l);
    if (m) syms.set(m[2], parseInt(m[1], 16));
  }
  const from = fromSym === '0' ? 0 : syms.get(fromSym), to = syms.get(toSym);
  if (from === undefined || to === undefined) throw new Error(`symbols ${fromSym}/${toSym} not found`);
  const tmp = `${elf}.rescheck.bin`;
  execFileSync('arm-none-eabi-objcopy', ['-O', 'binary', '-j', '.text', elf, tmp]);
  const text = fs.readFileSync(tmp);
  fs.rmSync(tmp, { force: true });
  const bad = [];
  const rel = execFileSync('arm-none-eabi-readelf', ['-rW', elf], { encoding: 'utf8' });
  let inText = false;
  for (const l of rel.split('\n')) {
    if (l.startsWith('Relocation section')) { inText = /'\.rel\.text'/.test(l); continue; }
    if (!inText) continue;
    const m = /^([0-9a-f]{8})\s+[0-9a-f]+\s+(R_ARM_\w+)\s+([0-9a-f]+)?\s*(.*)$/.exec(l.trim());
    if (!m) continue;
    const off = parseInt(m[1], 16), type = m[2];
    if (off < from || off >= to || off + 4 > text.length) continue;
    const w = text.readUInt32LE(off);
    let target = null;
    if (type === 'R_ARM_ABS32') target = w;
    else if (type === 'R_ARM_CALL' || type === 'R_ARM_JUMP24' || type === 'R_ARM_PC24') {
      let imm = w & 0xFFFFFF;
      if (imm & 0x800000) imm -= 0x1000000;
      target = off + 8 + imm * 4;
    }
    if (target === null) continue;
    if (target === 0xFFFFFFFF) continue;              // "no next driver"
    if (allow.some((a) => syms.get(a) === target)) continue;   // e.g. the INIT routine
    if (target >= to) bad.push(`${off.toString(16)}: ${type} -> ${target.toString(16)} (${m[4]})`);
  }
  return { from, to, bad };
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const [elf, fromSym, toSym, ...allow] = process.argv.slice(2);
  const { from, to, bad } = rescheck(elf, fromSym, toSym, allow);
  if (bad.length) { console.log(`resident part ${from.toString(16)}-${to.toString(16)} refers beyond it:\n  ${bad.join('\n  ')}`); process.exit(1); }
  console.log(`resident part ${from.toString(16)}-${to.toString(16)} (${to - from} bytes) is self-contained`);
}
