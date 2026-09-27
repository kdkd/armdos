#!/usr/bin/env python3
"""Playwright check of the joystick on the page (web/js/joystick.js): a fake game
controller (navigator.getGamepads overridden before the page loads) lights the joystick
lamps, plugs a stick into the game port and moves JOYTEST.EXE's crosshair; its button
lights JOYTEST's lamp; the on-screen pad's JOY switch drives the stick with the D-pad
and the big buttons; the open case reports the game port.

Stages its own copy of the site (build/joy-web/site) around the image the JOYTEST node
test builds (build/joytest-test/joytest.img: COMMAND.COM, C:\\DOS\\JOYTEST.EXE), so it
does not disturb build/site.

usage: python3 web/tests/test_joystick.py
"""
import os, sys, time, shutil, subprocess
sys.path.insert(0, os.path.dirname(__file__))
import serve
from playwright.sync_api import sync_playwright

ROOT = serve.ROOT
fails = 0
def check(name, ok, extra=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + (f'  ({extra})' if extra and not ok else ''))
    if not ok: fails += 1

def wait_for(page, js, timeout=15):
    t = time.time()
    while time.time() - t < timeout:
        if page.evaluate(js): return True
        time.sleep(0.1)
    return False

SCREEN = "(() => { const m = armdos.machine; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m.cpu.m8[0xB8000 + (y*80+x)*2] || 32); r.push(s.trimEnd()); } return r.join('\\n'); })()"
# JOYTEST's crosshair: the sun (CP437 0Fh) inside the Position box (rows 3-19, columns 42-77)
CROSS = "(() => { const m = armdos.machine.cpu.m8; for (let y = 3; y <= 19; y++) for (let x = 42; x <= 77; x++) if (m[0xB8000 + (y*80+x)*2] === 0x0F) return [x, y]; return null; })()"
ROW = "((r) => { const m = armdos.machine.cpu.m8; let s = ''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m[0xB8000 + (r*80+x)*2] || 32); return s; })"
# a standard-mapping controller the test drives: window.__pad
FAKEPAD = """
window.__pads = [];
window.__mkpad = () => {
  const p = { id: 'Test Controller (STANDARD GAMEPAD Vendor: 045e Product: 028e)', index: 0, connected: true, mapping: 'standard',
    timestamp: 0, axes: [0, 0, 0, 0], buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })) };
  window.__pads = [p]; window.__pad = p; return true;
};
window.__btn = (i, on) => { const b = window.__pad.buttons[i]; b.pressed = on; b.value = on ? 1 : 0; return true; };
Object.defineProperty(navigator, 'getGamepads', { value: () => window.__pads, configurable: true });
"""

def stage():
    img = os.path.join(ROOT, 'build', 'joytest-test', 'joytest.img')
    if not os.path.exists(img):
        subprocess.run(['node', os.path.join(ROOT, 'apps/joytest/tests/run.mjs')], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    d = os.path.join(ROOT, 'build', 'joy-web')
    os.makedirs(d, exist_ok=True)
    shutil.copy(os.path.join(ROOT, 'build', 'rom.bin'), d)
    shutil.copy(img, os.path.join(d, 'hd.img'))
    subprocess.run(['node', 'web/tools/build-site.mjs', '--out', 'build/joy-web/site'], cwd=ROOT, check=True,
                   env={**os.environ, 'BUILD': 'build/joy-web'}, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return os.path.join(d, 'site')

def main():
    serve.SITE = stage()
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(args=['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--autoplay-policy=no-user-gesture-required'])
            page = b.new_page(viewport={'width': 1440, 'height': 1000})
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.add_init_script(FAKEPAD)
            page.goto(url)
            page.wait_for_selector('#powerBtn')
            time.sleep(1)
            check('lamp dark with no controller', not page.evaluate("document.getElementById('joyLed').classList.contains('on')"))
            check('the lamp says how to connect one', 'Connect a game controller' in (page.get_attribute('#joyLamp', 'title') or ''))
            page.evaluate('__mkpad()')
            check('a controller lights the joystick lamp', wait_for(page, "document.getElementById('joyLed').classList.contains('on')", 5))
            check('...and names it', 'Test Controller' in (page.get_attribute('#joyLamp', 'title') or ''), page.get_attribute('#joyLamp', 'title'))

            page.click('#powerBtn')
            check('boots to C:\\>', wait_for(page, f"{SCREEN}.includes('C:\\\\>')", 60))
            check('the game port is in (Machine joystick option, from the open case)', page.evaluate('armdos.machine.hw.joystick === true && armdos.machine.in8(0x201) !== 0xFF'))
            check('stick A plugged in, centred (50 kOhm)', page.evaluate('armdos.machine.joy.ohms[0] === 50000 && armdos.machine.joy.ohms[1] === 50000'),
                  page.evaluate('JSON.stringify(armdos.machine.joy.ohms)'))
            page.evaluate("armdos.machine.typeText('JOYTEST\\r')")
            check('JOYTEST runs', wait_for(page, f"{SCREEN}.includes('Joystick Test and Calibration')", 20))
            check('crosshair shown', wait_for(page, f"!!{CROSS}", 10), page.evaluate(SCREEN))
            c0 = page.evaluate(CROSS)
            check('centred controller: crosshair in the middle', c0 and 57 <= c0[0] <= 62 and 10 <= c0[1] <= 12, str(c0))
            # push the left stick right and up (Gamepad y -1 = up)
            page.evaluate('window.__pad.axes[0] = 1; window.__pad.axes[1] = -1; true')
            check('stick right+up: the crosshair moves right and up',
                  wait_for(page, f"(() => {{ const c = {CROSS}; return c && c[0] >= 70 && c[1] <= 5; }})()", 10), str(page.evaluate(CROSS)))
            check('the game port sees 100 kOhm / 0 Ohm', page.evaluate('armdos.machine.joy.ohms[0] === 100000 && armdos.machine.joy.ohms[1] === 0'))
            # inside the dead zone = centre
            page.evaluate('window.__pad.axes[0] = 0.08; window.__pad.axes[1] = -0.1; true')
            check('dead zone: a slightly-off stick reads centred', wait_for(page, 'armdos.machine.joy.ohms[0] === 50000 && armdos.machine.joy.ohms[1] === 50000', 5))
            # the D-pad
            page.evaluate('__btn(14, true)')
            check('D-pad left: crosshair at the left edge', wait_for(page, f"(() => {{ const c = {CROSS}; return c && c[0] <= 44; }})()", 10), str(page.evaluate(CROSS)))
            page.evaluate('__btn(14, false)')
            # button A = joystick button 1
            page.evaluate('__btn(0, true)')
            check('button A: JOYTEST lamp for button 1', wait_for(page, f"{ROW}(8).slice(12, 20).includes('ON')", 10), page.evaluate(f"{ROW}(8)"))
            check('button A: the case lamp flashes amber', page.evaluate("document.getElementById('joyLed').classList.contains('fire')"))
            page.evaluate('__btn(0, false); __btn(3, true)')
            check('button Y: joystick button 4 (stick B button 2)', wait_for(page, f"{ROW}(17).slice(30, 38).includes('ON')", 10), page.evaluate(f"{ROW}(17)"))
            page.evaluate('__btn(3, false)')
            check('right stick: joystick B plugged in', wait_for(page, "armdos.machine.joy.ohms[2] === 50000", 5))

            # the controller goes away: stick unplugged, lamp dark
            page.evaluate('window.__pads = []; true')
            check('controller gone: lamp dark, sticks unplugged', wait_for(page, "!document.getElementById('joyLed').classList.contains('on') && armdos.machine.joy.ohms[0] === null", 5))
            check('JOYTEST: "No joystick in port A"', wait_for(page, f"{SCREEN}.includes('No joystick in port A')", 10))

            # the on-screen pad in JOY mode
            page.evaluate("armdos.appLayout.setKeys('pad')")
            page.wait_for_selector('#pcKeys .joytoggle')
            page.click('#pcKeys .joytoggle')
            check('pad JOY switch on: stick A plugged in, lamp lit', wait_for(page, "armdos.machine.joy.ohms[0] === 50000 && document.getElementById('joyLed').classList.contains('on')", 5))
            check('the JOY switch is lit', page.get_attribute('#pcKeys .joytoggle', 'aria-pressed') == 'true')
            wait_for(page, f"!!{CROSS}", 10)
            right = page.locator('#pcKeys .dpad .k[data-code="ArrowRight"]').bounding_box()
            page.mouse.move(right['x'] + right['width'] / 2, right['y'] + right['height'] / 2)
            page.mouse.down()
            check('pad D-pad right: the crosshair moves to the right edge', wait_for(page, f"(() => {{ const c = {CROSS}; return c && c[0] >= 75; }})()", 10), str(page.evaluate(CROSS)))
            check('...and no arrow key reached DOS', page.evaluate('armdos.machine.cpu.m8[0x41A] === armdos.machine.cpu.m8[0x41C]'))
            page.mouse.up()
            check('released: back to the centre', wait_for(page, f"(() => {{ const c = {CROSS}; return c && c[0] >= 57 && c[0] <= 62; }})()", 10), str(page.evaluate(CROSS)))
            fire = page.locator('#pcKeys .pad-buttons .k[data-code="ControlLeft"]').bounding_box()
            page.mouse.move(fire['x'] + fire['width'] / 2, fire['y'] + fire['height'] / 2)
            page.mouse.down()
            check('pad FIRE = joystick button 1', wait_for(page, f"{ROW}(8).slice(12, 20).includes('ON')", 10), page.evaluate(f"{ROW}(8)"))
            check('FIRE shows "Joy 1"', 'Joy 1' in page.inner_text('#pcKeys .pad-buttons .k[data-code="ControlLeft"]'))
            page.mouse.up()
            page.click('#pcKeys .joytoggle')
            check('JOY off: the pad is keys again, the stick unplugged', wait_for(page, "armdos.machine.joy.ohms[0] === null", 5)
                  and 'Ctrl' in page.inner_text('#pcKeys .pad-buttons .k[data-code="ControlLeft"]'))
            page.evaluate("armdos.appLayout.setKeys(null)")
            page.evaluate("armdos.machine.typeText('{ESC}')")
            wait_for(page, f"{SCREEN}.includes('C:\\\\>')", 10)

            # the open case: the game port in the summary, J2 on the I/O card
            page.click('#powerBtn')
            time.sleep(1)
            page.click('#openCaseBtn')
            page.wait_for_selector('#openbox:not([hidden])')
            summ = page.inner_text('#openbox .ob-summary').lower()
            check('open case: "Game port 201h on the Sound Blaster 16"', 'game port' in summ and '201h on the sound blaster 16' in summ, summ)
            check('the multi-I/O card has J2 GAME', page.locator('#openbox .ob-gamejp').count() == 1)
            page.click('#openbox .ob-gamejp')
            check('J2 off: the SB16 still answers at 201h', page.evaluate('JSON.stringify(armdos.box ? 1 : 1)') and '201h on the sound blaster 16' in page.inner_text('#openbox .ob-summary').lower())
            page.click('#openbox .ob-card[data-card="sb16"]')        # the SB16 to the bin
            summ = page.inner_text('#openbox .ob-summary').lower()
            check('no SB16, J2 off: no game port', 'game port' in summ and summ.split('game port')[1].split('\n')[1].strip() == 'none', summ)
            page.click('#openbox .ob-gamejp')
            summ = page.inner_text('#openbox .ob-summary').lower()
            check('J2 on: the multi-I/O card\'s game port', '201h on the multi-i/o card' in summ, summ)
            page.click('#openbox .ob-gamejp')
            page.click('#powerBtn')
            check('powered on without a game port: 201h floats', wait_for(page, "armdos.powered && armdos.machine.hw.joystick === false && armdos.machine.in8(0x201) === 0xFF", 10))
            check('no page errors', not errors, '; '.join(errors[:3]))
            b.close()
    finally:
        srv.shutdown()
    print('all passed' if not fails else f'{fails} FAILED')
    sys.exit(1 if fails else 0)

main()
