// ARM-DOS service worker (sw-<release>.js): the app launches offline and the disk images are
// downloaded once per release.
//
//   index.html (the page)                              network-first (a few seconds), else this release's copy
//   r/<release>/ (css, js, emulator, fonts, icons,     cache "armdos-app-<release>", precached; immutable URLs
//     images.json, disks.json)
//   images/*.<hash>.gz, images/c/<hash>.gz (C: chunks)  cache "armdos-data", cache-first by URL; entries that this
//                                                      release's images.json/disks.json don't name are deleted
//
// Every release lives in its own directory (web/tools/build-site.mjs), so a page loads one release's
// files and nothing else, whatever any cache holds. The worker installs a release all or nothing:
// every shell file must arrive and match the hash listed for it, or the install fails and the
// release already here keeps serving offline (a later visit tries again, fetching only what is still
// missing). A complete release takes over when the page running that same release asks (web/js/main.js),
// and not while another ARM-DOS window is open on an older one.
// build-site.mjs fills in BUILD (the release id) and SHELL.

const BUILD = '__BUILD__';
const SHELL = __SHELL__;      // [path, the first hex digits of its SHA-256; null: optional, unchecked]
const SHELL_CACHE = 'armdos-app-' + BUILD;
const DATA_CACHE = 'armdos-data';
const MANIFESTS = [`r/${BUILD}/images.json`, `r/${BUILD}/disks.json`];

const here = (p) => new URL(p, self.registration.scope).href;
const relOf = (u) => { const s = self.registration.scope; return u.startsWith(s) ? u.slice(s.length).split(/[?#]/)[0] : null; };
const isApp = (u) => { const r = relOf(u); return r === '' || r === 'index.html'; };
const hex = (b) => [...new Uint8Array(b)].map((x) => x.toString(16).padStart(2, '0')).join('');

self.addEventListener('install', (e) => {
  e.waitUntil((async () => {
    // (a worker from before release directories never asks a new one to take over: replace it at once, as it did)
    const legacy = (await caches.keys()).some((k) => k.startsWith('armdos-shell-'));
    const c = await caches.open(SHELL_CACHE);
    const bad = [];
    await Promise.all(SHELL.map(async ([p, sha]) => {
      try {
        if (!(await c.match(here(p)))) {                 // (already here from an attempt that failed part way)
          const { buf, type } = await fetchShellFile(p, sha);
          await c.put(here(p), new Response(buf, { headers: type ? { 'Content-Type': type } : {} }));
        }
      } catch (err) { if (sha) bad.push(`${p}: ${err.message || err}`); }
    }));
    if (bad.length) throw new Error(`ARM-DOS release ${BUILD} incomplete, ${bad.length} of ${SHELL.length} files missing (${bad.slice(0, 3).join('; ')})`);
    // (an install resumed from an earlier attempt ends in moments, before the page's request can
    // arrive: give it a few seconds to - the switch then happens here, the way that works reliably)
    if (!legacy && !takeoverAsked) await Promise.race([asked, new Promise((res) => setTimeout(res, 3000))]);
    if (legacy || (takeoverAsked && await alone())) self.skipWaiting();
  })());
});

/** One shell file, checked against this release's hash (a site half way through an upload fails here). One retry. */
async function fetchShellFile(p, sha) {
  for (let attempt = 0; ; attempt++) {
    try {
      const r = await fetch(here(p), { cache: 'reload' });
      if (!r.ok) throw new Error('HTTP ' + r.status);
      const buf = await r.arrayBuffer();
      const got = sha && hex(await crypto.subtle.digest('SHA-256', buf)).slice(0, sha.length);
      if (sha && got !== sha) throw new Error(`not this release's file (sha256 ${got}, expected ${sha})`);
      return { buf, type: r.headers.get('Content-Type') };
    } catch (err) {
      if (attempt) throw err;
      await new Promise((res) => setTimeout(res, 1000));
    }
  }
}

// A page running this release asks it to take over ({ takeover: <release> }), as soon as it sees
// it: while it is still installing, the switch then happens as the install ends (the browser does
// that reliably; asked only once installed, Chrome sometimes never switched, and held up new page
// loads meanwhile). A page from before release directories asks with 'takeover' once it is
// installed, and reloads itself afterwards. Not while another ARM-DOS window is open: it may be
// running an older release, whose files this one would delete.
let takeoverAsked = false, askedNow;
const asked = new Promise((res) => { askedNow = res; });
// (includeUncontrolled: from a waiting worker, Chrome counts only the windows controlled by that worker, none)
const alone = async () => (await self.clients.matchAll({ type: 'window', includeUncontrolled: true })).filter((w) => isApp(w.url)).length <= 1;
self.addEventListener('message', (e) => {
  const d = e.data, port = e.ports[0];
  if (d !== 'takeover' && !(d && d.takeover === BUILD)) { if (d && d.takeover) port?.postMessage('other release'); return; }
  e.waitUntil((async () => {
    if (!(await alone())) { port?.postMessage('busy'); return; }
    takeoverAsked = true; askedNow();
    port?.postMessage('ok');
    if (self.serviceWorker?.state !== 'installing') self.skipWaiting();     // (installing: the install's end does it)
  })());
});

self.addEventListener('activate', (e) => {
  e.waitUntil((async () => {
    for (const k of await caches.keys()) if ((k.startsWith('armdos-app-') && k !== SHELL_CACHE) || k.startsWith('armdos-shell-')) await caches.delete(k);
    await self.clients.claim();
    await prune();
  })());
});

/** Drop cached images that this release's manifests don't name. */
async function prune() {
  try {
    const want = new Set();
    for (const m of MANIFESTS) {
      const r = await caches.match(here(m), { cacheName: SHELL_CACHE });
      if (!r) return;
      const j = await r.json();
      const walk = (o) => {
        if (!o || typeof o !== 'object') return;
        if (Array.isArray(o.chunks) && typeof o.chunkBase === 'string') for (const h of o.chunks) if (h) want.add(here(o.chunkBase + h + '.gz'));
        for (const [k, v] of Object.entries(o)) { if (k === 'file' && typeof v === 'string') want.add(here(v)); else if (k !== 'chunks') walk(v); }
      };
      walk(j);
    }
    const c = await caches.open(DATA_CACHE);
    for (const req of await c.keys()) if (!want.has(req.url)) await c.delete(req);
  } catch { /* offline: keep everything */ }
}

self.addEventListener('fetch', (e) => {
  const req = e.request;
  if (req.method !== 'GET') return;
  const url = new URL(req.url);
  if (url.origin !== location.origin) return;
  const rel = relOf(req.url);

  if (rel !== null && rel.startsWith('images/')) { e.respondWith(cacheFirst(req)); return; }
  // the page itself (the site root or index.html); other pages in scope (docs/) are ordinary files
  if (req.mode === 'navigate' && (rel === '' || rel === 'index.html')) { e.respondWith(page(req)); return; }
  // this release's files from its cache; anything else (a newer release's, the manual's) is left to the
  // browser: relaying it would keep this worker busy, and the browser switches to a new worker only
  // once the old one has nothing in flight
  if (rel === null || !rel.startsWith(`r/${BUILD}/`)) return;
  e.respondWith((async () => (await caches.match(req, { cacheName: SHELL_CACHE, ignoreSearch: true })) || fetch(req))());
});

/** The page: the network's (so a new release is seen at once), else - offline, or no answer within
 *  4 s - this release's copy. */
async function page(req) {
  const mine = () => caches.match(here('index.html'), { cacheName: SHELL_CACHE });
  try {
    const r = await Promise.race([fetch(req.url, { cache: 'no-cache', credentials: 'same-origin' }),
      new Promise((_, no) => setTimeout(() => no(new Error('no answer')), 4000))]);
    if (r.ok) return r.redirected ? new Response(r.body, r) : r;     // (a followed redirect can't answer a navigation)
    return (await mine()) || r;
  } catch {
    return (await mine()) || Response.error();
  }
}

async function cacheFirst(req) {
  const hit = await caches.match(req, { cacheName: DATA_CACHE, ignoreVary: true });
  if (hit) return hit;
  const r = await fetch(req);
  if (r.ok && r.status === 200) { const copy = r.clone(); caches.open(DATA_CACHE).then((c) => c.put(req, copy)).catch(() => {}); }
  return r;
}
