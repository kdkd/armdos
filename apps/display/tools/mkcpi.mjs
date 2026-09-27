#!/usr/bin/env node
// apps/display/tools/mkcpi.mjs - builds EGA.CPI, the code page font file for
// DISPLAY.SYS / MODE CON CP PREPARE, in the "FONT" file format of MS-DOS 4.00's
// EGA.CPI (DEV/DISPLAY/EGA/CPI-HEAD.ASM, 437-CPI.ASM ...): code pages 437, 850,
// 860, 863 and 865, each with 8x16, 8x14 and 8x8 fonts (apps/display/fonts/,
// made from VileR's font pack, CC BY-SA 4.0 - see fonts/README.md).
//
//   node apps/display/tools/mkcpi.mjs OUT.CPI
//
// Layout: FontFileHeader (FFh "FONT   ", 8 reserved, 1 pointer of type 1 ->
// the FontInfoHeader at 17h), FontInfoHeader (count of code pages), then per
// code page a 1Ch-byte CodePageEntryHeader (size, next header, device type 1,
// "EGA     ", code page, 6 reserved, pointer to its CodePageInfoHeader), the
// CodePageInfoHeader (1, number of fonts, length of the font data) and the
// fonts (height, width 8, aspect 0,0, 256 characters, the bitmaps).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const CPI_PAGES = [437, 850, 860, 863, 865];
export const CPI_HEIGHTS = [16, 14, 8];

export function fontFile(cp, h) {
  return fs.readFileSync(path.join(HERE, '..', 'fonts', `CP${cp}-${h}.BIN`));
}

export function buildCpi(pages = CPI_PAGES) {
  const parts = [];
  let off = 0;
  const push = (b) => { parts.push(b); off += b.length; };
  const hdr = Buffer.alloc(0x19);
  hdr[0] = 0xFF; hdr.write('FONT   ', 1, 'latin1');
  hdr.writeUInt16LE(1, 16); hdr[18] = 1; hdr.writeUInt32LE(0x17, 19);
  hdr.writeUInt16LE(pages.length, 0x17);
  push(hdr);
  for (const [i, cp] of pages.entries()) {
    const fonts = CPI_HEIGHTS.map((h) => {
      const bits = fontFile(cp, h);
      if (bits.length !== h * 256) throw new Error(`CP${cp}-${h}.BIN: ${bits.length} bytes`);
      const fh = Buffer.alloc(6);
      fh[0] = h; fh[1] = 8; fh.writeUInt16LE(256, 4);
      return Buffer.concat([fh, bits]);
    });
    const data = Buffer.concat(fonts);
    const cpeh = Buffer.alloc(0x1C);
    const cpihOff = off + 0x1C;
    const next = i === pages.length - 1 ? 0 : cpihOff + 6 + data.length;
    cpeh.writeUInt16LE(0x1C, 0); cpeh.writeUInt32LE(next, 2); cpeh.writeUInt16LE(1, 6);
    cpeh.write('EGA     ', 8, 'latin1'); cpeh.writeUInt16LE(cp, 16); cpeh.writeUInt32LE(cpihOff, 24);
    push(cpeh);
    const cpih = Buffer.alloc(6);
    cpih.writeUInt16LE(1, 0); cpih.writeUInt16LE(fonts.length, 2); cpih.writeUInt16LE(data.length, 4);
    push(cpih);
    push(data);
  }
  return Buffer.concat(parts);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const out = process.argv[2];
  const img = buildCpi();
  fs.writeFileSync(out, img);
  console.log(`mkcpi: ${out}: ${img.length} bytes, code pages ${CPI_PAGES.join(' ')}`);
}
