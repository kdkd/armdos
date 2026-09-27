#!/usr/bin/env python3
"""Reliability of drive C: in the browser (web/js/storage.js, web/js/hdstream.js):

 1. a save that fails (the IndexedDB transaction aborts) keeps its sectors pending, shows
    "NOT SAVING" on the disk bay and a warning under the disk box, retries, and once the
    browser takes writes again saves them - the file is there after a reload;
 2. a streamed chunk is installed only if it is exactly the chunk the build made (its
    length and the content hash it is named by): an HTML error page served as 200 OK or
    a truncated chunk is a failed read, and the disk image is left alone.

usage: python3 web/tests/test_persist.py   (after ./build.sh; Chromium)
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
# abort every write to the sector store while window.__failSaves is set
FAIL_SAVES = """
(() => {
  const orig = IDBDatabase.prototype.transaction;
  IDBDatabase.prototype.transaction = function (names, mode, ...rest) {
    const tx = orig.call(this, names, mode, ...rest);
    if (window.__failSaves && mode === 'readwrite' && String(names).includes('hdsectors')) setTimeout(() => { try { tx.abort(); } catch {} }, 0);
    return tx;
  };
})();
"""

def main():
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            ctx = b.new_context(viewport={'width': 1300, 'height': 1000})
            ctx.add_init_script(FAIL_SAVES)
            pg = ctx.new_page(); errors = []; pg.on('pageerror', lambda e: errors.append(str(e)))
            pg.goto(url + '?nosw'); pg.wait_for_selector('#powerBtn')
            pg.evaluate("localStorage.clear(); indexedDB.deleteDatabase('armdos')"); pg.reload(); pg.wait_for_selector('#powerBtn')
            pg.click('#powerBtn')
            check('boots to C:\\>', wait_for(pg, PROMPT, 90))
            check('no warning while saving works', not pg.evaluate("document.getElementById('hdBay').classList.contains('not-saving')"))

            # ---- 1. the browser refuses the writes
            pg.evaluate("window.__failSaves = true")
            pg.evaluate("armdos.machine.typeText('ECHO kept through a failed save>PERSIST.TXT\\r')")
            check('a failed save: the page says so (disk bay + status line)',
                  wait_for(pg, "armdos.store.error && document.getElementById('hdBay').classList.contains('not-saving') && document.getElementById('hdStatus').classList.contains('warn')", 20),
                  pg.evaluate("armdos.store.error + ' / ' + document.getElementById('hdStatus').textContent"))
            check('...and keeps the written sectors pending', pg.evaluate("armdos.store.dirty.size") > 0, pg.evaluate("armdos.store.dirty.size"))
            f0 = pg.evaluate("armdos.store.failures"); time.sleep(4)
            check('...and keeps trying', pg.evaluate("armdos.store.failures") > f0, f"{f0} -> {pg.evaluate('armdos.store.failures')}")
            pg.evaluate("window.__failSaves = false")
            check('writes allowed again: the retry saves them and the warning goes',
                  wait_for(pg, "!armdos.store.error && armdos.store.dirty.size === 0 && armdos.store.saved > 0 && !document.getElementById('hdBay').classList.contains('not-saving')", 90),
                  pg.evaluate("[armdos.store.error, armdos.store.dirty.size, armdos.store.saved].join(' ')"))
            pg.reload(); pg.wait_for_selector('#powerBtn'); pg.click('#powerBtn')
            wait_for(pg, PROMPT, 90)
            pg.evaluate("armdos.machine.typeText('CLS\\rTYPE PERSIST.TXT\\r')")
            check('after a reload the file is there', wait_for(pg, f"{SCREEN}.includes('kept through a failed save')", 20), pg.evaluate(SCREEN)[-200:])

            # ---- 2. what a chunk must be before it becomes disk contents
            r = pg.evaluate("""async () => {
              const { StreamedDisk } = await import(new URL('../js/hdstream.js', document.querySelector('script[type=module]').src).href);
              const info = (await (await fetch('images.json', { cache: 'no-cache' })).json()).hd;
              const k = info.chunks.findIndex((h) => h);            // a stored chunk
              const out = { k };
              const try1 = async (label, body) => {
                const d = new StreamedDisk(info);
                const realFetch = window.fetch;
                window.fetch = (u, o) => (String(u).includes('/c/') && body !== null) ? Promise.resolve(new Response(body, { status: 200 })) : realFetch(u, o);
                try { out[label] = await d.load(k); } finally { window.fetch = realFetch; }
                out[label + 'Zero'] = d.img.subarray(k * info.chunkSize, (k + 1) * info.chunkSize).every((b) => b === 0);
              };
              await try1('html', '<!doctype html><html><body>Not Found (but 200 OK)</body></html>');
              const good = new Uint8Array(await (await fetch(info.chunkBase + info.chunks[k] + '.gz')).arrayBuffer());
              const { maybeGunzip } = await import(new URL('../js/util.js', document.querySelector('script[type=module]').src).href);
              const raw = await maybeGunzip(good);
              await try1('short', raw.slice(0, raw.length - 512));
              const bad = raw.slice(); bad[100] ^= 0xFF;
              await try1('flipped', bad);
              await try1('good', null);
              out.secure = !!(crypto && crypto.subtle);
              return out;
            }""")
            check('an HTML page served as 200 OK is a failed read, the disk untouched', r['html'] is False and r['htmlZero'], str(r))
            check('a truncated chunk is a failed read', r['short'] is False and r['shortZero'], str(r))
            check('a chunk with the right length but wrong contents is a failed read (hash)', (r['flipped'] is False and r['flippedZero']) or not r['secure'], str(r))
            check('the real chunk is installed', r['good'] is True and not r['goodZero'], str(r))
            check('no page errors', not errors, '; '.join(errors[:2]))
            b.close()
    finally:
        srv.shutdown()
    print(f'\npersist (web): {"all passed" if not fails else str(fails) + " failed"}')
    sys.exit(1 if fails else 0)

main()
