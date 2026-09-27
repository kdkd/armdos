#!/usr/bin/env python3
"""dropdecl.py - remove old-style function declarations (EXTERN WORD foo();)
from the GEM sources after knr2ansi.py gave them prototypes.

    dropdecl.py file.c|file.h ...

A declaration statement at file or block scope whose declarators are
functions with empty parameter lists, e.g.

	EXTERN WORD	strlen();
	EXTERN VOID	ABLINE(),HABLINE(),CLEARMEM();
	WORD		sh_find();

is deleted (declarators that are not functions are kept).  Function
pointers such as  WORD (*fcode)();  are left alone.  The prototypes of
everything live in the component's *proto.h.
"""
import re, sys

TYPEWORDS = r'(?:EXTERN|extern|GLOBAL|static|MLOCAL|REG|register|unsigned|signed|const|struct\s+\w+|[A-Z_][A-Z_0-9]*|int|char|long|short|void|double|float)'
DECL = re.compile(r'^(?P<ind>\s*)(?P<type>(?:' + TYPEWORDS + r'\s+)+)(?P<rest>[^;{}()=]*\(\s*\)[^;{}=]*);(?P<tail>\s*(/\*.*\*/)?\s*)$')


def split_top(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    out.append(cur)
    return out


def process(path):
    lines = open(path, encoding='latin-1').read().split('\n')
    out = []
    removed = 0
    for ln in lines:
        m = DECL.match(ln)
        if m and 'typedef' not in ln:
            decls = split_top(m.group('rest'))
            keep = []
            for d in decls:
                ds = d.strip()
                if re.fullmatch(r'\**\s*[A-Za-z_]\w*\s*\(\s*\)', ds):
                    removed += 1
                    continue
                keep.append(d)
            if len(keep) != len(decls):
                if keep and any(k.strip() for k in keep):
                    ln = m.group('ind') + m.group('type') + ','.join(keep).strip() + ';' + m.group('tail')
                else:
                    ln = None
        if ln is not None:
            out.append(ln)
    open(path, 'w', encoding='latin-1').write('\n'.join(out))
    return removed


if __name__ == '__main__':
    tot = 0
    for f in sys.argv[1:]:
        tot += process(f)
    print('removed', tot, 'old-style declarations')
