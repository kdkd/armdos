#!/usr/bin/env python3
"""The inspector's ELBOW view (web/js/elbow-panel.js, elbow-probe.js, x86disasm.js):
while ELBOW runs FIRE.COM it shows the busiest translated block - x86 instructions
next to the ARM code ELBOW generated - holds it steady, lights the sampled ARM lines,
says where the time goes, stacks its two columns at phone width, samples nothing
while closed, and goes back to "not running" when ELBOW ends (ports FCh-FFh cleared).
Screenshots: build/elbow-web/*.png.

usage: node web/tools/build-site.mjs && python3 web/tests/test_elbow.py
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import serve
from playwright.sync_api import sync_playwright

OUT = os.path.join(serve.ROOT, 'build', 'elbow-web')
fails = 0
def check(name, ok, extra=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + (f'  ({extra})' if extra else ''), flush=True)
    if not ok: fails += 1
def wait_for(page, js, timeout=15):
    t = time.time()
    while time.time() - t < timeout:
        try:
            if page.evaluate(js): return True
        except Exception: pass
        time.sleep(0.15)
    return False
SCREEN = "(() => { const m = armdos.machine; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m.cpu.m8[0xB8000 + (y*80+x)*2] || 32); r.push(s.trimEnd()); } return r.join('\\n'); })()"
PROMPT = f"armdos.machine && armdos.machine.cpu.m8[0x449] === 3 && {SCREEN}.trimEnd().endsWith('C:\\\\>')"
TEXT = "((id) => document.getElementById(id).textContent)"
COUNT = "((sel) => document.querySelectorAll(sel).length)"

def main():
    os.makedirs(OUT, exist_ok=True)
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            errors = []
            b = p.chromium.launch(args=['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
            ctx = b.new_context(viewport={'width': 1440, 'height': 1100})
            pg = ctx.new_page(); pg.on('pageerror', lambda e: errors.append(str(e)))
            pg.goto(url + '?nosw'); pg.wait_for_selector('#powerBtn')
            pg.evaluate("localStorage.removeItem('armdos.prefs')")
            pg.reload(); pg.wait_for_selector('#powerBtn')
            check('ELBOW section in the inspector, closed by default', pg.evaluate("!!document.getElementById('ebToggle') && document.getElementById('elbowView').hidden"))
            pg.click('#powerBtn')
            check('boots to C:\\>', wait_for(pg, PROMPT, 90))
            pg.click('#ebToggle')
            check('opened: "ELBOW is not running"', wait_for(pg, f"{TEXT}('ebStatus').includes('not running')", 5), pg.evaluate(f"{TEXT}('ebStatus')"))
            pg.focus('#screen'); pg.keyboard.type('cd \\elbow'); pg.keyboard.press('Enter'); time.sleep(0.5)
            wait_for(pg, PROMPT.replace('C:\\\\>', 'C:\\\\ELBOW>'), 10)
            pg.keyboard.type('fire'); pg.keyboard.press('Enter')
            check('FIRE under ELBOW: a translated block is shown', wait_for(pg, f"{TEXT}('ebHead').startsWith('BLOCK') && {COUNT}('#ebX86 .ln') > 2 && {COUNT}('#ebArm .ln') > 10", 40), pg.evaluate(f"{TEXT}('ebHead')"))
            time.sleep(2)
            x86 = pg.evaluate("[...document.querySelectorAll('#ebX86 .ln')].map(e => e.textContent)")
            arm = pg.evaluate("[...document.querySelectorAll('#ebArm .ln')].map(e => e.textContent)")
            check('x86 column: real instructions, no db', len(x86) > 2 and not any(' db ' in (' ' + l) for l in x86), ' | '.join(x86[:6]))
            check('ARM column: ELBOW\'s code (fp = &cpu, chained exits)', any('[fp' in l for l in arm) and any('exit' in l for l in arm), ' | '.join(arm[:4]))
            check('sampled ARM lines glow, the latest is marked', wait_for(pg, f"{COUNT}('#ebArm .ln.h1, #ebArm .ln.h2, #ebArm .ln.h3, #ebArm .ln.h4') > 0 && {COUNT}('#ebArm .ln.cur') === 1", 5))
            check('the time bar: translated code and helpers', wait_for(pg, "[...document.querySelectorAll('#ebBar .eb-legend span:not(.off)')].some(e => e.textContent.startsWith('TRANSLATED'))", 5), pg.evaluate(f"{TEXT}('ebBar')"))
            check('caption: TRANSLATING', pg.evaluate(f"{TEXT}('ebState')") == 'TRANSLATING')
            heads = set()
            for _ in range(12):
                time.sleep(0.25); heads.add(pg.evaluate(f"{TEXT}('ebHead')"))
            check('the block shown holds steady (3 s)', len(heads) <= 3, f'{len(heads)} different: ' + ' / '.join(list(heads)[:3]))
            pg.locator('#elbowView').screenshot(path=os.path.join(OUT, 'elbow-fire.png'))
            # phone width: the two columns stack and nothing overflows the page
            pg.set_viewport_size({'width': 400, 'height': 900})
            time.sleep(0.6)
            wait_for(pg, 'document.documentElement.scrollWidth <= innerWidth', 8)
            # (the page itself jitters by a few px while the monitor resizes: measure the inspector)
            geo = pg.evaluate("(() => { const a = document.getElementById('ebX86').getBoundingClientRect(), b = document.getElementById('ebArm').getBoundingClientRect(), v = document.getElementById('elbowView').getBoundingClientRect(), body = document.getElementById('inspBody'); return { stacked: b.top >= a.bottom - 1, right: Math.round(v.right), vw: innerWidth, body: body.scrollWidth - body.clientWidth }; })()")
            check('phone width: columns stacked, the panel fits the screen', geo['stacked'] and geo['right'] <= geo['vw'] and geo['body'] <= 1, str(geo))
            pg.locator('#elbowView').screenshot(path=os.path.join(OUT, 'elbow-phone.png'))
            pg.set_viewport_size({'width': 1440, 'height': 1100})
            # closed: no sampling at all
            pg.click('#ebToggle')
            n0 = pg.evaluate('armdos.elbow.n'); time.sleep(1.0); n1 = pg.evaluate('armdos.elbow.n')
            check('closed: no samples taken', n0 == n1 and pg.evaluate("document.getElementById('elbowView').hidden"), f'{n0} -> {n1}')
            check('closed caption still says x86 is running', wait_for(pg, f"{TEXT}('ebState') === 'X86 RUNNING'", 3))
            pg.click('#ebToggle')
            pg.focus('#screen'); pg.keyboard.press('x')
            check('FIRE ends on a key', wait_for(pg, PROMPT.replace('C:\\\\>', 'C:\\\\ELBOW>'), 30))
            check('ELBOW gone: descriptor cleared, "not running"', wait_for(pg, f"armdos.machine.elbowDesc === 0 && {TEXT}('ebStatus').includes('not running')", 5), pg.evaluate(f"{TEXT}('ebStatus')"))
            check('no page errors', not errors, '; '.join(errors[:3]))
            b.close()
    finally:
        srv.shutdown()
    print(f'\nelbow view (web): {"all passed" if not fails else str(fails) + " failed"}')
    sys.exit(1 if fails else 0)

main()
