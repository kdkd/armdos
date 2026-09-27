#!/usr/bin/env python3
"""Smoke test of a served ARM-DOS site in Chromium and Firefox (Playwright).

    python3 web/tests/serve.py public_html --port 8000 &     # or any web server
    python3 tools/smoke.py http://127.0.0.1:8000/armdos/

Checks, in each browser: the page loads without console errors or failed requests,
the power switch boots it to C:\\>, DOOM's data is on drive C:, and the manual
(docs/) opens, if the site has one. Needs Python with Playwright
(pip install playwright && playwright install chromium firefox).
"""
import sys, time, urllib.request, urllib.error
from playwright.sync_api import sync_playwright

URL = sys.argv[1] if len(sys.argv) > 1 else 'http://127.0.0.1:8000/armdos/'
if not URL.endswith('/'): URL += '/'
SCREEN = "(() => { const m = armdos.machine; if (!m) return ''; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m.cpu.m8[0xB8000 + (y*80+x)*2] || 32); r.push(s.trimEnd()); } return r.join('\\n'); })()"
fails = 0
def check(name, ok, extra=''):
    global fails
    print(('  ok   ' if ok else '  FAIL ') + name + (f'  ({extra})' if extra else ''), flush=True)
    if not ok: fails += 1
def wait_for(page, js, timeout):
    t = time.time()
    while time.time() - t < timeout:
        if page.evaluate(js): return True
        time.sleep(0.2)
    return False

try:
    has_docs = urllib.request.urlopen(URL + 'docs/').status == 200
except urllib.error.URLError:
    has_docs = False

with sync_playwright() as p:
    for bt in (p.chromium, p.firefox):
        print(f'{bt.name}: {URL}', flush=True)
        b = bt.launch()
        page = b.new_page(viewport={'width': 1440, 'height': 1000})
        errs = []
        page.on('console', lambda m: errs.append('console: ' + m.text) if m.type == 'error' else None)
        page.on('pageerror', lambda e: errs.append('page error: ' + str(e)))
        page.on('response', lambda r: errs.append(f'HTTP {r.status} {r.url}') if r.status >= 400 else None)
        page.on('requestfailed', lambda r: errs.append(f'request failed: {r.url} {r.failure}'))
        page.goto(URL)
        page.wait_for_selector('#powerBtn')
        page.click('#powerBtn')
        check('boots to C:\\>', wait_for(page, f"{SCREEN}.includes('C:\\\\>')", 90))
        page.click('#screen')
        for cmd in ('CD GAMES', 'CD DOOM', 'DIR'):     # (no backslashes: they depend on the host's keyboard layout)
            page.keyboard.type(cmd + '\n', delay=40)
            time.sleep(1.0)
        check("DOOM1.WAD is on drive C:", wait_for(page, f"/DOOM1\\s+WAD\\s+4,?196,?020/.test({SCREEN})", 30), page.evaluate(SCREEN).strip().splitlines()[-4:])
        time.sleep(1)
        check('no console errors or failed requests', not errs, '; '.join(errs[:8]))
        scope = page.evaluate("navigator.serviceWorker ? navigator.serviceWorker.ready.then((r) => r.scope) : null")
        check('the service worker is active for this directory', scope == URL, scope)
        if has_docs:
            del errs[:]
            page.click('a.manual')      # (leaving the page cancels its background disk downloads: not errors)
            page.wait_for_load_state('load')
            check('the manual link opens docs/', page.url.rstrip('/').endswith('/docs') or page.url.endswith('/docs/index.html'), page.url)
            time.sleep(1)
            bad = [e for e in errs if 'ABORTED' not in e]
            check('the manual loads without errors', not bad, '; '.join(bad[:8]))
        else:
            print('  note: the site has no docs/ yet; manual link not checked')
        b.close()
print('smoke: ' + ('all checks passed' if not fails else f'{fails} check(s) FAILED'))
sys.exit(1 if fails else 0)
