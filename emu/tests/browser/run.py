#!/usr/bin/env python3
"""Headless-Chromium checks for the emulator (needs playwright).
Serves emu/ over http on a free localhost port, then:
  1. bench.html: CPU MIPS in Chromium (JIT and interpreter)
  2. demo.html: boots the device test ROM, types keys, checks screen + graphics
usage: python3 emu/tests/browser/run.py [--shots DIR]
"""
import http.server, json, os, socket, subprocess, sys, threading, time
from functools import partial
from playwright.sync_api import sync_playwright

EMU = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
shots = sys.argv[sys.argv.index('--shots') + 1] if '--shots' in sys.argv else None

class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a): pass
    def guess_type(self, path):
        return 'text/javascript' if path.endswith('.mjs') else super().guess_type(path)

def main():
    # make sure the test ROM and the bench binary exist
    subprocess.run([os.path.join(EMU, 'tests/rom/build.sh')], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['node', os.path.join(EMU, 'tests/machine/perf.mjs')], check=True)
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), partial(Quiet, directory=os.path.dirname(EMU)))   # the repo root: the test binaries are in build/emu-tests
    port = srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{port}'
    fails = 0
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            page = b.new_page()
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            # 1. benchmark
            page.goto(base + '/emu/tests/browser/bench.html')
            page.wait_for_function('window.benchResult', timeout=120000)
            r = page.evaluate('window.benchResult')
            print('chromium bench:', json.dumps(r))
            if not (r['jit']['ok'] and r['interp']['ok']): fails += 1; print('FAIL bench output')
            # 2. demo
            page = b.new_page()
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.on('console', lambda m: m.type == 'error' and errors.append(m.text))
            page.goto(base + '/emu/demo.html')
            screen_has = lambda t: page.evaluate('''t => { const m = window.emu && window.emu.machine; if (!m) return false;
                let s = ''; for (let i = 0; i < 2000; i++) s += String.fromCharCode(m.cpu.m8[0xB8000 + 2 * i]); return s.includes(t); }''', t)
            def wait(pred, secs=20):
                end = time.time() + secs
                while time.time() < end:
                    if pred(): return True
                    time.sleep(0.1)
                return False
            ok = wait(lambda: screen_has('ESC continues'))
            print('demo boot to key prompt:', ok); fails += not ok
            if shots: page.screenshot(path=os.path.join(shots, 'demo-text.png'))
            page.focus('#screen')
            page.keyboard.press('KeyA'); page.keyboard.press('KeyB')
            ok = wait(lambda: screen_has('key 30'))
            print('demo keyboard:', ok); fails += not ok
            page.keyboard.press('Escape')
            ok = wait(lambda: page.evaluate('window.emu.machine.vga.mode') == 0x13)
            print('demo mode 13h:', ok); fails += not ok
            time.sleep(0.5)
            if shots: page.screenshot(path=os.path.join(shots, 'demo-13h.png'))
            px = page.evaluate('''() => { const c = document.getElementById('screen'); const d = c.getContext('2d').getImageData(1, 0, 1, 1).data; return [c.width, c.height, d[0], d[1], d[2]]; }''')
            print('canvas 13h size/pixel(1,0):', px); fails += px != [320, 200, 255, 0, 255]
            time.sleep(1.5)
            stats = page.inner_text('#stats')
            print('demo stats:', stats)
            if errors: print('page errors:', errors); fails += 1
            b.close()
    finally:
        srv.shutdown()
    print('browser tests:', 'FAIL' if fails else 'ok')
    sys.exit(1 if fails else 0)

main()
