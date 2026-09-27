// The service's window on the real Internet: fetch with a cache, a polite rate limit per
// host, a timeout, and one retry-free error type. Isomorphic: pass `fetch` (node tests
// pass a fixture fetch that never touches the network).

export class ServiceError extends Error {
  constructor(message, { status = 0, notFound = false } = {}) { super(message); this.status = status; this.notFound = notFound; }
}

const hostOf = (url) => { try { return new URL(url).host; } catch { return ''; } };

export class Fetcher {
  constructor({ fetch = globalThis.fetch?.bind(globalThis), ttlMs = 10 * 60 * 1000, maxEntries = 80, minGapMs = 200, maxParallel = 4, timeoutMs = 20000, now = () => Date.now(),
    hostGaps = { 'hacker-news.firebaseio.com': 20, 'api.open-meteo.com': 100, 'geocoding-api.open-meteo.com': 100 } } = {}) {
    Object.assign(this, { fetchFn: fetch, ttlMs, maxEntries, minGapMs, maxParallel, timeoutMs, now, hostGaps });
    this.cache = new Map();      // key -> { at, promise }
    this.hosts = new Map();      // host -> time of the last request
    this.active = 0;
    this.queue = [];
    this.requests = 0;           // real fetches made (tests, curiosity)
    this.log = [];
  }
  /** GET url as 'json' | 'text' | 'bytes', cached. */
  get(url, as = 'json') {
    const key = as + ' ' + url;
    const hit = this.cache.get(key);
    if (hit && this.now() - hit.at < this.ttlMs) { this.cache.delete(key); this.cache.set(key, hit); return hit.promise; }
    const promise = this.limited(url).then((r) => this.body(r, as, url));
    const entry = { at: this.now(), promise };
    this.cache.set(key, entry);
    promise.catch(() => { if (this.cache.get(key) === entry) this.cache.delete(key); });
    while (this.cache.size > this.maxEntries) this.cache.delete(this.cache.keys().next().value);
    return promise;
  }
  async body(r, as, url) {
    if (!r.ok) throw new ServiceError(`HTTP ${r.status} for ${url}`, { status: r.status, notFound: r.status === 404 });
    try {
      if (as === 'json') return await r.json();
      if (as === 'text') return await r.text();
      return new Uint8Array(await r.arrayBuffer());
    } catch (e) { throw new ServiceError('bad response from ' + url + ': ' + e.message); }
  }
  limited(url) {
    return new Promise((resolve, reject) => { this.queue.push({ url, resolve, reject }); this.pump(); });
  }
  pump() {
    while (this.active < this.maxParallel && this.queue.length) {
      // the first queued request whose host is ready (real time, whatever now() says)
      let pick = -1, soonest = Infinity;
      for (let i = 0; i < this.queue.length; i++) {
        const host = hostOf(this.queue[i].url);
        const wait = (this.hosts.get(host) ?? -1e12) + (this.hostGaps[host] ?? this.minGapMs) - Date.now();
        if (wait <= 0) { pick = i; break; }
        soonest = Math.min(soonest, wait);
      }
      if (pick < 0) { if (!this.timer) this.timer = setTimeout(() => { this.timer = 0; this.pump(); }, soonest); return; }
      const job = this.queue.splice(pick, 1)[0];
      this.hosts.set(hostOf(job.url), Date.now());
      this.active++;
      this.run(job).finally(() => { this.active--; this.pump(); });
    }
  }
  async run({ url, resolve, reject }) {
    this.requests++;
    this.log.push(url); if (this.log.length > 50) this.log.shift();
    const ctl = typeof AbortController !== 'undefined' ? new AbortController() : null;
    const t = setTimeout(() => ctl?.abort(), this.timeoutMs);
    try {
      if (!this.fetchFn) throw new ServiceError('no network');
      resolve(await this.fetchFn(url, { signal: ctl?.signal, credentials: 'omit' }));
    } catch (e) {
      reject(e instanceof ServiceError ? e : new ServiceError('network: ' + (e?.name === 'AbortError' ? 'timed out' : e?.message || e)));
    } finally { clearTimeout(t); }
  }
}
