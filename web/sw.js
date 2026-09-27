// ARM-DOS service worker: the app launches offline and the disk images are
// downloaded once per release.
//
//   app shell (html, css, js, emulator, fonts, icons)  cache "armdos-app-<build>", cache-first;
//                                                      a new build = a new sw.js = a new cache, old ones deleted
//   images.json, disks.json                            network-first (never stale), cached copy offline
//   images/*.gz?v=<hash>, images/c/<hash>.gz (C: chunks) cache "armdos-data", cache-first by URL; entries that the
//                                                      current images.json/disks.json no longer name are deleted
//
// A new release installs all or nothing: every shell file must arrive and match the hash
// build-site.mjs recorded for it, or the install fails and the release already here keeps
// running (a later visit tries again, fetching only what is still missing). A complete release
// then waits: it takes over when the page asks as it starts (web/js/main.js), never under a
// running machine, which would mix the files of two releases.
// build-site.mjs fills in BUILD, MODV and SHELL.

const BUILD = '__BUILD__';
const MODV = '__MODV__';
const SHELL = __SHELL__;      // [path, the first hex digits of its SHA-256; null: optional, unchecked]
const SHELL_CACHE = 'armdos-app-' + BUILD;
const DATA_CACHE = 'armdos-data';
const MANIFESTS = ['images.json', 'disks.json'];

const here = (p) => new URL(p, self.registration.scope).href;
// the URLs a shell file is cached under: modules also as they import each other, index.html also as the site root
const urls = (p) => [here(p), ...(/^(js|emu)\/.*\.js$/.test(p) ? [here(p + '?v=' + MODV)] : []), ...(p === 'index.html' ? [here('./')] : [])];
const isApp = (u) => { const s = self.registration.scope; const r = u.startsWith(s) ? u.slice(s.length).split(/[?#]/)[0] : null; return r === '' || r === 'index.html'; };
const hex = (b) => [...new Uint8Array(b)].map((x) => x.toString(16).padStart(2, '0')).join('');

self.addEventListener('install', (e) => {
  e.waitUntil((async () => {
    // (a worker from before this scheme, "armdos-shell-*", never asks a new one to take over: replace it at once, as it did)
    const legacy = (await caches.keys()).some((k) => k.startsWith('armdos-shell-'));
    const c = await caches.open(SHELL_CACHE);
    const bad = [];
    let done = 0;
    const tell = async () => {
      for (const w of await self.clients.matchAll({ type: 'window', includeUncontrolled: true })) w.postMessage({ armdosUpdate: { done, total: SHELL.length } });
    };
    await Promise.all(SHELL.map(async ([p, sha]) => {
      try {
        const ks = urls(p);
        if (!(await Promise.all(ks.map((k) => c.match(k)))).every(Boolean)) {      // (already here from an attempt that failed part way)
          const { buf, type } = await fetchShellFile(p, sha);
          await Promise.all(ks.map((k) => c.put(k, new Response(buf, { headers: type ? { 'Content-Type': type } : {} }))));
        }
      } catch (err) { if (sha) bad.push(`${p}: ${err.message || err}`); }
      if (++done % 10 === 0 || done === SHELL.length) tell().catch(() => {});
    }));
    if (bad.length) throw new Error(`ARM-DOS release ${BUILD} incomplete, ${bad.length} of ${SHELL.length} files missing (${bad.slice(0, 3).join('; ')})`);
    if (legacy) await self.skipWaiting();
  })());
});

/** One shell file, checked against this build's hash (a site half way through an upload fails here). One retry. */
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

// The page, as it starts, asks a waiting release to take over. Not while another ARM-DOS window
// runs the old one: that window would go on loading files from the new release.
self.addEventListener('message', (e) => {
  if (e.data !== 'takeover') return;
  const port = e.ports[0];
  e.waitUntil((async () => {
    // (includeUncontrolled: from a waiting worker, Chrome counts only the windows controlled by that worker, none)
    const wins = (await self.clients.matchAll({ type: 'window', includeUncontrolled: true })).filter((w) => isApp(w.url));
    if (wins.length > 1) { port?.postMessage('busy'); return; }
    await self.skipWaiting();
    port?.postMessage('ok');
  })());
});

self.addEventListener('activate', (e) => {
  e.waitUntil((async () => {
    for (const k of await caches.keys()) if ((k.startsWith('armdos-app-') && k !== SHELL_CACHE) || k.startsWith('armdos-shell-')) await caches.delete(k);
    await self.clients.claim();
    await prune();
  })());
});

/** Drop cached images that the current manifests no longer reference. */
async function prune(fresh) {
  try {
    const want = new Set();
    for (const m of MANIFESTS) {
      const r = fresh?.[m] || await fetch(here(m), { cache: 'no-store' });
      if (!r.ok) return;
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
  const scope = self.registration.scope;
  const inScope = req.url.startsWith(scope);
  const rel = inScope ? req.url.slice(scope.length).split('?')[0] : null;

  if (rel !== null && MANIFESTS.includes(rel)) { e.respondWith(networkFirst(req, rel)); return; }
  if (rel !== null && rel.startsWith('images/')) { e.respondWith(cacheFirst(req, DATA_CACHE)); return; }
  // the page itself (the site root or index.html); other pages in scope (docs/) are ordinary files
  if (req.mode === 'navigate' && (rel === '' || rel === 'index.html')) {
    e.respondWith((async () => (await caches.match(here('./'), { cacheName: SHELL_CACHE })) || (await caches.match(here('index.html'), { cacheName: SHELL_CACHE })) || fetch(req))());
    return;
  }
  e.respondWith(cacheFirst(req, SHELL_CACHE));
});

// (Reads go through caches.match and writes check caches.has: caches.open would re-create this
// release's shell cache after a newer release has deleted it, from a fetch still in flight here.)
async function store(name, key, r) {
  if (name === SHELL_CACHE && !(await caches.has(name))) return;
  await (await caches.open(name)).put(key, r);
}
async function networkFirst(req, rel) {
  try {
    const r = await fetch(req, { cache: 'no-store' });
    if (r.ok) {
      await store(SHELL_CACHE, here(rel), r.clone());
      if (rel === 'images.json') prune({ [rel]: r.clone() });
    }
    return r;
  } catch {
    return (await caches.match(here(rel), { cacheName: SHELL_CACHE })) || Response.error();
  }
}
async function cacheFirst(req, name) {
  const hit = await caches.match(req, { cacheName: name, ignoreVary: true });
  if (hit) return hit;
  const r = await fetch(req);
  if (r.ok && r.status === 200) store(name, req, r.clone()).catch(() => {});
  return r;
}
