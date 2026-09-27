// apps/duke3d/tests/lib.mjs - shared helpers for the Duke Nukem 3D tests:
// builds a C: with IO.SYS, ARMDOS.SYS, COMMAND.COM, HIMEM.SYS (CONFIG.SYS),
// SET BLASTER + MOUSE (AUTOEXEC.BAT) and C:\GAMES\DUKE3D\ as
// apps/duke3d/hd.json has it, boots it, and drives the machine.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const B = (p) => path.join(ROOT, 'build', p);

// The game's own variables, from its ELF: symbol addresses (arm-none-eabi-nm)
// and the byte offsets of a struct's members from the DWARF debug info
// (arm-none-eabi-readelf). Both come with the ARM toolchain the build needs; a
// host gdb that reads ARM DWARF does not (macOS has none), and asking one that
// isn't there gave NaN offsets, i.e. reads of address 0 (the INT 0 vector).
export function elfSymbols(elf) {
  const m = new Map();
  for (const l of execFileSync('arm-none-eabi-nm', [elf]).toString().split('\n')) {
    const [a, , n] = l.trim().split(/\s+/);
    if (n) m.set(n, parseInt(a, 16));
  }
  return m;
}

// { member: byte offset } of the first full definition of "struct name" (each
// compilation unit has its own copy of the type, all the same), or null.
export function structOffsets(elf, name) {
  const dump = execFileSync('arm-none-eabi-readelf', ['--debug-dump=info', '--dwarf-depth=3', elf],
    { maxBuffer: 1 << 30 }).toString().split('\n');
  const die = /^\s*<(\d+)><[0-9a-f]+>: Abbrev Number: \d+ \((DW_TAG_\w+)\)/;
  const attr = /^\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s*(.*)$/;
  // DW_AT_name is "posx" (DW_FORM_string) or "(indirect string, offset: 0x20ca): posx"
  const str = (v) => v.replace(/^\([^)]*\):\s*/, '').trim();
  let inStruct = false, cur = null, member = null, off = {};
  for (const l of dump) {
    let m = die.exec(l);
    if (m) {
      const depth = +m[1];
      if (inStruct && depth <= 1) {
        if (Object.keys(off).length) return off;
        inStruct = false;
      }
      cur = depth === 1 && m[2] === 'DW_TAG_structure_type' ? 'struct'
          : inStruct && depth === 2 && m[2] === 'DW_TAG_member' ? 'member' : null;
      member = null;
      continue;
    }
    if (!cur || !(m = attr.exec(l))) continue;
    if (cur === 'struct') {
      if (m[1] === 'DW_AT_name') { inStruct = str(m[2]) === name; off = {}; }
      else if (m[1] === 'DW_AT_declaration') inStruct = false;
    } else if (m[1] === 'DW_AT_name') member = str(m[2]);
    else if (m[1] === 'DW_AT_data_member_location' && member !== null) off[member] = parseInt(m[2], 10);
  }
  return inStruct && Object.keys(off).length ? off : null;
}

export function makeImage(out, name, { autoexec = '', himem = true, extraFiles = [] } = {}) {
  const dir = path.join(out, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const frag = JSON.parse(fs.readFileSync(path.join(ROOT, 'apps/duke3d/hd.json'), 'utf8'));
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/COMMAND.COM' },
    { src: 'build/HIMEM.SYS', dst: 'DOS\\' },
    { src: 'build/MOUSE.COM', dst: 'DOS\\' },
    { src: 'build/MEM.EXE', dst: 'DOS\\' },
    { src: 'build/SBMIX.EXE', dst: 'DOS\\' },
    ...frag.files, ...extraFiles,
  ];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, text.replace(/\n/g, '\r\n'));
    files.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', (himem ? 'DEVICE=C:\\DOS\\HIMEM.SYS\n' : '') + 'FILES=20\n');
  put('AUTOEXEC.BAT', `@ECHO OFF\nPATH C:\\DOS\nPROMPT $P$G\nSET BLASTER=A220 I7 D1 H5 T6\nSBMIX /INIT /Q\n${autoexec}\n`);
  const m = {
    format: 'hd', sizeMB: 64, heads: 16, sectorsPerTrack: 63, label: 'DUKETEST',
    date: '1996-04-24 12:00:00', boot: { src: 'build/bootsect.bin' }, files,
    dirs: [...frag.dirs, 'DOS'],
  };
  const { img } = buildImage(m, ROOT);
  const p = path.join(out, name + '.img');
  fs.writeFileSync(p, img);
  return p;
}

export async function start(out, name, opts = {}) {
  const hd = makeImage(out, name, opts);
  // (no MPU-401 unless asked: DUKE3D.EXE would choose General MIDI music - tests/gm.mjs - instead of the OPL3)
  return boot({ rom: B('rom.bin'), hd, mhz: opts.mhz, jit: opts.jit !== false, mpu: opts.mpu || { present: false } });
}

export const mode = (pc) => pc.machine.vga.mode;
export const ivt = (pc, n) => pc.cpu.m32[n];
export const bdaTicks = (pc) => pc.cpu.m32[0x46C >> 2];
export const hold = (pc, code, ms) => { pc.machine.keyDown(code); pc.run(ms); pc.machine.keyUp(code); pc.run(60); };
export const tap = (pc, code, after = 400) => { pc.machine.keyDown(code); pc.run(80); pc.machine.keyUp(code); pc.run(after); };
export const colours = (pc) => { const img = pc.render(); const s = new Set(); for (let i = 0; i < img.data.length; i += 4 * 7) s.add(img.data[i] << 16 | img.data[i + 1] << 8 | img.data[i + 2]); return s.size; };
export const frameHash = (pc) => { const img = pc.render(); let h = 0; for (let i = 0; i < img.data.length; i += 3) h = (h * 31 + img.data[i]) | 0; return h; };
export const busy = (pc, ms) => {
  const h0 = pc.machine.haltedNs, e0 = pc.machine.timeNs();
  pc.run(ms);
  return 1 - (pc.machine.haltedNs - h0) / (pc.machine.timeNs() - e0);
};

export function readHostFile(pc, dosPath) {
  try {
    const fr = new FatReader(Buffer.from(pc.machine.ata.img.buffer, pc.machine.ata.img.byteOffset, pc.machine.ata.img.length));
    const ent = fr.lookup(dosPath);
    return ent ? fr.readFile(ent) : null;
  } catch (e) { return null; }
}
