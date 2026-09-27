#!/usr/bin/env python3
"""The keyboard nudge (web/js/input.js): until the first key typed into the machine, a
switched-on machine without the keyboard shows "Click the screen to type"; typing while
nothing on the page takes text gives the screen the keyboard and the key (Enter no longer
presses a button that was last clicked with the mouse); once something has been typed, the
nudge stays away.

usage: python3 web/tests/test_kbdnudge.py   (after node web/tools/build-site.mjs)
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import serve
from playwright.sync_api import sync_playwright

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
PROMPT = f"armdos.machine && {SCREEN}.trimEnd().endsWith('C:\\\\>')"
NUDGE = "!document.getElementById('kbdNudge').hidden"
FOCUSED = "document.activeElement === document.getElementById('screen')"

def main():
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            pg = b.new_context(viewport={'width': 1440, 'height': 1000}).new_page()
            errors = []; pg.on('pageerror', lambda e: errors.append(str(e)))
            pg.goto(url + '?nosw'); pg.wait_for_selector('#powerBtn')
            pg.evaluate("localStorage.removeItem('armdos.prefs')"); pg.reload(); pg.wait_for_selector('#powerBtn')
            check('switched off: no nudge', not pg.evaluate(NUDGE))
            pg.click('#powerBtn')
            check('boots to C:\\>', wait_for(pg, PROMPT, 90))
            check('after power-on the screen has the keyboard, no nudge', pg.evaluate(FOCUSED) and not pg.evaluate(NUDGE))
            sound0 = pg.evaluate("document.getElementById('soundBtn')?.getAttribute('aria-pressed')")
            pg.click('#soundBtn')                                  # a mouse click moves the focus to a button
            sound1 = pg.evaluate("document.getElementById('soundBtn')?.getAttribute('aria-pressed')")
            check('first use, the keyboard elsewhere: the nudge shows', wait_for(pg, NUDGE, 3))
            pg.keyboard.press('v')
            check('typing takes the keyboard back and hides the nudge', wait_for(pg, f"{FOCUSED} && !({NUDGE})", 3))
            pg.keyboard.press('e'); pg.keyboard.press('r'); pg.keyboard.press('Enter')
            check('the first key reached the PC: the typed "ver" is on the screen', wait_for(pg, f"{SCREEN}.includes('C:\\\\>ver')", 10), pg.evaluate(SCREEN)[-300:])
            pg.click('#soundBtn')                                  # focus on the button again
            sound2 = pg.evaluate("document.getElementById('soundBtn')?.getAttribute('aria-pressed')")
            pg.keyboard.press('Enter')
            time.sleep(0.3)
            check('Enter goes to the PC, not to the button last clicked', pg.evaluate(FOCUSED) and pg.evaluate("document.getElementById('soundBtn')?.getAttribute('aria-pressed')") == sound2)
            pg.click('#soundBtn'); time.sleep(0.6)
            check('once something was typed, no more nudge', not pg.evaluate(NUDGE))
            pg.focus('#soundBtn'); pg.keyboard.press('Tab')
            check('Tab still moves through the page', not pg.evaluate(FOCUSED))
            check('no page errors', not errors, '; '.join(errors[:2]))
            b.close()
    finally:
        srv.shutdown()
    print(f'\nkbd nudge (web): {"all passed" if not fails else str(fails) + " failed"}')
    sys.exit(1 if fails else 0)

main()
