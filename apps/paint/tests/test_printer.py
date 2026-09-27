#!/usr/bin/env python3
"""Playwright checks of the page's printer as an Epson FX-80 (web/js/printer.js).

usage: python3 apps/paint/tests/test_printer.py [--no-stage]

Stages its own copy of the site (build/paint-web/site) and a second copy with the
text-only printer.js from before the graphics (tests/printer-before.js), then:
  * plain text prints pixel-for-pixel as before (both pages with the same seeded
    Math.random, the canvases compared),
  * ESC/P type styles (bold, italic, underline, double width, condensed, elite,
    superscript) and line spacing (ESC 3, ESC J) move the head and paper as the
    FX-80 does,
  * bit images: ESC K / ESC L / ESC * 0..3 / ESC ^ put 8 (9) pins a column at the
    right density; a picture (ARM Paint's print stream of SUNSET.PCX, captured by
    run.mjs in build/paint-test/print-letter.prn if it exists, else a generated
    test pattern) comes out on the green-bar paper,
  * the tear-off PNG includes it.
Screenshots (LOOK at them): build/paint-test/web-*.png
"""
import os, sys, time, shutil, subprocess, json, base64, io
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'web', 'tests'))
import serve
from playwright.sync_api import sync_playwright
from PIL import Image, ImageChops

OUT = os.path.join(ROOT, 'build', 'paint-test')
SITE = os.path.join(ROOT, 'build', 'paint-web', 'site')
SITE0 = os.path.join(ROOT, 'build', 'paint-web', 'site-before')
os.makedirs(OUT, exist_ok=True)
fails = 0
def check(name, ok, extra=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + (f'  ({extra})' if extra else ''))
    if not ok: fails += 1

def stage():
    subprocess.run(['node', 'web/tools/build-site.mjs', '--out', 'build/paint-web/site'], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    shutil.rmtree(SITE0, ignore_errors=True)
    shutil.copytree(SITE, SITE0)
    shutil.copy(os.path.join(HERE, 'printer-before.js'), os.path.join(SITE0, 'js', 'printer.js'))
    # no service worker in the way
    for s in (SITE, SITE0):
        p = os.path.join(s, 'sw.js')
        if os.path.exists(p): open(p, 'w').write('')

SEED = """
(() => { let a = 1988; Math.random = function () { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a);
  t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; })();
"""
TEXT = ('ARM-DOS 4.00 printer test\r\n'
        'The quick brown fox jumps over the lazy dog. 0123456789 \xC9\xCD\xBB\r\n\r\n'
        'C:\\>DIR\r\n\tTAB\x08X\r\n' + 'W' * 85 + '\r\n')

def wait(page, js, timeout=60):
    t = time.time()
    while time.time() - t < timeout:
        if page.evaluate(js): return True
        time.sleep(0.1)
    return False

def canvas_png(page, idx=-1):
    url = page.evaluate(f"(() => {{ const c = [...document.querySelectorAll('#paperFeed canvas')]; return c.at({idx}).toDataURL('image/png'); }})()")
    return Image.open(io.BytesIO(base64.b64decode(url.split(',', 1)[1]))).convert('RGB')

def send(page, data):
    if isinstance(data, str): data = [ord(c) for c in data]
    page.evaluate("(bytes) => { for (const b of bytes) armdosPrinter.enqueue(b); }", list(data))

def settle(page, timeout=120):
    return wait(page, "armdosPrinter.queue.length === 0 && !document.getElementById('prData').classList.contains('on')", timeout)

def text_page(p, url):
    b = p.chromium.launch(args=['--use-gl=angle', '--use-angle=swiftshader'])
    ctx = b.new_context(viewport={'width': 1440, 'height': 1000})
    page = ctx.new_page()
    page.add_init_script(SEED)
    page.goto(url + '?nosw')
    page.wait_for_selector('#powerBtn')
    wait(page, "!!window.armdosPrinter")
    page.evaluate("document.fonts.ready")
    send(page, [ord(c) for c in TEXT])
    settle(page)
    time.sleep(0.3)
    img = canvas_png(page)
    b.close()
    return img

def esc(*a):
    out = []
    for x in a:
        if isinstance(x, str): out += [ord(c) for c in x]
        elif isinstance(x, (bytes, bytearray, list)): out += list(x)
        else: out.append(x)
    return out

def test_pattern():
    """a 480-column test picture as ESC * 1 bands: a circle, a grey ramp, 'FX-80'"""
    import math
    W, H = 480, 160
    ink = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            d = math.hypot((x - 90) / 2, y - 80)          # 120 dpi across, 72 down: a round circle
            if 55 < d < 70: ink[y][x] = 1
            if d < 40 and ((x + y) % 3 == 0): ink[y][x] = 1
            if x >= 200 and 20 <= y < 140:
                g = (x - 200) / 280                        # a ramp, ordered dither
                bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
                if g * 16 > bayer[y % 4][x % 4] + 0.5: ink[y][x] = 1
    data = esc(0x1B, '@', 0x1B, '3', 24)
    for band in range(H // 8):
        cols = []
        for x in range(W):
            v = 0
            for p in range(8):
                if ink[band * 8 + p][x]: v |= 0x80 >> p
            cols.append(v)
        data += esc(0x1B, '*', 1, W & 255, W >> 8, cols, '\r\n')
    return data + esc(0x1B, '2')

def main():
    if '--no-stage' not in sys.argv or not os.path.exists(SITE):
        stage()
    srv, url = serve.start(SITE)
    srv0, url0 = serve.start(SITE0)
    try:
        with sync_playwright() as p:
            # ---- text: exactly as before
            before = text_page(p, url0)
            after = text_page(p, url)
            dimg = ImageChops.difference(before, after).convert('L')
            diff = sum(1 for v in dimg.tobytes() if v)
            # (the old page alone differs from itself in a pixel or two between runs: Chromium's
            # rasterisation of overlapping anti-aliased arcs; anything real is thousands of pixels)
            before.crop((0, 0, 1368, 260)).save(os.path.join(OUT, 'web-text-before.png'))
            after.crop((0, 0, 1368, 260)).save(os.path.join(OUT, 'web-text-after.png'))
            check('plain text prints pixel for pixel as before (same seeded Math.random)', diff <= 4, f'{diff} pixels differ')

            b = p.chromium.launch(args=['--use-gl=angle', '--use-angle=swiftshader'])
            ctx = b.new_context(viewport={'width': 1440, 'height': 1000}, accept_downloads=True)
            page = ctx.new_page()
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.goto(url + '?nosw')
            page.wait_for_selector('#powerBtn')
            wait(page, "!!window.armdosPrinter")
            page.evaluate("armdosPrinter.speed = 6")

            # ---- styles and spacing
            y0 = page.evaluate("armdosPrinter.y")
            send(page, esc('Normal ', 0x1B, 'E', 'Emphasized', 0x1B, 'F', ' ', 0x1B, '4', 'Italic', 0x1B, '5', ' ',
                           0x1B, '-', 1, 'Underlined', 0x1B, '-', 0, ' ', 0x1B, 'W', 1, 'Wide', 0x1B, 'W', 0, '\r\n',
                           0x0F, 'Condensed: 137 characters fit on the 8-inch line of the FX-80 in condensed mode.', 0x12, ' ',
                           0x1B, 'M', 'Elite', 0x1B, 'P', ' x', 0x1B, 'S', 0, '2', 0x1B, 'T', ' ', 0x0E, 'SO wide', '\r\n'))
            settle(page)
            y1 = page.evaluate("armdosPrinter.y")
            check('two text lines feed 2 x 36/216"', y1 - y0 == 72, f'{y0} -> {y1}')
            send(page, esc(0x1B, '3', 12, '\n', 0x1B, 'J', 30, 0x1B, 'A', 10, '\n', 0x1B, '0', '\n', 0x1B, '2'))
            settle(page)
            y2 = page.evaluate("armdosPrinter.y")
            check('ESC 3 12 + LF, ESC J 30, ESC A 10 + LF, ESC 0 + LF: 12 + 30 + 30 + 27 units', y2 - y1 == 99, f'{y1} -> {y2}')
            send(page, esc(0x1B, '@', 'X', 0x1B, '$', 60, 0, 'Y'))
            settle(page)
            check('ESC $ 60 0 moves the head to 1" from the margin', abs(page.evaluate("armdosPrinter.x") - 144 - 14.4) < 0.01)
            send(page, '\r\n\r\n')
            settle(page)

            # ---- bit images at every density
            st0 = page.evaluate("({...armdosPrinter.stats})")
            for mode, cmd, dpi in [(0, esc(0x1B, 'K', 120, 0), 60), (1, esc(0x1B, 'L', 120, 0), 120), (2, esc(0x1B, 'Y', 120, 0), 120),
                                   (3, esc(0x1B, 'Z', 120, 0), 240)]:
                x0 = page.evaluate("armdosPrinter.x")
                send(page, cmd + [0xFF if i % 2 == 0 else 0x81 for i in range(120)])
                settle(page)
                w = page.evaluate("armdosPrinter.x") - x0
                check(f'ESC {chr(cmd[1])}: 120 columns at {dpi} dpi = {120 / dpi:.2f}" of head travel', abs(w - 144 * 120 / dpi) < 0.01, w)
                send(page, ' ')
                settle(page)
            for m, dpi in [(4, 80), (5, 72), (6, 90)]:
                x0 = page.evaluate("armdosPrinter.x")
                send(page, esc(0x1B, '*', m, 36, 0, [0x3C] * 36))
                settle(page)
                check(f'ESC * {m}: {dpi} dpi', abs(page.evaluate("armdosPrinter.x") - x0 - 144 * 36 / dpi) < 0.01)
                send(page, ' ')
                settle(page)
            x0 = page.evaluate("armdosPrinter.x")
            send(page, esc(0x1B, '^', 0, 20, 0, [0xFF, 0x80] * 20, '\r\n'))
            settle(page)
            st1 = page.evaluate("({...armdosPrinter.stats})")
            check('ESC ^ 0: 9-pin columns, 2 bytes each', st1['gfxCols'] - st0['gfxCols'] == 4 * 120 + 3 * 36 + 20)
            check('graphics put dots on the paper', st1['dots'] - st0['dots'] > 3000, st1['dots'] - st0['dots'])
            send(page, '\r\n')

            # ---- a picture
            prn = os.path.join(OUT, 'print-letter.prn')
            data = list(open(prn, 'rb').read()) if os.path.exists(prn) else test_pattern()
            send(page, esc(0x1B, '@', 'The ARM/AT prints a picture (ESC * bit images):\r\n'))
            settle(page)
            page.evaluate("armdosPrinter.speed = 1")
            send(page, data)
            # watch it print: the head moves in passes
            page.locator('.printer-wrap').scroll_into_view_if_needed()
            hx = set()
            for _ in range(12):
                time.sleep(0.12)
                hx.add(page.evaluate("armdosPrinter.head.style.transform"))
            page.screenshot(path=os.path.join(OUT, 'web-printing.png'), clip=page.locator('.printer-wrap').bounding_box())
            page.evaluate("armdosPrinter.speed = 8")
            check('the picture prints', settle(page, 180))
            time.sleep(0.5)
            page.screenshot(path=os.path.join(OUT, 'web-printer.png'), clip=page.locator('.printer-wrap').bounding_box())
            # the sheet itself, full resolution
            n = page.evaluate("document.querySelectorAll('#paperFeed canvas').length")
            sheets = [canvas_png(page, i) for i in range(1, n)]
            tall = Image.new('RGB', (sheets[0].width, sum(s.height for s in sheets)))
            yy = 0
            for s in sheets: tall.paste(s, (0, yy)); yy += s.height
            tall.save(os.path.join(OUT, 'web-sheets.png'))
            check('the head moves across the line while printing', len(hx) >= 4, len(hx))
            with page.expect_download() as dl:
                page.click('#tearOff')
            path = os.path.join(OUT, 'web-tearoff.png')
            dl.value.save_as(path)
            t = Image.open(path)
            check('tear off: a PNG with everything printed', t.width == 1368 and t.height > 900, t.size)
            check('no page errors', not errors, errors)
            b.close()
    finally:
        srv.shutdown(); srv0.shutdown()
    print('\nall printer checks passed' if not fails else f'\n{fails} failure(s)')
    sys.exit(1 if fails else 0)

if __name__ == '__main__':
    main()
