#!/usr/bin/env python3
"""Updates through the service worker (web/sw.js, web/js/main.js swStart) with release
directories (r/<release>/, sw-<release>.js; web/tools/build-site.mjs):

 1. a new release's page runs at once (the page itself is network-first); if the download
    for offline use is cut off part way, the old release stays installed and still launches
    offline;
 2. a site half way through an upload (a file still the old one) fails the install the same
    way (every shell file is hash-checked), and the next try fetches only what was missing;
 3. once it is all there, the new release takes over without a reload, and the old release's
    cache goes, as do disk images only the old release named; it then launches offline;
 4. nothing changes under a running machine: a release published meanwhile arrives with the
    next page load;
 5. with a second ARM-DOS window open on an older release, the new one waits for it to close;
 6. over a worker from before release directories, the new one takes over as soon as it is
    installed;
 7. nothing the page loads has a query string (a CDN may refuse to cache such URLs);
 8. the page shows its release id at the bottom.

usage: python3 web/tests/test_sw_update.py [chromium|firefox ...]   (after ./build.sh; default both)
"""
import hashlib, json, os, re, shutil, socket, sys, tempfile, threading, time
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
    down = False        # the site is unreachable (every connection drops): "offline", for any browser
    fail = {}           # path under the site -> 'cut': the service worker's download of it (cache: 'reload') drops half way
    seen = []           # (path, fetched by the service worker's install)
    def do_GET(self):
        rel = self.path.split('?')[0].split('#')[0][len(self.prefix):]
        installing = 'no-cache' in (self.headers.get('Pragma') or '') + (self.headers.get('Cache-Control') or '')
        Handler.seen.append((rel, installing))
        if Handler.down:
            self.close_connection = True
            try: self.connection.shutdown(socket.SHUT_RDWR)
            except OSError: pass
            return
        if installing and Handler.fail.get(rel) == 'cut':
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
    idx = os.path.join(site, 'index.html')
    cur = lambda: re.search(r'"r/([0-9a-f]+)/', open(idx).read()).group(1)
    first = cur()
    util_a = open(os.path.join(site, 'r', first, 'js', 'util.js'), 'rb').read()
    sha = lambda b: hashlib.sha256(b).hexdigest()[:16]

    def release(tag, util=None, keep_old=False, new_rom=False):
        """Publish a new release the way build-site.mjs does: a new r/<id>/ (util.js with a marker),
        index.html pointing at it, sw-<id>.js (and sw.js) listing it. The old directory and worker
        go, as an rsync --delete would take them, unless keep_old."""
        old, new = cur(), (tag.lower() * 12)[:12]
        shutil.copytree(os.path.join(site, 'r', old), os.path.join(site, 'r', new))
        body = util_a + f"\nexport const RELEASE = '{tag}';\n".encode()
        open(os.path.join(site, 'r', new, 'js', 'util.js'), 'wb').write(util if util is not None else body)
        if new_rom:          # new data too: the ROM under a new name (same bytes), in this release's images.json
            ij = os.path.join(site, 'r', new, 'images.json')
            img = json.load(open(ij)); rom = img['rom']['file']
            img['rom']['file'] = f'images/rom.bin.{tag}.gz'
            shutil.copy(os.path.join(site, rom), os.path.join(site, img['rom']['file']))
            open(ij, 'w').write(json.dumps(img))
        html = open(idx).read().replace(old, new)
        open(idx, 'w').write(html)
        sw = open(os.path.join(site, f'sw-{old}.js')).read().replace(old, new)
        sw = re.sub(r'\["r/%s/js/util\.js","[0-9a-f]+"\]' % new, f'["r/{new}/js/util.js","{sha(body)}"]', sw)
        sw = re.sub(r'\["index\.html","[0-9a-f]+"\]', f'["index.html","{sha(html.encode())}"]', sw)
        sw = re.sub(r'\["r/%s/images\.json","[0-9a-f]+"\]' % new, lambda m: f'["r/{new}/images.json","{sha(open(os.path.join(site, "r", new, "images.json"), "rb").read())}"]', sw)
        open(os.path.join(site, f'sw-{new}.js'), 'w').write(sw)
        open(os.path.join(site, 'sw.js'), 'w').write(sw)
        if not keep_old: shutil.rmtree(os.path.join(site, 'r', old)); os.remove(os.path.join(site, f'sw-{old}.js'))
        return new, body
    def fix_util(rid, body): open(os.path.join(site, 'r', rid, 'js', 'util.js'), 'wb').write(body)

    RUNNING = "(document.querySelector('script[type=module]').src.match(/r\\/(\\w+)\\//) || [])[1]"
    ACTIVE = "(navigator.serviceWorker.controller?.scriptURL.match(/sw-(\\w+)\\.js/) || [])[1]"
    LOADED = "!!(window.armdos && armdos.rom)"
    CACHES = "caches.keys()"
    errors, warnings, queried = [], [], set()

    try:
        b = getattr(p, name).launch()
        print(f'--- {name}', flush=True)
        ctx = b.new_context(viewport={'width': 1300, 'height': 1000})
        ctx.on('request', lambda r: queried.add(r.url) if '?' in r.url and not r.url.endswith(('?app', '?nosw')) else None)
        pg = ctx.new_page()
        pg.on('pageerror', lambda e: errors.append(str(e)))
        pg.on('console', lambda m: warnings.append(m.text) if m.type == 'warning' else None)
        navs = []
        pg.on('framenavigated', lambda f: navs.append(f.url) if f == pg.main_frame else None)

        # ============ release A installed and in control
        pg.goto(url)
        wait_for(pg, "navigator.serviceWorker.ready.then(() => true)", 20)
        pg.reload()
        check('A: installed and in control', wait_for(pg, f"{ACTIVE} === '{first}'", 10) and wait_for(pg, f"caches.has('armdos-app-{first}')", 10), pg.evaluate(CACHES))

        # ============ 1. release B, its download for offline use cut off part way
        rom_a = json.load(open(os.path.join(site, 'r', first, 'images.json')))['rom']['file']
        rb, util_b = release('B', new_rom=True)
        Handler.fail = {f'r/{rb}/js/util.js': 'cut'}
        warnings.clear(); navs.clear()
        pg.goto(url)
        check('B published: the page runs B at once', wait_for(pg, f"{LOADED} && {RUNNING} === '{rb}'", 30), pg.evaluate(RUNNING))
        check('cut-off download: the install fails, A stays installed with its cache',
              wait_for(pg, "navigator.serviceWorker.getRegistration().then(r => !r.installing && !r.waiting)", 30) and any('did not download completely' in w for w in warnings)
              and pg.evaluate(ACTIVE) == first and pg.evaluate(f"caches.has('armdos-app-{first}')"), [pg.evaluate(ACTIVE), warnings[-2:]])
        check('cut-off download: what did arrive is kept for the next try', pg.evaluate(f"caches.open('armdos-app-{rb}').then(c => c.keys()).then(k => k.length)") > 50)
        Handler.down = True
        pg.reload()
        check('offline: release A still launches', wait_for(pg, f"{LOADED} && {RUNNING} === '{first}'", 30), pg.evaluate(RUNNING))
        Handler.down = False

        # ============ 2. the site half way through an upload: B's util.js is still A's
        Handler.fail = {}
        fix_util(rb, util_a)
        warnings.clear(); Handler.seen.clear()
        pg.goto(url)
        check('half-uploaded site: the old file fails its hash check, A stays installed',
              wait_for(pg, LOADED, 30) and wait_for(pg, "navigator.serviceWorker.getRegistration().then(r => !r.installing && !r.waiting)", 30)
              and any('did not download completely' in w for w in warnings) and pg.evaluate(ACTIVE) == first)
        fetched = sorted({r for r, inst in Handler.seen if inst and r.startswith('r/')})      # (the manifests are read network-first anyway)
        check('the second try fetches only what was missing', fetched == [f'r/{rb}/js/util.js'], fetched)

        # ============ 3. B complete: it takes over, no reload
        fix_util(rb, util_b)
        navs.clear()
        pg.goto(url)
        check('complete: B takes over without a reload', wait_for(pg, f"{ACTIVE} === '{rb}'", 15) and len(navs) == 1 and pg.evaluate(RUNNING) == rb, [pg.evaluate(ACTIVE), navs])
        check("complete: the ROM B's images.json no longer names is dropped from the cache, B's is there",
              wait_for(pg, f"caches.open('armdos-data').then(c => c.keys()).then(k => k.map(r => r.url)).then(u => u.some(x => x.endsWith('rom.bin.B.gz')) && !u.some(x => x.endsWith('{rom_a}')))", 15),
              pg.evaluate("caches.open('armdos-data').then(c => c.keys()).then(k => k.map(r => r.url.split('/').pop()))"))
        check("complete: A's cache deleted", wait_for(pg, f"caches.keys().then(k => k.includes('armdos-app-{rb}') && !k.includes('armdos-app-{first}'))", 10), pg.evaluate(CACHES))
        Handler.down = True
        pg.reload()
        check('offline: B launches', wait_for(pg, f"{LOADED} && {RUNNING} === '{rb}'", 30))
        Handler.down = False
        pg.reload(); wait_for(pg, LOADED, 30)

        # ============ 4. release C published while the machine runs
        pg.click('#powerBtn')
        check('B boots', wait_for(pg, "!!armdos.driver?.running", 30))
        rc, _ = release('C')
        navs.clear()
        pg.evaluate("navigator.serviceWorker.getRegistration().then(r => r.update()).catch(() => {})")     # the browser's own check (a 404: sw-B.js is gone)
        time.sleep(2)
        check('while running: nothing changes under the machine', pg.evaluate(RUNNING) == rb and pg.evaluate(ACTIVE) == rb and not navs
              and pg.evaluate("armdos.driver.running"), [pg.evaluate(ACTIVE), navs])
        pg.goto(url)
        check('the next load runs C and installs it', wait_for(pg, f"{LOADED} && {RUNNING} === '{rc}'", 30) and wait_for(pg, f"{ACTIVE} === '{rc}'", 30), pg.evaluate(ACTIVE))

        # ============ 5. a second window on C; release D waits for it
        other = ctx.new_page()
        other.goto(url)
        wait_for(other, LOADED, 30)
        rd, _ = release('D', keep_old=True)
        warnings.clear()
        pg.goto(url)
        check('two windows: D runs here and waits to take over', wait_for(pg, f"{LOADED} && {RUNNING} === '{rd}'", 30)
              and wait_for(pg, "navigator.serviceWorker.getRegistration().then(r => !!r.waiting)", 30) and pg.evaluate(ACTIVE) == rc,
              pg.evaluate("navigator.serviceWorker.getRegistration().then(r => [!!r.installing, !!r.waiting])"))
        check('two windows: the other one still runs C, from its cache', other.evaluate(RUNNING) == rc and other.evaluate(f"caches.has('armdos-app-{rc}')"))
        other.close()
        pg.goto(url)
        check('the other window closed: D takes over', wait_for(pg, f"{ACTIVE} === '{rd}'", 30), pg.evaluate(ACTIVE))

        # ============ 6. over a worker from before release directories ("armdos-shell-*")
        pg.evaluate("caches.open('armdos-shell-0123456789ab')")
        re_, _ = release('E')
        pg.goto(url)
        check('over an old-style worker: E takes over by itself, the old cache goes',
              wait_for(pg, f"{ACTIVE} === '{re_}'", 30) and wait_for(pg, "caches.keys().then(k => !k.includes('armdos-shell-0123456789ab'))", 10), pg.evaluate(CACHES))

        check('the page shows its release at the bottom', pg.evaluate(f"document.getElementById('releaseId').textContent === {RUNNING}"), pg.evaluate("document.getElementById('releaseId').textContent"))
        check('nothing the page loaded had a query string', not queried, sorted(queried)[:4])
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
