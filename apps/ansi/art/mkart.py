#!/usr/bin/env python3
"""mkart.py - generates ANSI.ART, the ARM-DOS logo screen for ANSI.SYS.

    python3 apps/ansi/art/mkart.py > apps/ansi/art/ANSI.ART

An 80x23 picture in CP437 block characters and ANSI.SYS colour sequences
(ESC[..m, ESC[row;colH, ESC[2J). It ends with the cursor on line 24 and the
attributes reset, so COMMAND's prompt follows it without scrolling. Every
sequence used is one DOS 4's ANSI.SYS understands.
"""
import sys

W, H = 80, 23
# cell = (char, fg, bg)
scr = [[(' ', 7, 0) for _ in range(W)] for _ in range(H)]

def put(r, c, ch, fg, bg=None):
    if 0 <= r < H and 0 <= c < W:
        scr[r][c] = (ch, fg, scr[r][c][2] if bg is None else bg)

def text(r, c, s, fg, bg=None):
    for i, ch in enumerate(s):
        put(r, c + i, ch, fg, bg)

FULL, DARK, MED, LIGHT = '█', '▓', '▒', '░'
UP, DOWN = '▀', '▄'

# --- sky: a starfield on dark blue fading to black ----------------------------------
import random
random.seed(1988)
for r in range(H):
    for c in range(W):
        bg = 1 if r < 13 else 0
        scr[r][c] = (' ', 7, bg)
for _ in range(60):
    r, c = random.randrange(0, 11), random.randrange(W)
    if 1 <= r <= 9 and 4 <= c <= 76:
        continue                        # keep the stars out of the lettering
    put(r, c, random.choice(['.', '·', '∙', '*', '∙']), random.choice([7, 15, 11, 8]))

# --- the big letters: a 7-row block font --------------------------------------------
FONT = {
 'A': ["  ###  ", " ## ## ", "##   ##", "##   ##", "#######", "##   ##", "##   ##"],
 'R': ["###### ", "##   ##", "##   ##", "###### ", "## ##  ", "##  ## ", "##   ##"],
 'M': ["##   ##", "### ###", "#######", "## # ##", "##   ##", "##   ##", "##   ##"],
 '-': ["       ", "       ", "       ", " ##### ", "       ", "       ", "       "],
 'D': ["###### ", "##   ##", "##   ##", "##   ##", "##   ##", "##   ##", "###### "],
 'O': [" ##### ", "##   ##", "##   ##", "##   ##", "##   ##", "##   ##", " ##### "],
 'S': [" ######", "##     ", "##     ", " ##### ", "     ##", "     ##", "###### "],
}
word = "ARM-DOS"
top, left = 2, 5
x = left
# colour per row of the letters: a sunset gradient (yellow -> orange -> red)
rowfg = [14, 14, 14, 12, 12, 4, 4]
for ch in word:
    g = FONT[ch]
    for gy, line in enumerate(g):
        for gx, px in enumerate(line):
            if px == '#':
                # drop shadow first (down-right), then the letter
                sr, sc = top + gy + 1, x + gx + 1
                if scr[sr][sc][0] != FULL:
                    put(sr, sc, MED, 8, 0 if sr >= 13 else 1)
    for gy, line in enumerate(g):
        for gx, px in enumerate(line):
            if px == '#':
                put(top + gy, x + gx, FULL, rowfg[gy], 1)
    x += len(g[0]) + 3

# --- the horizon: a copper bar of shades --------------------------------------------
r = 11
for c in range(W):
    put(r, c, LIGHT, 3, 1)
    put(r + 1, c, MED, 11, 1)
for c in range(W):
    put(13, c, DARK, 3, 0)
    put(14, c, UP, 1, 0)

# --- the ARM chip -----------------------------------------------------------------
cr, cc = 16, 6
chip = [
    "  ║ ║ ║ ║  ",
    "╔═╧═╧═╧═╧═╗",
    "╢ ARM926  ╟",
    "╢ 100 MHz ╟",
    "╚═╤═╤═╤═╤═╝",
    "  ║ ║ ║ ║  ",
]
for i, line in enumerate(chip):
    for j, ch in enumerate(line):
        fg = 7
        if ch in '║': fg = 6
        if i in (2, 3) and 1 <= j <= 8: fg = 15 if ch != ' ' else 7
        bg = 8 if (1 <= i <= 4) else 0
        put(cr + i, cc + j, ch, fg, 0)
    # chip body shading
for i in (2, 3):
    for j in range(1, 9):
        ch, fg, _ = scr[cr + i][cc + j]
        put(cr + i, cc + j, ch, 15 if ch != ' ' else 7, 0)

# --- the texts ----------------------------------------------------------------------
text(16, 22, "ARM-DOS Version 4.00", 15)
text(17, 22, "─" * 20, 8)
text(18, 22, "The operating system for the", 7)
text(19, 22, "ARM Personal Computer", 11)
text(20, 22, "(C) Copyright Europa Micro Systems 1988", 3)

# a keyboard hint box on the right
bx, by, bw = 63, 16, 15
text(by, bx, "┌" + "─" * (bw - 2) + "┐", 9)
for i in range(3):
    text(by + 1 + i, bx, "│" + " " * (bw - 2) + "│", 9)
text(by + 4, bx, "└" + "─" * (bw - 2) + "┘", 9)
for i, (s, fg) in enumerate([(" 640K RAM OK ", 10), ("   ANSI.SYS  ", 14), ("  installed  ", 14)]):
    text(by + 1 + i, bx + 1, s, fg)

# --- encode ---------------------------------------------------------------------------
PC2ANSI = [0, 4, 2, 6, 1, 5, 3, 7]
out = ['\x1b[2J']
cur = None
for r in range(H):
    out.append('\x1b[%d;1H' % (r + 1))
    for c in range(W):
        ch, fg, bg = scr[r][c]
        a = (fg, bg)
        if a != cur:
            codes = ['0']
            if fg & 8: codes.append('1')
            codes.append(str(30 + PC2ANSI[fg & 7]))
            codes.append(str(40 + PC2ANSI[bg & 7]))
            out.append('\x1b[' + ';'.join(codes) + 'm')
            cur = a
        out.append(ch)
out.append('\x1b[0m\x1b[24;1H')
data = ''.join(out).encode('cp437')
sys.stdout.buffer.write(data)
