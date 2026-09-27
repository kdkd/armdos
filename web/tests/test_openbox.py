#!/usr/bin/env python3
"""Playwright checks of "open the box" (web/js/openbox.js): the lid comes off, cards move
between the slots and the parts bin (drag and click), SIMMs and the clock jumper change,
nothing can be touched while the power is on, POST shows the new configuration, it all
survives a reload, "Factory configuration" puts it back, the Hercules swap follows the
Monitor selector, and the phone and app layouts work.

usage: python3 web/tests/test_openbox.py [--site DIR] [--shots DIR]
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import serve
from playwright.sync_api import sync_playwright

arg = lambda k: sys.argv[sys.argv.index(k) + 1] if k in sys.argv else None
shots, site = arg('--shots'), arg('--site')
fails = 0
def check(name, ok, extra=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + (f'  ({extra})' if extra else ''))
    if not ok: fails += 1
def shot(page, name, **kw):
    if shots:
        os.makedirs(shots, exist_ok=True)
        page.screenshot(path=os.path.join(shots, name + '.png'), **kw)

ARGS = ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']
def screen_js(base):
    return ("(() => { const m = armdos.machine; if (!m) return ''; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) "
            f"s += String.fromCharCode(m.cpu.m8[{base} + (y*80+x)*2] || 32); r.push(s.trimEnd()); }} return r.join('\\n'); }})()")
COLOR, MONO = screen_js('0xB8000'), screen_js('0xB0000')
CFG = "JSON.parse(localStorage.getItem('armdos.prefs') || '{}')"

def wait_for(page, js, timeout=15, step=0.1):
    t = time.time()
    while time.time() - t < timeout:
        v = page.evaluate(js)
        if v: return v
        time.sleep(step)
    return None

def center(page, sel):
    b = page.locator(sel).first.bounding_box()
    return b['x'] + b['width'] / 2, b['y'] + b['height'] / 2

def drag(page, src, dst, dst_frac=(0.5, 0.5)):
    page.locator(src).first.scroll_into_view_if_needed()
    x0, y0 = center(page, src)
    b = page.locator(dst).first.bounding_box()
    x1, y1 = b['x'] + b['width'] * dst_frac[0], b['y'] + b['height'] * dst_frac[1]
    page.mouse.move(x0, y0); page.mouse.down()
    for i in range(1, 13): page.mouse.move(x0 + (x1 - x0) * i / 12, y0 + (y1 - y0) * i / 12); time.sleep(0.02)
    page.mouse.up(); time.sleep(0.15)

def slots(page):
    return page.evaluate("armdos.box.cfg.slots")

def post_box(page, screen, timeout=40):
    """The POST configuration box, caught while it is on the screen."""
    return wait_for(page, f"(() => {{ const s = {screen}; return s.includes('Parallel Port(s)') && s.includes('System Configuration') ? s : ''; }})()", timeout, 0.03) or ''

def main():
    srv, url = serve.start(site)
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(args=ARGS)
            ctx = b.new_context(viewport={'width': 1440, 'height': 2000})
            page = ctx.new_page()
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.goto(url + '?nosw')
            page.wait_for_selector('#openCaseBtn')

            # ---- the lid comes off
            check('the panel is closed at first', page.evaluate("document.getElementById('openbox').hidden"))
            page.click('#openCaseBtn')
            check('Open case shows the inside, the lid slides off', page.evaluate("!document.getElementById('openbox').hidden && document.getElementById('openbox').classList.contains('lid-off')"))
            wait_for(page, "document.querySelector('.ob-lid').getAnimations().every((a) => a.playState === 'finished')", 5)
            lid = page.evaluate("(() => { const l = document.querySelector('.ob-lid').getBoundingClientRect(), s = document.querySelector('.ob-stage').getBoundingClientRect(); return [l.bottom, s.top, document.querySelector('.ob-lid').getAnimations().map((a) => a.playState)]; })()")
            check('the lid is out of the way', lid[0] <= lid[1] + 2, str(lid))
            check('factory cards in their slots', slots(page) == ['io', 'modem', None, 'sb16', None, 'vga', 'ide'], str(slots(page)))
            check('silkscreens: CPU, VFP, BIOS', page.evaluate("(() => { const t = document.querySelector('.ob-board').textContent; return t.includes('ARM926EJ-S') && t.includes('VFP9-S') && t.includes('EUROPA ARM/AT BIOS') && t.includes('v1.00  (C)1988'); })()"))
            check('four 4 MB SIMMs, jumper on 100', page.evaluate("document.querySelector('.ob-board').textContent.split('4MB').length - 1 >= 4 && armdos.box.cfg.mhz === 100"))
            shot(page, 'openbox-factory', clip=page.locator('#openbox').bounding_box())

            # ---- drag the Sound Blaster out, click things out, set SIMMs and the jumper
            drag(page, '.ob-card[data-card="sb16"]', '.ob-bin-list')
            check('SB16 dragged to the bin: no sound card', page.evaluate("armdos.box.cfg.sound") == 'none' and 'sb16' not in slots(page))
            check('the bin shows it', page.evaluate("!!document.querySelector('.ob-part[data-id=\"sb16\"]')"))
            drag(page, '.ob-part[data-id="adlib"]', '.ob-card[data-card="modem"]', (0.5, 0.3))
            check('AdLib dropped on the modem\'s slot: it goes in, the modem shifts over', slots(page)[1] == 'adlib' and slots(page)[2] == 'modem' and page.evaluate("armdos.box.cfg.sound") == 'adlib', str(slots(page)))
            drag(page, '.ob-card[data-card="modem"]', '.ob-slot[data-slot="4"] .ob-drop', (0.5, 0.3))
            check('the modem dragged to slot 5', slots(page)[4] == 'modem', str(slots(page)))
            drag(page, '.ob-card[data-card="adlib"]', '.ob-bin-list')
            check('...and back to the bin', page.evaluate("armdos.box.cfg.sound") == 'none')
            page.click('.ob-card[data-card="modem"]')
            check('click the modem card: out', page.evaluate("armdos.box.cfg.modem") is False and 'modem' not in slots(page))
            page.click('.ob-cd')
            check('click the CD-ROM drive: out, the case shows a blank bay', page.evaluate("armdos.box.cfg.cdrom === false && document.getElementById('cdDrive').hidden && !document.querySelector('.cd-blank').hidden"))
            page.click('.ob-mouse rect')
            check('unplug the mouse', page.evaluate("armdos.box.cfg.mouse === false && document.getElementById('mouseBtn').disabled"))
            page.click('.ob-card[data-card="vga"]')
            check('the only display card stays in', 'vga' in slots(page) and page.evaluate("/display card/.test(document.querySelector('.ob-note').textContent)"))
            page.click('.ob-card[data-card="ide"]')
            check('the IDE controller is fixed', 'ide' in slots(page))
            page.click('#obRam button[data-v="4"]')
            page.click('.ob-jpos[data-mhz="33"]')
            check('4 MB of SIMMs, J1 on 33 MHz', page.evaluate("armdos.box.cfg.ram === 4 && armdos.box.cfg.mhz === 33"))
            check('saved in localStorage', page.evaluate(f"(() => {{ const h = {CFG}.hw; return h.ram === 4 && h.mhz === 33 && h.sound === 'none' && !h.modem && !h.cdrom && !h.mouse; }})()"))
            check('the modem panel goes dark', page.evaluate("document.getElementById('modemPanel').classList.contains('absent')"))
            shot(page, 'openbox-stripped', clip=page.locator('#openbox').bounding_box())

            # ---- power on: POST sees it; nothing may be touched now
            page.click('#powerBtn')
            banner = wait_for(page, f"(() => {{ const s = {COLOR}; return /Memory Test : +\\d+K OK/.test(s) ? s : ''; }})()", 30, 0.03) or ''
            check('POST: CPU at 33 MHz, memory test 4096K', 'CPU at 33 MHz' in banner and 'Memory Test :   4096K OK' in banner, banner[:500])
            box = post_box(page, COLOR)
            check('POST: 33 MHz, ext 3456 KB', '33 MHz, RISC' in box and 'Ext. Memory Size : 3456 KB' in box, box)
            check('POST: no sound, no MIDI, no mouse, no CD, one serial port', all(s in box for s in ['Sound Card       : None', 'MIDI Interface   : None', 'Pointing Device  : None', 'CD-ROM Drive     : None', 'Serial Port(s)   : 3F8 ']), box)
            check('the machine got the options', page.evaluate("(() => { const m = armdos.machine; return m.turboMhz === 33 && m.ramEnd === 0x460000 && m.hw.sound === 'none' && !m.hw.modem && !m.hw.cdrom && !m.hw.mouse; })()"))
            page.click('.ob-card[data-card="vga"]')
            check('powered: touching a card shows the warning', page.evaluate("!document.querySelector('.ob-warn').hidden && armdos.box.cfg.slots.includes('vga')"))
            page.click('#obRam button[data-v="16"]')
            check('powered: the SIMMs stay as they are', page.evaluate("armdos.box.cfg.ram") == 4)
            check('the PSU fan turns while the power is on', page.evaluate("document.getElementById('openbox').classList.contains('powered') && getComputedStyle(document.querySelector('.ob-fan')).animationName === 'obSpin'"))
            shot(page, 'openbox-powered', clip=page.locator('#openbox').bounding_box())
            check('DOS boots', wait_for(page, f"{COLOR}.includes('C:\\\\>')", 40))
            page.evaluate("armdos.machine.typeText('MEM\\r')")
            check('MEM: 3,473,408 bytes of extended memory (3456K less the BIOS 64K)', wait_for(page, f"{COLOR}.includes('3473408 bytes total extended memory')", 20), page.evaluate(COLOR)[-600:])
            page.click('#powerBtn'); time.sleep(0.5)

            # ---- persistence across a reload
            page.reload(); page.wait_for_selector('#openCaseBtn')
            page.click('#openCaseBtn'); time.sleep(0.4)
            check('reload: the configuration is still there', page.evaluate("(() => { const c = armdos.box.cfg; return c.ram === 4 && c.mhz === 33 && c.sound === 'none' && !c.modem && !c.cdrom && !c.mouse; })()"))
            check('reload: parts in the bin', page.evaluate("['sb16','adlib','modem','hercules'].every((id) => document.querySelector(`.ob-part[data-id=\"${id}\"]`)) && !!document.querySelector('.ob-part.ob-cdrom') && !!document.querySelector('.ob-part.ob-mouse')"))

            # ---- the Hercules swap follows the Monitor selector, both ways
            drag(page, '.ob-part[data-id="hercules"]', '.ob-card[data-card="vga"]', (0.5, 0.4))
            check('Hercules dragged onto the VGA: swapped, monitor = green', 'hercules' in slots(page) and 'vga' not in slots(page) and page.evaluate("document.getElementById('monitorSel').value") == 'green')
            page.select_option('#monitorSel', 'vga')
            check('Monitor selector back to VGA: the VGA card is in again', 'vga' in slots(page) and 'hercules' not in slots(page))
            page.select_option('#monitorSel', 'amber')
            check('Monitor selector amber: the Hercules card is in', 'hercules' in slots(page))
            page.click('.ob-part[data-id="sb16"]')
            page.click('.ob-daughter')
            check('SB16 back in; the MIDI daughterboard comes off and follows the MIDI switch', page.evaluate("armdos.box.cfg.sound === 'sb16' && document.getElementById('gmBtn').getAttribute('aria-pressed') === 'false' && !!document.querySelector('.ob-part.ob-midi')"))
            page.click('#gmBtn')
            check('MIDI switch on: the daughterboard is back on the card', page.evaluate("!!document.querySelector('.ob-daughter') && !document.querySelector('.ob-part.ob-midi')"))
            page.click('#powerBtn')
            box = post_box(page, MONO)
            check('POST on the Hercules: SB16 + MPU-401, 33 MHz', 'Monochrome (Hercules)' in box and 'SB16 220h' in box and 'MPU-401 330h' in box and '33 MHz' in box, box)
            page.click('#powerBtn'); time.sleep(0.5)

            # ---- factory configuration
            page.click('.ob-factory')
            check('factory configuration: everything back', page.evaluate("(() => { const c = armdos.box.cfg; return c.ram === 16 && c.mhz === 100 && c.sound === 'sb16' && c.modem && c.cdrom && c.mouse && armdos.monitor === 'vga' && armdos.gm.enabled; })()") and slots(page) == ['io', 'modem', None, 'sb16', None, 'vga', 'ide'], str(slots(page)))
            page.click('#powerBtn')
            box = post_box(page, COLOR)
            check('POST: the factory machine', all(s in box for s in ['100 MHz, RISC', 'Ext. Memory Size : 15360 KB', 'SB16 220h', 'MPU-401 330h', 'Pointing Device  : PS/2', 'ATAPI, 2nd IDE', '3F8,2F8']), box)
            page.click('#powerBtn'); time.sleep(0.3)
            page.click('.ob-close'); time.sleep(0.9)
            check('Put the lid back closes it', page.evaluate("document.getElementById('openbox').hidden"))
            check('no page errors', not errors, '; '.join(errors[:3]))
            ctx.close()

            # ---- a phone: the board scrolls sideways inside its frame, the page does not
            ctx = b.new_context(viewport={'width': 390, 'height': 844}, device_scale_factor=2, is_mobile=True, has_touch=True)
            page = ctx.new_page()
            page.goto(url + '?nosw'); page.wait_for_selector('#openCaseKey')
            page.click('#openCaseKey'); time.sleep(1.3)
            check('phone: no horizontal page scroll', page.evaluate("document.documentElement.scrollWidth <= innerWidth + 1"), page.evaluate("document.documentElement.scrollWidth"))
            check('phone: the board scrolls in its frame', page.evaluate("(() => { const s = document.querySelector('.ob-scroll'); return s.scrollWidth > s.clientWidth; })()"))
            page.locator('.ob-bin').scroll_into_view_if_needed()
            page.tap('.ob-part[data-id="hercules"]')
            check('phone: tap a part to fit it', 'hercules' in slots(page))
            page.tap('#obClk button[data-v="12"]')
            check('phone: the clock buttons work', page.evaluate("armdos.box.cfg.mhz") == 12)
            page.locator('#openbox').scroll_into_view_if_needed()
            shot(page, 'openbox-phone', full_page=False)
            page.locator('.ob-sheet').scroll_into_view_if_needed()
            shot(page, 'openbox-phone-sheet', full_page=False)
            ctx.close()

            # ---- the app layout: CASE in the bar opens a sheet over the screen
            ctx = b.new_context(viewport={'width': 390, 'height': 844}, device_scale_factor=2, is_mobile=True, has_touch=True)
            page = ctx.new_page()
            page.goto(url + '?nosw&app'); page.wait_for_selector('#abCase', state='attached')
            page.tap('#abCase'); time.sleep(1.3)
            check('app: the case opens as a sheet', page.evaluate("(() => { const o = document.getElementById('openbox'); return !o.hidden && getComputedStyle(o).position === 'fixed'; })()"))
            shot(page, 'openbox-app')
            page.tap('.ob-x'); time.sleep(0.9)
            check('app: ✕ closes it', page.evaluate("document.getElementById('openbox').hidden"))
            ctx.close()
            b.close()

            # ---- "Reduce motion" (the OS setting): animations end at once but still end where they
            # would have - the lid was left on, since its lifting off is an animation's final frame
            for bt in (p.chromium, p.firefox):
                b = bt.launch()
                ctx = b.new_context(viewport={'width': 1440, 'height': 2000}, reduced_motion='reduce')
                page = ctx.new_page()
                page.goto(url + '?nosw'); page.wait_for_selector('#openCaseBtn')
                page.click('#openCaseBtn'); time.sleep(0.5)
                lid = page.evaluate("(() => { const l = document.querySelector('.ob-lid').getBoundingClientRect(), s = document.querySelector('.ob-stage').getBoundingClientRect(); return [l.bottom, s.top]; })()")
                check(f'{bt.name}, reduced motion: the lid comes off', lid[0] <= lid[1] + 2, str(lid))
                page.click('.ob-close'); time.sleep(0.9)
                check(f'{bt.name}, reduced motion: the lid goes back on', page.evaluate("document.getElementById('openbox').hidden"))
                ctx.close()
                b.close()
    finally:
        srv.shutdown()
    print('FAILED: %d' % fails if fails else 'all passed')
    sys.exit(1 if fails else 0)

if __name__ == '__main__':
    main()
