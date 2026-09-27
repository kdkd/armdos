#!/usr/bin/env python3
"""msgtab.py - build-time replacement for Microsoft's BUILDMSG.EXE.

Resolves an MS-DOS 4.0 message skeleton (UTIL.SKL) against the country
message file (apps/mslib/msg/USA-MS.MSG, Microsoft's MIT-licensed text) and
writes a C table for the ARM-DOS message retriever (apps/mslib/src/msgret.c):

    python3 msgtab.py UTIL.SKL -o util_msg.c [--msg USA-MS.MSG]
                      [--subst OLD=NEW ...] [--extra NUM=TEXT ...]
    python3 msgtab.py --common -o common_msg.c    # EXTEND (class 1) + PARSE (class 2)

Rules (as BUILDMSG): ":use [n] SECTIONm" takes message m of a MSG section as
number n (default m); ":def n text" is the skeleton's own text, but the
utility's MSG section wins where it has message n (Microsoft shipped the MSG
text).  Classes 1 and 2 are extended/parse errors, letters are utility
messages (displayed with DH = 0FFh).  --subst rewrites bytes in every text
(ARM-DOS branding: MSDOS -> ARMDOS).
"""
import re, sys, os, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MSG = os.path.join(HERE, '..', 'msg', 'USA-MS.MSG')

NAMES = {'CR': 13, 'LF': 10, 'BELL': 7, 'BEEP': 7, 'TAB': 9, 'NULL': 0, 'NUL': 0,
         'COLON': ord(':'), 'FORMFD': 12, 'BS': 8, 'ESC': 27, 'BLANK': 32, 'SPACE': 32}


def read(p):
    return open(p, 'rb').read().decode('latin-1').replace('\r', '')


def strip_comment(s):
    out, q = '', None
    for ch in s:
        if q:
            out += ch
            if ch == q:
                q = None
        elif ch in '"\'':
            q = ch
            out += ch
        elif ch == ';':
            break
        else:
            out += ch
    return out.strip()


TOK = re.compile(r'"[^"]*"|\'[^\']*\'|[0-9][0-9A-Fa-f]*[Hh]\b|\d+|[A-Za-z_$][A-Za-z0-9_$]*')


def render(t):
    """MASM-style 'db' operand list -> bytes."""
    out = bytearray()
    for k in TOK.findall(strip_comment(t)):
        if k[0] in '"\'':
            out += k[1:-1].encode('latin-1')
        elif re.fullmatch(r'[0-9][0-9A-Fa-f]*[Hh]', k):
            out.append(int(k[:-1], 16) & 0xFF)
        elif k.isdigit():
            out.append(int(k) & 0xFF)
        elif k.upper() in NAMES:
            out.append(NAMES[k.upper()])
        else:
            out += ('<' + k + '>').encode('latin-1')
    return bytes(out)


def load_msg(path):
    sections, cur, last = {}, None, None
    for ln in read(path).split('\n')[1:]:
        m = re.match(r'^([A-Z0-9]+)\s+([0-9a-fA-F]{4})\s+([0-9a-fA-F]{4})\s*$', ln)
        if m:
            cur = m.group(1)
            sections[cur] = {}
            continue
        m = re.match(r'^(\d{4}) U (\d{4})\s+(.*)$', ln)
        if m and cur:
            n = int(m.group(1))
            sections[cur][n] = m.group(3)
            last = (cur, n)
            continue
        if (ln.startswith('\t') or ln.startswith(' ')) and last and ln.strip():
            sections[last[0]][last[1]] += ',' + ln.strip()
    return sections


def parse_skl(path, sections, util_section=None):
    util, cls, res, cur = None, None, [], None
    for ln in read(path).split('\n'):
        s = ln.strip()
        if not s or s.startswith(';'):
            continue
        if s.startswith(':'):
            cur = None
            parts = re.split(r'\s+', strip_comment(s), maxsplit=3)
            kw = parts[0].lower()
            if kw == ':util':
                util = parts[1].upper()
            elif kw == ':class':
                cls = parts[1].upper()
            elif kw == ':use':
                a = parts[1:]
                if a and re.fullmatch(r'-?\d+', a[0]):
                    num, ref = int(a[0]), (a[1] if len(a) > 1 else '')
                else:
                    num, ref = None, a[0]
                m = re.fullmatch(r'([A-Za-z]+?)(\d+)', ref)
                if not m:
                    continue
                sec, rn = m.group(1).upper(), int(m.group(2))
                if num is None:
                    num = rn
                txt = sections.get(sec, {}).get(rn)
                if txt is None:
                    continue            # e.g. EXTEND999: the retriever's own fallback
                b = render(txt)
                # an extended/parse error text taken into a utility class ends
                # with CR LF (the retriever adds it only for classes 1 and 2):
                # the real FDISK 1 /Q prints "Invalid parameter" on a line
                if sec in ('EXTEND', 'PARSE') and cls not in ('1', '2'):
                    b += b'\r\n'
                res.append([cls, num, b])
            elif kw == ':def':
                m = re.match(r'^:def\s+(\d+)\s*(.*)$', strip_comment(s), re.I)
                cur = [cls, int(m.group(1)), m.group(2)]
                res.append(cur)
            continue
        if cur is not None:
            cur[2] += ',' + strip_comment(s)
    usec = util_section or {'LABL': 'LABL'}.get(util, util)
    for r in res:
        if isinstance(r[2], str):
            mt = sections.get(usec, {}).get(r[1])
            r[2] = render(mt) if mt is not None else render(r[2])
    return util, res


def cls_code(c):
    return {'1': 1, '2': 2}.get(c, 0xFF)


def c_string(b):
    out, prev_hex = '"', False
    for x in b:
        ch = chr(x)
        if x == 34 or x == 92:
            out += '\\' + ch
            prev_hex = False
        elif 32 <= x < 127 and not (prev_hex and ch in '0123456789abcdefABCDEF'):
            out += ch
            prev_hex = False
        else:
            out += '\\%03o' % x
            prev_hex = False
    return out + '"'


def emit(rows, name, out):
    out.append('const struct msg_entry %s[] = {' % name)
    for c, n, b in rows:
        out.append('    { 0x%02X, %d, %d, %s },' % (c, n, len(b), c_string(b)))
    out.append('    { 0, 0, 0, 0 }')
    out.append('};')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('skl', nargs='?')
    ap.add_argument('-o', '--out', required=True)
    ap.add_argument('--msg', default=DEFAULT_MSG)
    ap.add_argument('--common', action='store_true')
    ap.add_argument('--section', help='MSG section of the utility (default: :util name)')
    ap.add_argument('--subst', action='append', default=[])
    ap.add_argument('--extra', action='append', default=[], help='CLS:NUM=TEXT (db syntax) extra/override')
    ap.add_argument('--name', default='_msg_util_table')
    a = ap.parse_args()
    sections = load_msg(a.msg)
    out = ['/* generated by apps/mslib/tools/msgtab.py from %s - do not edit */' %
           (os.path.basename(a.skl) if a.skl else 'USA-MS.MSG EXTEND/PARSE'),
           '/* Message text: MS-DOS 4.0 USA-MS.MSG, (c) Microsoft Corp., MIT License */',
           '#include "mslib_msg.h"']
    if a.common:
        rows = [(1, n, render(t)) for n, t in sorted(sections['EXTEND'].items())]
        rows.append((1, 0xFFFF, b'Extended Error %1'))
        emit(rows, '_msg_extend_table', out)
        rows = [(2, n, render(t)) for n, t in sorted(sections['PARSE'].items())]
        rows.append((2, 0xFFFF, b'Parse Error %1'))
        emit(rows, '_msg_parse_table', out)
    else:
        util, res = parse_skl(a.skl, sections, a.section)
        rows = {}
        order = []
        for c, n, b in res:
            key = (cls_code(c), n)
            if key not in rows:
                order.append(key)
            rows[key] = b
        for e in a.extra:
            m = re.fullmatch(r'(\w+):(\d+)=(.*)', e, re.S)
            key = (cls_code(m.group(1)), int(m.group(2)))
            if key not in rows:
                order.append(key)
            rows[key] = render(m.group(3))
        for s in a.subst:
            old, new = s.split('=', 1)
            for k in rows:
                rows[k] = rows[k].replace(old.encode('latin-1'), new.encode('latin-1'))
        emit([(k[0], k[1], rows[k]) for k in order], a.name, out)
    open(a.out, 'w').write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
