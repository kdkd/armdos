// Records the fixtures the node tests use: runs the service's channel code against the
// real web services once and saves every response under tests/fixtures/ (index.json maps
// URL -> file). Article HTML is trimmed at a section boundary to keep the files small;
// pictures are stored as gzipped PPM (python3 + Pillow converts them).
//   node apps/online/tests/record.mjs
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { Fetcher } from '../../../emu/online/net.mjs';
import * as ch from '../../../emu/online/channels.mjs';
import { FIXTURES, RECORDED_AT } from './fixtures.mjs';

fs.mkdirSync(FIXTURES, { recursive: true });
const indexFile = path.join(FIXTURES, 'index.json');
const index = {};
const name = (url, ext) => crypto.createHash('sha1').update(url).digest('hex').slice(0, 12) + ext;
const TRIM = 150 * 1024;

async function recFetch(url, opts) {
  const r = await fetch(url, opts);
  const buf = Buffer.from(await r.arrayBuffer());
  const type = r.headers.get('content-type') || '';
  let file, data = buf;
  if (type.startsWith('image/')) {
    const tmp = path.join(FIXTURES, 'tmp.img');
    fs.writeFileSync(tmp, buf);
    const ppm = execFileSync('python3', ['-c', 'import sys; from PIL import Image; im=Image.open(sys.argv[1]).convert("RGBA"); bg=Image.new("RGBA", im.size, "white"); bg.alpha_composite(im); bg.thumbnail((640, 442)); sys.stdout.buffer.write(b"P6 %d %d 255\\n" % bg.size + bg.convert("RGB").tobytes())', tmp], { maxBuffer: 1 << 26 });
    fs.rmSync(tmp);
    file = name(url, '.ppm.gz'); data = zlib.gzipSync(ppm, { level: 9 });
  } else {
    let text = buf.toString('utf8');
    if (url.includes('action=parse')) {
      const j = JSON.parse(text);
      if (j.parse && j.parse.text.length > TRIM) {
        const h = j.parse.text;
        let cut = h.lastIndexOf('<div class="mw-heading mw-heading2"', TRIM);
        if (cut < 20000) cut = TRIM;
        j.parse.text = h.slice(0, cut) + '</div>';
      }
      text = JSON.stringify(j);
    }
    file = name(url, text.length > 20000 ? '.json.gz' : '.json');
    data = file.endsWith('.gz') ? zlib.gzipSync(Buffer.from(text), { level: 9 }) : Buffer.from(text);
  }
  fs.writeFileSync(path.join(FIXTURES, file), data);
  index[url] = { file, status: r.status };
  console.log(r.status, file, url.slice(0, 110));
  return new Response(buf, { status: r.status, headers: r.headers });
}

const net = new Fetcher({ fetch: recFetch, now: () => RECORDED_AT, minGapMs: 300, maxParallel: 2 });
const s = await ch.search(net, 'ARM architecture');
const arm = await ch.article(net, 'ARM architecture family');
await ch.article(net, 'Reduced instruction set computer');
for (const l of arm.links.filter((l) => l && l.kind === 'P').slice(0, 3)) await net.get(l.src, 'bytes');
await ch.weatherSearch(net, 'Berlin');
await ch.define(net, 'modem');
const nd = await ch.news(net);
const first = nd.links.find((l) => l && l.kind === 'N');
await ch.newsItem(net, first.id);
await ch.onThisDay(net, 11, 4);
const rt = await ch.randomTitle(net);
await ch.article(net, rt);
fs.writeFileSync(indexFile, JSON.stringify(index, null, 1) + '\n');
// drop recordings nothing refers to any more
const used = new Set(Object.values(index).map((e) => e.file));
for (const f of fs.readdirSync(FIXTURES)) if (/^[0-9a-f]{12}\./.test(f) && !used.has(f)) fs.unlinkSync(path.join(FIXTURES, f));
console.log('search results', s.links.length - 1, '; random:', rt);
