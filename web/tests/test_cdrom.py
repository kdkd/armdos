#!/usr/bin/env python3
"""Playwright check of the CD-ROM drive on the page (web/js/cdrom.js + emu/dev/atapi.mjs):
the drive and the disc box are drawn; clicking the jewel case puts the Multimedia Sampler '93
in the drive; with ARMCD.SYS and ARMCDEX from the factory CONFIG.SYS/AUTOEXEC.BAT, DIR D: lists the
disc; CDPLAY plays track 2, whose Opus is fetched and decoded only then, and the machine's
audio output (the stream the Sound Blaster worklet plays) carries it; the tray button ejects
the disc and DIR D: then says "Not ready reading drive D".

Stages its own copy of the site in build/cd-web/site (so it does not disturb build/site).
Needs build/hd.img with C:\\DOS\\ARMCD.SYS, ARMCDEX.EXE and CDPLAY.EXE, and make cdrom-disc.

usage: python3 web/tests/test_cdrom.py      (make cdrom-web-test)
"""
import os, sys, time, subprocess
sys.path.insert(0, os.path.dirname(__file__))
import serve
from playwright.sync_api import sync_playwright

ROOT = serve.ROOT
OUT = os.path.join(ROOT, 'build', 'cd-web')
fails = 0
def check(name, ok, extra=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + (f'  ({extra})' if extra else ''), flush=True)
    if not ok: fails += 1
    return ok

def wait_for(page, js, timeout=15):
    t = time.time()
    while time.time() - t < timeout:
        if page.evaluate(js): return True
        time.sleep(0.1)
    return False

SCREEN = "(() => { const m = armdos.machine; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m.cpu.m8[0xB8000 + (y*80+x)*2] || 32); r.push(s.trimEnd()); } return r.join('\\n'); })()"
LAST = "(() => { const s = %s.split('\\n'); const y = armdos.machine.cpu.m8[0x451]; return s[y] || ''; })()" % SCREEN
CD = 'armdos.machine.cdrom'

def stage():
    subprocess.run(['node', 'web/tools/build-site.mjs', '--out', 'build/cd-web/site'], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    return os.path.join(OUT, 'site')

def main():
    serve.SITE = stage()
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(args=['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--autoplay-policy=no-user-gesture-required'])
            page = b.new_page(viewport={'width': 1440, 'height': 1000})
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.goto(url + '?nosw')
            page.wait_for_selector('.jewel', timeout=30000)
            check('the CD-ROM drive is in the case', page.locator('#case .bay.cd').count() == 1)
            check('the disc box lists the sampler with CC BY credits',
                  'Kevin MacLeod' in page.inner_text('#cdBox') and 'By Attribution 4.0 License' in page.inner_text('#cdBox')
                  and page.locator('#cdBox a[href="http://creativecommons.org/licenses/by/4.0/"]').count() >= 4)
            check('the footer credits the music', 'incompetech.com' in page.inner_text('#moreCredits'))
            page.locator('#cdBox').screenshot(path=os.path.join(OUT, 'test-cdbox.png'))

            page.click('#powerBtn')
            check('boots to C:\\>', wait_for(page, f"{SCREEN}.includes('C:\\\\>')", 60))
            # the factory CONFIG.SYS loads ARMCD.SYS and AUTOEXEC.BAT runs ARMCDEX (drive D: with no disc yet)
            s = page.evaluate(SCREEN)
            check('the factory boot loads ARMCD.SYS + ARMCDEX: Drive D: = Driver ARMCD001 unit 0',
                  'Device name: ARMCD001' in s and 'Drive D: = Driver ARMCD001 unit 0' in s, s)
            page.click('.jewel')
            check('the disc goes in (click on the jewel case)', wait_for(page, f"{CD}.disc && !{CD}.trayOpen", 5))
            check('the jewel case shows "in the drive"', page.locator('.jewel.in-drive').count() == 1)
            page.locator('#case').screenshot(path=os.path.join(OUT, 'test-case-disc.png'))

            def cmd(line, until=None, timeout=30):
                page.evaluate('(s) => armdos.machine.typeText(s)', line + '\r')   # (Playwright's type() has no Shift for ':')
                wait_for(page, 'armdos.machine.typingDone()', 20)
                time.sleep(0.3)
                if until: return wait_for(page, f"{SCREEN}.includes({until!r})", timeout)
                return wait_for(page, f"/^[A-Z]:\\\\[^>]*>$/.test({LAST}.trim())", timeout)

            cmd('CLS')
            ok = cmd('ARMCDEX /D:ARMCD001', 'ARM CD-ROM Extensions already installed')
            check('ARMCDEX again: already installed', ok, page.evaluate(SCREEN)[:400])
            cmd('CLS')
            ok = cmd('DIR D: /W', 'File(s)', 30)
            s = page.evaluate(SCREEN)
            check('DIR D: lists the disc', ok and 'Volume in drive D is SAMPLER93' in s and 'README   TXT' in s, s[:300])
            check('the busy LED blinked for the data reads', page.evaluate("armdos.cd.reads > 0"), f"{page.evaluate('armdos.cd.reads')} activity callbacks")

            # CDPLAY: 2 plays track 2
            if not os.path.exists(os.path.join(ROOT, 'build', 'CDPLAY.EXE')):
                check('build/CDPLAY.EXE exists', False)
            else:
                page.evaluate("""(() => {
                    window.__cdPeak = 0; window.__cdChunks = 0;
                    const sb = armdos.sbAudio, push = sb.push.bind(sb);
                    sb.push = (l, r) => { let pk = 0; for (let i = 0; i < l.length; i += 7) { const v = Math.abs(l[i]) + Math.abs(r[i]); if (v > pk) pk = v; }
                                          if (pk > window.__cdPeak) window.__cdPeak = pk; window.__cdChunks++; push(l, r); };
                    const ctx = sb.sound.ctx, a = ctx.createAnalyser(); a.fftSize = 2048;
                    if (sb.node) sb.node.connect(a);
                    const buf = new Float32Array(a.fftSize); window.__nodePeak = 0;
                    setInterval(() => { a.getFloatTimeDomainData(buf); for (const v of buf) window.__nodePeak = Math.max(window.__nodePeak, Math.abs(v)); }, 20);
                })()""")
                check('no audio track fetched before play', page.evaluate("armdos.cd.sources.get('sampler93').requests.length === 0"),
                      str(page.evaluate("armdos.cd.sources.get('sampler93').requests")))
                cmd('CLS')
                page.click('#screen')
                page.keyboard.type('CDPLAY')
                page.keyboard.press('Enter')
                check('CDPLAY is on screen', wait_for(page, f"{SCREEN}.includes('ARM-DOS CD Player')", 20), page.evaluate(SCREEN)[:200])
                time.sleep(0.5)
                page.evaluate('window.__cdPeak = 0; window.__nodePeak = 0')
                time.sleep(0.5)
                quiet = page.evaluate('window.__nodePeak')
                page.evaluate("armdos.machine.typeText('2')")
                ok = wait_for(page, f"(() => {{ const s = {CD}.state(); return s.playing && !s.holding && s.track === 2; }})()", 45)
                check('track 2 plays (fetched + decoded on demand)', ok, str(page.evaluate(f"{CD}.state()")))
                check('track 2 was requested by the drive', 2 in page.evaluate("armdos.cd.sources.get('sampler93').requests"),
                      str(page.evaluate("armdos.cd.sources.get('sampler93').requests")) + ' via ' + str(page.evaluate("armdos.cd.sources.get('sampler93').decodedWith")))
                time.sleep(3)
                pk = page.evaluate('window.__cdPeak'); npk = page.evaluate('window.__nodePeak')
                check('the machine audio output carries the CD audio', pk > 0.02, f'chunk peak {pk:.3f}, {page.evaluate("window.__cdChunks")} chunks')
                check('the audio node plays it', npk > 0.01 and npk > 10 * max(quiet, 1e-4), f'node peak {npk:.3f}, before {quiet:.4f}')
                # the front headphone jack: CD audio bypasses the SB16 mixer, the knob sets the level
                page.click('#cdJack')
                page.evaluate('window.__cdPeak = 0')
                time.sleep(1.5)
                hp = page.evaluate('window.__cdPeak')
                check('headphones plugged in: louder, at the knob\'s level', page.evaluate(f"!!{CD}.headphones") and hp > 2 * pk, f'peak {hp:.3f} vs {pk:.3f} through the mixer')
                page.click('#cdJack')
                check('the LED shows play', page.locator('#cdLed.play').count() == 1)
                st = page.evaluate(f"{CD}.state()")
                check('position advances', wait_for(page, f"{CD}.state().lba > {st['lba'] + 75}", 5))
                time.sleep(1)
                page.screenshot(path=os.path.join(OUT, 'test-cdplay-page.png'))
                page.locator('#tube').screenshot(path=os.path.join(OUT, 'test-cdplay.png'))
                page.evaluate("armdos.machine.typeText('{ESC}')")
                check('Esc leaves CDPLAY with the music playing', wait_for(page, f"/^C:\\\\[^>]*>$/.test({LAST}.trim())", 10)
                      and page.evaluate(f"{CD}.state().playing"))

            # the tray button: eject, then DIR D: is not ready
            page.click('#cdEject')
            check('the tray opens (button)', wait_for(page, f"{CD}.trayOpen && document.querySelector('#cdDrive').classList.contains('open')", 3))
            page.locator('#case').screenshot(path=os.path.join(OUT, 'test-case-open.png'))
            cmd('CLS')
            ok = cmd('DIR D:', 'Abort, Retry, Fail?', 20)
            s = page.evaluate(SCREEN)
            check('DIR D: with the tray open -> Not ready reading drive D', ok and 'Not ready reading drive D' in s, s[:300])
            for _ in range(4):          # DIR asks for the label search and again for the directory search
                if 'Abort, Retry, Fail?' not in page.evaluate(LAST): break
                page.evaluate("armdos.machine.typeText('F')")
                wait_for(page, 'armdos.machine.typingDone()', 5)
                time.sleep(0.8)
            check('Fail -> back to the prompt', wait_for(page, f"/^C:\\\\[^>]*>$/.test({LAST}.trim())", 10), page.evaluate(SCREEN)[:500])
            page.click('#cdEject')
            check('the tray closes with the disc', wait_for(page, f"!{CD}.trayOpen && {CD}.disc", 3))
            time.sleep(1.5)
            cmd('CLS')
            ok = cmd('DIR D: /W', 'File(s)', 30)
            check('DIR D: works again after the media change', ok and 'SAMPLER93' in page.evaluate(SCREEN), page.evaluate(SCREEN)[:500])
            check('no page errors', not errors, '; '.join(errors[:3]))
            b.close()
    finally:
        srv.shutdown()
    print('all passed' if not fails else f'{fails} FAILED')
    sys.exit(1 if fails else 0)

main()
