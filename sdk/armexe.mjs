// armexe.mjs - the ARM-DOS executable formats (ARCH.md section 8), shared by
// elf2exe.mjs, exeinfo.mjs and the tests. No dependencies.
//
//   .EXE  = MZ header (0x40) + 8086 stub + "AR1\0" header (64) + image + relocs
//   .COM  = raw position-independent image, or (elf2exe --selfreloc)
//           COMSTUB + image + relocs
//   AR1   = bare AR1 header + image + relocs (elf2exe --ar1; ARMDOS.SYS, drivers)

export const MZ_HDR_SIZE = 0x40;
export const AR1_HDR_SIZE = 64;
export const FLAG_THUMB = 1;
export const FLAG_XMS = 2;

// sdk/mzstub.asm, assembled with nasm -f bin (checked by tests/run-tests.mjs).
export const MZSTUB = Buffer.from(
  '0e1fba0e00b409cd21b8014ccd21546869732070726f6772616d20726571756972657320616e2041524d2070726f636573736f722e0d0a24',
  'hex');
export const MZSTUB_MESSAGE = 'This program requires an ARM processor.';

// sdk/comstub.S, assembled (checked by tests/run-tests.mjs). The last five
// words are stub_size, image_size, reloc_count, stack_top, entry.
export const COMSTUB = Buffer.from(
  '08c04fe278409fe504408ce074509fe5055084e070609fe5016056e20400004a047095e4' +
  '078094e7048088e0078084e7f8ffffea54709fe5077084e0020057e10400008a07d0a0e1' +
  '0410a0e140709fe5077084e017ff2fe110308fe2090ca0e3210000ef130ba0e3080080e3' +
  '210000ef4e6f7420656e6f756768206d656d6f72790d0a24000000000000000000000000' +
  '0000000000000000',
  'hex');
export const COMSTUB_WORDS_OFF = COMSTUB.length - 20;   // 0x84
export const COMSTUB_SIZE = 160;                         // padded: image stays 16-aligned

export const align = (n, a) => Math.ceil(n / a) * a;

/**
 * Build an .EXE file.
 * @param {object} o  { image: Buffer, bssSize, stackSize, entry, relocs: number[],
 *                      minExtra, maxExtra, flags }
 */
export function buildExe(o) {
  const stubLen = MZSTUB.length;
  const arOff = align(MZ_HDR_SIZE + stubLen, 16);
  const imageOff = align(arOff + AR1_HDR_SIZE, 16);
  const imageSize = o.image.length;
  const relocOff = align(imageOff + imageSize, 4);
  const total = relocOff + 4 * o.relocs.length;
  const f = Buffer.alloc(total);

  // --- MZ header: describes only header + stub, so real DOS loads just the stub
  const dosLen = MZ_HDR_SIZE + stubLen;
  f.write('MZ', 0, 'latin1');
  f.writeUInt16LE(dosLen % 512, 0x02);          // e_cblp
  f.writeUInt16LE(Math.ceil(dosLen / 512), 0x04); // e_cp
  f.writeUInt16LE(0, 0x06);                     // e_crlc
  f.writeUInt16LE(MZ_HDR_SIZE / 16, 0x08);      // e_cparhdr
  f.writeUInt16LE(0x0010, 0x0A);                // e_minalloc: 256 bytes for the stack
  f.writeUInt16LE(0xFFFF, 0x0C);                // e_maxalloc
  f.writeUInt16LE(0x0000, 0x0E);                // e_ss
  f.writeUInt16LE(0x0100, 0x10);                // e_sp (load module ~0x3A + 0x100 extra)
  f.writeUInt16LE(0, 0x12);                     // e_csum
  f.writeUInt16LE(0, 0x14);                     // e_ip
  f.writeUInt16LE(0, 0x16);                     // e_cs
  f.writeUInt16LE(MZ_HDR_SIZE, 0x18);           // e_lfarlc (>= 0x40: "new" header present)
  f.writeUInt16LE(0, 0x1A);                     // e_ovno
  f.writeUInt32LE(arOff, 0x3C);                 // e_lfanew
  MZSTUB.copy(f, MZ_HDR_SIZE);

  // --- AR1 header
  f.write('AR1\0', arOff, 'latin1');
  f.writeUInt16LE(AR1_HDR_SIZE, arOff + 4);
  f.writeUInt16LE(o.flags & 0xFFFF, arOff + 6);
  f.writeUInt32LE(imageOff, arOff + 8);
  f.writeUInt32LE(imageSize, arOff + 12);
  f.writeUInt32LE(o.bssSize >>> 0, arOff + 16);
  f.writeUInt32LE(o.stackSize >>> 0, arOff + 20);
  f.writeUInt32LE(o.entry >>> 0, arOff + 24);
  f.writeUInt32LE(relocOff, arOff + 28);
  f.writeUInt32LE(o.relocs.length, arOff + 32);
  f.writeUInt32LE(o.minExtra >>> 0, arOff + 36);
  f.writeUInt32LE(o.maxExtra >>> 0, arOff + 40);

  o.image.copy(f, imageOff);
  o.relocs.forEach((r, i) => f.writeUInt32LE(r >>> 0, relocOff + 4 * i));
  return f;
}

/**
 * Build a bare AR1 file (no MZ header/stub): the AR1 header at offset 0, then
 * image and relocations. For images DOS itself loads (ARMDOS.SYS, DEVICE=
 * drivers; ARCH.md section 14) where an 8086 stub is pointless.
 */
export function buildAr1(o) {
  const exe = buildExe(o);
  const a = exe.readUInt32LE(0x3C);
  const imageOff = exe.readUInt32LE(a + 8), relocOff = exe.readUInt32LE(a + 28);
  const shift = a;                                   // drop everything before the AR1 header
  const f = Buffer.from(exe.subarray(a));
  f.writeUInt32LE(imageOff - shift, 8);
  f.writeUInt32LE(relocOff - shift, 28);
  return f;
}

/** Build a self-relocating .COM (stub + image + relocs). */
export function buildSelfRelocCom(o) {
  const imageSize = align(o.image.length, 4);
  const stackTop = align(align(o.image.length + o.bssSize, 8) + o.stackSize, 8);
  const f = Buffer.alloc(COMSTUB_SIZE + imageSize + 4 * o.relocs.length);
  COMSTUB.copy(f, 0);
  const w = COMSTUB_WORDS_OFF;
  f.writeUInt32LE(COMSTUB_SIZE, w);
  f.writeUInt32LE(imageSize, w + 4);
  f.writeUInt32LE(o.relocs.length, w + 8);
  f.writeUInt32LE(stackTop, w + 12);
  f.writeUInt32LE(o.entry >>> 0, w + 16);
  o.image.copy(f, COMSTUB_SIZE);
  o.relocs.forEach((r, i) => f.writeUInt32LE(r >>> 0, COMSTUB_SIZE + imageSize + 4 * i));
  return f;
}

/**
 * Parse an ARM-DOS program file. Returns
 *   { kind: 'exe'|'com-selfreloc'|'com', mz?, ar?, image, relocs, entry, ... }
 * Throws on malformed input.
 */
export function parseProgram(buf) {
  if (buf.length >= 2 && buf[0] === 0x4D && buf[1] === 0x5A) return parseExe(buf);
  if (buf.length >= AR1_HDR_SIZE && buf.toString('latin1', 0, 4) === 'AR1\0') return parseAr1(buf, 0, 'ar1');
  if (buf.length >= COMSTUB_SIZE && buf.subarray(0, COMSTUB_WORDS_OFF).equals(COMSTUB.subarray(0, COMSTUB_WORDS_OFF))) {
    const w = COMSTUB_WORDS_OFF;
    const stubSize = buf.readUInt32LE(w), imageSize = buf.readUInt32LE(w + 4);
    const count = buf.readUInt32LE(w + 8), stackTop = buf.readUInt32LE(w + 12), entry = buf.readUInt32LE(w + 16);
    if (stubSize + imageSize + 4 * count !== buf.length) throw new Error('self-relocating .COM: sizes do not add up to the file size');
    const relocs = [];
    for (let i = 0; i < count; i++) relocs.push(buf.readUInt32LE(stubSize + imageSize + 4 * i));
    return { kind: 'com-selfreloc', stubSize, image: buf.subarray(stubSize, stubSize + imageSize),
             imageSize, relocs, entry, stackTop, flags: entry & 1 ? FLAG_THUMB : 0 };
  }
  return { kind: 'com', image: buf, imageSize: buf.length, relocs: [], entry: 0, flags: 0 };
}

export function parseExe(buf) {
  if (buf.length < MZ_HDR_SIZE) throw new Error('file too short for an MZ header');
  const mz = {
    e_magic: buf.toString('latin1', 0, 2),
    e_cblp: buf.readUInt16LE(2), e_cp: buf.readUInt16LE(4), e_crlc: buf.readUInt16LE(6),
    e_cparhdr: buf.readUInt16LE(8), e_minalloc: buf.readUInt16LE(0x0A), e_maxalloc: buf.readUInt16LE(0x0C),
    e_ss: buf.readUInt16LE(0x0E), e_sp: buf.readUInt16LE(0x10), e_csum: buf.readUInt16LE(0x12),
    e_ip: buf.readUInt16LE(0x14), e_cs: buf.readUInt16LE(0x16), e_lfarlc: buf.readUInt16LE(0x18),
    e_ovno: buf.readUInt16LE(0x1A), e_lfanew: buf.readUInt32LE(0x3C),
  };
  if (mz.e_magic !== 'MZ') throw new Error('no MZ signature');
  const a = mz.e_lfanew;
  if (a + AR1_HDR_SIZE > buf.length) throw new Error(`e_lfanew 0x${a.toString(16)} is outside the file (not an ARM-DOS EXE?)`);
  const dosLen = mz.e_cp * 512 - (mz.e_cblp ? 512 - mz.e_cblp : 0);
  const stubOff = mz.e_cparhdr * 16;
  return { ...parseAr1(buf, a, 'exe'), mz, stub: buf.subarray(stubOff, Math.max(stubOff, dosLen)) };
}

function parseAr1(buf, a, kind) {
  const ar = {
    sig: buf.toString('latin1', a, a + 4), hdrsize: buf.readUInt16LE(a + 4), flags: buf.readUInt16LE(a + 6),
    image_off: buf.readUInt32LE(a + 8), image_size: buf.readUInt32LE(a + 12), bss_size: buf.readUInt32LE(a + 16),
    stack_size: buf.readUInt32LE(a + 20), entry: buf.readUInt32LE(a + 24), reloc_off: buf.readUInt32LE(a + 28),
    reloc_count: buf.readUInt32LE(a + 32), min_extra: buf.readUInt32LE(a + 36), max_extra: buf.readUInt32LE(a + 40),
    reserved: [0, 1, 2, 3, 4].map(i => buf.readUInt32LE(a + 44 + 4 * i)),
  };
  if (ar.sig !== 'AR1\0') throw new Error(`no "AR1\\0" signature at e_lfanew 0x${a.toString(16)}`);
  if (ar.hdrsize < AR1_HDR_SIZE) throw new Error(`AR1 hdrsize ${ar.hdrsize} < 64`);
  if (ar.image_off + ar.image_size > buf.length) throw new Error('image extends past the end of the file');
  if (ar.reloc_off + 4 * ar.reloc_count > buf.length) throw new Error('relocation table extends past the end of the file');
  const relocs = [];
  for (let i = 0; i < ar.reloc_count; i++) relocs.push(buf.readUInt32LE(ar.reloc_off + 4 * i));
  return { kind, ar, image: buf.subarray(ar.image_off, ar.image_off + ar.image_size), imageSize: ar.image_size,
           relocs, entry: ar.entry, flags: ar.flags };
}

/**
 * Simulate the DOS loader: place the image at `base` inside a zeroed memory
 * buffer of the given size and apply the relocations. Returns the memory
 * (a Buffer covering [base, base+len)) and some facts for checking.
 */
export function relocate(prog, base, extra = 0) {
  const bss = prog.ar ? prog.ar.bss_size : 0;
  const mem = Buffer.alloc(prog.image.length + bss + extra);
  prog.image.copy(mem, 0);
  for (const off of prog.relocs) {
    if (off & 3) throw new Error(`relocation at 0x${off.toString(16)} is not word aligned`);
    if (off + 4 > prog.image.length) throw new Error(`relocation at 0x${off.toString(16)} is outside the image`);
    mem.writeUInt32LE((mem.readUInt32LE(off) + base) >>> 0, off);
  }
  return mem;
}
