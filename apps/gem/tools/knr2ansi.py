#!/usr/bin/env python3
"""knr2ansi.py - convert the K&R-style GEM sources to ANSI C definitions.

    knr2ansi.py [--proto OUT.h] [--int16] file.c ...

The GEM/3 sources (Caldera, GPL) are pre-ANSI C for 16-bit compilers:

	WORD
foo(a, p)
	WORD		a;
	BYTE		*p;
{

becomes

	WORD
foo(WORD a, BYTE *p)
{

and the prototypes of all non-static functions are written to --proto
(static ones are inserted into their file after the last #include).
--int16 first rewrites the plain C types that were 16 bits on the 8086
(int -> WORD, unsigned [int] -> UWORD), because WORD is 16 bits on ARM too.
Parameters without a declaration were implicitly int = WORD.  Functions
without a return type were implicitly int, i.e. WORD.
The conversion is a tool used once when importing; the output was then
edited by hand (see apps/gem/README.md).
"""
import re, sys

HDR = re.compile(r'^(?P<pre>[A-Za-z_][\w \t\*]*?[\s\*]|\*+\s*|)(?P<name>[A-Za-z_]\w*)\s*\((?P<params>[\w\s,]*)\)\s*(/\*.*\*/)?\s*$')
TYPELINE = re.compile(r'^\s*((static|MLOCAL|GLOBAL|EXTERN|REG|unsigned|struct|register)\s+)*[A-Za-z_]\w*(\s+[A-Za-z_]\w*)*\s*\**\s*$')
KEYWORDS = {'if', 'while', 'for', 'switch', 'return', 'sizeof', 'else', 'do', 'case'}


def strip_comments(s):
    return re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)


def int16(text):
    text = re.sub(r'\bunsigned\s+int\b', 'UWORD', text)
    text = re.sub(r'\bunsigned\s+long\b', 'ULONG', text)
    text = re.sub(r'\bunsigned\s+char\b', 'UBYTE', text)
    text = re.sub(r'\bunsigned\b(?!\s+(WORD|LONG|BYTE|short|char))', 'UWORD', text)
    text = re.sub(r'\bint\b', 'WORD', text)
    return text


def split_top(s, sep=','):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        if ch == sep and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return out


def declname(d):
    # identifier of a declarator like "*p", "a[]", "(*f)()", "**pp"
    m = re.search(r'\(\s*\*+\s*([A-Za-z_]\w*)\s*\)', d)
    if m:
        return m.group(1)
    ids = re.findall(r'[A-Za-z_]\w*', d)
    return ids[0] if ids else None


def convert(path, protos, do_int16):
    src = open(path, encoding='latin-1').read()
    if do_int16:
        src = int16(src)
    lines = src.split('\n')
    out = []
    statics = []
    depth = 0
    i = 0
    in_comment = False
    DEFS = {'MULTIAPP': 0, 'SINGLAPP': 1, 'MC68K': 0, 'I8086': 1, 'GEMDOS': 0, 'PCDOS': 1,
            'CPM': 0, '0': 0, '1': 1, 'ALCYON': 0, 'HILO': 0, 'DEBUG': 0, 'DESKTOP': 0}
    cond = []          # stack of (active, taken)
    def active():
        return all(c[0] for c in cond)
    while i < len(lines):
        line = lines[i]
        code = strip_comments(line)
        pm = re.match(r'^\s*#\s*(if|ifdef|ifndef|else|endif|elif)\b\s*(\w*)', line)
        if pm:
            d, arg = pm.group(1), pm.group(2)
            if d in ('if', 'ifdef', 'ifndef'):
                v = DEFS.get(arg, 1) if d == 'if' else 1
                if d == 'ifndef':
                    v = 1
                cond.append([bool(v), bool(v)])
            elif d == 'else' and cond:
                cond[-1][0] = not cond[-1][1]
            elif d == 'elif' and cond:
                cond[-1][0] = False
            elif d == 'endif' and cond:
                cond.pop()
            out.append(line)
            i += 1
            continue
        if not active():
            out.append(line)
            i += 1
            continue
        if depth == 0 and not in_comment:
            m = HDR.match(code.rstrip())
            if m and m.group('name') not in KEYWORDS and not code.rstrip().endswith(';'):
                # collect declarations up to '{'
                j = i + 1
                decls = []
                ok = False
                while j < len(lines):
                    t = strip_comments(lines[j]).strip()
                    if t == '':
                        j += 1
                        continue
                    if t.startswith('{'):
                        ok = True
                        break
                    if t.endswith(';') or t.endswith(','):
                        decls.append(t)
                        j += 1
                        continue
                    break
                if ok:
                    pre = m.group('pre').strip()
                    name = m.group('name')
                    params = [p.strip() for p in m.group('params').split(',') if p.strip()]
                    # return type from the previous line?
                    rtype = pre
                    k = len(out) - 1
                    while k >= 0 and out[k].strip() == '':
                        k -= 1
                    if k >= 0 and TYPELINE.match(strip_comments(out[k])) and not strip_comments(out[k]).strip().startswith('#'):
                        prev = strip_comments(out[k]).strip()
                        rtype = (prev + ' ' + pre).strip()
                        del out[k:]
                    is_static = bool(re.search(r'\b(static|MLOCAL)\b', rtype))
                    rbase = re.sub(r'\b(GLOBAL|REG)\b', '', rtype).strip()
                    if rbase in ('', '*') or re.fullmatch(r'(static|MLOCAL)\s*\**', rbase):
                        rbase = (rbase.replace('*', '') + ' WORD ' + ('*' * rbase.count('*'))).strip()
                    rbase = re.sub(r'\s+', ' ', rbase)
                    # parse declarations
                    pdecl = {}
                    for d in ' '.join(decls).split(';'):
                        d = d.strip()
                        if not d:
                            continue
                        # base type = qualifiers + one type name, then declarators
                        toks = re.split(r'(\s+|\*)', d)
                        pos = 0
                        base_parts = []
                        m2 = re.match(r'^\s*((?:(?:REG|register|const|static|MLOCAL)\s+)*)', d)
                        quals = m2.group(1)
                        rest = d[m2.end():]
                        m3 = re.match(r'^(struct\s+\w+|union\s+\w+|unsigned\s+(?:int|short|char|long)|unsigned|signed\s+\w+|[A-Za-z_]\w*)\b', rest)
                        if m3 and m3.group(1) not in params:
                            base = (quals + m3.group(1)).strip()
                            rest = rest[m3.end():]
                        else:
                            base = (quals + 'WORD').strip()
                        for dd in split_top(rest):
                            dd = dd.strip()
                            nm = declname(dd)
                            if nm:
                                pdecl[nm] = (base, dd)
                    plist = []
                    for p in params:
                        if p in pdecl:
                            b, dd = pdecl[p]
                            plist.append((b + ' ' + dd).strip())
                        else:
                            plist.append('WORD ' + p)
                    pstr = ', '.join(plist) if plist else 'VOID'
                    rshow = rbase
                    head = rshow + ('' if rshow.endswith('*') else '\n') + name + '(' + pstr + ')'
                    # keep GEM layout: return type on its own tab-indented line
                    if rshow.endswith('*'):
                        t = rshow.rstrip('*').strip()
                        head = '\t' + t + '\n' + ('*' * (len(rshow) - len(rshow.rstrip('*')))) + name + '(' + pstr + ')'
                    else:
                        head = '\t' + rshow + '\n' + name + '(' + pstr + ')'
                    out.append(head)
                    proto = re.sub(r'\b(REG|register)\s+', '', rbase + ' ' + name + '(' + pstr + ');')
                    proto = re.sub(r'\s+', ' ', proto)
                    if is_static:
                        statics.append(proto)
                    else:
                        protos.append(re.sub(r'^\s*', 'EXTERN ', proto))
                    # skip to '{'
                    i = j
                    continue
        # track braces / comments
        for ch in code:
            if ch == '{':
                depth += 1
            elif ch == '}':
                depth -= 1
        # comment state across lines
        k = 0
        while k < len(line):
            if not in_comment and line.startswith('/*', k):
                in_comment = True
                k += 2
            elif in_comment and line.startswith('*/', k):
                in_comment = False
                k += 2
            else:
                k += 1
        out.append(line)
        i += 1
    text = '\n'.join(out)
    if statics:
        # after the last #include
        incs = [m.end() for m in re.finditer(r'^#include.*$', text, flags=re.M)]
        pos = incs[-1] if incs else 0
        text = text[:pos] + '\n\n/* prototypes of static functions (knr2ansi.py) */\n' + '\n'.join(statics) + '\n' + text[pos:]
    open(path, 'w', encoding='latin-1').write(text)


def main():
    args = sys.argv[1:]
    proto = None
    do_int16 = False
    files = []
    while args:
        a = args.pop(0)
        if a == '--proto':
            proto = args.pop(0)
        elif a == '--int16':
            do_int16 = True
        else:
            files.append(a)
    protos = []
    for f in files:
        convert(f, protos, do_int16)
    if proto:
        with open(proto, 'w') as fp:
            fp.write('/* generated by tools/knr2ansi.py from the K&R definitions, then edited */\n')
            fp.write('\n'.join(protos) + '\n')


if __name__ == '__main__':
    main()
