#!/usr/bin/env python3
"""uniqscreens.py SHOTSDIR [-o OUT] - the distinct screens of a
dos400run.sh KEYS session (shots/sN.txt, decoded by the verification kit's
decode.py), in order, consecutive duplicates dropped.  Each screen: a line
"=== sN" then its 25 text rows (trailing blanks removed) and the attribute
runs."""
import sys, os, re, argparse
ap = argparse.ArgumentParser()
ap.add_argument('dir')
ap.add_argument('-o', '--out')
a = ap.parse_args()
files = sorted((f for f in os.listdir(a.dir) if re.fullmatch(r's\d+\.txt', f)), key=lambda f: int(f[1:-4]))
out, prev = [], None
for f in files:
    L = open(os.path.join(a.dir, f), encoding='utf-8').read().split('\n')
    rows = [l[4:-1].rstrip() for l in L if re.match(r'^\d\d \|', l)]
    attrs = [l for l in L if re.match(r'^\d\d: ', l)]
    if len(rows) != 25:
        continue
    key = '\n'.join(rows)
    if key == prev:
        continue
    prev = key
    out.append('=== ' + f[:-4])
    out += rows
    out += attrs
text = '\n'.join(out) + '\n'
if a.out:
    open(a.out, 'w', encoding='utf-8').write(text)
else:
    sys.stdout.write(text)
