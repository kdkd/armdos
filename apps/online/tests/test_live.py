#!/usr/bin/env python3
"""Live smoke test of ARM-DOS Online on the real page and the real web services (Chromium).
Network trouble is tolerated: a check that needs the Internet is reported as SKIP when the
service answers "... is not available right now" or a fetch fails, never as FAIL.

 1. the channels in the page against the real APIs (Wikipedia search/article/on this day/random,
    Wiktionary, Open-Meteo, Hacker News) and the picture pipeline with the browser's decoder
    (createImageBitmap + canvas -> median cut -> interlaced GIF)
 2. end to end at 56K: power on, ONLINE, sign on, search, read an article, watch a picture
    arrive in mode 13h, sign off (NO CARRIER)

usage: python3 apps/online/tests/test_live.py [--shots DIR]   (make site first)
"""
import os, sys, time, json
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'web', 'tests'))
import serve
from playwright.sync_api import sync_playwright

shots = sys.argv[sys.argv.index('--shots') + 1] if '--shots' in sys.argv else os.path.join(ROOT, 'build', 'online-test')
fails = skips = 0
def check(name, ok, extra=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + (f'  ({extra})' if extra else ''))
    if not ok: fails += 1
def skip(name, why):
    global skips
    skips += 1
    print(f'SKIP {name}  ({why})')
def wait_for(page, js, timeout=15):
    t = time.time()
    while time.time() - t < timeout:
        if page.evaluate(js): return True
        time.sleep(0.2)
    return False
def shot(page, name):
    os.makedirs(shots, exist_ok=True)
    p = os.path.join(shots, name + '.png')
    page.locator('#screen').screenshot(path=p)
    print('     screenshot', p)

ARGS = ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']
SCREEN = "(() => { const m = armdos.machine; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m.cpu.m8[0xB8000 + (y*80+x)*2] || 32); r.push(s.trimEnd()); } return r.join('\\n'); })()"
def has(page, text): return page.evaluate(f"{SCREEN}.includes({json.dumps(text)})")
def wait_text(page, text, timeout): return wait_for(page, f"{SCREEN}.includes({json.dumps(text)})", timeout)
def typ(page, s): page.evaluate(f"armdos.machine.typeText({json.dumps(s)})")

CHANNELS = """async () => {
  const ch = await import('./emu/online/channels.js');
  const { Fetcher } = await import('./emu/online/net.js');
  const { makeGif, decodeGif } = await import('./emu/online/picture.js');
  const net = new Fetcher();
  const out = {};
  const run = async (k, f) => { try { out[k] = await f(); } catch (e) { out[k] = { error: String(e.message || e) }; } };
  await run('search', async () => { const d = await ch.search(net, 'ARM architecture'); return { lines: d.plain.length, text: d.plain.join('\\n').slice(0, 400) }; });
  let pic = null;
  await run('article', async () => { const d = await ch.article(net, 'ARM architecture family');
    pic = d.links.find((l) => l && l.kind === 'P' && /\\.jpe?g/i.test(l.src)) || d.links.find((l) => l && l.kind === 'P');
    return { lines: d.plain.length, bytes: d.bytes, pictures: d.links.filter((l) => l && l.kind === 'P').length, text: d.plain.slice(0, 12).join('\\n') }; });
  await run('picture', async () => { if (!pic) throw new Error('no picture link');
    const bytes = await net.get(pic.src, 'bytes'); const g = await makeGif(bytes); const back = decodeGif(g.gif);
    return { src: pic.src, w: g.width, h: g.height, colours: g.colours, size: g.gif.length, interlaced: back.interlaced, ok: back.width === g.width }; });
  await run('dictionary', async () => (await ch.define(net, 'modem')).plain.join('\\n').slice(0, 300));
  await run('weather', async () => (await ch.weatherSearch(net, 'Berlin')).plain.join('\\n').slice(0, 400));
  await run('news', async () => (await ch.news(net, 5)).plain.join('\\n').slice(0, 400));
  await run('today', async () => (await ch.onThisDay(net, 9, 24)).plain.join('\\n').slice(0, 300));
  await run('random', async () => { const t = await ch.randomTitle(net); return (await ch.article(net, t)).title; });
  return out;
}"""

def main():
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(args=ARGS)
            ctx = b.new_context(viewport={'width': 1440, 'height': 1100})
            page = ctx.new_page()
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.goto(url + '?nosw')
            page.wait_for_selector('#modemPanel')
            check('phone list shows ARM-DOS Online 555-0199', 'ARM-DOS Online' in page.evaluate("document.querySelector('.phone-card').textContent") and '555-0199' in page.evaluate("document.querySelector('.phone-card').textContent"))
            check('the page registers the service at 555-0199', page.evaluate("armdos.line.exchange.lookup('555-0199') === armdos.line.online"))

            # ---- 1. the channels on the real APIs
            r = page.evaluate(CHANNELS)
            for k, v in r.items():
                if isinstance(v, dict) and 'error' in v:
                    skip(f'live {k}', v['error'][:120])
                    continue
                if k == 'search': check('live Encyclopedia search', v['lines'] > 10 and 'ARM' in v['text'], v['text'][:80].replace('\n', ' | '))
                elif k == 'article': check('live article: ARM architecture family', v['lines'] > 100 and v['pictures'] > 0, f"{v['lines']} lines, {v['bytes']} bytes, {v['pictures']} pictures")
                elif k == 'picture': check('live picture -> interlaced 256-colour GIF (browser decoder)', v['ok'] and v['interlaced'] and v['w'] <= 320 and v['h'] <= 184 and v['colours'] > 16, f"{v['w']}x{v['h']} {v['colours']} colours {v['size']} bytes")
                elif k == 'dictionary': check('live Dictionary: modem', 'Noun' in v or 'Verb' in v, v[:60].replace('\n', ' | '))
                elif k == 'weather': check('live Weather: Berlin', 'Berlin' in v and 'WEEK AHEAD' in v, v[:80].replace('\n', ' | '))
                elif k == 'news': check('live Technology News', ' 1. ' in v, v[:80].replace('\n', ' | '))
                elif k == 'today': check('live Today in History: September 24', 'September 24' in v, v[:60].replace('\n', ' | '))
                elif k == 'random': check('live Random Article', bool(v), v)

            # ---- 2. end to end at 56K
            page.click('.mspeed label:nth-child(4)')
            page.click('#powerBtn')
            if not wait_for(page, f"armdos.machine && armdos.powered && ({SCREEN}.includes('C:\\\\>') || {SCREEN}.includes('Shift+F9'))", 90):
                check('boots DOS', False); raise SystemExit
            if not has(page, 'C:\\>'): typ(page, '{SHIFT+F9}'); wait_text(page, 'C:\\', 30)
            typ(page, 'ONLINE\r')
            check('ONLINE: sign-on screen', wait_text(page, 'Screen Name:', 20))
            typ(page, 'Visitor\r'); time.sleep(0.5); typ(page, 'x\r')
            time.sleep(2); shot(page, 'live-dialing')
            check('56K call: welcome', wait_text(page, 'Welcome, Visitor!', 60), page.evaluate("armdos.line.online.state"))
            check('status bar: 56000 bps', has(page, '56000 bps'))
            shot(page, 'live-menu')
            typ(page, '1'); time.sleep(1); typ(page, 'Modem\r')
            ok = wait_for(page, f"{SCREEN}.includes('articles found') || {SCREEN}.includes('not available')", 40)
            if not ok or has(page, 'not available'):
                skip('live end to end', 'the Encyclopedia did not answer'); raise SystemExit
            check('live search results on the ARM-PC', True)
            shot(page, 'live-search')
            typ(page, '\t\r')
            check('live article arrives', wait_for(page, f"/Line 1 of \\d+/.test({SCREEN}) && !{SCREEN}.includes('Search:')", 40))
            time.sleep(3)
            shot(page, 'live-article')
            n = page.evaluate("(() => { const d = [...armdos.line.online.docs.values()].pop(); return d.links.findIndex((l) => l && l.kind === 'P'); })()")
            if n > 0:
                typ(page, f'{n}\r')
                if wait_for(page, "armdos.machine.vga.mode === 0x13", 40):
                    check('picture: mode 13h', True)
                    time.sleep(2); shot(page, 'live-picture-arriving')
                    wait_for(page, "armdos.line.online.out.length === 0 && armdos.machine.modem.backlog() === 0", 60)
                    time.sleep(2); shot(page, 'live-picture')
                    typ(page, ' ')
                    check('back to text', wait_for(page, "armdos.machine.vga.mode === 3", 10))
                else: skip('live picture', 'no mode 13h (picture not available?)')
            else: skip('live picture', 'the article has no picture')
            typ(page, '{ALT+X}')
            check('sign off: NO CARRIER', wait_text(page, 'NO CARRIER', 30))
            shot(page, 'live-signoff')
            check('no page errors', not errors, '; '.join(errors[:3]))
    except SystemExit:
        pass
    finally:
        srv.shutdown()
    print(f"\n{fails} FAILED" if fails else f"\nall passed ({skips} skipped)")
    sys.exit(1 if fails else 0)

main()
