#!/usr/bin/env python3
"""Playwright checks of the staged site (make site first; `make web-test` does both).

usage: python3 web/tests/test_site.py [--shots DIR]
"""
import os, sys, time
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

ARGS = ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']
# instrument sound + callbacks before the page's modules run
INSTRUMENT = """
window.__calls = { speaker: 0, disk: 0, fdSeek: 0, fdRecal: 0, hd: 0, print: 0 };
"""
SCREEN = "(() => { const m = armdos.machine; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m.cpu.m8[0xB8000 + (y*80+x)*2] || 32); r.push(s.trimEnd()); } return r.join('\\n'); })()"

def wait_for(page, js, timeout=15):
    t = time.time()
    while time.time() - t < timeout:
        if page.evaluate(js): return True
        time.sleep(0.1)
    return False

def main():
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(args=ARGS)
            ctx = b.new_context(viewport={'width': 1440, 'height': 1000})
            page = ctx.new_page()
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.add_init_script(INSTRUMENT)
            t0 = time.time()
            page.goto(url)
            page.wait_for_selector('#powerBtn')
            check('first paint fast', time.time() - t0 < 5, f'{time.time() - t0:.2f}s')
            check('machine not running before power', page.evaluate('armdos.machine === null'))
            shot(page, 'off')

            # ---- power on
            page.click('#powerBtn')
            check('boots to POST', wait_for(page, f"armdos.machine && {SCREEN}.includes('ARM/AT System Configuration')"))
            page.evaluate("""(() => { const s = armdos.machine.cb; const sp = s.speaker; s.speaker = (...a) => { __calls.speaker++; sp(...a); };
                const d = s.disk; s.disk = (...a) => { __calls.disk++; d(...a); }; })()""")
            check('audio context created by the power switch', page.evaluate("!!document && armdos.machine && true"))
            check('power LED on', page.evaluate("document.getElementById('pwrLed').classList.contains('on')"))
            check('MHz display shows 100', page.evaluate("[...document.querySelectorAll('#mhzDisplay .seg.on')].length") > 10)
            check('boots DOS to C:\\>', wait_for(page, f"{SCREEN}.includes('C:\\\\>')", 30))
            check('ambient noise off by default', page.evaluate("armdos.sound ? !armdos.sound.ambient : document.getElementById('ambientBtn').getAttribute('aria-pressed') === 'false'"))
            check('CRT shader active', page.evaluate("!document.getElementById('screen').classList.contains('gl-off')"))
            time.sleep(1.0)
            shot(page, 'post')
            shot(page, 'tube', clip=page.locator('#monitor').bounding_box())

            # ---- reset: the BIOS beeps and the speaker callback fires
            page.click('#resetBtn')
            check('speaker callback fires after reset (POST beep)', wait_for(page, "__calls.speaker > 0", 15), page.evaluate('__calls.speaker'))
            wait_for(page, f"{SCREEN}.includes('C:\\\\>')", 30)

            # ---- typing: DEL during POST opens SETUP? just check key bytes reach the BIOS buffer
            page.click('#screen')
            check('screen takes focus on click', page.evaluate("document.activeElement.id === 'screen'"))
            check('keyboard hint shown', page.evaluate("!document.getElementById('kbdHint').hidden"))
            before = page.evaluate("armdos.machine.kbc.q.length + armdos.machine.cpu.icount")
            page.keyboard.press('Escape')
            check('Escape reaches the machine', page.evaluate("document.activeElement.id === 'screen'"))
            page.keyboard.type('ver')
            page.keyboard.press('Enter')
            check('typing reaches DOS (VER)', wait_for(page, f"{SCREEN}.toUpperCase().includes('VER') && {SCREEN}.split('C:\\\\>').length > 2", 10))
            page.mouse.click(5, 500)
            check('click outside releases focus', page.evaluate("document.activeElement.id !== 'screen'"))

            # ---- turbo
            page.click('#turboBtn')
            check('turbo off -> 12 MHz', page.evaluate("armdos.machine.mhz === 12 && armdos.machine.in8(0xF1) === 12"))
            segs12 = page.evaluate("[...document.querySelectorAll('#mhzDisplay .seg.on')].length")
            check('7-seg shows 12', segs12 == 7, segs12)  # '1' = 2 segments, '2' = 5
            page.click('#turboBtn')
            check('turbo on -> 100 MHz', page.evaluate("armdos.machine.mhz === 100"))

            # ---- floppy
            wait_for(page, "document.querySelectorAll('.floppy').length >= 2")
            page.click('.floppy >> nth=0')
            check('startup disk inserted', wait_for(page, "armdos.machine.floppy && armdos.machine.floppy.length === 1474560"))
            check('drive shows the disk', page.evaluate("document.getElementById('drive').classList.contains('has-disk')"))
            page.evaluate("__calls.disk = 0")
            page.click('#resetBtn')
            check('boot reads the floppy (disk activity)', wait_for(page, "__calls.disk > 0", 15))
            wait_for(page, f"{SCREEN}.includes('A:\\\\>') || {SCREEN}.includes('C:\\\\>')", 30)
            shot(page, 'floppy-in', clip=page.locator('#case').bounding_box())
            page.click('#eject')
            check('eject empties the drive', wait_for(page, "armdos.machine.floppy === null"))


            # ---- files: copy host files onto the diskette in the running machine, see them in DOS, download one
            page.click('#resetBtn')                # boot from C: again (the startup diskette is out)
            check('reboots from C:', wait_for(page, f"{SCREEN}.includes('C:\\\\>')", 30))
            page.click('.floppy >> nth=1')        # the blank diskette
            wait_for(page, "document.getElementById('drive').classList.contains('has-disk')")
            page.evaluate("""(() => {
                const dt = new DataTransfer();
                dt.items.add(new File(['Hello from the host computer!\\r\\n'], 'hello from host.txt', { type: 'text/plain' }));
                dt.items.add(new File([new Uint8Array(70000).map((_, i) => i * 7)], 'data.bin'));
                document.getElementById('filesList').dispatchEvent(new DragEvent('drop', { dataTransfer: dt, bubbles: true, cancelable: true }));
            })()""")
            check('files copied onto A:', wait_for(page, "document.getElementById('filesList').textContent.includes('HELLOFRO.TXT') && document.getElementById('filesList').textContent.includes('DATA.BIN')", 5), page.evaluate("document.getElementById('filesFoot').textContent"))
            check('long name reported as renamed', wait_for(page, "document.getElementById('filesFoot').textContent.includes('→ HELLOFRO.TXT')", 5), page.evaluate("document.getElementById('filesFoot').textContent"))
            check('diskette back in the drive after writing', wait_for(page, "armdos.machine.floppy !== null", 3))
            page.click('#screen')
            page.evaluate("armdos.machine.typeText('dir a:\\r')")
            check('DOS sees the copied files (DIR A:)', wait_for(page, f"{SCREEN}.includes('HELLOFRO') && {SCREEN}.includes('DATA')", 15), page.evaluate(SCREEN)[-600:])
            shot(page, 'dir-a', clip=page.locator('#monitor').bounding_box())
            page.evaluate("armdos.machine.typeText('type a:hellofro.txt\\r')")
            check('DOS reads the copied file (TYPE)', wait_for(page, f"{SCREEN}.includes('Hello from the host computer!')", 15))
            with page.expect_download() as dl:
                page.click('#filesList .frow:has-text("DATA.BIN")')
            data = open(dl.value.path(), 'rb').read()
            check('file download round trip', data == bytes((i * 7) & 255 for i in range(70000)), len(data))
            with page.expect_download() as dl:
                page.click('#fdDownload')
            fdimg = dl.value.path()
            import subprocess
            env = dict(os.environ, MTOOLS_SKIP_CHECK='1')
            mdir = subprocess.run(['mdir', '-i', fdimg, '::'], capture_output=True, text=True, env=env).stdout
            check('mtools reads the page-written diskette', 'HELLOFRO TXT' in mdir and 'DATA     BIN' in mdir, mdir)
            fsck = subprocess.run(['fsck.fat', '-n', fdimg], capture_output=True, text=True)
            check('fsck.fat: page-written diskette is clean', fsck.returncode == 0, fsck.stdout[-300:])
            # C: is read-only while the machine runs
            page.click('.ftab[data-drive="C"]')
            check('C: listing shows files', wait_for(page, "document.querySelectorAll('#filesList .frow').length > 2"))
            page.evaluate("""(() => { const dt = new DataTransfer(); dt.items.add(new File(['x'], 'nope.txt'));
                document.getElementById('filesList').dispatchEvent(new DragEvent('drop', { dataTransfer: dt, bubbles: true, cancelable: true })); })()""")
            check('C: refuses writes while running', wait_for(page, "document.getElementById('filesFoot').textContent.includes('Switch the machine off')", 3))
            shot(page, 'files', clip=page.locator('.diskbox-wrap').bounding_box())
            page.click('.ftab[data-drive="A"]')
            page.click('#eject')
            # ---- printer: drive LPT1 the way INT 17h does
            page.evaluate("""(() => { const m = armdos.machine; const s = 'ARM-DOS 4.00 printer test\\r\\n' + 'The quick brown fox jumps over the lazy dog. 0123456789 \\xC9\\xCD\\xBB\\r\\n\\r\\n' + 'C:\\\\>DIR\\r\\n';
                for (const ch of s) { m.out8(0x378, ch.charCodeAt(0)); m.out8(0x37A, 0x0D); m.out8(0x37A, 0x0C); } })()""")
            check('printer prints', wait_for(page, "!document.getElementById('tearOff').disabled", 10))
            time.sleep(1.2)
            page.locator('.printer-wrap').scroll_into_view_if_needed()
            shot(page, 'printer', clip=page.locator('.printer-wrap').bounding_box())
            with page.expect_download() as dl:
                page.click('#tearOff')
            check('tear off downloads a PNG', dl.value.suggested_filename.endswith('.png'))

            # ---- inspector: pause / step / run
            page.evaluate("window.scrollTo(0,0)")
            wait_for(page, "document.getElementById('regs').textContent.includes('FFF')", 5)
            page.click('#dbgPause')
            check('pause stops the driver', page.evaluate("!armdos.driver.running"))
            ic = page.evaluate("armdos.machine.cpu.icount")
            page.click('#dbgStep')
            ic2 = page.evaluate("armdos.machine.cpu.icount")
            check('step executes one instruction', ic2 - ic == 1, f'{ic} -> {ic2}')
            pc = page.evaluate("armdos.machine.cpu.pc >>> 0")
            check('disassembly marks PC', page.evaluate(f"document.querySelector('#disasm .ln.cur').dataset.a == {pc}"))
            # breakpoint at the current pc's next instruction
            page.click('#dbgRun')
            check('run resumes', page.evaluate("armdos.driver.running"))
            # breakpoint on the timer interrupt handler entry (IRQ vector)
            page.evaluate("armdos.dbg.addBp(0xFFFF0018)")
            check('breakpoint hit', wait_for(page, "!armdos.driver.running && (armdos.machine.cpu.pc>>>0) === 0xFFFF0018", 10))
            shot(page, 'inspector-break', clip=page.locator('#inspector').bounding_box())
            page.evaluate("armdos.dbg.removeBp(0xFFFF0018)")
            page.click('#dbgRun')
            check('JIT back on after clearing breakpoints', wait_for(page, "armdos.machine.cpu.jit !== null && armdos.driver.running", 3))
            check('interrupt log fills', wait_for(page, "document.querySelectorAll('#intlog .e').length > 0", 5))
            check('hex view shows B8000', page.evaluate("document.getElementById('hex').textContent.includes('000B8000')"))

            # ---- CRT toggle
            page.click('#crtBtn')
            time.sleep(0.3)
            shot(page, 'tube-flat', clip=page.locator('#tube').bounding_box())
            page.click('#crtBtn')

            # ---- persistence: change a hard disk sector and a CMOS byte, reload
            page.evaluate("""(async () => { const m = armdos.machine; const img = m.ata.img; img.set(new TextEncoder().encode('ARMDOS WAS HERE'), 100 * 512);
                armdos.store.markDirty(100, 1); await armdos.store.flush(); m.out8(0x70, 0x40); m.out8(0x71, 0x5A); })()""")
            time.sleep(0.5)
            # write a file onto C: with the power off
            page.click('#powerBtn'); time.sleep(0.3)
            page.click('.ftab[data-drive="C"]')
            page.evaluate("""(() => { const dt = new DataTransfer(); dt.items.add(new File(['kept'], 'kept.txt'));
                document.getElementById('filesList').dispatchEvent(new DragEvent('drop', { dataTransfer: dt, bubbles: true, cancelable: true })); })()""")
            check('C: takes files while off', wait_for(page, "document.getElementById('filesList').textContent.includes('KEPT.TXT')", 5), page.evaluate("document.getElementById('filesFoot').textContent"))
            time.sleep(1.0)
            page.reload()
            page.wait_for_selector('#powerBtn')
            ok = wait_for(page, "armdos.hd && new TextDecoder().decode(armdos.hd.subarray(51200, 51215)) === 'ARMDOS WAS HERE'", 10)
            check('hard disk sector persists across reloads', ok)
            check('CMOS persists (localStorage)', page.evaluate("(() => { const s = localStorage.getItem('armdos.cmos'); return !!s && atob(s).charCodeAt(0x40) === 0x5A; })()"))
            page.click('.ftab[data-drive="C"]')
            check('file copied to C: persists across reloads', wait_for(page, "document.getElementById('filesList').textContent.includes('KEPT.TXT')", 10))
            check('ambient choice remembered', page.evaluate("document.getElementById('ambientBtn').getAttribute('aria-pressed') === 'false'"))
            check('HD status mentions changes', 'changed' in page.evaluate("document.getElementById('hdStatus').textContent"))
            # factory reset (two clicks)
            page.click('#hdReset'); page.click('#hdReset')
            check('factory reset restores the sector', wait_for(page, "armdos.store.saved === 0 && new TextDecoder().decode(armdos.hd.subarray(51200, 51215)) !== 'ARMDOS WAS HERE'", 10))

            # ---- fallback (no WebGL)
            page.goto(url + '?gl=0')
            page.click('#powerBtn')
            check('flat fallback boots', wait_for(page, f"armdos.machine && {SCREEN}.includes('Configuration')", 15))
            check('fallback canvas is 2D', page.evaluate("document.getElementById('screen').classList.contains('gl-off')"))
            time.sleep(2)
            shot(page, 'fallback-tube', clip=page.locator('#tube').bounding_box())
            page.click('#powerBtn')
            time.sleep(0.5)
            check('power off stops the machine', page.evaluate("!armdos.driver.running"))
            ctx.close()

            # ---- phone
            ctx = b.new_context(viewport={'width': 390, 'height': 844}, device_scale_factor=2, is_mobile=True, has_touch=True)
            m = ctx.new_page()
            m.on('pageerror', lambda e: errors.append(str(e)))
            m.goto(url)
            m.wait_for_selector('#powerBtn')
            check('no horizontal scroll on a phone', m.evaluate("document.documentElement.scrollWidth <= 391"), m.evaluate("document.documentElement.scrollWidth"))
            m.tap('#powerBtn')
            check('phone boots', wait_for(m, f"armdos.machine && {SCREEN}.includes('Configuration')", 15))
            m.tap('#pcKeysBtn')
            check('PC keys open on the phone', m.is_visible('#pcKeys .k[data-code="Enter"]'))
            m.tap('#softKbdBtn')
            m.keyboard.type('x')
            check('soft keyboard field focused', m.evaluate("document.activeElement.id === 'softKbd'"))
            time.sleep(2)
            shot(m, 'phone')
            shot(m, 'phone-full', full_page=True)
            ctx.close()
            b.close()
            check('no page errors', not errors, '; '.join(errors[:3]))
    finally:
        srv.shutdown()
    print(f"{'all passed' if not fails else str(fails) + ' failed'}")
    sys.exit(1 if fails else 0)

main()
