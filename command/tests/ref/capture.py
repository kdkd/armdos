#!/usr/bin/env python3
"""
capture.py - run command/tests/cases.txt on the REAL MS-DOS 4.00 (dos4ref.py)
and write the screens as command/tests/expected/NAME.txt, with the ARM-DOS
branding substituted.  The disk is the reference hard disk plus the same
test tree (command/tests/disk) and the x86 twins of the test programs.

  capture.py [--only NAME,...] [--out DIR]

Review every new expected file before committing it.
"""
import argparse, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HERE)

def parse_cases(text):
    cases, cur = [], None
    for raw in text.splitlines():
        if raw.startswith('=== '):
            parts = raw[4:].split()
            opts = {}
            for x in parts[1:]:
                k, _, v = x.partition('=')
                opts[k] = v or True
            cur = {'name': parts[0], 'opts': opts, 'lines': []}
            cases.append(cur)
        elif cur is not None and raw and not raw.startswith('#'):
            cur['lines'].append(raw)
    return cases

KEYS = {'ENTER': 'Return', 'ESC': 'Escape', 'BS': 'BackSpace', 'TAB': 'Tab', 'INS': 'Insert',
        'DEL': 'Delete', 'LEFT': 'Left', 'RIGHT': 'Right', 'UP': 'Up', 'DOWN': 'Down',
        'HOME': 'Home', 'END': 'End', 'CTRL+C': 'Control_L+c', 'CTRL+Z': 'Control_L+z',
        'CTRL+BREAK': 'Control_L+Pause'}

def key_lines(line, delay):
    """one case line -> typer lines"""
    out, items = [], []
    cr = True
    if line.endswith('{NOCR}'):
        line, cr = line[:-6], False
    for tok in re.split(r'(\{[^}]+\})', line):
        if not tok:
            continue
        if tok.startswith('{') and tok.endswith('}'):
            k = tok[1:-1]
            if k.startswith('WAIT:'):
                out.append('%s %s' % (delay, ' '.join(items) or '-'))
                items, delay = [], '%.2f' % (int(k[5:]) / 1000)
                continue
            if re.fullmatch(r'F\d+', k):
                items.append(k)
            else:
                items.append(KEYS[k])
        else:
            items.append('"%s"' % tok)
    if cr:
        items.append('Return')
    out.append('%s %s' % (delay, ' '.join(items) or '-'))
    return out

BRAND = [
    ('Microsoft(R) MS-DOS(R) Version 4.00', 'ARM-DOS(R) Version 4.00'),
    ('(C)Copyright Microsoft Corp 1981-1988', '(C)Copyright Europa Micro Systems 1988'),
    ('MS-DOS Version 4.00', 'ARM-DOS Version 4.00'),
]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only')
    ap.add_argument('--out', default=os.path.join(TESTS, 'expected'))
    a = ap.parse_args()
    cases = parse_cases(open(os.path.join(TESTS, 'cases.txt')).read())
    # cases that need machine actions exist only on the ARM-DOS side
    cases = [c for c in cases if 'armonly' not in c['opts']]
    if a.only:
        want = a.only.split(',')
        cases = [c for c in cases if c['name'] in want]
    keys = ['9 -']
    secs = 10
    for c in cases:
        keys += key_lines('cls', '1')
        for l in c['lines']:
            keys += key_lines(l, '0.8')
        w = float(c['opts'].get('refwait', 2))
        keys.append('%s SHOT %s' % (w, c['name']))
        secs += 3 + 1.5 * len(c['lines']) + w + sum(len(l) for l in c['lines']) * 0.1
    work = tempfile.mkdtemp(prefix='cap')
    kf = os.path.join(work, 'keys.txt')
    open(kf, 'w').write('\n'.join(keys) + '\n')
    # the test tree, in the directory order of the ARM-DOS test disk
    # (build/cmdtest/hd.img, made by "make command-test"), so DIR listings match
    root = os.path.dirname(os.path.dirname(TESTS))
    ls = subprocess.run(['node', os.path.join(root, 'disk/mkimage.mjs'), 'ls',
                         os.path.join(root, 'build/cmdtest/hd.img'), '-R'],
                        check=True, capture_output=True, text=True).stdout
    script, cur = [], None
    progs = {'RET.COM', 'ARGS.COM', 'UPCASE.COM', 'TRASH.COM', 'WAITKEY.COM'}
    for line in ls.splitlines():
        m = re.match(r' Directory of (\\.*)$', line)
        if m:
            cur = m.group(1).replace('\\', '/').rstrip('/')
            continue
        m = re.match(r'(\S+) +(\S*) +(<DIR>|\d+) ', line)
        if cur is None or not m or m.group(1) in ('.', '..'):
            continue
        name = m.group(1) + ('.' + m.group(2) if m.group(2) and m.group(3) != '<DIR>' else '')
        if m.group(3) == '<DIR>' and m.group(2):
            continue
        dos = cur + '/' + name
        if not dos.upper().startswith('/T'):
            if dos.upper() in ('/AUTOEXEC.BAT', '/CONFIG.SYS'):
                script.append('put %s %s' % (os.path.join(TESTS, 'disk', name), dos))
            continue
        if m.group(3) == '<DIR>':
            script.append('mkdir ' + dos)
        elif cur.upper() == '/T' and name in progs:
            script.append('put %s %s' % (os.path.join(HERE, 'x86', name), dos))
        else:
            script.append('put %s %s' % (os.path.join(TESTS, 'disk', dos.lstrip('/')), dos))
    sf = os.path.join(work, 'script.txt')
    open(sf, 'w').write('\n'.join(script) + '\n')
    shots = os.path.join(work, 'shots')
    os.mkdir(shots)
    cmd = [sys.executable, os.path.join(HERE, 'dos4ref.py'), '--keys', kf, '--secs', str(secs),
           '--shotdir', shots, '--script', sf]
    try:
        for attempt in range(3):
            if subprocess.run(cmd, stdout=subprocess.DEVNULL).returncode == 0:
                break
            print('dos4ref failed, retrying', file=sys.stderr)
            shutil.rmtree(shots, ignore_errors=True)
            os.mkdir(shots)
        collect(cases, shots, a.out)
    finally:
        shutil.rmtree(work, ignore_errors=True)

def collect(cases, shots, outdir):
    for c in cases:
        p = os.path.join(shots, c['name'] + '.txt')
        if not os.path.exists(p):
            print('no screen for', c['name'])
            continue
        s = open(p).read()
        for x, y in BRAND:
            s = s.replace(x, y)
        lines = [l.rstrip() for l in s.split('\n')]
        while lines and not lines[-1]:
            lines.pop()
        # the text cursor (an underline) is decoded as '_' after the last output
        if lines and lines[-1].endswith('_'):
            lines[-1] = lines[-1][:-1].rstrip()
        while lines and not lines[-1]:
            lines.pop()
        open(os.path.join(outdir, c['name'] + '.txt'), 'w').write('\n'.join(lines) + '\n')
        print('captured', c['name'])

if __name__ == '__main__':
    main()
