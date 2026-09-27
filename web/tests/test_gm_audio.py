#!/usr/bin/env python3
"""Playwright check of the MPU-401 + General MIDI synth on the page (web/js/audio-gm.js,
audio-gm-worklet.js): the sound set is not fetched at power-on or boot, only when a
program first uses the MPU-401; then it loads, the synth runs inside the Sound
Blaster's AudioWorklet and PLAYMIDI's music comes out of the node (AnalyserNode);
the "MIDI" switch turns the MPU-401 off (PLAYMIDI: "MPU-401 not found at 330h.").

Stages its own copy of the site (build/gm-web/site) around the image the PLAYMIDI
test builds (build/midi-test/playmidi.img), so it does not disturb build/site.

usage: python3 web/tests/test_gm_audio.py
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
    img = os.path.join(ROOT, 'build', 'midi-test', 'playmidi.img')
    if not os.path.exists(img):
        subprocess.run(['node', os.path.join(ROOT, 'apps/midi/tests/playmidi.mjs')], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    d = os.path.join(ROOT, 'build', 'gm-web')
    os.makedirs(d, exist_ok=True)
    shutil.copy(os.path.join(ROOT, 'build', 'rom.bin'), d)
    shutil.copy(img, os.path.join(d, 'hd.img'))
    subprocess.run(['node', 'web/tools/build-site.mjs', '--out', 'build/gm-web/site'], cwd=ROOT, check=True,
                   env={**os.environ, 'BUILD': 'build/gm-web'}, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return os.path.join(d, 'site')

def main():
    serve.SITE = stage()
    srv, url = serve.start()
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(args=['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
            page = b.new_page(viewport={'width': 1440, 'height': 1000})
            errors, sf = [], []
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.on('request', lambda r: sf.append(r.url) if 'ARMGS' in r.url else None)
            page.add_init_script(INSTRUMENT)
            page.goto(url + '?nosw')
            page.wait_for_selector('#powerBtn')
            check('the MIDI switch is on the toolbar, on by default', page.evaluate("document.getElementById('gmBtn')?.getAttribute('aria-pressed') === 'true'"))
            page.click('#powerBtn')
            check('boots to C:\\>', wait_for(page, f"{SCREEN}.includes('C:\\\\>')", 40))
            time.sleep(1)
            check('sound set not fetched at power-on or boot', not sf and page.evaluate("armdos.gm.state === 'idle'"), str(sf))
            page.evaluate("""(() => {
                const ctx = armdos.sbAudio.sound.ctx, a = ctx.createAnalyser(); a.fftSize = 2048;
                armdos.sbAudio.node.connect(a);
                const buf = new Float32Array(a.fftSize); window.__peak = 0;
                setInterval(() => { a.getFloatTimeDomainData(buf); for (const v of buf) window.__peak = Math.max(window.__peak, Math.abs(v)); }, 20);
            })()""")
            time.sleep(0.5)
            quiet = page.evaluate('window.__peak')
            page.click('#screen')
            page.evaluate(r"armdos.machine.typeText('PLAYMIDI /L C:\\MIDI\\BACH846\r')")
            check('PLAYMIDI runs', wait_for(page, f"{SCREEN}.includes('ARM-PC MIDI Player')", 20))
            check('the sound set is fetched on first use of the MPU-401', wait_for(page, 'armdos.gm.state !== "idle"', 5) and len(sf) == 1, str(sf))
            t = time.time()
            ok = wait_for(page, "armdos.gm.state === 'ready'", 60)
            check('the sound set loaded', ok, f'{page.evaluate("armdos.gm.state")} after {time.time() - t:.1f} s')
            check('the synth runs in the AudioWorklet', page.evaluate("armdos.gm.moduleReady === true && armdos.gm.local === null"))
            page.evaluate('window.__peak = 0')
            time.sleep(4)
            loud = page.evaluate('window.__peak')
            check('PLAYMIDI\'s music comes out of the audio node', loud > 0.03 and loud > 10 * max(quiet, 1e-4), f'peak {loud:.3f}, before {quiet:.4f}')
            st = page.evaluate('armdos.sbAudio.stats')
            check('the queue kept up (few underruns)', st['underruns'] < 20, str(st))
            load = page.evaluate('armdos.stats && armdos.stats.hostLoad')
            print(f'     emulator host load while playing: {load}')
            page.screenshot(path=os.path.join(ROOT, 'build', 'gm-web', 'playing.png'))
            page.evaluate("armdos.machine.typeText('{ESC}')")
            wait_for(page, f"!{SCREEN}.includes('ARM-PC MIDI Player')", 10)
            page.click('#gmBtn')
            check('MIDI switch off', page.evaluate("document.getElementById('gmBtn').getAttribute('aria-pressed') === 'false' && armdos.machine.mpu.present === false"))
            page.click('#screen')
            page.evaluate(r"armdos.machine.typeText('PLAYMIDI C:\\MIDI\\BACH846\r')")
            check('switched off: no MPU-401', wait_for(page, f"{SCREEN}.includes('MPU-401 not found at 330h.')", 20))
            page.click('#gmBtn')
            check('no page errors', not errors, '; '.join(errors[:3]))
            b.close()
    finally:
        srv.shutdown()
    print('all passed' if not fails else f'{fails} FAILED')
    sys.exit(1 if fails else 0)

main()
