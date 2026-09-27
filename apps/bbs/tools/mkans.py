#!/usr/bin/env python3
"""Generates the ANSI screens of The ARM Pit BBS (apps/bbs/data/BBS/DISPLAY,
BULLETIN) - raw ANSI with CP437 block characters, as TheDraw would have
saved them. Run: python3 apps/bbs/tools/mkans.py"""
import os
HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, '..', 'data', 'BBS')
E = '\x1b['
def sgr(*a): return E + ';'.join(str(x) for x in a) + 'm'
FULL, UP, DN = '\xdb', '\xdf', '\xdc'

FONT = {   # 5x5 block font, '#' = full block
 'A': [" ### ", "#   #", "#####", "#   #", "#   #"],
 'R': ["#### ", "#   #", "#### ", "#  # ", "#   #"],
 'M': ["#   #", "## ##", "# # #", "#   #", "#   #"],
 'P': ["#### ", "#   #", "#### ", "#    ", "#    "],
 'I': ["###", " # ", " # ", " # ", "###"],
 'T': ["#####", "  #  ", "  #  ", "  #  ", "  #  "],
 'H': ["#   #", "#   #", "#####", "#   #", "#   #"],
 'E': ["#####", "#    ", "#### ", "#    ", "#####"],
 ' ': ["  ", "  ", "  ", "  ", "  "],
}
def big(text, colors, indent=0):
    rows = []
    for r in range(5):
        line = ' ' * indent
        for ch in text:
            g = FONT[ch][r]
            col = colors[r % len(colors)]
            line += col + ''.join(FULL if c == '#' else ' ' for c in g) + '  '
        rows.append(line.rstrip() + sgr(0))
    return rows

def cp(s): return s.encode('latin1')

def write(name, lines):
    p = os.path.join(DATA, name)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'wb') as f:
        f.write(cp('\r\n'.join(lines) + '\r\n' + sgr(0)))
        f.write(b'\x1a')

# ---------------------------------------------------------------- WELCOME
reds = [sgr(0, 1, 31), sgr(0, 31), sgr(0, 1, 33), sgr(0, 33), sgr(0, 31)]
L = [sgr(0) + E + '2J' + E + 'H']
L.append(sgr(0, 34) + '  ' + DN * 74)
L.append(sgr(0, 1, 37, 44) + '  T h e   A R M   P i t   B B S' + ' ' * 17 + 'Hollywood, FL  V.90 56K     ' + sgr(0))
L.append(sgr(0, 34) + '  ' + UP * 74)
L.append(sgr(0, 1, 33) + '     Now with a digital line!  ' + sgr(1, 37) + '56K V.90' + sgr(1, 33) + ' callers welcome' +
         sgr(0, 33) + '  (2400 still works, too)' + sgr(0))
for row in big('THE ARM PIT', reds, indent=4):
    L.append(row)
L.append('')
L.append(sgr(0, 36) + '      "Where the ' + sgr(1, 37) + 'RISC' + sgr(0, 36) + ' takers hang out"' + sgr(1, 30) +
         '   \xfa\xfa\xfa' + sgr(0, 36) + '   Sysop: Europa   Node 1 of 1')
L.append(sgr(0, 34) + '  ' + '\xc4' * 74)
L.append(sgr(0, 37) + '  Message bases ' + sgr(1, 30) + '\xfa' + sgr(0, 37) + ' Files for ARM-DOS ' + sgr(1, 30) + '\xfa' + sgr(0, 37) +
         ' Four door games ' + sgr(1, 30) + '\xfa' + sgr(0, 37) + ' ZMODEM ' + sgr(1, 30) + '\xfa' + sgr(0, 37) + ' Est. 1987')
write('DISPLAY/WELCOME.ANS', L)

# ---------------------------------------------------------------- GOODBYE
G = []
G.append(sgr(0, 34) + '  ' + DN * 60)
G.append(sgr(0, 1, 37, 44) + '   Thank you for calling The ARM Pit!' + ' ' * 23 + ' ' + sgr(0))
G.append(sgr(0, 34) + '  ' + UP * 60)
G.append(sgr(0, 36) + '   Call again soon.  Same number, same bat-time: ' + sgr(1, 33) + '555-1989' + sgr(0))
G.append(sgr(0, 36) + '   Remember: never type ' + sgr(1, 31) + '+++' + sgr(0, 36) + ' in a message.  Just trust us.' + sgr(0))
G.append('')
write('DISPLAY/GOODBYE.ANS', G)

# ---------------------------------------------------------------- BULLETIN MENU
B = []
B.append(sgr(0, 1, 37, 44) + '  B U L L E T I N S  ' + sgr(0))
B.append('')
items = [('1', 'Rules of The ARM Pit (read this!)'),
         ('2', 'News: ARM-DOS 4.00 is here'),
         ('3', 'How to download with ZMODEM'),
         ('4', 'Other boards worth a call'),
         ('5', 'A history of The ARM Pit'),
         ('6', 'Meet the sysop'),
         ('7', 'The Wall of Fame'),
         ('8', 'The last 10 callers'),
         ('9', "Today's news from the doors"),
         ('10', 'SFARM, the user group')]
for n, t in items:
    B.append(sgr(0, 1, 37) + ' ' * (3 - len(n)) + '[' + sgr(1, 33) + n + sgr(1, 37) + '] ' + sgr(0, 36) + t + sgr(0))
write('BULLETIN/BULLET.ANS', B)
# ---------------------------------------------------------------- MAIN MENU
H, V = '\xcd', '\xba'
M = [sgr(0) + E + '2J' + E + 'H']
M.append(sgr(0, 1, 34) + '  \xc9' + H * 72 + '\xbb')
M.append(sgr(0, 1, 34) + '  ' + V + sgr(0, 1, 37, 44) + '  T H E   A R M   P I T  ' + sgr(0, 37, 44) + '   \xb3   M A I N   M E N U' + ' ' * 23 + sgr(0, 1, 34) + V)
M.append(sgr(0, 1, 34) + '  \xcc' + H * 72 + '\xb9')
def item(k, t, d):
    return sgr(0, 1, 34) + '[' + sgr(1, 33) + k + sgr(1, 34) + '] ' + sgr(0, 1, 37) + t.ljust(14) + sgr(0, 36) + d.ljust(14)
rows = [(('M', 'Messages', 'read & post'), ('F', 'Files', 'up/download')),
        (('B', 'Bulletins', 'news, rules'), ('D', 'Doors', 'online games')),
        (('W', "Who's online", 'who is on'), ('U', 'Userlog', 'our callers')),
        (('C', 'Comment', 'to the sysop'), ('S', 'Settings', 'you & ANSI')),
        (('L', 'Last callers', 'who called'), ('G', 'Goodbye', 'log off')),
        (('?', 'Help', 'this menu'), None)]
M.append(sgr(0, 1, 34) + '  ' + V + ' ' * 72 + V)
for a, b in rows:
    right = item(*b) if b else ' ' * 32
    M.append(sgr(0, 1, 34) + '  ' + V + '   ' + item(*a) + '   ' + right + '  ' + sgr(0, 1, 34) + V)
M.append(sgr(0, 1, 34) + '  ' + V + ' ' * 72 + V)
M.append(sgr(0, 1, 34) + '  \xc8' + H * 72 + '\xbc' + sgr(0))
write('DISPLAY/MAIN.ANS', M)
print('ok')
