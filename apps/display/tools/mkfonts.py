#!/usr/bin/env python3
# apps/display/tools/mkfonts.py - makes apps/display/fonts/CPnnn.BIN, the code page
# fonts of EGA.CPI, from VileR's "Ultimate Oldschool PC Font Pack" v2.2 (CC BY-SA 4.0,
# see apps/display/fonts/README.md). Run once; the .BIN files are committed.
#
#   pip install fonttools
#   python3 apps/display/tools/mkfonts.py oldschool_pc_font_pack_v2.2_FULL.zip
#
# For each code page and cell height the 256 glyphs are: 00h-7Fh and every
# character that code page 437 also has = the IBM glyph of code page 437 (the
# same bytes as the ROM font, emu/fonts/); every other character = the glyph
# the pack's "Plus" variant of the same IBM font has for that Unicode character.
# Heights: 16 (IBM VGA 8x16), 14 (IBM VGA 8x14), 8 (IBM PC BIOS 8x8).
import io, sys, zipfile, os, hashlib
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'fonts')
FONTS = {16: 'IBM_VGA_8x16', 14: 'IBM_VGA_8x14', 8: 'IBM_BIOS'}
CPS = [437, 850, 860, 863, 865]

def load(z, name, h):
    f = TTFont(io.BytesIO(z.read('otb - Bm (linux bitmap)/%s.otb' % name)))
    cmap = f.getBestCmap()
    order = f.getGlyphOrder()
    strike = f['EBDT'].strikeData[0]
    # glyph -> metrics: the index subtable's (formats 2, 5) or the glyph's own
    sub_metrics = {}
    ascent = None
    for ist in f['EBLC'].strikes[0].indexSubTables:
        m = getattr(ist, 'metrics', None)
        for gi in range(ist.firstGlyphIndex, ist.lastGlyphIndex + 1):
            sub_metrics[order[gi]] = m
        if m is not None and m.height == h and ascent is None: ascent = m.horiBearingY
    def glyph(u):
        g = cmap.get(u)
        if g is None: return None
        b = strike[g]
        if 'imageData' not in b.__dict__: b.decompile()
        m = b.__dict__.get('metrics') or sub_metrics[g]
        cell = [0] * h
        top = ascent - m.horiBearingY
        for row in range(m.height):
            bits = b.getRow(row, bitDepth=1, metrics=m)
            v = int.from_bytes(bits, 'big') >> (len(bits) * 8 - m.width)   # m.width bits, MSB = left
            v = (v << (8 - m.width - m.horiBearingX)) & 0xFF if m.width + m.horiBearingX <= 8 else v >> (m.width + m.horiBearingX - 8)
            y = top + row
            if 0 <= y < h: cell[y] = v
        return bytes(cell)
    return glyph

def main(zpath):
    z = zipfile.ZipFile(zpath)
    for h, name in FONTS.items():
        base = load(z, 'Bm437_' + name, h)
        plus = load(z, 'BmPlus_' + name, h)
        # the 437 font by code: Bm437 maps 00h-FFh through Unicode (control glyphs too)
        uni437 = [ord(bytes([c]).decode('cp437')) for c in range(256)]
        low = '\u0000☺☻♥♦♣♠•◘○◙♂♀♪♫☼' \
              '►◄↕‼¶§▬↨↑↓→←∟↔▲▼'
        for c in range(32): uni437[c] = ord(low[c])
        uni437[0x7F] = 0x2302
        g437 = []
        for c in range(256):
            g = base(uni437[c]) if c else bytes(h)
            if g is None: g = base(c)
            assert g is not None and len(g) == h, (name, hex(c))
            g437.append(g)
        for cp in CPS:
            out = []
            for c in range(256):
                if c < 0x80:
                    out.append(g437[c]); continue
                u = ord(bytes([c]).decode('cp%d' % cp))
                if u in uni437[0x80:]:
                    out.append(g437[0x80 + uni437[0x80:].index(u)])
                else:
                    g = plus(u)
                    assert g is not None and len(g) == h, (cp, hex(c), hex(u))
                    out.append(g)
            data = b''.join(out)
            path = os.path.join(OUT, 'CP%d-%d.BIN' % (cp, h))
            open(path, 'wb').write(data)
            print(path, len(data), hashlib.sha256(data).hexdigest()[:16])

if __name__ == '__main__':
    main(sys.argv[1])
