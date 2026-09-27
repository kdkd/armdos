#!/usr/bin/env python3
"""Playwright checks of the modem on the page (docs/MODEM.md): the front panel lamps and
line window, the dialling sound sequence (the modem's sound events and the WebAudio nodes they
create), the Host Link answering at 555-0100, hang-up on power off, and the BBS worker booting
on the first call to 555-1989 with its sysop view. If build/bbs.img does not exist yet the test
stands the factory hard disk in for it (it boots ARM-DOS but nothing answers the phone).

usage: python3 web/tests/test_modem.py [--shots DIR]   (make site first)
"""
import os, sys, time, json
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
        if page.evaluate(js): return True
        time.sleep(0.1)
    return False

ARGS = ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']
# count the WebAudio nodes the page creates (the modem's sounds are synthesised, not sampled)
INSTRUMENT = """
window.__audio = { osc: 0, buf: 0, bufLen: 0 };
(() => {
  const AC = window.AudioContext; if (!AC) return;
  const co = AC.prototype.createOscillator, cb = AC.prototype.createBuffer;
  AC.prototype.createOscillator = function () { __audio.osc++; return co.apply(this, arguments); };
  AC.prototype.createBuffer = function (ch, len, sr) { __audio.buf++; __audio.bufLen = Math.max(__audio.bufLen, len); return cb.apply(this, arguments); };
})();
"""
SCREEN = "(() => { const m = armdos.machine; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m.cpu.m8[0xB8000 + (y*80+x)*2] || 32); r.push(s.trimEnd()); } return r.join('\\n'); })()"
LAMP = "document.querySelector('.mlamp[data-k=\"{}\"]').classList.contains('on')"
LINE = "document.querySelector('.mline-text').textContent"
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

def boot_dos(page):
    """Power is on: wait for a DOS prompt (the factory disk may start DOSSHELL: Shift+F9)."""
    if not wait_for(page, f"armdos.machine && armdos.powered && ({SCREEN}.includes('C:\\\\>') || {SCREEN}.includes('Shift+F9'))", 60): return False
    if not page.evaluate(f"{SCREEN}.includes('C:\\\\>')"):
        page.evaluate("armdos.machine.typeText('{SHIFT+F9}')")
    return wait_for(page, f"/[A-Z]:\\\\[^>\\n]*>/.test({SCREEN})", 30)

def main():
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(args=ARGS)
            ctx = b.new_context(viewport={'width': 1440, 'height': 1100})
            page = ctx.new_page()
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.on('console', lambda m: m.type == 'error' and errors.append(m.text))
            page.add_init_script(INSTRUMENT)
            # no BBS image built yet: let the factory C: stand in (boots DOS, nothing answers)
            images = json.load(open(os.path.join(serve.SITE, 'images.json')))
            stand_in = 'bbs' not in images
            if stand_in:
                images['bbs'] = images['hd']
                page.route('**/images.json', lambda r: r.fulfill(status=200, content_type='application/json', body=json.dumps(images)))
            page.goto(url)
            page.wait_for_selector('#modemPanel')
            check('the modem sits between the monitor and the case', page.evaluate("document.getElementById('monitor').nextElementSibling.id === 'modemPanel' && document.getElementById('modemPanel').nextElementSibling.id === 'case'"))
            check('its phone list card is on the front of the case, under the name, clear of the drive bays', page.evaluate("""(() => {
                const c = document.querySelector('#case .badge-area .phone-card').getBoundingClientRect(), m = document.querySelector('#case .model').getBoundingClientRect(),
                      b = document.querySelector('#case .bays').getBoundingClientRect(), u = document.getElementById('case').getBoundingClientRect();
                return c.top >= m.bottom - 4 && c.right <= b.left + 4 && c.left >= u.left && c.bottom <= u.bottom; })()"""))
            check('eight lamps HS AA CD OH RD SD TR MR', page.evaluate("[...document.querySelectorAll('.modem-lamps label span')].map(s => s.textContent).join(' ')") == 'HS AA CD OH RD SD TR MR')
            check('line window says NO POWER', page.evaluate(LINE) == 'NO POWER')
            check('phone list card', '555-1989' in page.evaluate("document.querySelector('.phone-card').textContent"))

            check('speed switch: 4 positions, 2400 by default', page.evaluate("[...document.querySelectorAll('.mspeed input')].map(i => i.value + (i.checked ? '*' : '')).join(' ')") == '2400* 14400 33600 56000')

            page.click('#powerBtn')
            check('boots DOS', boot_dos(page))
            check('MR lamp on, ON HOOK', page.evaluate(LAMP.format('mr')) and page.evaluate(LINE) == 'ON HOOK')
            shot(page, 'modem-idle', clip=page.locator('#modemPanel').bounding_box())

            # ---- 2400: dial the Host Link from DOS (kernel COM2 device -> INT 14h -> the modem)
            page.evaluate("armdos.machine.typeText('ECHO ATDT5550100>COM2\\r')")
            check('OH lamp on and DIALING 555-0100', wait_for(page, f"{LAMP.format('oh')} && {LINE} === 'DIALING 555-0100'", 10), page.evaluate(LINE))
            shot(page, 'modem-dialing', clip=page.locator('#modemPanel').bounding_box())
            check('ringback: RINGING 555-0100', wait_for(page, f"{LINE} === 'RINGING 555-0100'", 10), page.evaluate(LINE))
            check('host answers: TRAINING', wait_for(page, f"{LINE}.startsWith('TRAINING')", 10), page.evaluate(LINE))
            check('CONNECT 2400, CD and HS lamps', wait_for(page, f"{LINE} === 'CONNECT 2400' && {LAMP.format('cd')} && {LAMP.format('hs')}", 15), page.evaluate(LINE))
            check('Host Link sends its menu', wait_for(page, "armdos.line.host.state === 'menu'", 10))
            # read COM2 the DOS way: the menu scrolls in at 2400 bps and RD flickers
            page.evaluate("armdos.machine.typeText('COPY COM2 CON\\r')")
            check('RD lamp flickers as the menu arrives', wait_for(page, LAMP.format('rd'), 10))
            # (COPY buffers until ^Z, so nothing shows yet; the menu drains through the UART)
            check('the menu drains into ARM-DOS at the line rate', wait_for(page, "armdos.machine.modem.backlog() === 0 && armdos.line.host.out.length === 0", 15), page.evaluate("armdos.machine.modem.backlog()"))
            shot(page, 'modem-connected', clip=page.locator('#modemPanel').bounding_box())
            log = page.evaluate("armdos.line.audio.log.map(e => e.kind)")
            digits = page.evaluate("armdos.line.audio.log.filter(e => e.kind === 'dtmf').map(e => e.digit).join('')")
            order = ['relay', 'dialtone', 'dtmf', 'ringback', 'answer', 'handshake', 'hush']
            idx = [log.index(k) if k in log else -1 for k in order]
            check('2400: click, dial tone, DTMF, ringback, answer tone, handshake, silence', all(i >= 0 for i in idx) and idx == sorted(idx), json.dumps(log))
            check('DTMF digits 5550100', digits == '5550100', digits)
            hs = page.evaluate("armdos.line.audio.log.find(e => e.kind === 'handshake')")
            check('2400 trains with V.22bis (1.9 s)', hs['mod'] == 'V22BIS' and hs['ms'] == 1900, json.dumps(hs))
            t = page.evaluate("(() => { const l = armdos.line.audio.log; const f = k => l.find(e => e.kind === k).t; return [f('dialtone'), f('dtmf'), f('ringback'), f('answer'), f('handshake'), f('hush')]; })()")
            check('timeline: ~0.9 s dial tone, answer tone ~2.4 s before the handshake', 800 < t[1] - t[0] < 1000 and 2300 < t[4] - t[3] < 2500, json.dumps(t))
            a = page.evaluate("__audio")
            check('WebAudio: DTMF/tone oscillators and a synthesised handshake buffer', a['osc'] >= 14 + 7 and a['bufLen'] >= 8000 * 1.9, json.dumps(a))
            check('speaker chain built', page.evaluate("armdos.line.audio.ready && Object.keys(armdos.line.audio.pairs).join() === 'dialtone,ringback,ringintl,busy,answer'"))

            # ---- power off hangs up: the Host Link sees the line drop
            page.click('#powerBtn')
            check('power off: the Host Link goes back on hook', wait_for(page, "armdos.line.host.state === 'idle'", 5), page.evaluate("armdos.line.host.state"))
            check('lamps dark, NO POWER', wait_for(page, f"!{LAMP.format('oh')} && {LINE} === 'NO POWER'", 3))

            # ---- the speed switch: 56K, remembered across a reload
            page.click('.mspeed label:nth-child(4)')
            check('switch moves to 56K', page.evaluate("document.querySelector('.mspeed').dataset.speed") == '56000' and 'SMARTLINE 56K' in page.evaluate("document.querySelector('.modem-brand').textContent"))
            check('choice saved in localStorage', json.loads(page.evaluate("localStorage.getItem('armdos.prefs')")).get('modemSpeed') == 56000)
            shot(page, 'modem-switch-56k', clip=page.locator('#modemPanel').bounding_box())
            page.reload(); page.wait_for_selector('#modemPanel')
            check('switch still at 56K after a reload', page.evaluate("document.querySelector('.mspeed input:checked').value") == '56000')
            page.click('#powerBtn')
            check('boots DOS again', boot_dos(page))
            check('the machine modem follows the switch', page.evaluate("armdos.machine.modem.switchRate") == 56000)

            # ---- 56K to the Host Link: the V.8bis / V.8 / V.34 / V.90 training, CONNECT 56000
            page.evaluate("armdos.line.audio.log.length = 0")
            page.evaluate("armdos.machine.typeText('ECHO ATDT5550100>COM2\\r')")
            check('56K: TRAINING 56000', wait_for(page, f"{LINE} === 'TRAINING 56000'", 20), page.evaluate(LINE))
            check('56K: CONNECT 56000 after the long handshake', wait_for(page, f"{LINE} === 'CONNECT 56000' && {LAMP.format('hs')}", 30), page.evaluate(LINE))
            log56 = page.evaluate("armdos.line.audio.log")
            hs56 = [e for e in log56 if e['kind'] == 'handshake']
            check('56K: one V.90 handshake event, ~17.9 s, no separate V.25 answer tone', len(hs56) == 1 and hs56[0]['mod'] == 'V90' and 17000 < hs56[0]['ms'] < 19000 and not any(e['kind'] == 'answer' for e in log56), json.dumps(hs56))
            check('56K: the handshake buffer is ~18 s of 8 kHz audio', page.evaluate("armdos.line.audio.lastHandshake.mod === 'V90' && armdos.line.audio.lastHandshake.secs > 17") and page.evaluate("__audio.bufLen") >= 8000 * 17, json.dumps(page.evaluate("armdos.line.audio.lastHandshake")))
            check('56K: modem reports 53333 down / 31200 up', page.evaluate("[armdos.machine.modem.rxRate, armdos.machine.modem.txRate].join('/')") == '53333/31200')
            shot(page, 'modem-connected-56k', clip=page.locator('#modemPanel').bounding_box())
            page.evaluate("armdos.machine.modem.hangup()")

            # ---- the other numbers (emu/phonelines.mjs): a fax machine screams, KREMVAX rings the Soviet way
            wait_for(page, f"{LINE} === 'ON HOOK'", 5)
            page.evaluate("armdos.line.audio.log.length = 0")
            page.evaluate("armdos.machine.typeText('ECHO ATDT5553299>COM2\\r')")
            check('555-3299: a fax answers - CED and V.21 in the speaker', wait_for(page, "armdos.line.audio.log.some(e => e.kind === 'line')", 40))
            check('...then hangs up (no carrier)', wait_for(page, f"{LINE} === 'ON HOOK'", 30), page.evaluate(LINE))
            page.evaluate("armdos.line.audio.log.length = 0")
            page.evaluate("armdos.machine.typeText('ECHO ATDT01170952311984>COM2\\r')")
            check('KREMVAX: international routing, then the 425 Hz ring', wait_for(page, "armdos.line.audio.log.some(e => e.kind === 'ringback' && e.on)", 40) and page.evaluate("armdos.line.audio.pairs.ringintl !== undefined"))
            check('...answers at 1200 over the cable', wait_for(page, f"{LINE} === 'CONNECT 1200'", 60), page.evaluate(LINE))
            page.evaluate("armdos.machine.modem.hangup()")

            # ---- 555-1989: the BBS worker boots on the first call; sysop view shows its screen
            check('no BBS worker before anyone calls', page.evaluate("!armdos.line.worker"))
            wait_for(page, f"{LINE} === 'ON HOOK'", 5)
            page.evaluate("armdos.machine.typeText('ECHO ATDT5551989>COM2\\r')")
            check('dialling 555-1989 starts the BBS worker', wait_for(page, "!!armdos.line.worker", 15))
            check('the BBS machine modem rings', wait_for(page, "armdos.line.bbsPanel && ['ringing', 'training', 'connected'].includes(armdos.line.bbsPanel.state)", 40), json.dumps(page.evaluate("armdos.line.bbsPanel || null")))
            page.click('.mbtn[aria-pressed]')
            check('sysop view opens', page.evaluate("!document.getElementById('sysopView').hidden"))
            check('sysop view draws the BBS screen', wait_for(page, "(() => { const c = document.querySelector('#sysopView canvas'); const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let lit = 0; for (let i = 0; i < d.length; i += 64) if (d[i] > 60 || d[i+1] > 60 || d[i+2] > 60) lit++; return lit > 50; })()", 40))
            if stand_in:
                time.sleep(1)
                check('nobody answers the stand-in: RINGING continues', page.evaluate(LINE) == 'RINGING 555-1989', page.evaluate(LINE))
            else:
                check('the BBS answers at 56K: CONNECT 56000', wait_for(page, f"{LINE} === 'CONNECT 56000'", 60), page.evaluate(LINE))
                check('the BBS screen shows the caller', wait_for(page, "armdos.line.bbsPanel && armdos.line.bbsPanel.cd", 10))
            time.sleep(1)
            shot(page, 'modem-sysop', clip=page.locator('#modemPanel').bounding_box())
            check('no page errors', not errors, '; '.join(errors[:3]))
            b.close()
    finally:
        srv.shutdown()
    print('modem web: ' + ('all passed' if not fails else f'{fails} failed'))
    sys.exit(1 if fails else 0)

if __name__ == '__main__':
    main()
