#!/usr/bin/env python3
"""The pictures of the whole machine in the README and the manual, taken from the staged site
(./build.sh first): the page at 1440 px wide, the desk (machine and inspector) scaled to 1100 px.

  machine.jpg  Sopwith's title screen under ELBOW, the inspector's ELBOW view open
               (README.md, web/docs/index.html)
  boot.jpg     just after booting, at the C:\\> prompt (web/docs/machine.html)

Each goes to docs/screenshots/ and web/docs/img/. The <img> sizes in the pages must match what
this prints. Needs Python with Playwright (pip install playwright && playwright install chromium).

usage: python3 tools/screenshots.py [machine|boot ...]   (default: both)
"""
import os, sys, time
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
sys.path.insert(0, os.path.join(ROOT, 'web', 'tests'))
import serve
from playwright.sync_api import sync_playwright

WIDTH, OUT_W = 1440, 1100
SCREEN = "(() => { const m = armdos.machine; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m.cpu.m8[0xB8000 + (y*80+x)*2] || 32); r.push(s.trimEnd()); } return r.join('\\n'); })()"
PROMPT = f"armdos.machine && armdos.driver?.running && {SCREEN}.trimEnd().endsWith('C:\\\\>')"

def wait_for(page, js, timeout):
    t = time.time()
    while time.time() - t < timeout:
        try:
            if page.evaluate(js): return True
        except Exception: pass
        time.sleep(0.2)
    raise SystemExit(f'screenshots: timed out waiting for {js[:60]}...')

def boot(page, url):
    page.goto(url + '?nosw')
    page.click('#powerBtn')
    wait_for(page, PROMPT, 90)
    if page.evaluate("document.getElementById('inspector').classList.contains('closed')"): page.click('#inspTab')

def dos(page, cmd):
    page.evaluate(f"armdos.machine.typeText({(cmd + chr(13))!r})")
    time.sleep(0.4 + len(cmd) * 0.06)

def machine(page):
    dos(page, 'CD \\ELBOW\\APPS\\SOPWITH')
    dos(page, 'ELBOW SOPWITH')
    wait_for(page, "armdos.machine.vga.mode === 4 || armdos.machine.vga.mode === 6", 60)   # CGA graphics: the title screen
    page.click('#ebToggle')
    wait_for(page, "document.getElementById('ebState').textContent === 'TRANSLATING'", 30)
    time.sleep(4)                                    # the planes fly, the meters settle
    # the view shows whichever block the ARM CPU is in: wait for one with a few x86 instructions
    wait_for(page, "document.querySelectorAll('#ebX86 > *').length >= 6", 60)

def boot_shot(page):
    time.sleep(3)                                    # the MIPS meter and the interrupt log settle

def shoot(page, name):
    desk = page.locator('#desk').bounding_box()
    rig = page.locator('.rig').bounding_box()
    height = rig['y'] + rig['height'] + 14 - desk['y']      # to just under the toolbar
    data = page.screenshot(type='jpeg', quality=86, clip={'x': desk['x'], 'y': desk['y'], 'width': desk['width'], 'height': height})
    for d in ('docs/screenshots', 'web/docs/img'):
        with open(os.path.join(ROOT, d, name), 'wb') as f: f.write(data)
    print(f'{name}: {round(desk["width"] * OUT_W / desk["width"])} x {round(height * OUT_W / desk["width"])}, {len(data) // 1024} KB')

def main():
    want = sys.argv[1:] or ['machine', 'boot']
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(args=['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
            for name in want:
                ctx = b.new_context(viewport={'width': WIDTH, 'height': 1400}, device_scale_factor=OUT_W / (WIDTH - 32))
                # a returning visitor: no "click the screen to type" nudge on the monitor
                ctx.add_init_script("localStorage.setItem('armdos.prefs', JSON.stringify({ typedOnce: true, seen: true }))")
                page = ctx.new_page()
                boot(page, url)
                {'machine': machine, 'boot': boot_shot}[name](page)
                shoot(page, name + '.jpg')
                ctx.close()
            b.close()
    finally:
        srv.shutdown()

main()
