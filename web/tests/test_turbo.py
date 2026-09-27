#!/usr/bin/env python3
"""TURBO.COM on the page (apps/turbo): TURBO OFF/ON move the TURBO button and its
LED, and TURBO MAX lifts the clock limit - the real-time driver then raises the
clock to what this computer can run, and the front panel's MHz display follows.

usage: python3 web/tests/test_turbo.py
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
# the digits lit on the MHz display: segments per digit -> a digit
DIGITS = """(() => { const S = {'1111110':'0','0110000':'1','1101101':'2','1111001':'3','0110011':'4','1011011':'5','1011111':'6','1110000':'7','1111111':'8','1111011':'9'};
  const p = [...document.querySelectorAll('#mhzDisplay polygon')].map(s => s.classList.contains('on') ? 1 : 0); let r = '';
  for (let d = 0; d < p.length; d += 7) r += S[p.slice(d, d + 7).join('')] ?? ' '; return r.trim(); })()"""

def cmd(pg, c):
    pg.focus('#screen'); pg.keyboard.type(c); pg.keyboard.press('Enter'); time.sleep(0.5); wait_for(pg, PROMPT, 20)

def main():
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            errors = []
            b = p.chromium.launch(args=['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
            pg = b.new_context(viewport={'width': 1440, 'height': 1100}).new_page(); pg.on('pageerror', lambda e: errors.append(str(e)))
            pg.goto(url + '?nosw'); pg.wait_for_selector('#powerBtn')
            pg.evaluate("localStorage.removeItem('armdos.prefs')")
            pg.click('#powerBtn')
            check('boots to C:\\>', wait_for(pg, PROMPT, 90))
            check('MHz display: 100', pg.evaluate(DIGITS) == '100', pg.evaluate(DIGITS))
            cmd(pg, 'TURBO OFF')
            check('TURBO OFF: 12 MHz, button and LED out', wait_for(pg, f"{DIGITS} === '12' && document.getElementById('turboBtn').getAttribute('aria-pressed') === 'false' && !document.getElementById('turboLed').classList.contains('on')", 5), pg.evaluate(DIGITS))
            cmd(pg, 'TURBO ON')
            check('TURBO ON: 100 MHz, LED lit', wait_for(pg, f"{DIGITS} === '100' && document.getElementById('turboLed').classList.contains('on')", 5))
            cmd(pg, 'TURBO MAX')
            check('TURBO MAX: unlocked', pg.evaluate('armdos.machine.unlocked') is True)
            check('the clock moves and the display follows', wait_for(pg, f"armdos.machine.mhz !== 100 && {DIGITS} !== '100' && Math.abs(+{DIGITS} - armdos.machine.mhz) < armdos.machine.mhz", 20), f"{pg.evaluate('armdos.machine.mhz')} / {pg.evaluate(DIGITS)}")
            cmd(pg, 'TURBO RESTORE')
            check('TURBO RESTORE: 100 MHz again', wait_for(pg, f"!armdos.machine.unlocked && {DIGITS} === '100'", 5), pg.evaluate(DIGITS))
            check('no page errors', not errors, '; '.join(errors[:3]))
            b.close()
    finally:
        srv.shutdown()
    print(f'\nturbo (web): {"all passed" if not fails else str(fails) + " failed"}')
    sys.exit(1 if fails else 0)

main()
