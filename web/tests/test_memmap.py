#!/usr/bin/env python3
"""Playwright check of the inspector's MEMORY MAP (web/js/memmap-panel.js, emu/memmap.mjs)
while DOOM runs its timedemo: the A0000 framebuffer lights up with writes, DOOM's block
with execution, the hover readout names the region, clicking moves the zoom window,
and the counters are detached (cpu.act null) when the inspector is closed. Measures
the page's host MIPS and the panel's own frame time with the map open and closed.

Stages its own site (build/memmap-web/site) around the DOOM timedemo image from
apps/doom/tests/run.mjs (build/doom-test/mhz33/timedemo.img or build/doom-test/timedemo/..),
so build/site is left alone. Screenshots go to build/memmap-web/*.png.

usage: python3 web/tests/test_memmap.py
"""
import os, sys, time, shutil, subprocess, glob
sys.path.insert(0, os.path.dirname(__file__))
import serve
from playwright.sync_api import sync_playwright

ROOT = serve.ROOT
OUT = os.path.join(ROOT, 'build', 'memmap-web')
fails = 0
def check(name, ok, extra=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + (f'  ({extra})' if extra and not ok else ''))
    if not ok: fails += 1

def wait_for(page, js, timeout=15):
    t = time.time()
    while time.time() - t < timeout:
        if page.evaluate(js): return True
        time.sleep(0.1)
    return False

# sum of a heat channel (0 exec, 1 read, 2 write) over 256-byte cells [a, b)
HEAT = "((c, a, b) => { const h = armdos.memmap.heat[c]; let s = 0; for (let i = a; i < b; i++) s += h[i]; return s; })"

def stage():
    imgs = [p for p in glob.glob(os.path.join(ROOT, 'build', 'doom-test', '*', 'timedemo.img'))]
    if not imgs:
        subprocess.run(['node', os.path.join(ROOT, 'apps/doom/tests/run.mjs'), 'timedemo'], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        imgs = glob.glob(os.path.join(ROOT, 'build', 'doom-test', '*', 'timedemo.img'))
    os.makedirs(OUT, exist_ok=True)
    shutil.copy(os.path.join(ROOT, 'build', 'rom.bin'), OUT)
    shutil.copy(imgs[0], os.path.join(OUT, 'hd.img'))
    subprocess.run(['node', 'web/tools/build-site.mjs', '--out', 'build/memmap-web/site'], cwd=ROOT, check=True,
                   env={**os.environ, 'BUILD': 'build/memmap-web'}, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return os.path.join(OUT, 'site')

def host_mips(page, secs):
    """mean of the driver's host speed (instructions per busy host ms) over secs"""
    v = []
    t = time.time()
    while time.time() - t < secs:
        time.sleep(0.25)
        v.append(page.evaluate('armdos.stats.mips'))
    v = [x for x in v if x > 0]
    return sum(v) / max(1, len(v))

def main():
    serve.SITE = stage()
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(args=['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--autoplay-policy=no-user-gesture-required'])
            page = b.new_page(viewport={'width': 1440, 'height': 1300})
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.goto(url)
            page.wait_for_selector('#powerBtn')
            if page.evaluate("document.getElementById('inspector').classList.contains('closed')"): page.click('#inspTab')
            check('MEMORY MAP section in the inspector, closed by default', page.evaluate("document.getElementById('memmap').hidden === true"))
            page.click('#mmToggle')
            check('opens', page.evaluate("document.getElementById('memmap').hidden === false"))
            page.click('#powerBtn')
            check('DOOM starts (mode 13h)', wait_for(page, 'armdos.machine && armdos.machine.vga.mode === 0x13', 90))
            check('the counters are attached', wait_for(page, 'armdos.machine.cpu.act !== null', 5))
            time.sleep(4)
            fb_w = page.evaluate(f'{HEAT}(2, 0xA00, 0xAFA)')
            conv_w = page.evaluate(f'{HEAT}(2, 0, 0xA00)')
            check('A0000 framebuffer: writes', fb_w > 0, fb_w)
            fb_cells = page.evaluate("(() => { const h = armdos.memmap.hp; let n = 0; for (let i = 0xA00; i < 0xAFA; i++) if (h[i] > 0.1) n++; return n; })()")
            check('...over the whole 64000-byte frame (written lines: all 250 cells)', fb_cells >= 249, fb_cells)
            doom = page.evaluate("armdos.memmap.regions.find((g) => g.name === 'DOOM')")
            check('the MCB chain names DOOM', doom is not None)
            if doom:
                # (right after the mode set DOOM is still loading the level: DOS and the BIOS disk code run)
                share = f"({HEAT}(0, {doom['start'] >> 8}, {doom['end'] >> 8}) / Math.max(1, {HEAT}(0, 0, 0x11000)))"
                check('most execution is in DOOM', wait_for(page, f'{share} > 0.8', 30), page.evaluate(share))
            check('reads too (WAD data, tables, the stack)', page.evaluate(f'{HEAT}(1, 0, 0x10000)') > 0)
            top = page.evaluate("document.getElementById('mmTop').textContent")
            check('busiest regions: DOOM first', top.split('second', 1)[-1].strip().startswith('DOOM'), top)
            state = page.evaluate("document.getElementById('mmState').textContent")
            check('state line says it samples', 'SAMPLING' in state, state)
            # hover the A0000 row of the zoom (conventional-memory window): row 40 = A0000
            page.query_selector('#mmZoom').scroll_into_view_if_needed()
            time.sleep(0.2)
            box = page.query_selector('#mmZoom').bounding_box()
            sc = box['width'] / 404
            page.mouse.move(box['x'] + (10 * 5 + 2) * sc, box['y'] + (40 * 5 + 2) * sc)
            time.sleep(0.3)
            ro = page.evaluate("document.getElementById('mmHover').textContent")
            check('hover: address + region', '0A0A00' in ro and 'VGA' in ro, repr(ro))
            page.screenshot(path=os.path.join(OUT, 'page.png'))
            page.query_selector('#memmap').screenshot(path=os.path.join(OUT, 'memmap-doom.png'))
            # click the 16 MB map's row 4 (1 MB): the zoom follows
            page.query_selector('#mmAll').scroll_into_view_if_needed()
            time.sleep(0.2)
            ab = page.query_selector('#mmAll').bounding_box()
            s1 = ab['width'] / 404
            page.mouse.click(ab['x'] + 20 * s1, ab['y'] + (12 + 5 * 4 + 2) * s1)
            check('click moves the zoom window to 1 MB', wait_for(page, "armdos.memmap.zoomBase === 0x100000", 3), page.evaluate('armdos.memmap.zoomBase'))
            time.sleep(1)
            page.query_selector('#memmap').screenshot(path=os.path.join(OUT, 'memmap-1mb.png'))
            page.mouse.click(ab['x'] + 20 * s1, ab['y'] + (12 + 2) * s1)
            wait_for(page, "armdos.memmap.zoomBase === 0", 3)
            # the panel's own cost per frame
            ms = page.evaluate("""(async () => { const p = armdos.memmap, t0 = p.tick.bind(p); let n = 0, s = 0;
              p.tick = () => { const a = performance.now(); p.last = 0; t0(); s += performance.now() - a; n++; };
              await new Promise((r) => setTimeout(r, 3000)); p.tick = t0; return s / n; })()""")
            print(f'     panel frame: {ms:.2f} ms per frame at ~15 Hz ({ms * 15 / 10:.1f}% of one core)')
            # host speed with the map open vs closed (the page runs in real time: the driver's busy-time MIPS)
            on1 = host_mips(page, 5)
            page.click('#mmToggle')
            check('closing the section detaches the counters', wait_for(page, 'armdos.machine.cpu.act === null', 2))
            off1 = host_mips(page, 5)
            page.click('#mmToggle')
            wait_for(page, 'armdos.machine.cpu.act !== null', 2)
            on2 = host_mips(page, 5)
            page.click('#mmToggle')
            off2 = host_mips(page, 5)
            on, off = (on1 + on2) / 2, (off1 + off2) / 2
            print(f'     browser host MIPS during DOOM: map open {on:.0f}, closed {off:.0f} ({(1 - on / off) * 100:+.1f}% with the map open; noisy: real-time scenes vary)')
            page.click('#mmToggle')
            wait_for(page, 'armdos.machine.cpu.act !== null', 2)
            page.click('#inspTab')
            check('collapsing the inspector detaches the counters', wait_for(page, 'armdos.machine.cpu.act === null', 2))
            page.click('#inspTab')
            check('...and opening it attaches them again', wait_for(page, 'armdos.machine.cpu.act !== null', 2))
            check('no page errors', not errors, errors)
            b.close()
    finally:
        srv.shutdown()
    print('memmap web:', 'FAIL' if fails else 'ok')
    sys.exit(1 if fails else 0)

if __name__ == '__main__':
    main()
