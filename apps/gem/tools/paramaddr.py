#!/usr/bin/env python3
"""paramaddr.py - list places where the address of a scalar function
parameter is taken (8086 C code sometimes treated consecutive stack
parameters as an array; on ARM they are in registers)."""
import re, sys
for path in sys.argv[1:]:
    s = open(path, encoding='latin-1').read()
    for m in re.finditer(r'^\**([A-Za-z_]\w*)\(([^)]*)\)\s*\n\{', s, re.M):
        name, params = m.group(1), m.group(2)
        scal = []
        for p in params.split(','):
            p = p.strip()
            if not p or p == 'VOID' or '*' in p or '[' in p or '(' in p:
                continue
            scal.append(p.split()[-1])
        if not scal:
            continue
        # body: up to the matching closing brace at column 0
        start = m.end()
        end = s.find('\n}', start)
        body = s[start:end]
        for v in scal:
            for mm in re.finditer(r'&\s*' + re.escape(v) + r'\b', body):
                line = s[:start + mm.start()].count('\n') + 1
                print(f'{path}:{line}: {name}(): &{v}')
