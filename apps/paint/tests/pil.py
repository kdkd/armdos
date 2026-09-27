#!/usr/bin/env python3
"""PIL helpers for apps/paint/tests/run.mjs (an independent reader and writer
of PCX and BMP: the files ARM Paint writes must be what modern tools see).

  pil.py info FILE          -> JSON {format, size, mode, pixels (hex), palette (hex, 768 bytes)}
  pil.py make KIND OUT      -> writes a 320x200 test picture with PIL (KIND = pcx | bmp | bigpcx)
                               and prints its JSON info
  pil.py png RAW W H OUT    -> RAW (1 byte a pixel, 0 = white, 1 = ink) as a PNG
"""
import json, sys
from PIL import Image

def info(path):
    im = Image.open(path)
    im.load()
    pal = im.getpalette() or []
    pal = (pal + [0] * 768)[:768]
    return {
        'format': im.format, 'size': list(im.size), 'mode': im.mode,
        'pixels': im.tobytes().hex() if im.mode in ('P', 'L') else '',
        'palette': bytes(pal).hex(),
    }

def make(kind, out):
    w, h = (400, 240) if kind == 'bigpcx' else (320, 200)
    im = Image.new('P', (w, h))
    pal = []
    for i in range(256):
        pal += [(i * 7) & 255, (i * 3 + 40) & 255, 255 - i]
    im.putpalette(pal)
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = ((x // 10) * 16 + (y // 10) * 3 + (x * y) % 7) & 255
    im.save(out, 'PCX' if kind != 'bmp' else 'BMP')
    print(json.dumps(info(out)))

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'info':
        print(json.dumps(info(sys.argv[2])))
    elif cmd == 'make':
        make(sys.argv[2], sys.argv[3])
    elif cmd == 'png':
        w, h = int(sys.argv[3]), int(sys.argv[4])
        raw = open(sys.argv[2], 'rb').read()
        im = Image.frombytes('L', (w, h), bytes(255 if b == 0 else 0 for b in raw))
        im.save(sys.argv[5])
