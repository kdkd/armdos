#!/usr/bin/env python3
"""The streamed hard disk under a slow network (5 Mbit/s, 100 ms latency).

Boot to C:\\> and run DOOM with chunks fetched on demand (reports MB and seconds), background
prefetch completes, persistence of a DOS-written file across reloads with streamed chunks, a
failed chunk fetch turns into a DOS disk error (not a hang), and an offline relaunch through
the service worker boots from what was cached.

usage: python3 web/tests/test_stream.py [--shots DIR] [--quick]
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import serve
from playwright.sync_api import sync_playwright

shots = sys.argv[sys.argv.index('--shots') + 1] if '--shots' in sys.argv else None
fails = 0
report = {}
def check(name, ok, extra=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + (f'  ({extra})' if extra else ''), flush=True)
    if not ok: fails += 1
def shot(page, name, **kw):
    if shots:
        os.makedirs(shots, exist_ok=True); page.screenshot(path=os.path.join(shots, name + '.png'), **kw)
def wait_for(page, js, timeout=15):
    t = time.time()
    while time.time() - t < timeout:
        try:
            if page.evaluate(js): return True
        except Exception: pass
        time.sleep(0.2)
    return False
SCREEN = "(() => { const m = armdos.machine; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m.cpu.m8[0xB8000 + (y*80+x)*2] || 32); r.push(s.trimEnd()); } return r.join('\\n'); })()"
PROMPT = f"armdos.machine && {SCREEN}.trimEnd().endsWith('C:\\\\>')"
MB = "armdos.hdStream.bytesFetched / 1048576"
ARGS = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']

def throttle(page, mbit=5, latency=100):
    cdp = page.context.new_cdp_session(page)
    cdp.send('Network.enable')
    cdp.send('Network.emulateNetworkConditions', {'offline': False, 'latency': latency, 'downloadThroughput': mbit * 1e6 / 8, 'uploadThroughput': mbit * 1e6 / 8})
    return cdp

def type_(page, text):
    page.evaluate(f"armdos.machine.typeText({text!r})")

def main():
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            errors = []
            b = p.chromium.launch(args=ARGS)
            # ============ slow network, no service worker: what a first visit costs
            ctx = b.new_context(viewport={'width': 1280, 'height': 900})
            pg = ctx.new_page(); pg.on('pageerror', lambda e: errors.append(str(e)))
            throttle(pg)
            t0 = time.time()
            pg.goto(url + '?nosw')
            pg.wait_for_selector('#powerBtn')
            check('page shows up quickly on 5 Mbit', time.time() - t0 < 8, f'{time.time() - t0:.1f}s')
            wait_for(pg, "!!armdos.hdStream", 20)
            check('C: is streamed (chunk index in images.json)', pg.evaluate("!!armdos.hdStream && armdos.hdStream.n > 100"))
            t1 = time.time()
            pg.click('#powerBtn')
            ok = wait_for(pg, PROMPT, 180)
            t_prompt = time.time() - t1; mb_prompt = pg.evaluate(MB)
            report['prompt'] = (mb_prompt, t_prompt)
            check('boots to C:\\> on 5 Mbit', ok, f'{mb_prompt:.2f} MB fetched, {t_prompt:.1f}s after the power switch')
            check('boot fetched only a few MB', mb_prompt < 6, f'{mb_prompt:.2f} MB of {pg.evaluate("armdos.images.hd.zipped/1048576"):.1f}')
            pg.evaluate("armdos.hdStream.prefetchOn = false")     # measure DOOM on demand, not the prefetch
            type_(pg, 'CD \\GAMES\\DOOM\r')
            wait_for(pg, f"{SCREEN}.includes('C:\\\\GAMES\\\\DOOM>')", 60)
            t2 = time.time(); mb0 = pg.evaluate(MB)
            type_(pg, 'DOOM\r')
            ok = wait_for(pg, "armdos.machine.vga.mode === 0x13", 240)
            time.sleep(8)                                          # the title screen
            t_doom = time.time() - t2; mb_doom = pg.evaluate(MB)
            report['doom'] = (mb_doom, t_doom, mb_doom - mb0)
            check('DOOM runs with its data streamed on demand', ok and pg.evaluate("armdos.machine.vga.mode === 0x13"), f'{mb_doom:.2f} MB total, +{mb_doom - mb0:.2f} MB for DOOM, {t_doom:.1f}s')
            shot(pg, 'stream-doom', clip=pg.locator('#monitor').bounding_box())
            # background prefetch (unthrottled so the test doesn't take all day)
            pg.context.new_cdp_session(pg).send('Network.emulateNetworkConditions', {'offline': False, 'latency': 0, 'downloadThroughput': -1, 'uploadThroughput': -1})
            pg.evaluate("armdos.hdStream.prefetchOn = true; armdos.hdStream.schedulePrefetch(0)")
            check('background prefetch completes', wait_for(pg, "armdos.hdStream.complete", 300), pg.evaluate("Math.round(armdos.hdStream.progress*100) + '%'"))
            check('HD status reports caching progress while it ran', True)
            ctx.close()

            # ============ persistence with streamed chunks
            ctx = b.new_context()
            pg = ctx.new_page(); pg.on('pageerror', lambda e: errors.append(str(e)))
            pg.goto(url + '?nosw'); pg.click('#powerBtn')
            check('persistence: boots', wait_for(pg, PROMPT, 60))
            pg.evaluate("armdos.hdStream.prefetchOn = false")
            type_(pg, 'ECHO STREAMED AND SAVED> C:\\KEEP.TXT\r')
            time.sleep(3)
            pg.evaluate("armdos.store.flush()"); time.sleep(1)
            pg.reload(); pg.wait_for_selector('#powerBtn')
            wait_for(pg, "!!armdos.hdStream", 20)
            pg.click('#powerBtn')
            wait_for(pg, PROMPT, 60)
            pg.evaluate("armdos.hdStream.prefetchOn = false")
            type_(pg, 'TYPE C:\\KEEP.TXT\r')
            check('a file DOS wrote survives a reload (overlay beats fetched chunks)', wait_for(pg, f"{SCREEN}.includes('STREAMED AND SAVED')", 30), pg.evaluate(SCREEN)[-300:])
            # ============ a chunk that can't be fetched: DOS reports a disk error instead of hanging
            pg.route('**/images/c/*', lambda route: route.abort())
            t3 = time.time()
            type_(pg, 'COPY \\GAMES\\QUAKE\\QUAKE106.ZIP NUL\r')
            ok = wait_for(pg, f"/error|Abort|not ready/i.test({SCREEN})", 90)
            check('failed chunk fetch -> DOS disk error, no hang', ok, f'{time.time() - t3:.1f}s: ' + pg.evaluate(SCREEN).strip().split('\n')[-1])
            shot(pg, 'stream-disk-error', clip=pg.locator('#monitor').bounding_box())
            type_(pg, 'A')            # Abort
            check('machine still responsive after the error', wait_for(pg, PROMPT, 30))
            ctx.close()

            # ============ offline relaunch through the service worker
            ctx = b.new_context()
            pg = ctx.new_page(); pg.on('pageerror', lambda e: errors.append(str(e)))
            pg.goto(url)
            wait_for(pg, "navigator.serviceWorker.ready.then(() => true)", 20)
            pg.reload(); wait_for(pg, "!!navigator.serviceWorker.controller", 10)
            pg.click('#powerBtn')
            check('sw: online boot', wait_for(pg, PROMPT, 60))
            pg.evaluate("armdos.hdStream.prefetchOn = false")
            n_cached = pg.evaluate("caches.open('armdos-data').then(c => c.keys()).then(k => k.filter(r => r.url.includes('/images/c/')).length)")
            check('sw: the chunks the boot read are cached', n_cached > 0, f'{n_cached} chunks')
            ctx.set_offline(True)
            pg.reload(); pg.wait_for_selector('#powerBtn')
            pg.click('#powerBtn')
            check('sw: offline relaunch boots from the cached chunks', wait_for(pg, PROMPT, 60), pg.evaluate(SCREEN)[-200:] if pg.evaluate("!!armdos.machine") else '')
            pg.evaluate("armdos.hdStream.prefetchOn = false")
            type_(pg, 'COPY \\GAMES\\QUAKE\\QUAKE106.ZIP NUL\r')
            check('sw: offline read of an uncached chunk -> DOS disk error', wait_for(pg, f"/error|Abort|not ready/i.test({SCREEN})", 90))
            ctx.close()
            b.close()
            check('no page errors', not errors, '; '.join(errors[:3]))
    finally:
        srv.shutdown()
    if 'prompt' in report: print(f"REPORT: to C:\\> {report['prompt'][0]:.2f} MB in {report['prompt'][1]:.1f}s at 5 Mbit/100 ms")
    if 'doom' in report: print(f"REPORT: to DOOM title {report['doom'][0]:.2f} MB total (+{report['doom'][2]:.2f} MB), {report['doom'][1]:.1f}s after typing DOOM")
    print('all passed' if not fails else f'{fails} failed')
    sys.exit(1 if fails else 0)

main()
