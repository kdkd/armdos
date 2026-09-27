#!/usr/bin/env python3
"""Round 5: full-screen mouse capture, the POST floppy seek sound, the visitor's own ISO as
drive E:, and the new title text.

usage: python3 web/tests/test_round5.py [--shots DIR]   (needs xorriso)
"""
import os, sys, time, tempfile, subprocess, shutil
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
    if shots: os.makedirs(shots, exist_ok=True); page.screenshot(path=os.path.join(shots, name + '.png'), **kw)
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
TITLE = 'What if the IBM PC used an ARM instead of x86? How overboard can we take this idea?'

def main():
    tmp = tempfile.mkdtemp(prefix='armdos-r5-')
    tree = os.path.join(tmp, 'tree'); os.makedirs(os.path.join(tree, 'DOCS'))
    open(os.path.join(tree, 'README.TXT'), 'w').write('Hello from a visitor ISO!\r\n')
    open(os.path.join(tree, 'DOCS', 'BIG.BIN'), 'wb').write(os.urandom(3 * 1024 * 1024))
    iso = os.path.join(tmp, 'mydisc.iso')
    subprocess.run(['xorriso', '-as', 'mkisofs', '-V', 'MYDISC', '-o', iso, tree], check=True, capture_output=True)
    notiso = os.path.join(tmp, 'notacd.iso'); open(notiso, 'wb').write(os.urandom(100000))
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            errors = []
            b = p.chromium.launch(args=['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
            ctx = b.new_context(viewport={'width': 1440, 'height': 1000})
            pg = ctx.new_page(); pg.on('pageerror', lambda e: errors.append(str(e)))
            pg.goto(url + '?nosw')
            pg.wait_for_selector('#powerBtn')
            check('page title', pg.title() == TITLE, pg.title())
            check('header subtitle', pg.inner_text('.masthead .subtitle').strip() == TITLE)
            check('manifest description', pg.evaluate("fetch('manifest.webmanifest').then(r => r.json()).then(m => m.description)") == TITLE)
            # ---- POST seek sound: the first recalibrate after power-on plays the out-and-back seek
            pg.evaluate("""(() => { const s = armdos.sound; window.__seek = { post: 0, recal: 0 };
                const a = s.fdPostSeek.bind(s), r = s.fdRecal.bind(s);
                s.fdPostSeek = () => { __seek.post++; __seek.t = performance.now(); a(); }; s.fdRecal = (n) => { __seek.recal++; r(n); }; })()""")
            t0 = pg.evaluate("performance.now()")
            pg.click('#powerBtn')
            check('POST recalibrate plays the seek sound once', wait_for(pg, "__seek.post === 1", 20), pg.evaluate("JSON.stringify(__seek)"))
            check('seek sound is scheduled into WebAudio (context running)', pg.evaluate("armdos.sound.ctx && armdos.sound.ctx.state !== 'closed' && armdos.sound.fdHeadFree > armdos.sound.ctx.currentTime - 5"))
            check('boots to C:\\>', wait_for(pg, PROMPT, 60))
            check('boot not slowed: prompt within 25 s of power', pg.evaluate("performance.now()") - t0 < 25000)
            check('POST seek only once per power-on', pg.evaluate("__seek.post") == 1)

            # ---- full screen: mouse captured on entry, Ctrl+Alt+M toggles, released on exit
            pg.evaluate("""(() => { const i = armdos.input; window.__mouse = { lock: 0, toggle: 0 };
                const l = i.lockMouse.bind(i), t = i.toggleMouse.bind(i);
                i.lockMouse = () => { __mouse.lock++; l(); }; i.toggleMouse = () => { __mouse.toggle++; t(); };
                const m = armdos.machine, d = m.keyDown.bind(m); window.__keys = []; m.keyDown = (c) => { __keys.push(c); return d(c); }; })()""")
            pg.click('#fullBtn')
            ok = wait_for(pg, "document.fullscreenElement === document.getElementById('monitor')", 5)
            check('full-screen button enters full screen', ok)
            check('entering full screen captures the mouse', wait_for(pg, "__mouse.lock >= 1", 3), pg.evaluate("JSON.stringify(__mouse) + ' locked=' + (document.pointerLockElement && document.pointerLockElement.id)"))
            check('full-screen bar is shown (auto-hiding)', wait_for(pg, "getComputedStyle(document.getElementById('fsBar')).display === 'flex'", 3))
            shot(pg, 'fullscreen')
            pg.focus('#screen')
            pg.keyboard.down('Control'); pg.keyboard.down('Alt'); pg.keyboard.press('m'); pg.keyboard.up('Alt'); pg.keyboard.up('Control')
            check('Ctrl+Alt+M toggles the mouse', pg.evaluate("__mouse.toggle") == 1)
            check('the M of Ctrl+Alt+M never reaches DOS', 'KeyM' not in pg.evaluate("__keys") and 'ControlLeft' in pg.evaluate("__keys"), pg.evaluate("__keys"))
            pg.click('#fsMouse', force=True)
            check('the bar button toggles the mouse too', pg.evaluate("__mouse.toggle") == 2)
            pg.evaluate("document.exitFullscreen()")
            check('leaving full screen releases the mouse', wait_for(pg, "!document.fullscreenElement && !document.pointerLockElement", 5))
            check('bar hidden outside full screen', pg.evaluate("getComputedStyle(document.getElementById('fsBar')).display === 'none'"))

            # ---- your own ISO as E: (the CD-ROM, after the hard disks C: and D:)
            pg.set_input_files('#cdIsoFile', notiso)
            check('a non-ISO file is refused with a friendly message', wait_for(pg, "document.getElementById('cdStatus').textContent.includes(\"isn't an ISO 9660\")", 5), pg.inner_text('#cdStatus'))
            pg.set_input_files('#cdIsoFile', iso)
            check('ISO goes into the drive', wait_for(pg, "armdos.machine.cdrom.disc && armdos.machine.cdrom.disc.title.includes('mydisc.iso') && !armdos.machine.cdrom.trayOpen", 8), pg.inner_text('#cdStatus'))
            check('status names the disc and its volume', wait_for(pg, "document.getElementById('cdStatus').textContent.includes('MYDISC')", 6), pg.inner_text('#cdStatus'))
            pg.evaluate("armdos.machine.typeText('DIR E:\\r')")
            check('DOS lists the ISO on E:', wait_for(pg, f"{SCREEN}.includes('README') && {SCREEN}.includes('DOCS')", 30), pg.evaluate(SCREEN)[-500:])
            pg.evaluate("armdos.machine.typeText('TYPE E:\\\\README.TXT\\r')")
            check('DOS reads a file from the ISO', wait_for(pg, f"{SCREEN}.includes('Hello from a visitor ISO!')", 20), pg.evaluate(SCREEN)[-300:])
            pg.evaluate("armdos.machine.typeText('COPY E:\\\\DOCS\\\\BIG.BIN NUL\\r')")
            check('a 3 MB file streams from the local file', wait_for(pg, f"/1 File\\(s\\) copied/i.test({SCREEN})", 60), pg.evaluate(SCREEN)[-200:])
            check('the ISO is read lazily (blocks, not the whole file)', pg.evaluate("(() => { for (const s of armdos.cd ? armdos.cd.sources.values() : []) if (s.img) return s.img.blocks.size; return -1; })()") != 0)
            pg.evaluate("window.scrollTo(0, 0)")
            if shots: pg.locator('#monitor').screenshot(path=os.path.join(shots, 'iso-d.png')); pg.locator('#cdBox').screenshot(path=os.path.join(shots, 'iso-box.png'))
            ctx.close(); b.close()
            check('no page errors', not errors, '; '.join(errors[:3]))
    finally:
        srv.shutdown(); shutil.rmtree(tmp, ignore_errors=True)
    print('all passed' if not fails else f'{fails} failed')
    sys.exit(1 if fails else 0)

main()
