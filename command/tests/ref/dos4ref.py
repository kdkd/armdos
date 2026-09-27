#!/usr/bin/env python3
"""
dos4ref.py - run the REAL MS-DOS 4.00 (PCjs disk set, booted in DOSBox-X from
the verified hard disk image in $ARMDOS_REFS/dos400-verify) and capture the
text screen, so COMMAND.COM's behaviour can be compared keystroke for
keystroke.  Reference tooling only: it needs dosbox-x, Xvfb, mtools and PIL,
and the MS-DOS 4.00 reference images under $ARMDOS_REFS (not distributed).

  dos4ref.py [--put local=DOSPATH ...] [--autoexec TEXT] [--config TEXT]
             [--keys FILE | --type "text"] [--secs N] [--get DOSPATH=local ...]
             [--shots] > screen.txt

Keys file lines: "<delay> <item> <item>..." where an item is "text" (typed),
Return, Escape, F1..F12, BackSpace, or a combination like Control_L+c.
"""
import argparse, atexit, ctypes, os, shutil, subprocess, sys, tempfile, time

REFS = os.environ.get('ARMDOS_REFS', '')
REF = os.path.join(REFS, 'dos400-verify')
BASE_HD = REF + '/tools/hd-after-tests.img'
BASE_HD0 = os.environ.get('DOS4REF_BASE', '')    # optional: a faster base image
FONT = REF + '/tools/font.png'
OFF = 63 * 512

def mtools(args, img, check=True):
    subprocess.run(args[:1] + ['-i', img + '@@%d' % OFF] + args[1:], check=check,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def decode(png):
    from PIL import Image
    X0, Y0 = 192, 201
    def cell(im, r, c):
        return [[im.getpixel((X0 + c * 9 + x, Y0 + r * 16 + y)) for x in range(9)] for y in range(16)]
    f = Image.open(FONT).convert('RGB')
    font = {}
    for i in range(256):
        px = cell(f, i // 80, i % 80)
        m = tuple(tuple(p != (0, 0, 0) for p in row[:8]) for row in px)
        font.setdefault(m, i)
    im = Image.open(png).convert('RGB')
    out = []
    for r in range(25):
        line = bytearray()
        for c in range(80):
            px = cell(im, r, c)
            cnt = {}
            for row in px:
                for p in row: cnt[p] = cnt.get(p, 0) + 1
            cols = sorted(cnt, key=lambda k: -cnt[k])
            bg = cols[0]; fg = cols[1] if len(cols) > 1 else None
            ch = 32
            if fg is not None:
                m = tuple(tuple(p == fg for p in row[:8]) for row in px)
                if m in font: ch = font[m]
                else:
                    m2 = tuple(tuple(p == bg for p in row[:8]) for row in px)
                    if m2 in font: ch = font[m2]
                    else:
                        best = min(font, key=lambda k: sum(a != b for ra, rb in zip(k, m) for a, b in zip(ra, rb)))
                        ch = font[best]
            line.append(ch)
        out.append(line)
    return out

LOW = ' ☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼'

def screen_text(lines):
    """25 lines, CP437 -> Unicode as emu/render.mjs does, trailing blanks trimmed"""
    out = []
    for l in lines:
        s = ''.join(LOW[c] if c < 32 else ('⌂' if c == 127 else bytes([c]).decode('cp437')) for c in l)
        out.append(s.rstrip())
    return '\n'.join(out) + '\n'

shots = []

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--put', action='append', default=[])
    ap.add_argument('--get', action='append', default=[])
    ap.add_argument('--mkdir', action='append', default=[])
    ap.add_argument('--script', help='lines "mkdir /DIR" or "put LOCAL /DOSPATH", done in order')
    ap.add_argument('--autoexec')
    ap.add_argument('--config')
    ap.add_argument('--keys')
    ap.add_argument('--type', action='append', default=[])
    ap.add_argument('--secs', type=float, default=12)
    ap.add_argument('--base', default=BASE_HD0 if BASE_HD0 and os.path.exists(BASE_HD0) else BASE_HD)
    ap.add_argument('--raw', action='store_true', help='print CP437 bytes')
    ap.add_argument('--shotdir', help='write SHOT screens here as NAME.txt')
    a = ap.parse_args()
    if not os.path.exists(a.base) or not os.path.exists(FONT):
        print('dos4ref: skipped - set ARMDOS_REFS to the MS-DOS 4.00 reference images', file=sys.stderr)
        sys.exit(0)
    work = tempfile.mkdtemp(prefix='dos4ref')
    atexit.register(shutil.rmtree, work, True)
    hd = os.path.join(work, 'hd.img')
    shutil.copy(a.base, hd)
    def put_text(name, text):
        p = os.path.join(work, 'put.tmp')
        with open(p, 'wb') as f: f.write(text.replace('\n', '\r\n').encode('cp437'))
        mtools(['mcopy', '-o', p, '::' + name], hd)
    if a.autoexec is not None: put_text('/AUTOEXEC.BAT', a.autoexec)
    if a.config is not None: put_text('/CONFIG.SYS', a.config)
    for d in a.mkdir:
        mtools(['mmd', '::' + d], hd, check=False)
    if a.script:
        for line in open(a.script):
            op, *args = line.split()
            if op == 'mkdir':
                mtools(['mmd', '::' + args[0]], hd, check=False)
            elif op == 'put':
                mtools(['mcopy', '-o', args[0], '::' + args[1]], hd)
    for spec in a.put:
        src, dst = spec.split('=', 1)
        mtools(['mcopy', '-o', src, '::' + dst], hd)
    conf = os.path.join(work, 't.conf')
    with open(conf, 'w') as f:
        f.write('[sdl]\noutput=surface\n[dosbox]\nmachine=svga_s3\nmemsize=4\nquit warning=false\n'
                '[cpu]\ncycles=max\n[autoexec]\n'
                'IMGMOUNT C %s -t hdd -size 512,63,16,65\nBOOT C:\n' % hd)
    fb = os.path.join(work, 'fb'); os.mkdir(fb)
    r, w = os.pipe()
    xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(w), '-screen', '0', '1024x768x24', '-fbdir', fb],
                            pass_fds=(w,), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.close(w)
    disp = ':' + os.read(r, 32).decode().strip()
    os.close(r)
    env = dict(os.environ, DISPLAY=disp)
    dbx = subprocess.Popen(['dosbox-x', '-conf', conf, '-nopromptfolder', '-fastlaunch'], env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=work)
    try:
        keys = []
        if a.keys:
            keys = [l.rstrip('\n') for l in open(a.keys)]
        for t in a.type:
            keys.append('2 "%s" Return' % t)
        start = time.time()
        if keys:
            typer(disp, keys, fb, work)
        while time.time() - start < a.secs:
            time.sleep(0.2)
        png = os.path.join(work, 's.png')
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', os.path.join(fb, 'Xvfb_screen0'), png], check=True)
        lines = decode(png)
    finally:
        dbx.kill(); xvfb.kill()
        dbx.wait(); xvfb.wait()
    for spec in a.get:
        src, dst = spec.split('=', 1)
        mtools(['mcopy', '-o', '::' + src, dst], hd)
    if a.shotdir:
        for name, png in shots:
            with open(os.path.join(a.shotdir, name + '.txt'), 'w') as f:
                f.write(screen_text(decode(png)))
    if a.raw:
        sys.stdout.buffer.write(b'\n'.join(bytes(l).rstrip() for l in lines) + b'\n')
    else:
        sys.stdout.write(screen_text(lines))
    shutil.rmtree(work, ignore_errors=True)

NAMES = {' ': 'space', ':': 'colon', '/': 'slash', '\\': 'backslash', '.': 'period', '*': 'asterisk',
         '%': 'percent', '>': 'greater', '<': 'less', '|': 'bar', '+': 'plus', '=': 'equal', '-': 'minus',
         '"': 'quotedbl', '$': 'dollar', '@': 'at', '?': 'question', ',': 'comma', ';': 'semicolon',
         '_': 'underscore', '(': 'parenleft', ')': 'parenright', '!': 'exclam', '#': 'numbersign',
         '&': 'ampersand', "'": 'apostrophe', '[': 'bracketleft', ']': 'bracketright', '^': 'asciicircum',
         '~': 'asciitilde', '{': 'braceleft', '}': 'braceright'}

def snap(fb, work, name):
    png = os.path.join(work, 'shot-%s.png' % name)
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', os.path.join(fb, 'Xvfb_screen0'), png], check=True)
    shots.append((name, png))

def typer(disp, lines, fb, work):
    x = ctypes.cdll.LoadLibrary('libX11.so.6'); t = ctypes.cdll.LoadLibrary('libXtst.so.6')
    x.XOpenDisplay.restype = ctypes.c_void_p
    x.XStringToKeysym.restype = ctypes.c_ulong
    x.XKeycodeToKeysym.restype = ctypes.c_ulong
    d = None
    for i in range(50):
        d = x.XOpenDisplay(disp.encode())
        if d: break
        time.sleep(0.2)
    if not d:
        raise SystemExit('cannot open display ' + disp)
    d = ctypes.c_void_p(d)
    for fn in (x.XFlush, x.XKeysymToKeycode, x.XKeycodeToKeysym):
        fn.argtypes = [ctypes.c_void_p] + ([ctypes.c_ulong] if fn is x.XKeysymToKeycode else
                                           [ctypes.c_uint, ctypes.c_int] if fn is x.XKeycodeToKeysym else [])
    t.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    t.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
    t.XTestFakeMotionEvent(d, 0, 1000, 760, 0); x.XFlush(d)
    def kc_of(name):
        ks = x.XStringToKeysym(name.encode()); kc = x.XKeysymToKeycode(d, ks)
        shift = x.XKeycodeToKeysym(d, kc, 0) != ks
        return kc, shift
    SH = x.XKeysymToKeycode(d, x.XStringToKeysym(b'Shift_L'))
    def ev(kc, down): t.XTestFakeKeyEvent(d, kc, down, 0); x.XFlush(d)
    def press(spec):
        parts = spec.split('+') if len(spec) > 1 else [spec]
        kcs = []
        for p in parts:
            kc, sh = kc_of(p)
            if sh and len(parts) == 1: kcs.append(SH)
            kcs.append(kc)
        for k in kcs: ev(k, 1); time.sleep(0.04)
        for k in reversed(kcs): ev(k, 0); time.sleep(0.04)
    # shifted characters as Shift + the US key (some keysyms, like "less" or
    # "parenleft", land on other keys in DOSBox-X's keyboard mapping)
    SHIFTED = {'!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '^': '6', '&': '7', '*': '8',
               '(': '9', ')': '0', '_': 'minus', '+': 'equal', '{': 'bracketleft',
               '}': 'bracketright', '|': 'backslash', ':': 'semicolon', '"': 'apostrophe',
               '<': 'comma', '>': 'period', '?': 'slash', '~': 'grave'}
    def typech(ch):
        if ch in SHIFTED:
            press('Shift_L+' + SHIFTED[ch]); time.sleep(0.04); return
        name = NAMES.get(ch, ch)
        if ch.isalpha() and ch.isupper():
            kc, _ = kc_of(ch.lower()); ev(SH, 1); ev(kc, 1); time.sleep(0.03); ev(kc, 0); ev(SH, 0)
        else: press(name)
        time.sleep(0.04)
    for line in lines:
        if not line.strip() or line.startswith('#'): continue
        delay, rest = line.split(None, 1)
        time.sleep(float(delay))
        rest = rest.strip()
        while rest:
            if rest.startswith('SHOT '):
                snap(fb, work, rest[5:].strip())
                break
            if rest.startswith('"'):
                e = rest.index('"', 1)
                for ch in rest[1:e]: typech(ch)
                rest = rest[e + 1:].strip()
            else:
                item, _, rest = rest.partition(' ')
                press(item); time.sleep(0.1)
                rest = rest.strip()

if __name__ == '__main__':
    main()
