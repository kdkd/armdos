# Code page fonts (EGA.CPI)

Raw bitmap fonts for DISPLAY.SYS's EGA.CPI (`../tools/mkcpi.mjs` packs them): `CPnnn-h.BIN`
= code page nnn (437, 850, 860, 863, 865) at h lines (16, 14, 8), 256 glyphs x h bytes, one
byte per pixel row, MSB = leftmost pixel, glyph `n` at `n * h` - the layout of
emu/fonts/*.bin.

## Source

- **The Ultimate Oldschool PC Font Pack** v2.2 (2020-11-21), by VileR
  - Home page: https://int10h.org/oldschool-pc-fonts/
  - Archive: https://int10h.org/oldschool-pc-fonts/download/oldschool_pc_font_pack_v2.2_FULL.zip
    (sha256 `21b3c0a3770ef0afc46564760613d7b078f4fcc9ed93db4b829a440b68822e08`, the same archive
    as emu/fonts)
- Files used from the archive (`otb - Bm (linux bitmap)/`):
  - `Bm437_IBM_VGA_8x16.otb` (`d6875d20...`), `BmPlus_IBM_VGA_8x16.otb` (`c0dd65b2...`) -> the 16-line fonts
  - `Bm437_IBM_VGA_8x14.otb` (`6846161e...`), `BmPlus_IBM_VGA_8x14.otb` (`9f065bd4...`) -> the 14-line fonts
  - `Bm437_IBM_BIOS.otb` (`fc346c28...`), `BmPlus_IBM_BIOS.otb` (`b206520d...`) -> the 8-line fonts

The "437" variants are the original IBM VGA / PC BIOS character sets; the "Plus" variants are the
same IBM fonts extended by VileR to some 780 Unicode characters, in the same style, including
every character of the DOS code pages.

## How they were made

`../tools/mkfonts.py` (Python 3 with fontTools), run once:

    python3 apps/display/tools/mkfonts.py oldschool_pc_font_pack_v2.2_FULL.zip

For each code page and height: characters 00h-7Fh, and every character the code page shares with
code page 437, are the 437 font's glyphs (so those are exactly the ROM font's: CP437-16.BIN =
emu/fonts/vga8x16.bin and CP437-8.BIN = emu/fonts/cga8x8.bin, byte for byte); every other
character (e.g. 850's `Ø` at 9Dh, `Í` at D6h) is the Plus font's glyph for that Unicode
character (Python's cp850/cp860/cp863/cp865 codecs give the mapping). Glyphs are rendered from
the OTB's EBDT/EBLC bitmap strike into the full cell (some Plus glyphs are stored 15 lines high or
7 pixels wide; they are placed at their metrics' offsets). No glyph was edited. They were
checked by eye (all five code pages' upper halves drawn side by side) and are checked by
apps/display/tests.

| file | sha256 |
|---|---|
| `CP437-14.BIN` | `657ca6588b6bf729...` |
| `CP437-16.BIN` | `a8bad6fd78475a6b...` |
| `CP437-8.BIN` | `75c79a7e7fa423dd...` |
| `CP850-14.BIN` | `7fe77c30d624ea79...` |
| `CP850-16.BIN` | `1619cf1da21e9d7a...` |
| `CP850-8.BIN` | `41ee57f736dc5eed...` |
| `CP860-14.BIN` | `60bad8c23c658214...` |
| `CP860-16.BIN` | `c165470c4d83c385...` |
| `CP860-8.BIN` | `426dfa8ce71cd8a6...` |
| `CP863-14.BIN` | `487ed67d090366a4...` |
| `CP863-16.BIN` | `4e67736687588d54...` |
| `CP863-8.BIN` | `bdd86463988a7ceb...` |
| `CP865-14.BIN` | `49bc0dc88d3756bb...` |
| `CP865-16.BIN` | `0dd92503314e8858...` |
| `CP865-8.BIN` | `b01f7cd559bfd5c7...` |

## License

The font pack is licensed under the Creative Commons Attribution-ShareAlike 4.0 International
License (CC BY-SA 4.0): https://creativecommons.org/licenses/by-sa/4.0/

Required attribution:

> The Ultimate Oldschool PC Font Pack (c) 2016-2020 VileR, https://int10h.org/oldschool-pc-fonts/ ,
> licensed under CC BY-SA 4.0 (https://creativecommons.org/licenses/by-sa/4.0/).
> The `CPnnn-h.BIN` files and EGA.CPI were made from its `Bm437_IBM_VGA_8x16`, `Bm437_IBM_VGA_8x14`,
> `Bm437_IBM_BIOS` and `BmPlus_*` fonts of the same names by selecting and re-arranging glyphs
> into DOS code page order; the glyph images themselves are unchanged.

Because of the ShareAlike term, these files, EGA.CPI (which is made of them) and any modified
versions must also be distributed under CC BY-SA 4.0. That applies only to the font data, not to
DISPLAY.SYS or the tools.
