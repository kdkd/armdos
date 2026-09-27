// Small helpers shared by the page modules.

export const $ = (id) => document.getElementById(id);
export const hex = (v, n = 8) => (v >>> 0).toString(16).toUpperCase().padStart(n, '0');
export const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

/** Fetch a (possibly gzipped) binary. Servers that already decoded it are fine too. */
export async function fetchBinary(url, onProgress, expect = null) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  let buf;
  if (onProgress && r.body && r.body.getReader) {
    const total = +r.headers.get('content-length') || 0;
    const reader = r.body.getReader(); const parts = []; let got = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      parts.push(value); got += value.length; onProgress(got, total);
    }
    buf = new Uint8Array(got); let o = 0; for (const p of parts) { buf.set(p, o); o += p.length; }
  } else buf = new Uint8Array(await r.arrayBuffer());
  const raw = await maybeGunzip(buf);
  return expect ? verifyBytes(raw, expect, url.split('?')[0]) : raw;
}

export async function maybeGunzip(buf) {
  if (buf.length < 2 || buf[0] !== 0x1F || buf[1] !== 0x8B) return buf;
  if (typeof DecompressionStream === 'undefined') throw new Error('this browser cannot decompress gzip (no DecompressionStream)');
  const stream = new Blob([buf]).stream().pipeThrough(new DecompressionStream('gzip'));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

/** Check downloaded bytes against what the build recorded: the exact length, and the
 *  hex prefix of their SHA-256 (images.json; chunk names). Throws on a mismatch, so a
 *  server's HTML error page served as "200 OK" is a failed download, not disk contents.
 *  (SHA-256 needs a secure context - https, localhost; elsewhere only the length is checked.) */
export async function verifyBytes(buf, { size, sha } = {}, what = 'download') {
  if (size != null && buf.length !== size) throw new Error(`${what}: ${buf.length} bytes, expected ${size}`);
  if (sha && typeof crypto !== 'undefined' && crypto.subtle) {
    const d = new Uint8Array(await crypto.subtle.digest('SHA-256', buf));
    const hex = [...d.subarray(0, Math.ceil(sha.length / 2))].map((b) => b.toString(16).padStart(2, '0')).join('').slice(0, sha.length);
    if (hex !== sha) throw new Error(`${what}: content does not match (sha256 ${hex}, expected ${sha})`);
  }
  return buf;
}

/** Offer bytes / a blob as a file download. */
export function download(data, name, type = 'application/octet-stream') {
  const blob = data instanceof Blob ? data : new Blob([data], { type });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = name; a.rel = 'noopener';
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 30000);
}

export function el(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') e.className = v;
    else if (k === 'text') e.textContent = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (v !== false && v != null) e.setAttribute(k, v === true ? '' : v);
  }
  for (const c of kids) if (c != null) e.append(c);
  return e;
}

export const prefs = {
  _p: null,
  load() { if (!this._p) { try { this._p = JSON.parse(localStorage.getItem('armdos.prefs') || '{}'); } catch { this._p = {}; } } return this._p; },
  get(k, d) { const p = this.load(); return k in p ? p[k] : d; },
  set(k, v) { const p = this.load(); p[k] = v; try { localStorage.setItem('armdos.prefs', JSON.stringify(p)); } catch {} },
};
