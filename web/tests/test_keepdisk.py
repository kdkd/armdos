#!/usr/bin/env python3
"""Drive D:, the one that is the user's to keep (web/js/keepdisk.js, disk/d.json):

 1. the first visit makes it: formatted, with D:\\README.TXT, stored in its own database
    (armdos-d); the CD-ROM is E:;
 2. what DOS saves on D: survives a reload;
 3. a new C: (another hard disk image) starts C: over - the page says so - but leaves D: alone;
    so does a new release, and so does a new build of D:'s own factory image;
 4. the Files panel lists D:, and copies files onto it while the machine is off;
 5. Back up D: saves the whole drive (gzipped); Erase D: (asked twice) empties it; Restore D:
    (confirmed) puts the backup back, and each lasts through a reload.

usage: python3 web/tests/test_keepdisk.py   (after ./build.sh; Chromium)
"""
import gzip, json, os, re, shutil, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(__file__))
import serve
from playwright.sync_api import sync_playwright

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
fails = 0
def check(name, ok, extra=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + (f'  ({extra})' if extra else ''), flush=True)
    if not ok: fails += 1
def wait_for(page, js, timeout=15):
    t = time.time()
    while time.time() - t < timeout:
        try:
            if page.evaluate(js): return True
        except Exception: pass
        time.sleep(0.15)
    return False
SCREEN = "(() => { const m = armdos.machine; const r = []; for (let y = 0; y < 25; y++) { let s=''; for (let x = 0; x < 80; x++) s += String.fromCharCode(m.cpu.m8[0xB8000 + (y*80+x)*2] || 32); r.push(s.trimEnd()); } return r.join('\\n'); })()"
PROMPT = f"armdos.machine && armdos.driver?.running && {SCREEN}.trimEnd().endsWith('C:\\\\>')"
D_SECTORS = "new Promise((res) => { const r = indexedDB.open('armdos-d'); r.onsuccess = () => { const db = r.result; if (!db.objectStoreNames.length) return res(0); const c = db.transaction('hdsectors').objectStore('hdsectors').count(); c.onsuccess = () => res(c.result); }; r.onerror = () => res(-1); })"

def boot(pg, url):
    pg.goto(url)
    pg.wait_for_selector('#powerBtn')
    pg.click('#powerBtn')
    return wait_for(pg, PROMPT, 60)
def dos(pg, cmd, want=None, t=20):
    """Type a command; wait until it has run (it shows on the screen, then the prompt is back)."""
    pg.evaluate(f"armdos.machine.typeText({(cmd + chr(13))!r})")
    wait_for(pg, f"{SCREEN}.includes({json.dumps('>' + cmd)})", t)
    ok = wait_for(pg, f"{SCREEN}.includes({json.dumps(want)})", t) if want else True
    wait_for(pg, f"{PROMPT} && !{SCREEN}.trimEnd().endsWith({json.dumps('>' + cmd)})", t)
    return ok
def restarted(pg):
    """The machine was switched off and on by the page: wait for it to go down, then for the prompt."""
    wait_for(pg, f"!({PROMPT})", 10)
    return wait_for(pg, PROMPT, 60)
def off(pg):
    if pg.evaluate("armdos.powered"): pg.click('#powerBtn')
    wait_for(pg, "!armdos.powered", 5)

def main():
    tmp = tempfile.mkdtemp(prefix='armdos-keep-')
    site = os.path.join(tmp, 'site')
    shutil.copytree(serve.SITE, site)
    srv, base = serve.start(site)
    url = base + '?nosw'
    rel = lambda: re.search(r'"r/([0-9a-f]+)/', open(os.path.join(site, 'index.html')).read()).group(1)
    ij = lambda: os.path.join(site, 'r', rel(), 'images.json')
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            ctx = b.new_context(viewport={'width': 1300, 'height': 1000}, accept_downloads=True)
            pg = ctx.new_page()
            errors = []
            pg.on('pageerror', lambda e: errors.append(str(e)))

            # ============ 1. the first visit makes D:
            check('boots with two hard disks', boot(pg, url))
            check('D: is made and stored in its own database', wait_for(pg, f"{D_SECTORS}.then(n => n >= 5)", 10), pg.evaluate(D_SECTORS))
            check('the status line says D: is yours', 'Drive D: is yours' in pg.evaluate("document.getElementById('dStatus').textContent"))
            check('DIR D: - formatted, MY FILES, README.TXT', dos(pg, 'DIR D:', 'Volume in drive D is MY FILES') and 'README   TXT' in pg.evaluate(SCREEN), pg.evaluate(SCREEN)[-400:])
            check('the CD-ROM is E:', dos(pg, 'DIR E:', 'Not ready reading drive E'), pg.evaluate(SCREEN)[-200:])
            pg.evaluate("armdos.machine.typeText('A')"); wait_for(pg, PROMPT, 10)       # (Abort: Fail would only make DIR ask again)

            # ============ 2. saved on D: - and on C: - then a reload
            dos(pg, 'ECHO kept for good> D:\\KEEP.TXT')
            dos(pg, 'ECHO only until a new C:> C:\\CHANGE.TXT')
            check('D: stores the new file', wait_for(pg, f"{D_SECTORS}.then(n => n > 9)", 5), pg.evaluate(D_SECTORS))
            wait_for(pg, "!armdos.store.dirty.size", 5)
            boot(pg, url)
            check('after a reload: D:\\KEEP.TXT', dos(pg, 'TYPE D:\\KEEP.TXT', 'kept for good'))
            check('after a reload: C:\\CHANGE.TXT too', dos(pg, 'TYPE C:\\CHANGE.TXT', 'only until a new C:'))

            # ============ 3. a new release with a new C: and a new build of D:'s factory image
            old, new = rel(), 'f' * 12
            shutil.copytree(os.path.join(site, 'r', old), os.path.join(site, 'r', new))
            idx = os.path.join(site, 'index.html')
            html = open(idx).read().replace(old, new)
            open(idx, 'w').write(html)
            img = json.load(open(ij()))
            img['hd']['sha'] = '0123456789abcdef'              # another C:
            dimg = os.path.join(site, img['d']['file'])        # another factory D: (same bytes, new name) - must not matter
            img['d']['file'] = 'images/d.sectors.rebuilt.gz'
            shutil.copy(dimg, os.path.join(site, img['d']['file']))
            json.dump(img, open(ij(), 'w'))
            boot(pg, url)
            check('the new release is running', pg.evaluate("document.getElementById('releaseId').textContent") == new)
            check('a new C: starts over, and the page says so', dos(pg, 'TYPE C:\\CHANGE.TXT', 'File not found')
                  and 'has been updated to a new version' in pg.evaluate("document.getElementById('hdStatus').textContent"),
                  pg.evaluate("document.getElementById('hdStatus').textContent"))
            check('D: is untouched by all of it', dos(pg, 'TYPE D:\\KEEP.TXT', 'kept for good'), pg.evaluate(SCREEN)[-300:])

            # ============ 4. the Files panel
            off(pg)
            pg.click('.ftab[data-drive="D"]')
            check('Files: D: lists KEEP.TXT and README.TXT', wait_for(pg, "[...document.querySelectorAll('#filesList .fn')].map(e => e.textContent).join(' ').includes('KEEP.TXT')", 5)
                  and 'README.TXT' in pg.evaluate("document.getElementById('filesList').textContent"))
            host = os.path.join(tmp, 'notes.txt'); open(host, 'w').write('dropped in from the host\r\n')
            pg.set_input_files('#filesAdd', host)
            check('Files: a file copied onto D: with the machine off', wait_for(pg, "document.getElementById('filesFoot').textContent.includes('Copied 1 file')", 10), pg.evaluate("document.getElementById('filesFoot').textContent"))
            time.sleep(1)
            check('DOS reads it, after a reload', boot(pg, url) and dos(pg, 'TYPE D:\\NOTES.TXT', 'dropped in from the host'))

            # ============ 5. back up, erase, restore
            with pg.expect_download() as dl:
                pg.click('#dBackup')
            path = os.path.join(tmp, 'backup.img.gz'); dl.value.save_as(path)
            raw = gzip.open(path).read()
            check('Back up D: saves the whole drive, gzipped', dl.value.suggested_filename == 'armdos-d.img.gz' and len(raw) == 263088 * 512 and os.path.getsize(path) < 1_000_000,
                  f'{dl.value.suggested_filename}, {os.path.getsize(path)} bytes')
            open(os.path.join(tmp, 'backup.img'), 'wb').write(raw)
            ls = subprocess.run(['node', os.path.join(ROOT, 'disk', 'mkimage.mjs'), 'ls', os.path.join(tmp, 'backup.img')], capture_output=True, text=True).stdout
            check('the backup holds the files', 'KEEP' in ls and 'NOTES' in ls, ls)
            pg.click('#dErase')
            check('Erase D: asks first', 'Erase everything on D:?' in pg.inner_text('#dErase'))
            pg.click('#dErase')
            check('Erase D:: KEEP.TXT gone, README.TXT back, the machine restarted', restarted(pg) and dos(pg, 'DIR D:', 'File(s)')
                  and 'KEEP' not in pg.evaluate(SCREEN) and 'README' in pg.evaluate(SCREEN), pg.evaluate(SCREEN)[-300:])
            boot(pg, url)
            check('... and it stays erased after a reload', dos(pg, 'DIR D:', 'File(s)') and 'KEEP' not in pg.evaluate(SCREEN))
            pg.set_input_files('#dRestore', path)
            check('Restore D: asks to confirm', wait_for(pg, "document.getElementById('dStatus').textContent.includes('Replace everything on D:')", 5))
            pg.click('#dStatus .danger')
            check('Restore D:: KEEP.TXT is back', restarted(pg) and dos(pg, 'TYPE D:\\KEEP.TXT', 'kept for good'), pg.evaluate(SCREEN)[-300:])
            boot(pg, url)
            check('... and after a reload', dos(pg, 'TYPE D:\\NOTES.TXT', 'dropped in from the host'))
            bad = os.path.join(tmp, 'small.img'); open(bad, 'wb').write(b'\0' * 1474560)
            pg.set_input_files('#dRestore', bad); pg.click('#dStatus .danger')
            check('Restore D: refuses a file that is not a D: backup', wait_for(pg, "document.getElementById('dStatus').textContent.includes('D: is unchanged')", 5)
                  and dos(pg, 'TYPE D:\\KEEP.TXT', 'kept for good'), pg.evaluate("document.getElementById('dStatus').textContent"))
            ctx.close()
            b.close()
            check('no page errors', not errors, '; '.join(errors[:3]))
    finally:
        srv.shutdown()
        shutil.rmtree(tmp, ignore_errors=True)
    print('keep disk (web): all passed' if not fails else f'{fails} failed')
    sys.exit(1 if fails else 0)

main()
