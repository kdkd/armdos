#!/usr/bin/env python3
# apps/keyb/tools/mkdvorak.py - writes apps/keyb/kdf/KDFDV.ASM, the Dvorak
# layouts (DV, DL, DR) in the keyboard definition language of MS-DOS 4.00's
# KEYBOARD.SYS sources, so that apps/keyb/tools/kdfasm.mjs links them in with
# the DOS 4.00 layouts. Run it after changing the tables below.
import os
US_ROWS = ['`1234567890-=', 'qwertyuiop[]\\', "asdfghjkl;'", 'zxcvbnm,./']
US_SCANS = [[41] + list(range(2, 14)), list(range(16, 28)) + [43], list(range(30, 41)), list(range(44, 54))]
SHIFT = dict(zip(r"`1234567890-=[]\;',./", '~!@#$%^&*()_+{}|:"<>?'))
LAYOUTS = {
    'DV': ('Dvorak (ANSI X4.22-1983)',
           ['`1234567890[]', "',.pyfgcrl/=\\", 'aoeuidhtns-', ';qjkxbmwvz']),
    'DL': ('Dvorak for the left hand (as Windows "United States-Dvorak for left hand")',
           ['`[]/pfmlj4321', ';qbyursoо65=\\'.replace('о', '.'), '-kcdtheaz87', "'xgvwni,09"]),
    'DR': ('Dvorak for the right hand (as Windows "United States-Dvorak for right hand")',
           ['`1234jlmfp/[]', '56q.orsuyb;=\\', '78zaehtdck-', "90x,inwvg'"]),
}
us_key = {}          # char -> US scan
for row, scans in zip(US_ROWS, US_SCANS):
    for c, s in zip(row, scans): us_key[c] = s
def alt_code(c):     # what the BIOS stores for Alt + the US key of c (AH; AL = 0)
    s = us_key[c]
    if 2 <= s <= 13: return s + 0x76
    return s
CTRL = {'[': 0x1B, ']': 0x1D, '\\': 0x1C, '-': 0x1F, '6': 0x1E, '2': 0x00}
def ctrl_code(c):    # (AL, AH) of Ctrl + the US key of c, or None
    if c.isalpha(): return (ord(c) - 96, us_key[c])
    if c in CTRL: return (CTRL[c], us_key[c])
    return None
def ch(c):
    if c == "'": return '"\'"'
    if c == '\\': return "'\\'"
    return "'%s'" % c

def logic(code):
    return f"""
{code}_LOGIC:
   DW  {code}_LOGIC_END-$\t\t       ;; length
   DW  0\t\t\t       ;; special features
   OPTION EXIT_IF_FOUND
   IFKBD G_KB+P12_KB\t\t       ;; not the "/" of the numeric pad
   ANDF LC_E0
      EXIT_STATE_LOGIC
   ENDIFF
   IFF EITHER_CTL,NOT
      IFF  ALT_SHIFT\t\t       ;; Alt: the US key of the character
      ANDF R_ALT_SHIFT,NOT
         XLATT ALT_CASE
      ENDIFF
   ELSEF
      IFF EITHER_ALT,NOT\t       ;; Ctrl: likewise
         XLATT CTRL_CASE
      ENDIFF
   ENDIFF
   IFF EITHER_ALT,NOT
   ANDF EITHER_CTL,NOT
      IFF EITHER_SHIFT
         XLATT NON_ALPHA_UPPER
         IFF CAPS_STATE
            XLATT ALPHA_LOWER
         ELSEF
            XLATT ALPHA_UPPER
         ENDIFF
      ELSEF
         XLATT NON_ALPHA_LOWER
         IFF CAPS_STATE
            XLATT ALPHA_UPPER
         ELSEF
            XLATT ALPHA_LOWER
         ENDIFF
      ENDIFF
   ENDIFF
   EXIT_STATE_LOGIC
{code}_LOGIC_END:
"""

def state(code, name, sid, opts, entries, fmt):
    lines = [f"   DW\t {code}_{name}_END-$\t\t       ;; length of state section",
             f"   DB\t {sid}\t\t       ;; State ID",
             "   DW\t ANY_KB \t\t       ;; Keyboard Type",
             "   DB\t -1,-1\t\t\t       ;; Buffer entry for error character",
             f"   DW\t {code}_{name}_T_END-$\t       ;; Size of xlat table",
             f"   DB\t {opts}\t\t       ;; xlat options",
             f"   DB\t {len(entries)}\t\t\t       ;; number of entries"]
    lines += ['   DB\t ' + fmt(e) for e in entries]
    lines += [f"{code}_{name}_T_END:", "   DW\t 0\t\t\t       ;; null table", f"{code}_{name}_END:"]
    return '\n'.join(lines) + '\n'

out = [""";; KDFDV.ASM - ARM-DOS keyboard definition file: the Dvorak layouts.
;; Written by apps/keyb/tools/mkdvorak.py in the keyboard definition language
;; of MS-DOS 4.00's KEYBOARD.SYS sources (KEYBMAC.INC). MS-DOS 4.00 had no
;; Dvorak layouts; these are ARM-DOS's own:
"""]
for code, (desc, _) in LAYOUTS.items(): out.append(f";;   {code}  {desc}\n")
out.append("""
\tINCLUDE KEYBSHAR.INC
\tINCLUDE POSTEQU.INC
\tINCLUDE KEYBMAC.INC
""")
for code in LAYOUTS:
    out.append(f"\tPUBLIC {code}_LOGIC\n")
    for cp in (437, 850, 860, 863, 865): out.append(f"\tPUBLIC {code}_{cp}_XLAT\n")
out.append("""
CODE\tSEGMENT PUBLIC 'CODE'
\tASSUME CS:CODE,DS:CODE
STANDARD_TABLE\t    EQU   TYPE_2_TAB+ASCII_ONLY
""")
for code, (desc, rows) in LAYOUTS.items():
    assert [len(r) for r in rows] == [len(r) for r in US_ROWS], code
    alpha_lo, alpha_up, na_lo, na_up, alt, ctrl = [], [], [], [], [], []
    for row, usrow, scans in zip(rows, US_ROWS, US_SCANS):
        for c, u, s in zip(row, usrow, scans):
            if c == u: continue
            if c.isalpha():
                alpha_lo.append((s, c)); alpha_up.append((s, c.upper()))
            else:
                na_lo.append((s, c)); na_up.append((s, SHIFT[c]))
            alt.append((s, 0, alt_code(c)))
            cc = ctrl_code(c)
            ctrl.append((s, cc[0], cc[1]) if cc else (s, -1, -1))
    out.append(f"\n;;***************************************\n;; {code}: {desc}\n;;***************************************\n")
    out.append(logic(code))
    out.append(f"\n{code}_COMMON_XLAT:\n   DW\t {code}_COMMON_END-$\t       ;; length of section\n   DW\t -1\t\t\t       ;; code page\n")
    fmt2 = lambda e: f"{e[0]},{ch(e[1])}"
    fmt3 = lambda e: f"{e[0]},{e[1]},{e[2]}"
    out.append(state(code, 'ALT', 'ALT_CASE', 'TYPE_2_TAB', alt, fmt3))
    out.append(state(code, 'CTRL', 'CTRL_CASE', 'TYPE_2_TAB', ctrl, fmt3))
    out.append(state(code, 'AL_LO', 'ALPHA_LOWER', 'STANDARD_TABLE', alpha_lo, fmt2))
    out.append(state(code, 'AL_UP', 'ALPHA_UPPER', 'STANDARD_TABLE', alpha_up, fmt2))
    out.append(state(code, 'NA_LO', 'NON_ALPHA_LOWER', 'STANDARD_TABLE', na_lo, fmt2))
    out.append(state(code, 'NA_UP', 'NON_ALPHA_UPPER', 'STANDARD_TABLE', na_up, fmt2))
    out.append(f"   DW\t 0\t\t\t       ;; last state\n{code}_COMMON_END:\n")
    for cp in (437, 850, 860, 863, 865):   # nothing code-page specific: ASCII only
        out.append(f"\n{code}_{cp}_XLAT:\n   DW\t {code}_{cp}_END-$\t       ;; length of section\n   DW\t {cp}\t\t\t       ;; code page\n   DW\t 0\t\t\t       ;; last state\n{code}_{cp}_END:\n")
out.append("\nCODE\tENDS\n\tEND\n")
dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'kdf', 'KDFDV.ASM')
open(dst, 'w').write(''.join(out))
print('wrote', dst)
