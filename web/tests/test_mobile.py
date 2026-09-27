#!/usr/bin/env python3
"""Phones, tablets, the home-screen app and the offline cache.

  WebKit with the iPhone 13 descriptor, Chromium with Pixel 5: the Keyboard button focuses the
  text field inside the tap; insertText / deleteContentBackward / line breaks reach DOS; the
  on-screen PC keys (Ctrl latch + C, held arrows with typematic repeat, multi-touch on the pad);
  the app layout in both orientations; the service worker (offline launch, release update).

usage: python3 web/tests/test_mobile.py [--shots DIR]
"""
import os, sys, time, shutil, tempfile, json, re
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
def wait_for(page, js, timeout=15):
    t = time.time()
    while time.time() - t < timeout:
        try:
            if page.evaluate(js): return True
        except Exception: pass
        time.sleep(0.1)
    return False
SCREEN = "(() => { const m = armdos.machine; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m.cpu.m8[0xB8000 + (y*80+x)*2] || 32); r.push(s.trimEnd()); } return r.join('\\n'); })()"
PROMPT = f"{SCREEN}.includes('C:\\\\>')"
INPUT = """((type, data) => { const t = document.getElementById('softKbd');
  const e = new InputEvent('beforeinput', { inputType: type, data, cancelable: true, bubbles: true });
  if (t.dispatchEvent(e)) t.dispatchEvent(new InputEvent('input', { inputType: type, data, bubbles: true })); })"""
# count make/break codes the page sends
SPY = """(() => { const m = armdos.machine; window.__keys = [];
  const d = m.keyDown.bind(m), u = m.keyUp.bind(m);
  m.keyDown = (c) => { __keys.push('+' + c); return d(c); }; m.keyUp = (c) => { __keys.push('-' + c); return u(c); }; })()"""

def safe_eval(page, js):
    try: return page.evaluate(js)
    except Exception as e: return str(e).split('\n')[0]

def boot(page, url, power='#powerBtn'):
    page.goto(url)
    page.wait_for_selector(power)
    page.tap(power)
    return wait_for(page, PROMPT, 40)

def soft_keyboard_tests(page, tag):
    page.tap('#softKbdBtn')
    check(f'{tag}: Keyboard button focuses the text field in the tap', page.evaluate("document.activeElement && document.activeElement.id === 'softKbd'"))
    f = page.evaluate("(() => { const t = document.getElementById('softKbd'), s = getComputedStyle(t), r = t.getBoundingClientRect(); return { fs: parseFloat(s.fontSize), disp: s.display, vis: s.visibility, w: r.width, top: r.top, left: r.left, ro: t.readOnly, dis: t.disabled, ac: t.getAttribute('autocapitalize'), ekh: t.getAttribute('enterkeyhint') }; })()")
    check(f'{tag}: text field is focusable and on screen (16px, visible, enabled)', f['fs'] >= 16 and f['disp'] != 'none' and f['vis'] == 'visible' and f['w'] > 0 and not f['ro'] and not f['dis'] and f['ac'] == 'off' and f['ekh'] == 'enter', f)
    for ch in 'dir': page.evaluate(INPUT + "('insertText', " + json.dumps(ch) + ")")
    page.evaluate(INPUT + "('insertLineBreak', null)")
    check(f'{tag}: insertText + line break run DIR', wait_for(page, f"{SCREEN}.includes('bytes free')", 20), page.evaluate(SCREEN)[-300:])
    page.evaluate(INPUT + "('insertText', 'verx')")
    page.evaluate(INPUT + "('deleteContentBackward', null)")
    page.evaluate(INPUT + "('insertLineBreak', null)")
    wait_for(page, f"{SCREEN}.includes('Version 4.00')", 15); time.sleep(0.5)
    scr = page.evaluate(SCREEN)
    check(f'{tag}: deleteContentBackward erases (VERX -> VER)', re.search(r'C:\\>ver\n', scr, re.I) is not None and 'Bad command' not in scr, scr[-300:])
    # the fallback path: a non-cancelable edit (composition) seen only as a changed value
    page.evaluate("(() => { const t = document.getElementById('softKbd'); t.value = t.value + 'cls'; t.dispatchEvent(new InputEvent('input', { inputType: 'insertCompositionText', data: 'cls', bubbles: true })); })()")
    page.evaluate(INPUT + "('insertLineBreak', null)")
    check(f'{tag}: value-diff fallback types too (CLS clears)', wait_for(page, f"!{SCREEN}.includes('bytes free')", 15))
    check(f'{tag}: sentinel keeps the field non-empty', page.evaluate("document.getElementById('softKbd').value.length > 0"))

def main():
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            errors = []
            # ============ iPhone (WebKit)
            iphone = p.devices['iPhone 13']
            b = p.webkit.launch()
            ctx = b.new_context(**iphone)
            pg = ctx.new_page(); pg.on('pageerror', lambda e: errors.append('webkit: ' + str(e)))
            check('iPhone: boots to C:\\>', boot(pg, url + '?nosw'))
            check('iPhone: Keyboard button visible', pg.is_visible('#softKbdBtn'))
            soft_keyboard_tests(pg, 'iPhone')
            # on-screen PC keys
            pg.tap('#pcKeysBtn')
            check('iPhone: PC keys shown', pg.is_visible('#pcKeys .k[data-code="KeyQ"]'))
            time.sleep(1)
            box, mon = pg.evaluate("[document.getElementById('pcKeys'), document.getElementById('monitor')].map(e => { const r = e.getBoundingClientRect(); return { y: r.top, height: r.height }; })")
            check('iPhone portrait: keyboard right under the monitor', box['y'] >= mon['y'] + mon['height'] - 2 and box['y'] - (mon['y'] + mon['height']) < 40, (mon, box))
            pg.evaluate(SPY)
            pg.tap('#pcKeys .k[data-code="ControlLeft"]')
            check('iPhone: Ctrl latches (lit, still down)', pg.evaluate("document.querySelector('#pcKeys .k[data-code=\"ControlLeft\"]').classList.contains('latched') && !__keys.includes('-ControlLeft')"))
            pg.tap('#pcKeys .k[data-code="KeyC"]')
            check('iPhone: Ctrl+C sent, then Ctrl released', wait_for(pg, "__keys.join(',') === '+ControlLeft,+KeyC,-KeyC,-ControlLeft'", 3), pg.evaluate("__keys"))
            check('iPhone: DOS shows ^C', wait_for(pg, f"{SCREEN}.includes('^C')", 10))
            shot(pg, 'iphone-keys')
            # hold an arrow: typematic repeat, then a single break
            pg.evaluate("__keys = []")
            a = pg.locator('#pcKeys .k[data-code="ArrowUp"]').bounding_box()
            pg.evaluate("""(() => { const k = document.querySelector('#pcKeys .k[data-code="ArrowUp"]');
                k.dispatchEvent(new PointerEvent('pointerdown', { pointerId: 11, bubbles: true, pointerType: 'touch', isPrimary: true })); })()""")
            time.sleep(1.0)
            pg.evaluate("""(() => { const k = document.querySelector('#pcKeys .k[data-code="ArrowUp"]');
                k.dispatchEvent(new PointerEvent('pointerup', { pointerId: 11, bubbles: true, pointerType: 'touch' })); })()""")
            ks = pg.evaluate("__keys")
            check('iPhone: held arrow repeats, one break at release', ks.count('+ArrowUp') >= 4 and ks.count('-ArrowUp') == 1 and ks[-1] == '-ArrowUp', ks[:4] + ['...'] + ks[-2:])
            ctx.close()

            # ============ the app layout (iPhone, both orientations), the game pad, multi-touch
            for land in (False, True):
                d = dict(iphone)
                if land: d['viewport'] = {'width': 844, 'height': 390}
                ctx = b.new_context(**d)
                pg = ctx.new_page(); pg.on('pageerror', lambda e: errors.append('webkit app: ' + str(e)))
                tag = 'app ' + ('landscape' if land else 'portrait')
                check(f'{tag}: boots from the bar', boot(pg, url + '?app&nosw', '#abPower'))
                check(f'{tag}: app layout on, page prose hidden', pg.evaluate("document.documentElement.classList.contains('app') && getComputedStyle(document.querySelector('.about')).display === 'none' && getComputedStyle(document.querySelector('.case')).display === 'none'"))
                vw, vh = d['viewport']['width'], d['viewport']['height']
                t = pg.locator('#tube').bounding_box()
                check(f'{tag}: the screen is big', (t['width'] >= vw * 0.9) if not land else (t['height'] >= vh * 0.65), t)
                check(f'{tag}: no page scrolling', pg.evaluate("document.documentElement.scrollHeight <= innerHeight + 1 && document.documentElement.scrollWidth <= innerWidth + 1"))
                shot(pg, f'app-{"landscape" if land else "portrait"}-keys')
                pg.tap('#abPad')
                check(f'{tag}: game pad shown', pg.is_visible('#pcKeys .dpad'))
                pg.evaluate(SPY)
                # multi-touch: hold right, tap FIRE (Ctrl), release right
                pg.evaluate("""(() => { const q = (c) => document.querySelector('#pcKeys .k[data-code="' + c + '"]');
                    const ev = (el, type, id) => el.dispatchEvent(new PointerEvent(type, { pointerId: id, bubbles: true, pointerType: 'touch' }));
                    ev(q('ArrowRight'), 'pointerdown', 21); ev(q('ControlLeft'), 'pointerdown', 22); ev(q('ControlLeft'), 'pointerup', 22); ev(q('ArrowRight'), 'pointerup', 21); })()""")
                ks = pg.evaluate("__keys")
                check(f'{tag}: pad multi-touch (hold arrow, tap fire): real press/release, no latching', ks == ['+ArrowRight', '+ControlLeft', '-ControlLeft', '-ArrowRight'], ks)
                shot(pg, f'app-{"landscape" if land else "portrait"}-pad')
                pg.tap('#abDisk')
                check(f'{tag}: disk menu opens', pg.is_visible('#diskMenu'))
                pg.locator('#diskMenu button').first.tap()
                check(f'{tag}: disk menu inserts a diskette', wait_for(pg, "armdos.machine.floppy !== null", 5))
                ctx.close()
            b.close()

            # ============ Android (Chromium, Pixel 5)
            b = p.chromium.launch(args=['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
            ctx = b.new_context(**p.devices['Pixel 5'])
            pg = ctx.new_page(); pg.on('pageerror', lambda e: errors.append('pixel: ' + str(e)))
            check('Pixel: boots to C:\\>', boot(pg, url + '?nosw'))
            soft_keyboard_tests(pg, 'Pixel')
            ctx.close()

            # ============ manifest + icons
            ctx = b.new_context()
            pg = ctx.new_page()
            pg.goto(url + '?nosw')
            man = pg.evaluate("fetch(document.querySelector('link[rel=manifest]').href).then(r => r.json())")
            check('manifest: names, display, icons', man['short_name'] == 'ARM-DOS' and man['display'] == 'standalone' and {'192x192', '512x512'} <= {i['sizes'] for i in man['icons']} and any(i.get('purpose') == 'maskable' for i in man['icons']))
            sizes = pg.evaluate("""Promise.all(['apple-touch-icon.png','icon-192.png','icon-512.png','icon-maskable-512.png'].map((n) => document.querySelector('link[rel=manifest]').href.replace(/manifest\.webmanifest$/, '') + document.querySelector('link[rel=icon]').getAttribute('href').replace(/[^/]+$/, n)).map(u => new Promise(r => { const i = new Image(); i.onload = () => r(i.naturalWidth); i.onerror = () => r(0); i.src = u; })))""")
            check('icons decode at 180/192/512/512', sizes == [180, 192, 512, 512], sizes)
            check('iOS web-app meta tags', pg.evaluate("!!document.querySelector('meta[name=apple-mobile-web-app-capable][content=yes]') && document.querySelector('meta[name=apple-mobile-web-app-status-bar-style]').content === 'black-translucent'"))
            ctx.close()
            b.close()

            # ============ the service worker: offline launch and a new release
            tmp = tempfile.mkdtemp(prefix='armdos-sw-')
            site = os.path.join(tmp, 'site')
            shutil.copytree(serve.SITE, site)
            srv2, url2 = serve.start(site)
            b = p.chromium.launch(args=['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
            ctx = b.new_context(has_touch=True)
            pg = ctx.new_page(); pg.on('pageerror', lambda e: errors.append('sw: ' + str(e)))
            pg.goto(url2)
            check('sw: registers and controls the page', wait_for(pg, "navigator.serviceWorker.ready.then(() => true)", 20) and (pg.reload() or True) and wait_for(pg, "!!navigator.serviceWorker.controller", 10))
            check('sw: first launch boots', boot(pg, url2))
            hd = json.load(open(os.path.join(site, 'images.json')))['rom']['file']
            check('sw: disk image cached', wait_for(pg, f"caches.open('armdos-data').then(c => c.match(new URL({json.dumps(hd)}, location.href).href)).then(r => !!r)", 15))
            ctx.set_offline(True)
            check('sw: launches and boots offline', boot(pg, url2))
            ctx.set_offline(False)
            # new data: the ROM under a new name (same bytes); a new app release is test_sw_update.py's job
            img = json.load(open(os.path.join(site, 'images.json')))
            old_rom = img['rom']['file']
            img['rom']['file'] = 'images/rom.bin.newrelease.gz'
            shutil.copy(os.path.join(site, old_rom), os.path.join(site, img['rom']['file']))
            json.dump(img, open(os.path.join(site, 'images.json'), 'w'))
            pg.goto(url2)
            check('sw: images.json is never stale (network-first)', pg.evaluate("fetch('images.json', {cache: 'no-cache'}).then(r => r.json()).then(j => j.rom.file.endsWith('newrelease.gz'))"))
            check('sw: boots the new release', boot(pg, url2))
            check('sw: old disk image dropped from the cache, new one cached',
                  wait_for(pg, f"caches.open('armdos-data').then(c => c.keys()).then(ks => {{ const u = ks.map(r => r.url); return u.some(x => x.endsWith('newrelease.gz')) && !u.includes(new URL({json.dumps(hd)}, location.href).href); }})", 20),
                  pg.evaluate("caches.open('armdos-data').then(c => c.keys()).then(ks => ks.map(r => r.url.split('/').pop()))"))
            ctx.close(); b.close()
            srv2.shutdown()
            shutil.rmtree(tmp, ignore_errors=True)
            check('no page errors', not errors, '; '.join(errors[:3]))
    finally:
        srv.shutdown()
    print('all passed' if not fails else f'{fails} failed')
    sys.exit(1 if fails else 0)

main()
