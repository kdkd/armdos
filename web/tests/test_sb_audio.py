#!/usr/bin/env python3
"""Playwright check of the Sound Blaster audio path on the page (web/js/audio-sb.js):
no AudioContext / audio node before a user gesture; after the power switch the
AudioWorklet runs and receives the machine's audio; SBTEST.EXE's chime and FM
chord come out of the node (measured with an AnalyserNode).

Stages its own copy of the site (build/sb-web/site) around the image the SBTEST
test builds (build/sbtest-test/sbtest.img: COMMAND.COM, AUTOEXEC.BAT with SET
BLASTER, C:\\DOS\\SBTEST.EXE), so it does not disturb build/site.

usage: python3 web/tests/test_sb_audio.py
"""
import os, sys, time, shutil, subprocess
sys.path.insert(0, os.path.dirname(__file__))
import serve
from playwright.sync_api import sync_playwright

ROOT = serve.ROOT
fails = 0
def check(name, ok, extra=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + (f'  ({extra})' if extra else ''))
    if not ok: fails += 1

def wait_for(page, js, timeout=15):
    t = time.time()
    while time.time() - t < timeout:
        if page.evaluate(js): return True
        time.sleep(0.1)
    return False

SCREEN = "(() => { const m = armdos.machine; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m.cpu.m8[0xB8000 + (y*80+x)*2] || 32); r.push(s.trimEnd()); } return r.join('\\n'); })()"
# count AudioContexts made, before the page's modules run
INSTRUMENT = """
window.__ac = 0;
for (const k of ['AudioContext', 'webkitAudioContext']) {
  const C = window[k]; if (!C) continue;
  window[k] = class extends C { constructor(...a) { super(...a); window.__ac++; } };
}
"""

def stage():
    img = os.path.join(ROOT, 'build', 'sbtest-test', 'sbtest.img')
    if not os.path.exists(img):
        subprocess.run(['node', os.path.join(ROOT, 'apps/sbtest/tests/run.mjs')], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    d = os.path.join(ROOT, 'build', 'sb-web')
    os.makedirs(d, exist_ok=True)
    shutil.copy(os.path.join(ROOT, 'build', 'rom.bin'), d)
    shutil.copy(img, os.path.join(d, 'hd.img'))
    subprocess.run(['node', 'web/tools/build-site.mjs', '--out', 'build/sb-web/site'], cwd=ROOT, check=True,
                   env={**os.environ, 'BUILD': 'build/sb-web'}, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return os.path.join(d, 'site')

def main():
    serve.SITE = stage()
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            # no autoplay override: audio must wait for a real gesture
            b = p.chromium.launch(args=['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
            page = b.new_page(viewport={'width': 1440, 'height': 1000})
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.add_init_script(INSTRUMENT)
            page.goto(url)
            page.wait_for_selector('#powerBtn')
            time.sleep(1.5)
            check('no AudioContext before a user gesture', page.evaluate('window.__ac === 0'))
            check('no Sound Blaster audio node before a user gesture', page.evaluate('armdos.sbAudio && armdos.sbAudio.node === null'))

            page.click('#powerBtn')                                  # the gesture
            check('Sound Blaster AudioWorklet created after the power switch',
                  wait_for(page, "armdos.sbAudio.kind === 'worklet'", 20), page.evaluate('armdos.sbAudio.kind'))
            check('audio context running', wait_for(page, "armdos.sbAudio.sound.ctx && armdos.sbAudio.sound.ctx.state === 'running'", 10),
                  page.evaluate('armdos.sbAudio.sound.ctx && armdos.sbAudio.sound.ctx.state'))
            check('machine audio output started at the device rate',
                  page.evaluate('armdos.machine.audio.enabled && armdos.machine.audio.rate === armdos.sbAudio.sound.ctx.sampleRate'))
            check('worklet receives the machine\'s audio', wait_for(page, 'armdos.sbAudio.stats && armdos.sbAudio.stats.received > 10000', 15),
                  str(page.evaluate('armdos.sbAudio.stats')))
            check('boots to C:\\>', wait_for(page, f"{SCREEN}.includes('C:\\\\>')", 40))
            # listen at the node's output
            page.evaluate("""(() => {
                const ctx = armdos.sbAudio.sound.ctx, a = ctx.createAnalyser(); a.fftSize = 2048;
                armdos.sbAudio.node.connect(a);
                const buf = new Float32Array(a.fftSize); window.__peak = 0;
                setInterval(() => { a.getFloatTimeDomainData(buf); for (const v of buf) window.__peak = Math.max(window.__peak, Math.abs(v)); }, 20);
            })()""")
            time.sleep(0.5)
            quiet = page.evaluate('window.__peak')
            page.click('#screen')
            page.keyboard.type('SBTEST')
            page.keyboard.press('Enter')
            ok = wait_for(page, f"{SCREEN}.includes('Done.')", 40)
            screen = page.evaluate(SCREEN)
            check('SBTEST ran in the page', ok and 'Sound Blaster 16 found at 220h, IRQ 7, DMA 1/5, DSP 4.05' in screen)
            loud = page.evaluate('window.__peak')
            check('the chime and chord came out of the audio node', loud > 0.05 and loud > 10 * max(quiet, 1e-4), f'peak {loud:.3f}, before {quiet:.4f}')
            st = page.evaluate('armdos.sbAudio.stats')
            check('the queue kept up (few underruns)', st['underruns'] < 20, str(st))
            check('no page errors', not errors, '; '.join(errors[:3]))
            b.close()
    finally:
        srv.shutdown()
    print('all passed' if not fails else f'{fails} FAILED')
    sys.exit(1 if fails else 0)

main()
