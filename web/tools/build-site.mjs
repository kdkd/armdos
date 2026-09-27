#!/usr/bin/env node
// Stages the static web site into build/site (see web/README.md).
//
//   node web/tools/build-site.mjs [--out build/site]
//
// * copies web/{index.html,css,js,assets,fonts} as they are, and web/docs (the manual) to docs/
// * copies the emulator (emu/*.mjs, emu/dev/*.mjs) to site/emu/*.js, rewriting
//   the relative imports, so no web server needs to know the .mjs MIME type
// * gzips rom.bin / hd.img / floppy images into site/images/*.gz (the page
//   inflates them with DecompressionStream) and writes site/images.json with
//   sizes and content hashes (the hash keys the hard disk's saved sectors)
// * turns web/disks.json into site/disks.json (entries whose image does not
//   exist yet are kept, marked "missing", so the disk box shows them as blanks)
import { readFileSync, writeFileSync, mkdirSync, readdirSync, statSync, existsSync, rmSync, copyFileSync } from 'node:fs';
import { join, dirname, basename, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { gzipSync } from 'node:zlib';
import { createHash } from 'node:crypto';
import { makeIcons } from './icons.mjs';

const WEB = dirname(dirname(fileURLToPath(import.meta.url)));
const ROOT = dirname(WEB);
const args = process.argv.slice(2);
const arg = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const OUT = resolve(ROOT, arg('--out', 'build/site'));
const BUILD = resolve(ROOT, process.env.BUILD || 'build');

rmSync(OUT, { recursive: true, force: true });
mkdirSync(OUT, { recursive: true });

function copyTree(src, dst, filter = () => true) {
  if (!existsSync(src)) return;
  for (const name of readdirSync(src)) {
    const s = join(src, name), d = join(dst, name);
    if (name.startsWith('.')) continue;
    if (statSync(s).isDirectory()) { copyTree(s, d, filter); continue; }
    if (!filter(s)) continue;
    mkdirSync(dirname(d), { recursive: true });
    copyFileSync(s, d);
  }
}
const sha = (buf) => createHash('sha256').update(buf).digest('hex').slice(0, 16);

// ---- page
copyFileSync(join(WEB, 'index.html'), join(OUT, 'index.html'));
for (const dir of ['css', 'js', 'assets', 'fonts']) copyTree(join(WEB, dir), join(OUT, dir));
// the manual (web/docs, plain HTML), linked from the page's header
copyTree(join(WEB, 'docs'), join(OUT, 'docs'));
copyFileSync(join(WEB, 'manifest.webmanifest'), join(OUT, 'manifest.webmanifest'));
makeIcons(join(OUT, 'icons'), readFileSync(join(ROOT, 'emu', 'fonts', 'vga8x16.bin')));

// ---- module version: every relative module import (and the BBS worker's URL) gets
// ?v=<hash of the emulator + page sources>, so a page and a Web Worker can never mix modules of
// two releases out of a heuristic HTTP cache or an old service worker (docs/MODEM.md: the BBS
// worker once ran last release's modem.js while the page was new). The service worker's shell
// list carries the same URLs, so offline start still works.
const MODV = (() => {
  const h = createHash('sha256');
  for (const d of [join(ROOT, 'emu'), join(ROOT, 'emu', 'dev'), join(ROOT, 'emu', 'online'), join(WEB, 'js')])
    for (const n of readdirSync(d).sort()) if (/\.(mjs|js)$/.test(n)) h.update(n).update(readFileSync(join(d, n)));
  return h.digest('hex').slice(0, 10);
})();
const versionImports = (code) => code
  .replace(/((?:\bfrom|\bimport)\s*\(?\s*['"])(\.{1,2}\/[^'"?]+\.js)(['"])/g, `$1$2?v=${MODV}$3`)
  .replace(/(new URL\(\s*['"])(\.{1,2}\/[^'"?]+\.js)(['"])/g, `$1$2?v=${MODV}$3`);
for (const n of readdirSync(join(OUT, 'js'))) if (n.endsWith('.js')) { const f = join(OUT, 'js', n); writeFileSync(f, versionImports(readFileSync(f, 'utf8'))); }

// ---- emulator modules (.mjs -> .js)
const emuOut = join(OUT, 'emu');
function copyModules(src, dst) {
  mkdirSync(dst, { recursive: true });
  for (const name of readdirSync(src)) {
    if (!name.endsWith('.mjs') || name === 'headless.mjs' || name === 'testkit.mjs') continue;
    const code = readFileSync(join(src, name), 'utf8')
      .replace(/(from\s+['"]\.{1,2}\/[^'"]+)\.mjs(['"])/g, '$1.js$2')
      .replace(/(import\(\s*['"]\.{1,2}\/[^'"]+)\.mjs(['"])/g, '$1.js$2');
    writeFileSync(join(dst, name.replace(/\.mjs$/, '.js')), versionImports(code));
  }
}
copyModules(join(ROOT, 'emu'), emuOut);
copyModules(join(ROOT, 'emu', 'dev'), join(emuOut, 'dev'));
copyModules(join(ROOT, 'emu', 'online'), join(emuOut, 'online'));   // ARM-DOS Online (555-0199), apps/online
copyFileSync(join(ROOT, 'emu', 'dev', 'OPL3-LICENSE.txt'), join(emuOut, 'dev', 'OPL3-LICENSE.txt'));   // LGPL-2.1: dev/opl3.js (Nuked OPL3)
mkdirSync(join(emuOut, 'fonts'), { recursive: true });
for (const f of ['vga8x16.bin', 'cga8x8.bin', 'mda9x14.bin', 'README.md']) copyFileSync(join(ROOT, 'emu', 'fonts', f), join(emuOut, 'fonts', f));

// ---- images
const imgDir = join(OUT, 'images');
mkdirSync(imgDir, { recursive: true });
function stageImage(path) {
  const raw = readFileSync(path);
  const name = basename(path) + '.gz';
  writeFileSync(join(imgDir, name), gzipSync(raw, { level: 9 }));
  const h = sha(raw);
  // the hash in the URL: a new image is a new URL, so no browser or proxy cache can serve a stale disk
  return { file: 'images/' + name + '?v=' + h, size: raw.length, sha: h };
}
// The hard disk is streamed: fixed 256 KB chunks of the raw image, each gzipped and named by
// its own content hash (images/c/<hash>.gz), so a chunk that doesn't change between releases
// keeps its URL and stays cached. All-zero chunks are not stored at all (null in the index).
export const CHUNK = 256 * 1024;
function stageChunked(path) {
  const raw = readFileSync(path);
  const dir = join(imgDir, 'c');
  mkdirSync(dir, { recursive: true });
  const chunks = []; let stored = 0, zipped = 0;
  for (let off = 0; off < raw.length; off += CHUNK) {
    const part = raw.subarray(off, Math.min(raw.length, off + CHUNK));
    if (part.every((b) => b === 0)) { chunks.push(null); continue; }
    const h = createHash('sha256').update(part).digest('hex').slice(0, 20);
    const f = join(dir, h + '.gz');
    if (!existsSync(f)) writeFileSync(f, gzipSync(part, { level: 9 }));
    zipped += statSync(f).size; stored++;
    chunks.push(h);
  }
  console.log(`build-site: ${relative(ROOT, path)}: ${chunks.length} chunks of ${CHUNK / 1024} KB, ${stored} stored, ${(zipped / 1048576).toFixed(1)} MB gzipped`);
  return { size: raw.length, sha: sha(raw), chunkSize: CHUNK, chunkBase: 'images/c/', chunks, zipped };
}
const need = (p) => { if (!existsSync(p)) { console.error(`build-site: missing ${relative(ROOT, p)} (run make first)`); process.exit(1); } return p; };
const images = {
  rom: stageImage(need(join(BUILD, 'rom.bin'))),
  hd: stageChunked(need(join(BUILD, 'hd.img'))),
  built: new Date().toISOString(),
};
// the ARM Pit BBS's hard disk (docs/MODEM.md; built by apps/bbs), booted in a worker on demand
if (existsSync(join(BUILD, 'bbs.img'))) images.bbs = stageImage(join(BUILD, 'bbs.img'));
// the General MIDI sound set (apps/midi/sf, GeneralUser GS derived; emu/dev/gmsynth.mjs): fetched by
// web/js/audio-gm.js only when a program first uses the MPU-401
if (existsSync(join(ROOT, '3rdparty/midi/ARMGS.SFA'))) {
  images.gm = stageImage(join(ROOT, '3rdparty/midi/ARMGS.SFA'));
  copyFileSync(join(ROOT, 'apps/midi/sf/GeneralUser-GS-LICENSE.txt'), join(imgDir, 'ARMGS-LICENSE.txt'));
}
// CD-ROMs (apps/cdrom, `make cdrom-disc`; web/js/cdrom.js): disc.json with the data track
// gzipped and the audio as Opus + MP3 (no WAVs), under images/cd/<id>/ so the service worker
// caches them when first used and never precaches them
images.cdroms = [];
for (const id of ['sampler93']) {
  const dir = join(BUILD, 'cdrom', id);
  if (!existsSync(join(dir, 'disc.json'))) { console.error(`build-site: note: ${relative(ROOT, dir)}/disc.json missing (make cdrom-disc); no CD-ROMs`); continue; }
  const disc = JSON.parse(readFileSync(join(dir, 'disc.json'), 'utf8'));
  const out = join(imgDir, 'cd', disc.id), rel = `images/cd/${disc.id}/`;
  mkdirSync(out, { recursive: true });
  for (const t of disc.tracks) {
    if (t.type === 'data') {
      const raw = readFileSync(join(dir, t.file)), h = sha(raw);
      writeFileSync(join(out, t.file + '.gz'), gzipSync(raw, { level: 9 }));
      t.data = { file: rel + t.file + '.gz?v=' + h, size: raw.length, sha: h };
      delete t.file;
    } else {
      t.audio = {};
      for (const k of ['opus', 'mp3']) {
        if (!t.files?.[k] || !existsSync(join(dir, t.files[k]))) continue;
        const raw = readFileSync(join(dir, t.files[k]));
        copyFileSync(join(dir, t.files[k]), join(out, t.files[k]));
        t.audio[k] = { file: rel + t.files[k] + '?v=' + sha(raw), size: raw.length };
      }
      delete t.files;
    }
  }
  images.cdroms.push(disc);
}
writeFileSync(join(OUT, 'images.json'), JSON.stringify(images, null, 1) + '\n');

// ---- the disk box
const box = JSON.parse(readFileSync(join(WEB, 'disks.json'), 'utf8'));
const staged = new Map();
for (const d of box.disks) {
  const p = d.file ? join(ROOT, d.file) : null;
  if (p && existsSync(p)) {
    if (!staged.has(p)) staged.set(p, stageImage(p));
    Object.assign(d, staged.get(p));
  } else {
    if (d.file) console.error(`build-site: note: ${d.file} does not exist yet; "${d.label}" stays an empty sleeve`);
    d.missing = true; delete d.file;
  }
}
writeFileSync(join(OUT, 'disks.json'), JSON.stringify(box, null, 1) + '\n');

// ---- the service worker: the app shell list (each file with its hash, which the worker checks as it
// installs) and a build id that changes whenever any of it does
{
  const shell = [];
  (function walk(d, pre) {
    for (const n of readdirSync(d).sort()) {
      const p = join(d, n), rel = pre + n;
      if (statSync(p).isDirectory()) { if (rel !== 'images' && rel !== 'docs') walk(p, rel + '/'); continue; }
      if (['images.json', 'disks.json', 'sw.js'].includes(rel) || rel.endsWith('.md') || rel.endsWith('.txt')) continue;
      shell.push([rel, createHash('sha256').update(readFileSync(p)).digest('hex').slice(0, 16)]);
    }
  })(OUT, '');
  const tmpl = readFileSync(join(WEB, 'sw.js'), 'utf8');
  const build = createHash('sha256').update(tmpl).update(MODV).update(JSON.stringify(shell)).digest('hex').slice(0, 12);
  const sw = tmpl.replace("'__BUILD__'", JSON.stringify(build))
    .replace("'__MODV__'", JSON.stringify(MODV)).replace('__SHELL__', JSON.stringify(shell));
  writeFileSync(join(OUT, 'sw.js'), sw);
}

let total = 0;
(function du(d) { for (const n of readdirSync(d)) { const p = join(d, n); const s = statSync(p); if (s.isDirectory()) du(p); else total += s.size; } })(OUT);
console.log(`build-site: ${relative(ROOT, OUT)} staged, ${(total / 1024).toFixed(0)} KB`);
