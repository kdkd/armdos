#!/usr/bin/env python3
"""Host interop test for apps/term/lib/zmodem.c.

Connects two programs (stdin/stdout each) through a relay that can corrupt or
drop bytes, and checks the received files. Pairs: our sender -> lrzsz rz,
lrzsz sz -> our receiver, ours -> ours; with and without line noise; crash
recovery (a partial file is continued).
usage: run.py HOSTBIN LSZ LRZ WORKDIR"""
import os, sys, subprocess, threading, random, hashlib, shutil, time

host, lsz, lrz, work = [os.path.abspath(p) for p in sys.argv[1:5]]
fails = 0

def relay(src, dst, noise, rnd, stats):
    while True:
        try:
            b = os.read(src, 4096)
        except OSError:
            b = b''
        if not b:
            try: os.close(dst)
            except OSError: pass
            return
        if noise:
            ba = bytearray()
            for x in b:
                r = rnd.random()
                if r < noise: stats['hits'] += 1; x ^= 1 << rnd.randrange(8)
                elif r < noise * 1.5: stats['hits'] += 1; continue      # dropped byte
                ba.append(x)
            b = bytes(ba)
        try:
            os.write(dst, b)
        except OSError:
            return

# (the receiver runs in its directory and is given relative paths: the ZMODEM code keeps
#  paths in 128-byte buffers, as DOS paths are short, and a deep checkout would not fit)
def run_pair(name, send_cmd, recv_cmd, recv_cwd, noise=0.0, seed=1, timeout=120):
    a_in_r, a_in_w = os.pipe(); a_out_r, a_out_w = os.pipe()
    b_in_r, b_in_w = os.pipe(); b_out_r, b_out_w = os.pipe()
    err = open(os.path.join(work, name + '.log'), 'w')
    pa = subprocess.Popen(send_cmd, stdin=a_in_r, stdout=a_out_w, stderr=err, cwd=work)
    pb = subprocess.Popen(recv_cmd, stdin=b_in_r, stdout=b_out_w, stderr=err, cwd=recv_cwd)
    for fd in (a_in_r, a_out_w, b_in_r, b_out_w): os.close(fd)
    rnd = random.Random(seed); stats = {'hits': 0}
    t1 = threading.Thread(target=relay, args=(a_out_r, b_in_w, noise, rnd, stats), daemon=True)
    t2 = threading.Thread(target=relay, args=(b_out_r, a_in_w, noise, rnd, stats), daemon=True)
    t1.start(); t2.start()
    t0 = time.time()
    try:
        ra = pa.wait(timeout); rb = pb.wait(timeout)
    except subprocess.TimeoutExpired:
        pa.kill(); pb.kill(); ra = rb = 'timeout'
    return ra, rb, stats['hits'], time.time() - t0

def md5(p): return hashlib.md5(open(p, 'rb').read()).hexdigest()

def check(ok, what):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok: fails += 1

os.makedirs(work, exist_ok=True)
src = os.path.join(work, 'src'); os.makedirs(src, exist_ok=True)
rnd = random.Random(42)
files = {'small.txt': b'Hello from the ARM Pit!\r\n' * 3,
         'binary.bin': bytes(rnd.randrange(256) for _ in range(70000)),
         'zdle.bin': bytes([0x18, 0x10, 0x11, 0x13, 0x90, 0x91, 0x93, 0x40, 0x0d, 0xc0, 0x8d, 0x7f, 0xff, 0x2a] * 700),
         'empty.dat': b''}
for n, d in files.items():
    open(os.path.join(src, n), 'wb').write(d)
names = list(files)
srcpaths = [os.path.join(src, n) for n in names]

def fresh(d):
    shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    return d

def verify(d, upper, label):
    for n in names:
        p = os.path.join(d, n.upper() if upper else n)
        check(os.path.exists(p) and md5(p) == md5(os.path.join(src, n)), f'{label}: {n} intact')

for noise in (0.0, 0.0002):
    tag = 'noisy' if noise else 'clean'
    d = fresh(os.path.join(work, 'r1'))
    ra, rb, hits, dt = run_pair(f'ours-to-lrz-{tag}', [host, 'send'] + srcpaths, [lrz, '-b', '-y', '-q'], d, noise, 7)
    check(ra == 0 and rb == 0, f'ours -> lrzsz rz ({tag}, {hits} bytes damaged, {dt:.1f}s): exit {ra}/{rb}')
    verify(d, False, f'ours -> rz {tag}')

    d = fresh(os.path.join(work, 'r2'))
    ra, rb, hits, dt = run_pair(f'lsz-to-ours-{tag}', [lsz, '-b', '-q'] + srcpaths, [host, 'recv', '.'], d, noise, 9)
    check(ra == 0 and rb == 0, f'lrzsz sz -> ours ({tag}, {hits} bytes damaged, {dt:.1f}s): exit {ra}/{rb}')
    verify(d, True, f'sz -> ours {tag}')

    d = fresh(os.path.join(work, 'r3'))
    ra, rb, hits, dt = run_pair(f'ours-to-ours-{tag}', [host, 'send'] + srcpaths, [host, 'recv', '.'], d, noise, 11)
    check(ra == 0 and rb == 0, f'ours -> ours ({tag}, {hits} bytes damaged, {dt:.1f}s): exit {ra}/{rb}')
    verify(d, True, f'ours -> ours {tag}')

# CRC-16 data subpackets (our receiver without CANFC32); lrzsz sz with 8K subpackets
d = fresh(os.path.join(work, 'r4'))
# (lrzsz 0.12.20's sz crashes on an empty file in CRC-16 mode: leave that one out)
ra, rb, hits, dt = run_pair('lsz16-to-ours', [lsz, '-b', '-q'] + srcpaths[:3], ['env', 'ZNO32=1', host, 'recv', '.'], d)
check(ra == 0 and rb == 0, f'lrzsz sz -> ours offering CRC-16 only ({hits} bytes damaged): exit {ra}/{rb}')
for n in names[:3]:
    check(md5(os.path.join(d, n.upper())) == md5(os.path.join(src, n)), f'sz CRC16 -> ours: {n} intact')
d = fresh(os.path.join(work, 'r5'))
ra, rb, hits, dt = run_pair('lsz8k-to-ours', [lsz, '-b', '-q', '-8'] + srcpaths, [host, 'recv', '.'], d)
check(ra == 0 and rb == 0, f'lrzsz sz -8 (8K subpackets) -> ours: exit {ra}/{rb}')
verify(d, True, 'sz 8K -> ours')

# crash recovery: receiver already holds the first 30000 bytes
d = fresh(os.path.join(work, 'r6'))
open(os.path.join(d, 'BINARY.BIN'), 'wb').write(files['binary.bin'][:30000])
ra, rb, hits, dt = run_pair('recover-ours', [host, 'send', os.path.join(src, 'binary.bin')], [host, 'recv', '.'], d)
log = open(os.path.join(work, 'recover-ours.log')).read()
check(ra == 0 and rb == 0 and md5(os.path.join(d, 'BINARY.BIN')) == md5(os.path.join(src, 'binary.bin')), 'crash recovery ours -> ours: file completed')
check('Resuming at 30000' in log, 'crash recovery: resumed at 30000 (not from 0)')
d = fresh(os.path.join(work, 'r7'))
open(os.path.join(d, 'binary.bin'), 'wb').write(files['binary.bin'][:30000])
env = dict(os.environ, ZRECOVER='1')
ra, rb, hits, dt = run_pair('recover-lrz', [host, 'send', os.path.join(src, 'binary.bin')], [lrz, '-b', '-q'], d)
# (lrz honours ZCRECOV from the sender only; plain run overwrites) - use env via wrapper
d = fresh(os.path.join(work, 'r7'))
open(os.path.join(d, 'binary.bin'), 'wb').write(files['binary.bin'][:30000])
a_env = ['env', 'ZRECOVER=1', host, 'send', os.path.join(src, 'binary.bin')]
ra, rb, hits, dt = run_pair('recover-lrz', a_env, [lrz, '-b', '-q'], d)
log = open(os.path.join(work, 'recover-lrz.log')).read()
check(ra == 0 and rb == 0 and md5(os.path.join(d, 'binary.bin')) == md5(os.path.join(src, 'binary.bin')), 'ZCRECOV ours -> lrzsz rz: file completed')
check('Resuming at 29696' in log, 'ZCRECOV ours -> lrzsz rz: resumed (lrzsz rounds to 1K: 29696)')
d = fresh(os.path.join(work, 'r8'))
open(os.path.join(d, 'BINARY.BIN'), 'wb').write(files['binary.bin'][:30000])
ra, rb, hits, dt = run_pair('recover-lsz', [lsz, '-b', '-q', '-r', os.path.join(src, 'binary.bin')], [host, 'recv', '.'], d)
check(ra == 0 and rb == 0 and md5(os.path.join(d, 'BINARY.BIN')) == md5(os.path.join(src, 'binary.bin')), 'lrzsz sz -r -> ours: resumed file completed')

# ---- XMODEM / YMODEM against lrzsz's sx/rx/sb/rb (lsz/lrz called by those names)
bindir = os.path.join(work, 'bin'); os.makedirs(bindir, exist_ok=True)
for nm, tgt in (('lsx', lsz), ('lsb', lsz), ('lrx', lrz), ('lrb', lrz)):
    p = os.path.join(bindir, nm)
    if os.path.lexists(p): os.remove(p)
    os.symlink(tgt, p)
B = lambda n: os.path.join(bindir, n)
bin_ = os.path.join(src, 'binary.bin')
for noise in (0.0, 0.0001):
    tag = 'noisy' if noise else 'clean'
    d = fresh(os.path.join(work, 'x1'))
    ra, rb, hits, dt = run_pair(f'xsend-{tag}', ['env', 'X1K=1', host, 'xsend', bin_], [B('lrx'), '-c', '-q', 'out.bin'], d, noise, 3)
    got = open(os.path.join(d, 'out.bin'), 'rb').read() if os.path.exists(os.path.join(d, 'out.bin')) else b''
    check(ra == 0 and rb == 0 and got.rstrip(b'\x1a') == files['binary.bin'].rstrip(b'\x1a'), f'XMODEM-1K ours -> lrzsz rx ({tag}, {hits} hits)')
    d = fresh(os.path.join(work, 'x2'))
    ra, rb, hits, dt = run_pair(f'xrecv-{tag}', [B('lsx'), '-q', bin_], [host, 'xrecv', 'OUT.BIN'], d, noise, 4)
    got = open(os.path.join(d, 'OUT.BIN'), 'rb').read() if os.path.exists(os.path.join(d, 'OUT.BIN')) else b''
    check(ra == 0 and rb == 0 and got.rstrip(b'\x1a') == files['binary.bin'].rstrip(b'\x1a'), f'XMODEM lrzsz sx -> ours ({tag}, {hits} hits)')
    d = fresh(os.path.join(work, 'y1'))
    ra, rb, hits, dt = run_pair(f'ysend-{tag}', [host, 'ysend'] + srcpaths[:3], [B('lrb'), '-q'], d, noise, 5)
    check(ra == 0 and rb == 0, f'YMODEM ours -> lrzsz rb ({tag}): exit {ra}/{rb}')
    for n in names[:3]:
        p = os.path.join(d, n)
        check(os.path.exists(p) and md5(p) == md5(os.path.join(src, n)), f'YMODEM ours -> rb {tag}: {n} intact')
    d = fresh(os.path.join(work, 'y2'))
    ra, rb, hits, dt = run_pair(f'yrecv-{tag}', [B('lsb'), '-q'] + srcpaths[:3], [host, 'yrecv', '.'], d, noise, 6)
    check(ra == 0 and rb == 0, f'YMODEM lrzsz sb -> ours ({tag}): exit {ra}/{rb}')
    for n in names[:3]:
        p = os.path.join(d, n.upper())
        check(os.path.exists(p) and md5(p) == md5(os.path.join(src, n)), f'YMODEM sb -> ours {tag}: {n} intact')
d = fresh(os.path.join(work, 'y3'))
ra, rb, hits, dt = run_pair('ymodem-g', ['env', 'YG=1', host, 'ysend'] + srcpaths[:3], ['env', 'YG=1', host, 'yrecv', '.'], d)
check(ra == 0 and rb == 0 and all(md5(os.path.join(d, n.upper())) == md5(os.path.join(src, n)) for n in names[:3]), 'YMODEM-G ours -> ours')

print(f'{fails} failure(s)')
sys.exit(1 if fails else 0)
