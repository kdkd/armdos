#!/usr/bin/env python3
"""The ARM Pit BBS after a failed download (web/js/modem-line.js): the BBS disk is
fetched on the first call to 555-1989; if that fails (a busy server refusing a
connection shows as "NetworkError" in Firefox), the page retries, then says so -
and the next call tries again instead of ringing for ever until a reload.

usage: python3 web/tests/test_bbs_retry.py   (after ./build.sh; Chromium and Firefox)
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
STATE = "document.querySelector('.sysop-state').textContent"

def run(bt, url):
    b = bt.launch(); page = b.new_context(viewport={'width': 1300, 'height': 1000}).new_page()
    blocked = {'n': 0}
    def block(route):
        blocked['n'] += 1
        route.abort('connectionrefused')
    page.route('**/images/bbs.img.gz*', block)
    page.goto(url + '?nosw'); page.wait_for_selector('#powerBtn')
    page.click('#powerBtn')
    check(f'{bt.name}: boots', wait_for(page, PROMPT, 90))
    page.evaluate("armdos.machine.typeText('ECHO ATDT5551989>COM2\\r')")
    check(f'{bt.name}: the download fails three times, then the panel says to dial again',
          wait_for(page, f"{STATE}.includes('dial again')", 30), f"{blocked['n']} attempts: " + page.evaluate(STATE))
    check(f'{bt.name}: no BBS worker yet', page.evaluate("!armdos.line.worker"))
    page.unroute('**/images/bbs.img.gz*')
    page.evaluate("armdos.machine.modem.hangup()"); time.sleep(1)
    page.evaluate("armdos.machine.typeText('ECHO ATDT5551989>COM2\\r')")
    check(f'{bt.name}: the next call starts the BBS after all', wait_for(page, "!!armdos.line.worker", 30), page.evaluate(STATE))
    b.close()

def main():
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            for bt in (p.chromium, p.firefox): run(bt, url)
    finally:
        srv.shutdown()
    print(f'\nbbs retry (web): {"all passed" if not fails else str(fails) + " failed"}')
    sys.exit(1 if fails else 0)

main()
