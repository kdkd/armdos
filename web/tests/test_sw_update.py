#!/usr/bin/env python3
"""Updates through the service worker (web/sw.js, web/js/main.js swStart) survive failures:

 1. a release whose download is cut off part way does not install: the old release keeps
    running, online and offline;
 2. a site half way through an upload (sw.js new, a file still old) fails the same way (every
    shell file is checked against the hash sw.js lists for it), and that attempt fetches only
    what the first one did not get;
 3. once the new release is all there, loading the page brings it in (the page reloads itself
    once, before the machine has loaded anything) and the old release's cache goes;
 4. a release that arrives while the machine is running waits: the page keeps its files and
    the running machine carries on; the next load brings it in;
 5. with two ARM-DOS windows open the new release waits for the other one to close;
 6. over a worker from before this scheme, a new release takes over as soon as it is installed.

usage: python3 web/tests/test_sw_update.py [chromium|firefox ...]   (after ./build.sh; default both)
"""
import hashlib, http.server, json, os, re, shutil, socket, sys, tempfile, threading, time
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

class Handler(serve.Handler):
    fail = {}           # path under the site -> 'cut' (the connection drops half way through the body)
    seen = []           # paths requested, in order
    def do_GET(self):
        rel = self.path.split('?')[0].split('#')[0][len(self.prefix):]
        Handler.seen.append(rel)
        if Handler.fail.get(rel) == 'cut':
            data = open(os.path.join(self.site, rel), 'rb').read()
            self.send_response(200)
            self.send_header('Content-Type', 'text/javascript')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data[:len(data) // 2]); self.wfile.flush()
            self.close_connection = True
            try: self.connection.shutdown(socket.SHUT_RDWR)
            except OSError: pass
            return
        super().do_GET()

def run(p, name):
    tmp = tempfile.mkdtemp(prefix='armdos-swup-')
    site = os.path.join(tmp, 'site')
    shutil.copytree(serve.SITE, site)
    H = type('H', (Handler,), {'site': site, 'prefix': serve.PREFIX})
    srv = serve.Server(('127.0.0.1', 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{srv.server_address[1]}{serve.PREFIX}'
    swf = os.path.join(site, 'sw.js')
    util = os.path.join(site, 'js', 'util.js')
    util_a = open(util, 'rb').read()

    def build_of_sw():
        return re.search(r'const BUILD = "(\w+)"', open(swf).read()).group(1)
    def release(tag):
        """Publish a new release: util.js gains a marker, sw.js a new build id and util.js's new hash."""
        body = util_a + f"\nexport const RELEASE = '{tag}';\n".encode()
        sw = open(swf).read()
        old = build_of_sw()
        new = (tag.lower() * 12)[:12]
        sw = sw.replace(f'const BUILD = "{old}"', f'const BUILD = "{new}"')
        sw = re.sub(r'\["js/util\.js","[0-9a-f]+"\]', f'["js/util.js","{hashlib.sha256(body).hexdigest()[:16]}"]', sw)
        open(swf, 'w').write(sw)
        return new, body
    RUNNING = "fetch('js/util.js').then(r => r.text()).then(t => (t.match(/RELEASE = '(\\w)'/) || [])[1] || 'A')"
    LOADED = "!!(window.armdos && armdos.rom)"
    CACHES = "caches.keys()"
    errors, warnings = [], []

    try:
        b = getattr(p, name).launch()
        print(f'--- {name}', flush=True)
        ctx = b.new_context(viewport={'width': 1300, 'height': 1000})
        pg = ctx.new_page()
        pg.on('pageerror', lambda e: errors.append(str(e)))
        pg.on('console', lambda m: warnings.append(m.text) if m.type == 'warning' else None)
        navs = []
        pg.on('framenavigated', lambda f: navs.append(f.url) if f == pg.main_frame else None)

        # ============ release A installed and in control
        build_a = build_of_sw()
        pg.goto(url)
        wait_for(pg, "navigator.serviceWorker.ready.then(() => true)", 20)
        pg.reload()
        check('A: installed and controls the page', wait_for(pg, "!!navigator.serviceWorker.controller", 10) and
              wait_for(pg, f"caches.has('armdos-app-{build_a}')", 10), pg.evaluate(CACHES))

        # ============ 1. release B, its download cut off part way
        build_b, util_b = release('B')
        open(util, 'wb').write(util_b)
        Handler.fail = {'js/util.js': 'cut'}
        warnings.clear(); navs.clear()
        pg.goto(url)
        check('cut-off download: the install fails, the page carries on',
              wait_for(pg, LOADED, 40) and any('did not arrive complete' in w for w in warnings), '; '.join(warnings[-2:]))
        check('cut-off download: still release A, and its cache kept', pg.evaluate(RUNNING) == 'A' and pg.evaluate(f"caches.has('armdos-app-{build_a}')"),
              pg.evaluate(RUNNING))
        check('cut-off download: the page did not reload', len(navs) == 1, navs)
        check('cut-off download: what did arrive is kept for the next try',
              pg.evaluate(f"caches.open('armdos-app-{build_b}').then(c => c.keys()).then(k => k.length)") > 50)
        ctx.set_offline(True)
        pg.reload()
        check('cut-off download: release A still launches offline', wait_for(pg, LOADED, 30) and pg.evaluate(RUNNING) == 'A')
        ctx.set_offline(False)

        # ============ 2. the site half way through an upload: sw.js is B's, util.js still A's
        Handler.fail = {}
        open(util, 'wb').write(util_a)
        warnings.clear(); Handler.seen.clear()
        pg.goto(url)
        check('half-uploaded site: the old file fails its hash check, still release A',
              wait_for(pg, LOADED, 40) and any('did not arrive complete' in w for w in warnings) and pg.evaluate(RUNNING) == 'A')
        fetched = [s for s in Handler.seen if s not in ('sw.js', 'images.json', 'disks.json') and not s.startswith('images/')]
        check('the second try fetches only what was missing', set(fetched) == {'js/util.js'}, fetched)

        # ============ 3. B complete: the next load brings it in
        open(util, 'wb').write(util_b)
        navs.clear()
        pg.goto(url)
        check('complete release: the page reloads itself into B', wait_for(pg, f"{LOADED} && {RUNNING}.then(r => r === 'B')", 40), pg.evaluate(RUNNING))
        check('complete release: one reload', len(navs) == 2, navs)
        check('complete release: A\'s cache deleted', wait_for(pg, f"caches.keys().then(k => k.includes('armdos-app-{build_b}') && !k.includes('armdos-app-{build_a}'))", 10),
              pg.evaluate(CACHES))

        # ============ 4. release C arrives while the machine is running: it waits
        pg.click('#powerBtn')
        check('B boots', wait_for(pg, "!!armdos.driver?.running", 30))
        build_c, util_c = release('C')
        open(util, 'wb').write(util_c)
        navs.clear()
        pg.evaluate("navigator.serviceWorker.getRegistration().then(r => r.update())")     # the browser's own check
        check('while running: C installs and waits', wait_for(pg, "navigator.serviceWorker.getRegistration().then(r => !!r.waiting)", 30))
        time.sleep(1)
        st = pg.evaluate("[armdos.driver.running, navigator.serviceWorker.controller?.scriptURL, typeof armdos.machine]")
        check('while running: the page stays on B, no reload, the machine still running',
              pg.evaluate(RUNNING) == 'B' and not navs and st[0], [pg.evaluate(RUNNING), navs, st])
        pg.goto(url)
        check('the next load brings in C', wait_for(pg, f"{LOADED} && {RUNNING}.then(r => r === 'C')", 40), pg.evaluate(RUNNING))

        # ============ 5. two windows: release D waits until the other one is closed
        other = ctx.new_page()
        other.goto(url)
        wait_for(other, LOADED, 30)
        build_d, util_d = release('D')
        open(util, 'wb').write(util_d)
        warnings.clear()
        pg.goto(url)
        check('two windows: D installs and waits', wait_for(pg, "navigator.serviceWorker.getRegistration().then(r => !!r.waiting)", 40) and wait_for(pg, LOADED, 20),
              pg.evaluate("navigator.serviceWorker.getRegistration().then(r => [!!r.installing, !!r.waiting, !!r.active, caches.keys()])") and pg.evaluate(CACHES))
        time.sleep(1)
        check('two windows: both stay on C', pg.evaluate(RUNNING) == 'C' and other.evaluate(RUNNING) == 'C', [pg.evaluate(RUNNING), other.evaluate(RUNNING), warnings[-3:]])
        other.close()
        pg.goto(url)
        check('the other window closed: the next load brings in D', wait_for(pg, f"{LOADED} && {RUNNING}.then(r => r === 'D')", 40), pg.evaluate(RUNNING))

        # ============ 6. over a worker from before this scheme (its cache "armdos-shell-*"), which never
        # asks a new release to take over: the new one takes over as soon as it is installed, as before
        pg.evaluate("caches.open('armdos-shell-0123456789ab')")
        build_e, util_e = release('E')
        open(util, 'wb').write(util_e)
        pg.evaluate("navigator.serviceWorker.getRegistration().then(r => r.update())")
        check('over an old-style worker: E takes over at once, the old cache goes',
              wait_for(pg, f"navigator.serviceWorker.getRegistration().then(r => !r.waiting && r.active && caches.keys()).then(k => k && k.includes('armdos-app-{build_e}') && !k.includes('armdos-shell-0123456789ab'))", 30),
              pg.evaluate(CACHES))
        ctx.close()
        b.close()
        check('no page errors', not errors, '; '.join(errors[:3]))
    finally:
        srv.shutdown()
        shutil.rmtree(tmp, ignore_errors=True)

def main():
    with sync_playwright() as p:
        for name in sys.argv[1:] or ['chromium', 'firefox']:
            run(p, name)
    print('all passed' if not fails else f'{fails} failed')
    sys.exit(1 if fails else 0)

main()
