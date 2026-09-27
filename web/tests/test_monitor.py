#!/usr/bin/env python3
"""Playwright checks of the monitor swap (VGA colour / Hercules mono, emu/dev/hercules.mjs).
Needs a staged site (make site).

usage: python3 web/tests/test_monitor.py [--shots DIR]
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import serve
from playwright.sync_api import sync_playwright

shots = sys.argv[sys.argv.index('--shots') + 1] if '--shots' in sys.argv else None
fails = 0
def check(name, ok, extra=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + (f'  ({extra})' if extra else ''))
    if not ok: fails += 1
def shot(page, name, **kw):
    if shots:
        os.makedirs(shots, exist_ok=True)
        page.screenshot(path=os.path.join(shots, name + '.png'), **kw)

ARGS = ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']
def screen_js(base):
    return ("(() => { const m = armdos.machine; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) "
            f"s += String.fromCharCode(m.cpu.m8[{base} + (y*80+x)*2] || 32); r.push(s.trimEnd()); }} return r.join('\\n'); }})()")
MONO, COLOR = screen_js('0xB0000'), screen_js('0xB8000')
# the average colour of the bright pixels on the tube (the WebGL canvas keeps its drawing buffer)
TINT = """(() => { const c = document.getElementById('screen'); const o = document.createElement('canvas'); o.width = 160; o.height = 120;
  const x = o.getContext('2d'); x.drawImage(c, 0, 0, 160, 120); const d = x.getImageData(0, 0, 160, 120).data;
  let r = 0, g = 0, b = 0, n = 0; for (let i = 0; i < d.length; i += 4) { if (d[i] + d[i+1] + d[i+2] > 180) { r += d[i]; g += d[i+1]; b += d[i+2]; n++; } }
  return n ? [r / n, g / n, b / n, n] : [0, 0, 0, 0]; })()"""

def wait_for(page, js, timeout=15):
    t = time.time()
    while time.time() - t < timeout:
        if page.evaluate(js): return True
        time.sleep(0.1)
    return False

def main():
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(args=ARGS)
            page = b.new_context(viewport={'width': 1440, 'height': 1000}).new_page()
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.goto(url + '?nosw')
            page.wait_for_selector('#monitorSel')
            check('Monitor selector next to the CRT toggle, VGA by default',
                  page.evaluate("document.getElementById('monitorSel').value === 'vga' && document.getElementById('crtBtn').nextElementSibling.contains(document.getElementById('monitorSel'))"))

            # ---- amber Hercules from a cold start
            page.select_option('#monitorSel', 'amber')
            check('no note while the machine is off', page.evaluate("document.getElementById('monitorNote').hidden"))
            page.click('#powerBtn')
            check('POST says Monochrome (Hercules)', wait_for(page, f"armdos.machine && {MONO}.includes('Monochrome (Hercules)')", 30))
            check('machine has the Hercules card', page.evaluate("armdos.machine.video === 'hercules' && armdos.machine.hgc.monitor === 'amber'"))
            check('boots DOS in mode 7', wait_for(page, f"{MONO}.includes('C:\\\\>') && armdos.machine.cpu.m8[0x449] === 7", 40))
            time.sleep(1.2)
            r, g, bl, n = page.evaluate(TINT)
            check('the tube glows amber', n > 50 and r > g * 1.2 and g > bl * 1.8, f'rgb {r:.0f},{g:.0f},{bl:.0f} over {n} px')
            shot(page, 'mono-amber', clip=page.locator('#monitor').bounding_box())

            # ---- swap while running: a note, nothing changes until the power cycle
            page.select_option('#monitorSel', 'green')
            check('note: takes effect at the next power-on', page.evaluate("!document.getElementById('monitorNote').hidden && /next power-on/.test(document.getElementById('monitorNote').textContent)"))
            check('still amber until then', page.evaluate("armdos.machine.hgc.monitor === 'amber'"))
            page.click('#powerBtn'); time.sleep(2.2)
            check('note gone with the power off', page.evaluate("document.getElementById('monitorNote').hidden"))
            page.click('#powerBtn')
            check('green: boots DOS again', wait_for(page, f"armdos.machine.hgc && armdos.machine.hgc.monitor === 'green' && {MONO}.includes('C:\\\\>')", 40))
            time.sleep(1.2)
            r, g, bl, n = page.evaluate(TINT)
            check('the tube glows green', n > 50 and g > r * 1.5 and g > bl * 1.3, f'rgb {r:.0f},{g:.0f},{bl:.0f}')
            shot(page, 'mono-green', clip=page.locator('#monitor').bounding_box())
            page.evaluate("armdos.machine.typeText('C:\\\\DEMO\\\\HERCULES\\r')")
            check('HERCULES.EXE switches the card to graphics', wait_for(page, "armdos.machine.hgc.graphics", 20))
            time.sleep(2)
            shot(page, 'hgc-demo', clip=page.locator('#monitor').bounding_box())
            page.evaluate("armdos.machine.typeText('{ESC}')")
            check('and back to text', wait_for(page, "!armdos.machine.hgc.graphics", 10))

            # ---- back to VGA
            page.select_option('#monitorSel', 'vga')
            page.click('#powerBtn'); time.sleep(2.2); page.click('#powerBtn')
            check('VGA again after the power cycle', wait_for(page, f"armdos.machine.video === 'vga' && !armdos.machine.hgc && {COLOR}.includes('C:\\\\>')", 40))
            check('choice remembered', page.evaluate("JSON.parse(localStorage.getItem('armdos.prefs') || '{}').monitor === 'vga'"))
            check('no page errors', not errors, '; '.join(errors)[:300])
            b.close()
    finally:
        srv.shutdown()
    print(f'\n{fails} failure(s)' if fails else '\nall monitor checks passed')
    sys.exit(1 if fails else 0)

if __name__ == '__main__':
    main()
