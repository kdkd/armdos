#!/usr/bin/env python3
"""scr2txt.py FILE.SCR [--attr] - an 80x25 text screen (4000 bytes of CP437
character/attribute pairs, as SCRDUMP.COM saves it) as UTF-8 text, 25 lines,
trailing blanks removed; --attr adds the attribute bytes as hex rows."""
import sys
b = open(sys.argv[1], 'rb').read()
cp = bytes(range(256)).decode('cp437')
for r in range(25):
    row = b[r * 160:(r + 1) * 160]
    print(''.join(cp[row[i]] if row[i] >= 32 else ' ' for i in range(0, 160, 2)).rstrip())
if '--attr' in sys.argv:
    for r in range(25):
        print(''.join('%02X' % b[r * 160 + i] for i in range(1, 160, 2)))
