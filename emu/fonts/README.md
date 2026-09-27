# Fonts

Raw bitmap text-mode fonts for the emulator.

| File          | Size       | Layout                                              |
|---------------|------------|-----------------------------------------------------|
| `vga8x16.bin` | 4096 bytes | 256 glyphs x 16 bytes (one byte per row), 8x16 cells |
| `cga8x8.bin`  | 2048 bytes | 256 glyphs x 8 bytes (one byte per row), 8x8 cells   |
| `mda9x14.bin` | 3584 bytes | 256 glyphs x 14 bytes, the 8 left columns of the 9x14 MDA cell |

Glyph `n` is at byte offset `n * height`. Each byte is one pixel row, top row first, and the
most significant bit is the leftmost pixel. Glyphs follow code page 437 order (0x00-0xFF),
including the control-character glyphs 0x00-0x1F (smiley, card suits, arrows and so on),
the house glyph at 0x7F, and box-drawing characters 0xB0-0xDF.

## Source

- **The Ultimate Oldschool PC Font Pack** v2.2 (2020-11-21), by VileR
  - Home page: https://int10h.org/oldschool-pc-fonts/
  - Archive: https://int10h.org/oldschool-pc-fonts/download/oldschool_pc_font_pack_v2.2_FULL.zip
    (sha256 `21b3c0a3770ef0afc46564760613d7b078f4fcc9ed93db4b829a440b68822e08`)
- Files used from the archive:
  - `fon - Bm (windows bitmap)/Bm437_IBM_VGA_8x16.FON` -> `vga8x16.bin`
    (sha256 `b6bcbfbadeb1af3532791b2e42d3806e281dd6e335ebc02c46a397919ec0854b`)
  - `fon - Bm (windows bitmap)/Bm437_IBM_BIOS.FON` -> `cga8x8.bin`
    (sha256 `1eee9aea62b1aa6cd6970020f5fef82b4da2319994e1feb2eef9819de7fd7dfc`)
  - `fon - Bm (windows bitmap)/Bm437_IBM_MDA.FON` -> `mda9x14.bin`
    (sha256 `10a53013bb147b05f2eab8198d24468cde5f3ef4d84fc1f629f57e6f93a58d65`)

These are the pack's "437" variants, which contain exactly the original 256-character CP437
set as it appears in the IBM VGA 8x16 ROM font and the IBM PC BIOS (CGA) 8x8 font.

## License

The font pack is licensed under the Creative Commons Attribution-ShareAlike 4.0 International
License (CC BY-SA 4.0): https://creativecommons.org/licenses/by-sa/4.0/

Required attribution:

> The Ultimate Oldschool PC Font Pack (c) 2016-2020 VileR, https://int10h.org/oldschool-pc-fonts/ ,
> licensed under CC BY-SA 4.0 (https://creativecommons.org/licenses/by-sa/4.0/).
> `vga8x16.bin` and `cga8x8.bin` were converted from `Bm437_IBM_VGA_8x16.FON` and
> `Bm437_IBM_BIOS.FON` to raw bitmaps; the glyph images themselves are unchanged.

Because of the ShareAlike term, these two `.bin` files (and any modified versions of them)
must also be distributed under CC BY-SA 4.0. That applies only to the font data, not to the
emulator code that loads it.

## How the .bin files were made

Each `.FON` file is a 16-bit Windows NE executable that holds a single `RT_FONT` (0x8008)
resource in FNT version 2.00 format, with `dfFirstChar`=0x00, `dfLastChar`=0xFF,
`dfCharSet`=255 (OEM) and every character 8 pixels wide. A short Python 3 script:

1. read `e_lfanew` at offset 0x3C, then found the NE resource table (at NE header + 0x24)
   and the first `RT_FONT` entry, and scaled its offset and length by the alignment shift;
2. in the FNT resource, read `dfPixHeight` (at 0x58: 16 or 8) and the character table at
   0x76, which has 4-byte entries (`width`, `offset`);
3. for each character from 0x00 to 0xFF, copied `dfPixHeight` bytes from its bitmap offset.
   An 8-pixel-wide FNT glyph is one byte column, so each byte is a row with MSB = leftmost
   pixel, which is already the raw ROM-font layout.

No scaling, re-ordering or editing was done. The results were checked by eye by rendering
glyphs as ASCII art: 'A' (0x41), smiley (0x01/0x02), heart (0x03), full block (0xDB),
horizontal line (0xC4, row 7 of 16 and row 4 of 8), house (0x7F), box-drawing (0xB3, 0xC9)
and descenders ('g'). The VGA glyphs use the true 16-row cell, not an 8x14 font stretched to 16 rows.

## mda9x14.bin (the Hercules / MDA character ROM)

`Bm437_IBM_MDA.FON` holds 9-pixel-wide glyphs (two FNT byte columns, 14 rows). The first
column byte of each glyph is stored, like the 8-wide fonts above. The 9th pixel column was
checked against the MDA hardware rule for all 256 glyphs: it equals column 8 for C0h-DFh and
is blank for every other character, so the renderer (emu/render-hgc.mjs) generates it the
way the card does and nothing is lost. `emu/dev/mdafont.mjs` carries the same bytes as base64
(so the browser and node load it as a module); it is covered by the same attribution and
CC BY-SA 4.0 licence as the .bin files:

> The Ultimate Oldschool PC Font Pack (c) 2016-2020 VileR, https://int10h.org/oldschool-pc-fonts/ ,
> licensed under CC BY-SA 4.0 (https://creativecommons.org/licenses/by-sa/4.0/).
> `mda9x14.bin` and `emu/dev/mdafont.mjs` were converted from `Bm437_IBM_MDA.FON`; the glyph
> images themselves are unchanged.

