#!/usr/bin/env python3
"""International keyboards on the page (apps/keyb, ARCH.md 4.6): the on-screen PC
keyboard relabels its caps for the layout KEYB has active (port F6h) - after KEYB GR,
Ctrl+Alt+F1 (US again) and KEYB DV - and "Keyboard: follow my computer's layout"
types the character the visitor's own layout made (e with acute from an AZERTY
keyboard's 2 key), in DOS's code page.

usage: python3 web/tests/test_keyb.py [--shots DIR]
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import serve
from playwright.sync_api import sync_playwright

shots = sys.argv[sys.argv.index('--shots') + 1] if '--shots' in sys.argv else None
fails = 0
def check(name, ok, extra=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + (f'  ({extra})' if extra else ''), flush=True)
    if not ok: fails += 1
def shot(page, name, **kw):
    if shots:
        os.makedirs(shots, exist_ok=True)
        if name.startswith('keys-'): page.locator('#pcKeys').screenshot(path=os.path.join(shots, name + '.png'))
        else: page.screenshot(path=os.path.join(shots, name + '.png'), **kw)
def wait_for(page, js, timeout=15):
    t = time.time()
    while time.time() - t < timeout:
        try:
            if page.evaluate(js): return True
        except Exception: pass
        time.sleep(0.15)
    return False
SCREEN = "(() => { const m = armdos.machine; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m.cpu.m8[0xB8000 + (y*80+x)*2] || 32); r.push(s.trimEnd()); } return r.join('\\n'); })()"
PROMPT = f"armdos.machine && {SCREEN}.trimEnd().endsWith('C:\\\\>')"
CAP = "(code) => { const b = document.querySelector(`#pcKeys .k[data-code=\"${code}\"]`); return b ? [...b.querySelectorAll('span')].map(s => s.className.split(' ')[0] + ':' + s.textContent + (s.classList.contains('dead') ? '*' : '')).join(' ') || b.textContent : null; }"

def cap(pg, code): return pg.evaluate(CAP, code)

def main():
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            errors = []
            b = p.chromium.launch(args=['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
            ctx = b.new_context(viewport={'width': 1440, 'height': 1100})
            pg = ctx.new_page(); pg.on('pageerror', lambda e: errors.append(str(e)))
            pg.goto(url + '?nosw')
            pg.wait_for_selector('#powerBtn')
            pg.evaluate("localStorage.removeItem('armdos.prefs')")
            pg.click('#powerBtn')
            check('boots to C:\\>', wait_for(pg, PROMPT, 90))
            pg.click('#pcKeysBtn')
            check('PC keys: US caps without KEYB', pg.evaluate("document.getElementById('pcKeys').dataset.layout") == 'US' and cap(pg, 'KeyY') == 'Y'
                  and pg.evaluate("!document.querySelector('#pcKeys .k[data-code=\"AltRight\"]')"), cap(pg, 'KeyY'))
            shot(pg, 'keys-us', full_page=True)

            # ---- KEYB GR: the German caps
            pg.focus('#screen')
            pg.keyboard.type('KEYB GR'); pg.keyboard.press('Enter')
            ok = wait_for(pg, "document.getElementById('pcKeys').dataset.layout === 'GR'", 30)
            check('after KEYB GR the keyboard is German', ok, pg.evaluate("document.getElementById('pcKeys').dataset.layout"))
            check('  Y and Z swapped', cap(pg, 'KeyY') == 'lg-1:Z' and cap(pg, 'KeyZ') == 'lg-1:Y', f"{cap(pg, 'KeyY')} / {cap(pg, 'KeyZ')}")
            check('  [ ; \' are \u00dc \u00d6 \u00c4, - is \u00df ? (with \\ on AltGr)', cap(pg, 'BracketLeft') == 'lg-1:\u00dc' and cap(pg, 'Semicolon') == 'lg-1:\u00d6'
                  and cap(pg, 'Quote') == 'lg-1:\u00c4' and cap(pg, 'Minus') == 'lg-s:? lg-b:\u00df lg-a:\\', f"{cap(pg, 'BracketLeft')} {cap(pg, 'Minus')}")
            check('  AltGr legend @ on Q (small type)', cap(pg, 'KeyQ') == 'lg-1:Q lg-a:@', cap(pg, 'KeyQ'))
            check('  dead accent keys marked: \u00b4 ` on =', cap(pg, 'Equal') == 'lg-s:`* lg-b:\u00b4*', cap(pg, 'Equal'))
            check('  the 102nd key < > | and an AltGr key appear', cap(pg, 'IntlBackslash') == 'lg-s:> lg-b:< lg-a:|'
                  and pg.evaluate("!!document.querySelector('#pcKeys .k[data-code=\"AltRight\"]')"), cap(pg, 'IntlBackslash'))
            shot(pg, 'keys-gr', full_page=True)
            # the on-screen keys type German: the key labelled Z (KeyY) gives z in DOS
            pg.evaluate("""(() => { const k = document.querySelector('#pcKeys .k[data-code="KeyY"]'); const o = { bubbles: true, pointerId: 7, isPrimary: true };
                k.dispatchEvent(new PointerEvent('pointerdown', o)); setTimeout(() => k.dispatchEvent(new PointerEvent('pointerup', o)), 60); })()""")
            check('  the on-screen "Z" key types z', wait_for(pg, f"{SCREEN}.trimEnd().endsWith('C:\\\\>z')", 10), pg.evaluate(SCREEN).splitlines()[-1] if pg.evaluate(SCREEN) else '')
            pg.focus('#screen'); pg.keyboard.press('Backspace')

            # ---- Ctrl+Alt+F1: US again
            pg.keyboard.down('Control'); pg.keyboard.down('Alt'); pg.keyboard.press('F1'); pg.keyboard.up('Alt'); pg.keyboard.up('Control')
            check('Ctrl+Alt+F1: the caps are US again', wait_for(pg, "document.getElementById('pcKeys').dataset.layout === 'US'", 10))

            # ---- KEYB DV (typed with the US layout)
            pg.keyboard.type('KEYB DV'); pg.keyboard.press('Enter')
            check('after KEYB DV the keyboard is Dvorak', wait_for(pg, "document.getElementById('pcKeys').dataset.layout === 'DV'", 30))
            check("  Q W E R T Y are ' , . P Y F", [cap(pg, c) for c in ['KeyQ', 'KeyW', 'KeyE', 'KeyR', 'KeyT', 'KeyY']] ==
                  ['lg-s:" lg-b:\'', 'lg-s:< lg-b:,', 'lg-s:> lg-b:.', 'lg-1:P', 'lg-1:Y', 'lg-1:F'], str([cap(pg, c) for c in ['KeyQ', 'KeyW', 'KeyE', 'KeyR']]))
            check('  A S D F are A O E U', [cap(pg, c) for c in ['KeyA', 'KeyS', 'KeyD', 'KeyF']] == ['lg-1:A', 'lg-1:O', 'lg-1:E', 'lg-1:U'])
            shot(pg, 'keys-dv', full_page=True)

            # ---- "Keyboard: follow my computer's layout": an AZERTY visitor's 2 key is e-acute
            check('the follow option is off by default', pg.get_attribute('#followKbdBtn', 'aria-pressed') == 'false')
            pg.click('#followKbdBtn')
            check('  ... and can be switched on', pg.get_attribute('#followKbdBtn', 'aria-pressed') == 'true')
            pg.focus('#screen')
            send = """([key, code]) => { const c = document.getElementById('screen');
                c.dispatchEvent(new KeyboardEvent('keydown', { key, code, bubbles: true, cancelable: true }));
                c.dispatchEvent(new KeyboardEvent('keyup', { key, code, bubbles: true, cancelable: true })); }"""
            # "ECHO é" as an AZERTY keyboard sends it: E C H O on their own keys, space, and é on Digit2
            for key, code in [('e', 'KeyE'), ('c', 'KeyC'), ('h', 'KeyH'), ('o', 'KeyO'), (' ', 'Space'), ('\u00e9', 'Digit2')]:
                pg.evaluate(send, [key, code]); time.sleep(0.15)
            ok = wait_for(pg, f"{SCREEN}.split('\\n').some(l => l.endsWith('C:\\\\>echo \\x82'))", 10)
            check('  typed "echo \u00e9": the e-acute arrives as 82h (code page 437) although DOS has Dvorak', ok,
                  repr([l for l in pg.evaluate(SCREEN).splitlines() if l][-2:]))
            pg.evaluate(send, ['Enter', 'Enter'])
            ok = wait_for(pg, f"{SCREEN}.split('\\n').some(l => l === '\\x82')", 10)
            check('  ... and ECHO prints it', ok)
            shot(pg, 'follow', full_page=False)
            pg.click('#followKbdBtn')
            check('  switched off again', pg.get_attribute('#followKbdBtn', 'aria-pressed') == 'false')
            check('no page errors', not errors, '; '.join(errors[:3]))
            b.close()
    finally:
        srv.shutdown()
    print('FAILED' if fails else 'all keyboard layout checks passed', flush=True)
    sys.exit(1 if fails else 0)

if __name__ == '__main__':
    main()
